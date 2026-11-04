# Persistence

Mini Redis uses an append-only file for persistence. Mutating commands are encoded as RESP arrays and appended to the AOF after they execute successfully.

When the AOF is enabled, all mutating commands pass through one commit lock. The Store mutation and its AOF append therefore have the same total order, even when multiple workers repeatedly write the same key. A reply is not sent until the append has completed. When the AOF is disabled there is no log order to protect, so writes skip the commit lock and rely only on per-shard locks.

Recorded commands are normalized before append so that every record is a blind write or an absolute deadline:

- `SET` (`EX`/`PX` are converted to `PXAT` with an absolute Unix-millisecond deadline; `NX`/`XX` are dropped because only applied writes are logged)
- `DEL`
- `PEXPIREAT` (`EXPIRE` is converted to this absolute form) and `EXPIREAT`
- `INCR`/`DECR` are logged as `SET key <result>`, plus `PXAT <deadline>` when the key has a TTL

Older AOF files that contain `INCR`, `DECR`, or conditional `SET` records still replay correctly; only new records use the normalized form.

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

## Rewrite (Compaction)

Overwritten values and deleted keys stay in the log until a rewrite replaces it with one record per live key. A rewrite starts on `BGREWRITEAOF` or automatically once the file is at least `--auto-aof-rewrite-min-size` bytes (default 64 MiB, `0` disables) and has grown by `--auto-aof-rewrite-percentage` (default 100%) since the previous rewrite or startup.

The rewrite runs in a background thread and does not fork:

1. Under the commit lock, mark a rewrite in progress and start capturing every newly committed record into a delta buffer. Normal appends to the current AOF continue.
2. Without the commit lock, copy one shard at a time under that shard's shared lock and write `SET key value [PXAT deadline]` for each live key into `appendonly.aof.rewrite.tmp`, then `fsync` it.
3. Under the commit lock, append the delta buffer, `fsync`, `rename` the temporary file over the AOF, `fsync` the directory, and reopen the AOF for appending.

Writers are blocked only during step 3. If any step before the rename fails, the temporary file is removed and the original AOF is untouched. If the rename succeeds but reopening fails, the server enters the same write-disabled `MISCONF` state as any other AOF failure rather than append to an unlinked file.

### Why concurrent writes are safe

A shard copy in step 2 can include some of the writes that are also in the delta buffer, so replay applies those writes twice. That is harmless because every record is a blind write or an absolute deadline. For one key, `SET` and `DEL` set the final state regardless of what came before, and `PEXPIREAT` only changes the deadline of an existing key. Replaying the full delta sequence over any state that the same sequence passed through therefore ends in the same state as replaying it over the state before the rewrite began. This is the reason `INCR` and conditional `SET` are logged in their resulting form: replaying `INCR` twice would not be harmless.

The unit suite rewrites five times while four threads mix `SET`, `DEL`, and `INCR`, then checks that replaying the final file reproduces the live store exactly. The integration suite issues `BGREWRITEAOF` against a running server, keeps writing, restarts, and verifies values and TTLs.
