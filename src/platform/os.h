/*
 * os.h: which operating system Dotkeeper is running on.
 *
 * Only Linux vs macOS matters for dotfiles. Distro detection (Arch vs
 * Debian) belongs here too once the application manager needs it.
 */
#ifndef DK_OS_H
#define DK_OS_H

typedef enum {
    DK_OS_LINUX = 1 << 0,
    DK_OS_MACOS = 1 << 1,
} dk_os;

#define DK_OS_ALL (DK_OS_LINUX | DK_OS_MACOS)

dk_os dk_os_current(void);
const char *dk_os_name(dk_os os);
/* Parses "linux" or "macos". Returns 0 on success, -1 if unknown. */
int dk_os_parse(const char *name, dk_os *out);

#endif
