# Benchmarking

Mini Redis includes a benchmark client for tracking throughput and latency under concurrent workloads.

The client reports total requests, concurrent clients, pipeline depth, throughput, average latency, and p50/p95/p99 latency. `SETGET` can use a configurable keyspace to vary contention.

## Example

```bash
./build/miniredis_benchmark \
  --host 127.0.0.1 \
  --port 6380 \
  --requests 100000 \
  --clients 16 \
  --pipeline 16 \
  --keyspace 100000 \
  --command SETGET
```

## Saturation profile

`benchmarks/scripts/run_saturation_profile.sh` increases the client count and records throughput, p99 latency, peak server RSS, and peak sampled CPU in `performance-results/saturation.csv`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
REQUESTS=100000 PIPELINE=16 KEYSPACE=100000 \
CLIENT_LEVELS="1 4 8 16 32 64 128 256" \
  benchmarks/scripts/run_saturation_profile.sh
```

## Sample local results

The earlier worker-pool build peaked at 16 clients and degraded beyond its 16 workers; at 128 clients its p99 rose to tens of milliseconds because extra connections queued for a worker. Those figures described the old design and are no longer representative.

The event-loop build was measured on a 2-vCPU Linux VM against Redis 7.0.15:

- Non-pipelined `redis-benchmark -t set` at 50, 200, and 1,000 clients: about 92k, 80k, and 90k requests/second with 2 event loops (Redis 7.0: 82k, 107k, 102k).
- Pipelined `SET` (16 clients, depth 16, persistence off): median 738k requests/second with 16 loops and 857k with 2 loops, up from 481k before the commit lock was skipped for non-AOF writes.
- `GET` with 2M TTL-bearing keys loaded: max latency 4 to 5.5 ms, matching an empty keyspace, versus 19 to 27 ms with the old full-scan cleaner.

On a 2-vCPU machine more loops than cores adds scheduling overhead, so `--workers` defaults to the CPU count. These measurements are local baselines rather than cross-machine performance claims.
