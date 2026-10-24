// Agent templates (.agt): characters, monsters, NPCs, mines and cameras, and
// their animation lists. Format notes and evidence: docs/formats/agent.md
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace harbinger::agt {

// The class index (first word of the file) picks the engine class from the
// factory table at 0x5838d0. Named classes the rebuild cares about so far:
enum Class : uint16_t {
    Player = 0,          // GPlayer: dude.agt, gladiator.agt, culibine.agt (not in agent.mgr)
    HumanNPC = 20, GladiatorNPC = 21, CulibineNPC = 22, Ona = 23, Tinker = 24, SpyNPC = 25,
    Scavenger = 26, Wik = 27, ScintillaCaptive = 28,
    Camera = 33, RangedCamera = 34, MeleeCamera = 35, Camerakazi = 36,
    ClassCount = 42,
};

// GAgentAnimCtr (vtable 0x54a05c): one animation the agent can play.
struct Anim {
    int16_t kind = 0;     // ctr+4; kind 2 derives a speed from `param`
    int16_t param = 0;    // ctr+0xc
    std::string path;     // the .anm
};

struct Template {
    uint16_t cls = 0;
    uint16_t type = 0;    // agent+0x54
    double version = 0;   // 1.96 in most shipped files
    uint16_t word10 = 0;
    double d20 = 0, d28 = 0;
    std::vector<uint16_t> stats;  // 8 to 10 words (agent+0x38..)
    std::string name;
    std::vector<Anim> anims;
    std::vector<uint8_t> rest;    // the fields after the animations, undecoded

    bool isNpc() const { return cls >= HumanNPC && cls <= ScintillaCaptive; }
    // Whether a level record of this class carries the GAgentPlug-derived
    // word after the shared fields (chk::parseAgents).
    bool recordHasExtraWord() const { return !(cls == Player || (cls >= Camera && cls <= Camerakazi)); }
};

// Throws std::runtime_error on malformed data or trailing bytes.
Template parse(const std::vector<uint8_t>& data);

}  // namespace harbinger::agt
