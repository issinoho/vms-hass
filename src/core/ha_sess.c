/* ha_sess.c - one MQTT session (see ha_sess.h). */
#include <stdio.h>
#include <string.h>

#include "ha_sess.h"

void ha_sess_init(ha_sess *s)
{
    memset(s, 0, sizeof *s);
    ha_buf_init(&s->out);
    ha_mqtt_dec_init(&s->dec);
    s->connack_code = -1;
}

void ha_sess_free(ha_sess *s)
{
    if (s->out.p)
        memset(s->out.p, 0, s->out.cap);    /* the CONNECT held the password */
    ha_buf_free(&s->out);
    ha_mqtt_dec_free(&s->dec);
}

static int fail(ha_sess *s, const char *why)
{
    snprintf(s->err, sizeof s->err, "%s", why);
    s->state = HA_SESS_DOWN;
    return 0;
}

static unsigned new_id(ha_sess *s)
{
    if (++s->next_id > 65535)
        s->next_id = 1;
    return s->next_id;
}

static uint64_t keepalive_ms(const ha_sess *s)
{
    return (uint64_t)s->cfg.keepalive_s * 1000;
}

/* how long a QoS 1 PUBLISH or a SUBSCRIBE may wait for its answer */
static uint64_t ack_ms(const ha_sess *s)
{
    uint64_t k = keepalive_ms(s);
    return k > 30000 ? k : 30000;
}

int ha_sess_begin(ha_sess *s, const struct ha_sess_cfg *cfg, uint64_t now)
{
    struct ha_mqtt_connect c;
    if (s->out.p)
        memset(s->out.p, 0, s->out.cap);
    ha_buf_clear(&s->out);
    ha_mqtt_dec_reset(&s->dec);
    s->cfg = *cfg;
    s->ninflight = 0;
    s->ping_out = 0;
    s->just_up = 0;
    s->err[0] = 0;
    memset(&c, 0, sizeof c);
    c.client_id = cfg->client_id;
    c.user = cfg->user;
    c.password = cfg->user ? cfg->password : NULL;
    c.keepalive_s = cfg->keepalive_s;
    c.will_topic = cfg->will_topic;
    c.will_payload = cfg->will_payload;
    c.will_qos = 1;
    c.will_retain = 1;
    if (!ha_mqtt_connect(&s->out, &c))
        return fail(s, "cannot build CONNECT (a setting is too long?)");
    s->state = HA_SESS_CONNECTING;
    s->began = s->last_send = now;
    return 1;
}

static const char *connack_text(int rc)
{
    switch (rc) {                       /* MQTT 3.2.2.3 */
    case 1: return "unacceptable protocol version";
    case 2: return "client identifier rejected";
    case 3: return "server unavailable";
    case 4: return "bad user name or password";
    case 5: return "not authorised (check MQTT_USER and the password file)";
    default: return "refused";
    }
}

static int acked(ha_sess *s, unsigned id)
{
    int i;
    for (i = 0; i < s->ninflight; i++) {
        if (s->inflight[i].id == id) {
            s->inflight[i] = s->inflight[--s->ninflight];
            return 1;
        }
    }
    return 0;                           /* a stray ack: harmless */
}

static int on_packet(ha_sess *s, const struct ha_mqtt_packet *pkt, uint64_t now)
{
    (void)now;
    if (s->state == HA_SESS_CONNECTING) {
        /* 3.2: the first packet from the server must be CONNACK */
        if (pkt->type != HA_MQTT_CONNACK || pkt->len != 2)
            return fail(s, "the broker's first packet was not a CONNACK");
        if (pkt->body[1] != 0) {
            char e[120];
            s->connack_code = pkt->body[1];
            snprintf(e, sizeof e, "the broker refused the connection: %s (CONNACK %d)",
                     connack_text(pkt->body[1]), pkt->body[1]);
            return fail(s, e);
        }
        s->connack_code = -1;
        s->state = HA_SESS_UP;
        s->just_up = 1;
        return 1;
    }
    switch (pkt->type) {
    case HA_MQTT_PUBACK:
        if (pkt->len != 2)
            return fail(s, "malformed PUBACK");
        acked(s, ((unsigned)pkt->body[0] << 8) | pkt->body[1]);
        return 1;
    case HA_MQTT_SUBACK:
        /* 3.9.3: one return code per filter; 0x80 is a failure */
        if (pkt->len < 3)
            return fail(s, "malformed SUBACK");
        acked(s, ((unsigned)pkt->body[0] << 8) | pkt->body[1]);
        if (pkt->body[2] == 0x80)
            return fail(s, "the broker refused the subscription to HA's status topic");
        return 1;
    case HA_MQTT_PINGRESP:
        s->ping_out = 0;
        return 1;
    case HA_MQTT_PUBLISH: {
        struct ha_mqtt_publish_in pub;
        if (!ha_mqtt_parse_publish(pkt, &pub))
            return fail(s, "malformed PUBLISH from the broker");
        if (pub.qos == 1 && !ha_mqtt_puback(&s->out, pub.packet_id))
            return fail(s, "out of memory");
        if (pub.qos == 2)
            return fail(s, "the broker sent QoS 2, which was not asked for");
        if (s->cfg.status_topic && pub.topic_len == strlen(s->cfg.status_topic) &&
            memcmp(pub.topic, s->cfg.status_topic, pub.topic_len) == 0 &&
            pub.payload_len == 6 && memcmp(pub.payload, "online", 6) == 0)
            s->ha_online++;
        return 1;
    }
    default:
        return fail(s, "unexpected packet from the broker");
    }
}

int ha_sess_input(ha_sess *s, const void *p, size_t n, uint64_t now)
{
    struct ha_mqtt_packet pkt;
    int r;
    if (s->state == HA_SESS_DOWN)
        return 0;
    if (!ha_mqtt_feed(&s->dec, p, n))
        return fail(s, "out of memory");
    while ((r = ha_mqtt_next(&s->dec, &pkt)) == 1)
        if (!on_packet(s, &pkt, now))
            return 0;
    if (r < 0)
        return fail(s, "malformed or oversized packet from the broker");
    return 1;
}

int ha_sess_tick(ha_sess *s, uint64_t now)
{
    int i;
    if (s->state == HA_SESS_DOWN)
        return 0;
    if (s->state == HA_SESS_CONNECTING) {
        if (now - s->began >= HA_SESS_CONNACK_MS)
            return fail(s, "no CONNACK from the broker");
        return 1;
    }
    for (i = 0; i < s->ninflight; i++)
        if (now - s->inflight[i].at >= ack_ms(s))
            return fail(s, "the broker did not acknowledge a message");
    if (s->cfg.keepalive_s) {
        if (s->ping_out && now - s->ping_sent >= keepalive_ms(s))
            return fail(s, "no PINGRESP from the broker");
        /* 3.1.2.10: something must be sent within each keepalive period; ping at
           half of it, so a late timer can't make us miss it */
        if (!s->ping_out && now - s->last_send >= keepalive_ms(s) / 2) {
            if (!ha_mqtt_pingreq(&s->out))
                return fail(s, "out of memory");
            s->ping_out = 1;
            s->ping_sent = s->last_send = now;
        }
    }
    return 1;
}

uint64_t ha_sess_wake_in(const ha_sess *s, uint64_t now)
{
    uint64_t best = 60000, t;
    int i;
#define SOONER(deadline) do { t = (deadline) > now ? (deadline) - now : 0; if (t < best) best = t; } while (0)
    if (s->state == HA_SESS_CONNECTING) {
        SOONER(s->began + HA_SESS_CONNACK_MS);
        return best;
    }
    if (s->state != HA_SESS_UP)
        return best;
    for (i = 0; i < s->ninflight; i++)
        SOONER(s->inflight[i].at + ack_ms(s));
    if (s->cfg.keepalive_s) {
        if (s->ping_out)
            SOONER(s->ping_sent + keepalive_ms(s));
        else
            SOONER(s->last_send + keepalive_ms(s) / 2);
    }
#undef SOONER
    return best;
}

int ha_sess_publish(ha_sess *s, const char *topic, const void *payload, size_t n,
                    int qos, int retain, uint64_t now)
{
    unsigned id = 0;
    if (s->state != HA_SESS_UP)
        return 0;
    if (qos > 1)
        return fail(s, "QoS 2 is not used");
    if (qos == 1) {
        if (s->ninflight == HA_SESS_INFLIGHT)
            return fail(s, "too many unacknowledged messages");
        id = new_id(s);
    }
    if (!ha_mqtt_publish(&s->out, topic, payload, n, qos, retain, id))
        return fail(s, "cannot build PUBLISH");
    if (qos == 1) {
        s->inflight[s->ninflight].id = id;
        s->inflight[s->ninflight].at = now;
        s->ninflight++;
    }
    s->last_send = now;
    return 1;
}

int ha_sess_subscribe(ha_sess *s, const char *topic, int qos, uint64_t now)
{
    unsigned id;
    if (s->state != HA_SESS_UP)
        return 0;
    if (s->ninflight == HA_SESS_INFLIGHT)
        return fail(s, "too many unacknowledged messages");
    id = new_id(s);
    if (!ha_mqtt_subscribe(&s->out, id, topic, qos))
        return fail(s, "cannot build SUBSCRIBE");
    s->inflight[s->ninflight].id = id;
    s->inflight[s->ninflight].at = now;
    s->ninflight++;
    s->last_send = now;
    return 1;
}

void ha_sess_end(ha_sess *s)
{
    if (s->state == HA_SESS_UP)
        ha_mqtt_disconnect(&s->out);
    s->state = HA_SESS_DOWN;
}

void ha_sess_sent(ha_sess *s, size_t n)
{
    ha_buf_consume(&s->out, n);
}
