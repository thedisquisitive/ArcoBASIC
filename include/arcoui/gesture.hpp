#pragma once

// Flick/Throw recognizer -- pure geometry/velocity math over a short pointer-sample history, with
// no knowledge of windows, widgets, or any particular surface. RFC-ArcoUI section 10.4/45
// deliberately requires this NOT be embedded inside one title-bar widget; any surface (a window's
// drag region, a card, a notification) feeds it samples and asks what happened on release.
//
// Recognition grammar (RFC section 10.4's "shake -> release/throw -> gone" reading):
//   grab (feed_press) -> some number of rapid direction reversals -> a minimum movement amplitude
//   -> release while still moving above a velocity threshold, all within a maximum duration.
// The critical condition -- and the one that distinguishes a real throw from an idle wobble -- is
// release *while still moving fast*, so feed_release() always looks at the velocity between the
// last two samples, not just whether the total path was long enough.

#include <vector>

namespace arcoui {

struct GestureSample {
    double x = 0.0;
    double y = 0.0;
    double t = 0.0; // seconds, monotonic, caller-supplied clock
};

enum class GestureResult {
    None,
    Throw,
};

class GestureRecognizer {
public:
    // Tunables -- defaults are deliberately generous (see class doc); callers tune per surface
    // (e.g. touchscreen vs. mouse) rather than this recognizer assuming one input device.
    double max_duration = 0.8;          // seconds from press to release
    double min_amplitude = 40.0;        // max displacement from the press point, in caller units
    double velocity_threshold = 300.0;  // release velocity, caller units/second
    int min_reversals = 2;              // direction-sign changes on the dominant axis required

    void reset();
    void feed_press(double x, double y, double t);
    void feed_move(double x, double y, double t);
    // Evaluates the whole gesture against the tunables above and resets internal state either way.
    GestureResult feed_release(double x, double y, double t);

    bool active() const { return active_; }

private:
    bool active_ = false;
    double start_t_ = 0.0;
    std::vector<GestureSample> samples_;
};

} // namespace arcoui
