/* ha_text.h - small text helpers.  From vms-beszel 47dbcf8 bz_text.h (D4). */
#ifndef HA_TEXT_H
#define HA_TEXT_H

#include <stddef.h>

/* trim ASCII whitespace in place over [*s, *s + *n) */
void ha_trim(const char **s, size_t *n);
int ha_ieq(const char *a, const char *b);                    /* ASCII case-insensitive */
int ha_ieqn(const char *a, size_t an, const char *b);
/* copy at most cap-1 bytes and terminate; returns 0 if truncated */
int ha_strlcpy(char *dst, const char *src, size_t cap);
int ha_strlcpyn(char *dst, size_t cap, const char *src, size_t n);
/* an MQTT topic level / HA object ID from any name: lower-case [a-z0-9_],
   runs of anything else become one '_', no leading or trailing '_'.
   "$1$DGA100:" -> "1_dga100".  Returns 0 if the result is empty or truncated. */
int ha_slug(char *out, size_t cap, const char *in);

#endif
