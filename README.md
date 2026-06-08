# Mini Redis C++

A small Redis server implementation in C++.

The codebase is organized around the main Redis components: protocol parsing,
command execution, in-memory storage, expiration, persistence, and benchmarks.

## Build

```bash
cmake -S . -B build
cmake --build build
```

Run the server:

```bash
./build/miniredis_server --port 6379
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

## Layout

```text
include/miniredis/    Public headers
src/                  Implementation files
tests/                Tests
benchmarks/           Benchmark client and scripts
docs/                 Architecture and protocol notes
examples/             redis-cli examples
data/                 Local append-only files
```
