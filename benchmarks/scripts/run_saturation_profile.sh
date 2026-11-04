#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
PORT="${PORT:-6380}"
REQUESTS="${REQUESTS:-100000}"
PIPELINE="${PIPELINE:-16}"
KEYSPACE="${KEYSPACE:-100000}"
COMMAND="${COMMAND:-SETGET}"
CLIENT_LEVELS="${CLIENT_LEVELS:-1 2 4 8 16 32 64}"
OUTPUT_DIR="${OUTPUT_DIR:-performance-results}"

mkdir -p "$OUTPUT_DIR"
results="$OUTPUT_DIR/saturation.csv"
details="$OUTPUT_DIR/benchmark.log"
samples="$OUTPUT_DIR/server-samples.txt"
printf 'clients,throughput_req_s,p99_ms,peak_rss_kb,peak_cpu_percent\n' > "$results"
: > "$details"

"$BUILD_DIR/miniredis_server" \
  --port "$PORT" \
  --workers "${WORKERS:-$(getconf _NPROCESSORS_ONLN)}" \
  --max-clients 4096 \
  --idle-timeout-ms 30000 \
  --aof "" > "$OUTPUT_DIR/server.log" 2>&1 &
server_pid=$!

cleanup() {
  kill "$server_pid" 2>/dev/null || true
  wait "$server_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM
sleep 1

for clients in $CLIENT_LEVELS; do
  : > "$samples"
  (
    while kill -0 "$server_pid" 2>/dev/null; do
      ps -p "$server_pid" -o rss=,%cpu= >> "$samples" 2>/dev/null || true
      sleep 0.2
    done
  ) &
  sampler_pid=$!

  output=$("$BUILD_DIR/miniredis_benchmark" \
    --host 127.0.0.1 \
    --port "$PORT" \
    --requests "$REQUESTS" \
    --clients "$clients" \
    --pipeline "$PIPELINE" \
    --keyspace "$KEYSPACE" \
    --command "$COMMAND")

  kill "$sampler_pid" 2>/dev/null || true
  wait "$sampler_pid" 2>/dev/null || true
  printf '%s\n\n' "$output" >> "$details"

  throughput=$(printf '%s\n' "$output" | awk '/^Throughput:/ {print $2}')
  p99=$(printf '%s\n' "$output" | awk '/^p99 latency:/ {print $3}')
  peak_rss=$(awk 'BEGIN {m=0} $1+0>m {m=$1+0} END {print m}' "$samples")
  peak_cpu=$(awk 'BEGIN {m=0} $2+0>m {m=$2+0} END {print m}' "$samples")
  printf '%s,%s,%s,%s,%s\n' "$clients" "$throughput" "$p99" "$peak_rss" "$peak_cpu" | tee -a "$results"
done

awk -F, 'NR==2 || $2+0>best {best=$2+0; clients=$1} END {printf "Peak throughput %.2f req/s at %s clients\n", best, clients}' "$results"
printf 'Results: %s\n' "$results"
