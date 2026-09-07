#pragma once

// RFC section 14: "All frontends consume events rather than parsing human CLI output." This is a
// deliberately plain, synchronous, single-process event bus -- no threading, no queueing, no
// serialization. The CLI renderer (apps/fissure) is just one subscriber; a future TUI/ARCADE/CI
// frontend (RFC section 20, M6) subscribes the same way instead of scraping stdout.

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace fissure {

// The RFC's own event list (section 14), verbatim. ConfidenceChanged/CalibrationRequired are
// declared now so M5's later work never needs to touch this enum, even though nothing emits them
// yet (see AGENT_PROGRESS.md's Session 1 design-decision #5).
enum class EventType {
    DisturbanceDetected,
    GraphUpdated,
    ImpactComputed,
    ProbeSelected,
    ProbeSkipped,
    ProbeStarted,
    ProbePassed,
    ProbeFailed,
    ConfidenceChanged,
    CalibrationRequired,
    RunCompleted,
};

std::string to_string(EventType type);

struct Event {
    EventType type;
    // Free-form key/value payload -- deliberately not a tagged union of per-event-type structs.
    // A generic string map keeps every subscriber (current: the CLI renderer; future: TUI/ARCADE/
    // CI/JSON) decoupled from the exact field set of each event kind, matching the RFC's own
    // "structured events... permits later CLI/TUI/ARCADE/CI/JSON/dashboard" framing (section 14)
    // without a new payload type per frontend.
    std::map<std::string, std::string> fields;
};

class EventBus {
public:
    using Subscriber = std::function<void(const Event&)>;

    void subscribe(Subscriber subscriber);
    void emit(Event event) const;
    void emit(EventType type, std::map<std::string, std::string> fields = {}) const;

private:
    std::vector<Subscriber> subscribers_;
};

} // namespace fissure
