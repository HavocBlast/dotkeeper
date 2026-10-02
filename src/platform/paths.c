#include "paths.h"

#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *dk_home(void)
{
    static char *home;
    if (!home) {
        const char *h = getenv("HOME");
        if (!h || !*h) {
            struct passwd *pw = getpwuid(getuid());
            h = pw && pw->pw_dir ? pw->pw_dir : "/";
        }
        home = xstrdup(h);
    }
    return home;
}

char *dk_path_join(const char *a, const char *b)
{
    size_t la = strlen(a);
    while (la > 1 && a[la - 1] == '/')
        la--;
    while (*b == '/')
        b++;
    if (!*b)
        return xasprintf("%.*s", (int)la, a);
    if (la == 1 && a[0] == '/')
        return xasprintf("/%s", b);
    return xasprintf("%.*s/%s", (int)la, a, b);
}

char *dk_expand_user(const char *path)
{
    if (path[0] == '~' && (path[1] == '\0' || path[1] == '/'))
        return dk_path_join(dk_home(), path + 1);
    return xstrdup(path);
}

/* Rebuilds an absolute path, dropping "." components and extra slashes. */
static char *normalize(const char *abs)
{
    size_t n = strlen(abs);
    char *out = xmalloc(n + 2);
    size_t o = 0;
    const char *p = abs;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        const char *end = strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (!(len == 1 && p[0] == '.')) {
            out[o++] = '/';
            memcpy(out + o, p, len);
            o += len;
        }
        p += len;
    }
    if (o == 0)
        out[o++] = '/';
    out[o] = '\0';
    return out;
}

char *dk_abspath(const char *path, dk_err *err)
{
    if (!*path) {
        dk_err_set(err, "empty path");
        return NULL;
    }
    char *expanded = dk_expand_user(path);
    char *joined;
    if (expanded[0] == '/') {
        joined = expanded;
    } else {
        char cwd[4096];
        if (!getcwd(cwd, sizeof cwd)) {
            dk_err_set(err, "cannot read current directory: %s", strerror(errno));
            free(expanded);
            return NULL;
        }
        joined = dk_path_join(cwd, expanded);
        free(expanded);
    }
    char *out = normalize(joined);
    free(joined);
    return out;
}

char *dk_tildify(const char *path)
{
    const char *home = dk_home();
    size_t lh = strlen(home);
    if (lh > 1 && strncmp(path, home, lh) == 0 && (path[lh] == '/' || path[lh] == '\0'))
        return xasprintf("~%s", path + lh);
    return xstrdup(path);
}

char *dk_dirname(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (!slash)
        return xstrdup(".");
    if (slash == path)
        return xstrdup("/");
    return xasprintf("%.*s", (int)(slash - path), path);
}

static char *xdg_dir(const char *env, const char *fallback)
{
    const char *v = getenv(env);
    /* The XDG spec says relative values are invalid and must be ignored. */
    char *base = v && v[0] == '/' ? xstrdup(v) : dk_path_join(dk_home(), fallback);
    char *dir = dk_path_join(base, "dotkeeper");
    free(base);
    return dir;
}

char *dk_config_dir(void)
{
    return xdg_dir("XDG_CONFIG_HOME", ".config");
}

char *dk_state_dir(void)
{
    return xdg_dir("XDG_STATE_HOME", ".local/state");
}
