// Conformance suite for anything implementing tinyos::Allocator.
//
// This is the spec for what you are about to write. Point it at your allocator
// and it will tell you whether the thing actually behaves like an allocator --
// alignment honoured, no overlapping blocks, graceful exhaustion, stats sane.
//
// TO USE: scroll to the bottom, include your header, and register it.

#include "tinyos/memory/allocator.hpp"
#include "tinyos/test.hpp"

#include <cstring>
#include <vector>

namespace {

using tinyos::Allocator;

struct Block {
    void* ptr;
    std::size_t size;
    unsigned char fill;
};

// Every allocator must pass all of this, whatever its internal strategy.
// `supports_free` is false for arena-style allocators that only reclaim on
// reset() -- they are exempt from the reuse checks, nothing else.
void run_conformance(Allocator& alloc, bool supports_free) {
    // --- alignment -------------------------------------------------------
    // A pointer that is not suitably aligned is undefined behaviour waiting to
    // happen, and on x86 it silently costs you a split cache line.
    for (std::size_t align = 1; align <= 64; align *= 2) {
        void* p = alloc.allocate(align * 2, align);
        if (p != nullptr) {
            auto const addr = reinterpret_cast<std::uintptr_t>(p);
            CHECK((addr % align) == 0);
        }
    }
    alloc.reset();

    // --- no overlap, and writes do not corrupt neighbours -----------------
    // The classic allocator bug: an off-by-one in size or alignment maths
    // hands out two blocks that share a byte. Fill each block with a distinct
    // pattern, then verify every pattern survived.
    std::vector<Block> live;
    unsigned char fill = 1;
    for (int i = 0; i < 64; ++i) {
        std::size_t const size = 16 + static_cast<std::size_t>(i % 8) * 8;
        void* p = alloc.allocate(size, alignof(std::max_align_t));
        if (p == nullptr) {
            break;
        }
        std::memset(p, fill, size);
        live.push_back({p, size, fill});
        ++fill;
        if (fill == 0) {
            fill = 1;
        }
    }
    REQUIRE(!live.empty());

    for (auto const& b : live) {
        auto const* bytes = static_cast<unsigned char const*>(b.ptr);
        for (std::size_t i = 0; i < b.size; ++i) {
            if (bytes[i] != b.fill) {
                CHECK(bytes[i] == b.fill);
                break;  // one report per block is enough
            }
        }
    }

    // --- stats accounting -------------------------------------------------
    CHECK(alloc.stats().allocations >= live.size());

    // --- exhaustion returns nullptr, never crashes or overruns -------------
    // Predictable failure matters more than it sounds: a hot path that must
    // not throw needs to know it can ask and be told no.
    std::size_t guard = 0;
    while (alloc.allocate(64, alignof(std::max_align_t)) != nullptr && guard < 10'000'000) {
        ++guard;
    }
    CHECK(guard < 10'000'000);
    CHECK(alloc.stats().failed_allocations > 0);

    // --- reset reclaims everything ----------------------------------------
    alloc.reset();
    void* after_reset = alloc.allocate(64, alignof(std::max_align_t));
    CHECK(after_reset != nullptr);

    // --- free then reallocate ---------------------------------------------
    if (supports_free) {
        alloc.reset();
        void* a = alloc.allocate(64, alignof(std::max_align_t));
        REQUIRE(a != nullptr);
        alloc.deallocate(a, 64);
        void* b = alloc.allocate(64, alignof(std::max_align_t));
        CHECK(b != nullptr);
        // Most designs reuse the just-freed block (LIFO free list). Not
        // required, but if it never reuses, the allocator leaks by design.
        alloc.deallocate(b, 64);
    }

    alloc.reset();
}

}  // namespace

// ---------------------------------------------------------------------------
// REGISTERED ALLOCATORS
//
// Each gets its own buffer. They are static so a 1 MiB arena does not blow the
// stack, and alignas(64) so the pool can hand out cache-line-aligned blocks.
// ---------------------------------------------------------------------------

#include "tinyos/memory/bump_allocator.hpp"
#include "tinyos/memory/pool_allocator.hpp"

TEST("bump allocator conformance") {
    alignas(64) static std::byte buffer[1 << 20];
    tinyos::BumpAllocator alloc(buffer, sizeof(buffer));
    run_conformance(alloc, /*supports_free=*/false);
}

TEST("pool allocator conformance") {
    // 64-byte blocks aligned to 64: the suite requests alignments up to 64, and
    // a pool can only honour what its block alignment guarantees.
    alignas(64) static std::byte buffer[1 << 20];
    tinyos::PoolAllocator alloc(buffer, sizeof(buffer), 64, 64);
    run_conformance(alloc, /*supports_free=*/true);
}

int main() {
    if (tinyos::test::registry().empty()) {
        std::printf("no allocators registered yet -- see the bottom of %s\n", __FILE__);
        return 0;
    }
    return tinyos::test::run_all();
}