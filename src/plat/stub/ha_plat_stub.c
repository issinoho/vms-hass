/* ha_plat_stub.c - the platform layer on Linux, with scripted system counters.
 *
 * For the tests and for protocol work without a VMS node.  The "system" is an
 * imaginary 2-CPU OpenVMS node whose load follows a control file
 * (HA_STUB_CONTROL), re-read on every sample:
 *     cpu <percent busy>          default 20
 *     rx <bytes/s>  tx <bytes/s>  on EWA0, defaults 20000 / 5000
 *     disks <dev>[,<dev>...]      mounted disks, default DKA0:,DKA100:
 *     reset                       counters start again, as after a reboot
 * HA_STUB_NODE names the node (default STUBVMS).  Settings come from
 * HASS_AGENT_<NAME> environment variables, then the config file.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ha_agent.h"
#include "ha_platform.h"
#include "ha_text.h"

#define NCPU 2

static volatile sig_atomic_t stop_flag;
static uint64_t last_ms;
static int64_t boot_unix;
static struct ha_cpu_modes acc;         /* cumulative, ticks of 10 ms */
static uint64_t rx_acc, tx_acc;
static double cpu_pct = 20, rx_rate = 20000, tx_rate = 5000;
static char disks[256] = "DKA0:,DKA100:";

static void on_signal(int sig)
{
    (void)sig;
    stop_flag = 1;
}

uint64_t ha_plat_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

void ha_plat_sleep_ms(unsigned ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

unsigned ha_plat_random(void)
{
    return (unsigned)rand();
}

int ha_plat_read_file(const char *path, ha_buf *out)
{
    FILE *f = fopen(path, "rb");
    char tmp[4096];
    size_t n;
    if (!f)
        return 0;
    ha_buf_clear(out);
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0)
        ha_buf_put(out, tmp, n);
    fclose(f);
    return !out->oom;
}

int ha_plat_write_file(const char *path, const void *p, size_t n)
{
    char tmp[1100];
    FILE *f;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f)
        return 0;
    chmod(tmp, 0600);
    if (fwrite(p, 1, n, f) != n) {
        fclose(f);
        remove(tmp);
        return 0;
    }
    if (fclose(f) != 0 || rename(tmp, path) != 0) {
        remove(tmp);
        return 0;
    }
    return 1;
}

int ha_plat_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

int ha_plat_file_exposed(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & 044) != 0;
}

void ha_plat_data_path(char *out, size_t cap, const char *dir, const char *name)
{
    snprintf(out, cap, "%s/%s", dir, name);
}

const char *ha_plat_default_config(void)
{
    return "hass-agent.conf";
}

const char *ha_plat_default_data_dir(void)
{
    return "hass-data";
}

const char *ha_plat_default_ca_file(void)
{
    return NULL;
}

int ha_plat_conf_override(void *ctx, const char *name, ha_buf *out)
{
    char env[64];
    const char *v;
    (void)ctx;
    snprintf(env, sizeof env, "HASS_AGENT_%s", name);
    v = getenv(env);
    if (!v)
        return 0;
    ha_buf_puts(out, v);
    return 1;
}

void ha_plat_log(int level, const char *msg)
{
    static const char *const names[] = { "DEBUG", "INFO", "WARN", "ERROR" };
    time_t now = time(NULL);
    struct tm tm;
    char ts[32], clean[600];
    size_t i;
    localtime_r(&now, &tm);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
    for (i = 0; msg[i] && i < sizeof clean - 1; i++)
        clean[i] = ((unsigned char)msg[i] < 0x20 || msg[i] == 0x7F) ? '?' : msg[i];
    clean[i] = 0;
    fprintf(stderr, "%s %s %s\n", ts, names[level & 3], clean);
    fflush(stderr);
}

static void boot(void)
{
    /* a day of uptime at 5% busy */
    uint64_t day = 86400ULL * 100 * NCPU;
    memset(&acc, 0, sizeof acc);
    acc.idle = day * 95 / 100;
    acc.user = day * 3 / 100;
    acc.kernel = day * 2 / 100;
    rx_acc = 3000000000ULL;
    tx_acc = 900000000ULL;
    boot_unix = (int64_t)time(NULL) - 86400;
}

int ha_plat_init(void)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    last_ms = ha_plat_now_ms();
    boot();
    return 1;
}

int ha_plat_identity(struct ha_identity *id)
{
    const char *n = getenv("HA_STUB_NODE");
    memset(id, 0, sizeof *id);
    ha_strlcpy(id->node, n ? n : "STUBVMS", sizeof id->node);
    ha_strlcpy(id->system_id, "1025", sizeof id->system_id);
    ha_strlcpy(id->version, "OpenVMS E9.2-4 (stub)", sizeof id->version);
    ha_strlcpy(id->model, "Linux stub platform", sizeof id->model);
    ha_strlcpy(id->arch, "x86_64", sizeof id->arch);
    return 1;
}

static void read_control(void)
{
    const char *path = getenv("HA_STUB_CONTROL");
    FILE *f;
    char line[300];
    double v;
    if (!path || !(f = fopen(path, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (sscanf(line, "cpu %lf", &v) == 1)
            cpu_pct = v < 0 ? 0 : v > 100 ? 100 : v;
        else if (sscanf(line, "rx %lf", &v) == 1)
            rx_rate = v;
        else if (sscanf(line, "tx %lf", &v) == 1)
            tx_rate = v;
        else if (strncmp(line, "disks ", 6) == 0)
            ha_strlcpy(disks, line + 6, sizeof disks);
        else if (strcmp(line, "disks") == 0)
            disks[0] = 0;
        else if (strcmp(line, "reset") == 0)
            boot();
    }
    fclose(f);
}

int ha_plat_sample(struct ha_sample *s)
{
    uint64_t now = ha_plat_now_ms(), ticks = (now - last_ms) / 10;
    const char *p;
    read_control();
    if (ticks) {
        uint64_t all = ticks * NCPU, busy = (uint64_t)((double)all * cpu_pct / 100 + 0.5);
        last_ms += ticks * 10;
        acc.idle += all - busy;
        acc.user += busy * 6 / 10;
        acc.kernel += busy * 3 / 10;
        acc.exec += busy / 20;
        acc.interrupt += busy - busy * 6 / 10 - busy * 3 / 10 - busy / 20;
        rx_acc += (uint64_t)(rx_rate * (double)ticks / 100);
        tx_acc += (uint64_t)(tx_rate * (double)ticks / 100);
    }
    memset(s, 0, sizeof *s);
    s->cpu = acc;
    s->ncpu = NCPU;
    s->mem_total = 8ULL << 30;
    s->mem_free = (5ULL << 30) - (uint64_t)(cpu_pct * 10485760);  /* 10 MiB less per % busy */
    s->mem_modified = 96ULL << 20;
    s->page_total = 2ULL << 30;
    s->page_free = (2ULL << 30) - (128ULL << 20);
    s->swap_total = 1ULL << 30;
    s->swap_free = 1ULL << 30;
    s->boot_time = boot_unix;
    s->processes = 42;
    s->interactive = 2;
    s->compute_queue = cpu_pct * NCPU / 100;
    for (p = disks; *p && s->ndisk < HA_MAX_DISK;) {
        size_t n = strcspn(p, ",");
        struct ha_disk *d = &s->disk[s->ndisk];
        if (n && ha_strlcpyn(d->name, sizeof d->name, p, n)) {
            d->total = 21474836480ULL;      /* 20 GiB, 10 GiB free */
            d->free = 10737418240ULL;
            s->ndisk++;
        }
        p += n;
        if (*p == ',')
            p++;
    }
    s->nnic = 2;
    strcpy(s->nic[0].name, "EWA0");
    s->nic[0].rx = rx_acc;
    s->nic[0].tx = tx_acc;
    strcpy(s->nic[1].name, "EWB0");         /* configured, never used: no traffic */
    return 1;
}

int ha_plat_stop_requested(void)
{
    return stop_flag;
}
