#pragma once

#include <cstdint>

#if defined(__x86_64__)
#include <x86intrin.h>
#endif

namespace tinyos {

// A monotonic hardware counter read, in platform ticks.
//
//   x86-64  -- rdtsc, one tick per reference cycle.
//   aarch64 -- cntvct_el0. Measured on this machine: 1 GHz, i.e. 1 ns/tick,
//              monotonic. (cntfrq_el0 reports the rate; do not hardcode it,
//              other Apple parts have run this counter at 24 MHz.)
//
// NOTE on inline asm: the instructions below are separated with "\n\t", not
// ";". On AArch64 the semicolon does not reliably separate instructions and
// the second one gets silently dropped -- which leaves the output variable
// uninitialised and produces plausible-looking garbage timings. This cost a
// debugging session; leave it as is.
inline std::uint64_t rdtick() {
#if defined(__x86_64__)
    return __rdtsc();
#elif defined(__aarch64__)
    std::uint64_t v;
    asm volatile("mrs %0, cntvct_el0" : "=r"(v)::"memory");
    return v;
#else
#error "unsupported architecture"
#endif
}

// Serialising variant: stops the CPU reordering the counter read across the
// work being measured. Costs more (~17 ns on this machine, vs ~2 ns for the
// plain read) but without it an out-of-order core will happily hoist the
// second read above the code you are trying to time.
inline std::uint64_t rdtick_serialised() {
#if defined(__x86_64__)
    unsigned aux;
    return __rdtscp(&aux);
#elif defined(__aarch64__)
    std::uint64_t v;
    asm volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(v)::"memory");
    return v;
#else
#error "unsupported architecture"
#endif
}

// Ticks per second, so results can be reported in nanoseconds.
std::uint64_t tick_frequency();

inline double ticks_to_ns(std::uint64_t ticks) {
    return static_cast<double>(ticks) * 1e9 / static_cast<double>(tick_frequency());
}

// Stops the optimiser deleting work whose result is never used -- the single
// most common way a microbenchmark silently measures nothing.
template <typename T>
inline void do_not_optimise(T const& value) {
    asm volatile("" : : "r,m"(value) : "memory");
}

inline void clobber_memory() {
    asm volatile("" : : : "memory");
}

}  // namespace tinyos