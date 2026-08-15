#include "arco/random.hpp"

#include <atomic>
#include <chrono>
#include <random>
#include <stdexcept>

namespace arco {

namespace {

std::uint32_t rotate_right_32(std::uint32_t value, std::uint32_t rotation) {
    rotation &= 31U;
    return static_cast<std::uint32_t>((value >> rotation) | (value << ((0U - rotation) & 31U)));
}

} // namespace

Pcg32::Pcg32(std::uint64_t seed, std::uint64_t sequence) { reseed(seed, sequence); }

void Pcg32::reseed(std::uint64_t seed, std::uint64_t sequence) {
    state_ = 0;
    increment_ = (sequence << 1U) | 1U;
    (void)next_u32();
    state_ += seed;
    (void)next_u32();
}

std::uint32_t Pcg32::next_u32() {
    const std::uint64_t old_state = state_;
    state_ = old_state * UINT64_C(6364136223846793005) + increment_;
    const auto xorshifted = static_cast<std::uint32_t>(((old_state >> 18U) ^ old_state) >> 27U);
    const auto rotation = static_cast<std::uint32_t>(old_state >> 59U);
    return rotate_right_32(xorshifted, rotation);
}

std::uint32_t Pcg32::bounded(std::uint64_t bound) {
    if (bound == 0 || bound > (UINT64_C(1) << 32U)) {
        throw std::invalid_argument("PCG32 bound must be between 1 and 2^32");
    }
    if (bound == (UINT64_C(1) << 32U)) {
        return next_u32();
    }
    const auto width = static_cast<std::uint32_t>(bound);
    const std::uint32_t threshold = static_cast<std::uint32_t>(0U - width) % width;
    while (true) {
        const std::uint32_t value = next_u32();
        if (value >= threshold) {
            return value % width;
        }
    }
}

double Pcg32::unit_interval() {
    const std::uint32_t high = next_u32() >> 5U;
    const std::uint32_t low = next_u32() >> 6U;
    return (static_cast<double>(high) * 67108864.0 + static_cast<double>(low)) /
           9007199254740992.0;
}

std::uint64_t automatic_random_seed() {
    static std::atomic<std::uint64_t> counter{0};
    const auto wall = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto monotonic = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::uint64_t entropy = 0;
    try {
        std::random_device device;
        entropy = (static_cast<std::uint64_t>(device()) << 32U) ^ static_cast<std::uint64_t>(device());
    } catch (...) {
        // RFC-0021 permits transient clock/process-local fallback state for this non-cryptographic
        // service. The counter separates Runtime instances created within one clock tick.
    }
    const std::uint64_t serial = counter.fetch_add(1, std::memory_order_relaxed);
    return entropy ^ wall ^ (monotonic + UINT64_C(0x9e3779b97f4a7c15) * (serial + 1U));
}

} // namespace arco
