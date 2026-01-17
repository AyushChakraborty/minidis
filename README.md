# Redis like Key-Value Store

A lightweight, non-blocking key-value store server implemented in C++ from scratch. This project implements high-performance network programming using an event-driven architecture and a custom intrusive hash table with progressive resizing.

## Features

* Event-Driven Architecture: Uses a custom event loop with poll() for non-blocking I/O handling.

* Custom Binary Protocol: Efficient Type-Length-Value (TLV) serialization protocol supporting Strings, Integers, Doubles, and Arrays.

* Intrusive Hash Table: Implements a custom hashtable using intrusive linked lists to minimize memory overhead.

* Progressive Resizing: Eliminates latency spikes by resizing the hash table incrementally across operations instead of "stopping the world."

* Pipelining Support: The server handles multiple requests in a single read/write cycle for improved throughput.

## Technical Details

### Architecture
The server uses a single-threaded event loop (poll) to manage client connections. State is maintained per connection (struct Conn) with separate read/write buffers to handle partial packet transmission.

### Data Protocol
Communication is binary and little-endian. Format: `[Tag (1B)] [Length (4B)] [Value]` Supported Tags: `NIL`, `ERR`, `STR`, `INT`, `DBL`, `ARR`.

### Hash Table Design
* Chaining: Uses intrusive nodes embedded directly in the `Entry` struct.

* Resizing: When the load factor exceeds 8.0, a new table (2x size) is allocated.

* Migration: Keys are moved from the old table to the new one incrementally during every `lookup` and `insert` operation, ensuring predictable latency.

## Getting Started

### Prerequisites

* Linux/Unix environment (uses `<sys/socket.h>`, `<arpa/inet.h>`, `<sys/poll.h>`)
* g++ (C++11 or higher)

### Build & Run

1) Compile the Server and Client:
```bash
g++ -O2 -Wall -Wextra -std=c++11 server.cpp hashtable.cpp -o server
g++ -O2 -Wall -Wextra -std=c++11 client.cpp -o client
```

2) Start the Server:
```bash
./server
```

3) Run the Client:
```bash
./client set mykey "Hello World"
./client get mykey
./client del mykey
./client keys
```

## Acknowledgements

This project is based on the [Build Your Own Redis](https://build-your-own.org/redis/) guide by James Smith. The core architecture and protocols follow the book's specifications, with additional personal implementations and refactoring.