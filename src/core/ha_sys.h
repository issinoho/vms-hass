/* ha_sys.h - what the platform layer reports about the system.
 *
 * Plain numbers and strings: no OpenVMS types.  Cumulative counters only grow,
 * except across a reset (a reboot, a counter wrap) that the core detects.
 */
#ifndef HA_SYS_H
#define HA_SYS_H

#include "ha_port.h"

#define HA_MAX_DISK 32
#define HA_MAX_NIC 8
#define HA_MAX_SHADOW 16
#define HA_MAX_QUEUE 32
#define HA_NAME 32

/* cumulative CPU time in processor modes, summed over the active CPUs, in ticks
   of any fixed unit (RMI$_MODES on OpenVMS, 10 ms) */
struct ha_cpu_modes {
    ha_u64 interrupt;
    ha_u64 mpsync;
    ha_u64 kernel;
    ha_u64 exec;
    ha_u64 super;
    ha_u64 user;
    ha_u64 idle;
};

struct ha_disk {
    char name[HA_NAME];                 /* device name, e.g. "DKA0:" or "$1$DGA100:" */
    char label[HA_NAME];                /* volume label, "" if none */
    ha_u64 total;                     /* bytes */
    ha_u64 free;
    uint32_t errors;                    /* device error count (a shadow set: its members' too) */
    int mntver;                         /* mount verification in progress */
    int offline;                        /* the device (or a member) is not online */
};

/* a mounted shadow set (its virtual unit is also in disk[]) */
struct ha_shadow {
    char name[HA_NAME];                 /* "DSA1:" */
    int members;                        /* full members */
    int devices;                        /* members, counting copy targets */
    int failed;                         /* members failed out of the set */
    int copying;                        /* a copy is in progress */
    int merging;                        /* a merge is in progress */
    int progress;                       /* % done of the copy or merge; -1 none */
};

/* queue states: not OpenVMS's bits (the platform translates) */
#define HA_Q_STOPPED      0x001
#define HA_Q_STOPPING     0x002         /* stopping, or a stop is pending */
#define HA_Q_STALLED      0x004
#define HA_Q_PAUSED       0x008         /* paused or pausing */
#define HA_Q_STARTING     0x010         /* starting, resuming, resetting, aligning */
#define HA_Q_UNAVAILABLE  0x020         /* unavailable, or autostart inactive */
#define HA_Q_BUSY         0x040
#define HA_Q_IDLE         0x080
#define HA_Q_CLOSED       0x100         /* accepts no new jobs */

struct ha_queue {
    char name[HA_NAME];                 /* "SYS$BATCH" */
    unsigned status;                    /* HA_Q_* */
    int generic;
    int pending;
    int executing;
};

struct ha_cluster {
    int member;                         /* 0: not in a cluster, the rest unused */
    int nodes;
    int votes;
    int quorum;
};

struct ha_nic {
    char name[HA_NAME];                 /* e.g. "EWA0" */
    ha_u64 rx;                        /* cumulative bytes received */
    ha_u64 tx;                        /* cumulative bytes sent */
};

struct ha_sample {
    struct ha_cpu_modes cpu;
    int ncpu;                           /* active CPUs */
    ha_u64 mem_total;                 /* bytes */
    ha_u64 mem_free;                  /* free list */
    ha_u64 mem_modified;              /* modified list */
    ha_u64 page_total;                /* page files, bytes */
    ha_u64 page_free;
    ha_u64 swap_total;                /* swap files, bytes */
    ha_u64 swap_free;
    ha_i64 boot_time;                  /* Unix time, seconds */
    int processes;
    int interactive;                    /* interactive processes (users signed in) */
    double compute_queue;               /* processes waiting for a CPU */
    int ndisk;                          /* mounted disks */
    struct ha_disk disk[HA_MAX_DISK];
    int nnic;
    struct ha_nic nic[HA_MAX_NIC];
    int nshadow;
    struct ha_shadow shadow[HA_MAX_SHADOW];
    int nqueue;
    struct ha_queue queue[HA_MAX_QUEUE];
    struct ha_cluster cluster;
};

/* what does not change while the agent runs */
struct ha_identity {
    char node[16];                      /* SCS node name, e.g. "X86VMS" */
    char system_id[24];                 /* SCSSYSTEMID, decimal */
    char version[32];                   /* "OpenVMS V9.2-3" */
    char model[96];                     /* hardware model */
    char arch[16];                      /* "x86_64", "IA64" */
};

#endif
