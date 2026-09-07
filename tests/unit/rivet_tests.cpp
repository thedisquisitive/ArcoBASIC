// Plain-assert unit tests for Rivet's native core, matching tests/unit/fissure_tests.cpp's own
// style. Covers what rivet/docs/rivet-rfc.md section 56 asks for at this slice's scope:
// fingerprinting, graph (including cycle rejection and duplicate-output rejection), cache
// (hit/miss/tamper detection), scheduler (real concurrency, failure cascade), and the state store
// primitive `rivet why rebuild` depends on.

#include "rivet/cache.hpp"
#include "rivet/fingerprint.hpp"
#include "rivet/graph.hpp"
#include "rivet/scheduler.hpp"
#include "rivet/store.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>

namespace {

using namespace rivet;

std::string write_temp_file(const std::string& name, const std::string& content) {
    std::string path = "/tmp/" + name;
    std::ofstream file(path);
    file << content;
    return path;
}

BuildAction make_action(std::string identity, std::string target, std::vector<std::string> inputs,
                         std::vector<std::string> outputs, std::vector<std::string> dependencies = {}) {
    BuildAction action;
    action.identity = std::move(identity);
    action.display_name = action.identity;
    action.type = ActionType::Compile;
    action.target = std::move(target);
    action.inputs = std::move(inputs);
    action.outputs = std::move(outputs);
    action.tool = "/bin/true";
    action.tool_version = "1";
    action.dependencies = std::move(dependencies);
    action.origin = {"test.abas", "TestFunction"};
    return action;
}

// --- Fingerprint ---------------------------------------------------------------------------

void test_hash_file_stable_and_content_sensitive() {
    std::string path = write_temp_file("rivet_test_hash_a.txt", "hello world");
    Digest first = hash_file(path);
    Digest second = hash_file(path);
    assert(first == second);

    std::string other_path = write_temp_file("rivet_test_hash_b.txt", "hello WORLD");
    Digest third = hash_file(other_path);
    assert(first != third);

    std::remove(path.c_str());
    std::remove(other_path.c_str());
}

void test_combine_is_order_sensitive() {
    Digest a = hash_string("a");
    Digest b = hash_string("b");
    Digest ab = combine({a, b});
    Digest ba = combine({b, a});
    assert(ab != ba);
    assert(combine({a, b}) == combine({a, b}));
}

// --- Graph ----------------------------------------------------------------------------------

void test_graph_independent_actions_both_appear() {
    BuildGraph graph;
    graph.add_action(make_action("A", "t", {}, {"a.o"}));
    graph.add_action(make_action("B", "t", {}, {"b.o"}));
    auto order = graph.topo_order();
    assert(order.size() == 2);
}

void test_graph_linear_chain_preserves_order() {
    BuildGraph graph;
    graph.add_action(make_action("A", "t", {}, {"a.o"}));
    graph.add_action(make_action("B", "t", {}, {"b.o"}, {"A"}));
    graph.add_action(make_action("C", "t", {}, {"c.o"}, {"B"}));
    auto order = graph.topo_order();
    auto index_of = [&](const std::string& id) {
        return static_cast<std::size_t>(std::find(order.begin(), order.end(), id) - order.begin());
    };
    assert(index_of("A") < index_of("B"));
    assert(index_of("B") < index_of("C"));
}

void test_graph_fan_in_and_fan_out() {
    BuildGraph graph;
    graph.add_action(make_action("A", "t", {}, {"a.o"}));
    graph.add_action(make_action("B", "t", {}, {"b.o"}, {"A"}));
    graph.add_action(make_action("C", "t", {}, {"c.o"}, {"A"})); // fan-out: A feeds both B and C
    graph.add_action(make_action("Link", "t", {}, {"out"}, {"B", "C"})); // fan-in
    auto order = graph.topo_order();
    auto index_of = [&](const std::string& id) {
        return static_cast<std::size_t>(std::find(order.begin(), order.end(), id) - order.begin());
    };
    assert(index_of("A") < index_of("B"));
    assert(index_of("A") < index_of("C"));
    assert(index_of("B") < index_of("Link"));
    assert(index_of("C") < index_of("Link"));
}

void test_graph_cycle_is_rejected() {
    BuildGraph graph;
    graph.add_action(make_action("A", "t", {}, {"a.o"}, {"B"}));
    graph.add_action(make_action("B", "t", {}, {"b.o"}, {"A"}));
    bool threw = false;
    try {
        graph.topo_order();
    } catch (const GraphCycleError& error) {
        threw = true;
        assert(!error.cycle_identities.empty());
    }
    assert(threw);
}

void test_graph_duplicate_output_is_rejected() {
    BuildGraph graph;
    graph.add_action(make_action("A", "t", {}, {"shared.o"}));
    bool threw = false;
    try {
        graph.add_action(make_action("B", "t", {}, {"shared.o"}));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

// --- Cache -----------------------------------------------------------------------------------

void test_cache_miss_then_hit_then_miss_on_argument_change() {
    std::string db_path = "/tmp/rivet_test_cache.db";
    std::remove(db_path.c_str());
    StateStore store = StateStore::open(db_path);
    CacheManager cache(store);

    std::string input_path = write_temp_file("rivet_test_cache_input.txt", "content");
    std::string output_path = "/tmp/rivet_test_cache_output.txt";
    std::ofstream(output_path) << "built";

    BuildAction action = make_action("Compile:cache_test", "t", {input_path}, {output_path});
    action.arguments = {"-O0"};

    CacheDecision first = cache.evaluate(action);
    assert(!first.hit);
    cache.record(action, first, ActionStatus::Ran, 0.1);

    CacheDecision second = cache.evaluate(action);
    assert(second.hit);

    action.arguments = {"-O2"}; // flag change must invalidate
    CacheDecision third = cache.evaluate(action);
    assert(!third.hit);

    std::remove(input_path.c_str());
    std::remove(output_path.c_str());
    std::remove(db_path.c_str());
}

void test_cache_detects_output_tampering() {
    std::string db_path = "/tmp/rivet_test_cache_tamper.db";
    std::remove(db_path.c_str());
    StateStore store = StateStore::open(db_path);
    CacheManager cache(store);

    std::string input_path = write_temp_file("rivet_test_tamper_input.txt", "content");
    std::string output_path = "/tmp/rivet_test_tamper_output.txt";
    std::ofstream(output_path) << "built";

    BuildAction action = make_action("Compile:tamper_test", "t", {input_path}, {output_path});
    CacheDecision decision = cache.evaluate(action);
    cache.record(action, decision, ActionStatus::Ran, 0.1);
    assert(cache.evaluate(action).hit);

    // Someone deletes the output by hand between builds, without touching the input.
    std::remove(output_path.c_str());
    assert(!cache.evaluate(action).hit);

    std::remove(input_path.c_str());
    std::remove(db_path.c_str());
}

// --- Scheduler ---------------------------------------------------------------------------------

void test_scheduler_runs_independent_actions_concurrently() {
    std::string db_path = "/tmp/rivet_test_scheduler_concurrency.db";
    std::remove(db_path.c_str());
    StateStore store = StateStore::open(db_path);
    CacheManager cache(store);
    Scheduler scheduler(cache, 2);

    BuildGraph graph;
    std::string out1 = "/tmp/rivet_test_sched_out1.txt";
    std::string out2 = "/tmp/rivet_test_sched_out2.txt";
    std::remove(out1.c_str());
    std::remove(out2.c_str());

    BuildAction a = make_action("Compile:sched1", "t", {}, {out1});
    a.tool = "/bin/sh";
    a.arguments = {"-c", "sleep 0.3 && touch " + out1};
    BuildAction b = make_action("Compile:sched2", "t", {}, {out2});
    b.tool = "/bin/sh";
    b.arguments = {"-c", "sleep 0.3 && touch " + out2};
    graph.add_action(a);
    graph.add_action(b);

    auto start = std::chrono::steady_clock::now();
    auto results = scheduler.run(graph);
    auto elapsed = std::chrono::steady_clock::now() - start;

    assert(results.size() == 2);
    for (const auto& result : results) assert(result.status == ActionStatus::Ran);
    // Two 0.3s actions run with 2 workers should finish well under their serial sum (0.6s) --
    // generous bound (0.5s) to stay robust under real scheduling jitter.
    assert(std::chrono::duration<double>(elapsed).count() < 0.5);

    std::remove(out1.c_str());
    std::remove(out2.c_str());
    std::remove(db_path.c_str());
}

void test_scheduler_cascades_failure_but_runs_independent_siblings() {
    std::string db_path = "/tmp/rivet_test_scheduler_failure.db";
    std::remove(db_path.c_str());
    StateStore store = StateStore::open(db_path);
    CacheManager cache(store);
    Scheduler scheduler(cache, 2);

    BuildGraph graph;
    std::string independent_out = "/tmp/rivet_test_sched_independent.txt";
    std::remove(independent_out.c_str());

    BuildAction broken = make_action("Compile:broken", "t", {}, {"/tmp/rivet_test_sched_broken.o"});
    broken.tool = "/bin/false"; // always exits 1
    BuildAction dependent = make_action("Link:broken_dependent", "t", {}, {"/tmp/rivet_test_sched_broken_out"}, {"Compile:broken"});
    dependent.tool = "/bin/true";
    BuildAction independent = make_action("Compile:independent", "t", {}, {independent_out});
    independent.tool = "/bin/sh";
    independent.arguments = {"-c", "touch " + independent_out};

    graph.add_action(broken);
    graph.add_action(dependent);
    graph.add_action(independent);

    auto results = scheduler.run(graph);
    ActionStatus broken_status{}, dependent_status{}, independent_status{};
    for (const auto& result : results) {
        if (result.identity == "Compile:broken") broken_status = result.status;
        if (result.identity == "Link:broken_dependent") dependent_status = result.status;
        if (result.identity == "Compile:independent") independent_status = result.status;
    }
    assert(broken_status == ActionStatus::Failed);
    assert(dependent_status == ActionStatus::SkippedDueToFailure);
    assert(independent_status == ActionStatus::Ran);

    std::remove(independent_out.c_str());
    std::remove(db_path.c_str());
}

// --- Store -----------------------------------------------------------------------------------

void test_store_round_trip_and_input_lookup() {
    std::string db_path = "/tmp/rivet_test_store.db";
    std::remove(db_path.c_str());
    StateStore store = StateStore::open(db_path);

    BuildAction action = make_action("Compile:store_test", "t", {"src/foo.cpp"}, {"foo.o"});
    Digest fingerprint = hash_string("fingerprint");
    store.record_action(action, fingerprint, ActionStatus::Ran, 0.5, "test reason",
                         {{"src/foo.cpp", hash_string("content")}}, {{"foo.o", hash_string("output")}});

    auto record = store.last_record("Compile:store_test");
    assert(record.has_value());
    assert(record->fingerprint == fingerprint);
    assert(record->reason == "test reason");
    assert(record->status == ActionStatus::Ran);

    auto referencing = store.actions_referencing_input("src/foo.cpp");
    assert(referencing.size() == 1);
    assert(referencing[0] == "Compile:store_test");

    std::remove(db_path.c_str());
}

} // namespace

int main() {
    test_hash_file_stable_and_content_sensitive();
    test_combine_is_order_sensitive();
    test_graph_independent_actions_both_appear();
    test_graph_linear_chain_preserves_order();
    test_graph_fan_in_and_fan_out();
    test_graph_cycle_is_rejected();
    test_graph_duplicate_output_is_rejected();
    test_cache_miss_then_hit_then_miss_on_argument_change();
    test_cache_detects_output_tampering();
    test_scheduler_runs_independent_actions_concurrently();
    test_scheduler_cascades_failure_but_runs_independent_siblings();
    test_store_round_trip_and_input_lookup();

    std::cout << "rivet_tests: all tests passed" << std::endl;
    return 0;
}
