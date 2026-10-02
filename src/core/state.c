#include "state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ini.h"

#include "../platform/fs.h"
#include "../platform/paths.h"

char *dk_state_path(void)
{
    char *dir = dk_state_dir();
    char *path = dk_path_join(dir, "state.ini");
    free(dir);
    return path;
}

char *dk_new_backup_dir(void)
{
    char stamp[32];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
    char *state = dk_state_dir();
    char *base = dk_path_join(state, "backups");
    char *dir = dk_path_join(base, stamp);
    free(state);
    free(base);
    return dir;
}

typedef struct {
    dk_state *state;
    char *target, *source; /* fields of the [link] section being read */
    const char *path;
    dk_err *err;
    int failed;
} state_ctx;

/* Each link is its own [link] section; commit it when the next starts. */
static int flush_link(state_ctx *c)
{
    if (!c->target && !c->source)
        return 1;
    if (!c->target || !c->source) {
        dk_err_set(c->err, "%s: a [link] section is missing its target or source", c->path);
        c->failed = 1;
        return 0;
    }
    dk_state_add_link(c->state, c->target, c->source);
    free(c->target);
    free(c->source);
    c->target = c->source = NULL;
    return 1;
}

static int state_handler(void *user, const char *section, const char *name,
                         const char *value)
{
    state_ctx *c = user;
    if (!name)
        return flush_link(c);
    if (strcmp(section, "selection") == 0 && strcmp(name, "preset") == 0) {
        dk_selection_set_preset(&c->state->sel, value);
    } else if (strcmp(section, "overrides") == 0) {
        dk_selection_set_override(&c->state->sel, name, value);
    } else if (strcmp(section, "link") == 0 && strcmp(name, "target") == 0) {
        free(c->target);
        c->target = xstrdup(value);
    } else if (strcmp(section, "link") == 0 && strcmp(name, "source") == 0) {
        free(c->source);
        c->source = xstrdup(value);
    }
    /* Unknown keys are ignored so older versions can read newer state. */
    return 1;
}

int dk_state_load(const char *path, dk_state *out, dk_err *err)
{
    memset(out, 0, sizeof *out);
    if (dk_fs_kind_of(path) == DK_FS_MISSING)
        return 0;
    state_ctx c = {out, NULL, NULL, path, err, 0};
    int rc = ini_parse(path, state_handler, &c);
    if (rc == 0 && !c.failed)
        flush_link(&c);
    free(c.target);
    free(c.source);
    if (c.failed || rc != 0) {
        if (!c.failed)
            dk_err_set(err, rc < 0 ? "cannot read %s" : "%s: syntax error", path);
        dk_state_free(out);
        return -1;
    }
    return 0;
}

/* Values are written raw, so they must fit on one line. */
static int check_value(const char *v, dk_err *err)
{
    if (strchr(v, '\n') || strchr(v, '\r'))
        return dk_err_set(err, "cannot record a path containing a newline");
    return 0;
}

int dk_state_save(const char *path, const dk_state *state, dk_err *err)
{
    char *buf = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&buf, &len);
    if (!f)
        return dk_err_set(err, "cannot allocate state buffer");

    fputs("# Written by dotkeeper. Edit with care.\n\n[selection]\n", f);
    if (state->sel.preset)
        fprintf(f, "preset = %s\n", state->sel.preset);
    fputs("\n[overrides]\n", f);
    for (size_t i = 0; i < state->sel.noverrides; i++)
        fprintf(f, "%s = %s\n", state->sel.overrides[i].app, state->sel.overrides[i].variant);
    int rc = 0;
    for (size_t i = 0; i < state->nlinks && rc == 0; i++) {
        const dk_link *l = &state->links[i];
        rc = check_value(l->target, err) ? -1 : check_value(l->source, err);
        fprintf(f, "\n[link]\ntarget = %s\nsource = %s\n", l->target, l->source);
    }
    fclose(f);
    if (rc == 0)
        rc = dk_write_file(path, buf, len, err);
    free(buf);
    return rc;
}

void dk_state_free(dk_state *state)
{
    dk_selection_free(&state->sel);
    for (size_t i = 0; i < state->nlinks; i++) {
        free(state->links[i].target);
        free(state->links[i].source);
    }
    free(state->links);
    memset(state, 0, sizeof *state);
}

const dk_link *dk_state_find_link(const dk_state *state, const char *target)
{
    for (size_t i = 0; i < state->nlinks; i++)
        if (strcmp(state->links[i].target, target) == 0)
            return &state->links[i];
    return NULL;
}

void dk_state_add_link(dk_state *state, const char *target, const char *source)
{
    for (size_t i = 0; i < state->nlinks; i++) {
        if (strcmp(state->links[i].target, target) == 0) {
            free(state->links[i].source);
            state->links[i].source = xstrdup(source);
            return;
        }
    }
    dk_link l = {xstrdup(target), xstrdup(source)};
    DK_PUSH(state->links, state->nlinks, state->clinks, l);
}

void dk_state_remove_link(dk_state *state, const char *target)
{
    for (size_t i = 0; i < state->nlinks; i++) {
        if (strcmp(state->links[i].target, target) == 0) {
            free(state->links[i].target);
            free(state->links[i].source);
            /* Keep order stable so state.ini diffs stay readable. */
            memmove(&state->links[i], &state->links[i + 1],
                    (state->nlinks - i - 1) * sizeof *state->links);
            state->nlinks--;
            return;
        }
    }
}
