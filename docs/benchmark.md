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

These numbers are local baseline measurements, not production claims.

| Scenario | Clients | Requests | Throughput | Avg latency | p50 | p95 | p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| PING | 4 | 4000 | ~45.6k req/sec | ~0.087 ms | ~0.066 ms | ~0.156 ms | ~0.233 ms |
| SETGET | 4 | 4000 | ~39.1k req/sec | ~0.101 ms | ~0.087 ms | ~0.183 ms | ~0.281 ms |
