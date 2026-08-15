// Correctness tests for SPSC<T>.
//
// Single-threaded tests pin down the index arithmetic (wrap-around, full vs
// empty). The threaded test is the one that would catch a memory-ordering bug:
// it checks that every item arrives exactly once, in order.

#include "tinyos/ipc/spsc_ring.hpp"
#include "tinyos/test.hpp"

#include <stdexcept>
#include <thread>
#include <vector>

using tinyos::SPSC;

TEST("capacity must be a power of two") {
    bool threw = false;
    try {
        SPSC<int> bad(100);
    } catch (std::invalid_argument const&) {
        threw = true;
    }
    CHECK(threw);
}

TEST("fill to capacity then reject") {
    SPSC<int> ring(8);
    CHECK(ring.empty());
    CHECK_EQ(ring.capacity(), std::size_t{8});

    for (int i = 0; i < 8; ++i) {
        CHECK(ring.try_produce(i));
    }

    CHECK(ring.full());
    CHECK_EQ(ring.size(), std::size_t{8});
    CHECK(!ring.try_produce(99));   // full: must refuse, not overwrite
}

TEST("drains in FIFO order") {
    SPSC<int> ring(8);
    for (int i = 0; i < 8; ++i) {
        ring.try_produce(i * 7);
    }

    for (int i = 0; i < 8; ++i) {
        int* p = ring.try_peek();
        REQUIRE(p != nullptr);
        CHECK_EQ(*p, i * 7);
        CHECK(ring.try_consume());
    }

    CHECK(ring.empty());
    CHECK(ring.try_peek() == nullptr);
    CHECK(!ring.try_consume());
}

TEST("wraps around correctly") {
    // 100 items through 8 slots: forces the index masking to be exercised
    // repeatedly, which is where off-by-one bugs live.
    SPSC<int> ring(8);
    for (int i = 0; i < 100; ++i) {
        REQUIRE(ring.try_produce(i));
        int* p = ring.try_peek();
        REQUIRE(p != nullptr);
        CHECK_EQ(*p, i);
        REQUIRE(ring.try_consume());
    }
    CHECK(ring.empty());
}

TEST("full and empty are distinguishable after wrapping") {
    // The classic bug: with plain wrapped indices, full and empty both look
    // like head == tail. Monotonic counters avoid it -- verify that.
    SPSC<int> ring(4);
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 4; ++i) {
            REQUIRE(ring.try_produce(i));
        }
        CHECK(ring.full());
        CHECK(!ring.empty());

        for (int i = 0; i < 4; ++i) {
            REQUIRE(ring.try_consume());
        }
        CHECK(ring.empty());
        CHECK(!ring.full());
    }
}

TEST("batch produce and consume") {
    SPSC<int> ring(8);

    int in[16];
    for (int i = 0; i < 16; ++i) {
        in[i] = i * 10;
    }

    // Asking for more than fits must return a partial count, not overflow.
    CHECK_EQ(ring.produce_n(in, 16), std::size_t{8});

    int out[16] = {};
    CHECK_EQ(ring.consume_n(out, 16), std::size_t{8});

    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(out[i], i * 10);
    }
    CHECK(ring.empty());
}

TEST("batch across a wrap boundary") {
    SPSC<int> ring(8);
    int in[8];
    for (int i = 0; i < 8; ++i) {
        in[i] = i;
    }

    // Offset the indices so the next batch straddles the end of the buffer.
    ring.produce_n(in, 5);
    int drain[5];
    ring.consume_n(drain, 5);

    ring.produce_n(in, 8);
    int out[8] = {};
    CHECK_EQ(ring.consume_n(out, 8), std::size_t{8});
    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(out[i], i);
    }
}

TEST("threaded: every item arrives exactly once, in order") {
    // The real test. A missing release/acquire pair shows up here as a value
    // that is stale or out of sequence -- usually only under -O2 on a weakly
    // ordered CPU, which is exactly what this machine is.
    constexpr int kCount = 1'000'000;
    SPSC<int> ring(1024);

    std::thread producer([&] {
        for (int i = 0; i < kCount; ++i) {
            while (!ring.try_produce(i)) {
                // spin: ring full, consumer will drain it
            }
        }
    });

    bool ordered = true;
    long long sum = 0;

    std::thread consumer([&] {
        for (int expected = 0; expected < kCount; ++expected) {
            int* p = nullptr;
            while ((p = ring.try_peek()) == nullptr) {
                // spin: ring empty
            }
            if (*p != expected) {
                ordered = false;
            }
            sum += *p;
            ring.try_consume();
        }
    });

    producer.join();
    consumer.join();

    CHECK(ordered);
    CHECK_EQ(sum, static_cast<long long>(kCount) * (kCount - 1) / 2);
    CHECK(ring.empty());
}

TEST("threaded: batched transfer") {
    constexpr int kCount = 1'000'000;
    constexpr std::size_t kBatch = 32;
    SPSC<int> ring(1024);

    std::thread producer([&] {
        int buf[kBatch];
        int next = 0;
        while (next < kCount) {
            std::size_t const want =
                std::min(kBatch, static_cast<std::size_t>(kCount - next));
            for (std::size_t i = 0; i < want; ++i) {
                buf[i] = next + static_cast<int>(i);
            }
            std::size_t sent = 0;
            while (sent < want) {
                sent += ring.produce_n(buf + sent, want - sent);
            }
            next += static_cast<int>(want);
        }
    });

    bool ordered = true;

    std::thread consumer([&] {
        int buf[kBatch];
        int expected = 0;
        while (expected < kCount) {
            std::size_t const got = ring.consume_n(buf, kBatch);
            for (std::size_t i = 0; i < got; ++i) {
                if (buf[i] != expected++) {
                    ordered = false;
                }
            }
        }
    });

    producer.join();
    consumer.join();

    CHECK(ordered);
    CHECK(ring.empty());
}

int main() {
    return tinyos::test::run_all();
}
