#include "rivet/fingerprint.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace rivet {

namespace {

// Two independent FNV-1a parameter sets (the standard 64-bit offset/prime, and a second pair
// derived the same way FNV's own authors describe generating alternates) -- run over the same
// bytes to get 128 bits of spread instead of relying on one 64-bit pass alone.
constexpr std::uint64_t kOffsetA = 0xcbf29ce484222325ULL;
constexpr std::uint64_t kPrimeA = 0x100000001b3ULL;
constexpr std::uint64_t kOffsetB = 0x9e3779b97f4a7c15ULL; // golden-ratio-derived, distinct from A
constexpr std::uint64_t kPrimeB = 0x00000100000001b3ULL ^ 0xff51afd7ed558ccdULL;

std::uint64_t fnv1a(const void* data, std::size_t length, std::uint64_t offset, std::uint64_t prime) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint64_t hash = offset;
    for (std::size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= prime;
    }
    return hash;
}

} // namespace

std::string Digest::hex() const {
    std::ostringstream out;
    out << std::hex << std::uppercase;
    auto emit = [&out](std::uint64_t value) {
        char buffer[17];
        std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
        out << buffer;
    };
    emit(hi);
    emit(lo);
    return out.str();
}

Digest hash_bytes(const void* data, std::size_t length) {
    Digest digest;
    digest.lo = fnv1a(data, length, kOffsetA, kPrimeA);
    digest.hi = fnv1a(data, length, kOffsetB, kPrimeB);
    return digest;
}

Digest hash_string(const std::string& text) {
    return hash_bytes(text.data(), text.size());
}

Digest hash_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("rivet: could not open " + path + " for hashing");

    std::uint64_t lo = kOffsetA;
    std::uint64_t hi = kOffsetB;
    std::array<char, 65536> chunk{};
    while (input) {
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        std::streamsize read = input.gcount();
        if (read <= 0) break;
        for (std::streamsize i = 0; i < read; ++i) {
            auto byte = static_cast<unsigned char>(chunk[static_cast<std::size_t>(i)]);
            lo ^= byte;
            lo *= kPrimeA;
            hi ^= byte;
            hi *= kPrimeB;
        }
    }
    Digest digest;
    digest.lo = lo;
    digest.hi = hi;
    return digest;
}

Digest combine(const std::vector<Digest>& parts) {
    // Order-sensitive: each part's hex digest is concatenated with a separator and re-hashed,
    // rather than e.g. XORed together (which would make combine({a,b}) == combine({b,a}), losing
    // exactly the argument-order sensitivity RFC section 12 needs -- see this header's own note).
    std::string joined;
    for (const auto& part : parts) {
        joined += part.hex();
        joined.push_back('\x1f');
    }
    return hash_string(joined);
}

} // namespace rivet
