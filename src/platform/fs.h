/*
 * fs.h: the file system operations Dotkeeper needs, in portable POSIX.
 *
 * Every check uses lstat(), never stat(): we must see a symlink itself,
 * not the file it points to, to know whether the link is ours.
 */
#ifndef DK_FS_H
#define DK_FS_H

#include "../util/util.h"

typedef enum {
    DK_FS_MISSING,
    DK_FS_FILE,
    DK_FS_DIR,
    DK_FS_SYMLINK,
    DK_FS_OTHER,
} dk_fs_kind;

dk_fs_kind dk_fs_kind_of(const char *path);
/* Returns the link's target, or NULL if path is not a readable symlink. */
char *dk_readlink(const char *path);
/* Like `mkdir -p`. */
int dk_mkdirs(const char *path, dk_err *err);
/* Creates the symlink `link` pointing at `target`, creating parent dirs. */
int dk_symlink(const char *target, const char *link, dk_err *err);
int dk_unlink(const char *path, dk_err *err);
/* Moves a file, symlink or directory, creating the destination's parents. */
int dk_move(const char *from, const char *to, dk_err *err);
/* Writes a whole file by writing a temporary file and renaming it. */
int dk_write_file(const char *path, const char *data, size_t len, dk_err *err);
/* Sorted entry names of a directory, without "." and "..". */
int dk_list_dir(const char *path, dk_strlist *out, dk_err *err);

/* Called for each non-directory entry under a root. `rel` is the path
 * relative to the root. A non-zero return stops the walk and becomes
 * dk_walk's return value. */
typedef int (*dk_walk_fn)(const char *rel, const char *abs, void *ctx);
/* Walks a directory tree depth-first in sorted order. Symlinks to
 * directories are reported as entries, not followed. */
int dk_walk(const char *root, dk_walk_fn fn, void *ctx, dk_err *err);

#endif
