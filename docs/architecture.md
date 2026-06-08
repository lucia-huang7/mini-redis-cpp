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

First version: one thread per connection.

Later version: acceptor thread plus worker thread pool.

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

Planned commands:

- `PING`
- `ECHO`
- `SET`
- `GET`
- `DEL`
- `EXPIRE`
- `TTL`

### Store

The in-memory key-value engine.

The first implementation uses:

```cpp
std::unordered_map<std::string, Value>
std::shared_mutex
```

Reads use shared locks. Writes use exclusive locks.

### TTL Manager

Expiration has two layers:

- Lazy expiration during reads and writes
- Background cleanup for expired keys

### AOF Persistence

Append-only file persistence records mutating commands:

- `SET`
- `DEL`
- `EXPIRE`

On startup, the server replays the AOF file to rebuild the in-memory store.

## Milestones

1. RESP parser and encoder
2. `PING` and `ECHO` over TCP
3. `SET`, `GET`, and `DEL`
4. `EXPIRE` and `TTL`
5. AOF append and replay
6. Concurrent clients
7. Benchmarks and CI
