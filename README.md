# vms-hass

OpenVMS systems in [Home Assistant](https://www.home-assistant.io). A native agent on
each node publishes its instrumentation (CPU, memory, disks, network, and later
clusters, queues and shadow sets) over MQTT. HA's MQTT discovery turns each node into a
device whose sensors can go on dashboards, keep history and drive automations.

**Status:** planning (Phase 0). See [`docs/PLAN.md`](docs/PLAN.md) and
[`docs/DECISIONS.md`](docs/DECISIONS.md).

Part of the [OpenVMS ports and tools family](https://openvms.issinoho.com).

## License

MIT
