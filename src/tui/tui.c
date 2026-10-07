#include "tui.h"

#include <curses.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../platform/paths.h"

enum { PANE_APPS, PANE_VARIANTS };

typedef struct {
    dk_ctx *ctx;
    const dk_opts *opts;
    dk_choice *choices; /* resolved selection, one per app on this OS */
    size_t nchoices;
    size_t app;         /* selected row in the apps pane */
    size_t var;         /* selected row in the variants pane */
    size_t app_top, var_top; /* first visible row of each pane */
    int focus;
    char msg[512];
} ui_t;

static void set_msg(ui_t *u, const char *fmt, ...) DK_PRINTF(2, 3);

static void set_msg(ui_t *u, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(u->msg, sizeof u->msg, fmt, ap);
    va_end(ap);
}

static const dk_choice *cur(const ui_t *u)
{
    return u->nchoices ? &u->choices[u->app] : NULL;
}

/* Points the variants cursor at the app's active variant. */
static void sync_variant_cursor(ui_t *u)
{
    const dk_choice *c = cur(u);
    u->var = 0;
    u->var_top = 0;
    for (size_t i = 0; c && c->variant && i < c->app->nvariants; i++)
        if (&c->app->variants[i] == c->variant)
            u->var = i;
}

static void reload(ui_t *u)
{
    dk_err err;
    free(u->choices);
    u->choices = NULL;
    u->nchoices = 0;
    if (dk_resolve(&u->ctx->repo, &u->ctx->state.sel, u->ctx->os, &u->choices, &u->nchoices,
                   &err) != 0)
        set_msg(u, "Error: %s", err.msg);
    if (u->app >= u->nchoices)
        u->app = u->nchoices ? u->nchoices - 1 : 0;
    sync_variant_cursor(u);
}

/* ---- drawing helpers ---- */

static void put(int y, int x, int width, const char *s)
{
    if (width > 0)
        mvaddnstr(y, x, s, width);
}

static void draw_box(int y, int x, int h, int w, const char *title, int focused)
{
    if (focused)
        attron(A_BOLD);
    mvaddch(y, x, ACS_ULCORNER);
    mvhline(y, x + 1, ACS_HLINE, w - 2);
    mvaddch(y, x + w - 1, ACS_URCORNER);
    mvvline(y + 1, x, ACS_VLINE, h - 2);
    mvvline(y + 1, x + w - 1, ACS_VLINE, h - 2);
    mvaddch(y + h - 1, x, ACS_LLCORNER);
    mvhline(y + h - 1, x + 1, ACS_HLINE, w - 2);
    mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
    put(y, x + 2, w - 4, title);
    if (focused)
        attroff(A_BOLD);
}

/* Keeps the selected row inside the visible window of a pane. */
static void scroll_to(size_t sel, size_t *top, int rows)
{
    if (rows < 1)
        rows = 1;
    if (sel < *top)
        *top = sel;
    else if (sel >= *top + (size_t)rows)
        *top = sel - (size_t)rows + 1;
}

static void links_summary(const ui_t *u, char *out, size_t n)
{
    const dk_choice *c = cur(u);
    if (!c) {
        snprintf(out, n, "No apps for %s in the repo yet.", dk_os_name(u->ctx->os));
        return;
    }
    if (!c->variant) {
        snprintf(out, n, "%s has no \"%s\" variant, so nothing is linked.", c->app->name,
                 DK_DEFAULT_VARIANT);
        return;
    }
    dk_err err;
    dk_link *want = NULL;
    size_t nwant = 0;
    if (dk_wanted_links(c, 1, u->ctx->os, &want, &nwant, &err) != 0) {
        snprintf(out, n, "Error: %.400s", err.msg);
        return;
    }
    size_t counts[DK_HEALTH_WRONG + 1] = {0};
    for (size_t i = 0; i < nwant; i++)
        counts[dk_link_health(&want[i], &u->ctx->state)]++;
    size_t len = (size_t)snprintf(out, n, "%s links:", c->app->name);
    if (nwant == 0)
        snprintf(out + len, n - len, " no files");
    for (int h = DK_HEALTH_OK; h <= DK_HEALTH_WRONG && len < n; h++)
        if (counts[h])
            len += (size_t)snprintf(out + len, n - len, " %zu %s", counts[h],
                                    dk_health_name((dk_health)h));
    dk_links_free(want, nwant);
}

static void draw(ui_t *u)
{
    erase();
    if (LINES < 12 || COLS < 50) {
        put(0, 0, COLS, "Terminal too small for Dotkeeper (need 50x12). q quits.");
        refresh();
        return;
    }

    char *repo = dk_tildify(u->ctx->repo_path);
    char header[1024];
    snprintf(header, sizeof header, " Dotkeeper   repo: %s   preset: %s   system: %s%s", repo,
             u->ctx->state.sel.preset ? u->ctx->state.sel.preset : "(none)",
             dk_os_name(u->ctx->os), u->opts->dry_run ? "   [dry run]" : "");
    free(repo);
    attron(A_REVERSE);
    mvhline(0, 0, ' ', COLS);
    put(0, 0, COLS, header);
    attroff(A_REVERSE);

    int top = 2, h = LINES - 6;
    int lw = COLS * 2 / 5, rw = COLS - lw;
    int rows = h - 2;

    /* Apps pane: name, active variant, and where the choice came from. */
    draw_box(top, 0, h, lw, " Apps ", u->focus == PANE_APPS);
    scroll_to(u->app, &u->app_top, rows);
    for (size_t i = u->app_top; i < u->nchoices && (int)(i - u->app_top) < rows; i++) {
        const dk_choice *c = &u->choices[i];
        char line[256];
        snprintf(line, sizeof line, " %-14s %s%s", c->app->name,
                 c->variant ? c->variant->name : "-",
                 c->origin == DK_FROM_OVERRIDE ? " (override)" : "");
        int attr = i == u->app ? (u->focus == PANE_APPS ? A_REVERSE : A_BOLD) : 0;
        attron(attr);
        mvhline(top + 1 + (int)(i - u->app_top), 1, ' ', lw - 2);
        put(top + 1 + (int)(i - u->app_top), 1, lw - 2, line);
        attroff(attr);
    }

    /* Variants pane for the selected app; '*' marks the active one. */
    const dk_choice *c = cur(u);
    char title[128];
    snprintf(title, sizeof title, " Variants: %s ", c ? c->app->name : "");
    draw_box(top, lw, h, rw, title, u->focus == PANE_VARIANTS);
    if (c) {
        scroll_to(u->var, &u->var_top, rows);
        for (size_t i = u->var_top; i < c->app->nvariants && (int)(i - u->var_top) < rows;
             i++) {
            const dk_variant *v = &c->app->variants[i];
            char line[256];
            snprintf(line, sizeof line, " %c %s", v == c->variant ? '*' : ' ', v->name);
            int attr = i == u->var ? (u->focus == PANE_VARIANTS ? A_REVERSE : A_BOLD) : 0;
            attron(attr);
            mvhline(top + 1 + (int)(i - u->var_top), lw + 1, ' ', rw - 2);
            put(top + 1 + (int)(i - u->var_top), lw + 1, rw - 2, line);
            attroff(attr);
        }
    }

    char summary[512];
    links_summary(u, summary, sizeof summary);
    put(LINES - 4, 1, COLS - 2, summary);
    attron(A_BOLD);
    put(LINES - 3, 1, COLS - 2, u->msg);
    attroff(A_BOLD);
    attron(A_REVERSE);
    mvhline(LINES - 1, 0, ' ', COLS);
    put(LINES - 1, 0, COLS,
        " Up/Down move  Tab switch pane  Enter use variant  r reset  p presets  d deploy  q quit");
    attroff(A_REVERSE);
    refresh();
}

/* ---- dialogs ---- */

/* Draws a centered box with the given lines; returns the next key. */
static int dialog(const char *title, char **lines, size_t n, const char *keys)
{
    int w = COLS - 8 < 100 ? COLS - 8 : 100;
    int max_rows = LINES - 8;
    int shown = (int)n < max_rows ? (int)n : max_rows;
    int h = shown + 4;
    int y = (LINES - h) / 2, x = (COLS - w) / 2;
    for (int r = 0; r < h; r++)
        mvhline(y + r, x, ' ', w);
    draw_box(y, x, h, w, title, 1);
    for (int i = 0; i < shown; i++) {
        if (i == shown - 1 && (int)n > shown) {
            char more[64];
            snprintf(more, sizeof more, "... and %zu more", n - (size_t)shown + 1);
            put(y + 1 + i, x + 2, w - 4, more);
        } else {
            put(y + 1 + i, x + 2, w - 4, lines[i]);
        }
    }
    attron(A_BOLD);
    put(y + h - 2, x + 2, w - 4, keys);
    attroff(A_BOLD);
    refresh();
    return getch();
}

/* Shows a plan and returns 'y' (apply), 'b' (retry with backup) or 'n'. */
static int confirm_plan(const ui_t *u, const dk_plan *plan, const char *what)
{
    dk_strlist lines = {0};
    for (size_t i = 0; i < plan->nconflicts; i++) {
        char *p = dk_tildify(plan->conflicts[i].path);
        strlist_push_owned(&lines, xasprintf("CONFLICT %s: %s", p, plan->conflicts[i].reason));
        free(p);
    }
    for (size_t i = 0; i < plan->nacts; i++) {
        const dk_action *a = &plan->acts[i];
        char *p = dk_tildify(a->path);
        char *o = a->other ? dk_tildify(a->other) : NULL;
        strlist_push_owned(&lines, o ? xasprintf("%-7s %s -> %s", dk_act_name(a->kind), p, o)
                                     : xasprintf("%-7s %s", dk_act_name(a->kind), p));
        free(p);
        free(o);
    }
    char title[160];
    snprintf(title, sizeof title, " %s ", what);
    const char *keys = plan->nconflicts
                           ? "Nothing was changed.  b: move files aside and retry   n/Esc: cancel"
                           : (u->opts->dry_run ? "Dry run: Esc to close"
                                               : "y/Enter: apply   n/Esc: cancel");
    int key;
    for (;;) {
        key = dialog(title, lines.items, lines.len, keys);
        if (key == KEY_RESIZE)
            continue;
        if (plan->nconflicts) {
            if (key == 'b' || key == 'B') {
                key = 'b';
                break;
            }
        } else if (!u->opts->dry_run && (key == 'y' || key == 'Y' || key == '\n' ||
                                         key == '\r' || key == KEY_ENTER)) {
            key = 'y';
            break;
        }
        if (key == 'n' || key == 'N' || key == 27 || key == 'q') {
            key = 'n';
            break;
        }
    }
    strlist_free(&lines);
    return key;
}

/* Plans the selection now in ctx->state.sel, asks, applies and saves.
 * Returns 0 if the change stands, -1 if the caller should undo it. */
static int run_change(ui_t *u, const char *what, const char *skip_app)
{
    dk_err err;
    char *backup_dir = NULL;
    int rc = -1;
    for (;;) {
        dk_plan plan;
        if (dk_plan_selection(&u->ctx->repo, &u->ctx->state, u->ctx->os, skip_app, backup_dir,
                              &plan, &err) != 0) {
            set_msg(u, "Error: %s", err.msg);
            break;
        }
        if (plan.nacts == 0 && plan.nconflicts == 0) {
            dk_plan_free(&plan);
            if (u->opts->dry_run) {
                set_msg(u, "Dry run: nothing would change.");
                break;
            }
            rc = dk_state_save(u->ctx->state_path, &u->ctx->state, &err);
            if (rc == 0)
                set_msg(u, "Everything is up to date.");
            else
                set_msg(u, "Error: %s", err.msg);
            break;
        }
        draw(u);
        int key = confirm_plan(u, &plan, what);
        if (key == 'b' && !backup_dir) {
            backup_dir = dk_new_backup_dir();
            dk_plan_free(&plan);
            continue;
        }
        if (key != 'y') {
            set_msg(u, u->opts->dry_run ? "Dry run: nothing changed." : "Cancelled.");
            dk_plan_free(&plan);
            break;
        }
        int applied = dk_plan_apply(&plan, &u->ctx->state, &err);
        dk_err serr;
        int saved = dk_state_save(u->ctx->state_path, &u->ctx->state, &serr);
        if (applied != 0)
            set_msg(u, "Error: %s", err.msg);
        else if (saved != 0)
            set_msg(u, "Error: %s", serr.msg);
        else
            set_msg(u, "Done: %zu change%s applied.", plan.nacts, plan.nacts == 1 ? "" : "s");
        /* Even a partial apply changed files, so the selection stays. */
        rc = 0;
        dk_plan_free(&plan);
        break;
    }
    free(backup_dir);
    return rc;
}

/* Runs a selection edit, undoing it if the user cancels or it fails. */
static void change_selection(ui_t *u, const dk_selection *before, const char *what)
{
    if (run_change(u, what, NULL) != 0) {
        dk_selection_free(&u->ctx->state.sel);
        dk_selection_copy(&u->ctx->state.sel, before);
    }
    reload(u);
}

static void use_variant(ui_t *u)
{
    const dk_choice *c = cur(u);
    if (!c || u->var >= c->app->nvariants)
        return;
    const dk_variant *v = &c->app->variants[u->var];
    if (v == c->variant && c->origin == DK_FROM_OVERRIDE) {
        set_msg(u, "%s already uses %s.", c->app->name, v->name);
        return;
    }
    dk_selection before;
    dk_selection_copy(&before, &u->ctx->state.sel);
    char what[256];
    snprintf(what, sizeof what, "Use %s for %s", v->name, c->app->name);
    dk_selection_set_override(&u->ctx->state.sel, c->app->name, v->name);
    change_selection(u, &before, what);
    dk_selection_free(&before);
}

static void reset_app(ui_t *u)
{
    const dk_choice *c = cur(u);
    if (!c)
        return;
    if (!dk_selection_override(&u->ctx->state.sel, c->app->name)) {
        set_msg(u, "%s has no override.", c->app->name);
        return;
    }
    dk_selection before;
    dk_selection_copy(&before, &u->ctx->state.sel);
    char what[256];
    snprintf(what, sizeof what, "Reset %s to its preset variant", c->app->name);
    dk_selection_clear_override(&u->ctx->state.sel, c->app->name);
    change_selection(u, &before, what);
    dk_selection_free(&before);
}

static void choose_preset(ui_t *u)
{
    const dk_repo *repo = &u->ctx->repo;
    size_t n = repo->npresets + 1; /* the last row means "no preset" */
    size_t sel = 0, top = 0;
    for (size_t i = 0; i < repo->npresets; i++)
        if (str_eq(repo->presets[i].name, u->ctx->state.sel.preset))
            sel = i;
    if (!u->ctx->state.sel.preset)
        sel = n - 1;
    for (;;) {
        draw(u);
        int w = COLS - 8 < 70 ? COLS - 8 : 70;
        int rows = (int)n < LINES - 8 ? (int)n : LINES - 8;
        int h = rows + 4, y = (LINES - h) / 2, x = (COLS - w) / 2;
        for (int r = 0; r < h; r++)
            mvhline(y + r, x, ' ', w);
        draw_box(y, x, h, w, " Presets ", 1);
        scroll_to(sel, &top, rows);
        for (size_t i = top; i < n && (int)(i - top) < rows; i++) {
            char line[512];
            if (i < repo->npresets) {
                const dk_preset *p = &repo->presets[i];
                snprintf(line, sizeof line, " %c %-16s %s",
                         str_eq(p->name, u->ctx->state.sel.preset) ? '*' : ' ', p->name,
                         p->description ? p->description : "");
            } else {
                snprintf(line, sizeof line, " %c (no preset: every app uses default)",
                         u->ctx->state.sel.preset ? ' ' : '*');
            }
            int attr = i == sel ? A_REVERSE : 0;
            attron(attr);
            mvhline(y + 1 + (int)(i - top), x + 1, ' ', w - 2);
            put(y + 1 + (int)(i - top), x + 1, w - 2, line);
            attroff(attr);
        }
        attron(A_BOLD);
        put(y + h - 2, x + 2, w - 4, "Enter: apply preset   Esc: cancel");
        attroff(A_BOLD);
        refresh();
        int key = getch();
        if ((key == KEY_UP || key == 'k') && sel > 0)
            sel--;
        else if ((key == KEY_DOWN || key == 'j') && sel + 1 < n)
            sel++;
        else if (key == 27 || key == 'q' || key == 'n')
            return;
        else if (key == '\n' || key == '\r' || key == KEY_ENTER)
            break;
    }
    dk_selection before;
    dk_selection_copy(&before, &u->ctx->state.sel);
    const char *name = sel < repo->npresets ? repo->presets[sel].name : NULL;
    char what[256];
    snprintf(what, sizeof what, name ? "Apply preset %s" : "Stop using a preset%s",
             name ? name : "");
    /* As in the CLI, applying a preset drops per-app overrides. */
    dk_selection_set_preset(&u->ctx->state.sel, name);
    dk_selection_clear_overrides(&u->ctx->state.sel);
    change_selection(u, &before, what);
    dk_selection_free(&before);
}

static void deploy(ui_t *u)
{
    run_change(u, "Deploy", NULL);
    reload(u);
}

/* ---- main loop ---- */

int dk_tui_run(const dk_opts *opts)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        dk_error("--tui needs a terminal");
        return DK_EXIT_USAGE;
    }
    dk_ctx ctx;
    if (ctx_open(opts, &ctx) != 0)
        return DK_EXIT_ERROR;

    setlocale(LC_ALL, "");
    /* Make Esc respond quickly; ncurses reads this before initscr(). */
    setenv("ESCDELAY", "25", 0);
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    ui_t u = {0};
    u.ctx = &ctx;
    u.opts = opts;
    u.focus = PANE_APPS;
    reload(&u);
    if (!u.msg[0])
        set_msg(&u, "Choose an app, then a variant with Enter.");

    for (;;) {
        draw(&u);
        int key = getch();
        const dk_choice *c = cur(&u);
        if (key == 'q' || key == 'Q')
            break;
        switch (key) {
        case KEY_UP:
        case 'k':
            if (u.focus == PANE_APPS && u.app > 0) {
                u.app--;
                sync_variant_cursor(&u);
            } else if (u.focus == PANE_VARIANTS && u.var > 0) {
                u.var--;
            }
            break;
        case KEY_DOWN:
        case 'j':
            if (u.focus == PANE_APPS && u.app + 1 < u.nchoices) {
                u.app++;
                sync_variant_cursor(&u);
            } else if (u.focus == PANE_VARIANTS && c && u.var + 1 < c->app->nvariants) {
                u.var++;
            }
            break;
        case '\t':
        case KEY_LEFT:
        case KEY_RIGHT:
        case 'h':
        case 'l':
            u.focus = u.focus == PANE_APPS ? PANE_VARIANTS : PANE_APPS;
            break;
        case '\n':
        case '\r':
        case KEY_ENTER:
            if (u.focus == PANE_APPS)
                u.focus = PANE_VARIANTS;
            else
                use_variant(&u);
            break;
        case 'r':
            reset_app(&u);
            break;
        case 'p':
            choose_preset(&u);
            break;
        case 'd':
            deploy(&u);
            break;
        default:
            break; /* KEY_RESIZE and unknown keys just redraw */
        }
    }

    endwin();
    free(u.choices);
    ctx_close(&ctx);
    return DK_EXIT_OK;
}
