#pragma once

// RFC section 8: "Third-party and project-local ArcoBASIC extensions must declare capabilities.
// Fissure should deny undeclared privileged operations." This is real parsing of the packet's own
// example manifest shape (section 8), not parsed-and-ignored -- vm.cpp's host contracts actually
// check the parsed capability set before performing any privileged operation. See vm.hpp's own
// comment for the one disclosed scoping compromise this enforcement makes at M1.

#include <set>
#include <string>

namespace fissure {

struct Manifest {
    int version = 0;
    std::string name;
    std::string type;
    std::set<std::string> capabilities;
    bool present = false; // false if the source had no #FISSURE-PLUGIN header at all
};

// Scans the leading comment lines of `source` (ArcoBASIC line comments start with `'`) for
// `#FISSURE-PLUGIN`, `#NAME`, `#TYPE`, and (repeatable) `#REQUIRES` directives, stopping at the
// first non-comment, non-blank line. A script with no `#FISSURE-PLUGIN` header parses to a
// Manifest with `present == false` and an empty capability set -- it is not granted any
// capability implicitly.
Manifest parse_manifest(const std::string& source);

} // namespace fissure
