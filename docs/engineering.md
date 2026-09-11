# Engineering Notes

The codebase keeps the network, protocol, command, storage, expiration, and persistence layers separate.

This project uses a small set of C++ patterns deliberately:

- RAII wrappers own file descriptors and sockets so close happens on scope exit.
- `ThreadPool` owns worker threads and joins them in its destructor.
- Shared server state is explicit: client handlers share `CommandDispatcher`, `Store`, and `Aof` by reference.
- `Store` uses lock striping: each shard owns its map and `std::shared_mutex`.
- `Aof` protects append and replay operations with a mutex.
- `std::optional` represents missing keys and parse failures without sentinel values.
- AOF durability is configured through an explicit sync policy.
- `GET` and `TTL` use shared locks for the common read path.
- `INCR` and `DECR` update values atomically and keep an existing TTL.
- Conditional `SET` commands are appended to AOF only when they actually change state.

## Network Model

The server uses blocking sockets with a fixed worker pool. The accept loop enqueues each client connection as a task, and workers run the blocking client loop. Admission is bounded, and socket timeouts release workers held by inactive clients.

Compared with `poll`, `epoll`, or `kqueue`, this model has a smaller implementation surface but dedicates one worker to each active connection.

## Store Concurrency

The store is partitioned into shards by key hash. Each shard has its own `unordered_map` and `std::shared_mutex`.

This keeps per-key commands simple: a command only locks the shard for its key. Read-heavy commands take shared locks, while mutations take exclusive locks. Background expiration scans shards one at a time rather than blocking the entire store behind one global lock.

## Benchmarking

The benchmark client supports concurrent clients, request pipelining, and configurable keyspace size.

Pipelining helps separate server throughput from single-request network round-trip behavior. Keyspace size helps reveal whether the store is bottlenecked on a small set of hot keys or distributing work across shards.

## Tooling

Linux CI includes Release, ASan/UBSan, ThreadSanitizer, and bounded libFuzzer jobs. The repository also includes a `.clang-tidy` profile for local static analysis.

## Test Coverage

The unit tests cover RESP encoding/parsing, command semantics, store behavior, thread-pool execution, TTL cleanup, AOF replay, partial writes, and injected I/O failures. The integration tests start a real server process and exercise TCP requests, pipelining, split and hostile RESP packets, concurrent same-key writes, idle clients, queue exhaustion, TTL expiry, error responses, and restart-time AOF verification.
