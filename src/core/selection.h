/*
 * selection.h: which variant of each app this machine uses.
 *
 * A selection is an optional preset plus per-app overrides. Resolving
 * it against the repo gives one choice per app: override first, then
 * the preset, then the app's "default" variant.
 */
#ifndef DK_SELECTION_H
#define DK_SELECTION_H

#include "repo.h"

typedef struct {
    char *preset; /* NULL when no preset is active */
    dk_pair *overrides;
    size_t noverrides, coverrides;
} dk_selection;

void dk_selection_set_preset(dk_selection *sel, const char *preset);
void dk_selection_set_override(dk_selection *sel, const char *app, const char *variant);
/* Returns 1 if an override was removed, 0 if there was none. */
int dk_selection_clear_override(dk_selection *sel, const char *app);
void dk_selection_clear_overrides(dk_selection *sel);
const char *dk_selection_override(const dk_selection *sel, const char *app);
void dk_selection_free(dk_selection *sel);

typedef enum {
    DK_FROM_DEFAULT,
    DK_FROM_PRESET,
    DK_FROM_OVERRIDE,
} dk_origin;

const char *dk_origin_name(dk_origin origin);

typedef struct {
    const dk_app *app;
    const dk_variant *variant; /* NULL: the app has no variant to deploy */
    dk_origin origin;
} dk_choice;

/* Resolves the selection for every app that applies to `os`. Fails if the
 * preset or an override names something the repo does not have. */
int dk_resolve(const dk_repo *repo, const dk_selection *sel, dk_os os, dk_choice **out,
               size_t *nout, dk_err *err);

#endif
