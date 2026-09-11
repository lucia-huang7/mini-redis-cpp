# Project Brief

Mini Redis C++ is a small Redis-compatible server written from scratch in C++20. It implements RESP parsing, TCP request handling, command dispatch, concurrent storage, TTL expiration, append-only persistence, testing, and benchmarking.

## Components

- RESP parser/encoder and a TCP server compatible with common `redis-cli` workflows.
- Command dispatch for `SET`, `GET`, `MGET`, `EXISTS`, `DEL`, `EXPIRE`, `TTL`, `INCR`, and `DECR`.
- Sharded in-memory storage with one `std::shared_mutex` per shard.
- Lazy expiration and a background TTL cleanup worker.
- Ordered AOF commits, configurable `fsync`, startup replay, and corrupt-tail truncation.
- Fixed worker pool with bounded client admission and idle timeouts.
- Unit and real-socket integration tests covering concurrency, persistence failures, malformed requests, and resource limits.
- Benchmark client with concurrent clients, pipelining, configurable keyspace size, throughput, and latency percentiles.
- Linux Release, sanitizer, ThreadSanitizer, and fuzz CI jobs, plus local `.clang-tidy` configuration.

## Tradeoffs

The server currently uses blocking sockets with a fixed worker pool. Bounded admission and idle timeouts limit resource use, but each active connection still occupies a worker. An event loop based on `poll`, `epoll`, or `kqueue` would support more simultaneous connections.

The store uses lock striping instead of a single global mutex. This improves concurrent access when keys are distributed across shards, while keeping per-key operations simple and atomic. Multi-key commands such as `MGET` intentionally call single-key read paths rather than trying to acquire many shard locks at once.

AOF persistence serializes each state-changing command and its log append under one commit lock. The current implementation does not include AOF rewrite/compaction; a rewrite would compact the current store into a temporary AOF and atomically replace the old file.

## Known Limitations

- Blocking client handlers mean active connections occupy worker threads until completion or timeout.
- AOF rewrite/compaction is not implemented, so overwritten or deleted keys remain in the log.
- Relative `EXPIRE` commands are replayed relative to restart time rather than original write time.
- The command set is intentionally small and does not include hashes, sorted sets, pub/sub, replication, or clustering.
