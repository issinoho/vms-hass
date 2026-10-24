/* ha_mqtt.h - MQTT 3.1.1 packets: encoding and an incremental decoder.
 *
 * No I/O.  Encoders append one complete packet to a buffer; the decoder takes
 * bytes as they arrive, in any split, and hands back whole packets.  Section
 * numbers are those of MQTT Version 3.1.1 (OASIS Standard, 29 October 2014);
 * docs/MQTT.md has the subset this agent uses.
 */
#ifndef HA_MQTT_H
#define HA_MQTT_H

#include <stddef.h>

#include "ha_buf.h"

/* control packet types, the high nibble of byte 1 (2.2.1) */
#define HA_MQTT_CONNECT     1
#define HA_MQTT_CONNACK     2
#define HA_MQTT_PUBLISH     3
#define HA_MQTT_PUBACK      4
#define HA_MQTT_SUBSCRIBE   8
#define HA_MQTT_SUBACK      9
#define HA_MQTT_PINGREQ     12
#define HA_MQTT_PINGRESP    13
#define HA_MQTT_DISCONNECT  14

/* the largest remaining length the encoding allows (2.2.3) */
#define HA_MQTT_MAX_REMAINING 268435455UL
/* what we accept from the broker: SUBACKs, PUBACKs and homeassistant/status */
#define HA_MQTT_MAX_INBOUND 65536UL

struct ha_mqtt_connect {
    const char *client_id;
    const char *user;                   /* NULL: no user name or password */
    const char *password;
    unsigned keepalive_s;               /* 0 turns keepalive off (3.1.2.10) */
    const char *will_topic;             /* NULL: no will */
    const char *will_payload;
    int will_qos;
    int will_retain;
};

/* each returns 0 (and leaves out->oom or the buffer unchanged) on a bad argument */
int ha_mqtt_connect(ha_buf *out, const struct ha_mqtt_connect *c);
int ha_mqtt_publish(ha_buf *out, const char *topic, const void *payload, size_t n,
                    int qos, int retain, unsigned packet_id);
int ha_mqtt_subscribe(ha_buf *out, unsigned packet_id, const char *topic, int qos);
int ha_mqtt_puback(ha_buf *out, unsigned packet_id);
int ha_mqtt_pingreq(ha_buf *out);
int ha_mqtt_disconnect(ha_buf *out);

/* append the remaining-length encoding of n (1 to 4 bytes) */
int ha_mqtt_put_length(ha_buf *out, unsigned long n);

/* ---- decoding ---- */

struct ha_mqtt_packet {
    int type;                           /* HA_MQTT_* */
    int flags;                          /* low nibble of byte 1 */
    const unsigned char *body;          /* valid until the next ha_mqtt_next */
    size_t len;
};

typedef struct ha_mqtt_dec {
    ha_buf in;
    size_t consumed;                    /* bytes of `in` already handed out */
} ha_mqtt_dec;

void ha_mqtt_dec_init(ha_mqtt_dec *d);
void ha_mqtt_dec_free(ha_mqtt_dec *d);
void ha_mqtt_dec_reset(ha_mqtt_dec *d);
/* add received bytes; 0 if out of memory */
int ha_mqtt_feed(ha_mqtt_dec *d, const void *p, size_t n);
/* 1 a packet, 0 need more bytes, -1 malformed or too large (close the connection) */
int ha_mqtt_next(ha_mqtt_dec *d, struct ha_mqtt_packet *pkt);

/* an inbound PUBLISH (3.3), its fields pointing into the packet body */
struct ha_mqtt_publish_in {
    const char *topic;                  /* not NUL-terminated */
    size_t topic_len;
    int qos;
    int retain;
    unsigned packet_id;                 /* 0 at QoS 0 */
    const unsigned char *payload;
    size_t payload_len;
};
/* 0 if the PUBLISH is malformed */
int ha_mqtt_parse_publish(const struct ha_mqtt_packet *pkt, struct ha_mqtt_publish_in *pub);

#endif
