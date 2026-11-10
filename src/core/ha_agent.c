/* ha_agent.c - the agent (see ha_agent.h). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ha_agent.h"
#include "ha_text.h"

static void say(ha_agent *a, int level, const char *fmt, ...)
{
    char msg[400];
    va_list ap;
    if (!a->log)
        return;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    a->log(a->log_ctx, level, msg);
}

void ha_agent_init(ha_agent *a, const struct ha_agent_cfg *cfg, const struct ha_identity *id,
                   ha_log_fn log, void *log_ctx, ha_u64 now)
{
    memset(a, 0, sizeof *a);
    a->cfg = *cfg;
    a->id = *id;
    a->log = log;
    a->log_ctx = log_ctx;
    ha_sess_init(&a->sess);
    ha_buf_init(&a->doc);
    ha_topic_config(a->t_config, sizeof a->t_config, &a->cfg.model);
    ha_topic_state(a->t_state, sizeof a->t_state, &a->cfg.model);
    ha_topic_avail(a->t_avail, sizeof a->t_avail, &a->cfg.model);
    a->cfg.sess.will_topic = a->t_avail;
    a->cfg.sess.will_payload = "offline";
    a->next_sample = now;
}

void ha_agent_free(ha_agent *a)
{
    ha_sess_free(&a->sess);
    ha_buf_free(&a->doc);
}

int ha_agent_up(const ha_agent *a)
{
    return a->sess.state == HA_SESS_UP;
}

int ha_agent_pending(const ha_agent *a)
{
    return a->sess.ninflight > 0;
}

ha_buf *ha_agent_out(ha_agent *a)
{
    return &a->sess.out;
}

void ha_agent_sent(ha_agent *a, size_t n)
{
    ha_sess_sent(&a->sess, n);
}

static int publish_discovery(ha_agent *a, ha_u64 now)
{
    const struct ha_comp_set *rem = NULL;
    if (a->have_published) {
        ha_model_removed(&a->published, &a->comps, &a->removed);
        if (a->removed.n)
            rem = &a->removed;
    }
    if (!ha_model_discovery(&a->cfg.model, &a->id, &a->comps, rem, &a->doc)) {
        say(a, HA_LOG_ERROR, "cannot build the discovery document");
        return 0;
    }
    if (!ha_sess_publish(&a->sess, a->t_config, a->doc.p, a->doc.len, 1, 1, now))
        return 0;
    say(a, HA_LOG_INFO, "published discovery: %d sensors%s (%lu bytes)", a->comps.n,
        rem ? ", some removed" : "", (unsigned long)a->doc.len);
    if (!a->have_published || !ha_model_same(&a->published, &a->comps))
        a->published_changed = 1;
    a->published = a->comps;
    a->have_published = 1;
    return 1;
}

static int publish_state(ha_agent *a, ha_u64 now)
{
    if (a->nsamples == 0)
        return 1;
    if (!ha_model_state(&a->comps, &a->cur, &a->fig, &a->doc)) {
        say(a, HA_LOG_ERROR, "cannot build the state document");
        return 0;
    }
    /* retained, so HA has the values as soon as it creates the entities, and
       after its own restart; QoS 0, since the next one is never far off */
    return ha_sess_publish(&a->sess, a->t_state, a->doc.p, a->doc.len, 0, 1, now);
}

/* everything HA needs, after a CONNACK or HA's own restart */
static int announce(ha_agent *a, ha_u64 now)
{
    if (a->nsamples == 0)
        return 1;                       /* discovery needs the disks and NICs */
    return publish_discovery(a, now) &&
           ha_sess_publish(&a->sess, a->t_avail, "online", 6, 1, 1, now) &&
           publish_state(a, now);
}

static int after_input(ha_agent *a, ha_u64 now)
{
    if (a->sess.just_up) {
        a->sess.just_up = 0;
        a->up_since = now;
        a->was_up = 1;
        a->seen_online = a->sess.ha_online;
        say(a, HA_LOG_INFO, "connected to the broker as %s", a->cfg.sess.client_id);
        if (!ha_sess_subscribe(&a->sess, a->cfg.sess.status_topic, 0, now) ||
            !announce(a, now))
            return 0;
    }
    if (a->sess.ha_online != a->seen_online) {
        a->seen_online = a->sess.ha_online;
        say(a, HA_LOG_INFO, "Home Assistant came online: publishing discovery again");
        if (!announce(a, now))
            return 0;
    }
    return 1;
}

int ha_agent_begin(ha_agent *a, ha_u64 now)
{
    a->was_up = 0;
    return ha_sess_begin(&a->sess, &a->cfg.sess, now);
}

int ha_agent_input(ha_agent *a, const void *p, size_t n, ha_u64 now)
{
    if (!ha_sess_input(&a->sess, p, n, now))
        return 0;
    return after_input(a, now);
}

int ha_agent_tick(ha_agent *a, ha_u64 now)
{
    return ha_sess_tick(&a->sess, now);
}

ha_u64 ha_agent_wake_in(const ha_agent *a, ha_u64 now)
{
    ha_u64 w = ha_sess_wake_in(&a->sess, now);
    ha_u64 s = a->next_sample > now ? a->next_sample - now : 0;
    return s < w ? s : w;
}

int ha_agent_sample_due(const ha_agent *a, ha_u64 now)
{
    return now >= a->next_sample;
}

static int name_eq(const char *a, size_t n, const char *b)
{
    size_t i;
    for (i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (!y)
            return 0;
        if (x >= 'a' && x <= 'z')
            x = (char)(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z')
            y = (char)(y - 'a' + 'A');
        if (x != y)
            return 0;
    }
    return b[n] == 0;
}

int ha_agent_queue_wanted(const char *list, const char *name)
{
    const char *p = list;
    size_t n;
    if (!list)
        return 1;
    while (*p == ' ')
        p++;
    if (strcmp(p, "*") == 0)
        return 1;
    while (*p) {
        const char *s;
        n = strcspn(p, ",");
        s = p;
        ha_trim(&s, &n);
        if (n == 1 && *s == '*')
            return 1;
        if (n && name_eq(s, n, name))
            return 1;
        p += strcspn(p, ",");
        if (*p == ',')
            p++;
    }
    return 0;                           /* "NONE" matches no queue name a node allows */
}

int ha_agent_sample(ha_agent *a, const struct ha_sample *s, ha_u64 now)
{
    struct ha_comp_set *next = &a->removed;     /* scratch until discovery */
    int i, k;
    if (a->nsamples) {
        a->prev = a->cur;
        a->prev_at = a->cur_at;
    }
    a->cur = *s;
    for (i = k = 0; i < a->cur.nqueue && i < HA_MAX_QUEUE; i++)
        if (ha_agent_queue_wanted(a->cfg.queues, a->cur.queue[i].name))
            a->cur.queue[k++] = a->cur.queue[i];
    a->cur.nqueue = k;
    a->cur_at = now;
    a->nsamples++;
    ha_model_figures(a->nsamples > 1 ? &a->prev : NULL, &a->cur,
                     a->nsamples > 1 ? now - a->prev_at : 0, &a->fig);
    ha_track_update(&a->track, &a->cur, now, (ha_u64)a->cfg.error_hold_s * 1000, &a->fig);
    a->next_sample = now + (a->nsamples == 1 ? HA_FIRST_INTERVAL_MS
                                            : (ha_u64)a->cfg.interval_s * 1000);
    ha_model_components(&a->cur, next);
    if (!ha_model_same(next, &a->comps)) {
        a->comps = *next;
        if (ha_agent_up(a) && a->nsamples > 1) {
            say(a, HA_LOG_INFO, "the sensors changed (a disk, NIC, shadow set or queue came or went)");
            if (!publish_discovery(a, now))
                return 0;
        }
    }
    if (!ha_agent_up(a))
        return 1;
    if (a->nsamples == 1)               /* connected before the first sample */
        return announce(a, now);
    return publish_state(a, now);
}

void ha_agent_stop(ha_agent *a, ha_u64 now)
{
    if (ha_agent_up(a))
        ha_sess_publish(&a->sess, a->t_avail, "offline", 7, 0, 1, now);
    ha_sess_end(&a->sess);
}

void ha_agent_end(ha_agent *a)
{
    ha_sess_end(&a->sess);
}

int ha_agent_remove(ha_agent *a, ha_u64 now)
{
    /* an empty retained message deletes the retained one (MQTT 3.3.1.3), and an
       empty config removes the device from HA (docs/MQTT.md) */
    return ha_sess_publish(&a->sess, a->t_config, "", 0, 1, 1, now) &&
           ha_sess_publish(&a->sess, a->t_state, "", 0, 1, 1, now) &&
           ha_sess_publish(&a->sess, a->t_avail, "", 0, 1, 1, now);
}

void ha_agent_closed(ha_agent *a, ha_u64 now)
{
    if (a->sess.err[0])
        say(a, HA_LOG_WARN, "connection closed: %s", a->sess.err);
    /* a connection that lasted a minute was a success: start the backoff again */
    if (a->was_up && now - a->up_since >= 60000)
        a->failures = 0;
    a->failures++;
    a->was_up = 0;
    a->sess.state = HA_SESS_DOWN;
}

unsigned ha_agent_backoff_ms(const ha_agent *a, unsigned rnd)
{
    static const unsigned steps[] = { 1000, 2000, 5000, 10000, 30000, 60000 };
    unsigned i = a->failures ? a->failures - 1 : 0, base;
    if (i >= sizeof steps / sizeof steps[0])
        i = sizeof steps / sizeof steps[0] - 1;
    base = steps[i];
    /* up to 20% more, so a broker restart isn't met by every node at once */
    return base + rnd % (base / 5 + 1);
}

void ha_agent_load_published(ha_agent *a, const char *text, size_t n)
{
    size_t i = 0;
    a->published.n = 0;
    while (i < n && a->published.n < HA_MAX_COMP) {
        const char *s = text + i, *p;
        size_t len = 0, klen;
        struct ha_comp *c;
        while (i + len < n && text[i + len] != '\n')
            len++;
        i += len + 1;
        ha_trim(&s, &len);
        /* "<key>" (a sensor: Phase 2 files) or "<key> <platform>" */
        for (klen = 0; klen < len && s[klen] != ' '; klen++)
            ;
        if (klen == 0 || klen >= HA_KEY)
            continue;
        p = s + klen;
        while (p < s + len && *p == ' ')
            p++;
        c = &a->published.c[a->published.n++];
        memset(c, 0, sizeof *c);
        ha_strlcpyn(c->key, sizeof c->key, s, klen);
        c->platform = (size_t)(s + len - p) == 13 && memcmp(p, "binary_sensor", 13) == 0
                      ? "binary_sensor" : "sensor";
        c->precision = -1;
    }
    a->have_published = a->published.n > 0;
}

int ha_agent_published_changed(ha_agent *a)
{
    int c = a->published_changed;
    a->published_changed = 0;
    return c;
}

void ha_agent_published_text(const ha_agent *a, ha_buf *out)
{
    int i;
    ha_buf_clear(out);
    for (i = 0; i < a->published.n; i++) {
        ha_buf_puts(out, a->published.c[i].key);
        if (strcmp(a->published.c[i].platform, "sensor") != 0) {
            ha_buf_putc(out, ' ');
            ha_buf_puts(out, a->published.c[i].platform);
        }
        ha_buf_putc(out, '\n');
    }
}
