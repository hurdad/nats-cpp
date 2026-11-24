#!/usr/bin/env bash
# Stop the servers started by tests/ci/start-servers.sh <work-dir>.
set -u
work=${1:?usage: stop-servers.sh <work-dir>}
[ -f "$work/pids" ] && xargs -r kill < "$work/pids" 2>/dev/null
rm -f "$work/pids"
