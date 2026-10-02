#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../core/config.h"
#include "../platform/fs.h"
#include "../platform/paths.h"

/* Rejects flags a command does not take, e.g. `status --reset`. */
static int no_flags(const dk_opts *opts, const char *cmd)
{
    for (int i = 0; i < opts->nargs; i++) {
        if (opts->args[i][0] == '-' && opts->args[i][1] != '\0') {
            dk_error("%s: unknown option %s", cmd, opts->args[i]);
            return -1;
        }
    }
    return 0;
}

/* ---- init ---- */

int cmd_init(const dk_opts *opts)
{
    if (no_flags(opts, "init") != 0)
        return DK_EXIT_USAGE;
    if (opts->nargs > 1) {
        dk_error("usage: dotkeeper init [path]");
        return DK_EXIT_USAGE;
    }
    char *raw = NULL;
    if (opts->nargs == 1)
        raw = xstrdup(opts->args[0]);
    else if (opts->repo)
        raw = xstrdup(opts->repo);
    else if (isatty(STDIN_FILENO))
        raw = prompt_repo_path();
    if (!raw) {
        dk_error("no repo path given; use `dotkeeper init <path>`");
        return DK_EXIT_USAGE;
    }

    dk_err err;
    char *path = dk_abspath(raw, &err);
    free(raw);
    if (!path) {
        dk_error("%s", err.msg);
        return DK_EXIT_ERROR;
    }
    int code = DK_EXIT_ERROR;
    dk_fs_kind kind = dk_fs_kind_of(path);
    if (kind != DK_FS_MISSING && kind != DK_FS_DIR) {
        dk_error("%s exists and is not a directory", path);
    } else if (dk_repo_create(path, &err) != 0 || dk_config_save(path, &err) != 0) {
        dk_error("%s", err.msg);
    } else {
        char *shown = dk_tildify(path);
        printf("Dotfile repo: %s\n", shown);
        printf("Add an app by creating %s/apps/<app>/default/ with files laid out as in $HOME.\n",
               shown);
        free(shown);
        code = DK_EXIT_OK;
    }
    free(path);
    return code;
}

/* ---- status ---- */

static void print_app_links(const dk_ctx *ctx, const dk_choice *c, int verbose)
{
    dk_err err;
    dk_link *want = NULL;
    size_t n = 0;
    if (!c->variant) {
        printf("no \"%s\" variant\n", DK_DEFAULT_VARIANT);
        return;
    }
    if (dk_wanted_links(c, 1, ctx->os, &want, &n, &err) != 0) {
        printf("error: %s\n", err.msg);
        return;
    }
    size_t counts[DK_HEALTH_WRONG + 1] = {0};
    for (size_t i = 0; i < n; i++)
        counts[dk_link_health(&want[i], &ctx->state)]++;
    if (n == 0)
        printf("no files");
    int first = 1;
    for (int h = DK_HEALTH_OK; h <= DK_HEALTH_WRONG; h++) {
        if (!counts[h])
            continue;
        printf("%s%zu %s", first ? "" : ", ", counts[h], dk_health_name((dk_health)h));
        first = 0;
    }
    printf("\n");
    if (verbose) {
        for (size_t i = 0; i < n; i++) {
            char *t = dk_tildify(want[i].target);
            printf("    %-8s %s\n", dk_health_name(dk_link_health(&want[i], &ctx->state)), t);
            free(t);
        }
    }
    dk_links_free(want, n);
}

int cmd_status(const dk_opts *opts)
{
    if (no_flags(opts, "status") != 0 || opts->nargs != 0) {
        dk_error("usage: dotkeeper status");
        return DK_EXIT_USAGE;
    }
    dk_ctx ctx;
    if (ctx_open(opts, &ctx) != 0)
        return DK_EXIT_ERROR;

    char *repo = dk_tildify(ctx.repo_path);
    printf("Repo:    %s\n", repo);
    printf("Preset:  %s\n", ctx.state.sel.preset ? ctx.state.sel.preset : "(none)");
    printf("System:  %s\n\n", dk_os_name(ctx.os));
    free(repo);

    dk_err err;
    dk_choice *choices = NULL;
    size_t n = 0;
    int code = DK_EXIT_OK;
    if (dk_resolve(&ctx.repo, &ctx.state.sel, ctx.os, &choices, &n, &err) != 0) {
        dk_error("%s", err.msg);
        code = DK_EXIT_ERROR;
    } else if (n == 0) {
        printf("No apps in the repo for %s yet.\n", dk_os_name(ctx.os));
    } else {
        int wa = 3, wv = 7;
        for (size_t i = 0; i < n; i++) {
            int la = (int)strlen(choices[i].app->name);
            int lv = choices[i].variant ? (int)strlen(choices[i].variant->name) : 1;
            wa = la > wa ? la : wa;
            wv = lv > wv ? lv : wv;
        }
        printf("%-*s  %-*s  %-8s  %s\n", wa, "APP", wv, "VARIANT", "FROM", "LINKS");
        for (size_t i = 0; i < n; i++) {
            const dk_choice *c = &choices[i];
            printf("%-*s  %-*s  %-8s  ", wa, c->app->name, wv,
                   c->variant ? c->variant->name : "-", dk_origin_name(c->origin));
            print_app_links(&ctx, c, opts->verbose);
        }
    }
    free(choices);
    ctx_close(&ctx);
    return code;
}

/* ---- list ---- */

int cmd_list(const dk_opts *opts)
{
    const char *what = opts->nargs > 0 ? opts->args[0] : "apps";
    if (no_flags(opts, "list") != 0)
        return DK_EXIT_USAGE;
    int want_args = strcmp(what, "variants") == 0 ? 2 : (opts->nargs ? 1 : 0);
    if (opts->nargs != want_args ||
        (strcmp(what, "apps") && strcmp(what, "presets") && strcmp(what, "variants"))) {
        dk_error("usage: dotkeeper list [apps | presets | variants <app>]");
        return DK_EXIT_USAGE;
    }
    dk_ctx ctx;
    if (ctx_open(opts, &ctx) != 0)
        return DK_EXIT_ERROR;
    int code = DK_EXIT_OK;
    if (strcmp(what, "apps") == 0) {
        for (size_t i = 0; i < ctx.repo.napps; i++) {
            const dk_app *a = &ctx.repo.apps[i];
            printf("%s:", a->name);
            for (size_t j = 0; j < a->nvariants; j++)
                printf(" %s", a->variants[j].name);
            if (a->os_mask != DK_OS_ALL)
                printf("  (%s only)", dk_os_name((dk_os)a->os_mask));
            printf("\n");
        }
    } else if (strcmp(what, "presets") == 0) {
        for (size_t i = 0; i < ctx.repo.npresets; i++) {
            const dk_preset *p = &ctx.repo.presets[i];
            int active = str_eq(p->name, ctx.state.sel.preset);
            printf("%c %s%s%s\n", active ? '*' : ' ', p->name, p->description ? "  " : "",
                   p->description ? p->description : "");
        }
    } else {
        const dk_app *a = dk_repo_find_app(&ctx.repo, opts->args[1]);
        if (!a) {
            dk_error("no app \"%s\" in the repo", opts->args[1]);
            code = DK_EXIT_ERROR;
        } else {
            dk_choice *choices = NULL;
            size_t n = 0;
            dk_err err;
            const dk_variant *active = NULL;
            if (dk_resolve(&ctx.repo, &ctx.state.sel, ctx.os, &choices, &n, &err) == 0)
                for (size_t i = 0; i < n; i++)
                    if (choices[i].app == a)
                        active = choices[i].variant;
            free(choices);
            for (size_t i = 0; i < a->nvariants; i++)
                printf("%c %s\n", &a->variants[i] == active ? '*' : ' ', a->variants[i].name);
        }
    }
    ctx_close(&ctx);
    return code;
}

/* ---- use ---- */

int cmd_use(const dk_opts *opts)
{
    int reset = opts->nargs == 2 && strcmp(opts->args[1], "--reset") == 0;
    if (opts->nargs != 2 || (!reset && opts->args[1][0] == '-') || opts->args[0][0] == '-') {
        dk_error("usage: dotkeeper use <app> <variant>  |  dotkeeper use <app> --reset");
        return DK_EXIT_USAGE;
    }
    dk_ctx ctx;
    if (ctx_open(opts, &ctx) != 0)
        return DK_EXIT_ERROR;
    int code = DK_EXIT_ERROR;
    const char *app_name = opts->args[0];
    const dk_app *app = dk_repo_find_app(&ctx.repo, app_name);
    if (!app) {
        dk_error("no app \"%s\" in the repo", app_name);
    } else if (!(app->os_mask & (unsigned)ctx.os)) {
        dk_error("app \"%s\" is not enabled on %s", app_name, dk_os_name(ctx.os));
    } else if (reset) {
        if (!dk_selection_clear_override(&ctx.state.sel, app_name))
            printf("%s has no override.\n", app_name);
        code = sync_links(&ctx, opts, NULL);
    } else if (!dk_app_find_variant(app, opts->args[1])) {
        dk_error("app \"%s\" has no variant \"%s\"", app_name, opts->args[1]);
    } else {
        dk_selection_set_override(&ctx.state.sel, app_name, opts->args[1]);
        code = sync_links(&ctx, opts, NULL);
    }
    ctx_close(&ctx);
    return code;
}

/* ---- preset ---- */

static int preset_save(dk_ctx *ctx, const char *name)
{
    dk_err err;
    if (!dk_name_valid(name)) {
        dk_error("\"%s\" is not a valid preset name", name);
        return DK_EXIT_USAGE;
    }
    char *dir = dk_path_join(ctx->repo_path, "presets");
    char *file = xasprintf("%s.ini", name);
    char *path = dk_path_join(dir, file);
    free(dir);
    free(file);
    int code = DK_EXIT_ERROR;
    dk_choice *choices = NULL;
    size_t n = 0;
    if (dk_fs_kind_of(path) != DK_FS_MISSING) {
        dk_error("preset \"%s\" already exists (%s)", name, path);
    } else if (dk_resolve(&ctx->repo, &ctx->state.sel, ctx->os, &choices, &n, &err) != 0) {
        dk_error("%s", err.msg);
    } else {
        /* Apps a preset does not list use "default", so only list the rest. */
        char *text = xstrdup("[preset]\n\n[apps]\n");
        for (size_t i = 0; i < n; i++) {
            if (!choices[i].variant || strcmp(choices[i].variant->name, DK_DEFAULT_VARIANT) == 0)
                continue;
            char *more = xasprintf("%s%s = %s\n", text, choices[i].app->name,
                                   choices[i].variant->name);
            free(text);
            text = more;
        }
        if (dk_write_file(path, text, strlen(text), &err) != 0) {
            dk_error("%s", err.msg);
        } else {
            char *shown = dk_tildify(path);
            printf("Saved preset \"%s\" to %s\n", name, shown);
            free(shown);
            code = DK_EXIT_OK;
        }
        free(text);
    }
    free(choices);
    free(path);
    return code;
}

int cmd_preset(const dk_opts *opts)
{
    const char *sub = opts->nargs ? opts->args[0] : "";
    int ok = (strcmp(sub, "apply") == 0 && opts->nargs == 2) ||
             (strcmp(sub, "save") == 0 && opts->nargs == 2) ||
             (strcmp(sub, "clear") == 0 && opts->nargs == 1);
    if (!ok || no_flags(opts, "preset") != 0) {
        dk_error("usage: dotkeeper preset apply <name> | save <name> | clear");
        return DK_EXIT_USAGE;
    }
    dk_ctx ctx;
    if (ctx_open(opts, &ctx) != 0)
        return DK_EXIT_ERROR;
    int code = DK_EXIT_ERROR;
    if (strcmp(sub, "save") == 0) {
        code = preset_save(&ctx, opts->args[1]);
    } else if (strcmp(sub, "apply") == 0 && !dk_repo_find_preset(&ctx.repo, opts->args[1])) {
        dk_error("no preset \"%s\" in the repo", opts->args[1]);
    } else {
        /* Applying a preset switches every app, so per-app overrides go. */
        dk_selection_set_preset(&ctx.state.sel, strcmp(sub, "apply") == 0 ? opts->args[1] : NULL);
        dk_selection_clear_overrides(&ctx.state.sel);
        code = sync_links(&ctx, opts, NULL);
    }
    ctx_close(&ctx);
    return code;
}

/* ---- deploy / undeploy ---- */

int cmd_deploy(const dk_opts *opts)
{
    if (no_flags(opts, "deploy") != 0 || opts->nargs != 0) {
        dk_error("usage: dotkeeper deploy");
        return DK_EXIT_USAGE;
    }
    dk_ctx ctx;
    if (ctx_open(opts, &ctx) != 0)
        return DK_EXIT_ERROR;
    int code = sync_links(&ctx, opts, NULL);
    ctx_close(&ctx);
    return code;
}

int cmd_undeploy(const dk_opts *opts)
{
    if (no_flags(opts, "undeploy") != 0 || opts->nargs > 1) {
        dk_error("usage: dotkeeper undeploy [app]");
        return DK_EXIT_USAGE;
    }
    dk_ctx ctx;
    if (ctx_open(opts, &ctx) != 0)
        return DK_EXIT_ERROR;
    int code = DK_EXIT_ERROR;
    if (opts->nargs == 1 && !dk_repo_find_app(&ctx.repo, opts->args[0]))
        dk_error("no app \"%s\" in the repo", opts->args[0]);
    else
        code = sync_links(&ctx, opts, opts->nargs == 1 ? opts->args[0] : "*");
    ctx_close(&ctx);
    return code;
}
