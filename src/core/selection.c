#include "selection.h"

#include <stdlib.h>
#include <string.h>

void dk_selection_set_preset(dk_selection *sel, const char *preset)
{
    free(sel->preset);
    sel->preset = preset ? xstrdup(preset) : NULL;
}

void dk_selection_set_override(dk_selection *sel, const char *app, const char *variant)
{
    for (size_t i = 0; i < sel->noverrides; i++) {
        if (strcmp(sel->overrides[i].app, app) == 0) {
            free(sel->overrides[i].variant);
            sel->overrides[i].variant = xstrdup(variant);
            return;
        }
    }
    dk_pair p = {xstrdup(app), xstrdup(variant)};
    DK_PUSH(sel->overrides, sel->noverrides, sel->coverrides, p);
}

int dk_selection_clear_override(dk_selection *sel, const char *app)
{
    for (size_t i = 0; i < sel->noverrides; i++) {
        if (strcmp(sel->overrides[i].app, app) == 0) {
            free(sel->overrides[i].app);
            free(sel->overrides[i].variant);
            sel->overrides[i] = sel->overrides[--sel->noverrides];
            return 1;
        }
    }
    return 0;
}

void dk_selection_clear_overrides(dk_selection *sel)
{
    for (size_t i = 0; i < sel->noverrides; i++) {
        free(sel->overrides[i].app);
        free(sel->overrides[i].variant);
    }
    sel->noverrides = 0;
}

const char *dk_selection_override(const dk_selection *sel, const char *app)
{
    for (size_t i = 0; i < sel->noverrides; i++)
        if (strcmp(sel->overrides[i].app, app) == 0)
            return sel->overrides[i].variant;
    return NULL;
}

void dk_selection_copy(dk_selection *dst, const dk_selection *src)
{
    memset(dst, 0, sizeof *dst);
    dk_selection_set_preset(dst, src->preset);
    for (size_t i = 0; i < src->noverrides; i++)
        dk_selection_set_override(dst, src->overrides[i].app, src->overrides[i].variant);
}

void dk_selection_free(dk_selection *sel)
{
    dk_selection_clear_overrides(sel);
    free(sel->overrides);
    free(sel->preset);
    memset(sel, 0, sizeof *sel);
}

const char *dk_origin_name(dk_origin origin)
{
    switch (origin) {
    case DK_FROM_DEFAULT:
        return "default";
    case DK_FROM_PRESET:
        return "preset";
    case DK_FROM_OVERRIDE:
        return "override";
    }
    return "?";
}

int dk_resolve(const dk_repo *repo, const dk_selection *sel, dk_os os, dk_choice **out,
               size_t *nout, dk_err *err)
{
    *out = NULL;
    *nout = 0;

    const dk_preset *preset = NULL;
    if (sel->preset && !(preset = dk_repo_find_preset(repo, sel->preset)))
        return dk_err_set(err, "preset \"%s\" does not exist in the repo", sel->preset);

    for (size_t i = 0; i < sel->noverrides; i++)
        if (!dk_repo_find_app(repo, sel->overrides[i].app))
            return dk_err_set(err, "app \"%s\" has an override but no longer exists",
                              sel->overrides[i].app);
    if (preset)
        for (size_t i = 0; i < preset->napps; i++)
            if (!dk_repo_find_app(repo, preset->apps[i].app))
                return dk_err_set(err, "preset \"%s\" lists unknown app \"%s\"", preset->name,
                                  preset->apps[i].app);

    dk_choice *choices = xcalloc(repo->napps ? repo->napps : 1, sizeof *choices);
    size_t n = 0;
    for (size_t i = 0; i < repo->napps; i++) {
        const dk_app *app = &repo->apps[i];
        if (!(app->os_mask & (unsigned)os))
            continue;
        dk_choice c = {app, NULL, DK_FROM_DEFAULT};
        const char *name = dk_selection_override(sel, app->name);
        if (name) {
            c.origin = DK_FROM_OVERRIDE;
        } else if (preset && (name = dk_preset_variant_for(preset, app->name))) {
            c.origin = DK_FROM_PRESET;
        } else {
            name = DK_DEFAULT_VARIANT;
        }
        c.variant = dk_app_find_variant(app, name);
        if (!c.variant && c.origin != DK_FROM_DEFAULT) {
            free(choices);
            return dk_err_set(err, "app \"%s\" has no variant \"%s\" (selected by %s)",
                              app->name, name, dk_origin_name(c.origin));
        }
        choices[n++] = c;
    }
    *out = choices;
    *nout = n;
    return 0;
}
