# Onyx

A Redis-compatible in-memory key-value server, written from scratch in C++17 on raw POSIX sockets. One thread, a `poll()` event loop, the RESP2 wire protocol, and key expiry. No external libraries.

Any Redis client can talk to it: `redis-cli`, `valkey-cli`, or a client library.

## What it supports

| Command | Behaviour |
|---|---|
| `PING` | `+PONG` |
| `ECHO message` | returns `message` |
| `SET key value [EX seconds \| PX milliseconds]` | stores the value; a plain `SET` clears any existing expiry |
| `GET key` | the value, or null if the key is missing or expired |

- **RESP2 protocol** — commands arrive as arrays of bulk strings; replies are simple strings, bulk strings, null and errors
- **Binary-safe** keys and values: any bytes, including `\0` and `\r\n`
- **Partial and pipelined input** — a command split across several reads, or many commands in one read, are both handled
- **Many concurrent clients** on a single thread
- **Key expiry**, both lazy and active (see below)
- Malformed input gets `-ERR Protocol error` and the connection is closed; wrong arity, unknown commands and bad options get Redis's error replies

**Not supported:** inline (non-RESP) commands, persistence to disk, RESP3, any command not listed above (`DEL`, `EXPIRE`, `TTL`, ...), authentication. The port is fixed at 6379.

## Build and run

Requires Linux, CMake 3.16+ and a C++17 compiler.

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/onyx
```

Then, from another terminal:

```sh
valkey-cli -p 6379 SET greeting "hello world" PX 5000
valkey-cli -p 6379 GET greeting
```

A Debug build (`-DCMAKE_BUILD_TYPE=Debug`) compiles with AddressSanitizer, which stops the program on any use-after-free or out-of-bounds access and reports the lines involved. Per-request logging is behind the `DEBUG_MODE` constant in `main.cpp`.

## Design

### One thread, one event loop

The server keeps every socket — the listening socket and each client — in a list, and calls `poll()` on all of them. `poll()` waits until any one is ready and reports which. The loop then accepts a new connection or serves a ready client, and goes back to `poll()`. That is the only place the server ever waits; `accept`, `read` and `write` only run once `poll()` has said they won't block.

The first concurrent version used a thread per client (kept under the git tag `thread-per-client`). It worked, but every piece of shared state needed a lock — its log output interleaved mid-line under 50 simultaneous clients until a mutex was added — and every idle client cost a whole thread.

Redis uses a single thread for the same reasons this one does. The keyspace is shared, mutable state; with one thread every command is atomic, with no locks, races or deadlocks. And the cost of a command is dominated by moving bytes through the network stack, not by the processor (the benchmarks below show `PING`, `SET` and `GET` running at the same rate), so extra threads would buy little.

### Framing

TCP delivers a byte stream with no message boundaries: one `read()` may return half a command, or several. Each client therefore has its own input buffer. Every `read` appends to it, and a parser repeatedly tries to take one complete command off the front:

| Parser returns | Meaning | Action |
|---|---|---|
| `> 0` | one complete command, using that many bytes | handle it, erase those bytes, try again |
| `0` | incomplete | leave the buffer untouched, wait for the next read |
| `-1` | not valid RESP | reply with an error and close the connection |

Bulk strings are length-prefixed, so the parser takes exactly that many bytes rather than searching for a delimiter — which is what makes values binary-safe. Numbers in the protocol are parsed by hand, rejecting non-digits and anything over 18 digits (so they cannot overflow a 64-bit integer), rather than with `std::stoi`, which would throw on bad input and take the whole server down.

A client's buffer is erased when it disconnects, so a later client reusing the same file descriptor never inherits leftover bytes.

### Replies

All replies produced by one read are appended to a single string and sent with one `write()`. Client sockets also set `TCP_NODELAY`. If a write fails or is short, the client is disconnected: without an output buffer the unsent bytes would be lost and every later reply misaligned.

### Keyspace and expiry

The keyspace is one `std::unordered_map<std::string, std::string>`, owned by the event loop and shared by all clients. Deadlines live in a second map, holding only keys that can expire. They are absolute times on `std::chrono::steady_clock`, a monotonic clock — the wall clock can be corrected backwards or forwards, which would make keys live too long or die early.

Expiry works the way Redis's does:

- **Lazy:** reading a key whose deadline has passed deletes it and returns null. This guarantees no client ever sees an expired key, but on its own never frees keys that nobody reads again.
- **Active:** every 100 ms (`poll()` has a 100 ms timeout so the loop never sleeps longer), the server samples 20 keys from random hash-table buckets and deletes the expired ones. If more than 5 of the 20 had expired, the table probably holds many more, so it samples again — within a 25 ms budget, so clients are never starved.

## Performance

Measured on an AMD Ryzen 7 4800H with 16 GB RAM, Arch Linux. Onyx Release build (GCC 16, `-O3`) against Valkey 9.1.2 with persistence disabled (`--save "" --appendonly no`), run one after the other on the same machine. `valkey-benchmark`, 1,000,000 requests per test, 50 clients, 3-byte values, 100,000 random keys (`-r 100000`), over loopback, with the benchmark running on the same machine as the server.

**One request in flight per client:**

| | Onyx | p99 | Valkey | p99 |
|---|---|---|---|---|
| PING | 67,824 req/s | 0.455 ms | 66,662 req/s | 0.463 ms |
| SET | 69,682 req/s | 0.447 ms | 66,733 req/s | 0.487 ms |
| GET | 67,413 req/s | 0.495 ms | 67,590 req/s | 0.503 ms |

**Pipelined, 16 requests in flight per client (`-P 16`):**

| | Onyx | p99 | Valkey | p99 |
|---|---|---|---|---|
| PING | 1,013,171 req/s | 0.607 ms | 1,010,101 req/s | 0.615 ms |
| SET | 965,251 req/s | 1.391 ms | 699,301 req/s | 1.503 ms |
| GET | 922,509 req/s | 1.631 ms | 856,898 req/s | 1.199 ms |

Without pipelining the two servers are indistinguishable: each client waits for every reply, so both are bound by the round trip rather than by their own work. With pipelining, Onyx's throughput is comparable to Valkey's. This is not a claim that Onyx is faster than Valkey — Valkey does considerably more per command (statistics, memory limits, keyspace notifications, incremental rehashing, access control), and its tail latency on pipelined `GET` is lower.

**A bug the benchmark found.** The first pipelined run reached only ~18,000 requests/s — four times *slower* than without pipelining. Timing a single client showed every batch taking ~43 ms regardless of its size. The server was writing each reply separately: TCP's Nagle algorithm held back each small write until the previous one was acknowledged, while the client delayed its acknowledgement (up to ~40 ms on Linux) waiting for the rest of the batch. Batching each read's replies into one `write()` and setting `TCP_NODELAY` took pipelined throughput from ~18,000 to ~1,000,000 requests/s.

## Limitations and next steps

- **Output buffering** — keep unsent reply bytes per client and finish sending when `poll()` reports `POLLOUT`, instead of disconnecting on a short write
- **Inline commands**, so plain-text clients such as `telnet` work
- **Persistence** — RDB snapshots
- More commands: `DEL`, `EXPIRE`, `TTL`, `EXISTS`
- A configurable port
