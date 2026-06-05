# RESP Protocol Notes

RESP is the Redis wire protocol. The server receives arrays of bulk strings for commands and returns typed responses.

## Example Request

```text
*2\r\n$3\r\nGET\r\n$4\r\nname\r\n
```

Meaning:

```text
["GET", "name"]
```

## Example Responses

```text
+OK\r\n
$5\r\nalice\r\n
$-1\r\n
:1\r\n
-ERR wrong number of arguments\r\n
```

## Command Set

```text
PING [message]
ECHO value
SET key value [EX seconds | PX milliseconds] [NX | XX]
GET key
MGET key [key ...]
EXISTS key [key ...]
DEL key
EXPIRE key seconds
TTL key
INCR key
DECR key
```

`MGET` returns an array of bulk strings and null bulk strings. `SET NX` and
`SET XX` return `+OK` when a write is applied and `$-1` when the condition is
not met.
