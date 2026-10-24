/* ha_mqtt.c - MQTT 3.1.1 packets (see ha_mqtt.h). */
#include <string.h>

#include "ha_mqtt.h"

static void put_u16(ha_buf *b, unsigned v)
{
    unsigned char p[2];
    p[0] = (unsigned char)((v >> 8) & 0xFF);
    p[1] = (unsigned char)(v & 0xFF);
    ha_buf_put(b, p, 2);
}

/* 1.5.3: a two-byte length, then the bytes; at most 65535 of them */
static int put_str(ha_buf *b, const char *s)
{
    size_t n = strlen(s);
    if (n > 65535)
        return 0;
    put_u16(b, (unsigned)n);
    ha_buf_put(b, s, n);
    return 1;
}

int ha_mqtt_put_length(ha_buf *out, unsigned long n)
{
    if (n > HA_MQTT_MAX_REMAINING)
        return 0;
    do {
        unsigned char d = (unsigned char)(n % 128);
        n /= 128;
        if (n)
            d |= 0x80;
        ha_buf_putc(out, d);
    } while (n);
    return !out->oom;
}

/* the fixed header and the variable part built in `body` */
static int finish(ha_buf *out, int first_byte, const ha_buf *body)
{
    if (body->oom || body->len > HA_MQTT_MAX_REMAINING)
        return 0;
    ha_buf_putc(out, first_byte);
    ha_mqtt_put_length(out, (unsigned long)body->len);
    ha_buf_put(out, body->p, body->len);
    return !out->oom;
}

int ha_mqtt_connect(ha_buf *out, const struct ha_mqtt_connect *c)
{
    ha_buf b;
    int flags = 0x02, ok;                   /* clean session (3.1.2.4) */
    if (!c->client_id || c->keepalive_s > 65535 || (c->password && !c->user) ||
        (c->will_topic && (!c->will_payload || c->will_qos < 0 || c->will_qos > 2)))
        return 0;
    if (c->user)
        flags |= 0x80;                      /* 3.1.2.8 */
    if (c->user && c->password)
        flags |= 0x40;                      /* 3.1.2.9 */
    if (c->will_topic) {
        flags |= 0x04 | (c->will_qos << 3); /* 3.1.2.5, 3.1.2.6 */
        if (c->will_retain)
            flags |= 0x20;                  /* 3.1.2.7 */
    }
    ha_buf_init(&b);
    put_str(&b, "MQTT");                    /* 3.1.2.1 */
    ha_buf_putc(&b, 4);                     /* 3.1.2.2: level 4 is 3.1.1 */
    ha_buf_putc(&b, flags);
    put_u16(&b, c->keepalive_s);            /* 3.1.2.10 */
    /* payload order (3.1.3): client ID, will topic, will message, user, password */
    ok = put_str(&b, c->client_id);
    if (c->will_topic)
        ok = ok && put_str(&b, c->will_topic) && put_str(&b, c->will_payload);
    if (c->user)
        ok = ok && put_str(&b, c->user);
    if (c->user && c->password)
        ok = ok && put_str(&b, c->password);
    ok = ok && finish(out, HA_MQTT_CONNECT << 4, &b);
    if (b.p)
        memset(b.p, 0, b.cap);              /* it held the password */
    ha_buf_free(&b);
    return ok;
}

int ha_mqtt_publish(ha_buf *out, const char *topic, const void *payload, size_t n,
                    int qos, int retain, unsigned packet_id)
{
    ha_buf b;
    int ok;
    /* 3.3.2.1: no wildcards in a topic name; 3.3.2.2: QoS > 0 needs an ID */
    if (!*topic || strchr(topic, '+') || strchr(topic, '#') || qos < 0 || qos > 2 ||
        (qos > 0 && (packet_id == 0 || packet_id > 65535)))
        return 0;
    ha_buf_init(&b);
    ok = put_str(&b, topic);
    if (qos > 0)
        put_u16(&b, packet_id);
    ha_buf_put(&b, payload, n);
    ok = ok && finish(out, (HA_MQTT_PUBLISH << 4) | (qos << 1) | (retain ? 1 : 0), &b);
    ha_buf_free(&b);
    return ok;
}

int ha_mqtt_subscribe(ha_buf *out, unsigned packet_id, const char *topic, int qos)
{
    ha_buf b;
    int ok;
    if (packet_id == 0 || packet_id > 65535 || !*topic || qos < 0 || qos > 2)
        return 0;
    ha_buf_init(&b);
    put_u16(&b, packet_id);                 /* 3.8.2 */
    ok = put_str(&b, topic);                /* 3.8.3: filter, then requested QoS */
    ha_buf_putc(&b, qos);
    ok = ok && finish(out, (HA_MQTT_SUBSCRIBE << 4) | 0x02, &b);  /* 3.8.1: flags 0010 */
    ha_buf_free(&b);
    return ok;
}

int ha_mqtt_puback(ha_buf *out, unsigned packet_id)
{
    unsigned char p[4];
    if (packet_id == 0 || packet_id > 65535)
        return 0;
    p[0] = HA_MQTT_PUBACK << 4;
    p[1] = 2;
    p[2] = (unsigned char)(packet_id >> 8);
    p[3] = (unsigned char)(packet_id & 0xFF);
    ha_buf_put(out, p, 4);
    return !out->oom;
}

int ha_mqtt_pingreq(ha_buf *out)
{
    unsigned char p[2] = { HA_MQTT_PINGREQ << 4, 0 };   /* 3.12 */
    ha_buf_put(out, p, 2);
    return !out->oom;
}

int ha_mqtt_disconnect(ha_buf *out)
{
    unsigned char p[2] = { HA_MQTT_DISCONNECT << 4, 0 };  /* 3.14 */
    ha_buf_put(out, p, 2);
    return !out->oom;
}

/* ---- decoding ---- */

void ha_mqtt_dec_init(ha_mqtt_dec *d)
{
    ha_buf_init(&d->in);
    d->consumed = 0;
}

void ha_mqtt_dec_free(ha_mqtt_dec *d)
{
    ha_buf_free(&d->in);
    d->consumed = 0;
}

void ha_mqtt_dec_reset(ha_mqtt_dec *d)
{
    ha_buf_clear(&d->in);
    d->consumed = 0;
}

int ha_mqtt_feed(ha_mqtt_dec *d, const void *p, size_t n)
{
    if (d->consumed) {                      /* drop what was handed out already */
        ha_buf_consume(&d->in, d->consumed);
        d->consumed = 0;
    }
    ha_buf_put(&d->in, p, n);
    return !d->in.oom;
}

int ha_mqtt_next(ha_mqtt_dec *d, struct ha_mqtt_packet *pkt)
{
    const unsigned char *p = d->in.p + d->consumed;
    size_t avail = d->in.len - d->consumed, i;
    unsigned long len = 0, mul = 1;
    if (avail < 2)
        return 0;
    /* 2.2.3: up to four length bytes, the high bit meaning "more" */
    for (i = 1;; i++) {
        if (i > 4)
            return -1;
        if (i >= avail)
            return 0;
        len += (unsigned long)(p[i] & 0x7F) * mul;
        mul *= 128;
        if (!(p[i] & 0x80))
            break;
    }
    if (len > HA_MQTT_MAX_INBOUND)
        return -1;
    if (avail - (i + 1) < len)
        return 0;
    pkt->type = p[0] >> 4;
    pkt->flags = p[0] & 0x0F;
    pkt->body = p + i + 1;
    pkt->len = (size_t)len;
    d->consumed += i + 1 + (size_t)len;
    return 1;
}

int ha_mqtt_parse_publish(const struct ha_mqtt_packet *pkt, struct ha_mqtt_publish_in *pub)
{
    size_t tl, off;
    if (pkt->type != HA_MQTT_PUBLISH || pkt->len < 2)
        return 0;
    pub->qos = (pkt->flags >> 1) & 3;
    pub->retain = pkt->flags & 1;
    if (pub->qos == 3)                      /* 3.3.1.2: reserved */
        return 0;
    tl = ((size_t)pkt->body[0] << 8) | pkt->body[1];
    off = 2 + tl;
    if (off > pkt->len || tl == 0)
        return 0;
    pub->topic = (const char *)pkt->body + 2;
    pub->topic_len = tl;
    pub->packet_id = 0;
    if (pub->qos > 0) {
        if (off + 2 > pkt->len)
            return 0;
        pub->packet_id = ((unsigned)pkt->body[off] << 8) | pkt->body[off + 1];
        off += 2;
        if (pub->packet_id == 0)
            return 0;
    }
    pub->payload = pkt->body + off;
    pub->payload_len = pkt->len - off;
    return 1;
}
