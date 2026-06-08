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

## Initial Command Set

```text
PING
ECHO value
SET key value
GET key
DEL key
EXPIRE key seconds
TTL key
```
