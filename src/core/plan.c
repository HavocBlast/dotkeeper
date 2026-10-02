#include "plan.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "../platform/fs.h"
#include "../platform/paths.h"

const char *dk_act_name(dk_act_kind kind)
{
    switch (kind) {
    case DK_ACT_REMOVE:
        return "remove";
    case DK_ACT_FORGET:
        return "forget";
    case DK_ACT_BACKUP:
        return "backup";
    case DK_ACT_CREATE:
        return "link";
    case DK_ACT_ADOPT:
        return "adopt";
    }
    return "?";
}

const char *dk_health_name(dk_health h)
{
    switch (h) {
    case DK_HEALTH_OK:
        return "ok";
    case DK_HEALTH_MISSING:
        return "missing";
    case DK_HEALTH_REPLACED:
        return "replaced";
    case DK_HEALTH_BLOCKED:
        return "blocked";
    case DK_HEALTH_WRONG:
        return "wrong";
    }
    return "?";
}

/* ---- wanted links ---- */

typedef struct {
    const char *root;
    const char *app;
    dk_link **links;
    const char ***owners; /* which app wants each link, for error messages */
    size_t *n, *cap;
    dk_err *err;
} want_ctx;

static int want_file(const char *rel, const char *abs, void *user)
{
    want_ctx *c = user;
    char *target = dk_path_join(c->root, rel);
    for (size_t i = 0; i < *c->n; i++) {
        if (strcmp((*c->links)[i].target, target) == 0) {
            dk_err_set(c->err, "apps \"%s\" and \"%s\" both provide %s", (*c->owners)[i],
                       c->app, target);
            free(target);
            return -1;
        }
    }
    size_t cap = *c->cap;
    dk_link l = {target, xstrdup(abs)};
    DK_PUSH(*c->links, *c->n, *c->cap, l);
    if (*c->cap != cap)
        *c->owners = xrealloc(*c->owners, *c->cap * sizeof **c->owners);
    (*c->owners)[*c->n - 1] = c->app;
    return 0;
}

int dk_wanted_links(const dk_choice *choices, size_t nchoices, dk_os os, dk_link **out,
                    size_t *nout, dk_err *err)
{
    dk_link *links = NULL;
    const char **owners = NULL;
    size_t n = 0, cap = 0;
    for (size_t i = 0; i < nchoices; i++) {
        if (!choices[i].variant)
            continue;
        want_ctx c = {dk_app_root(choices[i].app, os), choices[i].app->name, &links, &owners,
                      &n, &cap, err};
        if (dk_walk(choices[i].variant->dir, want_file, &c, err) != 0) {
            dk_links_free(links, n);
            free(owners);
            return -1;
        }
    }
    free(owners);
    *out = links;
    *nout = n;
    return 0;
}

void dk_links_free(dk_link *links, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        free(links[i].target);
        free(links[i].source);
    }
    free(links);
}

/* ---- planning ---- */

static void add_act(dk_plan *p, dk_act_kind kind, const char *path, const char *other)
{
    dk_action a = {kind, xstrdup(path), other ? xstrdup(other) : NULL};
    DK_PUSH(p->acts, p->nacts, p->cacts, a);
}

static void add_conflict(dk_plan *p, const char *path, char *reason)
{
    dk_conflict c = {xstrdup(path), reason};
    DK_PUSH(p->conflicts, p->nconflicts, p->cconflicts, c);
}

static const dk_link *find_link(const dk_link *links, size_t n, const char *target)
{
    for (size_t i = 0; i < n; i++)
        if (strcmp(links[i].target, target) == 0)
            return &links[i];
    return NULL;
}

/* realpath() of the closest existing ancestor of path. */
static char *real_ancestor(const char *path)
{
    char *dir = dk_dirname(path);
    for (;;) {
        char buf[PATH_MAX];
        if (realpath(dir, buf)) {
            free(dir);
            return xstrdup(buf);
        }
        if (strcmp(dir, "/") == 0 || strcmp(dir, ".") == 0) {
            free(dir);
            return NULL;
        }
        char *up = dk_dirname(dir);
        free(dir);
        dir = up;
    }
}

static int is_within(const char *path, const char *dir)
{
    size_t n = strlen(dir);
    return strncmp(path, dir, n) == 0 && (path[n] == '/' || path[n] == '\0');
}

/* Where a file at target is moved to by --backup: the same path under
 * backup_dir, relative to $HOME when it lives there. */
static char *backup_path(const char *backup_dir, const char *target)
{
    const char *home = dk_home();
    const char *rel = is_within(target, home) ? target + strlen(home) : target;
    return dk_path_join(backup_dir, rel);
}

int dk_plan_build(const dk_link *want, size_t nwant, const dk_state *state,
                  const dk_plan_opts *opts, dk_plan *out, dk_err *err)
{
    memset(out, 0, sizeof *out);

    char repo_real[PATH_MAX];
    if (!realpath(opts->repo_root, repo_real))
        return dk_err_set(err, "cannot resolve repo path %s", opts->repo_root);

    /* First undo recorded links that are no longer wanted as they are. */
    for (size_t i = 0; i < state->nlinks; i++) {
        const dk_link *have = &state->links[i];
        const dk_link *w = find_link(want, nwant, have->target);
        if (w && str_eq(w->source, have->source))
            continue;
        char *rl = dk_readlink(have->target);
        if (rl && str_eq(rl, have->source))
            add_act(out, DK_ACT_REMOVE, have->target, have->source);
        else
            add_act(out, DK_ACT_FORGET, have->target, NULL); /* gone or replaced */
        free(rl);
    }

    for (size_t i = 0; i < nwant; i++) {
        const dk_link *w = &want[i];
        const dk_link *have = dk_state_find_link(state, w->target);

        char *anc = real_ancestor(w->target);
        int inside = anc && is_within(anc, repo_real);
        free(anc);
        if (inside) {
            add_conflict(out, w->target,
                         xasprintf("its directory resolves into the repo itself "
                                   "(an old directory symlink?)"));
            continue;
        }

        dk_fs_kind kind = dk_fs_kind_of(w->target);
        if (kind == DK_FS_MISSING) {
            add_act(out, DK_ACT_CREATE, w->target, w->source);
            continue;
        }
        char *reason = NULL;
        if (kind == DK_FS_SYMLINK) {
            char *rl = dk_readlink(w->target);
            if (rl && str_eq(rl, w->source)) {
                if (!have || !str_eq(have->source, w->source))
                    add_act(out, DK_ACT_ADOPT, w->target, w->source);
            } else if (have && rl && str_eq(rl, have->source)) {
                /* Our old link; the first loop removes it. */
                add_act(out, DK_ACT_CREATE, w->target, w->source);
            } else {
                reason = xasprintf("is a symlink to %s that Dotkeeper did not create",
                                   rl ? rl : "?");
            }
            free(rl);
        } else {
            reason = xasprintf(have ? "was a Dotkeeper link but is now a real %s"
                                    : "a %s already exists here",
                               kind == DK_FS_DIR ? "directory" : "file");
        }
        if (!reason)
            continue;
        if (opts->backup_dir) {
            char *dest = backup_path(opts->backup_dir, w->target);
            add_act(out, DK_ACT_BACKUP, w->target, dest);
            add_act(out, DK_ACT_CREATE, w->target, w->source);
            free(dest);
            free(reason);
        } else {
            add_conflict(out, w->target, reason);
        }
    }
    return 0;
}

int dk_plan_selection(const dk_repo *repo, const dk_state *state, dk_os os,
                      const char *skip_app, const char *backup_dir, dk_plan *out,
                      dk_err *err)
{
    dk_choice *choices = NULL;
    size_t nchoices = 0;
    dk_link *want = NULL;
    size_t nwant = 0;
    memset(out, 0, sizeof *out);
    if (dk_resolve(repo, &state->sel, os, &choices, &nchoices, err) != 0)
        return -1;
    for (size_t i = 0; skip_app && i < nchoices; i++)
        if (strcmp(skip_app, "*") == 0 || strcmp(skip_app, choices[i].app->name) == 0)
            choices[i].variant = NULL;
    int rc = dk_wanted_links(choices, nchoices, os, &want, &nwant, err);
    if (rc == 0) {
        dk_plan_opts opts = {repo->root, backup_dir};
        rc = dk_plan_build(want, nwant, state, &opts, out, err);
    }
    dk_links_free(want, nwant);
    free(choices);
    return rc;
}

int dk_plan_apply(const dk_plan *plan, dk_state *state, dk_err *err)
{
    for (size_t i = 0; i < plan->nacts; i++) {
        const dk_action *a = &plan->acts[i];
        switch (a->kind) {
        case DK_ACT_REMOVE: {
            /* Re-check right before deleting: never remove what is not ours. */
            char *rl = dk_readlink(a->path);
            int ours = rl && str_eq(rl, a->other);
            free(rl);
            if (!ours)
                return dk_err_set(err, "%s changed since the plan was made; not removing it",
                                  a->path);
            if (dk_unlink(a->path, err) != 0)
                return -1;
            dk_state_remove_link(state, a->path);
            break;
        }
        case DK_ACT_FORGET:
            dk_state_remove_link(state, a->path);
            break;
        case DK_ACT_BACKUP:
            if (dk_fs_kind_of(a->other) != DK_FS_MISSING)
                return dk_err_set(err, "backup destination %s already exists", a->other);
            if (dk_move(a->path, a->other, err) != 0)
                return -1;
            dk_state_remove_link(state, a->path);
            break;
        case DK_ACT_CREATE:
            if (dk_symlink(a->other, a->path, err) != 0)
                return -1;
            dk_state_add_link(state, a->path, a->other);
            break;
        case DK_ACT_ADOPT:
            dk_state_add_link(state, a->path, a->other);
            break;
        }
    }
    return 0;
}

void dk_plan_free(dk_plan *plan)
{
    for (size_t i = 0; i < plan->nacts; i++) {
        free(plan->acts[i].path);
        free(plan->acts[i].other);
    }
    free(plan->acts);
    for (size_t i = 0; i < plan->nconflicts; i++) {
        free(plan->conflicts[i].path);
        free(plan->conflicts[i].reason);
    }
    free(plan->conflicts);
    memset(plan, 0, sizeof *plan);
}

dk_health dk_link_health(const dk_link *want, const dk_state *state)
{
    dk_fs_kind kind = dk_fs_kind_of(want->target);
    if (kind == DK_FS_MISSING)
        return DK_HEALTH_MISSING;
    if (kind == DK_FS_SYMLINK) {
        char *rl = dk_readlink(want->target);
        int ok = rl && str_eq(rl, want->source);
        free(rl);
        return ok ? DK_HEALTH_OK : DK_HEALTH_WRONG;
    }
    return dk_state_find_link(state, want->target) ? DK_HEALTH_REPLACED : DK_HEALTH_BLOCKED;
}
