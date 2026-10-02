/*
 * paths.h: home directory, XDG base directories and path helpers.
 *
 * All returned strings are heap-allocated and owned by the caller,
 * except dk_home(), which is cached for the life of the process.
 */
#ifndef DK_PATHS_H
#define DK_PATHS_H

#include "../util/util.h"

const char *dk_home(void);
char *dk_path_join(const char *a, const char *b);
/* Expands a leading "~" or "~/". Other paths are copied unchanged. */
char *dk_expand_user(const char *path);
/* Expands "~" and makes the path absolute against the current directory.
 * Removes "." components and duplicate or trailing slashes; does not
 * resolve symlinks or "..". Returns NULL with err set on failure. */
char *dk_abspath(const char *path, dk_err *err);
/* Returns the path with the leading "$HOME/" replaced by "~/" for display. */
char *dk_tildify(const char *path);
/* The directory part of a path ("/a/b" -> "/a", "/a" -> "/"). */
char *dk_dirname(const char *path);

char *dk_config_dir(void); /* $XDG_CONFIG_HOME/dotkeeper */
char *dk_state_dir(void);  /* $XDG_STATE_HOME/dotkeeper */

#endif
