#include "formats/gsc.hpp"

#include <cstdlib>
#include <stdexcept>

namespace harbinger::gsc {

namespace {

constexpr const char* kNames[] = {
    "Cinematic", "PositionCamera", "MoveTo", "TurnToward", "Highlight", "ActivateGizmo", "Speech",
    "Attack", "ActivateTrigger", "Lock", "PlayerSight", "Sound", "Wait", "Shake", "SwapLevels", "Die",
    "ThoughtBubble", "Warp", "QuitGame", "SetLevel", "Snapshot", "StrikePose",
};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == size_t(Command::Count));

// Argument kinds each command's text parse (vtable slot 6) reads, in order:
// n = number (FUN_004fdeb0, 4fdf30, 4fdfb0), s = quoted string (FUN_004fe030),
// w = word (Wait/NoWait FUN_004fe060, On/Off FUN_004fe080, Visible FUN_004fe0a0,
// Range FUN_004fe0c0, or a name compared in place). Lowercase letters after
// '|' are optional. nullptr: not used by any shipped script, so not checked.
const char* signature(Command c, double version) {
    switch (c) {
    case Command::Cinematic: return "ww";             // 0x501f90
    case Command::PositionCamera: return "nnnw";      // 0x500f70
    case Command::MoveTo: return "nnnw";              // 0x4fed40
    case Command::TurnToward: return "nnnw";          // 0x4ff390
    case Command::Highlight: return "wnw";            // 0x502170
    case Command::ActivateGizmo: return "nw";         // 0x500020
    case Command::Attack: return "nnnww";             // 0x4feb90
    case Command::Sound: return "nw";                 // 0x501710
    case Command::Wait: return "n";                   // 0x501120
    case Command::Shake: return "nnnwww";             // 0x501310
    case Command::ThoughtBubble:                      // 0x4ff980: a voice id from 0.1
        return version >= 0.1 ? "snnnnn|w" : "snnnn";
    case Command::QuitGame: return "";                // 0x4d5080
    case Command::SetLevel: return "n";               // 0x501850
    case Command::Snapshot: return "w";               // 0x5024f0
    case Command::StrikePose: return "nwnn";          // 0x4ffc60
    default: return nullptr;
    }
}

[[noreturn]] void fail(int line, const std::string& what) {
    throw std::runtime_error("gsc line " + std::to_string(line) + ": " + what);
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::vector<Arg> parseArgs(std::string_view s, int line) {
    std::vector<Arg> args;
    size_t i = 0;
    auto skipSpace = [&] { while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i; };
    skipSpace();
    if (i == s.size()) return args;
    for (;;) {
        skipSpace();
        Arg a;
        if (i < s.size() && s[i] == '"') {
            a.kind = Arg::String;
            for (++i;; ++i) {
                if (i >= s.size()) fail(line, "unterminated string");
                if (s[i] == '"') break;
                if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
                    a.text += '\n';
                    ++i;
                } else {
                    a.text += s[i];
                }
            }
            ++i;
            skipSpace();
        } else {
            const size_t end = s.find(',', i);
            const std::string_view tok = trim(s.substr(i, end == std::string_view::npos ? s.size() - i : end - i));
            if (tok.empty()) fail(line, "empty argument");
            i = end == std::string_view::npos ? s.size() : end;
            const std::string t(tok);
            char* stop = nullptr;
            const double v = std::strtod(t.c_str(), &stop);
            if (stop && *stop == '\0') {
                a.kind = Arg::Number;
                a.number = v;
            } else {
                a.kind = Arg::Word;
            }
            a.text = t;
        }
        args.push_back(std::move(a));
        if (i == s.size()) break;
        if (s[i] != ',') fail(line, "expected ','");
        ++i;
    }
    return args;
}

void check(const Line& l, double version) {
    const char* sig = signature(l.command, version);
    if (!sig) return;
    size_t k = 0;
    bool optional = false;
    for (const char* p = sig; *p; ++p) {
        if (*p == '|') {
            optional = true;
            continue;
        }
        if (k >= l.args.size()) {
            if (optional) break;
            fail(l.lineNumber, std::string(name(l.command)) + ": too few arguments");
        }
        const Arg::Kind want = *p == 'n' ? Arg::Number : *p == 's' ? Arg::String : Arg::Word;
        if (l.args[k].kind != want) fail(l.lineNumber, std::string(name(l.command)) + ": argument kind");
        ++k;
    }
    if (k != l.args.size()) fail(l.lineNumber, std::string(name(l.command)) + ": too many arguments");
}

}  // namespace

const char* name(Command c) { return c < Command::Count ? kNames[size_t(c)] : "?"; }

double Line::number(size_t i) const {
    if (i >= args.size() || args[i].kind != Arg::Number) fail(lineNumber, "expected a number");
    return args[i].number;
}

const std::string& Line::text(size_t i) const {
    if (i >= args.size() || args[i].kind == Arg::Number) fail(lineNumber, "expected a string or word");
    return args[i].text;
}

bool Line::waits() const { return !args.empty() && args.back().kind == Arg::Word && args.back().text == "Wait"; }

Script parse(std::string_view text) {
    Script s;
    bool haveVersion = false;
    int lineNumber = 0;
    // FUN_004fda40: the first line is "Version <number>", then one command
    // per line, the name up to '(' looked up in the names table.
    while (!text.empty()) {
        const size_t nl = text.find('\n');
        const std::string_view raw = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        ++lineNumber;
        const std::string_view l = trim(raw);
        if (l.empty()) continue;
        if (!haveVersion) {
            if (l.substr(0, 8) != "Version ") fail(lineNumber, "expected Version");
            s.version = std::strtod(std::string(l.substr(8)).c_str(), nullptr);
            haveVersion = true;
            continue;
        }
        const size_t open = l.find('(');
        if (open == std::string_view::npos || l.back() != ')') fail(lineNumber, "expected Command(...)");
        const std::string_view cmd = trim(l.substr(0, open));
        Line line;
        line.lineNumber = lineNumber;
        for (size_t i = 0; i < size_t(Command::Count); ++i)
            if (cmd == kNames[i]) line.command = Command(i);
        if (line.command == Command::Count) fail(lineNumber, "unknown command " + std::string(cmd));
        line.args = parseArgs(l.substr(open + 1, l.size() - open - 2), lineNumber);
        check(line, s.version);
        s.lines.push_back(std::move(line));
    }
    if (!haveVersion) fail(lineNumber, "empty script");
    return s;
}

}  // namespace harbinger::gsc
