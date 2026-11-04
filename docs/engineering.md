# Engineering Notes

The codebase keeps the network, protocol, command, storage, expiration, and persistence layers separate.

This project uses a small set of C++ patterns deliberately:

- RAII wrappers own file descriptors and sockets so close happens on scope exit.
- Each `EventLoop` owns its thread, `Poller`, wakeup pipe, and connections, and joins its thread on destruction.
- Shared server state is explicit: client handlers share `CommandDispatcher`, `Store`, and `Aof` by reference.
- `Store` uses lock striping: each shard owns its map and `std::shared_mutex`.
- `Aof` protects append and replay operations with a mutex.
- `std::optional` represents missing keys and parse failures without sentinel values.
- AOF durability is configured through an explicit sync policy.
- `GET` and `TTL` use shared locks for the common read path.
- `INCR` and `DECR` update values atomically and keep an existing TTL.
- Conditional `SET` commands are appended to AOF only when they actually change state, and are logged without `NX`/`XX`.
- AOF records are blind writes or absolute deadlines, which is what makes online rewrite safe.

## Network Model

The server uses N level-triggered event loops over non-blocking sockets. An accept thread enforces the connection limit and distributes sockets round-robin. Each loop reads, executes requests inline, and writes replies; a 1 MiB per-connection output high-water mark pauses reading for clients that do not drain their replies.

The previous design dedicated one blocking worker to each connection. With 16 workers, the default `redis-benchmark` (50 clients) stalled: the first 16 connections could not finish because their share of requests depended on the other 34, which waited in the admission queue until the 30 s idle timeout. The integration suite now runs 64 interleaved clients against 2 loops as a regression test.

The `Poller` interface hides `epoll`, `kqueue`, and `poll`. `kqueue` filters are registered on first use and then toggled with `EV_ENABLE`/`EV_DISABLE`, and changes use `EV_RECEIPT` so each change reports its own error. CI exercises `kqueue` on a macOS runner; `libkqueue` on Linux is only suitable as a compile check, because it does not report write readiness on a socket that also has a read filter.

## Store Concurrency

The store is partitioned into shards by key hash. Each shard has its own `unordered_map` and `std::shared_mutex`.

This keeps per-key commands simple: a command only locks the shard for its key. Read-heavy commands take shared locks, while mutations take exclusive locks. Background expiration samples each shard's TTL index in rounds of 20 keys, so no lock is held for a full shard scan and the per-tick cost is bounded by a time budget.

## Benchmarking

The benchmark client supports concurrent clients, request pipelining, and configurable keyspace size.

Pipelining helps separate server throughput from single-request network round-trip behavior. Keyspace size helps reveal whether the store is bottlenecked on a small set of hot keys or distributing work across shards.

## Tooling

Linux CI includes Release, ASan/UBSan, ThreadSanitizer, `poll`-backend, and bounded libFuzzer jobs. A macOS job builds and tests the `kqueue` backend. `docs/scripts/render_diagrams.py` regenerates the README diagrams. The repository also includes a `.clang-tidy` profile for local static analysis.

## Test Coverage

The unit tests cover RESP encoding/parsing, command semantics, store behavior, TTL index bookkeeping, active expiry and its time budget, AOF replay, blind-write normalization, rewrite under concurrent writers, automatic rewrite, partial writes, and injected I/O failures. The integration tests start a real server process and exercise TCP requests, pipelining, split and hostile RESP packets, concurrent same-key writes, 64 clients over 2 event loops, output backpressure, idle clients, connection limits, TTL expiry, error responses, `BGREWRITEAOF`, and restart-time AOF verification.
