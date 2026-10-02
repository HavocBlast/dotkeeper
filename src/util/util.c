#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void oom(void)
{
    fputs("dotkeeper: out of memory\n", stderr);
    abort();
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        oom();
    return p;
}

void *xcalloc(size_t count, size_t size)
{
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (!p)
        oom();
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        oom();
    return p;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = xmalloc(n);
    memcpy(d, s, n);
    return d;
}

static char *vxasprintf(const char *fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0)
        oom();
    char *buf = xmalloc((size_t)n + 1);
    vsnprintf(buf, (size_t)n + 1, fmt, ap);
    return buf;
}

char *xasprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *s = vxasprintf(fmt, ap);
    va_end(ap);
    return s;
}

int str_eq(const char *a, const char *b)
{
    if (!a || !b)
        return a == b;
    return strcmp(a, b) == 0;
}

int str_starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

int str_ends_with(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && strcmp(s + ls - lx, suffix) == 0;
}

void strlist_push(dk_strlist *l, const char *s)
{
    strlist_push_owned(l, xstrdup(s));
}

void strlist_push_owned(dk_strlist *l, char *s)
{
    DK_PUSH(l->items, l->len, l->cap, s);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

void strlist_sort(dk_strlist *l)
{
    if (l->len > 1)
        qsort(l->items, l->len, sizeof *l->items, cmp_str);
}

void strlist_free(dk_strlist *l)
{
    for (size_t i = 0; i < l->len; i++)
        free(l->items[i]);
    free(l->items);
    l->items = NULL;
    l->len = l->cap = 0;
}

int dk_err_set(dk_err *e, const char *fmt, ...)
{
    if (e) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(e->msg, sizeof e->msg, fmt, ap);
        va_end(ap);
    }
    return -1;
}
