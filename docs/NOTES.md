# Platform notes

Surprises found on the nodes, broker or HA, each with how it was observed.
For the samplers' system-service pitfalls, see vms-beszel's docs/NOTES.md (D4).

## 1. The broker (Phase 0, 2026-10-24)

- Mosquitto app (core_mosquitto) 7.1.2 on the user's HA OS (2026.10.3), watchdog on,
  starts with HA. Listens on 1883 (plain), 1884 (WebSocket), 8883 and 8884 (TLS, using
  the HA `ssl` folder's fullchain.pem/privkey.pem; not yet tested).
- HA's MQTT integration was added with the "addon" option, so HA logs in to the broker
  with its own generated account. Discovery is on, prefix `homeassistant`; HA's birth
  message is `online` (not retained) on `homeassistant/status`, its will `offline`.
- The agents log in as a broker-local user (the app's `logins` option), not an HA user.
  Name in `tools/ha.conf`, password in `out/ha/mqtt.pwd` (both git-ignored).
- Checked from the Linux workstation with a hand-built MQTT 3.1.1 CONNECT: CONNACK 0
  with the right password, CONNACK 5 (not authorised) with a wrong one.

## 2. The nodes (Phase 0 probes, 2026-10-24)

| | x86 | IA64 |
| --- | --- | --- |
| OpenVMS | E9.2-4 (QEMU) | V8.4-2L3 (rx2660) |
| TCP/IP | VSI TCP/IP Services V6.0-31 | V6.0-31 |
| SSL3 | V3.0-21 (and SSL31 V3.1-4) | V3.0-22 (and SSL31 V3.1-4) |
| VSI C | V7.7-003 | V7.4-001 |

`SYS$SHARE:SSL3$LIBSSL_SHR32.EXE` is present on both.

- **`/STANDARD=C99` needs `/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES`.** Without it, the first
  x86 link left `SOCKET`, `CONNECT`, `SEND`, `RECV`, `CLOSE`, `SLEEP`, `HTONS`,
  `INET_ADDR` and `GETHOSTBYNAME` undefined (`%ILINK-W-NUDFSYMS`), and the image failed
  with `%SYSTEM-F-CALLUNDEFSYM`. In strict C99 mode only ISO C names are mapped to
  `DECC$`. vms-beszel's build_vms.com uses the same qualifier for the same reason.
- **`$SEVERITY` is reset by `WRITE`:** `$ if $severity .ne. 1 then write ...` followed
  by `$ if $severity .ne. 1 then exit` never exits. Save it in a symbol first.
- `probes/mqtt_probe.c` (plain BSD sockets, `$GETSYI`) built unchanged on both
  architectures and on Linux. Each node connected to the broker on 1883, logged in as
  the agent user, and its test device appeared in HA with both sensors. x86 uptime as
  sent: 104,025 s; `SHOW SYSTEM` about 50 s later: 1 04:54:37 (104,077 s).
- Empty retained messages on the config and availability topics removed each device
  and its entities from HA's registry (checked with the device list and an entity search).
- Not yet exercised: the last will (the probe always disconnects cleanly) and TLS on
  8883. Both belong in the Phase 1 tests.

## 3. HA and the Phase 1 agent (2026-10-24)

- **HA refuses `expire_after` at the top level of a device-discovery message.** It
  logged `Invalid MQTT device discovery payload for vmshass_test_stub, not a valid
  option at 'expire_after'` and created no entities. The broker had accepted the
  message, so only a live HA could catch this. Moved into each component; a unit test
  now checks for it.
- With it fixed, HA created the device with 29 entities: 23 enabled and the 6 meant
  to be disabled by default. Units, device classes, the serial number (system ID) and
  `hw_version` (architecture) were as sent.
- **`kill -9` makes the device unavailable at once**, not after 1.5 × keepalive: the
  kernel closes the socket, so the broker sees the drop and publishes the will at
  the same instant (22:05:37.6 for both). The keepalive limit is for silent failures,
  such as a node that stops dead; the local tests cover that path.
- On reconnect, the disk sensors (retained state) returned at once and the CPU 5 s
  later, after the second sample (`HA_FIRST_INTERVAL_MS`).
- **Removing a device with disabled entities logs a warning** per disabled entity:
  `No device components to cleanup for vmshass_test_stub, node_id 'None'` (count 6).
  HA never loaded them, so there was nothing to unload. The device and all 29
  entities were removed. Harmless; noted so it isn't mistaken for a fault.

## 4. Local test tools (2026-10-24)

- No Docker on the workstation and no Mosquitto installed. `tools/mosquitto.sh`
  unpacks Ubuntu's mosquitto 2.0.22 packages (plus libdlt3 and libwebsockets19t64)
  into `build/mosq/root`, so the integration tests need no root and change nothing
  system-wide.
- Mosquitto 2 logs `Client <id> disconnected.` for a DISCONNECT, and something else
  for a timeout or a dropped socket; the integration test uses that line to tell an
  orderly stop from a crash.

## 5. Phase 2 on the nodes (2026-10-24)

- Both nodes build with no warnings and pass all 255 unit checks on VMS. A constant
  `0.0 / 0.0` is refused by VSI C (`%CC-W-FLOATOVERFL`); the test divides at run time.
- `--print` agrees with DCL on both nodes:
  - memory agrees with SHOW MEMORY (used = "In Use", neither free nor modified)
  - users agree with SHOW USERS
  - free space agrees with SHOW DEVICE's free blocks × 512
  - CPU modes agree with MONITOR MODES over the same 11 s on x86 (about 62% busy, mostly kernel)
- **Unprivileged process scans see only your own processes** (4 of 39 on x86), so
  interactive users come from `SHOW USERS` through popen, at most once a minute.
  SHOW is installed with privileges.
- x86 has a system-mounted `LDM2585:` (label `MD…`), the boot memory disk, which is
  88.8% full. SHOW DEVICE D doesn't list it, because its name starts with L.
- On IA64, SHOW SYSTEM's uptime is 39 s shorter than `F$GETSYI("BOOTTIME")` implies.
  The agent uses BOOTTIME.
- NICs with zero counters since boot (IA64's EWA0, EIA0, EIB0) are left out until they
  carry traffic. The published key list is saved in DATA_DIR, so sensors gone between
  runs are removed. This removed the six orphaned NIC sensors from the first run.
- Run with only NETMBX and TMPMBX: about 0.5 s of CPU per 2 minutes on x86, 0.1 s on IA64.
- A deleted batch job made HA mark the node unavailable in 2 s (x86) and 10 s (IA64).
