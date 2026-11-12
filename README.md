# HeartCeat

A simple implementation of [heartbeat scheduling](https://www.andrew.cmu.edu/user/mrainey/heartbeat/heartbeat.html) in C.

> [!note]
> This implementation is for educational purposes only. It should not be relied upon for serious projects.

## Overview

This project implements a minimal heartbeat scheduling runtime, as described by [Acar et al.](https://www.andrew.cmu.edu/user/mrainey/heartbeat/heartbeat.html).
Heartbeat scheduling is a technique for controlling thread creation overhead.
The main idea is to limit thread creation to periodic _heartbeats_.
Doing so guarantees that the overhead of thread creation is amortized by a fixed amount of serial work.

HeartCeat implements a simple [work stealing](https://en.wikipedia.org/wiki/Work_stealing) scheduler.
The heartbeat algorithm is built on top of this scheduler.
To compare heartbeat scheduling with vanilla [fork-join](https://en.wikipedia.org/wiki/Fork%E2%80%93join_model) parallelism, we also provide an implementation of the latter.

## Results

WIP

## Project structure

> [!note]
> _Workers_ are permanently assigned to OS threads.
> _Threads_ are lightweight units of work created and executed by workers, and managed by the scheduler.

Core library:

- `src/work_stealing.{h,c}`: a work stealing scheduler. Defines the thread abstraction (`job`) and exposes a `push/pop` API for workers to manage their threads.
- `src/forkjoin.{h,c}`: a simple fork-join parallel runtime, allowing the client program to create, `fork`, and `join` tasks.
- `src/heartbeat.{h,c}`: does the same for the heartbeat runtime.
- `src/heartbeat_loops.{h,c}`: specializes the heartbeat scheduler for parallel loops, which allow for special treatment.

Benchmarking harness:

- `src/harness.{h,c}`: testing harness. Handles command line arguments, loads test cases, runs kernels, times and displays results. Written by Claude.
- `src/kernels.{h,c}`: define various kernels using the fork-join and heartbeat APIs.
- `src/loops.{h,c}`, `src/tree.{h,c}`, `src/types.h`, `src/util.h`: utilities for generating test cases; type defs for common types.

## Running

Build with `make`.
`clang` is required.
The Makefile's linker flags are macOS-specific.
The binary is `build/main`.

```sh
make
./build/main                                       # run all kernels on tests/cases.txt
./build/main -l                                    # list kernels and cases
./build/main -t perfect,random -k heartbeat -p 4   # selected cases and kernels, 4 threads
./build/main -g 'perfect 20'                       # ad-hoc tree instead of a cases file
./build/main -f tests/vadd.txt -b serial_vadd      # vector add, speedups relative to serial_vadd
make run ARGS="-r 10 -H 500"                       # pass arguments through make
```

Options:

- `-f FILE`: test cases file (default `tests/cases.txt`).
- `-t A,B,...`: only run these cases.
- `-g SPEC`: run an ad-hoc case instead of the cases file (repeatable).
- `-k A,B,...`: kernels to run (default all).
- `-b NAME`: baseline kernel for speedups (default `serial`).
- `-r N`: timed repetitions per kernel, after one warmup run (default 5).
- `-p N`: number of worker threads (default: online cores).
- `-H N`: heartbeat interval, counted in calls to `beat()` (default 1000).
