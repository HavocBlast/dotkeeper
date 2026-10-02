#include "fs.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "paths.h"

dk_fs_kind dk_fs_kind_of(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return DK_FS_MISSING;
    if (S_ISLNK(st.st_mode))
        return DK_FS_SYMLINK;
    if (S_ISDIR(st.st_mode))
        return DK_FS_DIR;
    if (S_ISREG(st.st_mode))
        return DK_FS_FILE;
    return DK_FS_OTHER;
}

char *dk_readlink(const char *path)
{
    size_t cap = 256;
    for (;;) {
        char *buf = xmalloc(cap);
        ssize_t n = readlink(path, buf, cap);
        if (n < 0) {
            free(buf);
            return NULL;
        }
        /* readlink does not terminate the string and silently truncates,
         * so a full buffer means we must retry with a bigger one. */
        if ((size_t)n < cap) {
            buf[n] = '\0';
            return buf;
        }
        free(buf);
        cap *= 2;
    }
}

int dk_mkdirs(const char *path, dk_err *err)
{
    char *p = xstrdup(path);
    for (char *s = p + 1;; s++) {
        if (*s != '/' && *s != '\0')
            continue;
        char saved = *s;
        *s = '\0';
        if (mkdir(p, 0755) != 0 && errno != EEXIST) {
            dk_err_set(err, "cannot create directory %s: %s", p, strerror(errno));
            free(p);
            return -1;
        }
        if (dk_fs_kind_of(p) != DK_FS_DIR) {
            /* A symlink to a directory is fine; anything else is not. */
            struct stat st;
            if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) {
                dk_err_set(err, "%s exists and is not a directory", p);
                free(p);
                return -1;
            }
        }
        *s = saved;
        if (saved == '\0')
            break;
    }
    free(p);
    return 0;
}

static int mkparents(const char *path, dk_err *err)
{
    char *dir = dk_dirname(path);
    int rc = dk_mkdirs(dir, err);
    free(dir);
    return rc;
}

int dk_symlink(const char *target, const char *link, dk_err *err)
{
    if (mkparents(link, err) != 0)
        return -1;
    if (symlink(target, link) != 0)
        return dk_err_set(err, "cannot link %s: %s", link, strerror(errno));
    return 0;
}

int dk_unlink(const char *path, dk_err *err)
{
    if (unlink(path) != 0)
        return dk_err_set(err, "cannot remove %s: %s", path, strerror(errno));
    return 0;
}

int dk_move(const char *from, const char *to, dk_err *err)
{
    if (mkparents(to, err) != 0)
        return -1;
    if (rename(from, to) != 0) {
        if (errno == EXDEV)
            return dk_err_set(err, "cannot move %s to %s: they are on different file systems",
                              from, to);
        return dk_err_set(err, "cannot move %s to %s: %s", from, to, strerror(errno));
    }
    return 0;
}

int dk_write_file(const char *path, const char *data, size_t len, dk_err *err)
{
    if (mkparents(path, err) != 0)
        return -1;
    char *tmp = xasprintf("%s.tmp.%ld", path, (long)getpid());
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        dk_err_set(err, "cannot write %s: %s", tmp, strerror(errno));
        free(tmp);
        return -1;
    }
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            dk_err_set(err, "cannot write %s: %s", tmp, strerror(errno));
            close(fd);
            unlink(tmp);
            free(tmp);
            return -1;
        }
        off += (size_t)n;
    }
    if (close(fd) != 0 || rename(tmp, path) != 0) {
        dk_err_set(err, "cannot write %s: %s", path, strerror(errno));
        unlink(tmp);
        free(tmp);
        return -1;
    }
    free(tmp);
    return 0;
}

int dk_list_dir(const char *path, dk_strlist *out, dk_err *err)
{
    DIR *d = opendir(path);
    if (!d)
        return dk_err_set(err, "cannot read directory %s: %s", path, strerror(errno));
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        strlist_push(out, e->d_name);
    }
    closedir(d);
    strlist_sort(out);
    return 0;
}

static int walk(const char *root, const char *rel, dk_walk_fn fn, void *ctx, dk_err *err)
{
    char *abs = rel[0] ? dk_path_join(root, rel) : xstrdup(root);
    dk_strlist names = {0};
    int rc = dk_list_dir(abs, &names, err);
    for (size_t i = 0; rc == 0 && i < names.len; i++) {
        char *child_rel = rel[0] ? dk_path_join(rel, names.items[i]) : xstrdup(names.items[i]);
        char *child_abs = dk_path_join(abs, names.items[i]);
        if (dk_fs_kind_of(child_abs) == DK_FS_DIR)
            rc = walk(root, child_rel, fn, ctx, err);
        else
            rc = fn(child_rel, child_abs, ctx);
        free(child_rel);
        free(child_abs);
    }
    strlist_free(&names);
    free(abs);
    return rc;
}

int dk_walk(const char *root, dk_walk_fn fn, void *ctx, dk_err *err)
{
    return walk(root, "", fn, ctx, err);
}
