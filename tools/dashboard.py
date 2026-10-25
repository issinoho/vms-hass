#!/usr/bin/env python3
"""dashboard.py - build an HA dashboard config for vms-hass nodes.

    tools/dashboard.py <entity-prefix>:<disk,disk>:<nic,nic>[:<queue,queue>] ... > dashboard.json

<entity-prefix> is the node's entity ID stem as HA made it from the device name
(device "X86VMS" -> "x86vms"); disks and NICs are slugged device names as in the
entity IDs ("dka0", "ewb0"), and so are queues ("SYS$BATCH" -> "sys_batch").
Example:

    tools/dashboard.py x86vms:dka0,dka200,dka300,ldm2585:eia0:sys_batch,x86vms_batch \
                       myi64:dka800:ewb0:sys_batch,canon

The output is a sections-view config: an overview of every node, then one view
per node with all of its sensors.  It holds no addresses or credentials.
"""
import json
import sys

MODES = ["user", "kernel", "executive", "supervisor", "interrupt", "mp_synchronisation", "idle"]
MODE_COLOURS = {"user": "#4caf50", "kernel": "#f44336", "executive": "#ff9800",
                "supervisor": "#9c27b0", "interrupt": "#2196f3",
                "mp_synchronisation": "#795548", "idle": "#9e9e9e"}
SEVERITY = {"green": 0, "yellow": 75, "red": 90}


def summary(e):
    """the node's identity and status, from the device registry and its sensors"""
    return {
        "type": "markdown",
        "content": (
            "{% set e = '" + e + "' %}{% set d = device_id(e ~ '_cpu') %}"
            "{% set b = states(e ~ '_booted') %}"
            "| | |\n|---|---|\n"
            "| Status | {{ '🔴 Offline' if states(e ~ '_cpu') in ['unavailable','unknown'] else '🟢 Online' }} |\n"
            "| Model | {{ device_attr(d, 'model') }} |\n"
            "| OpenVMS | {{ device_attr(d, 'sw_version') }} |\n"
            "| Architecture | {{ device_attr(d, 'hw_version') }} |\n"
            "| System ID | {{ device_attr(d, 'serial_number') }} |\n"
            "| Booted | {% if b not in ['unavailable','unknown'] %}{{ as_timestamp(b) | timestamp_custom('%a %d %b %Y %H:%M') }}{% else %}-{% endif %} |\n"
            "| Up | {% if b not in ['unavailable','unknown'] %}{{ time_since(as_datetime(b), 2) }}{% else %}-{% endif %} |\n"
            "| CPUs | {{ states(e ~ '_active_cpus') }} active |\n"
            "| Memory | {{ (states(e ~ '_memory_size') | float(0) / 1024) | round(1) }} GiB |"
        ),
    }


def gauge(entity, name, cols=4):
    return {"type": "gauge", "entity": entity, "name": name, "min": 0, "max": 100,
            "needle": True, "severity": SEVERITY, "grid_options": {"columns": cols, "rows": 2}}


def tile(entity, name=None, cols=6, trend=False):
    t = {"type": "tile", "entity": entity, "grid_options": {"columns": cols}}
    if name:
        t["name"] = name
    if trend:
        t["features"] = [{"type": "trend-graph", "hours_to_show": 24}]
        t["grid_options"]["rows"] = 2
    return t


def graph(entities, hours=24, rows=4):
    return {"type": "history-graph", "hours_to_show": hours, "entities": entities,
            "grid_options": {"columns": "full", "rows": rows}}


def heading(text, icon, sub=False, badges=None):
    h = {"type": "heading", "heading": text, "icon": icon,
         "heading_style": "subtitle" if sub else "title"}
    if badges:
        h["badges"] = badges
    return h


def problems(e, disks, queues):
    """every problem sensor of the node, shown only while one is on"""
    b = "binary_sensor." + e
    ents = [b + "_" + d + "_problem" for d in disks] + [b + "_" + q + "_problem" for q in queues]
    return {"type": "entity-filter", "entities": ents,
            "conditions": [{"condition": "state", "state": "on"}],
            "card": {"type": "entities", "title": "Problems", "state_color": True},
            "show_empty": False, "grid_options": {"columns": "full"}}


def overview_section(e, name, disks, queues, path):
    s = "sensor." + e
    return {"type": "grid", "cards": [
        heading(name, "mdi:server", badges=[
            {"type": "entity", "entity": s + "_cpu", "show_state": True, "color": "accent"},
            {"type": "entity", "entity": s + "_active_cpus", "show_state": True}],
        ) | {"tap_action": {"action": "navigate", "navigation_path": path}},
        summary(s),
        problems(e, disks, queues),
        gauge(s + "_cpu", "CPU"),
        gauge(s + "_memory_used", "Memory"),
        gauge(s + "_" + disks[0] + "_used", disks[0].upper()),
        tile(s + "_processes", "Processes", 4),
        tile(s + "_interactive_users", "Users", 4),
        tile(s + "_compute_queue", "Compute queue", 4),
        graph([{"entity": s + "_cpu", "name": "CPU"},
               {"entity": s + "_memory_used", "name": "Memory"}], rows=3),
        {"type": "shortcut", "label": "All " + name + " details", "icon": "mdi:arrow-right",
         "tap_action": {"action": "navigate", "navigation_path": path}},
    ]}


def node_view(e, name, disks, nics, queues):
    s = "sensor." + e
    b = "binary_sensor." + e
    cpu = [heading("CPU", "mdi:cpu-64-bit"),
           gauge(s + "_cpu", "Busy", 6),
           tile(s + "_compute_queue", "Compute queue", 6, trend=True)]
    cpu += [tile(s + "_cpu_" + m, None, 4) for m in MODES]
    cpu.append(graph([{"entity": s + "_cpu_" + m, "color": MODE_COLOURS[m]} for m in MODES if m != "idle"]))
    mem = [heading("Memory", "mdi:memory"),
           gauge(s + "_memory_used", "In use", 4),
           gauge(s + "_page_file_used", "Page files", 4),
           gauge(s + "_swap_file_used", "Swap files", 4),
           tile(s + "_memory_free", "Free", 4),
           tile(s + "_modified_page_list", "Modified list", 4),
           tile(s + "_memory_size", "Total", 4),
           graph([{"entity": s + "_memory_used", "name": "In use"},
                  {"entity": s + "_page_file_used", "name": "Page files"}], rows=3)]
    dsk = [heading("Disks", "mdi:harddisk")]
    for d in disks:
        dsk += [heading(d.upper(), "mdi:harddisk", sub=True),
                gauge(s + "_" + d + "_used", "Used", 4),
                tile(s + "_" + d + "_free", "Free", 4),
                tile(s + "_" + d + "_size", "Size", 4),
                tile(b + "_" + d + "_problem", "Problem", 6),
                tile(s + "_" + d + "_errors", "Errors", 6)]
    dsk.append(graph([{"entity": s + "_" + d + "_used", "name": d.upper()} for d in disks], hours=168))
    net = [heading("Network", "mdi:lan")]
    for n in nics:
        net += [tile(s + "_" + n + "_receive", n.upper() + " receive", 6, trend=True),
                tile(s + "_" + n + "_transmit", n.upper() + " transmit", 6, trend=True)]
    net.append(graph([x for n in nics for x in
                      ({"entity": s + "_" + n + "_receive"}, {"entity": s + "_" + n + "_transmit"})], rows=3))
    que = [heading("Queues", "mdi:tray-full")]
    for q in queues:
        que += [tile(s + "_" + q, None, 6),
                tile(b + "_" + q + "_problem", "Problem", 6),
                tile(s + "_" + q + "_pending", "Pending", 6),
                tile(s + "_" + q + "_executing", "Executing", 6)]
    system = [heading("System", "mdi:server"), summary(s),
              tile(s + "_processes", "Processes", 6, trend=True),
              tile(s + "_interactive_users", "Interactive users", 6, trend=True),
              tile(s + "_booted", "Booted", 6),
              tile(s + "_active_cpus", "Active CPUs", 6)]
    return {"title": name, "path": e, "type": "sections", "icon": "mdi:server", "max_columns": 3,
            "sections": [{"type": "grid", "cards": c} for c in (system, cpu, mem, dsk, net, que) if len(c) > 1]}


def main(argv):
    if not argv:
        sys.exit(__doc__)
    nodes = []
    for a in argv:
        e, disks, nics, queues = (a.split(":") + [""])[:4]
        nodes.append((e, e.upper(), [d for d in disks.split(",") if d], [n for n in nics.split(",") if n],
                      [q for q in queues.split(",") if q]))
    overview = {"title": "Overview", "path": "overview", "type": "sections", "icon": "mdi:view-dashboard",
                "max_columns": max(2, len(nodes)),
                "sections": [overview_section(e, n, d, q, "/openvms-servers/" + e) for e, n, d, _, q in nodes]}
    overview["sections"].append({"type": "grid", "column_span": max(2, len(nodes)), "cards": [
        heading("All nodes", "mdi:chart-line"),
        graph([{"entity": "sensor." + e + "_cpu", "name": n + " CPU"} for e, n, _, _, _ in nodes], rows=3),
        graph([{"entity": "sensor." + e + "_memory_used", "name": n + " memory"} for e, n, _, _, _ in nodes], rows=3),
    ]})
    config = {"views": [overview] + [node_view(e, n, d, ni, q) for e, n, d, ni, q in nodes]}
    json.dump(config, sys.stdout, ensure_ascii=False)


if __name__ == "__main__":
    main(sys.argv[1:])
