# OS Memory & Threading Engine

A compact C systems project that demonstrates two core operating-systems/networking ideas:

1. **Virtual memory management**: page tables, MMU-style address translation, page faults, swapping, FIFO/LRU-style eviction, and AMAT statistics.
2. **Thread-safe reliable transport**: packetization, checksums, ACK/NACK-style retransmission, synchronized queues, and sender/receiver worker threads.

This repository is intentionally written as an original portfolio implementation rather than a copy of any course starter code. It is small enough to read end-to-end but still demonstrates the systems concepts described on the resume.

## Build

```bash
make
```

## Run demos

```bash
./bin/systems_engine vm
./bin/systems_engine transport
./bin/systems_engine all
```

## What it demonstrates

- C memory management and pointer-heavy systems code
- Page-table based virtual-to-physical translation
- Page fault handling and swap restore/writeback
- FIFO and approximate-LRU frame replacement
- AMAT-style performance metrics
- POSIX threads, mutexes, condition variables
- Packet segmentation, checksums, ACK/NACK retransmission
- Thread-safe producer/consumer queues

## File layout

```text
include/
  vm.h             virtual memory API and structs
  transport.h      reliable transport API and structs
src/
  main.c           command-line demo runner
  vm.c             memory manager implementation
  transport.c      threaded transport implementation
Makefile
```
