#include "tinyos/timing.hpp"

#include <chrono>
#include <thread>

namespace tinyos {

namespace {

std::uint64_t measure_frequency() {
#if defined(__aarch64__)
    // ARM exposes the counter frequency directly -- no calibration needed.
    std::uint64_t freq;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq;
#else
    // x86 has no architectural way to ask, so calibrate the TSC against a
    // known-good clock. 50 ms is long enough to swamp scheduling jitter.
    using clock = std::chrono::steady_clock;

    auto const wall_start = clock::now();
    auto const tick_start = rdtick_serialised();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto const tick_end = rdtick_serialised();
    auto const wall_end = clock::now();

    auto const elapsed_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(wall_end - wall_start).count();

    return (tick_end - tick_start) * 1'000'000'000ULL / static_cast<std::uint64_t>(elapsed_ns);
#endif
}

}  // namespace

std::uint64_t tick_frequency() {
    // Calibration is not free, so pay for it once.
    static std::uint64_t const freq = measure_frequency();
    return freq;
}

}  // namespace tinyos