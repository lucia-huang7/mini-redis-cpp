# Architecture

Mini Redis C++ is organized as a small set of replaceable modules:

```text
Client
  -> TCP Server
  -> Connection Handler
  -> RESP Parser
  -> Command Dispatcher
  -> Store / TTL Manager
  -> AOF Logger
```

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

`SET` supports Redis-style `EX`, `PX`, `NX`, and `XX` options. The dispatcher
keeps parsing and arity validation separate from store mutation so command
semantics are testable without a network server.

### Store

The in-memory key-value engine.

The current implementation uses:

```cpp
std::unordered_map<std::string, Value>
std::shared_mutex
```

Read-heavy operations such as `GET` and `TTL` first take a shared lock. If they
observe an expired key, they release the read lock and clean it up through a
short exclusive path. Mutating operations such as `SET`, `DEL`, `EXPIRE`, and
`INCR` take an exclusive lock for atomicity.

### TTL Manager

Expiration has two layers:

- Lazy expiration during reads and writes
- Background cleanup for expired keys

### AOF Persistence

Append-only file persistence records mutating commands:

- `SET`
- `DEL`
- `EXPIRE`
- `INCR`
- `DECR`

On startup, the server replays the AOF file to rebuild the in-memory store. The AOF file remains open during runtime, supports configurable `fsync` policy, and truncates incomplete/corrupt tails during replay.

See `docs/persistence.md` for durability policy and TTL persistence tradeoffs.

## Current Capabilities

- RESP parser and encoder
- TCP server compatible with `redis-cli`
- `PING`, `ECHO`, `SET`, `GET`, `MGET`, `EXISTS`, `DEL`, `EXPIRE`, `TTL`, `INCR`, and `DECR`
- Thread-safe in-memory store
- Lazy expiration and background TTL cleanup
- AOF append and startup replay
- Fixed worker pool for client handling
- Single-process benchmark client with multi-client mode
