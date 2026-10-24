/* ha_json.h - a small streaming JSON writer.
 *
 * Commas are placed for the caller: each value written inside an object or an
 * array gets one if it is not the first.  Strings are escaped (RFC 8259 7);
 * numbers that JSON cannot represent (NaN, infinities) are written as null.
 */
#ifndef HA_JSON_H
#define HA_JSON_H

#include <stdint.h>

#include "ha_buf.h"

#define HA_JSON_DEPTH 8

typedef struct ha_json {
    ha_buf *b;
    int depth;
    int count[HA_JSON_DEPTH];           /* values written at each level (0 = top) */
    char kind[HA_JSON_DEPTH];           /* the closing bracket of each open level */
    int bad;                            /* nesting error: the output is unusable */
} ha_json;

void ha_json_init(ha_json *j, ha_buf *b);
void ha_json_obj(ha_json *j, const char *key);      /* key NULL at the top or in an array */
void ha_json_arr(ha_json *j, const char *key);
void ha_json_end(ha_json *j);                       /* closes the innermost object or array */
void ha_json_str(ha_json *j, const char *key, const char *v);
void ha_json_int(ha_json *j, const char *key, int64_t v);
/* fixed decimals, trailing zeros kept: 12.30 with decimals 2 */
void ha_json_num(ha_json *j, const char *key, double v, int decimals);
void ha_json_bool(ha_json *j, const char *key, int v);
void ha_json_null(ha_json *j, const char *key);
/* 1 if the document is complete and was written without error */
int ha_json_ok(const ha_json *j);

/* append s as a JSON string literal, quotes included */
void ha_json_quote(ha_buf *b, const char *s);

#endif
