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

- **VAXVMS added (2026-11-10, asked for by the user):** its overview section, its
  lines in the "All nodes" graphs and its own view were inserted with one
  python_transform, leaving the other nodes' cards as they were. Its six
  default-disabled sensors were enabled, as on the others. The generator's
  four-node command, for the next full rerun:

      tools/dashboard.py \
        x86vms:dka0,dka200,dka300,ldm2585:eia0:sys_batch,x86vms_batch,lpd_ts7500i,tcpip_lpd_in,tcpip_lpd_in_x86vms_1,tcpip_smtp,tcpip_smtp_x86vms_1 \
        myi64:dka800:ewb0:sys_batch,sys_print,canon,tcpip_lpd_in,tcpip_lpd_in_myi64_1,tcpip_smtp,tcpip_smtp_myi64_1 \
        axpbox:dka0,dka400,dka500:ewa0:sys_batch,tcpip_smtp,tcpip_smtp_axpbox_1 \
        vaxvms:dua1:xqa0:sys_batch

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
- **IA64 changed its clock 21 hours late:** at 22:34 UTC it still showed 23:34
  with `SYS$TIMEZONE_DIFFERENTIAL` 3600 (BST); by 22:56 UTC it showed GMT, offset 0
  (not done by us). The fixed agent was running by then, and `booted` stayed at
  22:18:20Z through the change. The C RTL's
  `time()` and `localtime()` still give the right UTC and GMT, because they follow
  `SYS$TIMEZONE_RULE`. Both nodes' rule is `GMT0BST^1,M3.5.0/01,M10.4.0/02`: the
  4th Sunday of October, not the last (it happened to be the same day in 2026).
- The agents' log timestamps are local time without a zone, so they jumped back an
  hour at 01:00 UTC on x86 and followed the C RTL rule on IA64.

## 11. Phase 3 live in HA (2026-10-25)

- Both agents restarted on the Phase 3 build at 22:45 (x86) and 22:47 UTC
  (IA64). HA has every sensor `tools/dashboard.py` names: 123 entities, none
  missing and none extra. Every disk and queue problem sensor is off.
- `probes/queue_live.com` (in batch, needs OPER) created a queue HASS_TEST, held it
  idle, stopped with a job pending, started, then deleted it, 90 s per state. In HA's
  history, on both nodes: the problem sensor went off, on, off, the state went
  idle, stopped, idle, and pending 0, 1, 0, each about 8 s after the change on the
  node. Deleting the queue removed its four entities from HA (none left in a
  search afterwards), and the procedure removed its job file and log.
- The "OpenVMS" dashboard was replaced, with the user's OK, by the generator's
  output for the Phase 3 queue list (NOTES.md 6). HA's write verified itself, and a
  search of the stored config finds the 2 Problems cards, a problem tile for each
  of the 5 disks and the 14 queue problem tiles.

## 12. Phase 4 install tests (2026-10-25/26, probes/install_test.com, in batch)

- **UIC group 376 is DECnet's** on both nodes (MIRRO$SERVER, MAIL$SERVER and others
  in `[376,*]`). The family's services use 360 (MariaDB), 361 (lighttpd), 362
  (steward) and 377 (beszel); HASS is `[363,1]`. `HASS$CONFIGURE` now refuses a new
  account's group if any account is in it.
- **AUTHORIZE `SHOW/BRIEF [g,*]` with no match** writes nothing to a redirected
  SYS$OUTPUT (the message isn't there either), so "no such user" can't be detected by
  its text: the procedure looks for the UIC `[g,` instead. `F$FAO("!OL")` pads to 11
  digits, so the group is taken from the UIC as typed.
- **The agent deleted the stop file** on its way out, so `HASS$RUN` found no stop
  request and started it again 15 s later (x86, 23:56:31). vms-beszel's agent leaves
  it, and so does this one now; `HASS$CONTROL START` deletes it before starting.
- **The account check came after the node ID.** Started as IAIN (the first configure
  had failed, so `HASS$USER` was unset), the agent couldn't read `[DATA]`, took the
  node ID as missing and tried to write a new one. The check now runs first, and a
  node ID file that exists but can't be read is an error, never replaced.
- **Exit status:** `return 1` from `main` is success on OpenVMS (`%X00000001`); errors
  now return `EXIT_FAILURE`.
- **`HASS$RUN` waited out its whole backoff** (up to 5 minutes) before seeing a stop
  request; it now checks every 5 s.
- Results, both nodes: install, configure (new account, then again on the existing
  one), carry over AGENT.CONF, MQTT.PWD, NODE_ID and PUBLISHED from the hand-run
  tree, START (as HASS via `HASS_LAUNCH`, NETMBX and TMPMBX only); STOP (clean:
  "stop requested", offline), then with `HASS$ROOT` and `HASS$USER` deassigned
  `@SYS$STARTUP:HASS$STARTUP` as a boot would (both defined again, agent started as
  HASS); `PRODUCT REMOVE` (agent stopped, both names deassigned, CONF/DATA/LOG kept),
  reinstall, configure, start. The node IDs stayed `x86vms_1025` and `myi64_1026`,
  and HA has no new device or `_2` entity.
- `tools/kit.sh` on IA64 twice came back with an empty log over ssh (as vms-beszel
  saw); a rerun worked each time.

## 13. The boot lines (2026-10-26, probes/boot_lines.com, asked for by the user)

- Both nodes' `SYSTARTUP_VMS.COM` run their products from a numbered list
  (`st_<n> = "label|procedure"`), which stops at the first missing number. The agent
  is the last service: `st_56` on x86, `st_47` on IA64. `SYSHUTDWN.COM` has
  `$ @SYS$STARTUP:HASS$SHUTDOWN.COM` at the end. Each is a new version, so the one
  before is the backup.
- IA64's `SYSHUTDWN.COM` called `BESZEL$SHTDOWN.COM`, so beszel's agent was never
  stopped cleanly at shutdown. Fixed at the user's request.
- **`SYS$MANAGER` is a search list** (SYS$SPECIFIC, then SYS$COMMON).
  `SYSTARTUP_VMS.COM` is in SYS$SPECIFIC and `SYSHUTDWN.COM` in SYS$COMMON on both.
  `OPEN/WRITE SYS$MANAGER:x` creates the new file in SYS$SPECIFIC, so the first x86
  run left `SYS$SPECIFIC:[SYSMGR]SYSHUTDWN.COM;1` (the old lines plus HASS) in front
  of `SYS$COMMON:[SYSMGR]SYSHUTDWN.COM;5`. It works, as it is found first, but
  shadows the real one. The procedure now writes where `F$SEARCH` finds the file.
  With the user's OK it was deleted, and the HASS line added as
  `SYS$COMMON:[SYSMGR]SYSHUTDWN.COM;6` (`;5` is the backup); nothing is left in
  SYS$SPECIFIC.

## 14. OpenVMS Alpha (2026-11-09, AXPBOX, V8.4-2L2, VSI C 7.4-002)

- The agent builds with no warnings and passes all 314 native checks on Alpha.
- **Memory read 100% used, 0 free** in HA on the first run. `SHOW MEMORY` showed
  501,420 of 524,288 pages free. `$GETRMI` `RMI$_FRLIST` (and `RMI$_MODLIST`)
  with an 8-byte buffer, as on IA64 and x86-64, returned 0 on Alpha (beszel's
  NOTES already say `$GETRMI` wants exact item sizes). `rmi_u()` now retries an
  8-byte item as 4 bytes when it fails or reads 0; a list that really is empty
  reads 0 either way. With it, `--print` gives mem_used 4.3, mem_free 3915 MiB
  (0.1.1).
- The node ID is `axpbox_1028`, from the node name and SCSSYSTEMID (changed from
  1025 to 1028 the evening before, so no older ID was ever published).

## 15. OpenVMS VAX: what the compiler and system have (2026-11-10, probes/vax_cc.com)

VAXVMS: VAXserver 3900 (SIMH), OpenVMS V7.3, Compaq C V6.4-005, Compaq TCP/IP
Services V5.1, 64 MB. There is no SSH, so it is driven over Telnet and FTP with
vms-site-config's `tools/vax.py` (as vms-zlib and vms-bzip2 were), not vms.sh.
`probes/vax_cc.c` was compiled once per feature:

- **No 64-bit integers at all:** `long long` and `__int64` give `%CC-E-NOLONGLONG`
  ("64-bit integral types are not supported on this platform"), and there is no
  `<stdint.h>` (`%CC-F-NOINCLFILEF`) nor `<inttypes.h>`. `long` and pointers are 4 bytes.
- **`/STANDARD=C99` is refused** with `%CC-W-C99NAONVAX`; the compiler falls back to
  "relaxed_ansi89", which still takes `//` comments, mixed declarations, `inline`,
  designated initialisers (an informational), compound literals, `__VA_ARGS__`,
  flexible array members, `restrict`, `__func__` and `<stdbool.h>`.
- **No `snprintf` or `vsnprintf`** (implicit declaration, then undefined at link),
  and no `isnan`.
- **Floating point is VAX D_float** by default: no IEEE, `DBL_MAX` 1.7e38, a 56-bit
  fraction, so a `double` holds every integer up to 2^56 exactly (`9007199254740993.0`
  printed back unchanged). `/FLOAT=G_FLOAT` is accepted.
- **BSD sockets work:** `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`,
  `select()` and `getaddrinfo` all compile and link with `/PREFIX=ALL`.
- **`$GETRMI` and `<rmidef.h>` exist** (RMI$_MODES, RMI$_FRLIST, RMI$_MODLIST compile).
- **No SSL:** no SSL3 (or any OpenSSL) kit, so only plain MQTT is possible (D10).
- Accounts: SYSTEM and IAIN are fully privileged and nothing else can log in, so the
  agent's account check (D9) refuses every existing account; `--print` skips it.
- The only LAN device is XQA0 (DELQA, CSMA/CD).

## 16. The agent on OpenVMS VAX (2026-11-10, D11)

- **What V7.3 VAX lacks, from its own headers** (`probes/vax_hdr.com`): no
  `SYS$LIB_C.TLB`; no DVI$_SHDW_MBR_COUNT, DVI$_SHDW_DEVICE_COUNT,
  DVI$_SHDW_MBR_COPY_DONE or DVI$_SHDW_MBR_MERGE_DONE (the other shadow items and
  every SYI$, RMI$, QUI$ and JPI$ item the agent uses are there); no prototype for
  `sys$getrmi` (an implicit declaration, `%CC-I-IMPLICITFUNC`); no `socklen_t`.
- **NMA counter codes:** `$NMADEF` in the node's `SYS$LIBRARY:LIB.MLB`
  (`probes/vax_nma.com`) gives NMA$C_CTLIN_BRC 1000, NMA$C_CTLIN_BSN 1001,
  NMA$M_CNT_TYP 4095, NMA$M_CNT_MAP 4096, NMA$M_CNT_WID 24576. That module has no
  confidentiality or copyright line (searched), unlike SYS$LIB_C.TLB (beszel NOTES 18).
  `LANCP SHOW DEVICE XQA0/COUNTERS` has the same "Bytes received"/"Bytes sent" lines
  the 64-bit fallback reads.
- **`_SOCKADDR_LEN` breaks the link on VAX:** it maps getaddrinfo, freeaddrinfo,
  gai_strerror and getnameinfo to `__BSD44_*` names the VAX C library doesn't have
  (`%LINK-W-NUDFSYMS`). Without it they link; the agent never reads `sa_len`.
- **The VAX C library differs in `%f`:** `%.0f` of 2.5 gives 3 (round half away from
  zero), and there is no negative zero (`%.1f` of -0.04 gives 0.0). And `0.0/0.0`
  at run time is `%SYSTEM-F-FLTDIV_F`, not a NaN: VAX floats have no NaN or infinity.
  The unit tests avoid ties and skip the NaN case on VAX.
- **Build and tests:** `build_vms.com` builds with no warnings on Compaq C 6.4, and
  all 327 unit checks pass on the VAX.
- **`--print` agrees with DCL** (with only NETMBX and TMPMBX): memory 64 MiB, 25.9%
  used (SHOW MEMORY: 33,868 of 131,072 pages in use); DUA1 1.06 GiB free (2,233,296
  blocks); page and swap files unused; 2 users; SYS$BATCH idle. With a DCL loop
  running, CPU supervisor 93.4-93.8%, executive 5.8-6.4%, interrupt 0.1-0.2%;
  MONITOR MODES over the same 14 s averaged 93.21, 6.07 and 0.21
  (`probes/vax_cpu.com`). The node ID is `vaxvms_1027`; 30 sensors.
- **`MONITOR ... /RECORD=NL:` fails on V7.3** (`-PLI-F-ENDFILE`); use
  `/SUMMARY=file` without /RECORD.
- **`HASS_LAUNCH` cannot start a process as another account on V7.3 VAX.** The
  persona is created and assumed, but the LOGINOUT process `$CREPRC` makes logs in
  as the caller (SYSTEM); the agent saw group 1 and refused, as D9 intends.
  `PRC$M_INHERIT_PERSONA` is in VAX's PRCDEF, but `$CREPRC` refuses it
  (`%SYSTEM-F-IVSTSFLG`).
- The discovery message is longer than one record in a VMS log file: `--print`
  output splits it at 8,190 characters (only the log; MQTT gets it whole).
- **`getaddrinfo()` is a stub on TCP/IP Services V5.1 for VAX:** it returns 11 for
  every name and every hint combination, numeric addresses and localhost included,
  and `gai_strerror(11)` is NULL (`probes/vax_gai.c`); the agent logged "lookup of
  ... failed: (null)". `gethostbyname()` works, so the VAX build resolves with it
  (and `inet_addr`), IPv4 only. `netdb.h` defines getaddrinfo and friends as
  macros there, so they are `#undef`ed first.
- **The agent runs as a batch job of HASS on VAX** (D11, chosen by the user): HASS
  has the BATCH login class there, and `HASS$CONTROL START` submits `HASS$RUN` to
  SYS$BATCH with `/USER=HASS`; `HASS$RUN` names its process HASS_AGENT. HASS$RUN's
  log showed "as HASS with TMPMBX,NETMBX". The job counts as SYS$BATCH's executing
  job (its queue sensors show 1 executing, state available).
- **Live in HA (2026-11-10 20:23 UTC):** device VAXVMS (`vaxvms_1027`), 24 enabled
  entities plus the 6 disabled by default, figures as `--print` gave. `HASS$CONTROL
  STOP` made HA show the sensors unavailable at once (20:25:04); START brought them
  back within 30 s, the boot time unchanged.
