# Build order

Ordered by interview yield. Each module is self-contained — finish and commit
one before starting the next.

For every module the loop is the same:

1. Write the header — decide the interface before the implementation.
2. Implement it.
3. Register it in the conformance test, make it pass.
4. Register it in the benchmark, look at the p99.9.
5. Write down *why* the numbers came out that way. This is the part that
   actually pays off in an interview.

---

## 1. Allocators

The highest-yield module, and the harness is already built for it. Implement
all four — the comparison between them is the interesting result, not any one
in isolation.

Interface: `include/tinyos/memory/allocator.hpp` (already written).
Files: `include/tinyos/memory/<name>_allocator.hpp` + `src/memory/<name>_allocator.cpp`.

### 1a. Bump / arena

Pointer-bump on allocate, no-op on deallocate, O(1) reset.

- Align the *offset*, not the pointer — keeps the arithmetic in integers and
  avoids forming out-of-range pointers (UB).
- Check capacity as `bytes > capacity - aligned`, not `aligned + bytes >
  capacity`. The latter overflows on a huge request.
- `supports_free = false` in the conformance suite.

Expect the fastest numbers in the project and a completely flat tail. Be ready
to say why: no search, no branching on block state, no metadata.

### 1b. Pool / slab

Fixed-size blocks, intrusive singly-linked free list.

- The free-list node lives *inside* the free block's own storage — zero
  metadata overhead. This trick is the point of the exercise.
- Minimum block size is `sizeof(FreeNode)`.
- Push/pop at the head, giving LIFO reuse: the just-freed block is still hot
  in cache.
- `reset()` should rebuild the list in ascending address order.

Then the experiment worth running: after heavy random churn, the free list
threads through memory in random order and every pop is a cache miss. Benchmark
sequential-order versus shuffled-order free lists and watch p99 move. That
result is a genuinely good interview story.

### 1c. Free-list (general purpose)

Variable sizes, block headers, splitting and coalescing.

- Implement **both** first-fit and best-fit, selectable, and benchmark them
  against each other. First-fit is faster per call; best-fit fragments less.
  Showing you measured the tradeoff beats asserting it.
- Coalescing needs a boundary tag (size duplicated at the block's end) so a
  freed block can find its left neighbour in O(1).
- Watch alignment of the payload, not the header.

This is where fragmentation appears in the churn benchmark. Expect a visibly
worse tail than the pool — explain why.

### 1d. Buddy

Power-of-two splitting, O(log n) allocate and free, merges buddies on free.

- Buddy address is `block ^ block_size` — one XOR, worth understanding.
- Internal fragmentation is up to 2x. Measure it and report it.

**Interview material from this module:** why fixed-size pools dominate in
order-entry paths; internal vs external fragmentation; why allocation on a hot
path is banned and what you do instead; why `p99.9` and not `mean`.

---

## 2. Lock-free SPSC ring buffer

The single most commonly asked HFT data structure. Do not skip it.

Files: `include/tinyos/ipc/spsc_ring.hpp` (header-only template), plus a test
and a benchmark.

- Power-of-two capacity so wraparound is a mask, not a modulo.
- Producer index and consumer index **on separate cache lines**. Without this,
  every producer write invalidates the consumer's line and throughput collapses.
  `alignas(64)` on each, and understand that 64 is the x86 line size.
- `std::atomic` with explicit ordering: `memory_order_acquire` on the load of
  the other side's index, `memory_order_release` on the store of your own.
  Be able to explain why `seq_cst` is unnecessary here and what it would cost.
- Cache the opposite index locally to avoid re-reading a contended line on
  every operation. This optimisation is a strong signal in an interview.

**The experiment that makes this project memorable:** benchmark it with the two
indices padded to separate cache lines, then deliberately pack them into the
same line. Report both numbers. That is false sharing, demonstrated rather than
recited, and it is a top-tier answer to "tell me about a performance problem
you've debugged."

Run producer and consumer on separate threads and measure round-trip latency
percentiles, not just throughput.

---

## 3. Spinlocks and contention

Files: `include/tinyos/sync/`.

Implement in this order, benchmarking each against the last under increasing
thread counts:

1. Naive test-and-set (`exchange` in a loop)
2. Test-and-test-and-set (read-only spin until it looks free)
3. Ticket lock (FIFO fairness)
4. MCS lock (each waiter spins on its *own* cache line)

The progression is the story: each one fixes a specific cache-coherence
pathology in the previous. Add exponential backoff with a CPU pause hint
(`__builtin_ia32_pause()` on x86, `__yield()` / `isb` on ARM) and measure it.

Report fairness as well as throughput — a ticket lock is slower and more
predictable, and knowing which you want is the actual skill.

---

## 4. Page replacement

Files: `include/tinyos/vm/`.

Pure simulation over a synthetic reference stream — no real paging needed.

- FIFO, LRU, CLOCK (second-chance), and optimal (Bélády) as an upper bound.
- Demonstrate Bélády's anomaly with FIFO: more frames, more faults. It is a
  great thing to have actually reproduced.
- Compare against optimal to show how close each heuristic gets.

Metric here is hit rate across working-set sizes, not latency. Lower yield for
HFT specifically, but strong classic-OS material and cheap to build once the
harness exists.

---

## 5. Scheduler

Files: `include/tinyos/sched/`.

Round-robin, priority with aging, and MLFQ. Simulated tasks are fine —
context switching in assembly is a lot of work for little interview return.

Measure scheduling latency and fairness under mixed CPU-bound and IO-bound
loads. Show starvation with naive priority, then fix it with aging.

---

# What to record as you go

Keep a short results file per module: the numbers, the machine, and your
explanation of the shape. Interviewers ask "what did you measure and what
surprised you?" — having a real answer with real percentiles puts you well
ahead of a candidate describing a project from memory.

A caveat to state honestly if it comes up: this is developed on ARM (Apple
silicon), while trading systems run x86-64. Cache line size and the memory
model both differ — ARM is weakly ordered, x86 is TSO. Code that is correct on
ARM will be correct on x86; code that only works on x86 may break on ARM.
Knowing that distinction is itself worth points.