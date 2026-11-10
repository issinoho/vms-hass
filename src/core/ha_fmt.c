/* ha_fmt.c - a bounded snprintf (ha_fmt.h).  No I/O: sprintf is used only for
   %f, into a local buffer sized for any double at the precision allowed. */
#include <stdio.h>
#include <string.h>

#include "ha_fmt.h"

struct out {
    char *p;
    size_t cap, n;                      /* n: the length the whole output would have */
};

static void put(struct out *o, char c)
{
    if (o->n + 1 < o->cap)
        o->p[o->n] = c;
    o->n++;
}

static void pad(struct out *o, char c, int k)
{
    while (k-- > 0)
        put(o, c);
}

/* body: the digits or text; prefix: a sign; zero: pad with zeros after the sign */
static void field(struct out *o, const char *prefix, const char *body, size_t blen,
                  int width, int left, int zero)
{
    int plen = (int)strlen(prefix), fill = width - plen - (int)blen;
    size_t i;
    if (!left && !zero)
        pad(o, ' ', fill);
    for (i = 0; prefix[i]; i++)
        put(o, prefix[i]);
    if (!left && zero)
        pad(o, '0', fill);
    for (i = 0; i < blen; i++)
        put(o, body[i]);
    if (left)
        pad(o, ' ', fill);
}

int ha_vsnprintf(char *out, size_t cap, const char *fmt, va_list ap)
{
    struct out o;
    const char *f;
    o.p = out;
    o.cap = cap;
    o.n = 0;
    for (f = fmt; *f; f++) {
        const char *start = f;
        int left = 0, zero = 0, plus = 0, space = 0, width = 0, prec = -1, lmod = 0;
        char num[48], sign[2];
        const char *body;
        size_t blen;
        if (*f != '%') {
            put(&o, *f);
            continue;
        }
        f++;
        for (;; f++) {
            if (*f == '-') left = 1;
            else if (*f == '0') zero = 1;
            else if (*f == '+') plus = 1;
            else if (*f == ' ') space = 1;
            else break;
        }
        if (*f == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left = 1;
                width = -width;
            }
            f++;
        } else {
            while (*f >= '0' && *f <= '9')
                width = width * 10 + (*f++ - '0');
        }
        if (*f == '.') {
            f++;
            prec = 0;
            if (*f == '*') {
                prec = va_arg(ap, int);
                f++;
            } else {
                while (*f >= '0' && *f <= '9')
                    prec = prec * 10 + (*f++ - '0');
            }
        }
        if (*f == 'l') {
            lmod = 'l';
            f++;
        } else if (*f == 'z') {
            lmod = 'z';
            f++;
        }
        if (left)
            zero = 0;
        sign[0] = sign[1] = 0;
        switch (*f) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
            unsigned long u;
            int base = *f == 'x' || *f == 'X' ? 16 : *f == 'o' ? 8 : 10;
            const char *digits = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char *e = num + sizeof num, *q = e;
            if (*f == 'd' || *f == 'i') {
                long v = lmod == 'l' ? va_arg(ap, long) : lmod == 'z' ? (long)va_arg(ap, size_t)
                                                                       : (long)va_arg(ap, int);
                if (v < 0) {
                    sign[0] = '-';
                    u = 0UL - (unsigned long)v;
                } else {
                    sign[0] = plus ? '+' : space ? ' ' : 0;
                    u = (unsigned long)v;
                }
            } else {
                u = lmod == 'l' ? va_arg(ap, unsigned long)
                    : lmod == 'z' ? (unsigned long)va_arg(ap, size_t)
                                  : (unsigned long)va_arg(ap, unsigned);
            }
            do {
                *--q = digits[u % (unsigned)base];
                u /= (unsigned)base;
            } while (u);
            if (prec >= 0) {
                zero = 0;
                if (prec == 0 && q == e - 1 && *q == '0')
                    q = e;              /* %.0d of 0 is empty */
                while (e - q < prec && q > num)
                    *--q = '0';
            }
            field(&o, sign, q, (size_t)(e - q), width, left, zero);
            break;
        }
        case 'c':
            num[0] = (char)va_arg(ap, int);
            field(&o, "", num, 1, width, left, 0);
            break;
        case 's':
            body = va_arg(ap, const char *);
            if (!body)
                body = "(null)";
            if (prec >= 0) {
                const char *z = memchr(body, 0, (size_t)prec);
                blen = z ? (size_t)(z - body) : (size_t)prec;
            } else {
                blen = strlen(body);
            }
            field(&o, "", body, blen, width, left, 0);
            break;
        case 'f': {
            /* any double, IEEE or VAX: at most 309 digits before the point */
            char tmp[400], spec[8];
            double v = va_arg(ap, double);
            int k = 0;
            if (prec < 0)
                prec = 6;
            if (prec > 40)
                prec = 40;
            spec[k++] = '%';
            if (plus) spec[k++] = '+';
            else if (space) spec[k++] = ' ';
            spec[k++] = '.';
            spec[k++] = '*';
            spec[k++] = 'f';
            spec[k] = 0;
            sprintf(tmp, spec, prec, v);
            body = tmp;
            if (*body == '-' || *body == '+' || *body == ' ') {
                sign[0] = *body;
                body++;
            }
            field(&o, sign, body, strlen(body), width, left, zero);
            break;
        }
        case '%':
            put(&o, '%');
            break;
        default:
            /* unknown or truncated: copy the specification as written */
            for (; start <= f && *start; start++)
                put(&o, *start);
            if (!*f)
                f--;
            break;
        }
    }
    if (cap)
        out[o.n < cap ? o.n : cap - 1] = 0;
    return (int)o.n;
}

int ha_snprintf(char *out, size_t cap, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = ha_vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    return n;
}
