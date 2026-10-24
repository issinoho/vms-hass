# vms-hass: plan

## Context

[Home Assistant](https://www.home-assistant.io) (HA) collects state from devices and
services, keeps history and long-term statistics, draws dashboards and runs automations.
We want OpenVMS systems to take part in that: each node appears in HA as a device whose
sensors (CPU, memory, disks, queues, cluster state, ...) can go on dashboards, raise
alerts and drive automations, next to everything else in the house or lab.

The user's HA is 2026.10.3 on HA OS (address in the git-ignored `tools/ha.conf`). It has
no MQTT broker yet (checked 2026-10-24).

Goal for the first release:

1. A native agent on each node publishes its instrumentation to an MQTT broker.
2. HA's built-in MQTT integration discovers each node as a **device** with typed
   sensors (device class, unit, state class) and no YAML from the user. The sensors
   record long-term statistics and work in the standard cards, in alerts and in
   automations.
3. A node that stops reporting goes `unavailable` in HA within one keepalive period, and
   comes back by itself after a restart of the agent, the broker or HA.
4. **Read-only.** HA cannot act on the node in the first release (D3).
5. It is measured, not asserted: the discovery payloads are checked against HA's
   documented schema, and the live HA shows both nodes.

Names (D0, approved): repository **vms-hass**, PCSI product **HASS**, logical names
and directories with the prefix **HASS$**, image `HASS$ROOT:[BIN]HASS_AGENT.EXE`, C
prefix `ha_`.

## Architecture

```
 OpenVMS node                           HA OS host
 +---------------------------+          +--------------------------------+
 | HASS_AGENT (detached,     |  MQTT    | Mosquitto add-on               |
 | own account)              | -------> |   homeassistant/device/<n>/config  (retained)
 |  samplers: $GETSYI,       | 1883 or  |   openvms/<n>/state              (JSON)
 |  $GETRMI, $GETDVI, $GETQUI|  8883    |   openvms/<n>/availability       (LWT)
 |  MQTT 3.1.1 client        |          | MQTT integration -> device +   |
 |  SSL3 (optional TLS)      | <------- |   sensors, history, dashboards |
 +---------------------------+  status  +--------------------------------+
```

- **Discovery:** one retained device-discovery message per node
  (`homeassistant/device/<node_id>/config`, HA's device-based discovery) listing every
  component. Sent at start-up and again whenever HA publishes `online` on
  `homeassistant/status`, so a restarted HA rebuilds the device without our help.
- **State:** one JSON document per interval on `openvms/<node_id>/state`; each sensor
  picks its field with a `value_template`. One message per interval keeps the broker
  and the network quiet, and the sample stays consistent.
- **Availability:** the MQTT last-will on `openvms/<node_id>/availability` (`offline`,
  retained), replaced by `online` after connecting. An orderly stop publishes `offline`
  itself.
- **Identity:** `node_id` is derived from the SCS node name and the system ID, so it is
  stable across reboots and unique in a cluster. It never changes once published:
  changing it orphans the device in HA.

## What gets published (by phase)

| Group | Sensors | Phase |
| --- | --- | --- |
| CPU | total busy %, per mode (user, supervisor, executive, kernel, interrupt, MP sync, idle), compute queue length | 2 |
| Memory | used %, free, modified list, total (diagnostic) | 2 |
| Paging | page and swap file use % | 2 |
| Disks | per mounted disk: free space, used %, free blocks; total (diagnostic) | 2 |
| Network | per LAN device: receive and transmit rates | 2 |
| System | uptime, boot time (timestamp), process count, interactive users | 2 |
| Identity (diagnostic) | VMS version, hardware model, architecture, CPUs, node name, agent version | 2 |
| Cluster | member count, votes, quorum, per-member state | 3 |
| Queues | per queue: state, pending and executing jobs; problem sensors for stopped or stalled queues | 3 |
| Devices | per disk: error count; shadow set state and members; problem sensors when errors rise or a shadow set is degraded | 3 |
| Error log | error-log entries since boot | 3 |

Phase 3's list is a starting point, to be agreed at the Phase 2 exit.

## Reuse from vms-beszel (D4)

vms-beszel already samples CPU modes, memory, page and swap files, disk use and LAN
counters on both architectures, and has a socket and TLS layer on SSL3. Those files
are copied in at a recorded beszel commit, renamed to the `ha_` prefix, and noted in
`docs/NOTES.md`; a fix in either repo is offered to the other.

## Phases

Each phase ends at an exit review with the user. Nothing starts on the next phase
before that.

### Phase 0: spec, probes, broker

- Confirm the names (D0).
- **Broker (the user's HA, ask first):** install the Mosquitto add-on and the MQTT
  integration, and create an MQTT user for the agents.
- Probes from both nodes: a TCP connect to the broker port; the SSL3 build; a
  hand-written MQTT CONNECT and PUBLISH (a 50-line probe) that appears in HA as a test
  sensor, then is deleted (an empty retained config).
- Write `docs/MQTT.md`: the MQTT 3.1.1 subset we use and HA's discovery schema, each
  fact citing its source (the MQTT specification section, HA's documentation page).
- Exit: a test sensor from each node showed up in HA and was removed cleanly.

### Phase 1: MQTT client and discovery core (Linux)

- `src/core/`: MQTT 3.1.1 encoder and decoder (CONNECT with will, PUBLISH QoS 0 and 1,
  SUBSCRIBE, PINGREQ, DISCONNECT), the session loop with keepalive and reconnect
  backoff, the discovery and state JSON writers. No I/O in the core.
- `src/plat/stub/`: scripted counters, so the agent runs on Linux.
- Tests: unit tests for the encoder; an integration test against a local Mosquitto
  (`mosquitto_sub` checks the bytes and the retained flags; broker restarts and
  dropped connections); the discovery JSON checked against HA's schema.
- Exit: the Linux build publishes a fake node that a test HA discovers. (Done on the
  user's HA with a test device, removed afterwards: there is no container runtime on
  the workstation. See docs/PHASE1.md.)

### Phase 2: the VMS agent

- `src/plat/vms/`: the samplers (copied from beszel, D4) and the identity.
- Built and run by hand on x86, then IA64, publishing to the user's broker.
- A sample dashboard in `ha/` (YAML the user can paste or that we create for them when
  asked).
- Exit: both nodes appear in HA with correct figures (checked against `MONITOR` and
  `SHOW` output), and history accumulates for a day.

### Phase 3: VMS-specific state

- Cluster, queues, device errors and shadow sets, error log (table above), with problem
  binary sensors that automations can use.
- Exit: each problem sensor has been seen to change on a test case (a stopped test
  queue, for instance) and back.

### Phase 4: run as a service, kit, release

- Its own account, a detached process, `HASS$STARTUP`, `HASS$SHUTDOWN`,
  `HASS$CONTROL STOP|START|STATUS`, a configuration file readable by the owner only
  (it holds the broker password), TLS to the broker.
- PCSI kits for both architectures and a release, following the family process
  (install test first; ask before releasing or updating the family site).
- Exit: the kit runs on both nodes across a reboot of a node, of the broker and of HA.

### Later (not scheduled)

- **Commands from HA (D3):** MQTT buttons for an allow-list of actions defined on the
  node (start or stop a queue, run a named DCL procedure). Needs its own security
  design first: who may publish to the command topics, broker ACLs, and what the agent
  account may do. Never free-form DCL.
- A HACS integration, only if MQTT turns out to lack something (D1).
- MQTT 5, if a feature needs it.
