/* ha_conf.h - agent configuration.  From vms-beszel 47dbcf8 bz_conf.h (D4).
 *
 * A file of NAME=value lines.  Blank lines and lines starting with '#' are
 * ignored; the value may be wrapped in double quotes.  An unknown name or a
 * malformed line is an error, reported with its line number, so a typo never
 * fails silently.  A platform override (logical name HASS$<NAME> on OpenVMS,
 * environment HASS_AGENT_<NAME> on Linux) takes precedence over the file.
 * The settings are listed in docs/CONFIG.md.
 */
#ifndef HA_CONF_H
#define HA_CONF_H

#include <stddef.h>

#include "ha_buf.h"

#define HA_CONF_MAX 32

typedef int (*ha_conf_override_fn)(void *ctx, const char *name, ha_buf *out);

typedef struct ha_conf {
    int n;
    char *name[HA_CONF_MAX];
    char *value[HA_CONF_MAX];
    ha_conf_override_fn override;
    void *override_ctx;
    ha_buf scratch;                         /* the last override value returned */
} ha_conf;

void ha_conf_init(ha_conf *c, ha_conf_override_fn fn, void *ctx);
void ha_conf_free(ha_conf *c);
/* parse a whole file's text; on error returns 0 and writes a message to err */
int ha_conf_parse(ha_conf *c, const char *text, size_t len, char *err, size_t errlen);
/* the value, or NULL if unset; valid until the next ha_conf_get */
const char *ha_conf_get(ha_conf *c, const char *name);
int ha_conf_known(const char *name);

/* a secret file (MQTT_PASSWORD_FILE): one non-comment line; 0 if more than one */
int ha_conf_secret_file(const char *text, size_t len, ha_buf *secret);

#endif
