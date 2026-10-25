// Portals: the doorways between levels. Notes and evidence:
// docs/formats/gizmo.md, "Portals".
#pragma once

#include <cstdint>
#include <optional>

#include "formats/chk.hpp"
#include "formats/giz.hpp"

namespace harbinger::world {

// A portal's record (GPortalPlug_v22): u16 state, u16 level, u16 entry,
// u16 own entry, u16 power needed, u16 inner trigger, u16 outer trigger.
struct PortalLink {
    uint16_t level = 0;     // plug+0xf4: the level.mgr id it leads to
    uint16_t entry = 0;     // plug+0xf6: the entry point it puts the player on there
    uint16_t own = 0;       // plug+0xf8: the entry point in this level that arrives through it
    uint16_t inner = 0;     // plug+0x104: the gizmo trigger inside (walking in here leaves the level)
    uint16_t outer = 0;     // plug+0x106: the gizmo trigger in front (stepping on it starts the walk in)
};
std::optional<PortalLink> portalLink(const giz::Template& t, const chk::GizmoRecord& g);

// What a portal does when one of its triggers fires (GPortalPlug_v1, the
// trigger's id in the call's y). Phases (plug+0x100): 0 idle, 1 walking in,
// 2 walking out.
enum class PortalStep {
    None,
    WalkIn,     // outer trigger from idle: phase 1, the player is taken in
    WalkedOut,  // outer trigger in phase 2: back to idle
    Leave,      // inner trigger in phase 1: phase 0, load the other level
    WalkOut,    // inner trigger otherwise (arriving): phase 2, the player is walked out
};
PortalStep portalStep(const PortalLink& link, uint8_t& phase, uint16_t trigger);

}  // namespace harbinger::world
