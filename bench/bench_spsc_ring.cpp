// SPSC ring benchmarks.
//
// Three questions:
//   1. What does false sharing actually cost?      (padded vs packed)
//   2. What does batching actually buy?            (1 vs 8 vs 32 per call)
//   3. What is the end-to-end round-trip latency?  (ping-pong, percentiles)

#include "tinyos/histogram.hpp"
#include "tinyos/ipc/spsc_ring.hpp"
#include "tinyos/timing.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <thread>

namespace {

using tinyos::Histogram;

// ---------------------------------------------------------------------------
// A deliberately broken twin of SPSC: identical logic, but head_ and tail_ sit
// on the SAME cache line. This is the control for the false-sharing experiment.
//
// Nothing here is "wrong" in a correctness sense -- it produces identical
// results. It is only slower, and the whole point is to measure by how much.
// ---------------------------------------------------------------------------
template <typename T>
class PackedSPSC {
public:
    explicit PackedSPSC(std::size_t capacity)
        : capacity_(capacity), mask_(capacity - 1), cached_tail_(0), cached_head_(0) {
        buffer_ = new T[capacity];
    }
    ~PackedSPSC() { delete[] buffer_; }
    PackedSPSC(PackedSPSC const&) = delete;
    PackedSPSC& operator=(PackedSPSC const&) = delete;

    bool try_produce(T const& item) {
        std::size_t const head = head_.load(std::memory_order_relaxed);
        if (head - cached_tail_ == capacity_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (head - cached_tail_ == capacity_) {
                return false;
            }
        }
        buffer_[head & mask_] = item;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    T* try_peek() {
        std::size_t const tail = tail_.load(std::memory_order_relaxed);
        if (tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (tail == cached_head_) {
                return nullptr;
            }
        }
        return &buffer_[tail & mask_];
    }

    bool try_consume() {
        std::size_t const tail = tail_.load(std::memory_order_relaxed);
        if (tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (tail == cached_head_) {
                return false;
            }
        }
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

private:
    T* buffer_;
    std::size_t capacity_;
    std::size_t mask_;

    // No alignas, no separation: both indices and both cached values land in
    // one 64-byte line. Every producer store to head_ invalidates that line in
    // the consumer's cache, so the consumer's next read of its OWN tail_ misses.
    std::atomic<std::size_t> head_;
    std::size_t cached_tail_;
    std::atomic<std::size_t> tail_;
    std::size_t cached_head_;
};

constexpr int kItems = 20'000'000;
constexpr std::size_t kCapacity = 1024;

// Throughput: producer and consumer on separate threads, single-item ops.
template <typename Ring>
double run_throughput(char const* label) {
    Ring ring(kCapacity);

    auto const start = tinyos::rdtick_serialised();

    std::thread producer([&] {
        for (int i = 0; i < kItems; ++i) {
            while (!ring.try_produce(i)) {
            }
        }
    });

    std::thread consumer([&] {
        for (int i = 0; i < kItems; ++i) {
            while (ring.try_peek() == nullptr) {
            }
            ring.try_consume();
        }
    });

    producer.join();
    consumer.join();

    auto const end = tinyos::rdtick_serialised();
    double const ns = tinyos::ticks_to_ns(end - start);
    double const ns_per_item = ns / kItems;

    std::printf("%-28s %10.2f ns/item %12.1f M items/s\n", label, ns_per_item,
                kItems / (ns / 1000.0));
    return ns_per_item;
}

// Throughput with batched transfers.
void run_batched(std::size_t batch) {
    tinyos::SPSC<int> ring(kCapacity);

    auto const start = tinyos::rdtick_serialised();

    std::thread producer([&] {
        std::vector<int> buf(batch);
        int next = 0;
        while (next < kItems) {
            std::size_t const want =
                std::min(batch, static_cast<std::size_t>(kItems - next));
            for (std::size_t i = 0; i < want; ++i) {
                buf[i] = next + static_cast<int>(i);
            }
            std::size_t sent = 0;
            while (sent < want) {
                sent += ring.produce_n(buf.data() + sent, want - sent);
            }
            next += static_cast<int>(want);
        }
    });

    std::thread consumer([&] {
        std::vector<int> buf(batch);
        int seen = 0;
        while (seen < kItems) {
            seen += static_cast<int>(ring.consume_n(buf.data(), batch));
        }
    });

    producer.join();
    consumer.join();

    auto const end = tinyos::rdtick_serialised();
    double const ns = tinyos::ticks_to_ns(end - start);

    char label[64];
    std::snprintf(label, sizeof(label), "batch = %zu", batch);
    std::printf("%-28s %10.2f ns/item %12.1f M items/s\n", label, ns / kItems,
                kItems / (ns / 1000.0));
}

// Round-trip latency: ping down one ring, pong back on another. Measures the
// full cross-core handoff, which is what actually matters for a request path.
void run_ping_pong() {
    constexpr int kRounds = 200'000;
    tinyos::SPSC<int> to_peer(1024);
    tinyos::SPSC<int> from_peer(1024);

    std::atomic<bool> stop{false};

    std::thread peer([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            int* p = to_peer.try_peek();
            if (p != nullptr) {
                int const v = *p;
                to_peer.try_consume();
                while (!from_peer.try_produce(v)) {
                }
            }
        }
    });

    Histogram hist("spsc ping-pong rtt");
    hist.reserve(kRounds);

    for (int i = 0; i < kRounds; ++i) {
        auto const t0 = tinyos::rdtick_serialised();

        while (!to_peer.try_produce(i)) {
        }
        while (from_peer.try_peek() == nullptr) {
        }
        from_peer.try_consume();

        auto const t1 = tinyos::rdtick_serialised();
        hist.add(tinyos::ticks_to_ns(t1 - t0));
    }

    stop.store(true, std::memory_order_relaxed);
    peer.join();

    hist.print_row();
}

}  // namespace

int main() {
    std::printf("tick frequency: %llu Hz\n\n",
                static_cast<unsigned long long>(tinyos::tick_frequency()));

    std::printf("--- false sharing: padded vs packed indices ---\n");
    std::printf("(cache line on this machine: %zu bytes)\n", tinyos::kCacheLineSize);
    double const padded = run_throughput<tinyos::SPSC<int>>("padded (own lines)");
    double const packed = run_throughput<PackedSPSC<int>>("packed (shared line)");
    std::printf("%-28s %10.2fx slower when packed\n\n", "cost of false sharing",
                packed / padded);

    std::printf("--- batching (padded ring) ---\n");
    run_batched(1);
    run_batched(8);
    run_batched(32);
    run_batched(128);
    std::printf("\n");

    std::printf("--- round-trip latency ---\n");
    Histogram::print_header();
    run_ping_pong();

    return 0;
}
