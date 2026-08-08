// Benchmark harness for anything implementing tinyos::Allocator.
//
// Three workloads, because allocators that look identical on one look very
// different on another:
//
//   sequential  -- allocate until full. Measures the pure fast path.
//   churn       -- steady-state alloc/free mix. Measures free-list quality and
//                  is where fragmentation shows up.
//   random size -- mixed sizes. Punishes designs that assume uniformity.
//
// TO USE: scroll to the bottom, include your header, and register it.

#include "tinyos/histogram.hpp"
#include "tinyos/memory/allocator.hpp"
#include "tinyos/timing.hpp"

#include <cstddef>
#include <cstdio>
#include <random>
#include <vector>

namespace {

using tinyos::Allocator;
using tinyos::Histogram;

// Operations timed per batch.
//
// This is not arbitrary. A serialised counter read costs ~17 ns on this
// machine, which is the same order as the allocation being measured -- time
// one operation and you mostly measure the clock. Timing a batch of 64 and
// dividing amortises the read down to ~0.3 ns per op. Each batch is still one
// histogram sample, so tail percentiles stay meaningful.
constexpr std::size_t kOpsPerBatch = 64;
constexpr std::size_t kBatches = 4096;

// --- workload 1: allocate until exhausted, then reset ----------------------
void bench_sequential(Allocator& alloc, std::size_t block_size, Histogram& hist) {
    for (std::size_t batch = 0; batch < kBatches; ++batch) {
        alloc.reset();

        auto const start = tinyos::rdtick_serialised();
        for (std::size_t i = 0; i < kOpsPerBatch; ++i) {
            void* p = alloc.allocate(block_size, alignof(std::max_align_t));
            tinyos::do_not_optimise(p);
        }
        auto const end = tinyos::rdtick_serialised();

        hist.add(tinyos::ticks_to_ns(end - start) / static_cast<double>(kOpsPerBatch));
    }
}

// --- workload 2: steady-state alloc/free churn -----------------------------
void bench_churn(Allocator& alloc, std::size_t block_size, Histogram& hist) {
    alloc.reset();

    // Fill to roughly half capacity first, so we measure steady state rather
    // than the artificially clean behaviour of a fresh allocator.
    std::vector<void*> live;
    live.reserve(1024);
    for (std::size_t i = 0; i < 512; ++i) {
        void* p = alloc.allocate(block_size, alignof(std::max_align_t));
        if (p == nullptr) {
            break;
        }
        live.push_back(p);
    }
    if (live.empty()) {
        return;
    }

    std::mt19937 rng(12345);  // fixed seed: benchmarks must be reproducible

    for (std::size_t batch = 0; batch < kBatches; ++batch) {
        auto const start = tinyos::rdtick_serialised();
        for (std::size_t i = 0; i < kOpsPerBatch; ++i) {
            // Free a random live block, then allocate a replacement. Random
            // choice is deliberate -- freeing in LIFO order flatters the
            // allocator and hides fragmentation.
            std::size_t const victim = rng() % live.size();
            alloc.deallocate(live[victim], block_size);
            void* p = alloc.allocate(block_size, alignof(std::max_align_t));
            live[victim] = p;
            tinyos::do_not_optimise(p);
        }
        auto const end = tinyos::rdtick_serialised();

        hist.add(tinyos::ticks_to_ns(end - start) / static_cast<double>(kOpsPerBatch));
    }
}

// --- workload 3: mixed sizes ----------------------------------------------
void bench_random_size(Allocator& alloc, Histogram& hist) {
    std::mt19937 rng(67890);
    std::uniform_int_distribution<std::size_t> size_dist(16, 512);

    for (std::size_t batch = 0; batch < kBatches; ++batch) {
        alloc.reset();

        auto const start = tinyos::rdtick_serialised();
        for (std::size_t i = 0; i < kOpsPerBatch; ++i) {
            void* p = alloc.allocate(size_dist(rng), alignof(std::max_align_t));
            tinyos::do_not_optimise(p);
        }
        auto const end = tinyos::rdtick_serialised();

        hist.add(tinyos::ticks_to_ns(end - start) / static_cast<double>(kOpsPerBatch));
    }
}

// Runs all three workloads and prints one row each.
void bench_all(Allocator& alloc, std::size_t block_size, bool supports_free) {
    {
        Histogram h(std::string(alloc.name()) + " / sequential");
        h.reserve(kBatches);
        bench_sequential(alloc, block_size, h);
        h.print_row();
    }
    if (supports_free) {
        Histogram h(std::string(alloc.name()) + " / churn");
        h.reserve(kBatches);
        bench_churn(alloc, block_size, h);
        h.print_row();
    }
    {
        Histogram h(std::string(alloc.name()) + " / random size");
        h.reserve(kBatches);
        bench_random_size(alloc, h);
        h.print_row();
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// REGISTER YOUR ALLOCATORS HERE
//
//   #include "tinyos/memory/bump_allocator.hpp"
//
//   alignas(64) static std::byte g_buffer[1 << 22];   // 4 MiB
//
//   ... then inside main():
//   {
//       tinyos::BumpAllocator alloc(g_buffer, sizeof(g_buffer));
//       bench_all(alloc, 64, /*supports_free=*/false);
//   }
// ---------------------------------------------------------------------------

int main() {
    std::printf("tick frequency: %llu Hz (%.2f ns/tick)\n\n",
                static_cast<unsigned long long>(tinyos::tick_frequency()),
                1e9 / static_cast<double>(tinyos::tick_frequency()));

    Histogram::print_header();

    // <-- register allocators here; see the comment block above.
    std::printf("(no allocators registered yet -- see the bottom of %s)\n", __FILE__);

    std::printf("\nall figures are ns per operation\n");
    return 0;
}