#include "fissure/types.hpp"

namespace fissure {

std::string to_string(NodeKind kind) {
    switch (kind) {
        case NodeKind::File: return "file";
        case NodeKind::Module: return "module";
        case NodeKind::Package: return "package";
        case NodeKind::Namespace: return "namespace";
        case NodeKind::Type: return "type";
        case NodeKind::Function: return "function";
        case NodeKind::Symbol: return "symbol";
        case NodeKind::BuildTarget: return "build-target";
        case NodeKind::Binary: return "binary";
        case NodeKind::GeneratedArtifact: return "generated-artifact";
        case NodeKind::Resource: return "resource";
        case NodeKind::Probe: return "probe";
        case NodeKind::ExternalDependency: return "external-dependency";
    }
    return "file";
}

std::optional<NodeKind> node_kind_from_string(const std::string& text) {
    if (text == "file") return NodeKind::File;
    if (text == "module") return NodeKind::Module;
    if (text == "package") return NodeKind::Package;
    if (text == "namespace") return NodeKind::Namespace;
    if (text == "type") return NodeKind::Type;
    if (text == "function") return NodeKind::Function;
    if (text == "symbol") return NodeKind::Symbol;
    if (text == "build-target") return NodeKind::BuildTarget;
    if (text == "binary") return NodeKind::Binary;
    if (text == "generated-artifact") return NodeKind::GeneratedArtifact;
    if (text == "resource") return NodeKind::Resource;
    if (text == "probe") return NodeKind::Probe;
    if (text == "external-dependency") return NodeKind::ExternalDependency;
    return std::nullopt;
}

std::string to_string(EdgeKind kind) {
    switch (kind) {
        case EdgeKind::Imports: return "imports";
        case EdgeKind::Calls: return "calls";
        case EdgeKind::Inherits: return "inherits";
        case EdgeKind::LinksTo: return "links-to";
        case EdgeKind::Generates: return "generates";
        case EdgeKind::Consumes: return "consumes";
        case EdgeKind::LoadsAtRuntime: return "loads-at-runtime";
        case EdgeKind::Reads: return "reads";
        case EdgeKind::Writes: return "writes";
        case EdgeKind::Executes: return "executes";
        case EdgeKind::ObservedDuringProbe: return "observed-during-probe";
        case EdgeKind::BuildDependency: return "build-dependency";
        case EdgeKind::DeclaredTestAssociation: return "declared-test-association";
    }
    return "reads";
}

std::optional<EdgeKind> edge_kind_from_string(const std::string& text) {
    if (text == "imports") return EdgeKind::Imports;
    if (text == "calls") return EdgeKind::Calls;
    if (text == "inherits") return EdgeKind::Inherits;
    if (text == "links-to") return EdgeKind::LinksTo;
    if (text == "generates") return EdgeKind::Generates;
    if (text == "consumes") return EdgeKind::Consumes;
    if (text == "loads-at-runtime") return EdgeKind::LoadsAtRuntime;
    if (text == "reads") return EdgeKind::Reads;
    if (text == "writes") return EdgeKind::Writes;
    if (text == "executes") return EdgeKind::Executes;
    if (text == "observed-during-probe") return EdgeKind::ObservedDuringProbe;
    if (text == "build-dependency") return EdgeKind::BuildDependency;
    if (text == "declared-test-association") return EdgeKind::DeclaredTestAssociation;
    return std::nullopt;
}

std::string to_string(Evidence evidence) {
    switch (evidence) {
        case Evidence::Static: return "static";
        case Evidence::Build: return "build";
        case Evidence::Observed: return "observed";
        case Evidence::Historical: return "historical";
        case Evidence::Declared: return "declared";
    }
    return "declared";
}

std::optional<Evidence> evidence_from_string(const std::string& text) {
    if (text == "static") return Evidence::Static;
    if (text == "build") return Evidence::Build;
    if (text == "observed") return Evidence::Observed;
    if (text == "historical") return Evidence::Historical;
    if (text == "declared") return Evidence::Declared;
    return std::nullopt;
}

std::string to_string(Classification value) {
    switch (value) {
        case Classification::Affected: return "AFFECTED";
        case Classification::Unaffected: return "UNAFFECTED";
        case Classification::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

} // namespace fissure
