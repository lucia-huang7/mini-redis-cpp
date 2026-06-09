# Mini Redis C++

A from-scratch Redis server in C++ with RESP protocol parsing, thread-pool concurrency, shared-mutex read paths, AOF persistence, and multi-client benchmarking with latency percentiles.

The project implements the core Redis path: RESP parsing, TCP request handling,
command dispatch, in-memory storage, key expiration, append-only persistence,
and local benchmarking.

## Features

- RESP parser and encoder
- `redis-cli` compatible TCP server
- Commands: `PING`, `ECHO`, `SET`, `GET`, `MGET`, `EXISTS`, `DEL`, `EXPIRE`, `TTL`, `INCR`, `DECR`
- `SET` options: `EX`, `PX`, `NX`, `XX`
- Thread-safe in-memory store with shared locks for read-heavy paths
- Fixed worker pool for concurrent clients
- Lazy expiration plus background TTL cleanup
- AOF persistence with configurable `fsync` policy
- AOF replay with corrupt-tail truncation
- Unit tests and integration tests that start a real server process
- Benchmark client with multi-client mode and latency percentiles

## Build

```bash
cmake -S . -B build
cmake --build build
```

Run the server:

```bash
./build/miniredis_server \
  --port 6380 \
  --workers 4 \
  --aof data/appendonly.aof \
--appendfsync everysec
```

For pure in-memory benchmark runs, disable AOF explicitly:

```bash
./build/miniredis_server --port 6380 --workers 4 --aof ""
```

Try it with `redis-cli`:

```bash
redis-cli -p 6380 PING
redis-cli -p 6380 SET name alice
redis-cli -p 6380 GET name
redis-cli -p 6380 SET lock token NX EX 10
redis-cli -p 6380 INCR visits
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

The CTest suite includes both unit tests and integration tests. The integration
tests start `miniredis_server` as a child process and exercise real TCP sockets,
including pipelined RESP, split packets, concurrent clients, TTL expiry, errors,
and AOF replay.

## Benchmark

```bash
./build/miniredis_benchmark \
  --host 127.0.0.1 \
  --port 6380 \
  --requests 4000 \
  --clients 4 \
  --pipeline 32 \
  --keyspace 10000 \
  --command SETGET
```

The benchmark reports throughput, average latency, and p50/p95/p99 latency.
`--pipeline` controls how many requests each client sends before reading the
matching responses; values such as 16, 32, or 64 are useful for measuring server
throughput without making one TCP round trip per command.
`--keyspace` controls how many keys the `SETGET` benchmark spreads requests
across; larger values exercise the sharded store under concurrent clients.
See [docs/benchmark.md](docs/benchmark.md) for sample local baseline numbers.

## Durability

AOF sync policy is explicit:

```bash
--appendfsync always
--appendfsync everysec
--appendfsync no
```

`everysec` is the default. See [docs/persistence.md](docs/persistence.md) for
the durability tradeoffs, replay behavior, and current TTL persistence semantics.

## Design Notes

The networking model uses blocking sockets with a fixed worker pool. This avoids
unbounded detached threads while keeping ownership and shutdown behavior easy to
inspect. Long-lived idle clients still occupy workers; this is an intentional
tradeoff before moving to a `poll`/`epoll`/`kqueue` event loop.

The store uses `std::shared_mutex`: read paths such as `GET` and `TTL` take
shared locks, while mutation, expiry cleanup, and integer updates take exclusive
locks. `INCR` and `DECR` are atomic at the store layer and preserve an existing
TTL.

CI builds Debug, Release, and ASan/UBSan configurations. The repository also
includes a `.clang-tidy` profile for local static analysis.

More detail:

- [docs/architecture.md](docs/architecture.md)
- [docs/persistence.md](docs/persistence.md)
- [docs/engineering.md](docs/engineering.md)

## Layout

```text
include/miniredis/    Public headers
src/                  Implementation files
tests/                Tests
benchmarks/           Benchmark client and scripts
docs/                 Architecture, persistence, and engineering notes
examples/             redis-cli examples
data/                 Local append-only files
```
