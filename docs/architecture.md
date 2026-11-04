# Architecture

Mini Redis C++ is organized as a small set of replaceable modules:

```text
Client
  -> Accept Thread (connection limit)
  -> Event Loop (epoll / kqueue / poll)
  -> RESP Parser
  -> Command Dispatcher
  -> Sharded Store / Active Expiry
  -> AOF Logger / AOF Rewrite
```

## Source Layout

The main implementation files are:

- `src/server.cpp` for socket setup, the accept thread, event loops, and per-connection buffering
- `src/poller.cpp` for the `epoll`, `kqueue`, and `poll` readiness backends
- `src/durable_executor.cpp` for AOF commit ordering and online rewrite
- `src/resp.cpp` for RESP parsing and encoding
- `src/command.cpp` for Redis command validation and dispatch
- `src/store.cpp` for sharded concurrency, TTL, and mutation semantics
- `src/aof.cpp` for append-only persistence and replay behavior
- `tests/integration_test.cpp` for real-server socket coverage
- `benchmarks/benchmark_client.cpp` for pipelined benchmark behavior

## Modules

### TCP Server

Owns the listening socket, accepts client connections, reads requests, and writes RESP responses.

An accept thread blocks in `accept`, rejects connections beyond `--max-clients` by closing them immediately, switches accepted sockets to non-blocking mode, and hands them round-robin to N event loops through a mutex-protected queue plus a wakeup pipe.

Each event loop owns one `Poller` and many connections. The poller is level-triggered and wraps `epoll` on Linux, `kqueue` on macOS/BSD, and `poll` elsewhere. On readiness the loop reads up to 64 KiB, executes every complete request in the buffer inline, and writes replies until the socket would block. If 1 MiB of replies is queued, the loop stops reading and parsing that client and waits for write readiness; reading resumes once the backlog drains. A periodic sweep closes connections with no read or write progress for `--idle-timeout-ms`.

Commands execute on the loop thread. Because the store locks per shard, several loops can serve unrelated keys in parallel, so loop count is a CPU knob rather than a connection limit.

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
- `EXPIREAT`
- `PEXPIREAT`
- `TTL`
- `INCR`
- `DECR`
- `BGREWRITEAOF` (handled by the durable executor, which owns the AOF)

`SET` supports Redis-style `EX`, `PX`, `EXAT`, `PXAT`, `NX`, and `XX` options. The dispatcher keeps parsing and arity validation separate from store mutation so command semantics are testable without a network server.

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

Each shard also keeps a vector of pointers to the entries that carry a TTL, and each such entry stores its index in that vector. Setting, clearing, or deleting a TTL updates the index in O(1) by swap-and-pop. `unordered_map` nodes are stable across rehashing, so the pointers stay valid until the entry is erased.

Read-heavy operations such as `GET` and `TTL` first take a shared lock on the relevant shard. If they observe an expired key, they release the read lock and clean it up through a short exclusive path. Mutating operations such as `SET`, `DEL`, `EXPIRE`, and `INCR` take an exclusive lock on the relevant shard for atomicity.

### TTL Manager

Expiration has two layers:

- Lazy expiration during reads and writes
- Active expiration in a background thread

Every 100 ms the background thread runs one Redis-style active expiry cycle. For each shard it picks 20 random entries from the shard's TTL index under that shard's exclusive lock, deletes the expired ones, and repeats on the same shard only while more than 25% of the sample was expired. The cycle stops after a 25 ms budget. Each lock hold therefore covers at most 20 checks, and the work per tick no longer grows with the size of the keyspace.

### AOF Persistence

Append-only file persistence records state changes as blind writes or absolute deadlines:

- `SET` (with `EX`/`PX` rewritten to `PXAT`, and `NX`/`XX` dropped because only applied writes are logged)
- `DEL`
- `PEXPIREAT` (`EXPIRE` is rewritten to this form) and `EXPIREAT`
- `INCR`/`DECR`, logged as the resulting `SET`, keeping the key's existing deadline

On startup, the server replays the AOF file to rebuild the in-memory store. The AOF file remains open during runtime, supports configurable `fsync` policy, and truncates incomplete/corrupt tails during replay.

`BGREWRITEAOF`, or growth past `--auto-aof-rewrite-min-size` and `--auto-aof-rewrite-percentage`, starts an online rewrite in a background thread. See `docs/persistence.md` for the rewrite protocol and the argument for why it is safe while writes continue.

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
- `PING`, `ECHO`, `SET`, `GET`, `MGET`, `EXISTS`, `DEL`, `EXPIRE`, `EXPIREAT`, `PEXPIREAT`, `TTL`, `INCR`, `DECR`, and `BGREWRITEAOF`
- Sharded thread-safe in-memory store
- Lazy expiration and sampled active expiration
- AOF append, startup replay, and online rewrite
- Event-loop networking with connection limits, idle sweep, and output backpressure
- Single-process benchmark client with multi-client and pipelined modes
