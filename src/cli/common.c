#include "cli.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../core/config.h"
#include "../platform/fs.h"
#include "../platform/paths.h"

void dk_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("dotkeeper: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void dk_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("dotkeeper: warning: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

char *prompt_repo_path(void)
{
    const char *def = "~/.dotfiles";
    char line[4096];
    fprintf(stderr, "Where is your dotfile repo? [%s] ", def);
    fflush(stderr);
    if (!fgets(line, sizeof line, stdin))
        return NULL;
    line[strcspn(line, "\r\n")] = '\0';
    char *s = line;
    while (*s == ' ' || *s == '\t')
        s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = '\0';
    return xstrdup(*s ? s : def);
}

int ctx_open(const dk_opts *opts, dk_ctx *ctx)
{
    dk_err err;
    memset(ctx, 0, sizeof *ctx);
    ctx->os = dk_os_current();

    char *raw = NULL;
    if (opts->repo) {
        raw = xstrdup(opts->repo);
    } else if (dk_config_load(&raw, &err) != 0) {
        dk_error("%s", err.msg);
        return -1;
    }
    if (!raw) {
        if (!isatty(STDIN_FILENO)) {
            dk_error("no dotfile repo is set; run `dotkeeper init <path>` first");
            return -1;
        }
        fprintf(stderr, "No dotfile repo is set up yet.\n");
        dk_opts init_opts = *opts;
        init_opts.nargs = 0;
        if (cmd_init(&init_opts) != DK_EXIT_OK)
            return -1;
        if (dk_config_load(&raw, &err) != 0 || !raw) {
            dk_error("could not read the repo location back from the config");
            return -1;
        }
    }
    ctx->repo_path = dk_abspath(raw, &err);
    free(raw);
    if (!ctx->repo_path) {
        dk_error("%s", err.msg);
        return -1;
    }
    if (dk_repo_load(ctx->repo_path, &ctx->repo, &err) != 0) {
        dk_error("%s", err.msg);
        ctx_close(ctx);
        return -1;
    }
    for (size_t i = 0; i < ctx->repo.warnings.len; i++)
        dk_warn("%s", ctx->repo.warnings.items[i]);
    ctx->state_path = dk_state_path();
    if (dk_state_load(ctx->state_path, &ctx->state, &err) != 0) {
        dk_error("%s", err.msg);
        ctx_close(ctx);
        return -1;
    }
    return 0;
}

void ctx_close(dk_ctx *ctx)
{
    dk_repo_free(&ctx->repo);
    dk_state_free(&ctx->state);
    free(ctx->repo_path);
    free(ctx->state_path);
    memset(ctx, 0, sizeof *ctx);
}

static char *new_backup_dir(void)
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

static void print_action(const dk_action *a)
{
    char *path = dk_tildify(a->path);
    char *other = a->other ? dk_tildify(a->other) : NULL;
    switch (a->kind) {
    case DK_ACT_CREATE:
    case DK_ACT_ADOPT:
        printf("  %-7s %s -> %s\n", dk_act_name(a->kind), path, other);
        break;
    case DK_ACT_BACKUP:
        printf("  %-7s %s => %s\n", dk_act_name(a->kind), path, other);
        break;
    case DK_ACT_REMOVE:
    case DK_ACT_FORGET:
        printf("  %-7s %s\n", dk_act_name(a->kind), path);
        break;
    }
    free(path);
    free(other);
}

int sync_links(dk_ctx *ctx, const dk_opts *opts, const char *skip_app)
{
    dk_err err;
    dk_choice *choices = NULL;
    size_t nchoices = 0;
    dk_link *want = NULL;
    size_t nwant = 0;
    dk_plan plan = {0};
    char *backup_dir = NULL;
    int code = DK_EXIT_ERROR;

    if (dk_resolve(&ctx->repo, &ctx->state.sel, ctx->os, &choices, &nchoices, &err) != 0) {
        dk_error("%s", err.msg);
        goto out;
    }
    for (size_t i = 0; skip_app && i < nchoices; i++)
        if (strcmp(skip_app, "*") == 0 || strcmp(skip_app, choices[i].app->name) == 0)
            choices[i].variant = NULL;
    if (dk_wanted_links(choices, nchoices, ctx->os, &want, &nwant, &err) != 0) {
        dk_error("%s", err.msg);
        goto out;
    }
    if (opts->backup)
        backup_dir = new_backup_dir();
    dk_plan_opts popts = {ctx->repo_path, backup_dir};
    if (dk_plan_build(want, nwant, &ctx->state, &popts, &plan, &err) != 0) {
        dk_error("%s", err.msg);
        goto out;
    }

    if (plan.nconflicts) {
        dk_error("%zu conflict%s, nothing was changed:", plan.nconflicts,
                 plan.nconflicts == 1 ? "" : "s");
        for (size_t i = 0; i < plan.nconflicts; i++) {
            char *p = dk_tildify(plan.conflicts[i].path);
            fprintf(stderr, "  %s: %s\n", p, plan.conflicts[i].reason);
            free(p);
        }
        if (!opts->backup)
            fprintf(stderr, "Re-run with --backup to move files in the way aside first.\n");
        goto out;
    }

    if (plan.nacts == 0) {
        printf("Everything is up to date.\n");
        code = DK_EXIT_OK;
        if (!opts->dry_run && dk_state_save(ctx->state_path, &ctx->state, &err) != 0) {
            dk_error("%s", err.msg);
            code = DK_EXIT_ERROR;
        }
        goto out;
    }

    if (opts->dry_run || opts->verbose) {
        printf(opts->dry_run ? "Would do:\n" : "Doing:\n");
        for (size_t i = 0; i < plan.nacts; i++)
            print_action(&plan.acts[i]);
    }
    if (opts->dry_run) {
        code = DK_EXIT_OK;
        goto out;
    }

    int rc = dk_plan_apply(&plan, &ctx->state, &err);
    if (rc != 0)
        dk_error("%s", err.msg);
    /* Save even after a failure: the record must match what was done. */
    dk_err serr;
    if (dk_state_save(ctx->state_path, &ctx->state, &serr) != 0) {
        dk_error("%s", serr.msg);
        rc = -1;
    }
    if (rc == 0) {
        size_t counts[DK_ACT_ADOPT + 1] = {0};
        for (size_t i = 0; i < plan.nacts; i++)
            counts[plan.acts[i].kind]++;
        printf("Done: %zu linked, %zu removed", counts[DK_ACT_CREATE],
               counts[DK_ACT_REMOVE]);
        if (counts[DK_ACT_BACKUP]) {
            char *b = dk_tildify(backup_dir);
            printf(", %zu backed up to %s", counts[DK_ACT_BACKUP], b);
            free(b);
        }
        printf(".\n");
        code = DK_EXIT_OK;
    }
out:
    dk_plan_free(&plan);
    dk_links_free(want, nwant);
    free(choices);
    free(backup_dir);
    return code;
}
