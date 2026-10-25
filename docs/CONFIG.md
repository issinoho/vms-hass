# Configuration

One file of `NAME=value` lines (`#` comments; values may be in double quotes). An
unknown name, a name set twice or a malformed line stops the agent with the line
number. Each setting can be overridden by the logical name `HASS$<NAME>` on OpenVMS,
or the environment variable `HASS_AGENT_<NAME>` on Linux.

| Setting | Default | Meaning |
| --- | --- | --- |
| `MQTT_HOST` | (required) | The broker's host name or address. For the Mosquitto app, this is the HA machine's LAN address, not HA's web URL. |
| `MQTT_PORT` | 1883, or 8883 with TLS | |
| `MQTT_USER` | none (anonymous) | The broker user. |
| `MQTT_PASSWORD_FILE` | (required with a user) | A file with the password on one line, readable by its owner only (D7). |
| `MQTT_TLS` | no | `yes` for TLS. The broker's certificate and host name are always verified. |
| `MQTT_CA_FILE` | the system's CA store | The CA that issued the broker's certificate, for a private CA. |
| `MQTT_KEEPALIVE` | 60 | Seconds, 10 to 3600. The broker declares the node offline after 1.5 × this with no traffic. |
| `INTERVAL` | 30 | Seconds between samples, 5 to 3600. |
| `DISCOVERY_PREFIX` | homeassistant | Must match HA's MQTT integration setting. |
| `TOPIC_PREFIX` | openvms | The first level of the state and availability topics. |
| `NODE_ID` | made on first run (D5) | Lower-case letters, digits and single `_`. Never change it once published. |
| `DEVICE_NAME` | the node name | The device's name in HA. Users can rename it in HA too. |
| `QUEUES` | `*` | Which queues to report: `*` for every execution queue on this node and every generic queue, `NONE` for none, or a comma-separated list of names. |
| `ERROR_HOLD` | 60 | Minutes a disk's problem sensor stays on after its error count rises, 1 to 10080 (D8). |
| `LOG_LEVEL` | info | debug, info, warn or error. |
| `DATA_DIR` | the platform's | Where `node_id` is kept. |

## Topics

| Topic | Retained | Content |
| --- | --- | --- |
| `<DISCOVERY_PREFIX>/device/<node_id>/config` | yes | the device-discovery document |
| `<TOPIC_PREFIX>/<node_id>/state` | yes | one JSON object, a field per sensor |
| `<TOPIC_PREFIX>/<node_id>/availability` | yes | `online`, or `offline` (the last will, or an orderly stop) |

The agent subscribes to `<DISCOVERY_PREFIX>/status` and publishes everything again
when HA announces `online` there.

## Commands

    hass_agent [config-file]            run
    hass_agent --remove [config-file]   remove this node's device from HA, then exit
    hass_agent --print [config-file]    print the discovery and state documents, offline
    hass_agent --version
