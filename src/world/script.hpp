// Running trigger scripts: which event triggers the player sets off, and
// stepping through a cutscene script's commands. What each command does is
// up to a host (the game view). Notes and evidence: docs/formats/script.md,
// "Running a script".
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "formats/chk.hpp"
#include "formats/gsc.hpp"

namespace harbinger::world {

// FUN_0048b390: true if a move from (x0, y0) to (x1, y1) starts inside the
// circle at (cx, cy) with radius r, or crosses its edge on the way.
bool entersCircle(double x0, double y0, double x1, double y1, double cx, double cy, double r);

// The level's event triggers and which of them have fired. Each fires once
// (FUN_0048c2c0 marks it fired, state 2; state 3, "re-arm once outside",
// is not set by anything the rebuild runs).
class TriggerWatch {
public:
    explicit TriggerWatch(const chk::Level& level);
    // FUN_0051afa0: the player moved from (x0, y0) to (x1, y1) this frame;
    // returns the triggers this sets off, in file order.
    std::vector<const chk::EventTrigger*> step(double x0, double y0, double x1, double y1);

private:
    const chk::Level& level_;
    std::vector<bool> fired_;
};

// What a command did this frame, as the handlers return it to
// FUN_004fde20: still running (call again next frame), done, or skipped
// (the target is missing); the runner goes on to the next command in the
// same frame after done or skipped.
enum class CommandResult { Running, Done, Skip };

// One script at a time (DAT_00584730 holds the running one at +0x40;
// starting another replaces it).
class ScriptRunner {
public:
    // `first` is true on a command's first call (its phase 0).
    using Host = std::function<CommandResult(const gsc::Line& line, bool first, uint64_t nowMs)>;

    void start(std::shared_ptr<const gsc::Script> script);
    void stop() { script_.reset(); }
    bool running() const { return script_ != nullptr; }
    // FUN_00502560 / FUN_004fd8a0: runs the current command and those after
    // it that finish at once. Returns true on the frame the script ends.
    bool update(const Host& host, uint64_t nowMs);
    size_t line() const { return next_; }  // index of the current command

private:
    std::shared_ptr<const gsc::Script> script_;
    size_t next_ = 0;
    bool started_ = false;  // the current command has had its first call
};

}  // namespace harbinger::world
