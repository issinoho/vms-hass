/* ha_text.c - small text helpers (see ha_text.h).  From vms-beszel 47dbcf8 (D4). */
#include <string.h>

#include "ha_text.h"

static int isws(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

void ha_trim(const char **s, size_t *n)
{
    while (*n && isws((unsigned char)**s)) {
        (*s)++;
        (*n)--;
    }
    while (*n && isws((unsigned char)(*s)[*n - 1]))
        (*n)--;
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

int ha_ieqn(const char *a, size_t an, const char *b)
{
    size_t i;
    for (i = 0; i < an; i++)
        if (!b[i] || lower((unsigned char)a[i]) != lower((unsigned char)b[i]))
            return 0;
    return b[an] == 0;
}

int ha_ieq(const char *a, const char *b)
{
    return ha_ieqn(a, strlen(a), b);
}

int ha_strlcpyn(char *dst, size_t cap, const char *src, size_t n)
{
    int ok = n < cap;
    if (!cap)
        return 0;
    if (!ok)
        n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
    return ok;
}

int ha_strlcpy(char *dst, const char *src, size_t cap)
{
    return ha_strlcpyn(dst, cap, src, strlen(src));
}

int ha_slug(char *out, size_t cap, const char *in)
{
    size_t n = 0;
    int gap = 0;
    if (!cap)
        return 0;
    for (; *in; in++) {
        int c = lower((unsigned char)*in);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            if (gap && n) {
                if (n + 1 >= cap)
                    break;
                out[n++] = '_';
            }
            gap = 0;
            if (n + 1 >= cap)
                break;
            out[n++] = (char)c;
        } else {
            gap = 1;
        }
    }
    out[n] = 0;
    return n > 0 && !*in;
}
