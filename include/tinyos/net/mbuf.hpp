#pragma once

#include "tinyos/memory/pool_allocator.hpp"

#include <cstddef>
#include <cstdint>
#include <new>

namespace tinyos::net {

inline constexpr std::size_t kMbufPayloadSize = 2048;
inline constexpr std::size_t kMbufAlignment = 64;

// A packet buffer. Fixed size so it can live in a pool, and cache-line aligned
// so two mbufs in flight on different cores never share a line.
//
// tx_tick rides inside the buffer rather than alongside it: the timestamp
// travels with the packet, so the consumer can compute one-way latency with no
// side channel and no extra synchronisation.
struct alignas(kMbufAlignment) Mbuf {
    std::uint64_t tx_tick;                 // producer's rdtick() at handoff
    std::uint32_t len;                     // valid bytes in payload
    std::uint8_t payload[kMbufPayloadSize];
};

static_assert(sizeof(Mbuf) % kMbufAlignment == 0,
              "Mbuf must be a whole number of cache lines, or adjacent mbufs share one");

// Owns the backing store for a fixed set of mbufs and translates between the
// u32 handles that travel through the rings and real Mbuf pointers.
//
// Why handles and not pointers: it mirrors how a real NIC descriptor ring
// works, halves the bytes moving through the ring, and keeps the buffers
// relocatable. Everything downstream passes u32 around and comes here to
// resolve it.
//
// Threading: acquire()/release() go through the pool's free list, which is
// single-threaded. Once buffers are cycling between two threads they must be
// handed back over a free ring instead -- see docs/ROADMAP.md section 6.
// at() and index_of() are pure address arithmetic and safe from any thread.
class MbufPool {
public:
    explicit MbufPool(std::size_t count)
        : bytes_(count * sizeof(Mbuf)),
          storage_(static_cast<std::byte*>(
              ::operator new(count * sizeof(Mbuf), std::align_val_t{kMbufAlignment}))),
          pool_(storage_, bytes_, sizeof(Mbuf), kMbufAlignment),
          count_(count) {}

    ~MbufPool() { ::operator delete(storage_, std::align_val_t{kMbufAlignment}); }

    MbufPool(MbufPool const&) = delete;
    MbufPool& operator=(MbufPool const&) = delete;

    // Handle -> pointer. No bounds check on the hot path; callers only ever
    // resolve handles that came out of a ring, and those came from index_of().
    [[nodiscard]] Mbuf* at(std::uint32_t index) {
        return reinterpret_cast<Mbuf*>(storage_) + index;
    }

    [[nodiscard]] Mbuf const* at(std::uint32_t index) const {
        return reinterpret_cast<Mbuf const*>(storage_) + index;
    }

    // Pointer -> handle.
    [[nodiscard]] std::uint32_t index_of(Mbuf const* mbuf) const {
        return static_cast<std::uint32_t>(mbuf - reinterpret_cast<Mbuf const*>(storage_));
    }

    // Startup-time allocation. Use these to prime the free ring, then stop
    // touching the pool and let the rings do the recycling.
    [[nodiscard]] Mbuf* acquire() {
        return static_cast<Mbuf*>(pool_.allocate(sizeof(Mbuf), kMbufAlignment));
    }

    void release(Mbuf* mbuf) { pool_.deallocate(mbuf, sizeof(Mbuf)); }

    [[nodiscard]] std::size_t count() const { return count_; }

private:
    // Declaration order matters: pool_ is constructed from storage_ and bytes_.
    std::size_t bytes_;
    std::byte* storage_;
    PoolAllocator pool_;
    std::size_t count_;
};

}  // namespace tinyos::net