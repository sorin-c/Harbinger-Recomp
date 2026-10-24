#include "world/script.hpp"

#include <cmath>

namespace harbinger::world {

bool entersCircle(double x0, double y0, double x1, double y1, double cx, double cy, double r) {
    const double dx = x1 - x0, dy = y1 - y0;
    const double ox = x0 - cx, oy = y0 - cy;
    const double c = ox * ox + oy * oy - r * r;
    if (c < 0) return true;  // starts inside
    if (dx == 0 && dy == 0) return false;
    // Where |start + t * d - centre| = r, for t in [0, 1].
    const double a = dx * dx + dy * dy, b = 2 * (ox * dx + oy * dy);
    const double disc = b * b - 4 * a * c;
    if (disc < 0) return false;
    const double root = std::sqrt(disc) / (2 * a), mid = -b / (2 * a);
    const double t0 = mid - root, t1 = mid + root;
    return (t0 >= 0 && t0 <= 1) || (t1 >= 0 && t1 <= 1);
}

TriggerWatch::TriggerWatch(const chk::Level& level) : level_(level), fired_(level.eventTriggers.size(), false) {}

std::vector<const chk::EventTrigger*> TriggerWatch::step(double x0, double y0, double x1, double y1) {
    std::vector<const chk::EventTrigger*> out;
    for (size_t i = 0; i < fired_.size(); ++i) {
        const chk::EventTrigger& t = level_.eventTriggers[i];
        if (fired_[i] || !entersCircle(x0, y0, x1, y1, t.x, t.y, t.radius)) continue;
        fired_[i] = true;
        out.push_back(&t);
    }
    return out;
}

void ScriptRunner::start(std::shared_ptr<const gsc::Script> script) {
    // FUN_004fd890 / FUN_004fdca0: from the first command, every phase 0.
    script_ = std::move(script);
    next_ = 0;
    started_ = false;
}

bool ScriptRunner::update(const Host& host, uint64_t nowMs) {
    if (!script_) return false;
    while (next_ < script_->lines.size()) {
        const bool first = !started_;
        started_ = true;
        const CommandResult r = host(script_->lines[next_], first, nowMs);
        if (!script_) return false;  // the host stopped it
        if (r == CommandResult::Running) return false;
        ++next_;
        started_ = false;
    }
    script_.reset();
    return true;
}

}  // namespace harbinger::world
