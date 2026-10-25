/* ha_model.h - from system samples to HA's discovery and state documents.
 *
 * Each sensor is a component of one device-discovery message (docs/MQTT.md,
 * "HA discovery").  A component's key is also its field in the state document
 * and the tail of its unique_id ("<node_id>_<key>"), so a key must never change
 * once published (CLAUDE.md).  Per-disk and per-NIC keys come from the device
 * names through ha_slug(): "DKA0:" -> "disk_dka0_free".
 */
#ifndef HA_MODEL_H
#define HA_MODEL_H

#include <stdint.h>

#include "ha_buf.h"
#include "ha_sys.h"

#define HA_KEY 48
#define HA_MAX_COMP 400

/* what a component measures */
enum ha_src {
    HA_SRC_CPU, HA_SRC_CPU_MODE, HA_SRC_COMPUTE_QUEUE, HA_SRC_MEM_USED, HA_SRC_MEM_FREE,
    HA_SRC_MEM_MODIFIED, HA_SRC_MEM_TOTAL, HA_SRC_PAGE_USED, HA_SRC_SWAP_USED,
    HA_SRC_BOOT_TIME, HA_SRC_PROCESSES, HA_SRC_INTERACTIVE, HA_SRC_CPUS,
    HA_SRC_DISK_USED, HA_SRC_DISK_FREE, HA_SRC_DISK_SIZE, HA_SRC_NIC_RX, HA_SRC_NIC_TX,
    HA_SRC_DISK_ERRORS, HA_SRC_DISK_PROBLEM,
    HA_SRC_SHADOW_STATE, HA_SRC_SHADOW_MEMBERS, HA_SRC_SHADOW_PROGRESS, HA_SRC_SHADOW_PROBLEM,
    HA_SRC_QUEUE_STATE, HA_SRC_QUEUE_PENDING, HA_SRC_QUEUE_EXECUTING, HA_SRC_QUEUE_PROBLEM,
    HA_SRC_CLUSTER_NODES, HA_SRC_CLUSTER_VOTES, HA_SRC_CLUSTER_QUORUM
};

struct ha_comp {
    char key[HA_KEY];
    int src;                            /* enum ha_src */
    int idx;                            /* the CPU mode, disk or NIC it is about */
    char name[64];
    const char *platform;               /* "sensor" or "binary_sensor" */
    const char *device_class;           /* NULL: none */
    const char *unit;
    const char *state_class;            /* "measurement", or NULL */
    const char *icon;
    int diagnostic;                     /* entity_category: diagnostic */
    int precision;                      /* suggested_display_precision, -1 none */
    int disabled;                       /* enabled_by_default: false */
    int expires;                        /* expire_after, without a state_class */
    const char *const *options;         /* an enum sensor's states, NULL-terminated */
};

struct ha_comp_set {
    int n;
    struct ha_comp c[HA_MAX_COMP];
};

/* the figures one state document carries; rates need two samples */
struct ha_figures {
    int have_rates;                     /* CPU and NIC rates valid */
    double cpu_busy;                    /* % of all active CPUs */
    double cpu_mode[7];                 /* % per mode, ha_cpu_modes order */
    double nic_rx[HA_MAX_NIC];          /* bytes/s */
    double nic_tx[HA_MAX_NIC];
    /* from ha_track_update, which sees every sample */
    int disk_problem[HA_MAX_DISK];
    int shadow_state[HA_MAX_SHADOW];    /* HA_SHADOW_* */
};

#define HA_SHADOW_STEADY   0
#define HA_SHADOW_COPYING  1
#define HA_SHADOW_MERGING  2
#define HA_SHADOW_DEGRADED 3

/* what the problem sensors need to remember between samples (D8) */
struct ha_track {
    int ndisk;
    struct {
        char name[HA_NAME];
        uint32_t errors;
        int rose;                       /* the count rose while we watched */
        uint64_t rose_at;
    } disk[HA_MAX_DISK];
    int nshadow;
    struct {
        char name[HA_NAME];
        int most;                       /* the most full members seen */
    } shadow[HA_MAX_SHADOW];
};

struct ha_model_cfg {
    const char *discovery_prefix;       /* "homeassistant" */
    const char *topic_prefix;           /* "openvms" */
    const char *node_id;                /* "x86vms_1025" */
    const char *device_name;            /* "X86VMS" */
    const char *agent_version;
    int expire_after_s;                 /* 0: none */
};

/* rates between two samples dt_ms apart; prev NULL or a counter reset leaves
   have_rates 0 */
void ha_model_figures(const struct ha_sample *prev, const struct ha_sample *cur,
                      uint64_t dt_ms, struct ha_figures *f);

/* the problem and shadow-set figures for this sample; `t` starts zeroed.  A disk
   is a problem while it is in mount verification or offline, or for hold_ms after
   its error count rose.  A shadow set is degraded with a failed member or with
   fewer full members than it has had since the agent started. */
void ha_track_update(struct ha_track *t, const struct ha_sample *s, uint64_t now,
                     uint64_t hold_ms, struct ha_figures *f);

/* a queue's state, one of ha_queue_states, and whether it is a problem */
const char *ha_queue_state(unsigned status);
int ha_queue_problem(unsigned status);
extern const char *const ha_queue_states[];
extern const char *const ha_shadow_states[];

/* the components for this sample (disks, NICs and page/swap files come and go) */
void ha_model_components(const struct ha_sample *s, struct ha_comp_set *set);

/* the device-discovery config; `removed` lists keys published before and gone
   now, sent as {"p": platform} so HA deletes those entities (docs/MQTT.md) */
int ha_model_discovery(const struct ha_model_cfg *cfg, const struct ha_identity *id,
                       const struct ha_comp_set *set, const struct ha_comp_set *removed,
                       ha_buf *out);

/* the state document: one field per component key */
int ha_model_state(const struct ha_comp_set *set, const struct ha_sample *s,
                   const struct ha_figures *f, ha_buf *out);

/* the components in `old` and not in `cur` */
void ha_model_removed(const struct ha_comp_set *old, const struct ha_comp_set *cur,
                      struct ha_comp_set *removed);
/* 1 if both sets have the same keys in the same order */
int ha_model_same(const struct ha_comp_set *a, const struct ha_comp_set *b);

/* topics */
void ha_topic_config(char *out, size_t cap, const struct ha_model_cfg *cfg);
void ha_topic_state(char *out, size_t cap, const struct ha_model_cfg *cfg);
void ha_topic_avail(char *out, size_t cap, const struct ha_model_cfg *cfg);

/* "x86vms_1025" from the node name and system ID; 0 if the node name is unusable */
int ha_node_id(char *out, size_t cap, const char *node, const char *system_id);

/* Unix seconds as "2026-10-23T15:48:29+00:00" (HA timestamp sensors want ISO 8601
   with an offset) */
void ha_iso8601(char *out, size_t cap, int64_t t);

#endif
