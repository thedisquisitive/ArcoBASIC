#include "rivet/action.hpp"

namespace rivet {

std::string to_string(ActionType type) {
    switch (type) {
        case ActionType::Compile: return "Compile";
        case ActionType::Link: return "Link";
    }
    return "Compile";
}

std::string to_string(ActionStatus status) {
    switch (status) {
        case ActionStatus::CacheHit: return "CacheHit";
        case ActionStatus::Ran: return "Ran";
        case ActionStatus::Failed: return "Failed";
        case ActionStatus::SkippedDueToFailure: return "SkippedDueToFailure";
    }
    return "Ran";
}

} // namespace rivet
