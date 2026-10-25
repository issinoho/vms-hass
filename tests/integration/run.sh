#!/usr/bin/env bash
# run.sh - the agent (stub platform, sanitizer build) against a real Mosquitto.
#
# Mosquitto comes from Ubuntu's packages unpacked into build/mosq/root
# (tools/mosquitto.sh), so nothing is installed.  The broker listens on
# 127.0.0.1 only: 18830 plain, 18831 TLS with a throwaway CA made here.
set -uo pipefail

top=$(cd "$(dirname "$0")/../.." && pwd)
mroot=$top/build/mosq/root
export LD_LIBRARY_PATH=$mroot/usr/lib/x86_64-linux-gnu
MOSQ=$mroot/usr/sbin/mosquitto
SUB=$mroot/usr/bin/mosquitto_sub
PUB=$mroot/usr/bin/mosquitto_pub
AGENT=$top/build/hass_agent-asan
[ -x "$MOSQ" ] || { echo "no Mosquitto: run tools/mosquitto.sh first" >&2; exit 2; }
[ -x "$AGENT" ] || { echo "build $AGENT first (make integration)" >&2; exit 2; }

w=$top/build/it
rm -rf "$w"; mkdir -p "$w/data" "$w/data2"
cd "$w"
pass=0 fail=0
ok()  { pass=$((pass + 1)); echo "  ok   $*"; }
bad() { fail=$((fail + 1)); echo "  FAIL $*"; }
check() { local what=$1; shift; if "$@"; then ok "$what"; else bad "$what"; fi; }

# ---- broker, users, certificates ----
umask 077
echo 's3cret-agent' > agent.pwd
echo 'wrong' > wrong.pwd
"$mroot/usr/bin/mosquitto_passwd" -b -c passwd agent s3cret-agent 2>/dev/null
"$mroot/usr/bin/mosquitto_passwd" -b passwd tester t3ster 2>/dev/null
openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj /CN=test-ca \
    -keyout ca.key -out ca.pem 2>/dev/null
openssl req -newkey rsa:2048 -nodes -subj /CN=localhost -keyout srv.key -out srv.csr 2>/dev/null
printf 'subjectAltName=DNS:localhost\n' > san.ext
openssl x509 -req -in srv.csr -CA ca.pem -CAkey ca.key -CAcreateserial -days 2 \
    -extfile san.ext -out srv.pem 2>/dev/null
cat > mosquitto.conf <<EOF
per_listener_settings false
allow_anonymous false
password_file $w/passwd
persistence false
log_dest file $w/broker.log
log_type all
listener 18830 127.0.0.1
listener 18831 127.0.0.1
cafile $w/ca.pem
certfile $w/srv.pem
keyfile $w/srv.key
EOF
umask 022

broker_pid=
start_broker() { "$MOSQ" -c mosquitto.conf & broker_pid=$!; sleep 0.5; }
stop_broker()  { [ -n "$broker_pid" ] && kill "$broker_pid" 2>/dev/null; wait "$broker_pid" 2>/dev/null; broker_pid=; }
S() { "$SUB" -h 127.0.0.1 -p 18830 -u tester -P t3ster "$@" 2>/dev/null; }
P() { "$PUB" -h 127.0.0.1 -p 18830 -u tester -P t3ster "$@" 2>/dev/null; }

agent_pid=
start_agent() {  # start_agent <log> <config>
    HA_STUB_CONTROL=$w/control "$AGENT" "$2" > "$1" 2>&1 & agent_pid=$!
}
stop_agent() { [ -n "$agent_pid" ] && kill "$agent_pid" 2>/dev/null; wait "$agent_pid" 2>/dev/null; agent_pid=; }
trap 'stop_agent; stop_broker' EXIT

conf() {  # conf <file> [NAME=value ...]: the defaults, each override replacing one
    local f=$1; shift
    printf '%s\n' "MQTT_HOST=127.0.0.1" "MQTT_PORT=18830" "MQTT_USER=agent" \
        "MQTT_PASSWORD_FILE=$w/agent.pwd" "MQTT_KEEPALIVE=10" "INTERVAL=5" \
        "DATA_DIR=$w/data" "$@" |
        awk -F= '{ v[$1] = $0; if (!($1 in seen)) { seen[$1] = 1; order[n++] = $1 } }
                 END { for (i = 0; i < n; i++) print v[order[i]] }' > "$f"
}
wait_for() {  # wait_for <secs> <cmd...>: poll until the command succeeds
    local t=$1; shift
    for _ in $(seq 1 $((t * 4))); do "$@" && return 0; sleep 0.25; done
    return 1
}
json_ok() { python3 -c 'import json,sys; json.load(sys.stdin)' 2>/dev/null; }

NODE=stubvms_1025
CONFIG_T=homeassistant/device/$NODE/config
STATE_T=openvms/$NODE/state
AVAIL_T=openvms/$NODE/availability

start_broker
echo 'cpu 20' > control

echo "== discovery, availability and state"
conf a.conf
start_agent agent1.log a.conf
check "the agent connects" wait_for 10 grep -q 'connected to the broker' agent1.log
check "the node ID is saved" test "$(cat data/node_id)" = "$NODE"
S -t "$CONFIG_T" -C 1 -W 5 > config.json
check "retained discovery config is valid JSON" json_ok < config.json
check "41 sensors, origin and device" python3 - config.json <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
assert len(d["cmps"]) == 41, len(d["cmps"])
assert d["cmps"]["queue_sys_batch_problem"]["p"] == "binary_sensor"
assert d["cmps"]["queue_sys_batch_state"]["device_class"] == "enum"
assert d["o"]["name"] == "vms-hass" and d["dev"]["ids"] == ["stubvms_1025"]
assert all(c["unique_id"] == "stubvms_1025_" + k for k, c in d["cmps"].items())
assert d["availability_topic"] == "openvms/stubvms_1025/availability"
EOF
check "retained availability is online" test "$(S -t "$AVAIL_T" -C 1 -W 5)" = online
check "rates appear within 10 s (second sample)" wait_for 10 sh -c \
    "\"$SUB\" -h 127.0.0.1 -p 18830 -u tester -P t3ster -t $STATE_T -C 1 -W 2 2>/dev/null | grep -q '\"cpu\":20.0'"
S -t "$STATE_T" -C 1 -W 5 > state.json
check "state keys match the sensors" python3 - config.json state.json <<'EOF'
import json, sys
d = json.load(open(sys.argv[1])); s = json.load(open(sys.argv[2]))
assert set(d["cmps"]) == set(s), set(d["cmps"]) ^ set(s)
assert s["cpu_user"] == 12.0 and s["nic_ewa0_rx"] == 20.0, s
EOF

echo "== HA restarts (birth message)"
S -t "$CONFIG_T" -R -C 1 -W 6 > rebirth.json & sp=$!
sleep 0.5
P -t homeassistant/status -m online
wait $sp
check "discovery is published again" json_ok < rebirth.json
check "the agent logs it" grep -q 'came online' agent1.log

echo "== a disk is dismounted"
S -t "$CONFIG_T" -R -C 1 -W 8 > removed.json & sp=$!
sleep 0.3
echo 'disks DKA0:' >> control
wait $sp
check "DKA100's sensors are removed" python3 - removed.json <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["cmps"]["disk_dka100_used"] == {"p": "sensor"}, d["cmps"].get("disk_dka100_used")
assert d["cmps"]["disk_dka100_problem"] == {"p": "binary_sensor"}, d["cmps"].get("disk_dka100_problem")
assert "unique_id" in d["cmps"]["disk_dka0_used"]
EOF
echo 'cpu 20' > control

echo "== problem sensors (D8)"
state_has() {  # state_has <python expression over s>: the retained state satisfies it
    "$SUB" -h 127.0.0.1 -p 18830 -u tester -P t3ster -t "$STATE_T" -C 1 -W 2 2>/dev/null |
        python3 -c "import json,sys; s=json.load(sys.stdin); sys.exit(0 if ($1) else 1)" 2>/dev/null
}
printf '%s\n' 'cpu 20' 'queue SYS$BATCH stopped 3 0 generic' 'queue STUBVMS_BATCH idle 0 1' \
    'errors DKA0: 0' 'shadow DSA1: 2 2 0 steady' 'disks DKA0:,DKA100:,DSA1:' 'cluster 2 2 2' > control
check "a stopped queue is a problem" wait_for 12 state_has \
    's["queue_sys_batch_state"] == "stopped" and s["queue_sys_batch_problem"] == "ON" and s["queue_sys_batch_pending"] == 3'
check "a running queue is not" state_has 's["queue_stubvms_batch_problem"] == "OFF" and s["queue_stubvms_batch_executing"] == 1'
check "a shadow set and the cluster appear" state_has \
    's["shadow_dsa1_state"] == "steady" and s["shadow_dsa1_problem"] == "OFF" and s["cluster_nodes"] == 2'
sed -i 's/^queue SYS\$BATCH stopped 3 0/queue SYS$BATCH idle 0 0/; s/^errors DKA0: 0/errors DKA0: 2/' control
check "the queue started again: problem off" wait_for 12 state_has 's["queue_sys_batch_problem"] == "OFF"'
check "disk errors rose: problem on" state_has 's["disk_dka0_errors"] == 2 and s["disk_dka0_problem"] == "ON"'
sed -i 's/^shadow DSA1: 2 2 0 steady/shadow DSA1: 1 1 0 steady/' control
check "a member left the shadow set: degraded" wait_for 12 state_has \
    's["shadow_dsa1_state"] == "degraded" and s["shadow_dsa1_problem"] == "ON"'
sed -i 's/^shadow DSA1: 1 1 0 steady/shadow DSA1: 1 2 0 copying 37/' control
check "it is copied back in: copying, 37%" wait_for 12 state_has \
    's["shadow_dsa1_state"] == "copying" and s["shadow_dsa1_progress"] == 37'
sed -i 's/^shadow DSA1: 1 2 0 copying 37/shadow DSA1: 2 2 0 steady/' control
check "and steady again" wait_for 12 state_has \
    's["shadow_dsa1_state"] == "steady" and s["shadow_dsa1_problem"] == "OFF" and s["shadow_dsa1_progress"] is None'
S -t "$CONFIG_T" -R -C 1 -W 12 > noshadow.json & sp=$!
sleep 0.3
echo 'cpu 20' > control
wait $sp
check "the set dismounted: its binary sensor is removed as one" python3 - noshadow.json <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["cmps"]["shadow_dsa1_problem"] == {"p": "binary_sensor"}, d["cmps"].get("shadow_dsa1_problem")
assert d["cmps"]["cluster_nodes"] == {"p": "sensor"}
EOF

echo "== the last will (kill -9)"
S -t "$AVAIL_T" -R -C 1 -W 25 > will.txt & sp=$!
sleep 0.3
t0=$(date +%s)
kill -9 "$agent_pid"; wait "$agent_pid" 2>/dev/null; agent_pid=
wait $sp
t1=$(date +%s)
check "the broker publishes offline" test "$(cat will.txt)" = offline
check "within 1.5 x keepalive (+2 s)" test $((t1 - t0)) -le 17
check "and retains it" test "$(S -t "$AVAIL_T" -C 1 -W 3)" = offline

echo "== the broker restarts"
start_agent agent2.log a.conf
check "the agent reconnects" wait_for 10 grep -q 'connected to the broker' agent2.log
stop_broker
start_broker                            # persistence off: every retained message is gone
check "the agent connects again" wait_for 15 sh -c "grep -c 'connected to the broker' agent2.log | grep -q 2"
check "and republishes discovery" sh -c "\"$SUB\" -h 127.0.0.1 -p 18830 -u tester -P t3ster -t $CONFIG_T -C 1 -W 5 2>/dev/null | python3 -c 'import json,sys; json.load(sys.stdin)'"
check "and online" test "$(S -t "$AVAIL_T" -C 1 -W 5)" = online

echo "== orderly stop (SIGTERM)"
kill -TERM "$agent_pid"; wait "$agent_pid"; rc=$?; agent_pid=
check "exit status 0" test $rc -eq 0
check "availability offline" test "$(S -t "$AVAIL_T" -C 1 -W 3)" = offline
check "the broker saw a DISCONNECT, not a timeout" grep -q 'Client vms-hass-stubvms_1025 disconnected\.' broker.log

echo "== a disk dismounted while the agent was stopped"
check "the sensor list was saved" grep -q '^disk_dka100_used$' data/published
echo 'disks DKA0:' > control
S -t "$CONFIG_T" -R -C 1 -W 10 > restart.json & sp=$!
sleep 0.3
start_agent agent2b.log a.conf
wait $sp
check "its sensors are removed on the next start" python3 - restart.json <<'EOF2'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["cmps"]["disk_dka100_used"] == {"p": "sensor"}, d["cmps"].get("disk_dka100_used")
EOF2
stop_agent
check "and the saved list no longer has them" sh -c "! grep -q dka100 data/published"
echo 'cpu 20' > control

echo "== --remove"
HA_STUB_CONTROL=$w/control "$AGENT" --remove a.conf > remove.log 2>&1; rc=$?
check "exit status 0" test $rc -eq 0
check "no retained config" test -z "$(S -t "$CONFIG_T" -C 1 -W 2)"
check "no retained state" test -z "$(S -t "$STATE_T" -C 1 -W 2)"
check "no retained availability" test -z "$(S -t "$AVAIL_T" -C 1 -W 2)"

echo "== a wrong password"
conf b.conf "MQTT_PASSWORD_FILE=$w/wrong.pwd" "DATA_DIR=$w/data2"
start_agent agent3.log b.conf
check "refused as not authorised" wait_for 8 grep -q 'not authorised' agent3.log
check "and retried" wait_for 8 sh -c "grep -c 'not authorised' agent3.log | grep -qv '^1$'"
stop_agent

echo "== a password file others can read"
chmod 644 wrong.pwd
HA_STUB_CONTROL=$w/control "$AGENT" b.conf > agent4.log 2>&1; rc=$?
check "refuses to start" test $rc -ne 0
check "and says why" grep -q 'can be read by others' agent4.log
chmod 600 wrong.pwd

echo "== a typo in the configuration"
printf 'MQTT_HOTS=x\n' > typo.conf
"$AGENT" typo.conf > agent5.log 2>&1; rc=$?
check "refuses to start, naming the line" sh -c "test $rc -ne 0 && grep -q 'line 1: unknown setting \"MQTT_HOTS\"' agent5.log"

echo "== TLS"
conf c.conf "MQTT_HOST=localhost" "MQTT_PORT=18831" "MQTT_TLS=yes" "MQTT_CA_FILE=$w/ca.pem"
start_agent agent6.log c.conf
check "connects over TLS, verifying the broker" wait_for 10 grep -q 'connected to the broker' agent6.log
check "and publishes" test "$(S -t "$AVAIL_T" -C 1 -W 5)" = online
stop_agent
conf d.conf "MQTT_HOST=127.0.0.1" "MQTT_PORT=18831" "MQTT_TLS=yes" "MQTT_CA_FILE=$w/ca.pem"
start_agent agent7.log d.conf
check "refuses a certificate for another name" wait_for 8 grep -q 'certificate is not for 127.0.0.1' agent7.log
stop_agent
conf e.conf "MQTT_HOST=localhost" "MQTT_PORT=18831" "MQTT_TLS=yes"
start_agent agent8.log e.conf
check "refuses an untrusted CA" wait_for 8 grep -q 'certificate not trusted' agent8.log
stop_agent

echo "== sanitizers"
check "no AddressSanitizer or UBSan reports" sh -c "! grep -l 'Sanitizer\|runtime error' agent*.log remove.log"
check "the password never reaches a log" sh -c "! grep -l 's3cret-agent' agent*.log remove.log broker.log"

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
