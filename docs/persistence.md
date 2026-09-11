# Persistence

Mini Redis uses an append-only file for persistence. Mutating commands are encoded as RESP arrays and appended to the AOF after they execute successfully.

All mutating commands pass through one commit lock. The Store mutation and its AOF append therefore have the same total order, even when multiple workers repeatedly write the same key. A reply is not sent until the append has completed.

Recorded commands are normalized before append:

- `SET` (`EX`/`PX` are converted to `PXAT` with an absolute Unix-millisecond deadline)
- `DEL`
- `PEXPIREAT` (`EXPIRE` is converted to this absolute form)
- `INCR`
- `DECR`

Read-only commands such as `GET`, `MGET`, `EXISTS`, `TTL`, `PING`, and `ECHO`
are not recorded.

Conditional writes are recorded only when they change state. For example,
`SET key value NX` is appended only if the key did not already exist and the
server returned `+OK`.

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

## AOF Failure Semantics

If an AOF append or sync fails, the command is not acknowledged as successful. Because its in-memory mutation may already have occurred, the server enters a protective write-disabled state: reads remain available, but every later write returns `MISCONF` until restart. This prevents subsequent writes from extending a state that cannot be reconstructed from the log.

The integration suite repeatedly writes one shared key from multiple clients, captures its final in-memory value, restarts the server, and verifies that replay produces the identical value.

Unit tests inject partial writes, `ENOSPC`, and `fsync` errors through the AOF I/O boundary. The production path still defaults directly to the operating-system `write` and `fsync` calls; the hooks exist only to make failure semantics deterministic and testable.

## Replay

On startup, the server replays the AOF command by command. Replay is streaming: the file is read in chunks rather than loaded into memory as one large string.

If replay reaches an incomplete or corrupt tail, the AOF is truncated to the last complete command that replayed successfully. This handles common crash cases where the process exits during an append.

## TTL Semantics

The in-memory store uses `std::chrono::steady_clock` deadlines, so normal expiration is monotonic and is not affected by wall-clock adjustments. The durable representation uses Unix epoch milliseconds because a monotonic clock value is not meaningful across process restarts.

Before a TTL-bearing mutation executes, the commit path computes one absolute deadline. `EXPIRE` is executed and recorded as `PEXPIREAT`; `SET EX` and `SET PX` are executed and recorded as `SET PXAT`. The same deadline therefore governs both the live mutation and its AOF record. During replay, an absolute deadline is converted back to a monotonic remaining duration. A deadline that passed while the server was offline expires immediately instead of receiving a fresh TTL.

`EXPIREAT`, `PEXPIREAT`, and the `SET EXAT`/`PXAT` options are also accepted directly. Legacy AOF entries containing relative `EXPIRE`, `SET EX`, or `SET PX` remain readable, although an old entry cannot recover downtime that was never encoded in that file.

## Compaction

AOF rewrite/compaction is not implemented yet. The file can grow because old overwritten values and deleted keys remain in the log.

The intended next step is a rewrite pass that snapshots the current store into a compact temporary AOF and atomically replaces the old file.
