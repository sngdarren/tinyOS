// Single-threaded proof that the mbuf handle cycle works, before any threads
// are involved. Catches the two bugs that are miserable to debug later:
// a wrong index <-> address mapping, and an off-by-one in ring sizing that
// deadlocks only once a NIC thread exists.

#include "tinyos/ipc/spsc_ring.hpp"
#include "tinyos/net/mbuf.hpp"
#include "tinyos/test.hpp"

#include <cstdint>

using tinyos::SPSC;
using tinyos::net::Mbuf;
using tinyos::net::MbufPool;

TEST("mbuf is cache-line sized and aligned") {
    // Checked against the platform's real line size, not a hardcoded 64 --
    // this machine's is 128, and asserting 64 would pass while adjacent mbufs
    // silently shared a line.
    CHECK_EQ(alignof(Mbuf), tinyos::kCacheLineSize);
    CHECK_EQ(sizeof(Mbuf) % tinyos::kCacheLineSize, std::size_t{0});
}

TEST("index and pointer round-trip") {
    MbufPool pool(64);
    for (std::uint32_t i = 0; i < 64; ++i) {
        CHECK_EQ(pool.index_of(pool.at(i)), i);
    }
}

TEST("distinct handles give distinct, non-overlapping buffers") {
    MbufPool pool(16);
    for (std::uint32_t i = 0; i + 1 < 16; ++i) {
        auto const* a = reinterpret_cast<std::byte const*>(pool.at(i));
        auto const* b = reinterpret_cast<std::byte const*>(pool.at(i + 1));
        CHECK_EQ(static_cast<std::size_t>(b - a), sizeof(Mbuf));
    }
}

TEST("pool hands out every block, then refuses") {
    MbufPool pool(32);
    int acquired = 0;
    while (pool.acquire() != nullptr) {
        ++acquired;
        if (acquired > 1000) {
            break;  // guard against a broken pool looping forever
        }
    }
    CHECK_EQ(acquired, 32);
}

TEST("full rx / free ring cycle") {
    // The real check: buffers go pool -> free_ring -> rx_ring -> free_ring,
    // round and round, and nothing is lost or duplicated.
    constexpr std::uint32_t kMbufs = 64;
    constexpr std::size_t kRingCapacity = 128;   // must exceed kMbufs

    MbufPool pool(kMbufs);
    SPSC<std::uint32_t> rx_ring(kRingCapacity);
    SPSC<std::uint32_t> free_ring(kRingCapacity);

    // Prime: every buffer starts free.
    for (std::uint32_t i = 0; i < kMbufs; ++i) {
        Mbuf* m = pool.acquire();
        REQUIRE(m != nullptr);
        REQUIRE(free_ring.try_produce(pool.index_of(m)));
    }
    CHECK_EQ(free_ring.size(), std::size_t{kMbufs});

    // Cycle far more times than there are buffers, so handles must genuinely
    // be recycled rather than just drawn from a fresh supply.
    for (std::uint64_t iteration = 0; iteration < 10'000; ++iteration) {
        // --- producer side ---
        std::uint32_t* free_idx = free_ring.try_peek();
        REQUIRE(free_idx != nullptr);
        std::uint32_t const idx = *free_idx;
        free_ring.try_consume();

        Mbuf* m = pool.at(idx);
        m->tx_tick = iteration;
        m->len = 4;
        m->payload[0] = static_cast<std::uint8_t>(iteration & 0xFF);

        REQUIRE(rx_ring.try_produce(idx));

        // --- consumer side ---
        std::uint32_t* rx_idx = rx_ring.try_peek();
        REQUIRE(rx_idx != nullptr);
        Mbuf const* got = pool.at(*rx_idx);

        // Same handle in means same data out.
        CHECK_EQ(got->tx_tick, iteration);
        CHECK_EQ(got->payload[0], static_cast<std::uint8_t>(iteration & 0xFF));

        std::uint32_t const done = *rx_idx;
        rx_ring.try_consume();
        REQUIRE(free_ring.try_produce(done));
    }

    // Conservation: every buffer accounted for, none leaked or duplicated.
    CHECK(rx_ring.empty());
    CHECK_EQ(free_ring.size(), std::size_t{kMbufs});
}

TEST("no handle is lost across a cycle") {
    // Stronger conservation check: drain the free ring and confirm we see each
    // handle exactly once. A duplicated handle means two owners of one buffer.
    constexpr std::uint32_t kMbufs = 32;
    MbufPool pool(kMbufs);
    SPSC<std::uint32_t> free_ring(64);

    for (std::uint32_t i = 0; i < kMbufs; ++i) {
        free_ring.try_produce(pool.index_of(pool.acquire()));
    }

    // One full lap through a notional rx path.
    for (std::uint32_t i = 0; i < kMbufs; ++i) {
        std::uint32_t const idx = *free_ring.try_peek();
        free_ring.try_consume();
        free_ring.try_produce(idx);
    }

    bool seen[kMbufs] = {};
    for (std::uint32_t i = 0; i < kMbufs; ++i) {
        std::uint32_t const idx = *free_ring.try_peek();
        free_ring.try_consume();
        REQUIRE(idx < kMbufs);
        CHECK(!seen[idx]);   // duplicate handle
        seen[idx] = true;
    }

    for (std::uint32_t i = 0; i < kMbufs; ++i) {
        CHECK(seen[i]);      // missing handle
    }
}

int main() {
    return tinyos::test::run_all();
}
