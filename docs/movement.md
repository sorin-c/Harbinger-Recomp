# Walking: the player, the walk grid, paths and the camera

How the player's character stands on a level, walks a clicked path or
follows a stick, collides with the map, and how the camera follows it.

Code: `src/world/motion.*` (angles, speed), `src/world/walkmap.*` (walk grid,
path finder), `src/world/player.*` (the player), `src/formats/animgroup.*`
(equipment layers), `src/app/world_view.*` (drawing, camera, input to the
player), `src/app/harbinger.cpp` (input). Tests: `tests/test_movement.cpp`.
`levelshot <dir> <level> out.tga 4` draws the walk grid over a level.

## Spawning

A new game enters level 1 at entry point 0, "Default": `FUN_004a05d0` sets
the entry id at game+0x136 to 0 and `FUN_00498250` looks it up in the
chunk's entry list (chunk+0x118), puts the player on its (x, y) and copies
the low byte of the entry's facing word into plug+0x3a and +0x3c. Torvus
Junction's Default entry is (20.06, 24.94), facing 19.

The player's template is `[Human|Gladiator|Culibine] Agent` in
Harbinger.ini (`dude.agt`, `gladiator.agt`, `culibine.agt`, class 0,
`GPlayer`).

## Angles

A facing is a byte, 256 steps a turn. 0 points along map +x (screen
down-right), 64 along +y (screen down-left). `FUN_00528b40` fills the
tables at start-up:

```
sine[i]  = sin(i * 2pi / 256)                       i = 0..255   (0x584db0)
atan[i]  = (int)((atan(i / 64) in degrees + 0.5) * 256 / 360)   i = 0..63 (0x5855b0)
```

`FUN_00528bf0(dy, dx)`, the facing from one point towards another: with
`ax = |dx|`, `ay = |dy|`, `a = atan[(int)(ay * 64 / ax)]` if `ay < ax`,
`64 - atan[(int)(ax * 64 / ay)]` if `ax < ay`, 32 if equal (0 with an error
when both are 0); then `a = 128 - a` if `dx < 0` and `a = -a` if `dy < 0`.
`FUN_00528cc0` is sine[f], `FUN_00528ce0` is sine[f + 64] (cosine).
`world::facingTowards`, `facingSin`, `facingCos`.

## Walking (`GAgentPlug_v80`, slot 80)

Each tick, for an agent with a waypoint (+0x68, +0x70) to reach:

1. If the waypoint is not where the agent stands (+0x20, +0x28), the facing
   +0x3a becomes `FUN_00528bf0` towards it. The sprite turns at once.
2. `acc += dt * 0.5` (+0xa8, dt in ms since +0x50); the step is
   `template+0x30 * acc * 0.01`; the agent moves `step * (cos f, sin f)`.
   Afterwards `acc = dt * 0.5`, so each step uses the mean of the last two
   frame times.
3. `FUN_0041e200`: on each axis, if the move (taken as 0 under 0.00001)
   went past the waypoint, that coordinate snaps onto it. Any snap, or
   standing exactly on it, counts as reaching the waypoint: the next one
   follows (`FUN_0041eb30`) or the walk ends.

**Speed.** `FUN_00406100` reads the template's animations and, for the move
animation (kind 2), stores `template+0x30 = param / (90.51 * 10)`
(`0x5497a0` float 90.51, `0x54a050` double 10). So a walk covers
`param / 905.1 * 0.01` cells per millisecond: the Human's 288 gives 3.18
cells a second. 90.51 is a cell side in pixels before the isometric squash
(64 * sqrt 2); moving straight across the screen, `param` is the speed in
screen pixels per second.

**Animation.** The move state plays the move animation at the usual
100 ms a frame, from its first frame when the state starts.

## The walk grid

The chunk keeps 8 bytes for every **half cell**: a (2 * width) by
(2 * height) grid at chunk+0xe4, column-major like the cells. Half cell
(sx, sy) covers map x in [sx/2, sx/2 + 0.5) and y in [sy/2, sy/2 + 0.5); the
engine finds a point's half cell by truncating twice its coordinates. Two
bytes matter here:

- byte 0, **height**: twice the brush height, or a gizmo mask cell's
  height. Negative values count stacked gizmos (`FUN_004475f0`).
- byte 1, **clearance**: 0xff = blocked; otherwise 0, 1 or 2.

**Cells** (`FUN_00448080` -> `FUN_00447470` for the top layer of each cell,
with the brush and the tile's group, `FUN_0043fac0`, 0xe if the brush has no
such tile):

| Brush | Half cells |
|---|---|
| walkable 1, or walkable 2 on a centre tile (group 0) | all walkable |
| floor (kind 0) not walkable, walkable byte not 2 | an edge piece of a hole: half cell k is walkable when `(subMask[k] \| groupMask[group]) != 0xf` |
| anything else (walls, holes' centres) | all blocked |

then height = 2 * brush height on all four. Half cells k = 0..3 are
(0,0), (1,0), (0,1), (1,1) (offsets 0x57a5f8, 0x57a600), `subMask` =
{0xe, 0xd, 0x7, 0xb} (0x57a7d8 indexed by 0x57a608 = {0xb, 0xc, 0xa, 0x9}),
`groupMask` by group = {0xf, 4, 0xc, 8, 9, 1, 3, 2, 6, 0xb, 7, 0xe, 0xd, 0,
0, 0} (0x57a7d8).

**Gizmos** (`FUN_0044a7c0`, for each placed gizmo, its first frame's mask):
each mask cell (dx, dy) lands on half cell (2x + dx, 2y + 1 + dy), the one at
the cell's left corner being the origin. `FUN_004b5ca0` stores the cell's
last two words swapped: the file's `a` is the in-memory word 4 and `b` word
3. Bit 0 of `a` set = blocked, else walkable (`FUN_004475f0` with
`~a & 1`); `b` is the height (`FUN_00447740(.., b, 1)`). Bit 1 of `a`
files the cell in a third list (not used here). Cells of kind 0xd go to a
separate list that this pass does not visit. Torvus Junction's lights,
generator and machinery block; trash and the pit's rim floor do not.

**Clearance** (`FUN_00448df0`): for each walkable half cell, look at three
rings around it, the 2x2, 4x4 and 6x6 squares whose inner lower-right
corner is the half cell (offsets 0x57a83c, 0x57a84c, 0x57a87c; counts 4, 12,
20 at 0x57a834). The byte becomes 2 if a half cell with height > 0 is in the
first ring, 1 if in the second, else 0.

**Who may stand where.** The path finder lets an agent onto a half cell
whose clearance is below `3 - size` (`FUN_00401cb0`, `FUN_00401170`:
`this+0x3a = 3 - size`), size being `GAgentPlug_v31` = template+0x50 =
stat 6 (`FUN_00406a60` reads stats into +0x38, +0x3a, +0x3c, +0x40, +0x3e,
+0x4e, +0x50, ...). The three player templates have size 1, so the player
keeps a half cell away from walls and tall gizmos.

## Paths (`GMoveAction` -> `FUN_004239f0` -> `FUN_00423c30`)

All in half cells (coordinates doubled):

1. **Straight line** (`FUN_00401cb0`): step one half cell at a time along
   the longer axis from start to target, the other coordinate following the
   line. If every half cell is open the agent walks straight to the target.
   The last open half cell is remembered.
2. **A\*** (`FUN_00401170`, `FUN_00401810`, `FUN_00401af0`): if the target
   half cell is blocked, give up. Otherwise search 8 neighbours; straight
   steps cost 5, diagonal ones 7 (0x578590) and need both straight
   neighbours open; nodes are ordered by `g + 49 * (dx^2 + dy^2)`, a squared
   distance that makes the search nearly greedy. A node whose cost reaches
   `7 * 25` (`FUN_004239f0` passes 25) ends the search: no path.
3. Found: the path keeps only the half cells where the direction changes
   (`FUN_004016c0`, `FUN_00401760`), each becomes its centre
   (`FUN_0041eb30`: `s * 0.5 + 0.25`), and the last waypoint is the exact
   target.
4. Not found or blocked: the agent walks the straight line to the last open
   half cell (scaled by 0.5, its corner), if there was one; otherwise it
   does not move.

Other agents are not obstacles in the rebuild yet (`FUN_00419a70`,
`DAT_00583a94`, not traced).

## Camera

`FUN_00498250` (level start) and the per-frame view update centre the view
on the followed agent (game+0x74, the player): the view's top-left is the
agent's screen position, `((x - y) * (64 >> zoom) + originX,
(x + y) * (32 >> zoom) + originY)`, minus half the view size (+0xf0,
+0xf2), plus shake offsets in the per-frame version. No clamping to the map
edge.

## Equipment layers

The player's sprite is the body (the template's own animation for the
state, e.g. `Dude\Run\RunBase.anm`: legs, hands and head) with equipment
drawn over it. `[Human] AnimGroups` (`hanimgroup.mgr`) lists the
equipment: a plain `.mgr` index (u32 n, n ids, n names with the NUL counted,
the index name "Human") followed by u16 states (9) and, per group, an
animation id for each agent state into `[Human] Anims` (`hanim.mgr`, a plain
.mgr of `.anm` paths). For example group "Armor 7" -> idle 82 =
`IdleArmor7.anm`, move -> `RunArmor7.anm`. All layers' sheets have the
body's size and line up with it; the body's hotspot is the ground point.

Layers stack as `FUN_004c2710` does: art replaces what is below, marker
pixels (palette entries 0-5, the shadow) never cover art.

## Rebuild choices

- **Starting equipment** comes from the starting inventory, not decoded.
  The rebuild dresses the Human in "Armor 7" and "Rifle 2", which match the
  original's sprite after New Game; the Gladiator in its "Base Torso",
  "Base Blade", "Base Gun"; the Culibine's index has no equipment.
- **Stick and keys** (the original has only the mouse): the left stick
  (radial dead zone 25%), arrows or WASD give a screen direction; the
  player faces it (`facingTowards` of the map direction) and walks at full
  speed, using the same step as `GAgentPlug_v80`. A move into a half cell
  the player may not stand on keeps only its component along the wall, so
  the player slides. Letting go stops at once.
- **Mouse**: a click walks to the point under the pointer as the original
  does; holding the button re-targets every 100 ms.
- **Draw order**: big gizmos (the umbilical pit) span many cells, so the
  player is placed in front of a nearby gizmo when it stands past the box of
  the half cells the gizmo blocks, behind when short of it, by depth
  otherwise.

## Checked against the original (Wine)

- After New Game -> Human -> Play and skipping the intro, the player stands
  at the Default entry, centred on screen, with the vest and bladed rifle of
  "Armor 7" + "Rifle 2".
- Clicking view pixel (300, 250): the original and the rebuild end with the
  umbilical pit at the same place on screen and the player beside its rim,
  within a few pixels.
- Walking straight across the screen, the background moved 355 px in
  1.239 s while the view followed (frames taken with `xwd` every ~95 ms):
  3.18 cells a second, as the formula gives for 288.

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| Spawn on entry 0 with its facing | proven | `FUN_004a05d0`, `FUN_00498250`; Wine capture |
| Angle tables and `FUN_00528bf0` | proven | disassembly at 0x528b40-0x528cb3; constants 0x559c20, 0x54df38, 0x559c18, 0x5492c8, 0x559c10, 0x54df50 |
| Walk step and averaging | proven | `GAgentPlug_v80` disassembly (0x41de80-0x41e18b) |
| Waypoint snapping, first snap reaches it | proven | `FUN_0041e200`, `GAgentPlug_v80` |
| Speed = param / 905.1 per 100 ms... in cells/ms * 0.01 | proven | `FUN_00406100` at 0x4061e4; measured 3.18 cells/s under Wine |
| Half-cell grid, bytes 0 and 1 | proven | allocation (w*h*4 entries of 8 bytes), `FUN_004475f0`, `FUN_00447740`, `FUN_004477b0` |
| Cell walkability from brush and group | proven | `FUN_00447470`, `FUN_00448080`, tables above |
| Gizmo mask: `a` bit 0 blocks, `b` is height, kind 0xd skipped | proven / likely | `FUN_004b5ca0` word swap, `FUN_0044a7c0`; the 0xd list being separate is read from the two map objects (likely) |
| Clearance rings | proven | `FUN_00448df0`, tables 0x57a834.. |
| Size limit `3 - size`, size = stat 6 | proven | `FUN_00401cb0`, `FUN_00401170`, `GAgentPlug_v31`, `FUN_00406a60` |
| Straight line, then A* with 5/7 costs, 49 d^2, budget 175 | proven | `FUN_00423c30`, `FUN_00401170`, `FUN_00401810`, `FUN_00401af0` |
| The line's minor coordinate rounding | guessed | hidden in x87 code the decompiler drops |
| Path simplification, waypoint = half-cell centre | proven | `FUN_004016c0`, `FUN_00401760`, `FUN_0041eb30` |
| Camera centred on the player, no clamp | proven / likely | `FUN_00498250` disassembly; per-frame function at 0x49d... (likely); Wine frames |
| Animation group layout | proven (by the files) | all three `*animgroup.mgr` parse to the last byte and every id resolves in the animation index; loader not traced |
| Layers line up by sheet | likely | every layer sheet has the body's size; composites match the Wine capture |
| Starting equipment | guessed | Wine capture (Human); the others unverified |

## Open questions

- Agents blocking agents (`FUN_00419a70`), and the turn rate at plug+0xa0
  used by `GAgentPlug_v81` (+0x3c follows +0x3a).
- What the original does on a held mouse button, and the starting inventory.
- The kind 0xd mask cells and the third list (`a` bit 1).
