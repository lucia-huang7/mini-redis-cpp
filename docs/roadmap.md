# Roadmap

This project is intentionally scoped as a clear, inspectable Redis-like server. The next improvements would focus on production-style performance, durability, and observability.

## Near-Term Improvements

- Add AOF rewrite/compaction so overwritten or deleted keys do not grow the log forever.
- Persist absolute expiration timestamps instead of relative `EXPIRE` commands during AOF replay.
- Add structured server logs for accepted connections, command errors, replay results, and shutdown.
- Expand benchmark scenarios to include mixed read/write workloads, larger values, and AOF sync-policy comparisons.
- Add memory usage reporting and keyspace statistics.

## Larger Design Upgrades

- Replace blocking per-client worker handling with a `poll`, `epoll`, or `kqueue` event loop.
- Add snapshot persistence in addition to AOF.
- Add background AOF writing or batching to reduce write-path latency under durable modes.
- Add a small compatibility test suite against selected Redis command behavior.

## Current Non-Goals

- Redis Cluster support
- Replication
- Lua scripting
- Streams, sorted sets, hashes, or pub/sub
- Production authentication and ACL support
