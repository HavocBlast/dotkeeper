#include "repo.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "ini.h"

#include "../platform/fs.h"
#include "../platform/paths.h"

int dk_name_valid(const char *name)
{
    if (!name[0] || name[0] == '.')
        return 0;
    for (const char *p = name; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-')
            return 0;
    return 1;
}

int dk_repo_create(const char *root, dk_err *err)
{
    if (dk_mkdirs(root, err) != 0)
        return -1;
    char *apps = dk_path_join(root, "apps");
    char *presets = dk_path_join(root, "presets");
    char *manifest = dk_path_join(root, "dotkeeper.ini");
    int rc = 0;
    if (dk_mkdirs(apps, err) != 0 || dk_mkdirs(presets, err) != 0) {
        rc = -1;
    } else if (dk_fs_kind_of(manifest) == DK_FS_MISSING) {
        char *text = xasprintf("# Dotkeeper repo manifest\n[dotkeeper]\nversion = %d\n",
                               DK_REPO_VERSION);
        rc = dk_write_file(manifest, text, strlen(text), err);
        free(text);
    }
    free(apps);
    free(presets);
    free(manifest);
    return rc;
}

/* ---- dotkeeper.ini ---- */

typedef struct {
    int version;
    dk_strlist *warnings;
    const char *path;
} manifest_ctx;

static int manifest_handler(void *user, const char *section, const char *name,
                            const char *value)
{
    manifest_ctx *c = user;
    if (!name)
        return 1;
    if (strcmp(section, "dotkeeper") == 0 && strcmp(name, "version") == 0)
        c->version = atoi(value);
    else
        strlist_push_owned(c->warnings, xasprintf("%s: unknown key [%s] %s", c->path,
                                                  section, name));
    return 1;
}

/* ---- app.ini ---- */

typedef struct {
    dk_app *app;
    dk_strlist *warnings;
    const char *path;
    dk_err *err;
    int failed;
} app_ctx;

static int parse_os_list(const char *value, unsigned *mask)
{
    char *copy = xstrdup(value);
    unsigned m = 0;
    int rc = 0;
    for (char *tok = strtok(copy, ", \t"); tok; tok = strtok(NULL, ", \t")) {
        dk_os os;
        if (dk_os_parse(tok, &os) != 0) {
            rc = -1;
            break;
        }
        m |= (unsigned)os;
    }
    free(copy);
    if (rc == 0)
        *mask = m;
    return rc;
}

static int set_root(app_ctx *c, char **slot, const char *value)
{
    char *expanded = dk_expand_user(value);
    if (expanded[0] != '/') {
        dk_err_set(c->err, "%s: target root must be absolute or start with ~: %s", c->path,
                   value);
        free(expanded);
        c->failed = 1;
        return 0;
    }
    free(*slot);
    *slot = expanded;
    return 1;
}

static int app_handler(void *user, const char *section, const char *name, const char *value)
{
    app_ctx *c = user;
    if (!name)
        return 1;
    if (strcmp(section, "app") == 0 && strcmp(name, "os") == 0) {
        if (parse_os_list(value, &c->app->os_mask) != 0) {
            dk_err_set(c->err, "%s: unknown OS in \"%s\" (use linux, macos)", c->path, value);
            c->failed = 1;
            return 0;
        }
        return 1;
    }
    if (strcmp(section, "target") == 0) {
        if (strcmp(name, "root") == 0)
            return set_root(c, &c->app->root, value);
        if (strcmp(name, "root.linux") == 0)
            return set_root(c, &c->app->root_linux, value);
        if (strcmp(name, "root.macos") == 0)
            return set_root(c, &c->app->root_macos, value);
    }
    /* Reserved for the application manager. */
    if (strcmp(section, "packages") == 0)
        return 1;
    strlist_push_owned(c->warnings,
                       xasprintf("%s: unknown key [%s] %s", c->path, section, name));
    return 1;
}

/* ---- presets/<name>.ini ---- */

typedef struct {
    dk_preset *preset;
    dk_strlist *warnings;
    dk_err *err;
    int failed;
} preset_ctx;

static int preset_handler(void *user, const char *section, const char *name,
                          const char *value)
{
    preset_ctx *c = user;
    dk_preset *p = c->preset;
    if (!name)
        return 1;
    if (strcmp(section, "preset") == 0 && strcmp(name, "description") == 0) {
        free(p->description);
        p->description = xstrdup(value);
        return 1;
    }
    if (strcmp(section, "apps") == 0) {
        if (!dk_name_valid(name) || !dk_name_valid(value)) {
            dk_err_set(c->err, "%s: invalid entry \"%s = %s\"", p->path, name, value);
            c->failed = 1;
            return 0;
        }
        for (size_t i = 0; i < p->napps; i++) {
            if (strcmp(p->apps[i].app, name) == 0) {
                dk_err_set(c->err, "%s: app \"%s\" is listed twice", p->path, name);
                c->failed = 1;
                return 0;
            }
        }
        dk_pair pair = {xstrdup(name), xstrdup(value)};
        DK_PUSH(p->apps, p->napps, p->capps, pair);
        return 1;
    }
    strlist_push_owned(c->warnings,
                       xasprintf("%s: unknown key [%s] %s", p->path, section, name));
    return 1;
}

/* inih returns 0 on success, -1 if the file cannot be opened, or the line
 * number of the first error. */
static int parse_ini(const char *path, ini_handler handler, void *ctx, int *failed,
                     dk_err *err)
{
    int rc = ini_parse(path, handler, ctx);
    if (failed && *failed)
        return -1;
    if (rc == -1)
        return dk_err_set(err, "cannot read %s", path);
    if (rc != 0)
        return dk_err_set(err, "%s:%d: syntax error", path, rc);
    return 0;
}

/* Names that differ only in case collide on macOS's default file system. */
static int check_case_collisions(const dk_strlist *names, const char *where, dk_err *err)
{
    for (size_t i = 0; i < names->len; i++)
        for (size_t j = i + 1; j < names->len; j++)
            if (strcasecmp(names->items[i], names->items[j]) == 0)
                return dk_err_set(err, "%s: \"%s\" and \"%s\" differ only by case", where,
                                  names->items[i], names->items[j]);
    return 0;
}

static void app_free(dk_app *app)
{
    for (size_t i = 0; i < app->nvariants; i++) {
        free(app->variants[i].name);
        free(app->variants[i].dir);
    }
    free(app->variants);
    free(app->name);
    free(app->dir);
    free(app->root);
    free(app->root_linux);
    free(app->root_macos);
}

static void preset_free(dk_preset *p)
{
    for (size_t i = 0; i < p->napps; i++) {
        free(p->apps[i].app);
        free(p->apps[i].variant);
    }
    free(p->apps);
    free(p->name);
    free(p->path);
    free(p->description);
}

static int load_app(dk_repo *repo, const char *apps_dir, const char *name, dk_err *err)
{
    dk_app app = {0};
    app.name = xstrdup(name);
    app.dir = dk_path_join(apps_dir, name);
    app.os_mask = DK_OS_ALL;

    char *ini = dk_path_join(app.dir, "app.ini");
    if (dk_fs_kind_of(ini) == DK_FS_FILE) {
        app_ctx c = {&app, &repo->warnings, ini, err, 0};
        if (parse_ini(ini, app_handler, &c, &c.failed, err) != 0) {
            free(ini);
            app_free(&app);
            return -1;
        }
    }
    free(ini);

    dk_strlist entries = {0};
    if (dk_list_dir(app.dir, &entries, err) != 0) {
        app_free(&app);
        return -1;
    }
    dk_strlist names = {0};
    int rc = 0;
    for (size_t i = 0; i < entries.len; i++) {
        const char *e = entries.items[i];
        char *dir = dk_path_join(app.dir, e);
        if (dk_fs_kind_of(dir) != DK_FS_DIR) {
            /* app.ini and stray files such as a README are not variants. */
            free(dir);
            continue;
        }
        if (!dk_name_valid(e)) {
            if (e[0] != '.')
                strlist_push_owned(&repo->warnings,
                                   xasprintf("apps/%s/%s: not a valid variant name, ignored",
                                             name, e));
            free(dir);
            continue;
        }
        dk_variant v = {xstrdup(e), dir};
        DK_PUSH(app.variants, app.nvariants, app.cvariants, v);
        strlist_push(&names, e);
    }
    char *where = xasprintf("apps/%s", name);
    rc = check_case_collisions(&names, where, err);
    free(where);
    strlist_free(&names);
    strlist_free(&entries);
    if (rc != 0) {
        app_free(&app);
        return -1;
    }
    DK_PUSH(repo->apps, repo->napps, repo->capps, app);
    return 0;
}

static int load_apps(dk_repo *repo, dk_err *err)
{
    char *apps_dir = dk_path_join(repo->root, "apps");
    dk_strlist entries = {0}, names = {0};
    int rc = 0;
    if (dk_fs_kind_of(apps_dir) == DK_FS_MISSING)
        goto out; /* an empty repo is valid */
    if ((rc = dk_list_dir(apps_dir, &entries, err)) != 0)
        goto out;
    for (size_t i = 0; i < entries.len && rc == 0; i++) {
        const char *e = entries.items[i];
        char *dir = dk_path_join(apps_dir, e);
        int is_dir = dk_fs_kind_of(dir) == DK_FS_DIR;
        free(dir);
        if (!is_dir)
            continue;
        if (!dk_name_valid(e)) {
            if (e[0] != '.')
                strlist_push_owned(&repo->warnings,
                                   xasprintf("apps/%s: not a valid app name, ignored", e));
            continue;
        }
        strlist_push(&names, e);
        rc = load_app(repo, apps_dir, e, err);
    }
    if (rc == 0)
        rc = check_case_collisions(&names, "apps", err);
out:
    strlist_free(&entries);
    strlist_free(&names);
    free(apps_dir);
    return rc;
}

static int load_presets(dk_repo *repo, dk_err *err)
{
    char *dir = dk_path_join(repo->root, "presets");
    dk_strlist entries = {0}, names = {0};
    int rc = 0;
    if (dk_fs_kind_of(dir) == DK_FS_MISSING)
        goto out;
    if ((rc = dk_list_dir(dir, &entries, err)) != 0)
        goto out;
    for (size_t i = 0; i < entries.len && rc == 0; i++) {
        const char *e = entries.items[i];
        if (!str_ends_with(e, ".ini"))
            continue;
        char *name = xasprintf("%.*s", (int)(strlen(e) - 4), e);
        if (!dk_name_valid(name)) {
            strlist_push_owned(&repo->warnings,
                               xasprintf("presets/%s: not a valid preset name, ignored", e));
            free(name);
            continue;
        }
        dk_preset p = {0};
        p.name = name;
        p.path = dk_path_join(dir, e);
        preset_ctx c = {&p, &repo->warnings, err, 0};
        if (parse_ini(p.path, preset_handler, &c, &c.failed, err) != 0) {
            preset_free(&p);
            rc = -1;
            break;
        }
        strlist_push(&names, name);
        DK_PUSH(repo->presets, repo->npresets, repo->cpresets, p);
    }
    if (rc == 0)
        rc = check_case_collisions(&names, "presets", err);
out:
    strlist_free(&entries);
    strlist_free(&names);
    free(dir);
    return rc;
}

int dk_repo_load(const char *root, dk_repo *out, dk_err *err)
{
    memset(out, 0, sizeof *out);
    out->root = xstrdup(root);

    if (dk_fs_kind_of(root) != DK_FS_DIR) {
        dk_err_set(err, "dotfile repo %s does not exist", root);
        goto fail;
    }
    char *manifest = dk_path_join(root, "dotkeeper.ini");
    if (dk_fs_kind_of(manifest) != DK_FS_FILE) {
        dk_err_set(err, "%s is not a Dotkeeper repo (no dotkeeper.ini); run `dotkeeper init`",
                   root);
        free(manifest);
        goto fail;
    }
    manifest_ctx mc = {0, &out->warnings, manifest};
    int rc = parse_ini(manifest, manifest_handler, &mc, NULL, err);
    free(manifest);
    if (rc != 0)
        goto fail;
    if (mc.version > DK_REPO_VERSION) {
        dk_err_set(err, "repo format version %d is newer than this Dotkeeper supports (%d)",
                   mc.version, DK_REPO_VERSION);
        goto fail;
    }
    if (load_apps(out, err) != 0 || load_presets(out, err) != 0)
        goto fail;
    return 0;
fail:
    dk_repo_free(out);
    return -1;
}

void dk_repo_free(dk_repo *repo)
{
    for (size_t i = 0; i < repo->napps; i++)
        app_free(&repo->apps[i]);
    free(repo->apps);
    for (size_t i = 0; i < repo->npresets; i++)
        preset_free(&repo->presets[i]);
    free(repo->presets);
    strlist_free(&repo->warnings);
    free(repo->root);
    memset(repo, 0, sizeof *repo);
}

const dk_app *dk_repo_find_app(const dk_repo *repo, const char *name)
{
    for (size_t i = 0; i < repo->napps; i++)
        if (strcmp(repo->apps[i].name, name) == 0)
            return &repo->apps[i];
    return NULL;
}

const dk_variant *dk_app_find_variant(const dk_app *app, const char *name)
{
    for (size_t i = 0; i < app->nvariants; i++)
        if (strcmp(app->variants[i].name, name) == 0)
            return &app->variants[i];
    return NULL;
}

const dk_preset *dk_repo_find_preset(const dk_repo *repo, const char *name)
{
    for (size_t i = 0; i < repo->npresets; i++)
        if (strcmp(repo->presets[i].name, name) == 0)
            return &repo->presets[i];
    return NULL;
}

const char *dk_preset_variant_for(const dk_preset *preset, const char *app)
{
    for (size_t i = 0; i < preset->napps; i++)
        if (strcmp(preset->apps[i].app, app) == 0)
            return preset->apps[i].variant;
    return NULL;
}

const char *dk_app_root(const dk_app *app, dk_os os)
{
    if (os == DK_OS_LINUX && app->root_linux)
        return app->root_linux;
    if (os == DK_OS_MACOS && app->root_macos)
        return app->root_macos;
    return app->root ? app->root : dk_home();
}
