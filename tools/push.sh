#!/usr/bin/env bash
# push.sh - copy the sources and build procedure to a node's work directory.
#   push.sh <node>
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
v=$root/tools/vms.sh
n=${1:?node}
read -r _ _ host port user _ sftpdir < <(awk -v n="$n" '$1==n' "$root/tools/nodes.conf")
# parent directories first: putdir makes only the last level
printf -- '-mkdir %s\n' "$sftpdir/src" "$sftpdir/src/plat" "$sftpdir/tests" |
    sftp -P "$port" -i "${VMS_SSH_KEY:-$HOME/.ssh/vms_ed25519}" -o BatchMode=yes -b - "$user@$host" >/dev/null 2>&1 || true
"$v" "$n" put "$root/build_vms.com"
for d in src/core src/plat src/plat/vms src/main tests/unit; do
    "$v" "$n" putdir "$root/$d" "$d" 2>&1 | grep -v 'Couldn.t \(create\|stat\)\|is not a regular file' || true
done
