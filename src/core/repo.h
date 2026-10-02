/*
 * repo.h: the dotfile repo loaded into memory.
 *
 * Layout on disk:
 *   <root>/dotkeeper.ini            repo manifest
 *   <root>/apps/<app>/app.ini       optional per-app settings
 *   <root>/apps/<app>/<variant>/    files that mirror $HOME
 *   <root>/presets/<preset>.ini     app -> variant mappings
 */
#ifndef DK_REPO_H
#define DK_REPO_H

#include <stddef.h>

#include "../platform/os.h"
#include "../util/util.h"

#define DK_REPO_VERSION 1
#define DK_DEFAULT_VARIANT "default"

typedef struct {
    char *name;
    char *dir; /* absolute path of the variant directory */
} dk_variant;

typedef struct {
    char *name;
    char *dir;          /* absolute path of apps/<name> */
    unsigned os_mask;   /* dk_os bits this app applies to */
    char *root;         /* where the variant maps to; NULL means $HOME */
    char *root_linux;   /* per-OS overrides of root */
    char *root_macos;
    dk_variant *variants;
    size_t nvariants, cvariants;
} dk_app;

typedef struct {
    char *app;
    char *variant;
} dk_pair;

typedef struct {
    char *name;
    char *path;
    char *description;
    dk_pair *apps;
    size_t napps, capps;
} dk_preset;

typedef struct {
    char *root;
    dk_app *apps;
    size_t napps, capps;
    dk_preset *presets;
    size_t npresets, cpresets;
    dk_strlist warnings; /* non-fatal problems found while loading */
} dk_repo;

/* Names of apps, variants and presets: letters, digits, '.', '_', '-',
 * not starting with '.'. */
int dk_name_valid(const char *name);

/* Creates the repo skeleton in root if it is missing. Existing files are
 * left alone. */
int dk_repo_create(const char *root, dk_err *err);
int dk_repo_load(const char *root, dk_repo *out, dk_err *err);
void dk_repo_free(dk_repo *repo);

const dk_app *dk_repo_find_app(const dk_repo *repo, const char *name);
const dk_variant *dk_app_find_variant(const dk_app *app, const char *name);
const dk_preset *dk_repo_find_preset(const dk_repo *repo, const char *name);
/* The variant a preset picks for an app, or NULL if it does not list it. */
const char *dk_preset_variant_for(const dk_preset *preset, const char *app);
/* The directory an app's variant maps onto on this OS (absolute). */
const char *dk_app_root(const dk_app *app, dk_os os);

#endif
