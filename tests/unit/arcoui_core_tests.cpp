// Plain-assert unit tests for the ArcoUI core (RFC-ArcoUI Milestone 0), matching the style of
// tests/unit/resource_registry_tests.cpp. No display/backend dependency -- this exercises
// arcoui::Runtime directly, nothing about arco::Value or a GUI backend.

#include "arcoui/core.hpp"
#include "arcoui/gesture.hpp"
#include "arcoui/geometry.hpp"

#include <cassert>
#include <iostream>

namespace {

void test_stale_handle_rejected_after_destroy() {
    arcoui::Runtime runtime;
    arco::RuntimeHandle surface = runtime.create_surface("Test", arcoui::Shape::Rectangle, 100, 100);
    assert(runtime.valid(surface));
    assert(runtime.surface(surface) != nullptr);

    assert(runtime.destroy_surface(surface));
    assert(!runtime.valid(surface));
    assert(runtime.surface(surface) == nullptr);
    // Destroying an already-destroyed handle must fail cleanly, not double-free or throw.
    assert(!runtime.destroy_surface(surface));
}

void test_slot_reuse_does_not_alias_stale_handle() {
    arcoui::Runtime runtime;
    arco::RuntimeHandle first = runtime.create_surface("First", arcoui::Shape::Rectangle, 10, 10);
    assert(runtime.destroy_surface(first));
    arco::RuntimeHandle second = runtime.create_surface("Second", arcoui::Shape::Rectangle, 20, 20);
    // The stale `first` handle must never resolve to whatever now occupies its old slot, even if
    // the slot was reused (RuntimeHandleTable's generation bump is exactly what prevents this).
    assert(!runtime.valid(first));
    assert(runtime.valid(second));
    arcoui::Surface* resolved = runtime.surface(first);
    assert(resolved == nullptr);
}

void test_wrong_type_handle_rejected() {
    arcoui::Runtime runtime;
    arco::RuntimeHandle surface = runtime.create_surface("Test", arcoui::Shape::Rectangle, 10, 10);
    // A Surface handle must not resolve as an Intent, even though both are alive.
    assert(runtime.intent(surface) == nullptr);
}

void test_intent_state_and_blocked_reason() {
    arcoui::Runtime runtime;
    arco::RuntimeHandle node = runtime.create_semantic_node("Selection", arco::RuntimeHandle{}, "selection");
    arco::RuntimeHandle save = runtime.define_intent("SaveDocument", node, false);

    const arcoui::Intent* info = runtime.intent(save);
    assert(info != nullptr);
    assert(info->state == arcoui::IntentState::Available);
    assert(info->blocked_reason.empty());

    assert(runtime.set_intent_state(save, arcoui::IntentState::Blocked, "no editable object is selected"));
    info = runtime.intent(save);
    assert(info->state == arcoui::IntentState::Blocked);
    assert(info->blocked_reason == "no editable object is selected");

    // A blocked intent cannot be invoked.
    assert(!runtime.invoke_intent(save, arcoui::EventValue::of_number(0), {}, 1.0));

    assert(runtime.set_intent_state(save, arcoui::IntentState::Available));
    info = runtime.intent(save);
    // Clearing back to Available also clears the stale blocked reason.
    assert(info->blocked_reason.empty());
    assert(runtime.invoke_intent(save, arcoui::EventValue::of_number(0), {}, 2.0));

    std::optional<arcoui::IntentEvent> event = runtime.poll_event();
    assert(event.has_value());
    assert(event->phase == arcoui::TransactionPhase::Committed);
    assert(!runtime.poll_event().has_value());
}

// RFC-ArcoUI section 40's explicit required test: an intent semantically belongs to an object
// even when its visible control is hosted under a completely different presentation node.
void test_semantic_and_presentation_trees_diverge() {
    arcoui::Runtime runtime;
    arco::RuntimeHandle scene_root = runtime.create_semantic_node("Scene", arco::RuntimeHandle{}, "scene");
    arco::RuntimeHandle object_node = runtime.create_semantic_node("Cube", scene_root, "cube-1");
    arco::RuntimeHandle rotate = runtime.define_intent("RotateObject", object_node, true);

    // The visible control for RotateObject lives in a toolbar panel with no structural
    // relationship to the semantic scene tree at all -- a fresh, unrelated presentation root.
    arco::RuntimeHandle toolbar_root = runtime.create_presentation_node(arco::RuntimeHandle{});
    arco::RuntimeHandle rotate_button_visual = runtime.create_presentation_node(toolbar_root);

    const arcoui::Intent* info = runtime.intent(rotate);
    assert(info->semantic_node == object_node);
    // Nothing in the semantic tree references the presentation tree at all -- proves the two are
    // genuinely separate handle spaces, not just separate field names over the same nodes.
    assert(runtime.semantic_node(object_node) != nullptr);
    assert(runtime.presentation_node(rotate_button_visual) != nullptr);
    assert(runtime.presentation_node(toolbar_root)->children.size() == 1);
    assert(runtime.semantic_node(scene_root)->children.size() == 1);
}

void test_transaction_begin_update_commit() {
    arcoui::Runtime runtime;
    arco::RuntimeHandle node = runtime.create_semantic_node("Object", arco::RuntimeHandle{}, "obj-1");
    arco::RuntimeHandle move = runtime.define_intent("MoveObject", node, true);

    // Update/Commit/Cancel before Begin must all fail.
    assert(!runtime.update_transaction(move, arcoui::EventValue::of_number(1), 0.0));
    assert(!runtime.commit_transaction(move, {}, "", 0.0));
    assert(!runtime.cancel_transaction(move, 0.0));

    assert(runtime.begin_transaction(move, arcoui::EventValue::of_number(0), 1.0));
    assert(runtime.intent(move)->phase == arcoui::TransactionPhase::Began);
    assert(runtime.intent(move)->state == arcoui::IntentState::Active);
    // A second Begin while already active must fail (state is no longer Available).
    assert(!runtime.begin_transaction(move, arcoui::EventValue::of_number(0), 1.1));

    assert(runtime.update_transaction(move, arcoui::EventValue::of_number(5), 1.2));
    assert(runtime.intent(move)->phase == arcoui::TransactionPhase::Updating);

    assert(runtime.commit_transaction(move, {{"dx", "5"}}, "move-right", 1.3));
    // Commit returns a repeatable intent (RFC: a continuous intent runs again next drag).
    assert(runtime.intent(move)->phase == arcoui::TransactionPhase::Idle);
    assert(runtime.intent(move)->state == arcoui::IntentState::Available);

    // Events: Began, Updating, Committed, in order.
    auto e1 = runtime.poll_event();
    auto e2 = runtime.poll_event();
    auto e3 = runtime.poll_event();
    assert(e1 && e1->phase == arcoui::TransactionPhase::Began);
    assert(e2 && e2->phase == arcoui::TransactionPhase::Updating);
    assert(e3 && e3->phase == arcoui::TransactionPhase::Committed);
    assert(!runtime.poll_event().has_value());
}

void test_transaction_cancel_and_undo_redo() {
    arcoui::Runtime runtime;
    arco::RuntimeHandle node = runtime.create_semantic_node("Object", arco::RuntimeHandle{}, "obj-1");
    arco::RuntimeHandle move = runtime.define_intent("MoveObject", node, true);

    runtime.begin_transaction(move, arcoui::EventValue::of_number(0), 1.0);
    assert(runtime.cancel_transaction(move, 1.1));
    assert(runtime.intent(move)->phase == arcoui::TransactionPhase::Idle);
    assert(runtime.intent(move)->state == arcoui::IntentState::Available);
    // A cancelled transaction must not be recorded on the undo stack.
    assert(!runtime.undo().has_value());

    runtime.begin_transaction(move, arcoui::EventValue::of_number(0), 2.0);
    runtime.commit_transaction(move, {{"dx", "5"}}, "move-1", 2.1);
    runtime.begin_transaction(move, arcoui::EventValue::of_number(0), 3.0);
    runtime.commit_transaction(move, {{"dx", "-2"}}, "move-2", 3.1);

    std::optional<arcoui::TransactionRecord> undone = runtime.undo();
    assert(undone.has_value());
    assert(undone->label == "move-2");
    assert(undone->data.at("dx") == "-2");

    std::optional<arcoui::TransactionRecord> redone = runtime.redo();
    assert(redone.has_value());
    assert(redone->label == "move-2");

    // A fresh commit after undo invalidates the redo stack (standard undo/redo semantics).
    runtime.undo();
    runtime.begin_transaction(move, arcoui::EventValue::of_number(0), 4.0);
    runtime.commit_transaction(move, {{"dx", "1"}}, "move-3", 4.1);
    std::optional<arcoui::TransactionRecord> after_new_commit = runtime.redo();
    assert(!after_new_commit.has_value());
}

void test_point_in_polygon_diamond_and_hexagon() {
    using arcoui::Shape;
    // A 100x100 diamond: center is inside, all four corners of the bounding box are outside.
    assert(arcoui::point_in_shape(Shape::Diamond, 100, 100, {}, 50, 50));
    assert(!arcoui::point_in_shape(Shape::Diamond, 100, 100, {}, 0, 0));
    assert(!arcoui::point_in_shape(Shape::Diamond, 100, 100, {}, 100, 100));
    assert(!arcoui::point_in_shape(Shape::Diamond, 100, 100, {}, 100, 0));

    // A 100x100 hexagon: center inside, far corner outside.
    assert(arcoui::point_in_shape(Shape::Hexagon, 100, 100, {}, 50, 50));
    assert(!arcoui::point_in_shape(Shape::Hexagon, 100, 100, {}, 0, 0));

    // Rectangle sanity check against the same API.
    assert(arcoui::point_in_shape(Shape::Rectangle, 100, 100, {}, 1, 1));
    assert(!arcoui::point_in_shape(Shape::Rectangle, 100, 100, {}, -1, 50));
}

void test_gesture_throw_requires_release_velocity() {
    arcoui::GestureRecognizer recognizer;
    recognizer.max_duration = 1.0;
    recognizer.min_amplitude = 30.0;
    recognizer.velocity_threshold = 200.0;
    recognizer.min_reversals = 2;

    // A real shake-then-throw: two direction reversals on x, big enough amplitude, fast release.
    recognizer.feed_press(0, 0, 0.0);
    recognizer.feed_move(40, 0, 0.1);   // right
    recognizer.feed_move(-40, 0, 0.2);  // reversal 1 (left)
    recognizer.feed_move(40, 0, 0.3);   // reversal 2 (right)
    // Fast final flick: 60 units in 0.02s = 3000 units/sec, well above threshold.
    arcoui::GestureResult result = recognizer.feed_release(100, 0, 0.32);
    assert(result == arcoui::GestureResult::Throw);

    // Same path, but released slowly (low velocity) -- must NOT resolve as a throw.
    recognizer.reset();
    recognizer.feed_press(0, 0, 0.0);
    recognizer.feed_move(40, 0, 0.1);
    recognizer.feed_move(-40, 0, 0.2);
    recognizer.feed_move(40, 0, 0.3);
    arcoui::GestureResult slow_result = recognizer.feed_release(41, 0, 0.9); // 1 unit in 0.6s
    assert(slow_result == arcoui::GestureResult::None);

    // Not enough amplitude at all -- an idle jiggle, must not resolve as a throw even if fast.
    recognizer.reset();
    recognizer.feed_press(0, 0, 0.0);
    recognizer.feed_move(2, 0, 0.01);
    arcoui::GestureResult tiny_result = recognizer.feed_release(4, 0, 0.02);
    assert(tiny_result == arcoui::GestureResult::None);
}

} // namespace

int main() {
    test_stale_handle_rejected_after_destroy();
    test_slot_reuse_does_not_alias_stale_handle();
    test_wrong_type_handle_rejected();
    test_intent_state_and_blocked_reason();
    test_semantic_and_presentation_trees_diverge();
    test_transaction_begin_update_commit();
    test_transaction_cancel_and_undo_redo();
    test_point_in_polygon_diamond_and_hexagon();
    test_gesture_throw_requires_release_velocity();

    std::cout << "arcoui_core_tests: all tests passed" << std::endl;
    return 0;
}
