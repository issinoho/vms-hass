/* ha_buf.h - growable byte buffer.  From vms-beszel 47dbcf8 bz_buf.h (D4).
 *
 * Every write either succeeds or sets `oom`; callers check `oom` once at the
 * end of a sequence of writes instead of after each one.  The buffer always
 * keeps a NUL after `len` so text content can be used as a C string.
 */
#ifndef HA_BUF_H
#define HA_BUF_H

#include <stddef.h>

typedef struct ha_buf {
    unsigned char *p;
    size_t len;
    size_t cap;
    int oom;
} ha_buf;

void ha_buf_init(ha_buf *b);
void ha_buf_free(ha_buf *b);
void ha_buf_clear(ha_buf *b);
int ha_buf_reserve(ha_buf *b, size_t extra);
void ha_buf_put(ha_buf *b, const void *data, size_t n);
void ha_buf_putc(ha_buf *b, int c);
void ha_buf_puts(ha_buf *b, const char *s);
void ha_buf_printf(ha_buf *b, const char *fmt, ...);
void ha_buf_consume(ha_buf *b, size_t n);   /* drop n bytes from the front */
void ha_buf_set(ha_buf *b, const void *data, size_t n);

#endif
