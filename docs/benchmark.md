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

The README saturation chart uses 16 server workers, pipeline depth 16, a 100,000-key space, 100,000 requests per point, and AOF disabled. Each value is the median of three runs on the same machine.

Peak median throughput occurred at 16 clients: approximately 942k requests/second for `PING` and 922k requests/second for `SETGET`. Throughput declined beyond the 16-worker concurrency level. At 128 clients, p99 latency increased to about 61 ms for both workloads, exposing the queueing boundary of the blocking worker model.

These measurements are local baselines rather than cross-machine performance claims.
