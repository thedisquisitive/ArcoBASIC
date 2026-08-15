#include "arco/random.hpp"
#include "arco/runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void require_error(Function function, const std::string& expected, const std::string& message) {
    try {
        function();
    } catch (const std::exception& error) {
        require(std::string(error.what()).find(expected) != std::string::npos,
                message + ": unexpected diagnostic: " + error.what());
        return;
    }
    throw std::runtime_error(message + ": expected an error");
}

std::vector<double> numbers(const arco::Value& value) {
    require(value.is_array(), "expected an array result");
    std::vector<double> result;
    for (const auto& item : value.as_array()) result.push_back(item.as_number());
    return result;
}

} // namespace

int main() {
    try {
        {
            arco::Pcg32 generator(42, 54);
            const std::uint32_t expected[] = {
                UINT32_C(0xa15c02b7), UINT32_C(0x7b47f409), UINT32_C(0xba1d3330),
                UINT32_C(0x83d2f293), UINT32_C(0xbfa4784b), UINT32_C(0xcbed606e),
            };
            for (const auto value : expected) {
                require(generator.next_u32() == value, "PCG32 known-answer vector mismatch");
            }
        }
        {
            arco::Pcg32 first(9007199254740991ULL, 9007199254740991ULL);
            arco::Pcg32 second(9007199254740991ULL, 9007199254740991ULL);
            for (int index = 0; index < 16; ++index) {
                require(first.next_u32() == second.next_u32(), "maximum safe seed is reproducible");
            }
        }
        {
            arco::Pcg32 generator(42, 54);
            const double value = generator.unit_interval();
            require(value >= 0.0 && value < 1.0, "unit interval stays in [0, 1)");
        }
        {
            arco::Pcg32 generator(42, 54);
            bool saw_zero = false;
            bool saw_one = false;
            for (int index = 0; index < 64; ++index) {
                const auto value = generator.bounded(2);
                saw_zero = saw_zero || value == 0;
                saw_one = saw_one || value == 1;
            }
            require(saw_zero && saw_one, "bounded sampling reaches both binary endpoints");
            require(generator.bounded(UINT64_C(1) << 32U) <= UINT32_MAX,
                    "bounded sampling supports the complete 32-bit range");
            require_error([&] { (void)generator.bounded(0); }, "between 1 and 2^32",
                          "zero PCG bound is rejected");
        }

        arco::Runtime runtime;
        const auto first = runtime.call_host_function("Random.Create", {42, 54});
        const auto second = runtime.call_host_function("random.create", {42, 54});
        const auto different_stream = runtime.call_host_function("Random.Create", {42, 55});
        require(first.is_handle() && second.is_handle(), "Random.Create returns opaque handles");
        require(runtime.value_matches_type(first, "RANDOM"), "RANDOM type annotations accept generators");
        require(runtime.value_matches_type(arco::Value(nullptr), "RANDOM"), "RANDOM type accepts NULL");
        require(runtime.call_host_function("Random.Float", {first}).as_number() ==
                    runtime.call_host_function("Random.Float", {second}).as_number(),
                "equal seeds and sequences produce equal values");
        require(runtime.call_host_function("Random.Float", {different_stream}).as_number() !=
                    runtime.call_host_function("Random.Float", {runtime.call_host_function("Random.Create", {42, 54})}).as_number(),
                "different sequence values initialize distinct streams");

        const auto shared = runtime.call_host_function("Random.Create", {12345});
        const auto copied = shared;
        const auto reference = runtime.call_host_function("Random.Create", {12345});
        const double shared_first = runtime.call_host_function("Random.Float", {shared}).as_number();
        const double shared_second = runtime.call_host_function("Random.Float", {copied}).as_number();
        const double reference_first = runtime.call_host_function("Random.Float", {reference}).as_number();
        const double reference_second = runtime.call_host_function("Random.Float", {reference}).as_number();
        require(shared_first == reference_first && shared_second == reference_second,
                "copied RANDOM handles share one stream");

        const auto clone_source = runtime.call_host_function("Random.Create", {20260814});
        (void)runtime.call_host_function("Random.Float", {clone_source});
        const auto clone = runtime.call_host_function("Random.Clone", {clone_source});
        require(clone.is_handle() && !(clone.as_handle() == clone_source.as_handle()),
                "Random.Clone returns a distinct RANDOM handle");
        require(runtime.resources().lookup(clone.as_handle()) != nullptr,
                "Random.Clone registers independent resource metadata");
        require(runtime.call_host_function("Random.Integer", {0, 1000, clone_source}).as_number() ==
                    runtime.call_host_function("Random.Integer", {0, 1000, clone}).as_number(),
                "a cloned generator begins at the source state");
        const double source_next = runtime.call_host_function("Random.Integer", {0, 1000, clone_source}).as_number();
        const double clone_next = runtime.call_host_function("Random.Integer", {0, 1000, clone}).as_number();
        require(source_next == clone_next, "equal calls retain equal cloned states");
        const double clone_advanced = runtime.call_host_function("Random.Integer", {0, 1000, clone}).as_number();
        const double source_advanced = runtime.call_host_function("Random.Integer", {0, 1000, clone_source}).as_number();
        require(clone_advanced == source_advanced, "cloned handles advance independently");
        require(runtime.call_host_function("Random.Destroy", {clone}).truthy(), "a clone can be destroyed independently");
        require(runtime.call_host_function("Random.Float", {clone_source}).is_number(),
                "destroying a clone leaves its source valid");
        require_error([&] { runtime.call_host_function("Random.Clone", {clone}); },
                      "invalid or destroyed RANDOM handle", "destroyed clones cannot be cloned");
        require_error([&] { runtime.call_host_function("Random.Clone", {arco::Value(nullptr)}); },
                      "explicit RANDOM handle", "the default generator cannot be cloned");
        require_error([&] { runtime.call_host_function("Random.Clone", {}); },
                      "expects 1 argument", "Random.Clone requires a source argument");
        require(runtime.call_host_function("Random.Destroy", {clone_source}).truthy(),
                "the clone source can be destroyed independently");

        require(runtime.call_host_function("Random.Reseed", {shared, 12345}).truthy(),
                "Random.Reseed succeeds");
        require(runtime.call_host_function("Random.Float", {shared}).as_number() == reference_first,
                "reseed reproduces the initial stream");

        const double default_value = runtime.call_host_function("Random.Float", {}).as_number();
        const double math_value = runtime.call_host_function("Math.Random", {}).as_number();
        require(default_value >= 0.0 && default_value < 1.0 && math_value >= 0.0 && math_value < 1.0,
                "default random APIs return unit interval values");
        require(runtime.call_host_function("Random.Integer", {7, 7, first}).as_number() == 7,
                "equal integer bounds return the bound");

        const arco::Value source(arco::Value::Array{1, 2, 3, 4, 5});
        const auto choice = runtime.call_host_function("Random.Choice", {source, first});
        require(choice.as_number() >= 1 && choice.as_number() <= 5, "Random.Choice returns a source value");
        const auto sample = runtime.call_host_function("Random.Sample", {source, 3, first});
        require(sample.as_array().size() == 3, "Random.Sample returns the requested count");
        require(source.as_array().size() == 5 && source.as_array()[0].as_number() == 1,
                "Random.Sample does not mutate its source");
        auto shuffled = numbers(runtime.call_host_function("Random.Shuffle", {source, first}));
        std::sort(shuffled.begin(), shuffled.end());
        require(shuffled == std::vector<double>({1, 2, 3, 4, 5}), "Random.Shuffle preserves source elements");
        require(source.as_array()[0].as_number() == 1 && source.as_array()[4].as_number() == 5,
                "Random.Shuffle does not mutate its source");
        require(runtime.call_host_function("Random.Sample", {source, 0, first}).as_array().empty(),
                "zero-count sampling returns an empty array");

        require_error([&] { runtime.call_host_function("Random.Create", {-1}); }, "non-negative safe integer",
                      "negative seeds are rejected");
        require_error([&] { runtime.call_host_function("Random.Create", {1.5}); }, "non-negative safe integer",
                      "fractional seeds are rejected");
        require_error([&] { runtime.call_host_function("Random.Create", {std::numeric_limits<double>::infinity()}); },
                      "non-negative safe integer", "non-finite seeds are rejected");
        require_error([&] { runtime.call_host_function("Random.Integer", {5, 4}); }, "minimum <= maximum",
                      "reversed integer bounds are rejected");
        require_error([&] { runtime.call_host_function("Random.Integer", {0, 4294967296.0}); },
                      "must not exceed 2^32", "over-wide integer ranges are rejected");
        require_error([&] {
            runtime.call_host_function("Random.Choice", std::vector<arco::Value>{arco::Value(arco::Value::Array{})});
        },
                      "non-empty array", "empty choice is rejected");
        require_error([&] { runtime.call_host_function("Random.Sample", {source, 6}); },
                      "between 0 and the source length", "oversized samples are rejected");
        require_error([&] { runtime.call_host_function("Random.Reseed", {arco::Value(nullptr), 1}); },
                      "explicit RANDOM handle", "the default generator cannot be reseeded");

        const auto wrong_handle = runtime.object_handles().create("SURFACE", std::make_shared<int>(1));
        require_error([&] { runtime.call_host_function("Random.Float", {arco::Value(wrong_handle)}); },
                      "invalid or destroyed RANDOM handle", "wrong-type handles are rejected");

        const auto destroyed = runtime.call_host_function("Random.Create", {9});
        const auto record = runtime.resources().lookup(destroyed.as_handle());
        require(record != nullptr && record->kind == "RANDOM", "explicit generators register resource metadata");
        require(runtime.call_host_function("Random.Destroy", {destroyed}).truthy(), "Random.Destroy succeeds");
        require(runtime.resources().lookup(destroyed.as_handle()) == nullptr,
                "destroying a generator unregisters resource metadata");
        require_error([&] { runtime.call_host_function("Random.Float", {destroyed}); },
                      "invalid or destroyed RANDOM handle", "stale handles are rejected");
        require_error([&] { runtime.call_host_function("Random.Destroy", {destroyed}); },
                      "invalid or destroyed RANDOM handle", "repeated destruction fails safely");
        require_error([&] { runtime.call_host_function("Random.Destroy", {arco::Value(nullptr)}); },
                      "invalid or destroyed RANDOM handle", "null destruction fails safely");

        const auto script = runtime.run_string(
            "LET rng AS RANDOM = Random.Create(42)\n"
            "PRINT Random.Integer(1, 1, rng)\n"
            "Random.Destroy(rng)\n");
        require(script.ok, "typed RANDOM handles execute in ArcoBASIC: " + script.error);

        std::cout << "PASS: deterministic random runtime\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
