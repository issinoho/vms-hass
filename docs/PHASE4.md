# Phase 4: run as a service, kit, release

**Status:** the kits install, configure, start, stop, survive a simulated boot, remove
and reinstall on both nodes; both agents now run from the kit as HASS. Still to do:
the boot lines and a real reboot of a node, the broker and HA (the user's, D9), then
the exit review and, when the user says so, the release.

## The kits

| Kit | SHA-256 |
| --- | --- |
| `ISSINOHO-X86VMS-HASS-V0001-0-1.PCSI` | `2755a115de1baa29d331dc71327af50db781f989437270be32b999fbc848136c` |
| `ISSINOHO-I64VMS-HASS-V0001-0-1.PCSI` | `55fead8be07a4e09153b361b6a290f249359413954f9816b256feaeb80efb246` |

`tools/kit.sh <node>` builds on the node, runs the native tests (314 checks, 0 failed
on both), makes the kit with `kit/make_kit.com` and fetches it to `out/kits/`. The
version is 0.1.0 (PCSI V0.1-0), from `src/core/ha_version.h` alone.

| Piece | What it does |
| --- | --- |
| `SYS$COMMON:[HASS]` | `[BIN]` the agent and its launcher; `[COM]` the procedures; `[CONF]` the template (and the site's AGENT.CONF and MQTT.PWD); `[DOC]` README.VMS and the licence; `[DATA]` and `[LOG]` |
| `SYS$STARTUP:HASS$STARTUP.COM` | at boot: defines `HASS$ROOT` and `HASS$USER`, starts the agent if the site settings say so; `INSTALL` and `REMOVE` for PCSI |
| `SYS$STARTUP:HASS$SHUTDOWN.COM` | stops the agent, which publishes offline |
| `HASS$ROOT:[COM]HASS$CONFIGURE.COM` | creates the account or re-checks one it made, sets owners and protections, makes AGENT.CONF from the template, writes `SYS$MANAGER:HASS$CONFIG.COM` |
| `HASS$ROOT:[COM]HASS$CONTROL.COM` | START, STOP, RESTART, STATUS |

## Tests

| Test | Result |
| --- | --- |
| Linux unit tests | 314 checks, 0 failed |
| `make integration` | 55 passed, 0 failed (new: the stop file, an old stop request, the plain-MQTT warning) |
| Native tests, both nodes | 314 checks, 0 failed |
| Install, configure, carry over, start (both) | runs as HASS `[363,1]` with NETMBX and TMPMBX; same node IDs; connected |
| Simulated boot (both) | STOP clean; `HASS$STARTUP` with nothing defined started it as HASS |
| Remove and reinstall (both) | stopped and deassigned; CONF, DATA and LOG kept; started again with them |
| HA | no new device and no `_2` entities; each stop left a gap of under a minute |
| Real reboot of a node, the broker and HA | *to do (the user)* |

Found and fixed on the way (NOTES.md 12): the stop file was deleted by the agent so
`HASS$RUN` restarted it; the account check ran after the node ID lookup; an
unreadable node ID would have been replaced; error exits were success on OpenVMS;
`HASS$RUN` didn't see a stop during its backoff; UIC group 376 is DECnet's.

## Deviations from the plan

- **TLS:** supported and tested on Linux, but not on the nodes: the user's broker has
  no TLS listener yet (D10). The agent warns at every start.
- **No CA bundle in the kit:** the broker is the user's own, so `MQTT_CA_FILE` names
  its CA.
