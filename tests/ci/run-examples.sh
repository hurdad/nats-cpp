#!/usr/bin/env bash
# Run every example against the servers from tests/ci/start-servers.sh and fail if any exits non-zero.
#
#   tests/ci/run-examples.sh <build-dir>
#
# Needs the NATSCPP_TEST_* variables that start-servers.sh prints (for the TLS/auth runs).
set -euo pipefail

build=${1:?usage: run-examples.sh <build-dir>}
failed=0

run() {  # label, command...
  local label=$1; shift
  if timeout 60 "$@" > "$build/example-$label.log" 2>&1; then
    echo "ok    $label"
  else
    echo "FAIL  $label (exit $?)"; sed 's/^/      /' "$build/example-$label.log"
    failed=1
  fi
}

for name in sync async request_reply headers_trace queue_group lifecycle coroutine \
            jetstream_sync_pull jetstream_sync_push jetstream_async_pull jetstream_async_push kv; do
  run "$name" "$build/natscpp_${name}_example"
done

# Two-process request/reply: the client must fail cleanly without the server, then succeed with it.
if "$build/natscpp_request_reply_client_example" > /dev/null 2>&1; then
  echo "FAIL  request_reply_client without a server should exit non-zero"; failed=1
else
  echo "ok    request_reply_client (no server: clean failure)"
fi
"$build/natscpp_request_reply_server_example" > "$build/example-server.log" 2>&1 &
server_pid=$!
for _ in $(seq 1 50); do grep -q listening "$build/example-server.log" && break; sleep 0.1; done
run request_reply_client "$build/natscpp_request_reply_client_example"
kill -INT "$server_pid"
if wait "$server_pid" && grep -q stopped "$build/example-server.log"; then
  echo "ok    request_reply_server (graceful stop on SIGINT)"
else
  echo "FAIL  request_reply_server did not stop cleanly"; sed 's/^/      /' "$build/example-server.log"; failed=1
fi

# TLS and user/password authentication.
if [ -n "${NATSCPP_TEST_TLS_URL_A:-}" ]; then
  run secure_tls env NATS_URL="$NATSCPP_TEST_TLS_URL_A" NATS_CA="$NATSCPP_TEST_TLS_DIR/root.pem" \
    "$build/natscpp_secure_connection_example"
fi
if [ -n "${NATSCPP_TEST_AUTH_URL:-}" ]; then
  run secure_auth env NATS_URL="$NATSCPP_TEST_AUTH_URL" NATS_USER=alice NATS_PASSWORD=secret \
    "$build/natscpp_secure_connection_example"
fi

exit "$failed"
