/* ha_model.c - discovery and state documents (see ha_model.h).
 *
 * Sensor options follow https://www.home-assistant.io/integrations/sensor.mqtt/
 * and the sensor device classes and units in
 * https://developers.home-assistant.io/docs/core/entity/sensor/ (docs/MQTT.md).
 */
#include <stdio.h>
#include <string.h>

#include "ha_json.h"
#include "ha_model.h"
#include "ha_text.h"

#define MiB (1024.0 * 1024.0)
#define GiB (1024.0 * 1024.0 * 1024.0)

static const char *const mode_key[7] = {
    "cpu_interrupt", "cpu_mpsync", "cpu_kernel", "cpu_exec", "cpu_super", "cpu_user", "cpu_idle"
};
static const char *const mode_name[7] = {
    "CPU interrupt", "CPU MP synchronisation", "CPU kernel", "CPU executive",
    "CPU supervisor", "CPU user", "CPU idle"
};

static uint64_t mode_at(const struct ha_cpu_modes *m, int i)
{
    switch (i) {
    case 0: return m->interrupt;
    case 1: return m->mpsync;
    case 2: return m->kernel;
    case 3: return m->exec;
    case 4: return m->super;
    case 5: return m->user;
    default: return m->idle;
    }
}

void ha_model_figures(const struct ha_sample *prev, const struct ha_sample *cur,
                      uint64_t dt_ms, struct ha_figures *f)
{
    uint64_t d[7], total = 0;
    int i, j;
    memset(f, 0, sizeof *f);
    if (!prev || dt_ms == 0)
        return;
    for (i = 0; i < 7; i++) {
        uint64_t a = mode_at(&prev->cpu, i), b = mode_at(&cur->cpu, i);
        if (b < a)
            return;                     /* a reset: wait for the next interval */
        d[i] = b - a;
        total += d[i];
    }
    if (total == 0)
        return;
    for (i = 0; i < 7; i++)
        f->cpu_mode[i] = 100.0 * (double)d[i] / (double)total;
    f->cpu_busy = 100.0 - f->cpu_mode[6];
    for (i = 0; i < cur->nnic; i++) {
        f->nic_rx[i] = f->nic_tx[i] = -1;   /* unknown until matched */
        for (j = 0; j < prev->nnic; j++) {
            if (strcmp(prev->nic[j].name, cur->nic[i].name) != 0)
                continue;
            if (cur->nic[i].rx >= prev->nic[j].rx && cur->nic[i].tx >= prev->nic[j].tx) {
                f->nic_rx[i] = (double)(cur->nic[i].rx - prev->nic[j].rx) * 1000.0 / (double)dt_ms;
                f->nic_tx[i] = (double)(cur->nic[i].tx - prev->nic[j].tx) * 1000.0 / (double)dt_ms;
            }
            break;
        }
    }
    f->have_rates = 1;
}

void ha_track_update(struct ha_track *t, const struct ha_sample *s, uint64_t now,
                     uint64_t hold_ms, struct ha_figures *f)
{
    struct ha_track old = *t;
    int i, j;
    /* rebuilt from the current sample: a disk or set that goes away is forgotten,
       and one that comes back starts afresh */
    t->ndisk = 0;
    for (i = 0; i < s->ndisk && i < HA_MAX_DISK; i++) {
        const struct ha_disk *d = &s->disk[i];
        int rose = 0;
        uint64_t rose_at = 0;
        for (j = 0; j < old.ndisk; j++) {
            if (strcmp(old.disk[j].name, d->name) != 0)
                continue;
            rose = old.disk[j].rose;
            rose_at = old.disk[j].rose_at;
            if (d->errors > old.disk[j].errors) {
                rose = 1;
                rose_at = now;
            }
            break;
        }
        ha_strlcpy(t->disk[t->ndisk].name, d->name, HA_NAME);
        t->disk[t->ndisk].errors = d->errors;
        t->disk[t->ndisk].rose = rose;
        t->disk[t->ndisk].rose_at = rose_at;
        t->ndisk++;
        f->disk_problem[i] = d->mntver || d->offline || (rose && now - rose_at < hold_ms);
    }
    t->nshadow = 0;
    for (i = 0; i < s->nshadow && i < HA_MAX_SHADOW; i++) {
        const struct ha_shadow *sh = &s->shadow[i];
        int most = sh->members;
        for (j = 0; j < old.nshadow; j++)
            if (strcmp(old.shadow[j].name, sh->name) == 0) {
                if (old.shadow[j].most > most)
                    most = old.shadow[j].most;
                break;
            }
        ha_strlcpy(t->shadow[t->nshadow].name, sh->name, HA_NAME);
        t->shadow[t->nshadow].most = most;
        t->nshadow++;
        if (sh->copying)
            f->shadow_state[i] = HA_SHADOW_COPYING;
        else if (sh->merging)
            f->shadow_state[i] = HA_SHADOW_MERGING;
        else if (sh->failed > 0 || sh->members < most)
            f->shadow_state[i] = HA_SHADOW_DEGRADED;
        else
            f->shadow_state[i] = HA_SHADOW_STEADY;
    }
}

const char *const ha_queue_states[] = {
    "stopped", "stopping", "stalled", "paused", "starting", "unavailable", "busy", "idle",
    "available", NULL
};
const char *const ha_shadow_states[] = { "steady", "copying", "merging", "degraded", NULL };

const char *ha_queue_state(unsigned st)
{
    /* the most urgent first: a stopped queue may still say idle */
    if (st & HA_Q_STOPPED) return "stopped";
    if (st & HA_Q_STOPPING) return "stopping";
    if (st & HA_Q_STALLED) return "stalled";
    if (st & HA_Q_PAUSED) return "paused";
    if (st & HA_Q_STARTING) return "starting";
    if (st & HA_Q_UNAVAILABLE) return "unavailable";
    if (st & HA_Q_BUSY) return "busy";
    if (st & HA_Q_IDLE) return "idle";
    return "available";
}

int ha_queue_problem(unsigned st)
{
    return (st & (HA_Q_STOPPED | HA_Q_STOPPING | HA_Q_STALLED | HA_Q_PAUSED | HA_Q_UNAVAILABLE)) != 0;
}

static struct ha_comp *add(struct ha_comp_set *set, int src, int idx, const char *key,
                           const char *name)
{
    struct ha_comp *c;
    if (set->n >= HA_MAX_COMP || strlen(key) >= HA_KEY)
        return NULL;
    c = &set->c[set->n++];
    memset(c, 0, sizeof *c);
    ha_strlcpy(c->key, key, sizeof c->key);
    ha_strlcpy(c->name, name, sizeof c->name);
    c->src = src;
    c->idx = idx;
    c->platform = "sensor";
    c->precision = -1;
    return c;
}

static void pct(struct ha_comp_set *set, int src, int idx, const char *key, const char *name, const char *icon)
{
    struct ha_comp *c = add(set, src, idx, key, name);
    if (!c)
        return;
    c->unit = "%";
    c->state_class = "measurement";
    c->icon = icon;
    c->precision = 1;
}

static void size(struct ha_comp_set *set, int src, int idx, const char *key, const char *name, const char *unit,
                 int precision, int diagnostic)
{
    struct ha_comp *c = add(set, src, idx, key, name);
    if (!c)
        return;
    c->device_class = "data_size";
    c->unit = unit;
    c->state_class = "measurement";
    c->precision = precision;
    c->diagnostic = diagnostic;
}

static void count(struct ha_comp_set *set, int src, const char *key, const char *name, const char *icon,
                  int diagnostic)
{
    struct ha_comp *c = add(set, src, 0, key, name);
    if (!c)
        return;
    c->state_class = "measurement";
    c->icon = icon;
    c->diagnostic = diagnostic;
}

static void problem(struct ha_comp_set *set, int src, int idx, const char *key, const char *name)
{
    struct ha_comp *c = add(set, src, idx, key, name);
    if (!c)
        return;
    c->platform = "binary_sensor";
    c->device_class = "problem";
    c->expires = 1;
}

static void choice(struct ha_comp_set *set, int src, int idx, const char *key, const char *name,
                   const char *icon, const char *const *options)
{
    struct ha_comp *c = add(set, src, idx, key, name);
    if (!c)
        return;
    c->device_class = "enum";
    c->options = options;
    c->icon = icon;
    c->expires = 1;
}

static void tally(struct ha_comp_set *set, int src, int idx, const char *key, const char *name,
                  const char *icon, int diagnostic)
{
    struct ha_comp *c = add(set, src, idx, key, name);
    if (!c)
        return;
    c->state_class = "measurement";
    c->icon = icon;
    c->precision = 0;
    c->diagnostic = diagnostic;
}

/* "DKA0:" -> "DKA0" for display */
static void display_dev(char *out, size_t cap, const char *dev)
{
    size_t n = strlen(dev);
    if (n && dev[n - 1] == ':')
        n--;
    ha_strlcpyn(out, cap, dev, n);
}

void ha_model_components(const struct ha_sample *s, struct ha_comp_set *set)
{
    char key[HA_KEY + 16], name[96], slug[HA_KEY - 12], dev[HA_NAME];
    struct ha_comp *c;
    int i;
    set->n = 0;

    pct(set, HA_SRC_CPU, 0, "cpu", "CPU", "mdi:cpu-64-bit");
    for (i = 0; i < 7; i++) {
        pct(set, HA_SRC_CPU_MODE, i, mode_key[i], mode_name[i], "mdi:cpu-64-bit");
        /* the busy total, user and kernel are on by default; the rest are there
           for whoever wants them */
        if (i != 2 && i != 5)
            set->c[set->n - 1].disabled = 1;
    }
    if ((c = add(set, HA_SRC_COMPUTE_QUEUE, 0, "compute_queue", "Compute queue")) != NULL) {
        c->state_class = "measurement";
        c->icon = "mdi:tray-full";
        c->precision = 0;
    }
    pct(set, HA_SRC_MEM_USED, 0, "mem_used", "Memory used", "mdi:memory");
    size(set, HA_SRC_MEM_FREE, 0, "mem_free", "Memory free", "MiB", 0, 0);
    size(set, HA_SRC_MEM_MODIFIED, 0, "mem_modified", "Modified page list", "MiB", 0, 0);
    set->c[set->n - 1].disabled = 1;
    size(set, HA_SRC_MEM_TOTAL, 0, "mem_total", "Memory size", "MiB", 0, 1);
    if (s->page_total)
        pct(set, HA_SRC_PAGE_USED, 0, "pagefile_used", "Page file used", "mdi:file-swap");
    if (s->swap_total)
        pct(set, HA_SRC_SWAP_USED, 0, "swapfile_used", "Swap file used", "mdi:file-swap-outline");
    if ((c = add(set, HA_SRC_BOOT_TIME, 0, "boot_time", "Booted")) != NULL)
        c->device_class = "timestamp";
    count(set, HA_SRC_PROCESSES, "processes", "Processes", "mdi:application-cog", 0);
    count(set, HA_SRC_INTERACTIVE, "interactive", "Interactive users", "mdi:account-multiple", 0);
    count(set, HA_SRC_CPUS, "cpus", "Active CPUs", "mdi:cpu-64-bit", 1);

    for (i = 0; i < s->ndisk; i++) {
        if (!ha_slug(slug, sizeof slug, s->disk[i].name))
            continue;
        display_dev(dev, sizeof dev, s->disk[i].name);
        snprintf(key, sizeof key, "disk_%s_used", slug);
        snprintf(name, sizeof name, "%s used", dev);
        pct(set, HA_SRC_DISK_USED, i, key, name, "mdi:harddisk");
        snprintf(key, sizeof key, "disk_%s_free", slug);
        snprintf(name, sizeof name, "%s free", dev);
        size(set, HA_SRC_DISK_FREE, i, key, name, "GiB", 2, 0);
        snprintf(key, sizeof key, "disk_%s_size", slug);
        snprintf(name, sizeof name, "%s size", dev);
        size(set, HA_SRC_DISK_SIZE, i, key, name, "GiB", 2, 1);
        snprintf(key, sizeof key, "disk_%s_errors", slug);
        snprintf(name, sizeof name, "%s errors", dev);
        if ((c = add(set, HA_SRC_DISK_ERRORS, i, key, name)) != NULL) {
            /* the count starts again at a reboot: total_increasing takes that as
               a new cycle, not a fall */
            c->state_class = "total_increasing";
            c->icon = "mdi:alert-circle-outline";
            c->precision = 0;
        }
        snprintf(key, sizeof key, "disk_%s_problem", slug);
        snprintf(name, sizeof name, "%s problem", dev);
        problem(set, HA_SRC_DISK_PROBLEM, i, key, name);
    }
    for (i = 0; i < s->nshadow; i++) {
        if (!ha_slug(slug, sizeof slug, s->shadow[i].name))
            continue;
        display_dev(dev, sizeof dev, s->shadow[i].name);
        snprintf(key, sizeof key, "shadow_%s_state", slug);
        snprintf(name, sizeof name, "%s shadow set", dev);
        choice(set, HA_SRC_SHADOW_STATE, i, key, name, "mdi:content-copy", ha_shadow_states);
        snprintf(key, sizeof key, "shadow_%s_members", slug);
        snprintf(name, sizeof name, "%s members", dev);
        tally(set, HA_SRC_SHADOW_MEMBERS, i, key, name, "mdi:harddisk-plus", 0);
        snprintf(key, sizeof key, "shadow_%s_progress", slug);
        snprintf(name, sizeof name, "%s copy or merge", dev);
        pct(set, HA_SRC_SHADOW_PROGRESS, i, key, name, "mdi:progress-clock");
        if (set->n && set->c[set->n - 1].src == HA_SRC_SHADOW_PROGRESS)
            set->c[set->n - 1].precision = 0;
        snprintf(key, sizeof key, "shadow_%s_problem", slug);
        snprintf(name, sizeof name, "%s shadow set problem", dev);
        problem(set, HA_SRC_SHADOW_PROBLEM, i, key, name);
    }
    for (i = 0; i < s->nqueue; i++) {
        const char *q = s->queue[i].name;
        if (!ha_slug(slug, sizeof slug, q))
            continue;
        snprintf(key, sizeof key, "queue_%s_state", slug);
        snprintf(name, sizeof name, "%s", q);
        choice(set, HA_SRC_QUEUE_STATE, i, key, name, s->queue[i].generic ? "mdi:tray-full" : "mdi:tray",
               ha_queue_states);
        snprintf(key, sizeof key, "queue_%s_pending", slug);
        snprintf(name, sizeof name, "%s pending", q);
        tally(set, HA_SRC_QUEUE_PENDING, i, key, name, "mdi:timer-sand", 0);
        snprintf(key, sizeof key, "queue_%s_executing", slug);
        snprintf(name, sizeof name, "%s executing", q);
        tally(set, HA_SRC_QUEUE_EXECUTING, i, key, name, "mdi:play-circle-outline", 0);
        snprintf(key, sizeof key, "queue_%s_problem", slug);
        snprintf(name, sizeof name, "%s problem", q);
        problem(set, HA_SRC_QUEUE_PROBLEM, i, key, name);
    }
    if (s->cluster.member) {
        tally(set, HA_SRC_CLUSTER_NODES, 0, "cluster_nodes", "Cluster nodes", "mdi:server-network", 0);
        tally(set, HA_SRC_CLUSTER_VOTES, 0, "cluster_votes", "Cluster votes", "mdi:vote", 1);
        tally(set, HA_SRC_CLUSTER_QUORUM, 0, "cluster_quorum", "Cluster quorum", "mdi:vote-outline", 1);
    }
    for (i = 0; i < s->nnic; i++) {
        if (!ha_slug(slug, sizeof slug, s->nic[i].name))
            continue;
        display_dev(dev, sizeof dev, s->nic[i].name);
        snprintf(key, sizeof key, "nic_%s_rx", slug);
        snprintf(name, sizeof name, "%s receive", dev);
        if ((c = add(set, HA_SRC_NIC_RX, i, key, name)) != NULL) {
            c->device_class = "data_rate";
            c->unit = "kB/s";
            c->state_class = "measurement";
            c->icon = "mdi:download-network";
            c->precision = 1;
        }
        snprintf(key, sizeof key, "nic_%s_tx", slug);
        snprintf(name, sizeof name, "%s transmit", dev);
        if ((c = add(set, HA_SRC_NIC_TX, i, key, name)) != NULL) {
            c->device_class = "data_rate";
            c->unit = "kB/s";
            c->state_class = "measurement";
            c->icon = "mdi:upload-network";
            c->precision = 1;
        }
    }
}

void ha_topic_config(char *out, size_t cap, const struct ha_model_cfg *cfg)
{
    snprintf(out, cap, "%s/device/%s/config", cfg->discovery_prefix, cfg->node_id);
}

void ha_topic_state(char *out, size_t cap, const struct ha_model_cfg *cfg)
{
    snprintf(out, cap, "%s/%s/state", cfg->topic_prefix, cfg->node_id);
}

void ha_topic_avail(char *out, size_t cap, const struct ha_model_cfg *cfg)
{
    snprintf(out, cap, "%s/%s/availability", cfg->topic_prefix, cfg->node_id);
}

int ha_model_discovery(const struct ha_model_cfg *cfg, const struct ha_identity *id,
                       const struct ha_comp_set *set, const struct ha_comp_set *removed,
                       ha_buf *out)
{
    ha_json j;
    char t[256], uid[128], tmpl[HA_KEY + 32];
    int i;
    ha_buf_clear(out);
    ha_json_init(&j, out);
    ha_json_obj(&j, NULL);

    ha_json_obj(&j, "dev");
    ha_json_arr(&j, "ids");
    ha_json_str(&j, NULL, cfg->node_id);
    ha_json_end(&j);
    ha_json_str(&j, "name", cfg->device_name);
    ha_json_str(&j, "mf", "VMS Software");
    if (id->model[0])
        ha_json_str(&j, "mdl", id->model);
    if (id->version[0])
        ha_json_str(&j, "sw", id->version);
    if (id->arch[0])
        ha_json_str(&j, "hw", id->arch);
    if (id->system_id[0])
        ha_json_str(&j, "sn", id->system_id);
    ha_json_end(&j);

    ha_json_obj(&j, "o");               /* origin: required for device discovery */
    ha_json_str(&j, "name", "vms-hass");
    ha_json_str(&j, "sw", cfg->agent_version);
    ha_json_str(&j, "url", "https://github.com/issinoho/vms-hass");
    ha_json_end(&j);

    ha_json_obj(&j, "cmps");
    for (i = 0; i < set->n; i++) {
        const struct ha_comp *c = &set->c[i];
        ha_json_obj(&j, c->key);
        ha_json_str(&j, "p", c->platform);
        snprintf(uid, sizeof uid, "%s_%s", cfg->node_id, c->key);
        ha_json_str(&j, "unique_id", uid);
        ha_json_str(&j, "name", c->name);
        if (c->device_class)
            ha_json_str(&j, "device_class", c->device_class);
        if (c->unit)
            ha_json_str(&j, "unit_of_measurement", c->unit);
        if (c->state_class)
            ha_json_str(&j, "state_class", c->state_class);
        if (c->icon)
            ha_json_str(&j, "icon", c->icon);
        if (c->diagnostic)
            ha_json_str(&j, "entity_category", "diagnostic");
        if (c->precision >= 0)
            ha_json_int(&j, "suggested_display_precision", c->precision);
        if (c->options) {
            int k;
            ha_json_arr(&j, "options");
            for (k = 0; c->options[k]; k++)
                ha_json_str(&j, NULL, c->options[k]);
            ha_json_end(&j);
        }
        if (c->disabled)
            ha_json_bool(&j, "enabled_by_default", 0);
        /* per component: HA 2026.10 refuses expire_after among the shared
           options at the top level (NOTES.md 3) */
        if (cfg->expire_after_s > 0 && (c->state_class || c->expires))
            ha_json_int(&j, "expire_after", cfg->expire_after_s);
        snprintf(tmpl, sizeof tmpl, "{{ value_json.%s }}", c->key);
        ha_json_str(&j, "value_template", tmpl);
        ha_json_end(&j);
    }
    for (i = 0; removed && i < removed->n; i++) {
        ha_json_obj(&j, removed->c[i].key);
        ha_json_str(&j, "p", removed->c[i].platform);
        ha_json_end(&j);
    }
    ha_json_end(&j);

    ha_topic_state(t, sizeof t, cfg);
    ha_json_str(&j, "state_topic", t);
    ha_topic_avail(t, sizeof t, cfg);
    ha_json_str(&j, "availability_topic", t);
    ha_json_end(&j);
    return ha_json_ok(&j);
}

static double used_pct(uint64_t total, uint64_t free_)
{
    if (!total)
        return 0;
    if (free_ > total)
        free_ = total;
    return 100.0 * (double)(total - free_) / (double)total;
}

int ha_model_state(const struct ha_comp_set *set, const struct ha_sample *s,
                   const struct ha_figures *f, ha_buf *out)
{
    ha_json j;
    char ts[40];
    int k;
    ha_buf_clear(out);
    ha_json_init(&j, out);
    ha_json_obj(&j, NULL);
    for (k = 0; k < set->n; k++) {
        const struct ha_comp *c = &set->c[k];
        const char *key = c->key;
        int i = c->idx;
        switch (c->src) {
        case HA_SRC_CPU:
            if (f->have_rates)
                ha_json_num(&j, key, f->cpu_busy, 1);
            else
                ha_json_null(&j, key);
            break;
        case HA_SRC_CPU_MODE:
            if (f->have_rates && i >= 0 && i < 7)
                ha_json_num(&j, key, f->cpu_mode[i], 1);
            else
                ha_json_null(&j, key);
            break;
        case HA_SRC_COMPUTE_QUEUE:
            ha_json_num(&j, key, s->compute_queue, 0);
            break;
        case HA_SRC_MEM_USED:
            /* SHOW MEMORY's "In Use": neither free nor on the modified list */
            ha_json_num(&j, key, used_pct(s->mem_total, s->mem_free + s->mem_modified), 1);
            break;
        case HA_SRC_MEM_FREE:
            ha_json_num(&j, key, (double)s->mem_free / MiB, 0);
            break;
        case HA_SRC_MEM_MODIFIED:
            ha_json_num(&j, key, (double)s->mem_modified / MiB, 0);
            break;
        case HA_SRC_MEM_TOTAL:
            ha_json_num(&j, key, (double)s->mem_total / MiB, 0);
            break;
        case HA_SRC_PAGE_USED:
            ha_json_num(&j, key, used_pct(s->page_total, s->page_free), 1);
            break;
        case HA_SRC_SWAP_USED:
            ha_json_num(&j, key, used_pct(s->swap_total, s->swap_free), 1);
            break;
        case HA_SRC_BOOT_TIME:
            ha_iso8601(ts, sizeof ts, s->boot_time);
            ha_json_str(&j, key, ts);
            break;
        case HA_SRC_PROCESSES:
            ha_json_int(&j, key, s->processes);
            break;
        case HA_SRC_INTERACTIVE:
            ha_json_int(&j, key, s->interactive);
            break;
        case HA_SRC_CPUS:
            ha_json_int(&j, key, s->ncpu);
            break;
        case HA_SRC_DISK_USED:
        case HA_SRC_DISK_FREE:
        case HA_SRC_DISK_SIZE:
            if (i < 0 || i >= s->ndisk)
                ha_json_null(&j, key);
            else if (c->src == HA_SRC_DISK_USED)
                ha_json_num(&j, key, used_pct(s->disk[i].total, s->disk[i].free), 1);
            else if (c->src == HA_SRC_DISK_FREE)
                ha_json_num(&j, key, (double)s->disk[i].free / GiB, 2);
            else
                ha_json_num(&j, key, (double)s->disk[i].total / GiB, 2);
            break;
        case HA_SRC_NIC_RX:
        case HA_SRC_NIC_TX: {
            double v = -1;
            if (f->have_rates && i >= 0 && i < s->nnic)
                v = c->src == HA_SRC_NIC_RX ? f->nic_rx[i] : f->nic_tx[i];
            if (v >= 0)
                ha_json_num(&j, key, v / 1000.0, 1);
            else
                ha_json_null(&j, key);
            break;
        }
        case HA_SRC_DISK_ERRORS:
            if (i < 0 || i >= s->ndisk)
                ha_json_null(&j, key);
            else
                ha_json_int(&j, key, (long)s->disk[i].errors);
            break;
        case HA_SRC_DISK_PROBLEM:
            ha_json_str(&j, key, i >= 0 && i < s->ndisk && f->disk_problem[i] ? "ON" : "OFF");
            break;
        case HA_SRC_SHADOW_STATE:
        case HA_SRC_SHADOW_PROBLEM: {
            int st = i >= 0 && i < s->nshadow ? f->shadow_state[i] : HA_SHADOW_STEADY;
            if (st < 0 || st > HA_SHADOW_DEGRADED)
                st = HA_SHADOW_DEGRADED;
            if (c->src == HA_SRC_SHADOW_STATE)
                ha_json_str(&j, key, ha_shadow_states[st]);
            else
                ha_json_str(&j, key, st != HA_SHADOW_STEADY ? "ON" : "OFF");
            break;
        }
        case HA_SRC_SHADOW_MEMBERS:
            if (i < 0 || i >= s->nshadow)
                ha_json_null(&j, key);
            else
                ha_json_int(&j, key, s->shadow[i].members);
            break;
        case HA_SRC_SHADOW_PROGRESS:
            if (i < 0 || i >= s->nshadow || s->shadow[i].progress < 0 ||
                !(s->shadow[i].copying || s->shadow[i].merging))
                ha_json_null(&j, key);
            else
                ha_json_int(&j, key, s->shadow[i].progress > 100 ? 100 : s->shadow[i].progress);
            break;
        case HA_SRC_QUEUE_STATE:
        case HA_SRC_QUEUE_PENDING:
        case HA_SRC_QUEUE_EXECUTING:
        case HA_SRC_QUEUE_PROBLEM:
            if (i < 0 || i >= s->nqueue)
                ha_json_null(&j, key);
            else if (c->src == HA_SRC_QUEUE_STATE)
                ha_json_str(&j, key, ha_queue_state(s->queue[i].status));
            else if (c->src == HA_SRC_QUEUE_PENDING)
                ha_json_int(&j, key, s->queue[i].pending);
            else if (c->src == HA_SRC_QUEUE_EXECUTING)
                ha_json_int(&j, key, s->queue[i].executing);
            else
                ha_json_str(&j, key, ha_queue_problem(s->queue[i].status) ? "ON" : "OFF");
            break;
        case HA_SRC_CLUSTER_NODES:
            ha_json_int(&j, key, s->cluster.nodes);
            break;
        case HA_SRC_CLUSTER_VOTES:
            ha_json_int(&j, key, s->cluster.votes);
            break;
        case HA_SRC_CLUSTER_QUORUM:
            ha_json_int(&j, key, s->cluster.quorum);
            break;
        default:
            ha_json_null(&j, key);
        }
    }
    ha_json_end(&j);
    return ha_json_ok(&j);
}

void ha_model_removed(const struct ha_comp_set *old, const struct ha_comp_set *cur,
                      struct ha_comp_set *removed)
{
    int i, k;
    removed->n = 0;
    for (i = 0; i < old->n; i++) {
        for (k = 0; k < cur->n; k++)
            if (strcmp(old->c[i].key, cur->c[k].key) == 0)
                break;
        if (k == cur->n && removed->n < HA_MAX_COMP)
            removed->c[removed->n++] = old->c[i];
    }
}

int ha_model_same(const struct ha_comp_set *a, const struct ha_comp_set *b)
{
    int i;
    if (a->n != b->n)
        return 0;
    for (i = 0; i < a->n; i++)
        if (strcmp(a->c[i].key, b->c[i].key) != 0)
            return 0;
    return 1;
}

int ha_node_id(char *out, size_t cap, const char *node, const char *system_id)
{
    char n[32], s[32];
    if (!ha_slug(n, sizeof n, node))
        return 0;
    if (system_id && ha_slug(s, sizeof s, system_id))
        snprintf(out, cap, "%s_%s", n, s);
    else
        snprintf(out, cap, "%s", n);
    return 1;
}

/* days since 1970-01-01 -> civil date (H. Hinnant's days_from_civil, inverted) */
static void civil(int64_t z, int *y, unsigned *m, unsigned *d)
{
    int64_t era, yy;
    unsigned doe, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    yy = (int64_t)yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)(yy + (*m <= 2));
}

void ha_iso8601(char *out, size_t cap, int64_t t)
{
    int64_t days = t >= 0 ? t / 86400 : -((-t + 86399) / 86400);
    int64_t secs = t - days * 86400;
    int y;
    unsigned m, d;
    civil(days, &y, &m, &d);
    snprintf(out, cap, "%04d-%02u-%02uT%02d:%02d:%02d+00:00", y, m, d,
             (int)(secs / 3600), (int)(secs / 60 % 60), (int)(secs % 60));
}
