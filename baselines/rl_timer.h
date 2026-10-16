/*
 * Cycle counter for the baselines.
 *   x86-64:  rdtsc (invariant TSC; converted to time by rl_cycles_per_us())
 *   RISC-V:  rdcycle (Ariane on ESP)
 *   other, or -DRL_TIMER_CLOCK: CLOCK_MONOTONIC in nanoseconds
 */
#ifndef RL_TIMER_H
#define RL_TIMER_H

#include <stdint.h>
#include <time.h>

#if !defined(RL_TIMER_CLOCK) && (defined(__x86_64__) || defined(__i386__))
#include <x86intrin.h>
#define RL_TIMER_NAME "rdtsc"
static inline uint64_t rl_cycles(void) { return __rdtsc(); }
static inline void rl_cpu_relax(void) { _mm_pause(); }
#elif !defined(RL_TIMER_CLOCK) && defined(__riscv)
#define RL_TIMER_NAME "rdcycle"
static inline uint64_t rl_cycles(void)
{
    uint64_t c;
    __asm__ volatile("rdcycle %0" : "=r"(c));
    return c;
}
static inline void rl_cpu_relax(void) { __asm__ volatile("nop"); }
#else
#define RL_TIMER_NAME "clock_monotonic_ns"
static inline uint64_t rl_cycles(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static inline void rl_cpu_relax(void) {}
#endif

/* Counter ticks per microsecond, measured against CLOCK_MONOTONIC (~100 ms). */
static inline double rl_cycles_per_us(void)
{
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    uint64_t c0 = rl_cycles();
    do {
        clock_gettime(CLOCK_MONOTONIC, &b);
    } while ((b.tv_sec - a.tv_sec) * 1000000000l + (b.tv_nsec - a.tv_nsec) < 100000000l);
    uint64_t c1 = rl_cycles();
    double us = (b.tv_sec - a.tv_sec) * 1e6 + (b.tv_nsec - a.tv_nsec) / 1e3;
    return (double)(c1 - c0) / us;
}

#endif
