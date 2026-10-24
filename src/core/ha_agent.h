/* ha_agent.h - the agent: samples in, MQTT session out.
 *
 * No I/O.  The caller (src/main) owns the socket and the clock: it opens the
 * connection, calls ha_agent_begin, feeds received bytes to ha_agent_input,
 * sends ha_agent_out(), calls ha_agent_tick when ha_agent_wake_in says, and
 * gives it a sample whenever ha_agent_sample_due says.  When any call returns
 * 0 it closes the connection, calls ha_agent_closed, and waits
 * ha_agent_backoff_ms before connecting again.
 */
#ifndef HA_AGENT_H
#define HA_AGENT_H

#include <stdint.h>

#include "ha_buf.h"
#include "ha_model.h"
#include "ha_sess.h"
#include "ha_sys.h"

#define HA_LOG_DEBUG 0
#define HA_LOG_INFO  1
#define HA_LOG_WARN  2
#define HA_LOG_ERROR 3

/* the first state needs two samples: the second comes this soon after the first */
#define HA_FIRST_INTERVAL_MS 5000

typedef void (*ha_log_fn)(void *ctx, int level, const char *msg);

struct ha_agent_cfg {
    struct ha_model_cfg model;
    struct ha_sess_cfg sess;
    unsigned interval_s;
};

typedef struct ha_agent {
    struct ha_agent_cfg cfg;
    struct ha_identity id;
    ha_sess sess;
    ha_log_fn log;
    void *log_ctx;

    struct ha_sample prev, cur;
    int nsamples;
    uint64_t prev_at, cur_at, next_sample;
    struct ha_figures fig;

    struct ha_comp_set comps;           /* for the current sample */
    struct ha_comp_set published;       /* in the last discovery sent */
    int have_published;
    struct ha_comp_set removed;

    int published_changed;              /* the caller should save the key list */
    int seen_online;                    /* sess.ha_online already handled */
    unsigned failures;                  /* connection attempts in a row that failed */
    uint64_t up_since;
    int was_up;

    ha_buf doc;
    char t_config[256], t_state[256], t_avail[256];
} ha_agent;

/* the cfg strings must outlive the agent */
void ha_agent_init(ha_agent *a, const struct ha_agent_cfg *cfg, const struct ha_identity *id,
                   ha_log_fn log, void *log_ctx, uint64_t now);
void ha_agent_free(ha_agent *a);

int ha_agent_begin(ha_agent *a, uint64_t now);
int ha_agent_input(ha_agent *a, const void *p, size_t n, uint64_t now);
int ha_agent_tick(ha_agent *a, uint64_t now);
uint64_t ha_agent_wake_in(const ha_agent *a, uint64_t now);

int ha_agent_sample_due(const ha_agent *a, uint64_t now);
/* a new sample; publishes the state (and discovery, if sensors changed) when up */
int ha_agent_sample(ha_agent *a, const struct ha_sample *s, uint64_t now);

/* bytes waiting to be sent, and how many were */
ha_buf *ha_agent_out(ha_agent *a);
void ha_agent_sent(ha_agent *a, size_t n);

int ha_agent_up(const ha_agent *a);
/* orderly stop: "offline", then DISCONNECT */
void ha_agent_stop(ha_agent *a, uint64_t now);
/* DISCONNECT without "offline" (after ha_agent_remove) */
void ha_agent_end(ha_agent *a);
/* remove the device from HA: empty retained config, availability and state */
int ha_agent_remove(ha_agent *a, uint64_t now);
/* 1 while QoS 1 messages are unacknowledged */
int ha_agent_pending(const ha_agent *a);

void ha_agent_closed(ha_agent *a, uint64_t now);
/* the wait before the next attempt; rnd is any random number (jitter) */
unsigned ha_agent_backoff_ms(const ha_agent *a, unsigned rnd);

/* The sensor keys published last, one per line, kept by the caller in DATA_DIR,
   so that a sensor gone while the agent was stopped (a dismounted disk) is
   still removed from HA.  Load before the first connection; save whenever
   ha_agent_published_changed returns 1. */
void ha_agent_load_published(ha_agent *a, const char *text, size_t n);
int ha_agent_published_changed(ha_agent *a);
void ha_agent_published_text(const ha_agent *a, ha_buf *out);

#endif
