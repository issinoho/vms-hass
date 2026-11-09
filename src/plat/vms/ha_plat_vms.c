/* ha_plat_vms.c - the platform layer on OpenVMS (x86-64 and IA64).
 *
 * Mostly vms-beszel 47dbcf8 bz_plat_vms.c (D4); "beszel NOTES n" below is
 * vms-beszel's docs/NOTES.md.  Every source needs no privilege:
 *   $GETSYI   identity, memory size, page and swap files, boot time
 *   $GETRMI   CPU mode times (RMI$_MODES), free/modified lists, process states;
 *             every item must be passed its exact size (beszel NOTES 1)
 *   $GETDVI   every mounted disk's size and free blocks (NOTES.md 5)
 *   $QIO      LAN read-counters on each LAN device's unit 0 (beszel NOTES 12-13);
 *             once a 32-bit counter sticks at 4 GiB, LANCP's 64-bit figures
 *   SHOW USERS  interactive users: an unprivileged process scan sees only its
 *             own processes, and SHOW is installed with privileges (NOTES.md 5)
 *
 * Built with SYS$LIB_C.TLB for NMADEF; nothing from that library is copied
 * into the sources (beszel NOTES 18).
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include <descrip.h>
#include <dcdef.h>
#include <dvidef.h>
#include <dvsdef.h>
#include <efndef.h>
#include <iodef.h>
#include <jpidef.h>
#include <jbcmsgdef.h>
#include <lnmdef.h>
#include <nmadef.h>
#include <prvdef.h>
#include <quidef.h>
#include <rmidef.h>
#include <ssdef.h>
#include <starlet.h>
#include <syidef.h>

#include "ha_agent.h"
#include "ha_platform.h"
#include "ha_text.h"

/* POSIX, in the C RTL, but hidden by VSI C under /STANDARD=C99 (beszel NOTES 26) */
FILE *popen(const char *command, const char *mode);
int pclose(FILE *stream);

/* item lists: our own types, as the default STARLET headers have no IOSB
   (beszel NOTES 16) */
struct item {
    unsigned short len, code;
    void *buf;
    unsigned short *retlen;
};
struct iosb {
    unsigned short st, cnt;
    unsigned int dev;
};

#define MAX_CPU 64
static volatile sig_atomic_t stop_flag;
static unsigned int cpu_prev[MAX_CPU][8];
static struct ha_cpu_modes cpu_acc;     /* all CPUs, 64-bit, only ever grows */
static int cpu_n = -1, cpu_have;
static unsigned int page_size = 8192;

static void say(int level, const char *fmt, ...);

/* ---- time ---- */

/* SYI$_BOOTTIME is local time. mktime() applies the C RTL's
   SYS$TIMEZONE_RULE for the boot date, so a DST change since boot doesn't
   move it, as "time() minus the local uptime" did (NOTES.md 10). Seconds
   only, so the value never jitters. Now if unknown, as before. */
static int64_t boot_utc(uint64_t boot)
{
    unsigned short n[7];
    struct tm t;
    time_t u;

    if (!boot || !(sys$numtim(n, (void *)&boot) & 1))
        return (int64_t)time(NULL);
    memset(&t, 0, sizeof t);
    t.tm_year = n[0] - 1900;
    t.tm_mon = n[1] - 1;
    t.tm_mday = n[2];
    t.tm_hour = n[3];
    t.tm_min = n[4];
    t.tm_sec = n[5];
    t.tm_isdst = -1;
    u = mktime(&t);
    return (int64_t)(u == (time_t)-1 ? time(NULL) : u);
}

static uint64_t vms_time(void)
{
    uint64_t q;
    sys$gettim((void *)&q);             /* 100 ns units since 17-NOV-1858 */
    return q;
}

uint64_t ha_plat_now_ms(void)
{
    /* The system clock can be set back.  Intervals must never run backwards,
       nor stand still until the clock catches up: a step back is absorbed
       into an offset, so time goes on from where it was (beszel review I7). */
    static uint64_t last, adjust;
    uint64_t now = vms_time() / 10000 + adjust;
    if (now < last) {
        adjust += last - now;
        now = last;
    }
    last = now;
    return now;
}

void ha_plat_sleep_ms(unsigned ms)
{
    /* usleep() takes less than a second: longer values return at once */
    if (ms >= 1000)
        sleep(ms / 1000);
    if (ms % 1000)
        usleep((ms % 1000) * 1000);
}

unsigned ha_plat_random(void)
{
    return (unsigned)rand();
}

/* ---- files ---- */

int ha_plat_read_file(const char *path, ha_buf *out)
{
    FILE *f = fopen(path, "r");
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
    /* owner read/write only: (S:RWED,O:RWED,G,W) in VMS terms */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600, "rfm=stmlf");
    long w;
    if (fd < 0)
        return 0;
    w = (long)write(fd, p, n);
    if (close(fd) != 0 || w != (long)n)
        return 0;
    return 1;
}

int ha_plat_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

int ha_plat_remove(const char *path)
{
    int i;
    for (i = 0; i < 100 && ha_plat_exists(path); i++)  /* remove() takes one version */
        if (remove(path) != 0)
            return 0;
    return !ha_plat_exists(path);
}

int ha_plat_file_exposed(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & 044) != 0;
}

void ha_plat_data_path(char *out, size_t cap, const char *dir, const char *name)
{
    /* HASS$ROOT:[DATA] + "node_id" -> HASS$ROOT:[DATA]node_id.;
       a Unix-style directory gets a slash */
    size_t n = strlen(dir);
    if (n && (dir[n - 1] == ']' || dir[n - 1] == '>' || dir[n - 1] == ':'))
        snprintf(out, cap, "%s%s.", dir, name);
    else
        snprintf(out, cap, "%s/%s", dir, name);
}

const char *ha_plat_default_config(void)
{
    return "HASS$ROOT:[CONF]AGENT.CONF";
}

const char *ha_plat_default_data_dir(void)
{
    return "HASS$ROOT:[DATA]";
}

const char *ha_plat_default_ca_file(void)
{
    /* SSL3 loads no CA store for us (beszel NOTES 20): the kit's bundle, if there */
    static const char bundle[] = "HASS$ROOT:[CONF]CACERT.PEM";
    struct stat st;
    return stat(bundle, &st) == 0 ? bundle : NULL;
}

/* ---- configuration overrides: logical name HASS$<NAME> ---- */

static int trnlnm(const char *table, const char *lnm, char *val, unsigned short *vl)
{
    struct item il[2];
    struct dsc$descriptor_s tab, nam;
    unsigned int attr = LNM$M_CASE_BLIND;
    uint64_t rl = 0;        /* 8 bytes of room, as for every return length (beszel NOTES 23) */
    tab.dsc$w_length = (unsigned short)strlen(table);
    tab.dsc$b_dtype = DSC$K_DTYPE_T;
    tab.dsc$b_class = DSC$K_CLASS_S;
    tab.dsc$a_pointer = (char *)table;
    nam.dsc$w_length = (unsigned short)strlen(lnm);
    nam.dsc$b_dtype = DSC$K_DTYPE_T;
    nam.dsc$b_class = DSC$K_CLASS_S;
    nam.dsc$a_pointer = (char *)lnm;
    memset(il, 0, sizeof il);
    il[0].len = 255;
    il[0].code = LNM$_STRING;
    il[0].buf = val;
    il[0].retlen = (unsigned short *)&rl;
    if (!(sys$trnlnm(&attr, &tab, &nam, 0, il) & 1))
        return 0;
    *vl = (unsigned short)rl;
    return 1;
}

int ha_plat_conf_override(void *ctx, const char *name, ha_buf *out)
{
    /* HASS$<NAME> in the process table, then the system table: not the job
       or group tables, which other users can write (beszel review L3) */
    char lnm[64], val[256];
    unsigned short vl = 0;
    (void)ctx;
    snprintf(lnm, sizeof lnm, "HASS$%s", name);
    if (!trnlnm("LNM$PROCESS_TABLE", lnm, val, &vl) && !trnlnm("LNM$SYSTEM_TABLE", lnm, val, &vl))
        return 0;
    ha_buf_put(out, val, vl);
    return 1;
}

/* ---- logging: to SYS$OUTPUT, the detached process's log file ---- */

void ha_plat_log(int level, const char *msg)
{
    static const char *const names[] = { "DEBUG", "INFO", "WARN", "ERROR" };
    time_t now = time(NULL);
    char ts[32], clean[600];
    size_t i;
    /* single-threaded: localtime is safe, and VSI C hides localtime_r under C99 */
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&now));
    for (i = 0; msg[i] && i < sizeof clean - 1; i++)
        clean[i] = ((unsigned char)msg[i] < 0x20 || msg[i] == 0x7F) ? '?' : msg[i];
    clean[i] = 0;
    printf("%s %-5s %s\n", ts, names[level & 3], clean);
    fflush(stdout);
}

static void say(int level, const char *fmt, ...)
{
    char msg[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    ha_plat_log(level, msg);
}

/* ---- system services ---- */

static int syi(int code, void *buf, unsigned short len, unsigned short *rl)
{
    struct item il[2];
    struct iosb iosb;
    unsigned short dummy;
    int st;
    memset(il, 0, sizeof il);
    il[0].len = len;
    il[0].code = (unsigned short)code;
    il[0].buf = buf;
    il[0].retlen = rl ? rl : &dummy;
    st = sys$getsyiw(EFN$C_ENF, 0, 0, il, (void *)&iosb, 0, 0);
    return (st & 1) ? iosb.st & 1 : 0;
}

/* one $GETRMI item, at its exact size (beszel NOTES 1).  The return length gets
   8 bytes: RMI$_MODES writes more than the 16-bit word an item list provides. */
static int rmi(int code, void *buf, unsigned short len)
{
    struct item il[2];
    struct iosb iosb;
    uint64_t rl = 0;
    int st;
    memset(il, 0, sizeof il);
    il[0].len = len;
    il[0].code = (unsigned short)code;
    il[0].buf = buf;
    il[0].retlen = (unsigned short *)&rl;
    st = sys$getrmi(EFN$C_ENF, 0, 0, il, (void *)&iosb, 0, 0);
    if (st & 1) {
        sys$synch(EFN$C_ENF, (void *)&iosb);
        st = iosb.st;
    }
    return st & 1;
}

static uint64_t rmi_u(int code, int size)
{
    if (size == 8) {
        uint64_t v = 0;
        unsigned int w = 0;
        if (rmi(code, &v, 8) && v != 0)
            return v;
        /* OpenVMS Alpha returns some of these (RMI$_FRLIST, RMI$_MODLIST) as
           longwords and refuses a quadword buffer, so the free memory read 0
           (NOTES 14).  A list that really is empty reads 0 either way. */
        return rmi(code, &w, 4) ? w : 0;
    } else {
        unsigned int w = 0;
        return rmi(code, &w, 4) ? w : 0;
    }
}

static void dsc(struct dsc$descriptor_s *d, char *p, unsigned short n)
{
    d->dsc$w_length = n;
    d->dsc$b_dtype = DSC$K_DTYPE_T;
    d->dsc$b_class = DSC$K_CLASS_S;
    d->dsc$a_pointer = p;
}

/* "_X86VMS$DKA0:" -> "DKA0:", "_$1$DGA100:" -> "$1$DGA100:", "_DSA1:" -> "DSA1:" */
static void clean_dev(char *out, size_t cap, const char *name, int keep_colon)
{
    const char *s = name, *e;
    if (*s == '_')
        s++;
    if (*s != '$' && (e = strchr(s, '$')) != NULL)
        s = e + 1;                      /* a node name, not an allocation class */
    e = strchr(s, ':');
    if (e && keep_colon)
        e++;
    ha_strlcpyn(out, cap, s, e ? (size_t)(e - s) : strlen(s));
}

/* ---- LAN devices: channels kept open, 32-bit counters extended ---- */

struct nic {
    char name[32];
    unsigned short chan;
    int have;
    unsigned int raw_tx, raw_rx;    /* last 32-bit readings */
    uint64_t tx, rx;                /* extended 64-bit totals */
    int stuck;                      /* a 32-bit counter stopped at 0xFFFFFFFF: use LANCP */
    int lancp_failed;
    uint64_t lancp_ms;              /* when LANCP was last asked */
};

#define LANCP_INTERVAL_MS 30000     /* about 22 ms of CPU a read on IA64 */
#define NIC_POLL_MS 5000            /* 4 GiB takes 34 s at 1 Gb/s: never miss a wrap */
static struct nic nics[HA_MAX_NIC];
static int nnic;
static uint64_t nic_last_ms;
static unsigned char cbuf[4096];

static void nic_open_all(void)
{
    char name[65];
    struct dsc$descriptor_s nd, pat, dev;
    unsigned short nl;
    uint64_t ctx = 0;
    int devclass = DC$_SCOM;
    struct item scan[2];
    static char star[] = "*";
    memset(scan, 0, sizeof scan);
    scan[0].len = 4;
    scan[0].code = DVS$_DEVCLASS;
    scan[0].buf = &devclass;
    dsc(&nd, name, 64);
    dsc(&pat, star, 1);
    while (nnic < HA_MAX_NIC) {
        unsigned int unit = 1;
        struct item il[2];
        struct iosb iosb;
        unsigned short chan = 0;
        int st = sys$device_scan(&nd, &nl, &pat, scan, (void *)&ctx);
        if (!(st & 1))
            break;
        dsc(&dev, name, nl);
        memset(il, 0, sizeof il);
        il[0].len = 4;
        il[0].code = DVI$_UNIT;
        il[0].buf = &unit;
        if (!(sys$getdviw(EFN$C_ENF, 0, &dev, il, (void *)&iosb, 0, 0, 0) & 1) || unit != 0)
            continue;           /* template units (0) carry the device counters */
        if (!(sys$assign(&dev, &chan, 0, 0, 0) & 1))
            continue;           /* e.g. RMA0: not ours to read (beszel NOTES 15) */
        name[nl] = 0;
        clean_dev(nics[nnic].name, sizeof nics[0].name, name, 0);
        nics[nnic].chan = chan;
        nnic++;
    }
}

static int nic_read(struct nic *n, unsigned int *tx, unsigned int *rx)
{
    struct dsc$descriptor_s bd;
    struct iosb iosb;
    int st, i, got = 0;
    dsc(&bd, (char *)cbuf, sizeof cbuf);
    st = sys$qiow(EFN$C_ENF, n->chan, IO$_SENSEMODE | IO$M_CTRL | IO$M_RD_COUNT, (void *)&iosb,
                  0, 0, 0, (unsigned long)&bd, 0, 0, 0, 0);
    if (!(st & 1) || !(iosb.st & 1))
        return 0;
    /* NMA counters: a 16-bit header (type, width), then the value */
    for (i = 0; i + 2 <= (int)iosb.cnt;) {
        unsigned short h;
        int type, w, bytes;
        unsigned int v = 0;
        memcpy(&h, cbuf + i, 2);
        i += 2;
        type = h & NMA$M_CNT_TYP;
        w = (h & NMA$M_CNT_WID) >> 13;
        bytes = w == 1 ? 1 : w == 2 ? 2 : w == 3 ? 4 : 8;
        if (h & NMA$M_CNT_MAP)
            bytes += 2;
        if (i + bytes > (int)iosb.cnt)
            break;
        if (bytes == 4)
            memcpy(&v, cbuf + i, 4);
        if (type == NMA$C_CTLIN_BSN && bytes == 4) {
            *tx = v;
            got |= 1;
        } else if (type == NMA$C_CTLIN_BRC && bytes == 4) {
            *rx = v;
            got |= 2;
        }
        i += bytes;
    }
    return got == 3;
}

static void extend(unsigned int raw, unsigned int *last, uint64_t *total)
{
    /* a 32-bit counter that went down has wrapped: add the distance mod 2^32 */
    *total += (unsigned int)(raw - *last);
    *last = raw;
}

/* LANCP's figures for a device: 64-bit, readable without privilege (LANCP
   asks the LANACP process; beszel NOTES 41) */
static int lancp_read(const char *dev, uint64_t *tx, uint64_t *rx)
{
    char cmd[96], line[256];
    FILE *p;
    int got = 0;
    snprintf(cmd, sizeof cmd, "mcr lancp show device %s/counters", dev);
    p = popen(cmd, "r");
    if (!p)
        return 0;
    while (fgets(line, sizeof line, p)) {
        unsigned long long v;
        int off = 0;
        const char *rest;
        if (sscanf(line, " %llu %n", &v, &off) != 1 || off == 0)
            continue;
        rest = line + off;
        /* exactly "Bytes received" / "Bytes sent", not "Multicast bytes ..." */
        if (strncmp(rest, "Bytes received", 14) == 0) {
            *rx = v;
            got |= 2;
        } else if (strncmp(rest, "Bytes sent", 10) == 0) {
            *tx = v;
            got |= 1;
        }
    }
    pclose(p);
    return got == 3;
}

static void nic_refresh(void)
{
    int i;
    nic_last_ms = ha_plat_now_ms();
    for (i = 0; i < nnic; i++) {
        struct nic *n = &nics[i];
        unsigned int tx, rx;
        if (!nic_read(n, &tx, &rx))
            continue;
        if (!n->have) {
            n->have = 1;
            n->raw_tx = tx;
            n->raw_rx = rx;
            n->tx = tx;
            n->rx = rx;
            continue;
        }
        if (!n->stuck && (tx == 0xFFFFFFFFu || rx == 0xFFFFFFFFu)) {
            /* DECnet-style counters stop at their maximum instead of wrapping
               (beszel NOTES 41) */
            n->stuck = 1;
            n->lancp_ms = 0;
            say(HA_LOG_WARN, "LAN counters for %s reached their 32-bit maximum: reading them "
                "through LANCP from now on (every %d s)", n->name, LANCP_INTERVAL_MS / 1000);
        }
        if (n->stuck) {
            uint64_t now = ha_plat_now_ms(), ltx, lrx;
            if (n->lancp_ms && now - n->lancp_ms < LANCP_INTERVAL_MS)
                continue;
            n->lancp_ms = now;
            if (lancp_read(n->name, &ltx, &lrx)) {
                if (ltx > n->tx) n->tx = ltx;
                if (lrx > n->rx) n->rx = lrx;
                n->lancp_failed = 0;
            } else if (!n->lancp_failed) {
                n->lancp_failed = 1;
                say(HA_LOG_WARN, "LANCP couldn't read the counters for %s: its traffic isn't "
                    "counted until it can", n->name);
            }
            continue;
        }
        extend(tx, &n->raw_tx, &n->tx);
        extend(rx, &n->raw_rx, &n->rx);
    }
}

void ha_plat_poll(void)
{
    if (ha_plat_now_ms() - nic_last_ms >= NIC_POLL_MS)
        nic_refresh();
}

/* ---- disks: every disk mounted on this node, shadow set members left out ---- */

/* UCB$L_STS bits, as $GETDVI's DVI$_STS returns them (UCBDEF in SYS$LIB_C.TLB,
   the same on x86 and IA64; NOTES.md 8).  Not in the STARLET headers. */
#define UCB_M_ONLINE   0x10
#define UCB_M_MNTVERIP 0x4000

/* a shadow set's members: their state, and their errors added to the set's.
   DVI$_SHDW_NEXT_MBR_NAME on the virtual unit names the first member, on a
   member the next one, and is empty after the last. */
static void shadow_members(struct dsc$descriptor_s *vu, struct ha_shadow *sh, struct ha_disk *d)
{
    char name[65];
    unsigned int mbr = 0, all = 0, vcopy = 0, vmerge = 0;
    uint64_t nl = 0;                    /* 8 bytes of room (beszel NOTES 23) */
    struct item il[6];
    struct iosb iosb;
    struct dsc$descriptor_s dev;
    int k, copy_pct = -1, merge_pct = -1;
    memset(il, 0, sizeof il);
    il[0].len = 4;  il[0].code = DVI$_SHDW_MBR_COUNT;       il[0].buf = &mbr;
    il[1].len = 4;  il[1].code = DVI$_SHDW_DEVICE_COUNT;    il[1].buf = &all;
    il[2].len = 4;  il[2].code = DVI$_SHDW_CATCHUP_COPYING; il[2].buf = &vcopy;
    il[3].len = 4;  il[3].code = DVI$_SHDW_MERGE_COPYING;   il[3].buf = &vmerge;
    il[4].len = 64; il[4].code = DVI$_SHDW_NEXT_MBR_NAME;   il[4].buf = name;
    il[4].retlen = (unsigned short *)&nl;
    if (!(sys$getdviw(EFN$C_ENF, 0, vu, il, (void *)&iosb, 0, 0, 0) & 1) || !(iosb.st & 1))
        return;
    sh->members = (int)mbr;
    sh->devices = (int)all;
    sh->copying = vcopy != 0;
    sh->merging = vmerge != 0;
    for (k = 0; k < 16 && (unsigned short)nl > 0 && (unsigned short)nl <= 64; k++) {
        unsigned int copying = 0, merging = 0, cdone = 0, mdone = 0, failed = 0, errs = 0, sts = 0;
        struct item ml[9];
        dsc(&dev, name, (unsigned short)nl);
        nl = 0;
        memset(ml, 0, sizeof ml);
        ml[0].len = 4;  ml[0].code = DVI$_SHDW_CATCHUP_COPYING; ml[0].buf = &copying;
        ml[1].len = 4;  ml[1].code = DVI$_SHDW_MERGE_COPYING;   ml[1].buf = &merging;
        ml[2].len = 4;  ml[2].code = DVI$_SHDW_MBR_COPY_DONE;   ml[2].buf = &cdone;
        ml[3].len = 4;  ml[3].code = DVI$_SHDW_MBR_MERGE_DONE;  ml[3].buf = &mdone;
        ml[4].len = 4;  ml[4].code = DVI$_SHDW_FAILED_MEMBER;   ml[4].buf = &failed;
        ml[5].len = 4;  ml[5].code = DVI$_ERRCNT;               ml[5].buf = &errs;
        ml[6].len = 4;  ml[6].code = DVI$_STS;                  ml[6].buf = &sts;
        ml[7].len = 64; ml[7].code = DVI$_SHDW_NEXT_MBR_NAME;   ml[7].buf = name;
        ml[7].retlen = (unsigned short *)&nl;
        if (!(sys$getdviw(EFN$C_ENF, 0, &dev, ml, (void *)&iosb, 0, 0, 0) & 1) || !(iosb.st & 1))
            break;
        if (copying) {
            sh->copying = 1;
            if (copy_pct < 0 || (int)cdone < copy_pct)
                copy_pct = (int)cdone;
        }
        if (merging) {
            sh->merging = 1;
            if (merge_pct < 0 || (int)mdone < merge_pct)
                merge_pct = (int)mdone;
        }
        if (failed)
            sh->failed++;
        d->errors += errs;
        if (sts & UCB_M_MNTVERIP)
            d->mntver = 1;
        if (!(sts & UCB_M_ONLINE))
            d->offline = 1;
    }
    sh->progress = sh->copying ? copy_pct : sh->merging ? merge_pct : -1;
}

static void disks(struct ha_sample *s)
{
    char name[65];
    struct dsc$descriptor_s nd, pat, dev;
    unsigned short nl;
    uint64_t ctx = 0;
    int devclass = DC$_DISK;
    struct item scan[2];
    static char star[] = "*";
    memset(scan, 0, sizeof scan);
    scan[0].len = 4;
    scan[0].code = DVS$_DEVCLASS;
    scan[0].buf = &devclass;
    dsc(&nd, name, 64);
    dsc(&pat, star, 1);
    while (s->ndisk < HA_MAX_DISK) {
        unsigned int mnt = 0, foreign = 0, member = 0, master = 0, maxb = 0, freeb = 0;
        unsigned int errs = 0, sts = 0;
        char vol[13];
        unsigned short vl = 0;
        struct item il[10];
        struct iosb iosb;
        struct ha_disk *d = &s->disk[s->ndisk];
        int st = sys$device_scan(&nd, &nl, &pat, scan, (void *)&ctx);
        if (!(st & 1))
            break;
        dsc(&dev, name, nl);
        memset(il, 0, sizeof il);
        il[0].len = 4;  il[0].code = DVI$_MNT;         il[0].buf = &mnt;
        il[1].len = 4;  il[1].code = DVI$_FOR;         il[1].buf = &foreign;
        il[2].len = 4;  il[2].code = DVI$_SHDW_MEMBER; il[2].buf = &member;
        il[3].len = 4;  il[3].code = DVI$_MAXBLOCK;    il[3].buf = &maxb;
        il[4].len = 4;  il[4].code = DVI$_FREEBLOCKS;  il[4].buf = &freeb;
        il[5].len = 12; il[5].code = DVI$_VOLNAM;      il[5].buf = vol; il[5].retlen = &vl;
        il[6].len = 4;  il[6].code = DVI$_ERRCNT;      il[6].buf = &errs;
        il[7].len = 4;  il[7].code = DVI$_STS;         il[7].buf = &sts;
        il[8].len = 4;  il[8].code = DVI$_SHDW_MASTER; il[8].buf = &master;
        if (!(sys$getdviw(EFN$C_ENF, 0, &dev, il, (void *)&iosb, 0, 0, 0) & 1) || !(iosb.st & 1))
            continue;
        /* a shadow set member's space is its virtual unit's (DSAn:); a foreign
           mount has no file system to measure */
        if (!mnt || foreign || member || maxb == 0)
            continue;
        memset(d, 0, sizeof *d);
        name[nl] = 0;
        vol[vl < sizeof vol ? vl : sizeof vol - 1] = 0;
        clean_dev(d->name, sizeof d->name, name, 1);
        {
            const char *v = vol;
            size_t n = strlen(vol);
            ha_trim(&v, &n);
            ha_strlcpyn(d->label, sizeof d->label, v, n);
        }
        d->total = (uint64_t)maxb * 512;
        d->free = (uint64_t)(freeb < maxb ? freeb : maxb) * 512;
        d->errors = errs;
        d->mntver = (sts & UCB_M_MNTVERIP) != 0;
        d->offline = !(sts & UCB_M_ONLINE);
        if (master && s->nshadow < HA_MAX_SHADOW) {
            struct ha_shadow *sh = &s->shadow[s->nshadow++];
            memset(sh, 0, sizeof *sh);
            ha_strlcpy(sh->name, d->name, sizeof sh->name);
            sh->progress = -1;
            shadow_members(&dev, sh, d);
        }
        s->ndisk++;
    }
}

/* ---- queues: $GETQUI, every queue that runs here, and generic queues ---- */

struct qiosb {
    unsigned int st;                    /* $GETQUI's status is a longword */
    unsigned int pad;
};

static char scsnode[16];

static unsigned queue_status(unsigned int v)
{
    unsigned st = 0;
    if (v & QUI$M_QUEUE_STOPPED) st |= HA_Q_STOPPED;
    if (v & (QUI$M_QUEUE_STOPPING | QUI$M_QUEUE_STOP_PENDING)) st |= HA_Q_STOPPING;
    if (v & QUI$M_QUEUE_STALLED) st |= HA_Q_STALLED;
    if (v & (QUI$M_QUEUE_PAUSED | QUI$M_QUEUE_PAUSING)) st |= HA_Q_PAUSED;
    if (v & (QUI$M_QUEUE_STARTING | QUI$M_QUEUE_RESUMING | QUI$M_QUEUE_RESETTING |
             QUI$M_QUEUE_ALIGNING)) st |= HA_Q_STARTING;
    if (v & (QUI$M_QUEUE_UNAVAILABLE | QUI$M_QUEUE_AUTOSTART_INACTIVE)) st |= HA_Q_UNAVAILABLE;
    if (v & QUI$M_QUEUE_BUSY) st |= HA_Q_BUSY;
    if (v & QUI$M_QUEUE_IDLE) st |= HA_Q_IDLE;
    if (v & QUI$M_QUEUE_CLOSED) st |= HA_Q_CLOSED;
    return st;
}

static void queues(struct ha_sample *s)
{
    static int warned;
    static char star[] = "*";
    struct qiosb iosb;
    unsigned int flags = QUI$M_SEARCH_WILDCARD;
    int st;
    sys$getquiw(EFN$C_ENF, QUI$_CANCEL_OPERATION, 0, 0, (void *)&iosb, 0, 0);
    for (;;) {
        char qname[32], node[16];
        unsigned int status = 0, qflags = 0, pending = 0, executing = 0;
        uint64_t ql = 0, nl = 0;        /* 8 bytes of room (beszel NOTES 23) */
        struct item il[9];
        const char *n;
        size_t len;
        memset(il, 0, sizeof il);
        il[0].len = 1;  il[0].code = QUI$_SEARCH_NAME;  il[0].buf = star;
        il[1].len = 4;  il[1].code = QUI$_SEARCH_FLAGS; il[1].buf = &flags;
        il[2].len = 31; il[2].code = QUI$_QUEUE_NAME;   il[2].buf = qname;
        il[2].retlen = (unsigned short *)&ql;
        il[3].len = 4;  il[3].code = QUI$_QUEUE_STATUS; il[3].buf = &status;
        il[4].len = 4;  il[4].code = QUI$_QUEUE_FLAGS;  il[4].buf = &qflags;
        il[5].len = 15; il[5].code = QUI$_SCSNODE_NAME; il[5].buf = node;
        il[5].retlen = (unsigned short *)&nl;
        il[6].len = 4;  il[6].code = QUI$_PENDING_JOB_COUNT;   il[6].buf = &pending;
        il[7].len = 4;  il[7].code = QUI$_EXECUTING_JOB_COUNT; il[7].buf = &executing;
        st = sys$getquiw(EFN$C_ENF, QUI$_DISPLAY_QUEUE, 0, il, (void *)&iosb, 0, 0);
        if (st & 1)
            st = (int)iosb.st;
        if (!(st & 1))
            break;
        qname[(unsigned short)ql < 31 ? (unsigned short)ql : 31] = 0;
        node[(unsigned short)nl < 15 ? (unsigned short)nl : 15] = 0;
        n = node;
        len = strlen(node);
        ha_trim(&n, &len);
        /* a queue that runs on another member is that member's to report */
        if (!(qflags & QUI$M_QUEUE_GENERIC) && len &&
            !(strlen(scsnode) == len && strncmp(n, scsnode, len) == 0))
            continue;
        if (s->nqueue == HA_MAX_QUEUE) {
            if (!warned) {
                warned = 1;
                say(HA_LOG_WARN, "more than %d queues: set QUEUES to choose", HA_MAX_QUEUE);
            }
            break;
        }
        {
            struct ha_queue *q = &s->queue[s->nqueue++];
            memset(q, 0, sizeof *q);
            ha_strlcpy(q->name, qname, sizeof q->name);
            q->status = queue_status(status);
            q->generic = (qflags & QUI$M_QUEUE_GENERIC) != 0;
            q->pending = (int)pending;
            q->executing = (int)executing;
        }
    }
    if (st != JBC$_NOMOREQUE && st != JBC$_NOSUCHQUE && !warned) {
        warned = 1;
        say(HA_LOG_WARN, "$GETQUI: status %%X%08X", (unsigned)st);
    }
    sys$getquiw(EFN$C_ENF, QUI$_CANCEL_OPERATION, 0, 0, (void *)&iosb, 0, 0);
}

static void cluster(struct ha_sample *s)
{
    unsigned int member = 0, nodes = 0, votes = 0, quorum = 0;
    syi(SYI$_CLUSTER_MEMBER, &member, 4, NULL);
    if (!(member & 1))
        return;
    syi(SYI$_CLUSTER_NODES, &nodes, 4, NULL);
    syi(SYI$_CLUSTER_VOTES, &votes, 4, NULL);
    syi(SYI$_CLUSTER_QUORUM, &quorum, 4, NULL);
    s->cluster.member = 1;
    s->cluster.nodes = (int)(nodes & 0xFFFF);
    s->cluster.votes = (int)(votes & 0xFFFF);
    s->cluster.quorum = (int)(quorum & 0xFFFF);
}

/* ---- interactive users: SHOW USERS, at most once a minute ---- */

#define USERS_INTERVAL_MS 60000
static int users = -1;
static uint64_t users_ms;

static void read_users(void)
{
    FILE *p;
    char line[256];
    int n = -1;
    uint64_t now = ha_plat_now_ms();
    if (users >= 0 && now - users_ms < USERS_INTERVAL_MS)
        return;
    users_ms = now;
    p = popen("show users", "r");
    if (!p)
        return;
    /* "    Total number of users = 2,  number of processes = 3" */
    while (fgets(line, sizeof line, p)) {
        const char *t = strstr(line, "Total number of users =");
        if (t)
            n = atoi(t + 23);
    }
    pclose(p);
    if (n >= 0)
        users = n;
    else if (users < 0) {
        users = 0;
        say(HA_LOG_WARN, "couldn't read the number of users from SHOW USERS");
    }
}

/* ---- the platform interface ---- */

static void on_signal(int sig)
{
    (void)sig;
    stop_flag = 1;
}

int ha_plat_init(void)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);           /* not declared by VSI C under /STANDARD=C99 */
#endif
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    syi(SYI$_PAGE_SIZE, &page_size, 4, NULL);
    {
        char n[16];
        unsigned short l = 0;
        const char *p = n;
        size_t len;
        syi(SYI$_SCSNODE, n, sizeof n - 1, &l);
        len = l < sizeof n ? l : sizeof n - 1;
        ha_trim(&p, &len);
        ha_strlcpyn(scsnode, sizeof scsnode, p, len);
    }
    nic_open_all();
    nic_refresh();
    return 1;
}

int ha_plat_identity(struct ha_identity *id)
{
    char ver[9], hw[96], arch[16], node[16];
    unsigned short vl = 0, hl = 0, al = 0, nl = 0;
    unsigned int sysid = 0;
    const char *v;
    size_t n;
    memset(id, 0, sizeof *id);
    syi(SYI$_VERSION, ver, 8, &vl);
    syi(SYI$_HW_NAME, hw, sizeof hw - 1, &hl);
    syi(SYI$_ARCH_NAME, arch, sizeof arch - 1, &al);
    syi(SYI$_NODENAME, node, sizeof node - 1, &nl);
    /* the low 32 of SCSSYSTEMID's 48 bits: what F$GETSYI("SCSSYSTEMID") shows */
    syi(SYI$_SCSSYSTEMID, &sysid, 4, NULL);
    v = ver;
    n = vl;
    ha_trim(&v, &n);
    hw[hl] = 0;
    arch[al] = 0;
    node[nl] = 0;
    ha_strlcpy(id->node, node, sizeof id->node);
    if (sysid)
        snprintf(id->system_id, sizeof id->system_id, "%u", sysid);
    snprintf(id->version, sizeof id->version, "OpenVMS %.*s", (int)n, v);
    ha_strlcpy(id->model, hw, sizeof id->model);
    ha_strlcpy(id->arch, arch, sizeof id->arch);
    return 1;
}

int ha_plat_sample(struct ha_sample *s)
{
    static unsigned char modes[4 + MAX_CPU * 33 + 512];
    static int warned_cpus;
    unsigned int ncpu = 0, i;
    uint64_t memsize = 0, boot = 0;
    unsigned int pf_page = 0, pf_free = 0, sw_page = 0, sw_free = 0;
    memset(s, 0, sizeof *s);

    /* RMI$_MODES: a CPU count, then per CPU an id byte and eight longwords of
       10 ms ticks: [0] interrupt+idle, [1] MP synch, [2] kernel, [3] exec,
       [4] super, [5] user, [6] compat, [7] idle.  The return length is wrong
       on both architectures, so the count decides (beszel NOTES 3, 5). */
    memset(modes, 0, sizeof modes);
    if (!rmi(RMI$_MODES, modes, (unsigned short)sizeof modes))
        return 0;
    memcpy(&ncpu, modes, 4);
    if (ncpu > MAX_CPU) {
        if (!warned_cpus) {
            warned_cpus = 1;
            say(HA_LOG_WARN, "more than %d CPUs: the CPU figures cover the first %d", MAX_CPU, MAX_CPU);
        }
        ncpu = MAX_CPU;
    }
    if ((int)ncpu != cpu_n) {           /* the CPU set changed: new baselines */
        cpu_n = (int)ncpu;
        cpu_have = 0;
    }
    for (i = 0; i < ncpu; i++) {
        unsigned int l[8], d[8];
        int k;
        memcpy(l, modes + 4 + i * 33 + 1, sizeof l);
        /* deltas mod 2^32, accumulated in 64 bits.  [0] includes idle, and on
           x86 [7] can advance more than [0] (beszel NOTES 4, 29): interrupt is
           [0] - [7], clamped at 0. */
        for (k = 0; k < 8; k++)
            d[k] = cpu_have ? l[k] - cpu_prev[i][k] : 0;
        memcpy(cpu_prev[i], l, sizeof l);
        cpu_acc.interrupt += d[0] > d[7] ? d[0] - d[7] : 0;
        cpu_acc.mpsync += d[1];
        cpu_acc.kernel += d[2];
        cpu_acc.exec += d[3];
        cpu_acc.super += d[4];
        cpu_acc.user += (uint64_t)d[5] + d[6];  /* compatibility mode counts as user */
        cpu_acc.idle += d[7];
    }
    cpu_have = 1;
    s->cpu = cpu_acc;
    s->ncpu = (int)ncpu;

    /* memory: SHOW MEMORY's Free and Modified lists */
    syi(SYI$_MEMSIZE, &memsize, 8, NULL);
    s->mem_total = memsize * page_size;
    s->mem_free = rmi_u(RMI$_FRLIST, 8) * page_size;
    s->mem_modified = rmi_u(RMI$_MODLIST, 8) * page_size;

    syi(SYI$_PAGEFILE_PAGE, &pf_page, 4, NULL);
    syi(SYI$_PAGEFILE_FREE, &pf_free, 4, NULL);
    syi(SYI$_SWAPFILE_PAGE, &sw_page, 4, NULL);
    syi(SYI$_SWAPFILE_FREE, &sw_free, 4, NULL);
    s->page_total = (uint64_t)pf_page * page_size;
    s->page_free = (uint64_t)(pf_free < pf_page ? pf_free : pf_page) * page_size;
    s->swap_total = (uint64_t)sw_page * page_size;
    s->swap_free = (uint64_t)(sw_free < sw_page ? sw_free : sw_page) * page_size;

    syi(SYI$_BOOTTIME, &boot, 8, NULL);
    s->boot_time = boot_utc(boot);

    s->processes = (int)rmi_u(RMI$_PROCS, 4);
    s->compute_queue = (double)(rmi_u(RMI$_COM, 4) + rmi_u(RMI$_COMO, 4));
    read_users();
    s->interactive = users;

    disks(s);
    queues(s);
    cluster(s);
    nic_refresh();
    for (i = 0; i < (unsigned)nnic; i++) {
        /* a device that has carried nothing since boot is configured but
           unused (beszel NOTES 15): left out until it has traffic, when the
           agent adds its sensors */
        if (!nics[i].have || (nics[i].rx == 0 && nics[i].tx == 0))
            continue;
        ha_strlcpy(s->nic[s->nnic].name, nics[i].name, sizeof s->nic[0].name);
        s->nic[s->nnic].tx = nics[i].tx;
        s->nic[s->nnic].rx = nics[i].rx;
        s->nnic++;
    }
    return s->ncpu > 0;
}

int ha_plat_stop_requested(void)
{
    return stop_flag;
}

int ha_plat_check_account(char *err, size_t errlen)
{
    /* D9, from vms-beszel D12: authorized privileges beyond NETMBX and TMPMBX
       can be turned back on by anything running in the process, and a
       system-group UIC reaches files a normal account can't.  Refuse both. */
    uint64_t auth = 0, allowed = (uint64_t)PRV$M_NETMBX | (uint64_t)PRV$M_TMPMBX;
    unsigned int grp = 0, maxsys = 8;
    struct item il[3];
    struct iosb iosb;
    uint64_t rl1 = 0, rl2 = 0;          /* 8 bytes of room (beszel NOTES 23) */
    memset(il, 0, sizeof il);
    il[0].len = 8; il[0].code = JPI$_AUTHPRIV; il[0].buf = &auth; il[0].retlen = (unsigned short *)&rl1;
    il[1].len = 4; il[1].code = JPI$_GRP; il[1].buf = &grp; il[1].retlen = (unsigned short *)&rl2;
    if (!(sys$getjpiw(EFN$C_ENF, 0, 0, il, (void *)&iosb, 0, 0) & 1) || !(iosb.st & 1)) {
        snprintf(err, errlen, "can't read this process's privileges");
        return 0;
    }
    syi(SYI$_MAXSYSGROUP, &maxsys, 4, NULL);
    if (grp <= maxsys) {
        snprintf(err, errlen, "this account's UIC group (%o) is a system group: run the agent "
                 "as its own account (HASS$USER; DECISIONS.md D9)", grp);
        return 0;
    }
    if (auth & ~allowed) {
        snprintf(err, errlen, "this account is authorized for privileges beyond NETMBX and "
                 "TMPMBX: run the agent as an account that has only those (HASS$USER; "
                 "DECISIONS.md D9)");
        return 0;
    }
    return 1;
}
