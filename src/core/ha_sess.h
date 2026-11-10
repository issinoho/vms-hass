/* ha_sess.h - one MQTT session with the broker, as a state machine.
 *
 * No I/O and no clock of its own: the caller passes the time in, feeds the
 * bytes it receives, and sends whatever accumulates in `out`.  When a call
 * returns 0 the connection must be closed (`err` says why), and a new session
 * begun later with ha_sess_begin.
 */
#ifndef HA_SESS_H
#define HA_SESS_H

#include "ha_port.h"

#include "ha_buf.h"
#include "ha_mqtt.h"

#define HA_SESS_DOWN        0
#define HA_SESS_CONNECTING  1           /* CONNECT sent, waiting for CONNACK */
#define HA_SESS_UP          2

#define HA_SESS_INFLIGHT 32
#define HA_SESS_CONNACK_MS 30000

struct ha_sess_cfg {
    const char *client_id;
    const char *user;                   /* NULL: anonymous */
    const char *password;
    unsigned keepalive_s;
    const char *will_topic;             /* retained, QoS 1 */
    const char *will_payload;
    const char *status_topic;           /* HA's birth topic: "homeassistant/status" */
};

typedef struct ha_sess {
    int state;
    ha_buf out;                         /* bytes to send */
    ha_mqtt_dec dec;
    struct ha_sess_cfg cfg;
    ha_u64 began;                     /* when CONNECT was queued */
    ha_u64 last_send;
    ha_u64 ping_sent;
    int ping_out;
    unsigned next_id;
    int ninflight;
    struct { unsigned id; ha_u64 at; } inflight[HA_SESS_INFLIGHT];
    int connack_code;                   /* the last refusal, or -1 */
    int just_up;                        /* set by the CONNACK; the caller clears it */
    int ha_online;                      /* count of "online" on the status topic */
    char err[160];
} ha_sess;

void ha_sess_init(ha_sess *s);
void ha_sess_free(ha_sess *s);
/* queue CONNECT; the strings in cfg must outlive the session */
int ha_sess_begin(ha_sess *s, const struct ha_sess_cfg *cfg, ha_u64 now);
int ha_sess_input(ha_sess *s, const void *p, size_t n, ha_u64 now);
/* timers: CONNACK, keepalive, PINGRESP and PUBACK deadlines */
int ha_sess_tick(ha_sess *s, ha_u64 now);
/* ms until ha_sess_tick has something to do */
ha_u64 ha_sess_wake_in(const ha_sess *s, ha_u64 now);
int ha_sess_publish(ha_sess *s, const char *topic, const void *payload, size_t n,
                    int qos, int retain, ha_u64 now);
int ha_sess_subscribe(ha_sess *s, const char *topic, int qos, ha_u64 now);
/* queue DISCONNECT (the broker then drops the will) and go down */
void ha_sess_end(ha_sess *s);
/* bytes of out were sent */
void ha_sess_sent(ha_sess *s, size_t n);

#endif
