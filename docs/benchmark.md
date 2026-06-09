# Benchmarking

The benchmark client measures:

- Total requests
- Concurrent clients
- Throughput
- Average latency
- p50 latency
- p95 latency
- p99 latency

## Example

```bash
./build/miniredis_benchmark \
  --host 127.0.0.1 \
  --port 6380 \
  --requests 4000 \
  --clients 4 \
  --command SETGET
```

## Sample Local Results

These numbers are local baseline measurements, not production claims. The server
was run with 16 worker threads and AOF disabled for the benchmark run.

| Scenario | Clients | Requests | Throughput | Avg latency | p50 | p95 | p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| PING | 1 | 10000 | ~25.8k req/sec | ~0.039 ms | ~0.033 ms | ~0.070 ms | ~0.103 ms |
| PING | 4 | 10000 | ~54.7k req/sec | ~0.073 ms | ~0.061 ms | ~0.141 ms | ~0.236 ms |
| PING | 8 | 10000 | ~56.1k req/sec | ~0.142 ms | ~0.119 ms | ~0.290 ms | ~0.662 ms |
| PING | 16 | 10000 | ~55.9k req/sec | ~0.283 ms | ~0.241 ms | ~0.546 ms | ~1.347 ms |
| SETGET | 1 | 10000 | ~25.1k req/sec | ~0.040 ms | ~0.033 ms | ~0.071 ms | ~0.122 ms |
| SETGET | 4 | 10000 | ~50.7k req/sec | ~0.078 ms | ~0.064 ms | ~0.147 ms | ~0.231 ms |
| SETGET | 8 | 10000 | ~41.5k req/sec | ~0.192 ms | ~0.121 ms | ~0.414 ms | ~1.611 ms |
| SETGET | 16 | 10000 | ~54.3k req/sec | ~0.292 ms | ~0.247 ms | ~0.559 ms | ~1.330 ms |
