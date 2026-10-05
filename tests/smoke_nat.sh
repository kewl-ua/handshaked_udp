#!/bin/sh
# Smoke test behind the CGNAT emulator: bin/server, bin/natemu with a port change every 700 ms,
# and bin/client talking to the emulator. The session must survive every port change.
set -eu

log_dir=$(mktemp -d)
pids=

cleanup() {
    for pid in $pids; do
        kill "$pid" 2>/dev/null || true
    done
    rm -rf "$log_dir"
}
trap cleanup EXIT

fail() {
    echo "FAIL: $1"
    for name in server natemu client; do
        echo "--- $name log"
        cat "$log_dir/$name.log"
    done
    exit 1
}

stdbuf -oL bin/server 16000 > "$log_dir/server.log" 2>&1 &
pids="$pids $!"
stdbuf -oL bin/natemu --listen 16001 --server 127.0.0.1:16000 --delay 5 --jitter 2 --rebind-every 700 > "$log_dir/natemu.log" 2>&1 &
pids="$pids $!"
sleep 0.5

# The client runs forever, so a timeout (exit status 124) is the expected outcome.
status=0
timeout 3 stdbuf -oL bin/client 127.0.0.1 16001 > "$log_dir/client.log" 2>&1 || status=$?

[ "$status" -eq 124 ] || fail "client exited with status $status before the timeout"
grep -q "connected. Session" "$log_dir/server.log" || fail "server did not accept the client"
grep -q "Connection established" "$log_dir/client.log" || fail "client did not complete the handshake"

rebinds=$(grep -c "dropped by rebind" "$log_dir/natemu.log" || true)
moves=$(grep -c "moved to" "$log_dir/server.log" || true)
[ "$rebinds" -ge 2 ] || fail "natemu rebound only $rebinds times"
[ "$moves" -ge 2 ] || fail "server followed only $moves of $rebinds port changes"
! grep -q "lost" "$log_dir/client.log" "$log_dir/server.log" || fail "the session was lost"

echo "OK: session survived $rebinds port changes, server followed $moves"
