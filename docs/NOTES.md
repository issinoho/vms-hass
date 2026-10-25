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

## 6. The dashboard (2026-10-25)

- `tools/dashboard.py` generates the "OpenVMS" dashboard (`openvms-servers`) from
  each node's entity stem, disks and NICs. It has an overview of every node, then one
  view per node with every sensor. It was created on the user's HA with the user's OK.
- Per the user's request for all details, the six default-disabled sensors per node
  were enabled in HA's entity registry. That is reversible in the UI, and the agent's
  defaults are unchanged.
- The summary card is a markdown template reading the device registry (`device_attr`:
  model, sw_version, hw_version, serial_number) and the sensors. It was checked with
  HA's template engine before use.
- When a node's disks or NICs change, rerun the generator: the dashboard does not
  follow sensors by itself.
- Phase 3 queue list, chosen by the user on 2026-10-25 (every queue on both nodes):

      tools/dashboard.py \
        x86vms:dka0,dka200,dka300,ldm2585:eia0:sys_batch,x86vms_batch,lpd_ts7500i,tcpip_lpd_in,tcpip_lpd_in_x86vms_1,tcpip_smtp,tcpip_smtp_x86vms_1 \
        myi64:dka800:ewb0:sys_batch,sys_print,canon,tcpip_lpd_in,tcpip_lpd_in_myi64_1,tcpip_smtp,tcpip_smtp_myi64_1

## 7. What Phase 3 has to work with (2026-10-25, probes/p3_survey*.com)

- Both nodes are single-member clusters (SHOW CLUSTER lists only themselves).
- No shadow sets. `F$GETSYI("SHADOWING")` is 0 on both, so shadowing is not loaded and
  turning it on needs SYSGEN and a reboot. IA64 has a VOLSHAD licence (to
  30-APR-2027); x86 lists none. SHDRIVER.EXE is not in SYS$LOADABLE_IMAGES on either.
- Every mounted disk has error count 0. There is no way to make one count an error
  on demand. LDDRIVER is present on both (x86 has LDM0 and its boot disk LDM2585).
- Queues: SYS$BATCH and X86VMS_BATCH on x86; on IA64 SYS$BATCH, the CANON printer,
  TCPIP$LPD_IN and TCPIP$SMTP. All available or idle.
- `SHOW DEVICE /ERRORS` is not a qualifier (`%DCL-W-IVQUAL`); the count is in /FULL.
- The error log is SYS$SYSROOT:[SYSERR]ERRLOG.SYS (512 blocks on IA64).

## 8. Item codes for Phase 3 (2026-10-25, probes/hdr_extract.com, hdr_ucb.com)

- DVIDEF, QUIDEF and SYIDEF from SYS$STARLET_C.TLB are identical on x86 and IA64
  for every item used: DVI$_ERRCNT, DVI$_STS, the DVI$_SHDW_* items, QUI$_* and
  QUI$M_QUEUE_*, and SYI$_CLUSTER_*.
- DVI$_STS returns UCB$L_STS. Its bits are in UCBDEF, in SYS$LIB_C.TLB, not in the
  STARLET headers: UCB$M_ONLINE 0x10, UCB$M_MNTVERIP 0x4000. The agent defines the
  two it needs.
- `LIBRARY/EXTRACT` output from SYS$LIB_C.TLB holds NUL bytes, so `grep` calls it
  binary and prints nothing. Use `grep -a`.
- $GETQUI's IOSB status is a longword (JBC$_ codes), unlike $GETDVI's word.

## 9. Phase 3 on the nodes (2026-10-25)

- Both nodes build clean and pass the 314 unit checks.
- `--print` with only NETMBX and TMPMBX lists every queue on the node, with
  states that agree with SHOW QUEUE: 7 on x86, 7 on IA64. $GETQUI needs no
  privilege to display queues.
- A batch queue running a job below its job limit is neither busy nor idle in
  QUI$_QUEUE_STATUS, so it reports "available", as SHOW QUEUE does. Generic queues
  report idle.
- `probes/queue_test.com` (in batch) created HASS_TEST, stopped it with a job
  submitted, started it, paused it and deleted it. At each step the agent agreed
  with SHOW QUEUE: idle/OFF, stopped/1 pending/ON, idle/OFF, paused/ON, then gone.
  Same on both nodes; the queue and its files were removed.
- Neither node is a cluster member (`CLUSTER_MEMBER` FALSE, `VAXCLUSTER` 0), so there
  are no cluster sensors. Every disk reports 0 errors, online, not in mount
  verification.

## 10. A day of history (2026-10-24 22:33 to 2026-10-25 22:33 UTC)

Observed with HA's recorder (hourly statistics and state history) and the agents'
batch logs (AGENT_RUN.LOG on each node).

- **History is complete:** 23 hourly statistics rows for CPU and memory on each
  node, no hour missing.
- **Every outage came from the broker's side**, on both nodes at once, and the
  agents recovered on their own:
  - broker restarts (00:11, 00:16, 18:07, 18:10, 18:13, 19:14 UTC): "the broker
    closed the connection", reconnected within 2 s. HA showed the nodes unavailable
    for about 40 s each time, from before the agents saw the close: HA loses the
    broker first while it shuts down.
  - HA restarts (23:24 and 10:06 UTC): the agents saw `homeassistant/status`
    online and published discovery again; HA was blank for 2-3 s.
  - 18:15 to 19:12 UTC the broker host did not answer: every connect timed out,
    retried every 60-72 s until the broker answered (19:12 IA64, 19:16 x86).
- **The DST change moved x86's boot time by an hour.** At 01:00 UTC x86 went from
  BST to GMT (`SYS$TIMEZONE_DIFFERENTIAL` 0). The agent computes the boot time as
  `time(NULL)` minus (local now - SYI$_BOOTTIME), so the local clock going back
  an hour moved `booted` from 15:48:29Z (right) to 16:48:29Z (wrong). SHOW SYSTEM's
  uptime (2 06:45:30 at 22:34 GMT) is still right.
- **The same arithmetic makes `booted` jitter by a second** (…:29 / …:30), a few
  state changes a day for a value that never changes.
- **Fixed:** the agent now converts SYI$_BOOTTIME with `mktime()` (`tm_isdst` -1),
  which applies the rule for the boot date. `probes/boot_probe.c` showed it right on
  both nodes (x86 15:48:29Z where the old sum gave 16:48:29Z; IA64 22:18:20Z), and
  for dates either side of the change. `--print` from the new build agrees.
- **IA64 did not change its clock:** at 22:34 UTC it showed 23:34 with
  `SYS$TIMEZONE_DIFFERENTIAL` 3600 (BST), so its `booted` did not move. The C RTL's
  `time()` and `localtime()` still give the right UTC and GMT, because they follow
  `SYS$TIMEZONE_RULE`. Both nodes' rule is `GMT0BST^1,M3.5.0/01,M10.4.0/02`: the
  4th Sunday of October, not the last (it happened to be the same day in 2026).
- The agents' log timestamps are local time without a zone, so they jumped back an
  hour at 01:00 UTC on x86 and followed the C RTL rule on IA64.
