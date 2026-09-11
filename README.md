# Mini Redis C++

![C++ 20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus&logoColor=white) ![Protocol RESP2](https://img.shields.io/badge/Protocol-RESP2-6C5CE7?style=flat-square) ![Networking POSIX TCP](https://img.shields.io/badge/Networking-POSIX_TCP-087E8B?style=flat-square) ![Store 64-shard KV](https://img.shields.io/badge/Store-64--shard_KV-2563A6?style=flat-square) ![Locking shared mutex](https://img.shields.io/badge/Locking-std%3A%3Ashared__mutex-7C3AED?style=flat-square)<br>
![Execution Bounded Worker Pool](https://img.shields.io/badge/Execution-Bounded_Worker_Pool-0F766E?style=flat-square) ![Index unordered map](https://img.shields.io/badge/Index-std%3A%3Aunordered__map-0369A1?style=flat-square) ![Expiration TTL Engine](https://img.shields.io/badge/Expiration-Lazy_TTL_%2B_Cleaner-DB2777?style=flat-square) ![Persistence AOF](https://img.shields.io/badge/Persistence-Crash--safe_AOF-D97706?style=flat-square) ![Quality Toolchain](https://img.shields.io/badge/Quality-CMake_%7C_Sanitizers_%7C_libFuzzer-2E7D32?style=flat-square&logo=cmake&logoColor=white)

A Redis-compatible in-memory key-value server written in C++20. It implements RESP over TCP, command dispatch, sharded storage, TTL expiration, AOF persistence, real-socket integration tests, and a local benchmark client. Supported commands are `PING`, `ECHO`, `SET`, `GET`, `MGET`, `EXISTS`, `DEL`, `EXPIRE`, `EXPIREAT`, `PEXPIREAT`, `TTL`, `INCR`, and `DECR`; `SET` supports `EX`, `PX`, `EXAT`, `PXAT`, `NX`, and `XX`.

## Architecture

![Mini Redis C++ engineering architecture](docs/engineering-architecture.png)

The network layer uses blocking sockets and a fixed-size worker pool. A bounded admission queue limits pending connections, while socket timeouts release workers held by inactive clients. Each active connection occupies one worker; scaling beyond this model would require an event loop based on `poll`, `epoll`, or `kqueue`.

The store hashes keys across 64 shards. Each shard owns an `unordered_map` and a `std::shared_mutex`: `GET` and `TTL` use shared locks, while writes, expiration cleanup, and integer updates take an exclusive lock only on the target shard. TTL is handled through lazy expiration and a background cleaner. State-changing commands and their AOF records share one commit order; TTL-bearing writes are persisted with absolute Unix-millisecond deadlines so downtime cannot extend key lifetime. The AOF supports `always`, `everysec`, and `no` sync policies, startup replay, and corrupt-tail truncation.

Tests cover protocol boundaries, command semantics, concurrency, idle clients, queue exhaustion, absolute-TTL restart behavior, AOF ordering, short writes, `ENOSPC`, and `fsync` failures. Integration tests start a real server process and communicate through TCP sockets. Linux CI runs Release, ASan/UBSan, ThreadSanitizer, and libFuzzer jobs; `.clang-tidy` is included for local static analysis.

![Mini Redis C++ failure containment paths](docs/failure-containment-paths.png)

## Build and Test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Start the server with AOF enabled:

```bash
./build/miniredis_server \
  --port 6380 \
  --workers 16 \
  --max-queued-clients 1024 \
  --idle-timeout-ms 30000 \
  --aof data/appendonly.aof \
  --appendfsync everysec
```

Example commands:

```bash
redis-cli -p 6380 PING
redis-cli -p 6380 SET name alice NX EX 60
redis-cli -p 6380 GET name
redis-cli -p 6380 INCR visits
```

## Performance

All numbers below are local loopback measurements on a single machine, reported as the median of five runs. The throughput comparison uses identical client count, pipeline depth, keyspace, and disabled persistence for Mini Redis and Redis 8.4. Results vary with system load and are not cross-machine comparisons.

### Throughput vs. Redis 8.4

Measured with `redis-benchmark`, 16 clients, pipeline depth 16, 500,000 requests per run, and a 100,000-key random keyspace.

| Command | Mini Redis | Redis 8.4 | Relative |
|---|---:|---:|---:|
| `GET` | 384.3k ops/s | 344.4k ops/s | 112% |
| `SET` | 392.5k ops/s | 330.0k ops/s | 119% |

The 64-shard store and fixed worker pool allow independent keys to be served concurrently, while Redis 8.4 executes these commands on its main command thread. The comparison highlights the parallelism available in this loopback workload; it is not intended as a general replacement claim for Redis.

### Latency at low concurrency

Measured with the bundled benchmark client using `PING`, pipeline depth 1, and 100,000 requests per run.

| Clients | p50 | p99 | Throughput |
|---:|---:|---:|---:|
| 1 | 0.088 ms | 0.627 ms | 7.8k ops/s |
| 2 | 0.127 ms | 0.412 ms | 14.2k ops/s |
| 4 | 0.147 ms | 0.471 ms | 23.4k ops/s |

Mini Redis accepts RESP array commands only. Because `redis-benchmark -t ping` begins with an inline `PING`, PING measurements use the bundled RESP benchmark client instead.

### Reproduce

```bash
# Start Mini Redis with persistence disabled for the throughput comparison.
./build/miniredis_server --port 6380 --workers 16 --aof ""

# Run the same workload against Mini Redis and Redis 8.4.
redis-benchmark -p 6380 -t set,get -c 16 -P 16 -n 500000 -r 100000
redis-benchmark -p 6379 -t set,get -c 16 -P 16 -n 500000 -r 100000

# Reproduce a low-concurrency latency point with the bundled client.
./build/miniredis_benchmark --host 127.0.0.1 --port 6380 \
  --requests 100000 --clients 1 --pipeline 1 --command PING
```

Implementation details are documented in [architecture](docs/architecture.md), [persistence](docs/persistence.md), [engineering notes](docs/engineering.md), and [benchmark methodology](docs/benchmark.md).
