#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-6380}"
REQUESTS="${2:-10000}"
COMMAND="${3:-PING}"

"$(dirname "$0")/../../build/miniredis_benchmark" \
  --host 127.0.0.1 \
  --port "$PORT" \
  --requests "$REQUESTS" \
  --command "$COMMAND"
