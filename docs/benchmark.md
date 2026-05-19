# Benchmark Plan

The benchmark client should measure:

- Total requests
- Concurrent clients
- QPS
- p50 latency
- p95 latency
- p99 latency

## Example

```bash
./build/miniredis_benchmark \
  --host 127.0.0.1 \
  --port 6379 \
  --clients 50 \
  --requests 100000 \
  --command set-get
```

## Result Table Template

| Scenario | Clients | Requests | QPS | p50 | p95 | p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| SET/GET | 50 | 100000 | TBD | TBD | TBD | TBD |
