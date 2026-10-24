# MQTT and Home Assistant discovery: what vms-hass uses

Every fact here cites its source. "MQTT x.y" is a section of *MQTT Version 3.1.1*
(OASIS Standard, 29 October 2014). "HA MQTT" is
<https://www.home-assistant.io/integrations/mqtt/>, as it applies to HA 2026.10.

## MQTT 3.1.1 subset

| Packet | Use | Source |
| --- | --- | --- |
| Fixed header | type in the high nibble; flags DUP, QoS (2 bits), RETAIN in the low nibble; remaining length in 1-4 bytes of 7 bits, high bit = more | MQTT 2.2 |
| CONNECT (0x10) | protocol name `MQTT`, level 4; flags: user name 0x80, password 0x40, will retain 0x20, will QoS 0x18, will 0x04, clean session 0x02; keepalive (s); payload in order: client ID, will topic, will message, user name, password | MQTT 3.1 |
| CONNACK (0x20) | 2 bytes: session present, return code (0 accepted, 4 bad user name or password, 5 not authorised) | MQTT 3.2 |
| PUBLISH (0x30) | topic (UTF-8 string), packet ID only when QoS > 0, payload as the rest of the packet; an empty retained payload deletes the retained message | MQTT 3.3 |
| PUBACK (0x40) | packet ID; the answer to a QoS 1 PUBLISH | MQTT 3.4 |
| SUBSCRIBE / SUBACK | `homeassistant/status` only (read-only agent, D3) | MQTT 3.8, 3.9 |
| PINGREQ / PINGRESP | keepalive; the broker drops a client silent for 1.5 x keepalive and publishes its will | MQTT 3.12, 3.13, 3.1.2.10 |
| DISCONNECT (0xE0) | orderly close; the broker discards the will | MQTT 3.14 |

Strings are a 2-byte big-endian length then UTF-8 bytes (MQTT 1.5.3).

The Mosquitto 2 broker in the app answers a wrong password with return code 5, not 4
(NOTES.md 1).

## HA discovery

- **Topic:** `<discovery_prefix>/device/<object_id>/config`. The prefix is the
  integration's `discovery_prefix` (`homeassistant` on the user's HA, NOTES.md 1).
  (HA MQTT, "Discovery topic".)
- **Device discovery payload** (one message for all of a device's entities):
  `dev` (device: `ids`, `name`, `mf`, `mdl`, `sw`, ...), `o` (origin: `name`, `sw`,
  `url`; required for device discovery), `cmps` (components, keyed by an ID of our
  choosing; each has `p`, the platform, plus that platform's options, and a
  `unique_id`). A few options shared by every component may go at the top level:
  `state_topic`, `availability_topic`, `availability` and `qos` among them. Others
  may not: HA 2026.10.3 refused the whole message for a top-level `expire_after`
  (NOTES.md 3), so per-sensor options stay inside each component. (HA MQTT, "Device
  discovery payload".) Abbreviations are listed under "Using abbreviations in
  discovery".
- **Removing:** an empty retained payload on the config topic removes the device and
  its entities. Removing one component means republishing the config with that
  component reduced to `{"p": "<platform>"}`. (HA MQTT, "Discovery messages", device
  discovery removal.)
- **Retain the config** so that HA finds it after its own restart, and also republish
  when HA sends `online` on `homeassistant/status` (the birth message, not retained).
  (HA MQTT, "Birth and last will messages" and "Discovery messages".)
- **Availability:** `availability_topic` with the default payloads `online` and
  `offline`; our will publishes a retained `offline`. (HA MQTT, "Availability".)
- **Sensors:** `device_class`, `unit_of_measurement` and `state_class` (`measurement`,
  `total`, `total_increasing`) decide the display and whether long-term statistics are
  kept; `value_template` picks a field out of the JSON state. `entity_category:
  diagnostic` for identity fields. (<https://www.home-assistant.io/integrations/sensor.mqtt/>,
  <https://developers.home-assistant.io/docs/core/entity/sensor/>.)

Seen working on HA 2026.10.3 with `probes/mqtt_probe.c` (NOTES.md 2) and with the
Phase 1 agent (NOTES.md 3).
