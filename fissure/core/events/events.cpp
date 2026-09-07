#include "fissure/events.hpp"

namespace fissure {

std::string to_string(EventType type) {
    switch (type) {
        case EventType::DisturbanceDetected: return "DisturbanceDetected";
        case EventType::GraphUpdated: return "GraphUpdated";
        case EventType::ImpactComputed: return "ImpactComputed";
        case EventType::ProbeSelected: return "ProbeSelected";
        case EventType::ProbeSkipped: return "ProbeSkipped";
        case EventType::ProbeStarted: return "ProbeStarted";
        case EventType::ProbePassed: return "ProbePassed";
        case EventType::ProbeFailed: return "ProbeFailed";
        case EventType::ConfidenceChanged: return "ConfidenceChanged";
        case EventType::CalibrationRequired: return "CalibrationRequired";
        case EventType::RunCompleted: return "RunCompleted";
    }
    return "Unknown";
}

void EventBus::subscribe(Subscriber subscriber) {
    subscribers_.push_back(std::move(subscriber));
}

void EventBus::emit(Event event) const {
    for (const auto& subscriber : subscribers_) subscriber(event);
}

void EventBus::emit(EventType type, std::map<std::string, std::string> fields) const {
    emit(Event{type, std::move(fields)});
}

} // namespace fissure
