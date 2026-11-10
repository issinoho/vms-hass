# Decisions

Each decision with its status, the evidence and the alternatives considered.

## D0. Names: vms-hass, HASS, HASS$

**Status:** approved by the user with the plan (2026-10-24).

Repository `vms-hass`, PCSI product `HASS` (producer ISSINOHO), rooted logical
`HASS$ROOT`, image `[BIN]HASS_AGENT.EXE`, C prefix `ha_`. The product name is kept short
because PCSI refuses kit names over 39 characters (vms-beszel D0, vms-lighttpd).

Alternatives: `vms-homeassistant` (long, and the product name would need shortening
anyway); `vms-mqtt` (names the transport, not the purpose, and would suit a general
MQTT client library better).

## D1. Transport: MQTT with HA's MQTT discovery

**Status:** chosen by the user (2026-10-24).

The agent publishes to an MQTT broker (the Mosquitto add-on on HA OS). HA's built-in
MQTT integration turns the discovery messages into a device with sensors.

Why: it is HA's standard way for external systems to push state; nothing has to be
written or maintained on the HA side; availability, devices, units and long-term
statistics come from HA's existing support; it works in both directions when commands
are added (D3); and other tools (Node-RED, Grafana, InfluxDB bridges) can read the same
topics.

Alternatives:
- **A custom HA integration (Python, through HACS)** that the agent pushes to over
  HTTP or WebSocket: no broker, a set-up wizard and branding, but two codebases, and
  the Python side must follow HA's internal API changes every few releases.
- **HA's REST API (`POST /api/states`)** with a long-lived token: entities created this
  way have no device, no unique ID and are lost on restart. HA advises against it for
  integrations.
- **HA polling an HTTP endpoint on the node** (vms-steward's API or a small CGI):
  pull instead of push, and HA has to reach every node.

## D2. Language and ABI: C99, VSI C, 32-bit pointers, SSL3 *_SHR32

**Status:** proposed, following vms-beszel D2 (approved there).

The same combination as vms-beszel, vms-steward and vmsguard. VSI Python would add an
interpreter dependency on every node and still need C for the system services.

## D3. Read-only first

**Status:** chosen by the user (2026-10-24).

The first release only publishes; the agent subscribes to nothing but
`homeassistant/status`. Commands from HA come later, as an allow-list behind their own
security design (PLAN.md, "Later"). Never free-form DCL.

## D4. Reuse vms-beszel's samplers and network layer by copying

**Status:** proposed.

Copy `bz_plat_vms.c` (samplers), `bz_net_ossl.c` (sockets and TLS) and the buffer
helpers from vms-beszel at a recorded commit (47dbcf8 at the time of writing), renamed
to `ha_`. Their behaviour on both architectures is already tested, including the
`$GETRMI` pitfalls in beszel's NOTES.md.

Alternative: a shared library repository used by both. Cleaner in the long run, but it
adds a build dependency and a release step to beszel for a project that does not exist
yet. Revisit if a third project needs the same code.

## D5. Node ID: node name and system ID, saved on first run

**Status:** proposed (Phase 1).

The node ID names the HA device (`dev.ids`), the topics and every `unique_id`, so it
must never change (CLAUDE.md). On first run it is made from the SCS node name and
SCSSYSTEMID, slugged (`X86VMS` and 1025 give `x86vms_1025`), and saved to
`node_id` in DATA_DIR; later runs read the file. `NODE_ID` overrides it.

Why both: the node name alone can repeat across clusters on one broker, and the
system ID alone tells a person nothing in the MQTT topics. Saving it means a later
change of node name or system ID doesn't orphan the device and its history.

Alternative: a hardware ID. On a virtual machine it is no more stable, and it is not
readable in topics.

## D6. Retained state, and the sensors' shape

**Status:** proposed (Phase 1).

- **The state message is retained** (QoS 0). HA gets the current values the moment
  it creates the entities, and again after its own restart, with no wait between
  discovery and state. The probe needed a 2-second sleep instead. Availability
  still decides whether the values are shown.
- **One device-discovery message** with one component per sensor. Disks and NICs
  come and go, so the set is recomputed every sample. When it changes, discovery is
  republished, with each removed key as `{"p": "sensor"}`.
- **Units:** memory in MiB, disks in GiB, network in kB/s, each with a
  `suggested_display_precision`. HA can convert `data_size` and `data_rate` to
  another unit for display.
- **Boot time, not uptime:** a `timestamp` sensor changes once per boot, while an
  uptime counter would change, and be recorded, every interval.
- **Disabled by default:** the minor CPU modes (interrupt, MP sync, executive,
  supervisor, idle) and the modified page list exist, but are off until the user
  enables them. That keeps the device page and the recorder lean.
- **`expire_after`** = 3 × INTERVAL + 10 s on every measured sensor. A hung agent
  whose connection stays up still goes unavailable.

## D7. The broker password only from a file the owner alone can read

**Status:** proposed (Phase 1).

`MQTT_PASSWORD_FILE` names a file holding one line. The agent refuses to start if
group or world can read it, never logs it, and clears it from memory on exit. There
is no inline password setting: configuration files get copied, mailed and committed.

## D8. Phase 3: what is published, and when a problem sensor is on

**Status:** agreed with the user 2026-10-25 (shadow sets tested on Linux only; a
temporary test queue on each node is fine).

Each problem sensor is an MQTT `binary_sensor` with `device_class: problem`, so it
shows as "Problem"/"OK" and an automation can trigger on it. Each state sensor is an
`enum` sensor with a fixed list of options.

- **Disks** (every disk already reported): `errors` (the device error count;
  `total_increasing`, so a reboot's reset is not a fall) and `problem`. Problem is on
  while mount verification is in progress, the device is not online, or the error
  count rose in the last ERROR_HOLD minutes (default 60). A shadow set's virtual unit
  reports the sum of its members' counts: the members themselves are not reported.
- **Shadow sets** (each mounted DSA): `state` (steady, copying, merging, degraded),
  `members` (full members), `progress` (% of the copy or merge, empty when steady) and
  `problem` (on unless steady). **Degraded** means a failed member, or fewer full
  members than the set had at any time since the agent started. Restarting the agent
  accepts the current membership. A count from a configuration file would be more
  exact, but would be wrong as soon as someone changed the set and forgot the file.
- **Queues:** every execution queue on this node, and every generic queue, unless
  QUEUES lists names. `state` (stopped, stopping, stalled, paused, starting,
  unavailable, busy, idle, available), `pending`, `executing` and `problem` (on when
  stopped, stopping, stalled, paused or unavailable). In a cluster each member
  reports the generic queues too.
- **Cluster** (only when the node is a cluster member): `cluster_nodes`, and
  `cluster_votes` and `cluster_quorum` as diagnostics. Each node reports its own view
  on its own device. A separate "cluster" device would be written by every member,
  and the last writer would win.
- **Error log: dropped.** Reading ERRLOG.SYS needs a privilege and a parser for its
  binary records. The disk error counts cover what an alert needs.

Limits: 32 queues, 16 shadow sets, 400 components per node.

## D9. Run as its own account, started like vms-beszel's agent

**Status:** agreed with the user 2026-10-25 (the account is HASS).

vms-beszel's D12 and D14 apply unchanged, and its procedures are copied (as D4 copied
the samplers):
- **The agent refuses to run** if its process is authorized for any privilege beyond
  NETMBX and TMPMBX, or has a system-group UIC (at or below `MAXSYSGROUP`). It exits
  with SS$_ABORT, which `HASS$RUN.COM` does not retry. `--print` and `--remove` are
  one-off commands and skip the check.
- **The account** (made by `HASS$CONFIGURE`): a UIC group of its own above
  `MAXSYSGROUP`; NETMBX and TMPMBX; every login class disallowed; DISCTLY, LOCKPWD,
  `LGICMD NL:`, a random password nobody needs; owner field "vms-hass agent", so the
  procedure only ever changes an account it made. It owns `[CONF]`, `[DATA]` and
  `[LOG]`, which nobody else can read.
- **Starting:** `HASS$CONTROL START` runs `HASS$RUN.COM` as a detached LOGINOUT
  process, through `HASS_LAUNCH.EXE` (persona) when the caller isn't the account.
  `HASS$RUN.COM` restarts the agent with a backoff.
- **Stopping:** `HASS$CONTROL STOP` creates `HASS$ROOT:[DATA]STOP.`; the agent
  publishes `offline`, disconnects and exits within about a second (longer only while
  a connect is timing out, at most 15 s), and the procedure stops the process if it
  hasn't gone in 20 s.
- **Boot:** the lines in `SYSTARTUP_VMS.COM` and `SYSHUTDWN.COM` are the user's to
  add, as for beszel.

## D10. Plain MQTT on the LAN for now, with a warning

**Status:** agreed with the user 2026-10-25.

The user's broker has no TLS listener yet (8883 refused, 2026-10-25). The agent
supports TLS (tested on Linux against Mosquitto, PHASE1.md) and verifies the
certificate and name. Without TLS, it logs a WARN at every start unless the broker is
this machine, as vms-beszel D13 does for plain http. TLS on the nodes is tested once
the user gives the Mosquitto app a certificate.

Alternative: refuse plain MQTT except to loopback. Rejected for now: it would need the
certificate before anything could run.

## D11. OpenVMS VAX: 64-bit values as double, plain MQTT, its own tooling

**Status:** chosen by the user (2026-11-10): double on VAX, a HASS account on the
VAX as on the other nodes (with batch logins: see below), and a hand run into HA
first (kit and boot later).

Compaq C 6.4 for VAX has no 64-bit integer type, no `<stdint.h>`, no `snprintf`
and no C99 mode, and the VAX has no OpenSSL (NOTES.md 15). So:

- **The core names `ha_u64`/`ha_i64` (`src/core/ha_port.h`), never `uint64_t`.**
  They are the C99 types everywhere except under `HA_WIDE_DOUBLE` (VAX), where they
  are `double`. VAX D_float has a 56-bit fraction, so millisecond clocks (about
  5e12), byte and tick counts stay exact. Only +, -, * and comparisons are used on
  them: the compiler rejects %, shifts and masks on a double, and the few integer
  divisions (the ISO 8601 date, log seconds) are written for both.
  `make test` runs the unit tests a third time with `HA_WIDE_DOUBLE` and
  `HA_NO_SNPRINTF` on Linux, and fails if a core file names a 64-bit type.
- **`ha_snprintf`** (`ha_fmt.c`, in the core on every platform, tested against
  fixed strings) stands in for `snprintf`/`vsnprintf` under `HA_NO_SNPRINTF`.
- **Quadwords in the VMS platform layer are `struct quad`** (two longwords), turned
  into `ha_u64` in one place, so the same file builds on all four architectures.
- **Plain MQTT only on VAX** (`HA_NO_TLS`): D10's warning applies, and MQTT_TLS=1 is
  refused with a clear error. `--random` fails there rather than give a guessable
  password; the VAX account's password is made on the workstation instead.
- **Tooling:** the VAX has no SSH, so `tools/vax.sh` (on vms-site-config's
  `vax.py`, Telnet/FTP) replaces vms.sh and push.sh for it, as in vms-zlib and
  vms-bzip2.
- **Items OpenVMS V7.3 VAX lacks** are left out under `#ifdef`, not guessed: the
  shadow-set member counts are counted on the walk, and copy progress is unknown
  (NOTES.md 16). The LAN counter codes come from `$NMADEF` in the node's `LIB.MLB`,
  as there is no `SYS$LIB_C.TLB`.

- **On VAX the agent runs as a batch job of its account** (chosen by the user
  2026-11-10). V7.3 VAX's `$CREPRC` doesn't create a process under an assumed
  persona and refuses `PRC$M_INHERIT_PERSONA` (NOTES.md 16), so `HASS_LAUNCH`
  can't work there. A batch job logs in as the account properly (its UIC, quotas
  and authorized privileges), so the VAX account alone gets the BATCH login class,
  and `HASS$CONTROL START` uses `SUBMIT/USER` (which needs CMKRNL). The password
  stays random, unknown and locked, and the privileges NETMBX and TMPMBX only.
  D9 is otherwise unchanged; other architectures keep every login class off.
- **The VAX kit** is base system `VMS`, without the SSL3 dependency; it is 0.1.2,
  as the 0.1.1 kits don't have the VAX changes. `HASS$CONFIGURE` won't create a
  new account on VAX (no generator for its password): it changes an existing one
  it made, as on this node.

Alternatives: 32-bit values with wrap-safe arithmetic on VAX (more changes to the
core's behaviour, harder to test against the other platforms); a two-longword
struct with helper functions everywhere (exact, but touches every arithmetic site on
all platforms and makes the core harder to read).
