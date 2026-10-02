/*
 * util.h: allocation helpers, strings, string lists, error reporting.
 *
 * Allocation failures abort: a dotfile manager has no useful way to
 * recover from running out of memory, and checking every malloc would
 * bury the real logic.
 */
#ifndef DK_UTIL_H
#define DK_UTIL_H

#include <stddef.h>

#if defined(__GNUC__) || defined(__clang__)
#define DK_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define DK_PRINTF(fmt, args)
#endif

void *xmalloc(size_t n);
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xasprintf(const char *fmt, ...) DK_PRINTF(1, 2);

/* Append item to a growable array described by (arr, len, cap). */
#define DK_PUSH(arr, len, cap, item)                                   \
    do {                                                               \
        if ((len) == (cap)) {                                          \
            (cap) = (cap) ? (cap) * 2 : 8;                             \
            (arr) = xrealloc((arr), (cap) * sizeof *(arr));            \
        }                                                              \
        (arr)[(len)++] = (item);                                       \
    } while (0)

int str_eq(const char *a, const char *b);
int str_starts_with(const char *s, const char *prefix);
int str_ends_with(const char *s, const char *suffix);

typedef struct {
    char **items;
    size_t len, cap;
} dk_strlist;

void strlist_push(dk_strlist *l, const char *s); /* stores a copy */
void strlist_push_owned(dk_strlist *l, char *s); /* takes ownership */
void strlist_sort(dk_strlist *l);
void strlist_free(dk_strlist *l);

/* Errors carry a message up to the front end, which decides how to show it. */
typedef struct {
    char msg[1024];
} dk_err;

/* Always returns -1 so callers can write `return dk_err_set(...)`. */
int dk_err_set(dk_err *e, const char *fmt, ...) DK_PRINTF(2, 3);

#endif
