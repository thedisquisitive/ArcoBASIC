#pragma once

// Fissure's own generic vocabulary (RFC section 4.1: "The native core must understand generic
// concepts only... It must not contain privileged knowledge of C++, Python, ArcoBASIC, Rust,
// Java, or any other language."). NodeKind/EdgeKind below are the RFC's own minimum lists
// (sections 5.1/5.2) verbatim -- nothing language-specific was added.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fissure {

enum class NodeKind {
    File,
    Module,
    Package,
    Namespace,
    Type,
    Function,
    Symbol,
    BuildTarget,
    Binary,
    GeneratedArtifact,
    Resource,
    Probe,
    ExternalDependency,
};

std::string to_string(NodeKind kind);
std::optional<NodeKind> node_kind_from_string(const std::string& text);

enum class EdgeKind {
    Imports,
    Calls,
    Inherits,
    LinksTo,
    Generates,
    Consumes,
    LoadsAtRuntime,
    Reads,
    Writes,
    Executes,
    ObservedDuringProbe,
    BuildDependency,
    DeclaredTestAssociation,
};

std::string to_string(EdgeKind kind);
std::optional<EdgeKind> edge_kind_from_string(const std::string& text);

// RFC section 5.3. Evidence provenance travels with every edge so classification and `fissure
// explain` can cite exactly why a relationship exists, not just that it does.
enum class Evidence {
    Static,
    Build,
    Observed,
    Historical,
    Declared,
};

std::string to_string(Evidence evidence);
std::optional<Evidence> evidence_from_string(const std::string& text);

struct Node {
    std::string id;
    NodeKind kind = NodeKind::File;
    // Free-form key/value attributes (e.g. a File node's on-disk path, a Probe node's command).
    // Kept as strings rather than arco::Value at this layer -- the core stays value-type-agnostic
    // per RFC 4.1; the VM boundary (core/vm) is the only place arco::Value appears.
    std::map<std::string, std::string> attributes;
};

struct Edge {
    std::string from;
    std::string to;
    EdgeKind kind = EdgeKind::Reads;
    Evidence evidence = Evidence::Declared;
    // 0.0-1.0. RFC 6.2: "Confidence influences policy but must not override hard safety
    // invariants" -- tracked here for provenance/explainability, never read by the tri-state
    // classifier itself (see impact.hpp).
    double confidence = 1.0;
};

// RFC section 9. A Probe is a graph Node (kind == Probe) plus the extra execution-specific fields
// below, kept as a separate struct so the generic Node type doesn't grow probe-only fields.
struct Probe {
    std::string id;
    std::string display_name;
    std::string command;
    std::vector<std::string> tags;
};

// RFC section 6.1's own three states, verbatim. The comment there is the whole point of this
// enum: "UNKNOWN must never be silently converted to UNAFFECTED."
enum class Classification {
    Affected,
    Unaffected,
    Unknown,
};

std::string to_string(Classification value);

struct ProbeVerdict {
    std::string probe_id;
    Classification classification = Classification::Unknown;
    // Human-readable evidence trail backing this verdict -- `fissure explain` prints this
    // directly. RFC section 21, acceptance criterion 7: "Every skipped probe is explainable with
    // recorded evidence."
    std::vector<std::string> reasons;
};

struct ProbeResult {
    std::string probe_id;
    bool passed = false;
    double duration_seconds = 0.0;
    std::string output;
};

} // namespace fissure
