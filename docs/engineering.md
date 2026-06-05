# Engineering Notes

This project uses a small set of C++ patterns deliberately:

- RAII wrappers own file descriptors and sockets so close happens on scope exit.
- `ThreadPool` owns worker threads and joins them in its destructor.
- Shared server state is explicit: client handlers share `CommandDispatcher`, `Store`, and `Aof` by reference.
- `Store` and `Aof` protect shared mutation with locks.
- `std::optional` represents missing keys and parse failures without sentinel values.
- AOF durability is configured through an explicit sync policy.
- `GET` and `TTL` use shared locks for the common read path.
- `INCR` and `DECR` update values atomically and keep an existing TTL.
- Conditional `SET` commands are appended to AOF only when they actually change state.

## Network Model

The server uses blocking sockets with a fixed worker pool. The accept loop enqueues each client connection as a task, and workers run the blocking client loop.

This is intentionally simpler than `poll`, `epoll`, or `kqueue`. The tradeoff is that long-lived clients occupy workers, but the design avoids unbounded detached threads and keeps ownership/lifetime straightforward.

## Tooling

CI builds Debug, Release, and ASan/UBSan configurations. The repository also includes a `.clang-tidy` profile for local static analysis.

ThreadSanitizer is not enabled in CI yet. It is a useful future addition, but it can be noisy for integration tests that fork server processes and use POSIX sockets.

## Test Coverage

The unit tests cover RESP encoding/parsing, command semantics, store behavior,
thread-pool execution, TTL cleanup, and AOF replay. The integration tests start a
real server process and exercise TCP requests, pipelining, split RESP packets,
concurrent clients, TTL expiry, error responses, and AOF replay.
