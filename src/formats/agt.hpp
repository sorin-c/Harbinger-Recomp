// Agent templates (.agt): characters, monsters, NPCs, mines and cameras, and
// their animation lists. Format notes and evidence: docs/formats/agent.md
#pragma once

#include <cstdint>
#include <string>
#include <utility>
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

// What an agent is doing. An animation's kind is the state it plays in:
// FUN_00406280 files each animation under template+0x74 + kind * 0x10, and
// FUN_00407950 picks one of a state's animations at random. Names from the
// shipped files (Idle\Idle.anm is kind 0, Run\Move.anm 2, Shoot\ShootBase.anm
// 3, Bite\Melee.anm 4, Die\Die.anm 5); the rest are not named yet.
enum State : int16_t { Idle = 0, Move = 2, Shoot = 3, Melee = 4, Die = 5 };

// GAgentAnimCtr (vtable 0x54a05c): one animation the agent can play.
struct Anim {
    int16_t kind = 0;     // ctr+4: the State; kind 2 derives a speed from `param`
    int16_t param = 0;    // ctr+0xc
    std::string path;     // the .anm
};

struct Template {
    uint16_t cls = 0;
    uint16_t type = 0;    // agent+0x54
    double version = 0;   // 1.96 in most shipped files
    uint16_t word10 = 0;  // template+0x10: the defence an attack roll must reach (FUN_00452290)
    double d20 = 0, d28 = 0;  // template+0x20: the half-size of the box a melee strike must cross
    std::vector<uint16_t> stats;  // 8 to 10 words (agent+0x38..)
    std::string name;
    std::vector<Anim> anims;
    // Damage taken per damage type, in percent (template+0x204, read by
    // FUN_004085a0); a type that is not listed does no damage.
    std::vector<std::pair<uint16_t, uint16_t>> resistances;
    // FUN_00407170 (version 1.8 on): four bytes to template+0xa..+0xd
    // (flags[2], +0xc, set: an enemy leaves faction 9 alone, GEnemyPlug_v97),
    // the faction (+0x21c: 1 the player's side, 2 NPCs, 4 Vantir, 8 biters,
    // ...), a word (+0x220), the thinker that runs the agent's mind (+0x58,
    // the GIThinker id: 1 stationary, 3 repair, 5 animal, 6 predator,
    // 7 trooper, 8 commander, 9 boss, 10 mechanized) and from 1.86 a byte
    // (+0xe). See docs/enemies.md.
    uint8_t flags[4] = {};
    uint16_t faction = 0, word220 = 0, thinker = 0;
    uint8_t byteE = 0;
    std::vector<uint8_t> rest;    // the fields after the animations (raw)

    // The animations filed under `state`, in file order (the variant numbers
    // FUN_00407950 picks from).
    std::vector<const Anim*> animsFor(int16_t state) const {
        std::vector<const Anim*> out;
        for (const auto& a : anims)
            if (a.kind == state) out.push_back(&a);
        return out;
    }

    bool isNpc() const { return cls >= HumanNPC && cls <= ScintillaCaptive; }
    // Whether a level record of this class carries the GAgentPlug-derived
    // word after the shared fields (chk::parseAgents).
    bool recordHasExtraWord() const { return !(cls == Player || (cls >= Camera && cls <= Camerakazi)); }
};

// Throws std::runtime_error on malformed data or trailing bytes.
Template parse(const std::vector<uint8_t>& data);

}  // namespace harbinger::agt
