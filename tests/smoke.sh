#!/bin/sh
# Smoke test: server and client on localhost complete the handshake and
# both keep running (exchanging MSG_DATA) until the client is stopped.
set -eu

log_dir=$(mktemp -d)
server_pid=

cleanup() {
    if [ -n "$server_pid" ]; then
        kill "$server_pid" 2>/dev/null || true
    fi
    rm -rf "$log_dir"
}
trap cleanup EXIT

fail() {
    echo "FAIL: $1"
    echo "--- server log"
    cat "$log_dir/server.log"
    echo "--- client log"
    cat "$log_dir/client.log"
    exit 1
}

stdbuf -oL bin/server > "$log_dir/server.log" 2>&1 &
server_pid=$!
sleep 0.5

# The client runs forever, so a timeout (exit status 124) is the expected outcome.
status=0
timeout 2 stdbuf -oL bin/client 127.0.0.1 > "$log_dir/client.log" 2>&1 || status=$?

[ "$status" -eq 124 ] || fail "client exited with status $status before the timeout"
kill -0 "$server_pid" 2>/dev/null || fail "server is not running"
grep -q "Received CONN_REQ" "$log_dir/server.log" || fail "server did not receive CONN_REQ"
grep -q "Connection established" "$log_dir/client.log" || fail "client did not complete the handshake"

echo "OK: handshake completed, both sides kept running"
