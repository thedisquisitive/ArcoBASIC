#pragma once

#include <cstdint>

namespace arco {

// PCG XSH RR 64/32 as specified by RFC-0021. This engine is deliberately independent from the
// host standard library so explicitly seeded ArcoBASIC programs are reproducible everywhere.
class Pcg32 {
public:
    static constexpr std::uint64_t default_sequence = 54;

    explicit Pcg32(std::uint64_t seed, std::uint64_t sequence = default_sequence);

    void reseed(std::uint64_t seed, std::uint64_t sequence = default_sequence);
    std::uint32_t next_u32();
    std::uint32_t bounded(std::uint64_t bound);
    double unit_interval();

private:
    std::uint64_t state_ = 0;
    std::uint64_t increment_ = 1;
};

std::uint64_t automatic_random_seed();

} // namespace arco
