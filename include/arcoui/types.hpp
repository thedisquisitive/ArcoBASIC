#pragma once

// Plain data types shared across the ArcoUI core (arcoui::Runtime, src/gui/arcoui/core.cpp).
//
// RFC-ArcoUI section 36.1 requires the runtime core not depend on ArcoBASIC-specific object
// layout as its public interoperability model. Concretely here: nothing under include/arcoui/ or
// src/gui/arcoui/{core,geometry,gesture}.cpp includes arco/value.hpp or otherwise touches
// arco::Value -- only the binding layer (src/gui/arcoui/bindings.cpp) marshals between this and
// ArcoBASIC's Value. The one exception is arco::RuntimeHandle/RuntimeHandleTable
// (arco/runtime_handles.hpp): a plain {slot, generation, type} struct plus a generic templated
// registry with no ArcoBASIC-specific content, already used by this project for exactly this kind
// of stale-reference-safe opaque handle (RANDOM/SURFACE/CALLABLE in src/runtime/runtime.cpp) -- it
// is reused here as a general-purpose C++ utility, not as a bridge back into the ArcoBASIC runtime.

#include "arco/runtime_handles.hpp"

#include <map>
#include <string>

namespace arcoui {

// Semantic state of an Intent (RFC-ArcoUI section 12.1).
enum class IntentState {
    Available,
    Unavailable,
    Active,
    Pending,
    Blocked,
    Ambiguous,
    Completed,
    Failed,
};

std::string to_string(IntentState state);

// Lifecycle phase of a continuous/transactional intent (RFC-ArcoUI section 12.4/6.8).
enum class TransactionPhase {
    Idle,
    Began,
    Updating,
    Committed,
    Cancelled,
};

std::string to_string(TransactionPhase phase);

// A value carried by an IntentEvent -- deliberately a small tagged union (not arco::Value) so this
// header stays free of any ArcoBASIC dependency. Bindings convert to/from arco::Value at the edge.
struct EventValue {
    bool is_text = false;
    double number = 0.0;
    std::string text;

    static EventValue of_number(double value) { return EventValue{false, value, {}}; }
    static EventValue of_text(std::string value) { return EventValue{true, 0.0, std::move(value)}; }
};

// A normalized, queueable record of something happening to an intent (RFC-ArcoUI section 18).
struct IntentEvent {
    arco::RuntimeHandle target;   // the semantic node the intent belongs to
    arco::RuntimeHandle intent;
    TransactionPhase phase = TransactionPhase::Idle;
    EventValue value;
    std::map<std::string, std::string> context;
    double timestamp = 0.0;
};

} // namespace arcoui
