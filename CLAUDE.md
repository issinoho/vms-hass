# CLAUDE.md

A native OpenVMS agent that publishes system instrumentation to Home Assistant over
MQTT, using HA's MQTT discovery, so each node appears in HA as a device with sensors
for dashboards, history and automations. Read `docs/PLAN.md` and `docs/DECISIONS.md`
before starting work. Work phase by phase; stop and ask at each phase exit.

## Ground rules (from the sibling vms-* repos)

- **Use `tools/vms.sh`** for remote work; never raw `ssh host cmd`, never `WAIT` over ssh.
  Run long jobs with `run_in_background`, and probes that might hang as batch jobs.
- **Committed files must not contain real node, broker or HA details** (they live in
  the git-ignored `tools/nodes.conf` and `tools/ha.conf`), nor credentials, tokens or
  keys. Use `<x86-host>`, `<ia64-host>`, `<ha-host>`, `<broker-host>`.
- **Nothing destructive or system-wide on the nodes** without asking: no DELETE/PURGE
  outside our work directories, no PRODUCT INSTALL, no new accounts, no system logical
  names, no startup changes.
- **The user's Home Assistant is theirs:** installing add-ons, creating MQTT users,
  dashboards or automations, and deleting discovered devices all need asking first.
  Test entities we create must be removed when the test ends (empty retained config).
- Every decision goes in `docs/DECISIONS.md`; every platform surprise in `docs/NOTES.md`
  with how it was observed.
- Prefer a 20-line probe in `probes/` over speculation. Do not claim something works
  until a test on the node (or in HA) shows it.

## Hard constraints

- **C99, VSI C, 32-bit pointers** (D2). The Linux build uses `-std=c99 -pedantic -Wall
  -Wextra`; do not weaken it.
- **`src/core/` does no I/O** and has no platform dependencies; the platform header is
  the entire OS surface and contains no VMS types.
- **Wire formats use explicit encoding**, never struct layout. Every MQTT and discovery
  fact in code or docs cites its source (`docs/MQTT.md`).
- **Never log the broker password.**
- **Never change a node's `node_id` or a sensor's `unique_id`** once published: HA
  would orphan the device or entity and lose its history.

## OpenVMS

Read vms-beszel's `docs/NOTES.md` before touching system-service code (the samplers
come from there, D4): `$GETRMI` wants exact item sizes and `RMI$_MODES` return lengths
are wrong. Undefined symbols are *warnings* on VMS: check `$SEVERITY` after every LINK.
Build from CLEAN after changing headers or qualifiers. Take item codes from the headers
on the node, never from memory. The sibling CLAUDE.md files (vms-beszel, vms-steward,
vms-lighttpd) list more pitfalls, and they apply here.

## Commits

Commit in logical steps with messages that explain why, and record what was verified
and how. Don't push without the user asking; the remote will be `origin`
(github.com/issinoho/vms-hass).
