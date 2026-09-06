#pragma once

// The ArcoUI language-neutral core (RFC-ArcoUI Part II, Milestone 0): opaque-handle object
// registry, a semantic tree kept structurally separate from a presentation tree (section 40/41),
// intents with state/blocked-reason (section 12), transactions with Begin/Update/Commit/Cancel/
// Undo/Redo (section 12.4), and an application-surface shape model (section 9/42).
//
// This header and its implementation (src/gui/arcoui/core.cpp) intentionally know nothing about
// arco::Value, the ArcoBASIC parser/runtime, or any rendering backend -- see the dependency note
// in arcoui/types.hpp. src/gui/arcoui/bindings.cpp is the only file that bridges this to
// ArcoBASIC, and rendering/input still flow through the existing arco::gui backends; a Surface
// here tracks which backend window id (if any) it is bound to but never creates or draws into one
// itself.

#include "arcoui/geometry.hpp"
#include "arcoui/gesture.hpp"
#include "arcoui/types.hpp"

#include "arco/runtime_handles.hpp"

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace arcoui {

// A node in the semantic tree: what an object *means*, independent of where (or whether) it is
// currently drawn. `owner_tag` is an opaque caller-supplied identifier (e.g. an ArcoBASIC widget's
// own object identity string) -- the core never interprets it.
struct SemanticNode {
    std::string name;
    std::string owner_tag;
    arco::RuntimeHandle parent;
    std::vector<arco::RuntimeHandle> children;
};

// A node in the presentation tree: what actually gets laid out/rendered/hit-tested. Deliberately a
// separate handle space from SemanticNode -- RFC-ArcoUI section 40 requires an intent be able to
// belong to a semantic object whose visible control is hosted elsewhere; nothing here forces a 1:1
// pairing between the two trees.
struct PresentationNode {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    bool visible = true;
    bool focused = false;
    std::string style_id;
    arco::RuntimeHandle parent;
    std::vector<arco::RuntimeHandle> children;
};

// A top-level (or composite-member) application surface. Shape is data here; actual pixel masking/
// hit-testing lives in geometry.hpp's shared shape_polygon()/point_in_shape(), and actual window
// creation stays with the existing arco::gui backend -- `backend_window_id` just remembers which
// one (if any) this Surface is bound to.
struct Surface {
    std::string title;
    Shape shape = Shape::Rectangle;
    std::vector<Point> custom_polygon; // only meaningful when shape == Shape::Polygon
    double width = 0.0;
    double height = 0.0;
    int backend_window_id = -1;
    arco::RuntimeHandle root_semantic;
    arco::RuntimeHandle root_presentation;

    bool point_inside(double px, double py) const {
        return point_in_shape(shape, width, height, custom_polygon, px, py);
    }
};

// A named, semantically-anchored capability (RFC-ArcoUI section 6.1/12).
struct Intent {
    std::string name;
    arco::RuntimeHandle semantic_node; // owning node; may be an invalid handle if unowned
    bool continuous = false;
    IntentState state = IntentState::Available;
    std::string blocked_reason;
    TransactionPhase phase = TransactionPhase::Idle;
};

// One committed transaction, kept on the undo/redo stack. `data` is an opaque caller-supplied
// snapshot (the core does not interpret it) -- e.g. a Canvas widget stores enough to redraw or
// remove one committed stroke.
struct TransactionRecord {
    arco::RuntimeHandle intent;
    std::string label;
    std::map<std::string, std::string> data;
};

class Runtime {
public:
    Runtime() = default;

    // --- Surfaces --------------------------------------------------------
    arco::RuntimeHandle create_surface(const std::string& title, Shape shape, double width, double height,
                                       std::vector<Point> custom_polygon = {});
    bool destroy_surface(const arco::RuntimeHandle& handle);
    Surface* surface(const arco::RuntimeHandle& handle);
    const Surface* surface(const arco::RuntimeHandle& handle) const;
    bool bind_surface_window(const arco::RuntimeHandle& handle, int backend_window_id);

    // --- Semantic tree -----------------------------------------------------
    arco::RuntimeHandle create_semantic_node(const std::string& name, const arco::RuntimeHandle& parent,
                                             const std::string& owner_tag);
    SemanticNode* semantic_node(const arco::RuntimeHandle& handle);
    const SemanticNode* semantic_node(const arco::RuntimeHandle& handle) const;

    // --- Presentation tree ---------------------------------------------
    arco::RuntimeHandle create_presentation_node(const arco::RuntimeHandle& parent);
    PresentationNode* presentation_node(const arco::RuntimeHandle& handle);
    const PresentationNode* presentation_node(const arco::RuntimeHandle& handle) const;

    // --- Intents -----------------------------------------------------------
    arco::RuntimeHandle define_intent(const std::string& name, const arco::RuntimeHandle& semantic_node,
                                      bool continuous);
    Intent* intent(const arco::RuntimeHandle& handle);
    const Intent* intent(const arco::RuntimeHandle& handle) const;
    bool set_intent_state(const arco::RuntimeHandle& handle, IntentState state, const std::string& blocked_reason = {});
    // A non-continuous intent firing once (e.g. a Button click). Fails (returns false, no event
    // pushed) unless the intent is currently Available -- an Unavailable/Blocked/Active intent
    // cannot be invoked out from under its own semantic state.
    bool invoke_intent(const arco::RuntimeHandle& handle, EventValue value, std::map<std::string, std::string> context,
                       double timestamp);

    // --- Transactions (continuous intents) --------------------------------
    bool begin_transaction(const arco::RuntimeHandle& handle, EventValue value, double timestamp);
    bool update_transaction(const arco::RuntimeHandle& handle, EventValue value, double timestamp);
    bool commit_transaction(const arco::RuntimeHandle& handle, std::map<std::string, std::string> record_data,
                            const std::string& label, double timestamp);
    bool cancel_transaction(const arco::RuntimeHandle& handle, double timestamp);
    std::optional<TransactionRecord> undo();
    std::optional<TransactionRecord> redo();

    // --- Event queue ---------------------------------------------------
    std::optional<IntentEvent> poll_event();

    // --- Gesture recognizers (RFC-ArcoUI section 45: Flick/Throw, a reusable primitive, not
    // embedded in one title-bar widget) -- handle-owned the same way as every other core object,
    // so an application can host several independent recognizers (one per draggable region).
    arco::RuntimeHandle create_gesture_recognizer();
    bool destroy_gesture_recognizer(const arco::RuntimeHandle& handle);
    GestureRecognizer* gesture_recognizer(const arco::RuntimeHandle& handle);

    // --- Handle introspection (RFC-ArcoUI section 38: invalid-handle behavior must be testable) --
    bool valid(const arco::RuntimeHandle& handle, const std::string& expected_type = {}) const;

private:
    void push_event(arco::RuntimeHandle target, arco::RuntimeHandle intent_handle, TransactionPhase phase,
                    EventValue value, std::map<std::string, std::string> context, double timestamp);

    arco::RuntimeHandleTable handles_;
    std::deque<IntentEvent> event_queue_;
    std::vector<TransactionRecord> undo_stack_;
    std::vector<TransactionRecord> redo_stack_;
};

} // namespace arcoui
