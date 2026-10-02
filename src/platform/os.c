#include "os.h"

#include <string.h>

dk_os dk_os_current(void)
{
#if defined(__APPLE__)
    return DK_OS_MACOS;
#elif defined(__linux__)
    return DK_OS_LINUX;
#else
#error "Dotkeeper supports Linux and macOS only"
#endif
}

const char *dk_os_name(dk_os os)
{
    switch (os) {
    case DK_OS_LINUX:
        return "linux";
    case DK_OS_MACOS:
        return "macos";
    }
    return "unknown";
}

int dk_os_parse(const char *name, dk_os *out)
{
    if (strcmp(name, "linux") == 0)
        *out = DK_OS_LINUX;
    else if (strcmp(name, "macos") == 0)
        *out = DK_OS_MACOS;
    else
        return -1;
    return 0;
}
