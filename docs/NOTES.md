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
