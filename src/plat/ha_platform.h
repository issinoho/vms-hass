/* ha_platform.h - the whole operating-system surface of the agent.
 *
 * No OpenVMS (or Linux) types appear here.  Implementations:
 *   ha_net_ossl.c          sockets and TLS on OpenSSL (SSL3 on OpenVMS), shared
 *   vms/ha_plat_vms.c      system services (Phase 2)
 *   stub/ha_plat_stub.c    Linux: scripted counters for the tests
 */
#ifndef HA_PLATFORM_H
#define HA_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#include "ha_buf.h"
#include "ha_sys.h"

/* ---- time ---- */
uint64_t ha_plat_now_ms(void);          /* monotonic */
void ha_plat_sleep_ms(unsigned ms);
unsigned ha_plat_random(void);          /* for backoff jitter: not for secrets */

/* ---- files ---- */
int ha_plat_read_file(const char *path, ha_buf *out);
/* atomically replace a small file, readable by the owner only */
int ha_plat_write_file(const char *path, const void *p, size_t n);
int ha_plat_exists(const char *path);
/* delete it (every version, on OpenVMS); 1 if it is gone */
int ha_plat_remove(const char *path);
/* 1 if group or world can read it (the password file must not be) */
int ha_plat_file_exposed(const char *path);
/* "<dir>/<name>" in the platform's syntax */
void ha_plat_data_path(char *out, size_t cap, const char *dir, const char *name);
const char *ha_plat_default_config(void);
const char *ha_plat_default_data_dir(void);
const char *ha_plat_default_ca_file(void);  /* NULL: the system store */

/* ---- configuration overrides (ha_conf.h) ---- */
int ha_plat_conf_override(void *ctx, const char *name, ha_buf *out);

/* ---- logging ---- */
void ha_plat_log(int level, const char *msg);

/* ---- the system ---- */
int ha_plat_init(void);
int ha_plat_identity(struct ha_identity *id);
int ha_plat_sample(struct ha_sample *s);
/* called about once a second: counters that must be read often (LAN) */
void ha_plat_poll(void);
int ha_plat_stop_requested(void);
/* 0, with the reason, if this process must not run the agent: authorized
   privileges beyond NETMBX and TMPMBX, or a system-group UIC (D9) */
int ha_plat_check_account(char *err, size_t errlen);

/* ---- network (ha_net_ossl.c) ---- */
/* n bytes from OpenSSL's generator: for secrets; 0 on failure */
int ha_net_random(void *p, size_t n);
typedef struct ha_sock ha_sock;
ha_sock *ha_net_open(const char *host, const char *port, int tls, const char *ca_file,
                     unsigned timeout_ms, char *err, size_t errlen);
int ha_net_send(ha_sock *s, const void *p, size_t n);     /* all or nothing: 1/0 */
/* >0 bytes, 0 the peer closed, -1 error, -2 nothing yet (a partial TLS record) */
long ha_net_recv(ha_sock *s, void *p, size_t cap);
/* 1 readable, 0 timeout, -1 error */
int ha_net_wait(ha_sock *s, unsigned timeout_ms);
void ha_net_close(ha_sock *s);

#endif
