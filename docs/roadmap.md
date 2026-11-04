# Roadmap

This project is intentionally scoped as a clear, inspectable Redis-like server.

## Done

- Event-loop networking (`epoll` on Linux, `kqueue` on macOS/BSD, `poll` fallback) with per-connection output backpressure, replacing the one-worker-per-connection model.
- Lock-free write path when AOF is disabled; AOF-backed writes keep one commit order.
- Absolute expiration deadlines in the AOF (`EXPIRE` and `SET EX/PX` are logged as `PEXPIREAT` and `SET PXAT`).
- Sampled active expiration over a per-shard TTL index instead of a periodic full keyspace scan.
- AOF rewrite: `BGREWRITEAOF` plus automatic rewrite on growth, with writes continuing during the snapshot.

## Near-Term Improvements

- Inline command support (`PING\r\n`), so `redis-benchmark -t ping` and `redis-cli --pipe` work end to end.
- Graceful shutdown on `SIGTERM`/`SIGINT` with a final AOF `fsync`.
- Structured logs and an `INFO`-style command for connections, keyspace size, expiry, and rewrite statistics.
- Mixed read/write benchmark scenarios, larger values, and AOF sync-policy comparisons.

## Larger Design Upgrades

- Snapshot (RDB-style) persistence alongside the AOF.
- Group commit for `appendfsync always` so concurrent writers share one `fsync`.
- A compatibility test suite that runs the same command scripts against Mini Redis and Redis.

## Current Non-Goals

- Redis Cluster support
- Replication
- Lua scripting
- Streams, sorted sets, hashes, or pub/sub
- Production authentication and ACL support
