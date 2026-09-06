#include "arcoui/bindings.hpp"

#include "arcoui/core.hpp"

#include "arco/runtime.hpp"
#include "arco/value.hpp"

#include <memory>
#include <stdexcept>

namespace arcoui {
namespace {

using arco::Value;

Shape shape_from_name(const std::string& name) {
    if (name == "Rectangle") return Shape::Rectangle;
    if (name == "Diamond") return Shape::Diamond;
    if (name == "Hexagon") return Shape::Hexagon;
    if (name == "Polygon") return Shape::Polygon;
    throw std::runtime_error("unknown ArcoUI shape: " + name);
}

std::string name_from_state(IntentState state) { return to_string(state); }
std::string name_from_phase(TransactionPhase phase) { return to_string(phase); }

// NULL from ArcoBASIC means "no parent" / "no owning node" throughout this binding surface -- a
// default-constructed RuntimeHandle{slot=0, generation=0, type=""} can never alias a real handle
// (RuntimeHandleTable never hands out generation 0), so it's a safe standalone "no handle" sentinel.
arco::RuntimeHandle handle_arg(const Value& value, const char* expected_type) {
    if (value.is_null()) return arco::RuntimeHandle{};
    if (!value.is_handle()) throw std::runtime_error(std::string("expected a ") + expected_type + " handle");
    return value.as_handle();
}

std::vector<Point> points_arg(const Value& value) {
    std::vector<Point> points;
    if (value.is_null()) return points;
    for (const auto& entry : value.as_array()) {
        points.push_back(Point{entry.get_property("X").as_number(), entry.get_property("Y").as_number()});
    }
    return points;
}

EventValue event_value_arg(const Value& value) {
    if (value.is_string()) return EventValue::of_text(value.to_string());
    if (value.is_null()) return EventValue::of_number(0.0);
    return EventValue::of_number(value.as_number());
}

Value event_value_to_value(const EventValue& value) {
    if (value.is_text) return Value(value.text);
    return Value(value.number);
}

std::map<std::string, std::string> string_map_arg(const Value& value) {
    std::map<std::string, std::string> result;
    if (value.is_null()) return result;
    for (const auto& [key, entry] : value.as_object()) result[key] = entry.to_string();
    return result;
}

Value string_map_to_value(const std::map<std::string, std::string>& map) {
    Value::Object object;
    for (const auto& [key, entry] : map) object[key] = Value(entry);
    return Value(object);
}

Value intent_event_to_value(const IntentEvent& event) {
    return Value::Object{
        {"Type", std::string("intent")},
        {"Target", Value(event.target)},
        {"Intent", Value(event.intent)},
        {"Phase", name_from_phase(event.phase)},
        {"Value", event_value_to_value(event.value)},
        {"Context", string_map_to_value(event.context)},
        {"Timestamp", event.timestamp},
    };
}

Value transaction_record_to_value(const TransactionRecord& record) {
    return Value::Object{
        {"Type", std::string("record")},
        {"Intent", Value(record.intent)},
        {"Label", record.label},
        {"Data", string_map_to_value(record.data)},
    };
}

Value no_record() { return Value::Object{{"Type", std::string("none")}}; }

} // namespace

void register_arcoui_functions(arco::Runtime& runtime) {
    auto core = std::make_shared<Runtime>();

    runtime.register_function("ArcoUI.CreateSurface", [core](const std::vector<Value>& args) -> Value {
        if (args.size() < 4 || args.size() > 5) {
            throw std::runtime_error("ArcoUI.CreateSurface expects title, shape, width, height, [points]");
        }
        Shape shape = shape_from_name(args[1].to_string());
        std::vector<Point> points = args.size() == 5 ? points_arg(args[4]) : std::vector<Point>{};
        if (shape == Shape::Polygon && points.size() < 3) {
            throw std::runtime_error("ArcoUI.CreateSurface: Polygon shape requires at least 3 points");
        }
        return Value(core->create_surface(args[0].to_string(), shape, args[2].as_number(), args[3].as_number(),
                                          std::move(points)));
    });

    runtime.register_function("ArcoUI.DestroySurface", [core](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("ArcoUI.DestroySurface expects a surface handle");
        return core->destroy_surface(handle_arg(args[0], "Surface"));
    });

    runtime.register_function("ArcoUI.BindSurfaceWindow", [core](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) throw std::runtime_error("ArcoUI.BindSurfaceWindow expects a surface handle and a window id");
        return core->bind_surface_window(handle_arg(args[0], "Surface"), static_cast<int>(args[1].as_number()));
    });

    runtime.register_function("ArcoUI.SurfacePointInside", [core](const std::vector<Value>& args) -> Value {
        if (args.size() != 3) throw std::runtime_error("ArcoUI.SurfacePointInside expects a surface handle, x, y");
        const Surface* target = core->surface(handle_arg(args[0], "Surface"));
        if (!target) throw std::runtime_error("invalid ArcoUI Surface handle");
        return target->point_inside(args[1].as_number(), args[2].as_number());
    });

    runtime.register_function("ArcoUI.ShapePolygon", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 4) throw std::runtime_error("ArcoUI.ShapePolygon expects shape, width, height, points");
        Shape shape = shape_from_name(args[0].to_string());
        std::vector<Point> polygon = shape_polygon(shape, args[1].as_number(), args[2].as_number(), points_arg(args[3]));
        Value::Array result;
        for (const auto& point : polygon) result.push_back(Value::Object{{"X", point.x}, {"Y", point.y}});
        return Value(result);
    });

    runtime.register_function("ArcoUI.PointInShape", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 6) {
            throw std::runtime_error("ArcoUI.PointInShape expects shape, width, height, points, x, y");
        }
        Shape shape = shape_from_name(args[0].to_string());
        return point_in_shape(shape, args[1].as_number(), args[2].as_number(), points_arg(args[3]),
                              args[4].as_number(), args[5].as_number());
    });

    runtime.register_function("ArcoUI.CreateSemanticNode", [core](const std::vector<Value>& args) -> Value {
        if (args.size() != 3) throw std::runtime_error("ArcoUI.CreateSemanticNode expects name, parent, ownerTag");
        return Value(core->create_semantic_node(args[0].to_string(), handle_arg(args[1], "SemanticNode"),
                                                args[2].to_string()));
    });

    runtime.register_function("ArcoUI.CreatePresentationNode", [core](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("ArcoUI.CreatePresentationNode expects a parent (or NULL)");
        return Value(core->create_presentation_node(handle_arg(args[0], "PresentationNode")));
    });

    runtime.register_function("ArcoUI.DefineIntent", [core](const std::vector<Value>& args) -> Value {
        if (args.size() != 3) throw std::runtime_error("ArcoUI.DefineIntent expects name, semanticNode, continuous");
        return Value(core->define_intent(args[0].to_string(), handle_arg(args[1], "SemanticNode"), args[2].truthy()));
    });

    runtime.register_function("ArcoUI.IntentInfo", [core](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("ArcoUI.IntentInfo expects an intent handle");
        const Intent* target = core->intent(handle_arg(args[0], "Intent"));
        if (!target) throw std::runtime_error("invalid ArcoUI Intent handle");
        return Value::Object{
            {"Name", target->name},
            {"State", name_from_state(target->state)},
            {"BlockedReason", target->blocked_reason},
            {"Phase", name_from_phase(target->phase)},
            {"Continuous", target->continuous},
        };
    });

    runtime.register_function("ArcoUI.SetIntentState", [core](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args.size() > 3) {
            throw std::runtime_error("ArcoUI.SetIntentState expects an intent handle, state, [blockedReason]");
        }
        IntentState state;
        const std::string name = args[1].to_string();
        if (name == "Available") state = IntentState::Available;
        else if (name == "Unavailable") state = IntentState::Unavailable;
        else if (name == "Active") state = IntentState::Active;
        else if (name == "Pending") state = IntentState::Pending;
        else if (name == "Blocked") state = IntentState::Blocked;
        else if (name == "Ambiguous") state = IntentState::Ambiguous;
        else if (name == "Completed") state = IntentState::Completed;
        else if (name == "Failed") state = IntentState::Failed;
        else throw std::runtime_error("unknown ArcoUI intent state: " + name);
        const std::string reason = args.size() == 3 ? args[2].to_string() : std::string{};
        return core->set_intent_state(handle_arg(args[0], "Intent"), state, reason);
    });

    runtime.register_function("ArcoUI.InvokeIntent", [core](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args.size() > 4) {
            throw std::runtime_error("ArcoUI.InvokeIntent expects an intent handle, timestamp, [value], [context]");
        }
        EventValue value = args.size() >= 3 ? event_value_arg(args[2]) : EventValue::of_number(0.0);
        std::map<std::string, std::string> context = args.size() == 4 ? string_map_arg(args[3]) : std::map<std::string, std::string>{};
        return core->invoke_intent(handle_arg(args[0], "Intent"), std::move(value), std::move(context), args[1].as_number());
    });

    runtime.register_function("ArcoUI.BeginTransaction", [core](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args.size() > 3) {
            throw std::runtime_error("ArcoUI.BeginTransaction expects an intent handle, timestamp, [value]");
        }
        EventValue value = args.size() == 3 ? event_value_arg(args[2]) : EventValue::of_number(0.0);
        return core->begin_transaction(handle_arg(args[0], "Intent"), std::move(value), args[1].as_number());
    });

    runtime.register_function("ArcoUI.UpdateTransaction", [core](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args.size() > 3) {
            throw std::runtime_error("ArcoUI.UpdateTransaction expects an intent handle, timestamp, [value]");
        }
        EventValue value = args.size() == 3 ? event_value_arg(args[2]) : EventValue::of_number(0.0);
        return core->update_transaction(handle_arg(args[0], "Intent"), std::move(value), args[1].as_number());
    });

    runtime.register_function("ArcoUI.CommitTransaction", [core](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args.size() > 4) {
            throw std::runtime_error("ArcoUI.CommitTransaction expects an intent handle, timestamp, [data], [label]");
        }
        std::map<std::string, std::string> data = args.size() >= 3 ? string_map_arg(args[2]) : std::map<std::string, std::string>{};
        std::string label = args.size() == 4 ? args[3].to_string() : std::string{};
        return core->commit_transaction(handle_arg(args[0], "Intent"), std::move(data), label, args[1].as_number());
    });

    runtime.register_function("ArcoUI.CancelTransaction", [core](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) throw std::runtime_error("ArcoUI.CancelTransaction expects an intent handle, timestamp");
        return core->cancel_transaction(handle_arg(args[0], "Intent"), args[1].as_number());
    });

    runtime.register_function("ArcoUI.Undo", [core](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("ArcoUI.Undo expects no arguments");
        std::optional<TransactionRecord> record = core->undo();
        return record ? transaction_record_to_value(*record) : no_record();
    });

    runtime.register_function("ArcoUI.Redo", [core](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("ArcoUI.Redo expects no arguments");
        std::optional<TransactionRecord> record = core->redo();
        return record ? transaction_record_to_value(*record) : no_record();
    });

    runtime.register_function("ArcoUI.PollIntentEvent", [core](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("ArcoUI.PollIntentEvent expects no arguments");
        std::optional<IntentEvent> event = core->poll_event();
        return event ? intent_event_to_value(*event) : Value::Object{{"Type", std::string("none")}};
    });

    runtime.register_function("ArcoUI.CreateGesture", [core](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("ArcoUI.CreateGesture expects no arguments");
        return Value(core->create_gesture_recognizer());
    });

    runtime.register_function("ArcoUI.DestroyGesture", [core](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("ArcoUI.DestroyGesture expects a gesture handle");
        return core->destroy_gesture_recognizer(handle_arg(args[0], "Gesture"));
    });

    runtime.register_function("ArcoUI.ConfigureGesture", [core](const std::vector<Value>& args) -> Value {
        if (args.size() != 5) {
            throw std::runtime_error("ArcoUI.ConfigureGesture expects a gesture handle, maxDuration, minAmplitude, "
                                     "velocityThreshold, minReversals");
        }
        GestureRecognizer* recognizer = core->gesture_recognizer(handle_arg(args[0], "Gesture"));
        if (!recognizer) throw std::runtime_error("invalid ArcoUI Gesture handle");
        recognizer->max_duration = args[1].as_number();
        recognizer->min_amplitude = args[2].as_number();
        recognizer->velocity_threshold = args[3].as_number();
        recognizer->min_reversals = static_cast<int>(args[4].as_number());
        return true;
    });

    runtime.register_function("ArcoUI.GesturePress", [core](const std::vector<Value>& args) -> Value {
        if (args.size() != 4) throw std::runtime_error("ArcoUI.GesturePress expects a gesture handle, x, y, timestamp");
        GestureRecognizer* recognizer = core->gesture_recognizer(handle_arg(args[0], "Gesture"));
        if (!recognizer) throw std::runtime_error("invalid ArcoUI Gesture handle");
        recognizer->feed_press(args[1].as_number(), args[2].as_number(), args[3].as_number());
        return true;
    });

    runtime.register_function("ArcoUI.GestureMove", [core](const std::vector<Value>& args) -> Value {
        if (args.size() != 4) throw std::runtime_error("ArcoUI.GestureMove expects a gesture handle, x, y, timestamp");
        GestureRecognizer* recognizer = core->gesture_recognizer(handle_arg(args[0], "Gesture"));
        if (!recognizer) throw std::runtime_error("invalid ArcoUI Gesture handle");
        recognizer->feed_move(args[1].as_number(), args[2].as_number(), args[3].as_number());
        return true;
    });

    // Returns "Throw" or "None" -- a plain string rather than a boolean so this vocabulary can
    // grow (e.g. a future Flick distinct from Throw) without changing every caller's shape.
    runtime.register_function("ArcoUI.GestureRelease", [core](const std::vector<Value>& args) -> Value {
        if (args.size() != 4) throw std::runtime_error("ArcoUI.GestureRelease expects a gesture handle, x, y, timestamp");
        GestureRecognizer* recognizer = core->gesture_recognizer(handle_arg(args[0], "Gesture"));
        if (!recognizer) throw std::runtime_error("invalid ArcoUI Gesture handle");
        GestureResult result = recognizer->feed_release(args[1].as_number(), args[2].as_number(), args[3].as_number());
        return std::string(result == GestureResult::Throw ? "Throw" : "None");
    });

    runtime.register_function("ArcoUI.HandleValid", [core](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("ArcoUI.HandleValid expects one handle");
        if (!args[0].is_handle()) return false;
        return core->valid(args[0].as_handle());
    });
}

} // namespace arcoui
