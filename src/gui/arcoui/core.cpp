#include "arcoui/core.hpp"

#include <memory>

namespace arcoui {

namespace {
constexpr const char* kSurfaceType = "ArcoUI.Surface";
constexpr const char* kSemanticType = "ArcoUI.SemanticNode";
constexpr const char* kPresentationType = "ArcoUI.PresentationNode";
constexpr const char* kIntentType = "ArcoUI.Intent";
constexpr const char* kGestureType = "ArcoUI.Gesture";
} // namespace

std::string to_string(IntentState state) {
    switch (state) {
        case IntentState::Available: return "Available";
        case IntentState::Unavailable: return "Unavailable";
        case IntentState::Active: return "Active";
        case IntentState::Pending: return "Pending";
        case IntentState::Blocked: return "Blocked";
        case IntentState::Ambiguous: return "Ambiguous";
        case IntentState::Completed: return "Completed";
        case IntentState::Failed: return "Failed";
    }
    return "Unavailable";
}

std::string to_string(TransactionPhase phase) {
    switch (phase) {
        case TransactionPhase::Idle: return "Idle";
        case TransactionPhase::Began: return "Began";
        case TransactionPhase::Updating: return "Updating";
        case TransactionPhase::Committed: return "Committed";
        case TransactionPhase::Cancelled: return "Cancelled";
    }
    return "Idle";
}

// --- Surfaces ------------------------------------------------------------

arco::RuntimeHandle Runtime::create_surface(const std::string& title, Shape shape, double width, double height,
                                            std::vector<Point> custom_polygon) {
    auto surface = std::make_shared<Surface>();
    surface->title = title;
    surface->shape = shape;
    surface->width = width;
    surface->height = height;
    surface->custom_polygon = std::move(custom_polygon);
    return handles_.create(kSurfaceType, surface);
}

bool Runtime::destroy_surface(const arco::RuntimeHandle& handle) {
    return handles_.destroy(handle);
}

Surface* Runtime::surface(const arco::RuntimeHandle& handle) {
    return static_cast<Surface*>(handles_.object(handle, kSurfaceType).get());
}

const Surface* Runtime::surface(const arco::RuntimeHandle& handle) const {
    return static_cast<const Surface*>(handles_.object(handle, kSurfaceType).get());
}

bool Runtime::bind_surface_window(const arco::RuntimeHandle& handle, int backend_window_id) {
    Surface* target = surface(handle);
    if (!target) return false;
    target->backend_window_id = backend_window_id;
    return true;
}

// --- Semantic tree ---------------------------------------------------------

arco::RuntimeHandle Runtime::create_semantic_node(const std::string& name, const arco::RuntimeHandle& parent,
                                                  const std::string& owner_tag) {
    auto node = std::make_shared<SemanticNode>();
    node->name = name;
    node->owner_tag = owner_tag;
    node->parent = parent;
    arco::RuntimeHandle created = handles_.create(kSemanticType, node);
    if (SemanticNode* parent_node = semantic_node(parent)) {
        parent_node->children.push_back(created);
    }
    return created;
}

SemanticNode* Runtime::semantic_node(const arco::RuntimeHandle& handle) {
    return static_cast<SemanticNode*>(handles_.object(handle, kSemanticType).get());
}

const SemanticNode* Runtime::semantic_node(const arco::RuntimeHandle& handle) const {
    return static_cast<const SemanticNode*>(handles_.object(handle, kSemanticType).get());
}

// --- Presentation tree -------------------------------------------------

arco::RuntimeHandle Runtime::create_presentation_node(const arco::RuntimeHandle& parent) {
    auto node = std::make_shared<PresentationNode>();
    node->parent = parent;
    arco::RuntimeHandle created = handles_.create(kPresentationType, node);
    if (PresentationNode* parent_node = presentation_node(parent)) {
        parent_node->children.push_back(created);
    }
    return created;
}

PresentationNode* Runtime::presentation_node(const arco::RuntimeHandle& handle) {
    return static_cast<PresentationNode*>(handles_.object(handle, kPresentationType).get());
}

const PresentationNode* Runtime::presentation_node(const arco::RuntimeHandle& handle) const {
    return static_cast<const PresentationNode*>(handles_.object(handle, kPresentationType).get());
}

// --- Intents -----------------------------------------------------------

arco::RuntimeHandle Runtime::define_intent(const std::string& name, const arco::RuntimeHandle& semantic_node_handle,
                                           bool continuous) {
    auto new_intent = std::make_shared<Intent>();
    new_intent->name = name;
    new_intent->semantic_node = semantic_node_handle;
    new_intent->continuous = continuous;
    return handles_.create(kIntentType, new_intent);
}

Intent* Runtime::intent(const arco::RuntimeHandle& handle) {
    return static_cast<Intent*>(handles_.object(handle, kIntentType).get());
}

const Intent* Runtime::intent(const arco::RuntimeHandle& handle) const {
    return static_cast<const Intent*>(handles_.object(handle, kIntentType).get());
}

bool Runtime::set_intent_state(const arco::RuntimeHandle& handle, IntentState state, const std::string& blocked_reason) {
    Intent* target = intent(handle);
    if (!target) return false;
    target->state = state;
    target->blocked_reason = (state == IntentState::Blocked || state == IntentState::Unavailable) ? blocked_reason : std::string{};
    return true;
}

bool Runtime::invoke_intent(const arco::RuntimeHandle& handle, EventValue value, std::map<std::string, std::string> context,
                            double timestamp) {
    Intent* target = intent(handle);
    if (!target || target->continuous || target->state != IntentState::Available) return false;
    push_event(target->semantic_node, handle, TransactionPhase::Committed, std::move(value), std::move(context), timestamp);
    return true;
}

// --- Transactions --------------------------------------------------------

bool Runtime::begin_transaction(const arco::RuntimeHandle& handle, EventValue value, double timestamp) {
    Intent* target = intent(handle);
    if (!target || !target->continuous || target->state != IntentState::Available) return false;
    target->state = IntentState::Active;
    target->phase = TransactionPhase::Began;
    push_event(target->semantic_node, handle, TransactionPhase::Began, std::move(value), {}, timestamp);
    return true;
}

bool Runtime::update_transaction(const arco::RuntimeHandle& handle, EventValue value, double timestamp) {
    Intent* target = intent(handle);
    if (!target || !target->continuous) return false;
    if (target->phase != TransactionPhase::Began && target->phase != TransactionPhase::Updating) return false;
    target->phase = TransactionPhase::Updating;
    push_event(target->semantic_node, handle, TransactionPhase::Updating, std::move(value), {}, timestamp);
    return true;
}

bool Runtime::commit_transaction(const arco::RuntimeHandle& handle, std::map<std::string, std::string> record_data,
                                 const std::string& label, double timestamp) {
    Intent* target = intent(handle);
    if (!target || !target->continuous) return false;
    if (target->phase != TransactionPhase::Began && target->phase != TransactionPhase::Updating) return false;

    push_event(target->semantic_node, handle, TransactionPhase::Committed, EventValue{}, {}, timestamp);

    undo_stack_.push_back(TransactionRecord{handle, label, std::move(record_data)});
    redo_stack_.clear(); // a fresh commit invalidates any redo history, standard undo/redo semantics

    target->phase = TransactionPhase::Idle;
    target->state = IntentState::Available; // ready to run again (a continuous intent is repeatable)
    return true;
}

bool Runtime::cancel_transaction(const arco::RuntimeHandle& handle, double timestamp) {
    Intent* target = intent(handle);
    if (!target || !target->continuous) return false;
    if (target->phase != TransactionPhase::Began && target->phase != TransactionPhase::Updating) return false;

    push_event(target->semantic_node, handle, TransactionPhase::Cancelled, EventValue{}, {}, timestamp);

    target->phase = TransactionPhase::Idle;
    target->state = IntentState::Available;
    return true;
}

std::optional<TransactionRecord> Runtime::undo() {
    if (undo_stack_.empty()) return std::nullopt;
    TransactionRecord record = undo_stack_.back();
    undo_stack_.pop_back();
    redo_stack_.push_back(record);
    return record;
}

std::optional<TransactionRecord> Runtime::redo() {
    if (redo_stack_.empty()) return std::nullopt;
    TransactionRecord record = redo_stack_.back();
    redo_stack_.pop_back();
    undo_stack_.push_back(record);
    return record;
}

// --- Event queue -----------------------------------------------------------

void Runtime::push_event(arco::RuntimeHandle target, arco::RuntimeHandle intent_handle, TransactionPhase phase,
                         EventValue value, std::map<std::string, std::string> context, double timestamp) {
    event_queue_.push_back(IntentEvent{std::move(target), std::move(intent_handle), phase, std::move(value),
                                       std::move(context), timestamp});
}

std::optional<IntentEvent> Runtime::poll_event() {
    if (event_queue_.empty()) return std::nullopt;
    IntentEvent event = event_queue_.front();
    event_queue_.pop_front();
    return event;
}

// --- Gesture recognizers -------------------------------------------------

arco::RuntimeHandle Runtime::create_gesture_recognizer() {
    return handles_.create(kGestureType, std::make_shared<GestureRecognizer>());
}

bool Runtime::destroy_gesture_recognizer(const arco::RuntimeHandle& handle) {
    return handles_.destroy(handle);
}

GestureRecognizer* Runtime::gesture_recognizer(const arco::RuntimeHandle& handle) {
    return static_cast<GestureRecognizer*>(handles_.object(handle, kGestureType).get());
}

// --- Handle introspection ----------------------------------------------

bool Runtime::valid(const arco::RuntimeHandle& handle, const std::string& expected_type) const {
    return handles_.valid(handle, expected_type);
}

} // namespace arcoui
