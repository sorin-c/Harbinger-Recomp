// Cutscene scripts (.gsc): plain text, one command per line, run by event
// triggers (chk::EventTrigger::script names one through the character's
// script manager, [Human|Gladiator|Culibine] Scripts). Format notes and
// evidence: docs/formats/script.md
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace harbinger::gsc {

// The command names table at 0x557554 (32 bytes each); the index is also the
// slot in the command factory table at 0x5846d8.
enum class Command : uint8_t {
    Cinematic, PositionCamera, MoveTo, TurnToward, Highlight, ActivateGizmo, Speech, Attack,
    ActivateTrigger, Lock, PlayerSight, Sound, Wait, Shake, SwapLevels, Die, ThoughtBubble, Warp,
    QuitGame, SetLevel, Snapshot, StrikePose,
    Count,
};

const char* name(Command c);

struct Arg {
    enum Kind : uint8_t { Number, String, Word } kind = Number;
    double number = 0;  // Number
    std::string text;   // String (quotes removed, \n turned into a line break) or Word
};

struct Line {
    Command command = Command::Count;
    std::vector<Arg> args;
    int lineNumber = 0;  // in the file, from 1

    // The argument at `i` as a number, string or word; throws if it is missing
    // or of another kind.
    double number(size_t i) const;
    const std::string& text(size_t i) const;
    // A trailing Wait / NoWait flag (FUN_004fe060): true for Wait.
    bool waits() const;
};

struct Script {
    double version = 0;  // "Version 0.0" or "Version 0.1" in the shipped files
    std::vector<Line> lines;
};

// Throws std::runtime_error naming the line on malformed text, an unknown
// command, or arguments that do not match the command's parse function.
Script parse(std::string_view text);

}  // namespace harbinger::gsc
