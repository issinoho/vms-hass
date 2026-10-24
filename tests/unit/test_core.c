/* test_core.c - unit tests for src/core (no I/O, no network).
 *
 * Expected bytes are assembled by hand from the MQTT 3.1.1 sections cited, not
 * produced by the code under test.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ha_agent.h"
#include "ha_conf.h"
#include "ha_json.h"
#include "ha_model.h"
#include "ha_mqtt.h"
#include "ha_sess.h"
#include "ha_text.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { const char *a_ = (a), *b_ = (b); checks++; \
    if (strcmp(a_, b_) != 0) { fails++; \
    fprintf(stderr, "%s:%d: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, a_, b_); } } while (0)

static int bytes_eq(const ha_buf *b, const unsigned char *want, size_t n)
{
    size_t i;
    if (b->len != n) {
        fprintf(stderr, "  length %lu, want %lu\n", (unsigned long)b->len, (unsigned long)n);
        return 0;
    }
    for (i = 0; i < n; i++)
        if (b->p[i] != want[i]) {
            fprintf(stderr, "  byte %lu: %02x, want %02x\n", (unsigned long)i, b->p[i], want[i]);
            return 0;
        }
    return 1;
}

/* ---- MQTT encoding ---- */

static void test_remaining_length(void)
{
    /* MQTT 2.2.3, table 2.4: the boundaries of each encoded length */
    static const struct { unsigned long n; unsigned char b[4]; size_t len; } v[] = {
        { 0, { 0x00 }, 1 },
        { 127, { 0x7F }, 1 },
        { 128, { 0x80, 0x01 }, 2 },
        { 16383, { 0xFF, 0x7F }, 2 },
        { 16384, { 0x80, 0x80, 0x01 }, 3 },
        { 2097151, { 0xFF, 0xFF, 0x7F }, 3 },
        { 2097152, { 0x80, 0x80, 0x80, 0x01 }, 4 },
        { 268435455, { 0xFF, 0xFF, 0xFF, 0x7F }, 4 },
    };
    size_t i;
    ha_buf b;
    ha_buf_init(&b);
    for (i = 0; i < sizeof v / sizeof v[0]; i++) {
        ha_buf_clear(&b);
        CHECK(ha_mqtt_put_length(&b, v[i].n));
        CHECK(bytes_eq(&b, v[i].b, v[i].len));
    }
    ha_buf_clear(&b);
    CHECK(!ha_mqtt_put_length(&b, 268435456UL));
    ha_buf_free(&b);
}

static void test_connect(void)
{
    /* 3.1: client "c1", will "w/t" = "off" (QoS 1, retained), user "u", password
       "p", keepalive 60, clean session */
    static const unsigned char want[] = {
        0x10, 30,                           /* 10 bytes of header + 20 of payload */
        0, 4, 'M', 'Q', 'T', 'T', 4,
        0xC0 | 0x20 | 0x08 | 0x04 | 0x02,   /* user, password, will retain, QoS 1, will, clean */
        0, 60,
        0, 2, 'c', '1',
        0, 3, 'w', '/', 't',
        0, 3, 'o', 'f', 'f',
        0, 1, 'u',
        0, 1, 'p',
    };
    static const unsigned char anon[] = {
        0x10, 14, 0, 4, 'M', 'Q', 'T', 'T', 4, 0x02, 0, 0, 0, 2, 'c', '1',
    };
    struct ha_mqtt_connect c;
    ha_buf b;
    ha_buf_init(&b);
    memset(&c, 0, sizeof c);
    c.client_id = "c1";
    c.user = "u";
    c.password = "p";
    c.keepalive_s = 60;
    c.will_topic = "w/t";
    c.will_payload = "off";
    c.will_qos = 1;
    c.will_retain = 1;
    CHECK(ha_mqtt_connect(&b, &c));
    CHECK(bytes_eq(&b, want, sizeof want));

    memset(&c, 0, sizeof c);
    c.client_id = "c1";
    ha_buf_clear(&b);
    CHECK(ha_mqtt_connect(&b, &c));
    CHECK(bytes_eq(&b, anon, sizeof anon));

    c.password = "p";                   /* 3.1.2.9: no password without a user */
    ha_buf_clear(&b);
    CHECK(!ha_mqtt_connect(&b, &c));
    ha_buf_free(&b);
}

static void test_publish_subscribe(void)
{
    static const unsigned char q0[] = { 0x31, 7, 0, 3, 'a', '/', 'b', 'h', 'i' };
    static const unsigned char q1[] = { 0x32, 9, 0, 3, 'a', '/', 'b', 0x12, 0x34, 'h', 'i' };
    static const unsigned char sub[] = { 0x82, 8, 0, 7, 0, 3, 'h', '/', 's', 0 };
    static const unsigned char ack[] = { 0x40, 2, 0x01, 0x02 };
    static const unsigned char ping[] = { 0xC0, 0 };
    static const unsigned char disc[] = { 0xE0, 0 };
    ha_buf b;
    ha_buf_init(&b);
    CHECK(ha_mqtt_publish(&b, "a/b", "hi", 2, 0, 1, 0));    /* 3.3.1: retain bit */
    CHECK(bytes_eq(&b, q0, sizeof q0));
    ha_buf_clear(&b);
    CHECK(ha_mqtt_publish(&b, "a/b", "hi", 2, 1, 0, 0x1234));
    CHECK(bytes_eq(&b, q1, sizeof q1));
    ha_buf_clear(&b);
    CHECK(!ha_mqtt_publish(&b, "a/+", "x", 1, 0, 0, 0));    /* 3.3.2.1 */
    CHECK(!ha_mqtt_publish(&b, "a/#", "x", 1, 0, 0, 0));
    CHECK(!ha_mqtt_publish(&b, "", "x", 1, 0, 0, 0));
    CHECK(!ha_mqtt_publish(&b, "a", "x", 1, 1, 0, 0));      /* 3.3.2.2: QoS 1 needs an ID */
    CHECK(b.len == 0);
    CHECK(ha_mqtt_subscribe(&b, 7, "h/s", 0));              /* 3.8.1: flags 0010 */
    CHECK(bytes_eq(&b, sub, sizeof sub));
    ha_buf_clear(&b);
    CHECK(ha_mqtt_puback(&b, 0x0102));
    CHECK(bytes_eq(&b, ack, sizeof ack));
    ha_buf_clear(&b);
    CHECK(ha_mqtt_pingreq(&b));
    CHECK(bytes_eq(&b, ping, sizeof ping));
    ha_buf_clear(&b);
    CHECK(ha_mqtt_disconnect(&b));
    CHECK(bytes_eq(&b, disc, sizeof disc));
    ha_buf_free(&b);
}

/* ---- MQTT decoding ---- */

static void test_decoder(void)
{
    /* CONNACK 0; PUBLISH QoS 1 "t" id 5 "online"; PINGRESP */
    static const unsigned char in[] = {
        0x20, 2, 0, 0,
        0x32, 11, 0, 1, 't', 0, 5, 'o', 'n', 'l', 'i', 'n', 'e',
        0xD0, 0,
    };
    static const unsigned char five[] = { 0x30, 0x80, 0x80, 0x80, 0x80, 0x01 };
    static const unsigned char big[] = { 0x30, 0x80, 0x80, 0x10 };   /* 262144 bytes */
    ha_mqtt_dec d;
    struct ha_mqtt_packet p;
    struct ha_mqtt_publish_in pub;
    size_t i;
    int types[3], n = 0, r;

    /* one byte at a time: every split must give the same packets */
    ha_mqtt_dec_init(&d);
    for (i = 0; i < sizeof in; i++) {
        CHECK(ha_mqtt_feed(&d, in + i, 1));
        while ((r = ha_mqtt_next(&d, &p)) == 1) {
            if (n < 3)
                types[n] = p.type;
            if (p.type == HA_MQTT_PUBLISH) {
                CHECK(ha_mqtt_parse_publish(&p, &pub));
                CHECK(pub.qos == 1 && pub.packet_id == 5 && pub.topic_len == 1);
                CHECK(pub.payload_len == 6 && memcmp(pub.payload, "online", 6) == 0);
            }
            n++;
        }
        CHECK(r == 0);
    }
    CHECK(n == 3);
    CHECK(types[0] == HA_MQTT_CONNACK && types[1] == HA_MQTT_PUBLISH &&
          types[2] == HA_MQTT_PINGRESP);

    /* all at once */
    ha_mqtt_dec_reset(&d);
    CHECK(ha_mqtt_feed(&d, in, sizeof in));
    for (n = 0; ha_mqtt_next(&d, &p) == 1; n++)
        ;
    CHECK(n == 3);

    ha_mqtt_dec_reset(&d);                  /* 2.2.3: at most four length bytes */
    CHECK(ha_mqtt_feed(&d, five, sizeof five));
    CHECK(ha_mqtt_next(&d, &p) == -1);
    ha_mqtt_dec_reset(&d);                  /* more than we accept */
    CHECK(ha_mqtt_feed(&d, big, sizeof big));
    CHECK(ha_mqtt_next(&d, &p) == -1);
    ha_mqtt_dec_free(&d);

    {
        /* malformed PUBLISHes: topic longer than the packet; QoS 3; packet ID 0 */
        static const unsigned char t1[] = { 0, 9, 'a' };
        static const unsigned char t3[] = { 0, 1, 'a', 0, 0 };
        struct ha_mqtt_packet q;
        q.type = HA_MQTT_PUBLISH;
        q.flags = 0;
        q.body = t1;
        q.len = sizeof t1;
        CHECK(!ha_mqtt_parse_publish(&q, &pub));
        q.flags = 6;
        q.body = t3;
        q.len = sizeof t3;
        CHECK(!ha_mqtt_parse_publish(&q, &pub));
        q.flags = 2;
        CHECK(!ha_mqtt_parse_publish(&q, &pub));
    }
}

/* ---- JSON, text, time ---- */

static void test_json(void)
{
    ha_buf b;
    ha_json j;
    ha_buf_init(&b);
    ha_json_init(&j, &b);
    ha_json_obj(&j, NULL);
    ha_json_str(&j, "s", "a\"b\\c\nd\x01");
    ha_json_num(&j, "n", 1.25, 1);
    ha_json_num(&j, "z", -0.001, 1);
    ha_json_num(&j, "nan", 0.0 / 0.0, 1);
    ha_json_int(&j, "i", -42);
    ha_json_arr(&j, "a");
    ha_json_bool(&j, NULL, 1);
    ha_json_null(&j, NULL);
    ha_json_obj(&j, NULL);
    ha_json_end(&j);
    ha_json_end(&j);
    ha_json_end(&j);
    CHECK(ha_json_ok(&j));
    CHECK_STR((const char *)b.p,
              "{\"s\":\"a\\\"b\\\\c\\nd\\u0001\",\"n\":1.2,\"z\":0.0,\"nan\":null,\"i\":-42,"
              "\"a\":[true,null,{}]}");

    ha_buf_clear(&b);                       /* unbalanced: not ok */
    ha_json_init(&j, &b);
    ha_json_obj(&j, NULL);
    CHECK(!ha_json_ok(&j));
    ha_json_end(&j);
    ha_json_end(&j);
    CHECK(!ha_json_ok(&j));
    ha_buf_free(&b);
}

static void test_slug(void)
{
    char s[16];
    CHECK(ha_slug(s, sizeof s, "$1$DGA100:"));
    CHECK_STR(s, "1_dga100");
    CHECK(ha_slug(s, sizeof s, "DKA0:"));
    CHECK_STR(s, "dka0");
    CHECK(ha_slug(s, sizeof s, "  X86VMS  "));
    CHECK_STR(s, "x86vms");
    CHECK(!ha_slug(s, sizeof s, ""));
    CHECK(!ha_slug(s, sizeof s, "$$::"));
    CHECK(!ha_slug(s, 6, "ABCDEFGH"));      /* truncated */
    CHECK(ha_node_id(s, sizeof s, "X86VMS", "1025"));
    CHECK_STR(s, "x86vms_1025");
}

static void test_iso8601(void)
{
    char t[40];
    ha_iso8601(t, sizeof t, 0);
    CHECK_STR(t, "1970-01-01T00:00:00+00:00");
    ha_iso8601(t, sizeof t, 951782400);     /* a leap day */
    CHECK_STR(t, "2000-02-29T00:00:00+00:00");
    ha_iso8601(t, sizeof t, 1792874277);
    CHECK_STR(t, "2026-10-24T20:37:57+00:00");
    ha_iso8601(t, sizeof t, -1);
    CHECK_STR(t, "1969-12-31T23:59:59+00:00");
    ha_iso8601(t, sizeof t, 4102444800LL);  /* 2100 is not a leap year */
    CHECK_STR(t, "2100-01-01T00:00:00+00:00");
}

/* ---- the model ---- */

static void base_sample(struct ha_sample *s)
{
    memset(s, 0, sizeof *s);
    s->cpu.idle = 1000;
    s->cpu.user = 100;
    s->ncpu = 2;
    s->mem_total = 1024ULL << 20;
    s->mem_free = 256ULL << 20;
    s->page_total = 0;                      /* no page file */
    s->swap_total = 100;
    s->swap_free = 25;
    s->boot_time = 0;
    s->ndisk = 2;
    strcpy(s->disk[0].name, "DKA0:");
    s->disk[0].total = 4ULL << 30;
    s->disk[0].free = 1ULL << 30;
    strcpy(s->disk[1].name, "$1$DGA100:");
    s->disk[1].total = 2ULL << 30;
    s->disk[1].free = 2ULL << 30;
    s->nnic = 1;
    strcpy(s->nic[0].name, "EWA0");
}

static int has_key(const struct ha_comp_set *set, const char *k)
{
    int i;
    for (i = 0; i < set->n; i++)
        if (strcmp(set->c[i].key, k) == 0)
            return 1;
    return 0;
}

static void test_model(void)
{
    static struct ha_sample a, b;
    static struct ha_comp_set set, set2, removed;
    struct ha_figures f;
    struct ha_model_cfg cfg;
    struct ha_identity id;
    ha_buf out;
    ha_buf_init(&out);

    base_sample(&a);
    b = a;
    b.cpu.idle += 750;                      /* 1000 ticks: 75% idle, 20% user, 5% kernel */
    b.cpu.user += 200;
    b.cpu.kernel += 50;
    b.nic[0].rx += 10000;
    b.nic[0].tx += 2000;
    ha_model_figures(&a, &b, 2000, &f);
    CHECK(f.have_rates);
    CHECK(f.cpu_busy > 24.99 && f.cpu_busy < 25.01);
    CHECK(f.cpu_mode[5] > 19.99 && f.cpu_mode[5] < 20.01);
    CHECK(f.nic_rx[0] == 5000 && f.nic_tx[0] == 1000);

    ha_model_figures(&b, &a, 2000, &f);     /* counters went down: a reboot */
    CHECK(!f.have_rates);
    ha_model_figures(NULL, &a, 0, &f);
    CHECK(!f.have_rates);

    ha_model_components(&b, &set);
    CHECK(has_key(&set, "cpu") && has_key(&set, "swapfile_used"));
    CHECK(!has_key(&set, "pagefile_used"));
    CHECK(has_key(&set, "disk_dka0_free") && has_key(&set, "disk_1_dga100_used"));
    CHECK(has_key(&set, "nic_ewa0_rx"));

    memset(&cfg, 0, sizeof cfg);
    cfg.discovery_prefix = "homeassistant";
    cfg.topic_prefix = "openvms";
    cfg.node_id = "n_1";
    cfg.device_name = "N";
    cfg.agent_version = "t";
    cfg.expire_after_s = 100;
    memset(&id, 0, sizeof id);
    ha_model_figures(&a, &b, 2000, &f);
    CHECK(ha_model_state(&set, &b, &f, &out));
    CHECK(strstr((char *)out.p, "\"cpu\":25.0,") != NULL);
    CHECK(strstr((char *)out.p, "\"mem_used\":75.0,") != NULL);
    CHECK(strstr((char *)out.p, "\"swapfile_used\":75.0,") != NULL);
    CHECK(strstr((char *)out.p, "\"disk_dka0_used\":75.0,\"disk_dka0_free\":1.00,"
                               "\"disk_dka0_size\":4.00") != NULL);
    CHECK(strstr((char *)out.p, "\"nic_ewa0_rx\":5.0,\"nic_ewa0_tx\":1.0") != NULL);
    CHECK(strstr((char *)out.p, "\"boot_time\":\"1970-01-01T00:00:00+00:00\"") != NULL);

    /* no rates yet: those fields are null, the others are there */
    ha_model_figures(NULL, &b, 0, &f);
    CHECK(ha_model_state(&set, &b, &f, &out));
    CHECK(strstr((char *)out.p, "\"cpu\":null,") != NULL);
    CHECK(strstr((char *)out.p, "\"nic_ewa0_rx\":null") != NULL);
    CHECK(strstr((char *)out.p, "\"mem_used\":75.0") != NULL);

    /* a disk goes away: its three sensors are removed */
    b.ndisk = 1;
    ha_model_components(&b, &set2);
    CHECK(!ha_model_same(&set, &set2));
    ha_model_removed(&set, &set2, &removed);
    CHECK(removed.n == 3 && has_key(&removed, "disk_1_dga100_size"));
    CHECK(ha_model_discovery(&cfg, &id, &set2, &removed, &out));
    CHECK(strstr((char *)out.p, "\"disk_1_dga100_used\":{\"p\":\"sensor\"}") != NULL);
    CHECK(strstr((char *)out.p, "\"unique_id\":\"n_1_disk_dka0_used\"") != NULL);
    CHECK(strstr((char *)out.p, "\"state_topic\":\"openvms/n_1/state\"") != NULL);
    CHECK(strstr((char *)out.p, "\"availability_topic\":\"openvms/n_1/availability\"}") != NULL);
    /* expire_after only inside sensors: HA refuses it at the top (NOTES.md 3) */
    CHECK(strstr((char *)out.p, "\"unit_of_measurement\":\"%\",\"state_class\":\"measurement\","
                               "\"icon\":\"mdi:cpu-64-bit\",\"suggested_display_precision\":1,"
                               "\"expire_after\":100,") != NULL);
    CHECK(strstr((char *)out.p, "\"state_topic\":\"openvms/n_1/state\",\"availability_topic\"") != NULL);
    {
        char t[128];
        ha_topic_config(t, sizeof t, &cfg);
        CHECK_STR(t, "homeassistant/device/n_1/config");
    }
    ha_model_components(&b, &set);
    CHECK(ha_model_same(&set, &set2));
    ha_buf_free(&out);
}

/* ---- the session ---- */

struct seen {
    int n;
    int type[16];
    int flags[16];
    char topic[16][128];
    char payload[16][64];
    unsigned id[16];
};

/* decode what the session wants to send, and drop it from out */
static void drain(ha_buf *out, struct seen *s)
{
    ha_mqtt_dec d;
    struct ha_mqtt_packet p;
    memset(s, 0, sizeof *s);
    ha_mqtt_dec_init(&d);
    ha_mqtt_feed(&d, out->p, out->len);
    while (ha_mqtt_next(&d, &p) == 1 && s->n < 16) {
        s->type[s->n] = p.type;
        s->flags[s->n] = p.flags;
        if (p.type == HA_MQTT_PUBLISH) {
            struct ha_mqtt_publish_in pub;
            if (ha_mqtt_parse_publish(&p, &pub)) {
                ha_strlcpyn(s->topic[s->n], 128, pub.topic, pub.topic_len);
                ha_strlcpyn(s->payload[s->n], 64, (const char *)pub.payload, pub.payload_len);
                s->id[s->n] = pub.packet_id;
            }
        } else if (p.type == HA_MQTT_SUBSCRIBE && p.len >= 2) {
            s->id[s->n] = ((unsigned)p.body[0] << 8) | p.body[1];
        }
        s->n++;
    }
    ha_mqtt_dec_free(&d);
    ha_buf_clear(out);
}

static void feed(ha_sess *s, const unsigned char *p, size_t n, uint64_t now, int want)
{
    CHECK(ha_sess_input(s, p, n, now) == want);
}

static void test_session(void)
{
    static const unsigned char ok[] = { 0x20, 2, 0, 0 };
    static const unsigned char refused[] = { 0x20, 2, 0, 5 };
    static const unsigned char pingresp[] = { 0xD0, 0 };
    static const unsigned char online[] = {
        0x30, 28, 0, 20, 'h', 'o', 'm', 'e', 'a', 's', 's', 'i', 's', 't', 'a', 'n', 't',
        '/', 's', 't', 'a', 't', 'u', 's', 'o', 'n', 'l', 'i', 'n', 'e' };
    struct ha_sess_cfg cfg;
    ha_sess s;
    struct seen seen;
    unsigned char ack[4];

    memset(&cfg, 0, sizeof cfg);
    cfg.client_id = "c";
    cfg.keepalive_s = 60;
    cfg.will_topic = "w";
    cfg.will_payload = "offline";
    cfg.status_topic = "homeassistant/status";
    ha_sess_init(&s);

    CHECK(ha_sess_begin(&s, &cfg, 1000));
    drain(&s.out, &seen);
    CHECK(seen.n == 1 && seen.type[0] == HA_MQTT_CONNECT);
    CHECK(!ha_sess_publish(&s, "t", "x", 1, 0, 0, 1000));   /* not up yet */
    feed(&s, pingresp, 2, 1100, 0);                          /* 3.2: CONNACK first */
    CHECK(s.state == HA_SESS_DOWN);

    CHECK(ha_sess_begin(&s, &cfg, 2000));
    feed(&s, refused, 4, 2100, 0);
    CHECK(s.connack_code == 5 && strstr(s.err, "not authorised") != NULL);

    CHECK(ha_sess_begin(&s, &cfg, 3000));                    /* no answer at all */
    CHECK(ha_sess_tick(&s, 3000 + HA_SESS_CONNACK_MS - 1));
    CHECK(!ha_sess_tick(&s, 3000 + HA_SESS_CONNACK_MS));

    CHECK(ha_sess_begin(&s, &cfg, 0));
    feed(&s, ok, 4, 10, 1);
    CHECK(s.state == HA_SESS_UP && s.just_up);
    drain(&s.out, &seen);

    /* keepalive: a ping at half the period, and a drop if it isn't answered */
    CHECK(ha_sess_wake_in(&s, 10) == 29990);  /* CONNECT at 0 */
    CHECK(ha_sess_tick(&s, 29999));
    CHECK(s.out.len == 0);
    CHECK(ha_sess_tick(&s, 30000));
    drain(&s.out, &seen);
    CHECK(seen.n == 1 && seen.type[0] == HA_MQTT_PINGREQ);
    feed(&s, pingresp, 2, 30100, 1);
    CHECK(!s.ping_out);
    CHECK(ha_sess_tick(&s, 60000));
    drain(&s.out, &seen);
    CHECK(seen.n == 1 && seen.type[0] == HA_MQTT_PINGREQ);
    CHECK(ha_sess_tick(&s, 119999));
    CHECK(!ha_sess_tick(&s, 120000));
    CHECK(strstr(s.err, "PINGRESP") != NULL);

    /* QoS 1: an answered PUBLISH is forgotten; an unanswered one drops the line */
    CHECK(ha_sess_begin(&s, &cfg, 0));
    feed(&s, ok, 4, 0, 1);
    drain(&s.out, &seen);
    CHECK(ha_sess_publish(&s, "t", "x", 1, 1, 1, 100));
    drain(&s.out, &seen);
    CHECK(seen.n == 1 && seen.flags[0] == 3 && seen.id[0] != 0);   /* QoS 1, retained */
    CHECK(s.ninflight == 1);
    ack[0] = 0x40;
    ack[1] = 2;
    ack[2] = (unsigned char)(seen.id[0] >> 8);
    ack[3] = (unsigned char)seen.id[0];
    feed(&s, ack, 4, 200, 1);
    CHECK(s.ninflight == 0);
    CHECK(ha_sess_publish(&s, "t", "x", 1, 1, 1, 300));
    CHECK(ha_sess_tick(&s, 300 + 59999));
    feed(&s, pingresp, 2, 300 + 59999, 1);
    CHECK(!ha_sess_tick(&s, 300 + 60000));
    CHECK(strstr(s.err, "acknowledge") != NULL);

    /* HA's birth message */
    CHECK(ha_sess_begin(&s, &cfg, 0));
    feed(&s, ok, 4, 0, 1);
    s.ha_online = 0;
    feed(&s, online, sizeof online, 10, 1);
    CHECK(s.ha_online == 1);

    /* DISCONNECT on the way out */
    drain(&s.out, &seen);
    ha_sess_end(&s);
    drain(&s.out, &seen);
    CHECK(seen.n == 1 && seen.type[0] == HA_MQTT_DISCONNECT);
    CHECK(s.state == HA_SESS_DOWN);
    ha_sess_free(&s);
}

/* ---- the agent ---- */

static int find(const struct seen *s, const char *topic)
{
    int i;
    for (i = 0; i < s->n; i++)
        if (s->type[i] == HA_MQTT_PUBLISH && strcmp(s->topic[i], topic) == 0)
            return i;
    return -1;
}

static void test_agent(void)
{
    static ha_agent a;
    static struct ha_agent_cfg cfg;
    static struct ha_identity id;
    static struct ha_sample smp;
    static const unsigned char ok[] = { 0x20, 2, 0, 0 };
    struct seen seen;
    int i, c, av, st, sub;
    uint64_t t = 1000;

    memset(&cfg, 0, sizeof cfg);
    cfg.model.discovery_prefix = "homeassistant";
    cfg.model.topic_prefix = "openvms";
    cfg.model.node_id = "n_1";
    cfg.model.device_name = "N";
    cfg.model.agent_version = "t";
    cfg.sess.client_id = "vms-hass-n_1";
    cfg.sess.keepalive_s = 60;
    cfg.sess.status_topic = "homeassistant/status";
    cfg.interval_s = 30;
    memset(&id, 0, sizeof id);
    ha_agent_init(&a, &cfg, &id, NULL, NULL, t);
    base_sample(&smp);

    CHECK(ha_agent_sample_due(&a, t));
    CHECK(ha_agent_sample(&a, &smp, t));            /* sampled while still down */
    CHECK(!ha_agent_sample_due(&a, t + HA_FIRST_INTERVAL_MS - 1));
    CHECK(ha_agent_sample_due(&a, t + HA_FIRST_INTERVAL_MS));

    CHECK(ha_agent_begin(&a, t));
    drain(ha_agent_out(&a), &seen);
    CHECK(seen.n == 1 && seen.type[0] == HA_MQTT_CONNECT);
    CHECK(ha_agent_input(&a, ok, 4, t));
    drain(ha_agent_out(&a), &seen);
    /* in order: SUBSCRIBE, config (QoS 1, retained), online (QoS 1, retained), state */
    sub = -1;
    for (i = 0; i < seen.n; i++)
        if (seen.type[i] == HA_MQTT_SUBSCRIBE)
            sub = i;
    c = find(&seen, "homeassistant/device/n_1/config");
    av = find(&seen, "openvms/n_1/availability");
    st = find(&seen, "openvms/n_1/state");
    CHECK(sub == 0 && c == 1 && av == 2 && st == 3);
    CHECK(c >= 0 && seen.flags[c] == 3);
    CHECK(av >= 0 && seen.flags[av] == 3 && strcmp(seen.payload[av], "online") == 0);
    CHECK(st >= 0 && seen.flags[st] == 1);          /* QoS 0, retained */

    /* the second sample: rates, and only the state */
    smp.cpu.idle += 100;
    t += HA_FIRST_INTERVAL_MS;
    CHECK(ha_agent_sample(&a, &smp, t));
    drain(ha_agent_out(&a), &seen);
    CHECK(seen.n == 1 && find(&seen, "openvms/n_1/state") == 0);
    CHECK(strstr(seen.payload[0], "\"cpu\":0.0") != NULL);
    CHECK(!ha_agent_sample_due(&a, t + 29999) && ha_agent_sample_due(&a, t + 30000));

    /* HA restarts: everything again */
    {
        static const unsigned char online[] = {
            0x30, 28, 0, 20, 'h', 'o', 'm', 'e', 'a', 's', 's', 'i', 's', 't', 'a', 'n', 't',
            '/', 's', 't', 'a', 't', 'u', 's', 'o', 'n', 'l', 'i', 'n', 'e' };
        CHECK(ha_agent_input(&a, online, sizeof online, t));
        drain(ha_agent_out(&a), &seen);
        CHECK(find(&seen, "homeassistant/device/n_1/config") == 0);
        CHECK(find(&seen, "openvms/n_1/availability") == 1);
        CHECK(find(&seen, "openvms/n_1/state") == 2);
    }

    /* a disk is dismounted: discovery (with the removals), then the state */
    smp.ndisk = 1;
    t += 30000;
    CHECK(ha_agent_sample(&a, &smp, t));
    drain(ha_agent_out(&a), &seen);
    c = find(&seen, "homeassistant/device/n_1/config");
    CHECK(c == 0 && seen.n == 2);

    /* orderly stop: "offline" (retained), then DISCONNECT */
    ha_agent_stop(&a, t);
    drain(ha_agent_out(&a), &seen);
    av = find(&seen, "openvms/n_1/availability");
    CHECK(av == 0 && strcmp(seen.payload[0], "offline") == 0 && seen.flags[0] == 1);
    CHECK(seen.n == 2 && seen.type[1] == HA_MQTT_DISCONNECT);

    /* backoff: 1, 2, 5, 10, 30, 60, 60 s (+ up to 20%) */
    {
        static const unsigned want[] = { 1000, 2000, 5000, 10000, 30000, 60000, 60000 };
        a.failures = 0;
        a.was_up = 0;
        for (i = 0; i < 7; i++) {
            unsigned b;
            ha_agent_closed(&a, t);
            b = ha_agent_backoff_ms(&a, 0);
            CHECK(b == want[i]);
            b = ha_agent_backoff_ms(&a, 0xFFFFFFFFu);
            CHECK(b >= want[i] && b <= want[i] + want[i] / 5);
        }
        /* a connection that lasted a minute starts the backoff again */
        CHECK(ha_agent_begin(&a, t));
        CHECK(ha_agent_input(&a, ok, 4, t));
        ha_agent_closed(&a, t + 60000);
        CHECK(ha_agent_backoff_ms(&a, 0) == 1000);
    }

    /* --remove: empty retained messages on all three topics */
    CHECK(ha_agent_begin(&a, t));
    CHECK(ha_agent_input(&a, ok, 4, t));
    drain(ha_agent_out(&a), &seen);
    CHECK(ha_agent_remove(&a, t));
    drain(ha_agent_out(&a), &seen);
    CHECK(seen.n == 3);
    for (i = 0; i < 3; i++)
        CHECK(seen.flags[i] == 3 && seen.payload[i][0] == 0);
    CHECK(ha_agent_pending(&a));
    ha_agent_free(&a);
}

/* ---- configuration ---- */

static void test_conf(void)
{
    ha_conf c;
    ha_buf b;
    char err[200];
    static const char good[] = "# comment\n\nMQTT_HOST = broker\nMQTT_USER=\"agent\"\n";
    ha_conf_init(&c, NULL, NULL);
    CHECK(ha_conf_parse(&c, good, strlen(good), err, sizeof err));
    CHECK_STR(ha_conf_get(&c, "MQTT_HOST"), "broker");
    CHECK_STR(ha_conf_get(&c, "MQTT_USER"), "agent");
    CHECK(ha_conf_get(&c, "INTERVAL") == NULL);
    ha_conf_free(&c);

    ha_conf_init(&c, NULL, NULL);
    CHECK(!ha_conf_parse(&c, "MQTT_HOTS=x\n", 12, err, sizeof err));
    CHECK(strstr(err, "line 1") && strstr(err, "MQTT_HOTS"));
    ha_conf_free(&c);
    ha_conf_init(&c, NULL, NULL);
    CHECK(!ha_conf_parse(&c, "INTERVAL=1\nINTERVAL=2\n", 22, err, sizeof err));
    CHECK(strstr(err, "set twice") != NULL);
    ha_conf_free(&c);

    ha_buf_init(&b);
    CHECK(ha_conf_secret_file("# pw\n s3cret \n", 14, &b));
    CHECK_STR((const char *)b.p, "s3cret");
    CHECK(!ha_conf_secret_file("a\nb\n", 4, &b));
    ha_buf_free(&b);
}

int main(void)
{
    test_remaining_length();
    test_connect();
    test_publish_subscribe();
    test_decoder();
    test_json();
    test_slug();
    test_iso8601();
    test_model();
    test_session();
    test_agent();
    test_conf();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
