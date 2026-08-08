# tinyOS

OS subsystems implemented and benchmarked in modern C++20 — allocators,
lock-free queues, schedulers, page replacement. Hosted rather than bare metal:
the goal is the algorithms and their latency behaviour, not boot plumbing.

Every subsystem ships with a conformance test and a latency benchmark
reporting p50/p99/p99.9, because in this domain the mean is the least
interesting number.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
./build/bench_allocators
```

New files under `src/`, `tests/test_*.cpp`, and `bench/bench_*.cpp` are picked
up automatically — no need to edit `CMakeLists.txt`.

## Layout

```
include/tinyos/
  timing.hpp       cycle counter, ns conversion, optimiser barriers
  histogram.hpp    latency percentiles
  test.hpp         minimal test framework (no external deps)
  memory/
    allocator.hpp  the Allocator interface
src/               implementations
tests/             conformance suites
bench/             latency benchmarks
docs/ROADMAP.md    what to build, in order
```

## Measurement notes

The benchmark harness times **batches of 64 operations**, not single ones. A
serialised counter read costs ~17 ns on Apple silicon — the same order as the
operation being measured — so per-op timing would mostly measure the clock.

`tinyos::do_not_optimise()` prevents the compiler deleting work whose result is
unused. Without it a microbenchmark will cheerfully report 0 ns.

## Status

Infrastructure is done and verified. Subsystems are unimplemented — see
`docs/ROADMAP.md`.