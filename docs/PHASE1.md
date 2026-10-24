# Phase 1: MQTT client and discovery core (Linux)

**Status:** exit checks met 2026-10-24; awaiting the user's exit review.

## What was built

| File | What it does |
| --- | --- |
| `src/core/ha_mqtt.c` | MQTT 3.1.1 encoders (CONNECT with will, PUBLISH QoS 0/1, SUBSCRIBE, PUBACK, PINGREQ, DISCONNECT) and an incremental decoder |
| `src/core/ha_sess.c` | one session as a state machine: CONNACK, keepalive pings, PINGRESP, PUBACK and SUBACK deadlines, HA's birth message |
| `src/core/ha_model.c` | samples to figures (CPU modes, NIC rates); the discovery and state documents (D6) |
| `src/core/ha_agent.c` | the agent: what to publish when, sensor-set changes, reconnect backoff (1, 2, 5, 10, 30, 60 s + up to 20%) |
| `src/core/ha_json.c` | a streaming JSON writer |
| `src/core/ha_buf.c`, `ha_text.c`, `ha_conf.c` | from vms-beszel 47dbcf8 (D4), renamed |
| `src/plat/ha_net_ossl.c` | sockets and TLS, from vms-beszel 47dbcf8 (D4) |
| `src/plat/stub/ha_plat_stub.c` | an imaginary 2-CPU node, driven by a control file |
| `src/main/ha_main.c` | configuration (docs/CONFIG.md), node ID (D5), the socket loop, `--remove`, `--print` |

`src/core` does no I/O. Nothing in it or in `ha_platform.h` is Linux-specific.

## Tests

| Test | Result |
| --- | --- |
| `make test`: unit tests, plain and with ASan + UBSan | 246 checks, 0 failed |
| `make fuzz`: 300,000 random and mutated broker streams, ASan + UBSan | no findings |
| `make integration`: the agent against a local Mosquitto 2.0.22 | 35 passed, 0 failed |
| Live: the Linux agent against the user's HA | passed after one fix (below) |

The integration test covers:
- the retained discovery, state and availability, and the 29 sensors' keys
- rates after the second sample
- HA's birth message
- a disk dismounted (its sensors removed)
- the last will on `kill -9`, within 1.5 × keepalive
- a broker restart with every retained message lost (the agent reconnects and republishes)
- SIGTERM (offline, then DISCONNECT)
- `--remove`
- a wrong password (CONNACK 5, retried with backoff)
- a password file others can read (refused)
- a configuration typo (refused, with the line number)
- TLS: verified; a wrong host name and an untrusted CA both refused
- no sanitizer reports, and the password in no log

Live on HA 2026.10.3, with a test device "vms-hass test stub" (NOTES.md 3):
- **Found:** HA refused the first discovery message because of a top-level
  `expire_after`. Fixed, with a unit test.
- Then 29 entities appeared, 23 enabled, with the right units and device classes.
- `kill -9` made them unavailable at once; a restart brought them back.
- `--remove` deleted the device and every entity.

## Deviations from the plan

- The plan's test HA in a container: there is no Docker on the workstation, so the
  user's HA was used with a clearly named test device, removed at the end, as in
  Phase 0.
- Mosquitto for the tests is unpacked from Ubuntu's packages, not installed
  (NOTES.md 4).

## Carried into Phase 2

- Build `src/core` with VSI C on both nodes (C99, `/PREFIX=ALL_ENTRIES`, 32-bit
  pointers) and `ha_net_ossl.c` against SSL3.
- TLS to the Mosquitto app on 8883 is still untested: it needs a certificate in HA's
  `ssl` folder, which is the user's to provide.
- A sensor removed while the agent was stopped is not removed from HA: the list of
  published keys lives in memory only. Saving it in DATA_DIR would fix that; it is
  cheap, but not needed until disks come and go on a real node.
