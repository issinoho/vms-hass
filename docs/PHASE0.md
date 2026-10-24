# Phase 0: spec, probes, broker

**Status:** exit checks met 2026-10-24; awaiting the user's exit review.

| Step | Result |
| --- | --- |
| Names (D0) | approved with the plan |
| Broker | Mosquitto app 7.1.2 installed, MQTT integration added, agent login created (NOTES.md 1) |
| TCP to the broker from both nodes | yes, port 1883 |
| SSL3 on both nodes | yes, SSL3 V3.0-21 (x86) and V3.0-22 (IA64), 32-bit images present |
| Hand-made MQTT session and test sensor | from Linux, x86 and IA64: device and both sensors appeared in HA with correct values (NOTES.md 2) |
| Test devices removed | yes, empty retained configs; nothing left in HA's registry |
| docs/MQTT.md | written |

Carried into Phase 1: test the last will (an unclean drop must turn the device
`unavailable`) and TLS on 8883.

The password file MQTT.PWD (owner access only) is left in each node's work directory
for the Phase 2 runs.
