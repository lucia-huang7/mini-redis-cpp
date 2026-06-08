# Persistence

Mini Redis uses an append-only file for persistence. Mutating commands are encoded as RESP arrays and appended to the AOF after they execute successfully.

Recorded commands:

- `SET`
- `DEL`
- `EXPIRE`

Read-only commands such as `GET`, `TTL`, `PING`, and `ECHO` are not recorded.

## Durability Policy

The server supports three AOF sync policies:

```bash
--appendfsync always
--appendfsync everysec
--appendfsync no
```

`always` calls `fsync` after each appended command. This is the strongest durability mode and the slowest write path.

`everysec` calls `fsync` at most once per second. This is the default. It reduces write overhead but can lose up to about one second of AOF data if the process or machine crashes.

`no` writes to the operating system page cache and does not call `fsync`. This is the fastest mode and provides the weakest durability guarantee.

## Replay

On startup, the server replays the AOF command by command. Replay is streaming: the file is read in chunks rather than loaded into memory as one large string.

If replay reaches an incomplete or corrupt tail, the AOF is truncated to the last complete command that replayed successfully. This handles common crash cases where the process exits during an append.

## TTL Semantics

The current AOF records `EXPIRE key seconds` as a relative TTL command. On replay, that relative TTL starts from replay time, not from the original write time.

This keeps the command set small, but it is not identical to Redis' production-grade expiration persistence. A stronger design would persist absolute expiration timestamps, for example with an internal `EXPIREAT` representation.

## Compaction

AOF rewrite/compaction is not implemented yet. The file can grow because old overwritten values and deleted keys remain in the log.

The intended next step is a rewrite pass that snapshots the current store into a compact temporary AOF and atomically replaces the old file.
