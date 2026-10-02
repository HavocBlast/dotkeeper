#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "ini.h"

#include "../platform/fs.h"
#include "../platform/paths.h"

char *dk_config_path(void)
{
    char *dir = dk_config_dir();
    char *path = dk_path_join(dir, "config.ini");
    free(dir);
    return path;
}

static int config_handler(void *user, const char *section, const char *name,
                          const char *value)
{
    char **repo = user;
    if (name && strcmp(section, "dotkeeper") == 0 && strcmp(name, "repo") == 0) {
        free(*repo);
        *repo = dk_expand_user(value);
    }
    return 1;
}

int dk_config_load(char **repo, dk_err *err)
{
    *repo = NULL;
    char *path = dk_config_path();
    int rc = 0;
    if (dk_fs_kind_of(path) != DK_FS_MISSING) {
        int line = ini_parse(path, config_handler, repo);
        if (line != 0) {
            rc = dk_err_set(err, line < 0 ? "cannot read %s" : "%s: syntax error", path);
            free(*repo);
            *repo = NULL;
        }
    }
    free(path);
    return rc;
}

int dk_config_save(const char *repo, dk_err *err)
{
    if (strchr(repo, '\n'))
        return dk_err_set(err, "repo path cannot contain a newline");
    char *path = dk_config_path();
    char *text = xasprintf("[dotkeeper]\nrepo = %s\n", repo);
    int rc = dk_write_file(path, text, strlen(text), err);
    free(text);
    free(path);
    return rc;
}
