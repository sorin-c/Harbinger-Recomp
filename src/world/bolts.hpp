// Ranged weapons' bolts in flight: fired from a muzzle towards an aim
// point, flying straight at the weapon's emitter speed, stopped by walls
// and landing on the first hostile agent in their way.
// Notes and evidence: docs/combat.md ("Ranged attacks"), docs/formats/emitter.md
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/emt.hpp"
#include "formats/mgr.hpp"
#include "formats/wpn.hpp"
#include "world/combat.hpp"
#include "world/walkmap.hpp"

namespace harbinger::world {

// Can a bolt at height `z` (map cells) fly from (x0, y0) to (x1, y1)?
// FUN_00402120: the line is walked over the half-cell grid; it stops at
// the map's edge and at a half cell whose height byte is above 2 * z.
bool lineOfFire(const WalkMap& walk, double x0, double y0, double x1, double y1, double z);

class Bolts {
public:
    Bolts(Vfs& vfs, const Ini& ini, const WalkMap& walk, Rng& rng);

    // The emitter of effects.mgr id `id`, or nullptr (missing or not decoded).
    const emt::Emitter* effect(uint16_t id);

    // Who fired: the player (-1) or the foe with that id.
    static constexpr int kPlayer = -1;

    // Fires `weapon` (a ranged one) from the muzzle (x, y) at height z,
    // facing `heading` (FUN_00520bc0 -> the emitter's physics description,
    // GBasicDesc_v1). The bolt's speed is the description's (no speed mod
    // chips yet: scale 1). Returns false if the weapon has no decoded bolt.
    bool fire(int shooter, double x, double y, double z, uint8_t heading, const wpn::Weapon& weapon, int rating,
              uint64_t nowMs);
    // The speed a weapon's bolts fly at, in map cells per ms (FUN_00522030),
    // or 0.
    double speedOf(const wpn::Weapon& weapon);

    // An agent a bolt can hit.
    struct Body {
        int id = 0;              // kPlayer or a foe id
        double x = 0, y = 0;
        double radius = 0;       // template+0x20 (slot 0x90)
        const Defender* defender = nullptr;
    };
    struct Impact {
        int shooter = 0;
        int target = 0;          // kPlayer or a foe id
        double x = 0, y = 0;     // where the bolt hit
        Strike strike;
        uint16_t sound = 0;      // the weapon's impact sound (sound.mgr id), or 0
    };
    // Moves every bolt to `nowMs` (FUN_00499320 for each emitter): a bolt
    // whose step crosses a hostile body's circle hits it and rolls the
    // attack (FUN_00452290 with the ranged weapon); one that meets a wall or
    // outlives its life goes out. `targets(shooter)` lists the bodies hostile
    // to that shooter.
    std::vector<Impact> update(uint64_t nowMs, const std::function<std::vector<Body>(int shooter)>& targets);

    struct Bolt {
        int shooter = 0;
        double x = 0, y = 0, z = 0;
        double vx = 0, vy = 0;    // map cells per ms
        uint8_t heading = 0;
        uint64_t born = 0, last = 0, dies = 0;
        int pierce = 0;           // particle+0xa8
        wpn::Weapon weapon;
        int rating = 0;
        const emt::Emitter* look = nullptr;
    };
    const std::vector<Bolt>& all() const { return bolts_; }

private:
    Vfs& vfs_;
    const WalkMap& walk_;
    Rng& rng_;
    mgr::Index index_;
    std::map<uint16_t, std::unique_ptr<emt::Emitter>> effects_;
    std::vector<Bolt> bolts_;
};

}  // namespace harbinger::world
