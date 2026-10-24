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
