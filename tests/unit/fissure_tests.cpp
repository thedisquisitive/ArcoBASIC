// Plain-assert unit tests for Fissure's native core, matching tests/unit/arcoui_core_tests.cpp's
// own style. Covers exactly what fissure/docs/fissure-rfc.md section 22.7 requires before any
// deeper language intelligence work: graph traversal, classification invariants (above all,
// "unit tests proving UNKNOWN cannot become skipped without explicit evidence"), and manifest
// parsing/capability enforcement (section 8).

#include "fissure/graph.hpp"
#include "fissure/impact.hpp"
#include "fissure/manifest.hpp"
#include "fissure/vm.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>

namespace {

using namespace fissure;

Edge make_edge(std::string from, std::string to, EdgeKind kind, Evidence evidence) {
    Edge edge;
    edge.from = std::move(from);
    edge.to = std::move(to);
    edge.kind = kind;
    edge.evidence = evidence;
    edge.confidence = 1.0;
    return edge;
}

Node make_node(std::string id, NodeKind kind) {
    Node node;
    node.id = std::move(id);
    node.kind = kind;
    return node;
}

// --- Graph traversal ---------------------------------------------------------------------------

void test_propagate_incoming_finds_transitive_dependents() {
    Graph graph;
    graph.add_node(make_node("a.cpp", NodeKind::File));
    graph.add_node(make_node("b.cpp", NodeKind::File));
    graph.add_node(make_node("c.cpp", NodeKind::File));
    // b imports a, c imports b -- so a change to a.cpp should reach both b.cpp and c.cpp when
    // propagating backward (Direction::Incoming, the direction impact analysis actually needs --
    // see graph.hpp's own doc comment on why forward/Outgoing would answer the wrong question).
    graph.add_edge(make_edge("b.cpp", "a.cpp", EdgeKind::Imports, Evidence::Static));
    graph.add_edge(make_edge("c.cpp", "b.cpp", EdgeKind::Imports, Evidence::Static));

    auto reached = graph.propagate({"a.cpp"}, 64, Graph::Direction::Incoming);
    bool found_b = false, found_c = false;
    for (const auto& entry : reached) {
        if (entry.node_id == "b.cpp") found_b = true;
        if (entry.node_id == "c.cpp") found_c = true;
    }
    assert(found_b);
    assert(found_c);

    // Forward/Outgoing from a.cpp must NOT reach b.cpp or c.cpp -- a.cpp doesn't point at either
    // of them (they point at it), confirming propagate() actually respects `direction` rather
    // than always walking the same way regardless of the argument.
    auto forward = graph.propagate({"a.cpp"}, 64, Graph::Direction::Outgoing);
    assert(forward.size() == 1); // only the seed itself
}

void test_propagate_respects_max_depth() {
    Graph graph;
    graph.add_edge(make_edge("b", "a", EdgeKind::Imports, Evidence::Static));
    graph.add_edge(make_edge("c", "b", EdgeKind::Imports, Evidence::Static));
    auto shallow = graph.propagate({"a"}, /*max_depth=*/1, Graph::Direction::Incoming);
    bool found_c = false;
    for (const auto& entry : shallow) if (entry.node_id == "c") found_c = true;
    assert(!found_c); // two hops away, beyond depth 1
}

// --- Classification invariants (RFC 6.1) --------------------------------------------------------

void test_probe_reachable_from_disturbance_is_affected() {
    Graph graph;
    graph.add_node(make_node("changed.cpp", NodeKind::File));
    graph.add_node(make_node("probe.a", NodeKind::Probe));
    graph.add_edge(make_edge("probe.a", "changed.cpp", EdgeKind::DeclaredTestAssociation, Evidence::Declared));

    ImpactEngine engine;
    auto verdicts = engine.classify(graph, {"changed.cpp"});
    assert(verdicts.size() == 1);
    assert(verdicts[0].probe_id == "probe.a");
    assert(verdicts[0].classification == Classification::Affected);
    assert(!verdicts[0].reasons.empty());
}

void test_probe_with_no_evidence_at_all_is_unknown_never_unaffected() {
    // The single most important invariant in the whole packet (RFC 6.1: "UNKNOWN must never be
    // silently converted to UNAFFECTED"). A probe with zero edges of any kind has no evidence
    // whatsoever connecting it to anything -- absence of evidence, not proof of independence.
    Graph graph;
    graph.add_node(make_node("changed.cpp", NodeKind::File));
    graph.add_node(make_node("probe.mystery", NodeKind::Probe)); // no edges at all

    ImpactEngine engine;
    auto verdicts = engine.classify(graph, {"changed.cpp"});
    assert(verdicts.size() == 1);
    assert(verdicts[0].classification == Classification::Unknown);
    assert(verdicts[0].classification != Classification::Unaffected);
}

void test_probe_with_declared_deps_not_intersecting_disturbance_is_unaffected() {
    Graph graph;
    graph.add_node(make_node("changed.cpp", NodeKind::File));
    graph.add_node(make_node("unrelated.cpp", NodeKind::File));
    graph.add_node(make_node("probe.safe", NodeKind::Probe));
    graph.add_edge(make_edge("probe.safe", "unrelated.cpp", EdgeKind::DeclaredTestAssociation, Evidence::Declared));

    ImpactEngine engine;
    auto verdicts = engine.classify(graph, {"changed.cpp"});
    assert(verdicts.size() == 1);
    assert(verdicts[0].classification == Classification::Unaffected);
}

void test_declared_directory_dependency_matches_child_disturbance() {
    Graph graph;
    graph.add_node(make_node("src/compiler/fission.cpp", NodeKind::File));
    graph.add_node(make_node("probe.compiler", NodeKind::Probe));
    graph.add_edge(make_edge("probe.compiler", "src/compiler/", EdgeKind::DeclaredTestAssociation, Evidence::Declared));

    ImpactEngine engine;
    auto verdicts = engine.classify(graph, {"src/compiler/fission.cpp"});
    assert(verdicts.size() == 1);
    assert(verdicts[0].classification == Classification::Affected);
    assert(verdicts[0].reasons[0].find("src/compiler/") != std::string::npos);
}

void test_declared_prefix_dependency_can_still_prove_unaffected() {
    Graph graph;
    graph.add_node(make_node("src/gui/glfw_backend.cpp", NodeKind::File));
    graph.add_node(make_node("probe.compiler", NodeKind::Probe));
    graph.add_edge(make_edge("probe.compiler", "src/compiler/", EdgeKind::DeclaredTestAssociation, Evidence::Declared));

    ImpactEngine engine;
    auto verdicts = engine.classify(graph, {"src/gui/glfw_backend.cpp"});
    assert(verdicts.size() == 1);
    assert(verdicts[0].classification == Classification::Unaffected);
}

void test_observed_only_evidence_does_not_yet_count_as_sufficient_for_unaffected() {
    // M1 has exactly one evidence source strong enough to prove UNAFFECTED: Declared (see
    // impact.cpp's own is_sufficient_for_unaffected). An edge with Observed evidence and no
    // Declared edge at all must still leave the probe UNKNOWN, not UNAFFECTED -- there is no
    // runtime observation backend yet (that's M4), so treating Observed evidence as sufficient
    // today would be claiming a capability that doesn't exist.
    Graph graph;
    graph.add_node(make_node("changed.cpp", NodeKind::File));
    graph.add_node(make_node("unrelated.cpp", NodeKind::File));
    graph.add_node(make_node("probe.observed", NodeKind::Probe));
    graph.add_edge(make_edge("probe.observed", "unrelated.cpp", EdgeKind::ObservedDuringProbe, Evidence::Observed));

    ImpactEngine engine;
    auto verdicts = engine.classify(graph, {"changed.cpp"});
    assert(verdicts.size() == 1);
    assert(verdicts[0].classification == Classification::Unknown);
}

void test_full_repo_mixed_verdicts() {
    // A slightly larger scenario exercising all three states together in one classify() call.
    Graph graph;
    graph.add_node(make_node("core.cpp", NodeKind::File));
    graph.add_node(make_node("docs.md", NodeKind::File));
    graph.add_node(make_node("probe.core", NodeKind::Probe));
    graph.add_node(make_node("probe.unrelated", NodeKind::Probe));
    graph.add_node(make_node("probe.nothing_declared", NodeKind::Probe));
    graph.add_edge(make_edge("probe.core", "core.cpp", EdgeKind::DeclaredTestAssociation, Evidence::Declared));
    graph.add_edge(make_edge("probe.unrelated", "docs.md", EdgeKind::DeclaredTestAssociation, Evidence::Declared));

    ImpactEngine engine;
    auto verdicts = engine.classify(graph, {"core.cpp"});
    assert(verdicts.size() == 3);
    for (const auto& verdict : verdicts) {
        if (verdict.probe_id == "probe.core") assert(verdict.classification == Classification::Affected);
        if (verdict.probe_id == "probe.unrelated") assert(verdict.classification == Classification::Unaffected);
        if (verdict.probe_id == "probe.nothing_declared") assert(verdict.classification == Classification::Unknown);
    }
}

// --- Manifest parsing and capability enforcement (RFC section 8) -------------------------------

void test_manifest_parses_capabilities() {
    std::string source =
        "' #FISSURE-PLUGIN 1\n"
        "' #NAME \"Test Adapter\"\n"
        "' #TYPE LANGUAGE\n"
        "' #REQUIRES FILE.READ\n"
        "' #REQUIRES PROCESS.EXEC\n"
        "\n"
        "FUNCTION Noop()\n"
        "END FUNCTION\n";
    Manifest manifest = parse_manifest(source);
    assert(manifest.present);
    assert(manifest.name == "Test Adapter");
    assert(manifest.type == "LANGUAGE");
    assert(manifest.capabilities.count("FILE.READ") == 1);
    assert(manifest.capabilities.count("PROCESS.EXEC") == 1);
    assert(manifest.capabilities.count("NETWORK") == 0);
}

void test_manifest_absent_grants_nothing() {
    Manifest manifest = parse_manifest("FUNCTION Noop()\nEND FUNCTION\n");
    assert(!manifest.present);
    assert(manifest.capabilities.empty());
}

void test_manifest_header_ends_at_first_code_line() {
    // A #REQUIRES appearing AFTER real code has started must not be picked up -- the header
    // region ends at the first non-comment, non-blank line (manifest.cpp's own documented rule).
    std::string source =
        "' #FISSURE-PLUGIN 1\n"
        "' #REQUIRES FILE.READ\n"
        "FUNCTION Noop()\n"
        "END FUNCTION\n"
        "' #REQUIRES NETWORK\n";
    Manifest manifest = parse_manifest(source);
    assert(manifest.capabilities.count("FILE.READ") == 1);
    assert(manifest.capabilities.count("NETWORK") == 0);
}

// --- VM host contract crossing the boundary (RFC M0: "Host contract call crosses VM/native
// boundary") + capability denial actually functioning, not just parsed -------------------------

void test_undeclared_capability_is_denied() {
    Graph graph;
    EventBus events;
    VM vm(graph, events, "/tmp");
    // No manifest header at all -- FISSURE.Graph.Node must be denied, not silently succeed.
    std::string script = "ignored = FISSURE.Graph.Node(\"x\", \"file\")\n";
    arco::RunResult result = vm.runtime().run_string(script);
    assert(!result.ok);
    assert(graph.node_count() == 0);
}

void test_declared_capability_crosses_the_boundary() {
    // Exercises the real path a CLI/adapter uses -- load_script(), which parses the manifest AND
    // grants the capability -- not run_string() alone (that's the previous test's own point: a
    // script's capabilities only take effect via load_script).
    std::string path = "/tmp/fissure_test_declared_capability.ab";
    {
        std::ofstream file(path);
        file << "' #FISSURE-PLUGIN 1\n"
             << "' #REQUIRES GRAPH.WRITE\n"
             << "FUNCTION RegisterIt()\n"
             << "    ignored = FISSURE.Graph.Node(\"x\", \"file\")\n"
             << "END FUNCTION\n";
    }

    Graph graph;
    EventBus events;
    VM vm(graph, events, "/tmp");
    vm.load_script(path);
    assert(vm.has_capability("GRAPH.WRITE"));
    bool called = vm.call_if_defined("RegisterIt");
    assert(called);
    assert(graph.has_node("x"));

    std::remove(path.c_str());
}

} // namespace

int main() {
    test_propagate_incoming_finds_transitive_dependents();
    test_propagate_respects_max_depth();
    test_probe_reachable_from_disturbance_is_affected();
    test_probe_with_no_evidence_at_all_is_unknown_never_unaffected();
    test_probe_with_declared_deps_not_intersecting_disturbance_is_unaffected();
    test_declared_directory_dependency_matches_child_disturbance();
    test_declared_prefix_dependency_can_still_prove_unaffected();
    test_observed_only_evidence_does_not_yet_count_as_sufficient_for_unaffected();
    test_full_repo_mixed_verdicts();
    test_manifest_parses_capabilities();
    test_manifest_absent_grants_nothing();
    test_manifest_header_ends_at_first_code_line();
    test_undeclared_capability_is_denied();
    test_declared_capability_crosses_the_boundary();

    std::cout << "fissure_tests: all tests passed" << std::endl;
    return 0;
}
