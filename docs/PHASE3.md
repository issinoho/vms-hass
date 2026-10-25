# Phase 3: VMS-specific state

**Status:** live on both nodes, and the exit test passed in HA (NOTES.md 11).
The dashboard is updated on HA. Waiting for the user's exit review.

## What was built (D8)

| Group | Sensors | Problem sensor on when |
| --- | --- | --- |
| Disks | `errors` (total_increasing), `problem` | mount verification, offline, or errors rose in the last ERROR_HOLD minutes |
| Shadow sets | `state` (enum), `members`, `progress`, `problem` | copying, merging, a failed member, or fewer full members than seen since the agent started |
| Queues | `state` (enum), `pending`, `executing`, `problem` | stopped, stopping, stalled, paused, unavailable |
| Cluster | `cluster_nodes`; `cluster_votes`, `cluster_quorum` (diagnostic) | (none) |

New settings: `QUEUES` and `ERROR_HOLD` (docs/CONFIG.md). The published key list
now records each key's platform, so binary sensors are removed as binary sensors.

## Tests

| Test | Result |
| --- | --- |
| Unit tests on Linux, plain and with ASan + UBSan | 314 checks, 0 failed |
| Unit tests on x86 E9.2-4 and IA64 V8.4-2L3 | 314 checks, 0 failed on each |
| `make fuzz` | no findings |
| `make integration` | 47 passed, 0 failed |
| A real queue stopped, started, paused, deleted (both nodes) | the agent agreed with SHOW QUEUE at every step (NOTES.md 9) |
| Live in HA, both nodes | a test queue's problem sensor went off, on, off; its entities were removed when the queue was deleted (NOTES.md 11) |

The integration test drives the stub through each problem sensor: a queue stopped
and started, disk errors rising, a shadow set losing a member, copying it back
(37%), going steady, and being dismounted.

## Not tested on a real system

- **Shadow sets:** shadowing is not loaded on either node (`SHADOWING` 0), and
  turning it on needs SYSGEN and a reboot. The user chose to build from VSI's
  `$GETDVI` documentation and test on Linux (D8). The README must say so.
- **Disk errors and mount verification:** no disk can be made to count an error on
  demand. Both are checked on Linux only; on the nodes every disk shows 0 and
  online.
- **Cluster:** neither node is a cluster member.

## Deviations from the plan

- The error log was dropped (D8).
