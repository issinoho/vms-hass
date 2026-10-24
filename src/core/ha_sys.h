/* ha_sys.h - what the platform layer reports about the system.
 *
 * Plain numbers and strings: no OpenVMS types.  Cumulative counters only grow,
 * except across a reset (a reboot, a counter wrap) that the core detects.
 */
#ifndef HA_SYS_H
#define HA_SYS_H

#include <stdint.h>

#define HA_MAX_DISK 32
#define HA_MAX_NIC 8
#define HA_NAME 32

/* cumulative CPU time in processor modes, summed over the active CPUs, in ticks
   of any fixed unit (RMI$_MODES on OpenVMS, 10 ms) */
struct ha_cpu_modes {
    uint64_t interrupt;
    uint64_t mpsync;
    uint64_t kernel;
    uint64_t exec;
    uint64_t super;
    uint64_t user;
    uint64_t idle;
};

struct ha_disk {
    char name[HA_NAME];                 /* device name, e.g. "DKA0:" or "$1$DGA100:" */
    char label[HA_NAME];                /* volume label, "" if none */
    uint64_t total;                     /* bytes */
    uint64_t free;
};

struct ha_nic {
    char name[HA_NAME];                 /* e.g. "EWA0" */
    uint64_t rx;                        /* cumulative bytes received */
    uint64_t tx;                        /* cumulative bytes sent */
};

struct ha_sample {
    struct ha_cpu_modes cpu;
    int ncpu;                           /* active CPUs */
    uint64_t mem_total;                 /* bytes */
    uint64_t mem_free;                  /* free list */
    uint64_t mem_modified;              /* modified list */
    uint64_t page_total;                /* page files, bytes */
    uint64_t page_free;
    uint64_t swap_total;                /* swap files, bytes */
    uint64_t swap_free;
    int64_t boot_time;                  /* Unix time, seconds */
    int processes;
    int interactive;                    /* interactive processes (users signed in) */
    double compute_queue;               /* processes waiting for a CPU */
    int ndisk;                          /* mounted disks */
    struct ha_disk disk[HA_MAX_DISK];
    int nnic;
    struct ha_nic nic[HA_MAX_NIC];
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
