# Project Brief

Mini Redis C++ is a small Redis-compatible server written from scratch in C++20. It implements RESP parsing, TCP request handling, command dispatch, concurrent storage, TTL expiration, append-only persistence, testing, and benchmarking.

## Components

- RESP parser/encoder and a TCP server compatible with common `redis-cli` workflows.
- Command dispatch for `SET`, `GET`, `MGET`, `EXISTS`, `DEL`, `EXPIRE`, `EXPIREAT`, `PEXPIREAT`, `TTL`, `INCR`, `DECR`, and `BGREWRITEAOF`.
- Sharded in-memory storage with one `std::shared_mutex` per shard.
- Lazy expiration plus sampled active expiration over a per-shard TTL index.
- Ordered AOF commits, configurable `fsync`, startup replay, corrupt-tail truncation, and online rewrite.
- Event loops over `epoll`, `kqueue`, or `poll`, with a connection limit, idle sweep, and output backpressure.
- Unit and real-socket integration tests covering concurrency, persistence failures, malformed requests, and resource limits.
- Benchmark client with concurrent clients, pipelining, configurable keyspace size, throughput, and latency percentiles.
- Linux Release, sanitizer, ThreadSanitizer, `poll`-backend, and fuzz CI jobs, a macOS `kqueue` job, plus local `.clang-tidy` configuration.

## Tradeoffs

The server runs commands inline on its event-loop threads. That keeps the request path short, and the sharded store still lets loops serve unrelated keys in parallel, but one very slow command (a large `MGET`, for example) delays the other clients on the same loop.

The store uses lock striping instead of a single global mutex. This improves concurrent access when keys are distributed across shards, while keeping per-key operations simple and atomic. Multi-key commands such as `MGET` intentionally call single-key read paths rather than trying to acquire many shard locks at once.

AOF persistence serializes each state-changing command and its log append under one commit lock, so `appendfsync always` pays one `fsync` per write. Online rewrite avoids `fork` by relying on blind-write records, at the cost of holding a per-shard copy in memory while each shard is written out.

## Known Limitations

- Inline (non-RESP) commands are not supported.
- `appendfsync always` does not batch concurrent writers into one `fsync`.
- There is no graceful shutdown path that forces a final `fsync` under `everysec`.
- The command set is intentionally small and does not include hashes, sorted sets, pub/sub, replication, or clustering.
