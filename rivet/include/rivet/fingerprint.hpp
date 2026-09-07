#pragma once

// Real content-based fingerprinting (RFC section 12: "should not rely solely on timestamp
// comparison"). No crypto library is linked anywhere in this repository, and this isn't a
// security boundary -- it's a content-addressed cache key, so a fast non-cryptographic hash is
// the right, minimal choice. 128-bit "wide FNV-1a": two independent 64-bit FNV-1a passes (each
// with its own offset-basis/prime pair) over the same bytes, concatenated -- ~60 lines, zero new
// dependencies, ample collision headroom for a build's realistic input volume (a handful to a few
// thousand files/strings), without pretending to be cryptographically strong.

#include <cstdint>
#include <string>
#include <vector>

namespace rivet {

struct Digest {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
    std::string hex() const; // 32 hex chars
    bool operator==(const Digest& other) const { return lo == other.lo && hi == other.hi; }
    bool operator!=(const Digest& other) const { return !(*this == other); }
};

Digest hash_bytes(const void* data, std::size_t length);
Digest hash_string(const std::string& text);
// Streaming, 64 KB chunks -- throws std::runtime_error if the file can't be opened.
Digest hash_file(const std::string& path);

// Order-sensitive fold: combine({a, b}) != combine({b, a}) in general. This is deliberate -- an
// argument-order change in a BuildAction's own argument list must be able to change its
// fingerprint the same way a content change does (RFC section 12's "-O0 -> -O2" example).
Digest combine(const std::vector<Digest>& parts);

} // namespace rivet
