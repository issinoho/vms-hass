# Phase 2: the VMS agent

**Status:** checked against DCL on both nodes; the day of history is still to be
checked (due 2026-10-25 22:30); the user's exit review comes after that.

## What was built

| File | What it does |
| --- | --- |
| `src/plat/vms/ha_plat_vms.c` | the VMS platform layer: identity, samples, files, logging, stop requests |
| `build_vms.com` | builds the agent and the unit tests with VSI C (C99, 32-bit pointers, `/PREFIX=ALL_ENTRIES`, IEEE floats) against SSL3 |
| `probes/agent.com` | runs the agent by hand: `PRINT`, `RUN` (with only NETMBX and TMPMBX), `REMOVE`, `COMPARE` |
| `probes/cpu_compare.com`, `users_probe.com` | the checks against MONITOR MODES and SHOW USERS |
| `tools/push.sh` | copies the sources to a node |
| `tools/dashboard.py` | generates the "OpenVMS" dashboard (NOTES.md 6) |
| `src/core/ha_agent.c` | now also saves the published sensor keys in DATA_DIR (below) |

None of the sources it reads needs a privilege:

| Figure | Source |
| --- | --- |
| CPU modes, compute queue, process count | `$GETRMI` (MODES, COM, COMO, PROCS) |
| memory free and modified | `$GETRMI` (FRLIST, MODLIST) |
| identity, memory size, page and swap files, boot time | `$GETSYI` |
| mounted disks | `$DEVICE_SCAN` (DC$_DISK) and `$GETDVI` |
| LAN counters | `$QIO` sense-mode, with LANCP as a fallback; read every 5 s so a 32-bit wrap is never missed |
| interactive users | `SHOW USERS` through popen, at most once a minute (an unprivileged process scan sees only your own processes) |

## Tests

| Test | Result |
| --- | --- |
| Unit tests on Linux, plain and with ASan + UBSan | 255 checks, 0 failed |
| Unit tests on x86 E9.2-4 and IA64 V8.4-2L3 | 255 checks, 0 failed on each |
| `make fuzz` | no findings |
| `make integration` | 38 passed, 0 failed |
| `--print` against DCL, both nodes | agrees (below) |
| Both nodes in HA, run with only NETMBX and TMPMBX | yes: x86vms and myi64 |
| Agent killed | HA marked the node unavailable in 2 s (x86) and 10 s (IA64) |
| A day of history | *to do: 2026-10-25 22:30* |

Checked against DCL:
- memory used agrees with SHOW MEMORY's "In Use"
- interactive users agree with SHOW USERS
- free space agrees with SHOW DEVICE's free blocks × 512
- CPU modes agree with MONITOR MODES over the same 11 s on x86 (about 62% busy, mostly kernel)
- uptime follows `F$GETSYI("BOOTTIME")`. On IA64 SHOW SYSTEM shows 39 s less; the agent is right.

Cost: about 0.5 s of CPU per 2 minutes on x86 and 0.1 s on IA64.

## Found on the way

- **The published key list is now saved.** Phase 1 kept it in memory only, so a
  sensor gone while the agent was stopped stayed in HA. It is now in
  DATA_DIR/published, with an integration test (a disk dismounted while the agent is
  stopped). It removed six orphaned NIC sensors left over from the first IA64 run.
- **NICs that have never carried traffic are left out** (IA64's EWA0, EIA0 and EIB0),
  until they do.
- **x86 has a boot memory disk, `LDM2585:`, 88.8% full.** SHOW DEVICE D doesn't list
  it. It is a real figure, not a bug, but worth a look.
- VSI C refuses a constant `0.0 / 0.0`; the NaN test divides at run time.

## Deviations from the plan

- The plan's sample dashboard as YAML in `ha/`: replaced by `tools/dashboard.py`, which
  generates one for the node's actual disks and NICs. The dashboard is live on the
  user's HA, created with the user's OK. `ha/` is empty and can go.
- The six sensors per node that HA creates disabled (four CPU modes, idle, modified
  list) were enabled in HA's entity registry at the user's request. The agent's
  defaults are unchanged.

## Carried into Phase 3

- The agents run as batch jobs under IAIN (x86: X86VMS_BATCH entry 254; IA64:
  SYS$BATCH entry 868), logging to `AGENT_RUN.LOG`. They become a detached service in
  Phase 4.
- TLS to the broker is still untested on the nodes (needs a certificate in HA's `ssl`
  folder, from the user).
- The dashboard does not follow new disks or NICs: rerun `tools/dashboard.py`.
