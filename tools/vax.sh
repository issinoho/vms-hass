#!/usr/bin/env bash
# vax.sh - vms.sh and push.sh for the OpenVMS VAX, which has Telnet and FTP but no
# SSH (NOTES.md 15).  Drives vms-site-config's tools/vax.py ($VAX_PY), with the host,
# user and work directory from the "vax" row of tools/nodes.conf; the password is
# vax.py's ($VAX_PWD_FILE, default ~/.config/vms/vax.pwd).
#
#   vax.sh push                        upload the sources and build_vms.com
#   vax.sh dcl '<cmd>' ...             run DCL lines in the work directory
#   vax.sh run <local.com> [params]    run a procedure in the work directory
#   vax.sh put <local> [remote-rel]    upload one file (relative to the work directory)
#   vax.sh get <remote-rel> [local]    download one file
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
vaxpy=${VAX_PY:-$root/../vms-site-config/tools/vax.py}
read -r _ _ host _ user workdir _ < <(awk '$1=="vax"' "$root/tools/nodes.conf") || true
[ -n "${host:-}" ] || { echo "vax.sh: no \"vax\" row in tools/nodes.conf" >&2; exit 2; }
export VAX_HOST=$host VAX_USER=$user
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export TMPDIR=$tmp

# DUA1:[WORK.VMS_HASS] + src/core/x.c -> DUA1:[WORK.VMS_HASS.SRC.CORE]X.C
remote() {
    local rel=$1 dir file
    dir=$(dirname "$rel") file=$(basename "$rel")
    if [ "$dir" = . ]; then echo "$workdir$file"; return; fi
    echo "${workdir%]}.$(echo "$dir" | tr '/' '.' | tr '[:lower:]' '[:upper:]')]$file"
}

op=${1:?usage: vax.sh push|dcl|run|put|get}; shift
case $op in
push)
    dirs=(src src/core src/plat src/plat/vms src/main tests tests/unit)
    mk=("create/directory $workdir")
    for d in "${dirs[@]}"; do mk+=("create/directory $(remote "$d/x")" ); done
    python3 "$vaxpy" dcl "${mk[@]/%x/}" | grep -v '^$' || true
    for f in build_vms.com src/core/*.[ch] src/plat/*.[ch] src/plat/vms/*.c src/main/*.c tests/unit/test_core.c; do
        python3 "$vaxpy" put "$root/$f" "$(remote "$f")"
    done
    echo "pushed to $workdir" ;;
dcl)
    python3 "$vaxpy" dcl "set default $workdir" "$@" ;;
run)
    com=$1; shift
    { echo "\$ set default $workdir"; cat "$com"; } > "$tmp/$(basename "$com")"
    python3 "$vaxpy" run "$tmp/$(basename "$com")" "$@" ;;
put)
    python3 "$vaxpy" put "$1" "$(remote "${2:-$(basename "$1")}")" ;;
get)
    python3 "$vaxpy" get "$(remote "$1")" "${2:-$(basename "$1")}" ;;
*)
    echo "vax.sh: unknown operation $op" >&2; exit 2 ;;
esac
