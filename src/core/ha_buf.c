/* ha_buf.c - growable byte buffer (see ha_buf.h).  From vms-beszel 47dbcf8 (D4). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ha_buf.h"
#include "ha_port.h"

void ha_buf_init(ha_buf *b)
{
    b->p = NULL;
    b->len = b->cap = 0;
    b->oom = 0;
}

void ha_buf_free(ha_buf *b)
{
    free(b->p);
    ha_buf_init(b);
}

void ha_buf_clear(ha_buf *b)
{
    b->len = 0;
    b->oom = 0;
    if (b->p)
        b->p[0] = 0;
}

int ha_buf_reserve(ha_buf *b, size_t extra)
{
    size_t need, cap;
    unsigned char *np;
    if (b->oom)
        return 0;
    if (extra > (size_t)-1 - b->len - 1) {
        b->oom = 1;
        return 0;
    }
    need = b->len + extra + 1;              /* +1 for the trailing NUL */
    if (need <= b->cap)
        return 1;
    cap = b->cap ? b->cap : 256;
    while (cap < need)
        cap = cap > ((size_t)-1) / 2 ? need : cap * 2;
    np = realloc(b->p, cap);
    if (!np) {
        b->oom = 1;
        return 0;
    }
    b->p = np;
    b->cap = cap;
    return 1;
}

void ha_buf_put(ha_buf *b, const void *data, size_t n)
{
    if (!ha_buf_reserve(b, n))
        return;
    if (n)
        memcpy(b->p + b->len, data, n);
    b->len += n;
    b->p[b->len] = 0;
}

void ha_buf_putc(ha_buf *b, int c)
{
    unsigned char ch = (unsigned char)c;
    ha_buf_put(b, &ch, 1);
}

void ha_buf_puts(ha_buf *b, const char *s)
{
    ha_buf_put(b, s, strlen(s));
}

void ha_buf_printf(ha_buf *b, const char *fmt, ...)
{
    va_list ap;
    int n;
    char small[256];
    va_start(ap, fmt);
    n = vsnprintf(small, sizeof small, fmt, ap);
    va_end(ap);
    if (n < 0) {
        b->oom = 1;
        return;
    }
    if ((size_t)n < sizeof small) {
        ha_buf_put(b, small, (size_t)n);
        return;
    }
    if (!ha_buf_reserve(b, (size_t)n))
        return;
    va_start(ap, fmt);
    vsnprintf((char *)b->p + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

void ha_buf_consume(ha_buf *b, size_t n)
{
    if (n >= b->len) {
        b->len = 0;
    } else {
        memmove(b->p, b->p + n, b->len - n);
        b->len -= n;
    }
    if (b->p)
        b->p[b->len] = 0;
}

void ha_buf_set(ha_buf *b, const void *data, size_t n)
{
    ha_buf_clear(b);
    ha_buf_put(b, data, n);
}
