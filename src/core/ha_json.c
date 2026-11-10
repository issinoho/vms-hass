/* ha_json.c - a small streaming JSON writer (see ha_json.h). */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ha_json.h"

/* the largest magnitude written as a number: VAX D_float stops at 1.7e38, where
   1e300 cannot even be written (NOTES.md 15) */
#if DBL_MAX_10_EXP >= 300
#define NUM_LIMIT 1e300
#else
#define NUM_LIMIT DBL_MAX
#endif

void ha_json_init(ha_json *j, ha_buf *b)
{
    memset(j, 0, sizeof *j);
    j->b = b;
}

void ha_json_quote(ha_buf *b, const char *s)
{
    static const char hex[] = "0123456789abcdef";
    ha_buf_putc(b, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  ha_buf_puts(b, "\\\""); break;
        case '\\': ha_buf_puts(b, "\\\\"); break;
        case '\n': ha_buf_puts(b, "\\n"); break;
        case '\r': ha_buf_puts(b, "\\r"); break;
        case '\t': ha_buf_puts(b, "\\t"); break;
        default:
            if (c < 0x20) {
                char u[7] = { '\\', 'u', '0', '0', 0, 0, 0 };
                u[4] = hex[c >> 4];
                u[5] = hex[c & 15];
                ha_buf_puts(b, u);
            } else {
                ha_buf_putc(b, c);      /* UTF-8 passes through */
            }
        }
    }
    ha_buf_putc(b, '"');
}

/* the comma and the key before a value */
static void lead(ha_json *j, const char *key)
{
    if (j->depth == 0) {
        if (j->count[0]++)
            j->bad = 1;                 /* a second top-level value */
    } else if (j->count[j->depth]++) {
        ha_buf_putc(j->b, ',');
    }
    if (key) {
        ha_json_quote(j->b, key);
        ha_buf_putc(j->b, ':');
    }
}

static void opener(ha_json *j, const char *key, int c)
{
    lead(j, key);
    if (j->depth + 1 >= HA_JSON_DEPTH) {
        j->bad = 1;
        return;
    }
    ha_buf_putc(j->b, c);
    j->depth++;
    j->kind[j->depth] = (char)(c == '{' ? '}' : ']');
    j->count[j->depth] = 0;
}

void ha_json_obj(ha_json *j, const char *key)
{
    opener(j, key, '{');
}

void ha_json_arr(ha_json *j, const char *key)
{
    opener(j, key, '[');
}

void ha_json_end(ha_json *j)
{
    if (j->depth == 0) {
        j->bad = 1;
        return;
    }
    ha_buf_putc(j->b, j->kind[j->depth]);
    j->depth--;
}

void ha_json_str(ha_json *j, const char *key, const char *v)
{
    lead(j, key);
    ha_json_quote(j->b, v);
}

void ha_json_int(ha_json *j, const char *key, ha_i64 v)
{
    lead(j, key);
#ifdef HA_WIDE_DOUBLE
    ha_buf_printf(j->b, "%.0f", v);
#else
    ha_buf_printf(j->b, "%lld", (long long)v);
#endif
}

void ha_json_num(ha_json *j, const char *key, double v, int decimals)
{
    lead(j, key);
    if (v != v || v > NUM_LIMIT || v < -NUM_LIMIT) {
        ha_buf_puts(j->b, "null");
        return;
    }
    if (decimals < 0)
        decimals = 0;
    if (decimals > 6)
        decimals = 6;
    {
        char tmp[64];
        snprintf(tmp, sizeof tmp, "%.*f", decimals, v);
        if (strcmp(tmp, "-0") == 0 || strncmp(tmp, "-0.", 3) == 0) {
            /* -0.00 reads oddly in HA: a rounded negative zero is zero */
            const char *q = tmp + 1;
            int zero = 1;
            for (; *q; q++)
                if (*q != '0' && *q != '.')
                    zero = 0;
            if (zero)
                memmove(tmp, tmp + 1, strlen(tmp));
        }
        ha_buf_puts(j->b, tmp);
    }
}

void ha_json_bool(ha_json *j, const char *key, int v)
{
    lead(j, key);
    ha_buf_puts(j->b, v ? "true" : "false");
}

void ha_json_null(ha_json *j, const char *key)
{
    lead(j, key);
    ha_buf_puts(j->b, "null");
}

int ha_json_ok(const ha_json *j)
{
    return !j->bad && !j->b->oom && j->depth == 0;
}
