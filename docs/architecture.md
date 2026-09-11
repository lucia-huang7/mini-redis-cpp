# Architecture

Mini Redis C++ is organized as a small set of replaceable modules:

```text
Client
  -> TCP Server
  -> Connection Handler
  -> RESP Parser
  -> Command Dispatcher
  -> Sharded Store / TTL Manager
  -> AOF Logger
```

## Source Layout

The main implementation files are:

- `src/server.cpp` for socket setup, accept loop, client handling, and worker dispatch
- `src/resp.cpp` for RESP parsing and encoding
- `src/command.cpp` for Redis command validation and dispatch
- `src/store.cpp` for sharded concurrency, TTL, and mutation semantics
- `src/aof.cpp` for append-only persistence and replay behavior
- `tests/integration_test.cpp` for real-server socket coverage
- `benchmarks/benchmark_client.cpp` for pipelined benchmark behavior

## Modules

### TCP Server

Owns the listening socket, accepts client connections, reads requests, and writes RESP responses.

The current implementation uses a fixed worker pool. The main thread accepts connections and enqueues each accepted client socket as a connection task. Worker threads then run the blocking connection loop for assigned clients.

This model is intentionally simpler than an event loop based on `poll`, `epoll`, or `kqueue`, but it avoids unbounded thread creation and makes concurrency behavior explicit. It is a reasonable intermediate design for this project because the command path, store locking, AOF writes, and benchmark tooling are still easy to inspect.

### RESP Parser

Parses Redis Serialization Protocol values and encodes responses.

Supported values:

- Simple string
- Error
- Integer
- Bulk string
- Array

The parser handles partial TCP reads by returning an incomplete parse result until enough bytes arrive.

### Command Dispatcher

Validates command arity, normalizes command names, calls the store, and returns RESP responses.

Supported commands:

- `PING`
- `ECHO`
- `SET`
- `GET`
- `MGET`
- `EXISTS`
- `DEL`
- `EXPIRE`
- `TTL`
- `INCR`
- `DECR`

`SET` supports Redis-style `EX`, `PX`, `NX`, and `XX` options. The dispatcher keeps parsing and arity validation separate from store mutation so command semantics are testable without a network server.

### Store

The in-memory key-value engine uses lock striping:

```cpp
std::array<Shard, kShardCount>
Shard {
  std::unordered_map<std::string, Value>
  std::shared_mutex
}
```

Each key maps to a shard by `std::hash<std::string>`. This keeps single-key operations atomic while reducing contention between unrelated keys.

Read-heavy operations such as `GET` and `TTL` first take a shared lock on the relevant shard. If they observe an expired key, they release the read lock and clean it up through a short exclusive path. Mutating operations such as `SET`, `DEL`, `EXPIRE`, and `INCR` take an exclusive lock on the relevant shard for atomicity.

### TTL Manager

Expiration has two layers:

- Lazy expiration during reads and writes
- Background cleanup for expired keys

The cleanup pass scans shards one at a time, so it does not take a global store lock.

### AOF Persistence

Append-only file persistence records mutating commands:

- `SET`
- `DEL`
- `EXPIRE`
- `INCR`
- `DECR`

On startup, the server replays the AOF file to rebuild the in-memory store. The AOF file remains open during runtime, supports configurable `fsync` policy, and truncates incomplete/corrupt tails during replay.

See `docs/persistence.md` for durability policy and TTL persistence tradeoffs.

## Benchmarking

The benchmark client supports:

- Concurrent clients
- Pipelined request batches through `--pipeline`
- Configurable key distribution through `--keyspace`
- Throughput, average latency, and p50/p95/p99 reporting

This makes lock contention and tail-latency behavior easier to inspect under different read/write and keyspace patterns.

## Current Capabilities

- RESP parser and encoder
- TCP server compatible with `redis-cli`
- `PING`, `ECHO`, `SET`, `GET`, `MGET`, `EXISTS`, `DEL`, `EXPIRE`, `TTL`, `INCR`, and `DECR`
- Sharded thread-safe in-memory store
- Lazy expiration and background TTL cleanup
- AOF append and startup replay
- Fixed worker pool for client handling
- Single-process benchmark client with multi-client and pipelined modes
