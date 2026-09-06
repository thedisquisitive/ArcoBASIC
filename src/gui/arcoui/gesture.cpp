#include "arcoui/gesture.hpp"

#include <cmath>

namespace arcoui {
namespace {

int sign(double value, double epsilon) {
    if (value > epsilon) return 1;
    if (value < -epsilon) return -1;
    return 0;
}

// Counts sign changes of consecutive-sample deltas along whichever axis (x or y) covered more
// total distance -- the "dominant axis" -- so a mostly-horizontal shake isn't diluted by tiny
// vertical jitter and vice versa. Near-zero deltas (below `epsilon`) don't count as a direction at
// all, so they can't manufacture a spurious reversal between two truly-stationary samples.
int count_reversals(const std::vector<GestureSample>& samples) {
    if (samples.size() < 3) return 0;

    double dx_total = 0.0;
    double dy_total = 0.0;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        dx_total += std::abs(samples[i].x - samples[i - 1].x);
        dy_total += std::abs(samples[i].y - samples[i - 1].y);
    }
    const bool use_x = dx_total >= dy_total;
    const double epsilon = 0.5; // caller-unit noise floor

    int reversals = 0;
    int last_sign = 0;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        const double delta = use_x ? (samples[i].x - samples[i - 1].x) : (samples[i].y - samples[i - 1].y);
        const int current_sign = sign(delta, epsilon);
        if (current_sign == 0) continue;
        if (last_sign != 0 && current_sign != last_sign) ++reversals;
        last_sign = current_sign;
    }
    return reversals;
}

double max_amplitude(const std::vector<GestureSample>& samples) {
    if (samples.empty()) return 0.0;
    const GestureSample& origin = samples.front();
    double best = 0.0;
    for (const auto& sample : samples) {
        const double dx = sample.x - origin.x;
        const double dy = sample.y - origin.y;
        best = std::max(best, std::sqrt(dx * dx + dy * dy));
    }
    return best;
}

} // namespace

void GestureRecognizer::reset() {
    active_ = false;
    start_t_ = 0.0;
    samples_.clear();
}

void GestureRecognizer::feed_press(double x, double y, double t) {
    active_ = true;
    start_t_ = t;
    samples_.clear();
    samples_.push_back({x, y, t});
}

void GestureRecognizer::feed_move(double x, double y, double t) {
    if (!active_) return;
    samples_.push_back({x, y, t});
}

GestureResult GestureRecognizer::feed_release(double x, double y, double t) {
    if (!active_) return GestureResult::None;
    samples_.push_back({x, y, t});
    active_ = false;

    const double duration = t - start_t_;
    if (duration <= 0.0 || duration > max_duration) {
        reset();
        return GestureResult::None;
    }
    if (max_amplitude(samples_) < min_amplitude) {
        reset();
        return GestureResult::None;
    }
    if (count_reversals(samples_) < min_reversals) {
        reset();
        return GestureResult::None;
    }

    // Release velocity: the whole point of a Throw vs. an idle wobble (RFC-ArcoUI section 10.4).
    // Computed from the last two samples rather than an average over the whole gesture, so a
    // gesture that decelerated to a stop before release correctly fails to qualify.
    const GestureSample& last = samples_[samples_.size() - 1];
    const GestureSample& prev = samples_[samples_.size() - 2];
    const double dt = last.t - prev.t;
    GestureResult result = GestureResult::None;
    if (dt > 0.0) {
        const double dx = last.x - prev.x;
        const double dy = last.y - prev.y;
        const double release_velocity = std::sqrt(dx * dx + dy * dy) / dt;
        if (release_velocity >= velocity_threshold) result = GestureResult::Throw;
    }

    reset();
    return result;
}

} // namespace arcoui
