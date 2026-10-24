#!/usr/bin/env bash
# mosquitto.sh - unpack Ubuntu's Mosquitto packages into build/mosq/root for the
# integration tests, without installing anything (no root needed).
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
d=$top/build/mosq
mkdir -p "$d/debs" "$d/root"
cd "$d/debs"
apt-get download mosquitto mosquitto-clients libmosquitto1 libdlt3 libwebsockets19t64
for f in *.deb; do dpkg-deb -x "$f" "$d/root"; done
LD_LIBRARY_PATH=$d/root/usr/lib/x86_64-linux-gnu "$d/root/usr/sbin/mosquitto" -h | head -1
