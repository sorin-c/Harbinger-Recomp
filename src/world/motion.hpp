// The engine's angle and walking arithmetic. Facings are bytes, 256 steps a
// turn, with 0 pointing along map +x and 64 along +y (screen down-right and
// down-left). Notes and evidence: docs/movement.md
#pragma once

#include <cstdint>

namespace harbinger::world {

// FUN_00528bf0: the facing from one map point towards another, (dx, dy)
// apart. Uses the engine's 64-entry arctangent table (FUN_00528b40), so the
// result matches the original's to the step. (0, 0) gives 0.
uint8_t facingTowards(double dx, double dy);

// FUN_00528cc0 / FUN_00528ce0: sine and cosine of a facing, from the
// engine's 256-entry table.
double facingSin(uint8_t facing);
double facingCos(uint8_t facing);

// An agent's walking speed in map cells per millisecond. FUN_00406100 turns
// the move animation's parameter into template+0x30 = param / (90.51 * 10),
// and GAgentPlug_v80 advances by template+0x30 * 0.01 per millisecond. 90.51
// is the length of a cell's side in pixels before the isometric squash
// (64 * sqrt 2), so the parameter is a speed in those pixels per second.
inline double walkSpeed(int16_t moveParam) { return moveParam / (90.51 * 10.0) * 0.01; }

}  // namespace harbinger::world
