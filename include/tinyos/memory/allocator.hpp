#pragma once

#include <cstddef>
#include <cstdint>

namespace tinyos {

// Common interface so the benchmark harness can drive every allocator through
// one code path and compare them fairly.
//
// Worth knowing why this is a design compromise: a virtual call costs an
// indirect branch the predictor may miss, which is real money when the
// allocation itself is ~20 ns. Production low-latency allocators are chosen at
// compile time via templates or CRTP so the call inlines away. The vtable here
// buys apples-to-apples comparison, and it costs every allocator equally.
class Allocator {
public:
    struct Stats {
        std::size_t bytes_in_use = 0;      // currently handed out to callers
        std::size_t bytes_reserved = 0;    // total the allocator owns
        std::size_t allocations = 0;       // successful allocate() calls
        std::size_t deallocations = 0;
        std::size_t failed_allocations = 0;
    };

    virtual ~Allocator() = default;

    Allocator() = default;
    Allocator(Allocator const&) = delete;
    Allocator& operator=(Allocator const&) = delete;

    // Returns nullptr when the request cannot be satisfied. Never throws --
    // the whole point is predictable behaviour under memory pressure.
    // `alignment` must be a power of two.
    virtual void* allocate(std::size_t bytes,
                           std::size_t alignment = alignof(std::max_align_t)) = 0;

    // `bytes` is the size originally requested. Passing it in is what lets an
    // allocator skip storing a per-block header -- the same trick C++17's
    // sized delete uses, and a good thing to be able to explain.
    virtual void deallocate(void* ptr, std::size_t bytes) = 0;

    // Release everything at once. O(1) for arena-style allocators, which is
    // their main selling point.
    virtual void reset() = 0;

    [[nodiscard]] virtual char const* name() const = 0;
    [[nodiscard]] Stats const& stats() const { return stats_; }

protected:
    Stats stats_{};
};

// Round `n` up to the next multiple of `alignment` (power of two).
constexpr std::size_t align_up(std::size_t n, std::size_t alignment) {
    return (n + alignment - 1) & ~(alignment - 1);
}

constexpr bool is_power_of_two(std::size_t n) {
    return n != 0 && (n & (n - 1)) == 0;
}

inline constexpr std::size_t kCacheLineSize = 64;

}  // namespace tinyos