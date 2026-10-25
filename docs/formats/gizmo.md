# Gizmos (`.giz` templates and a level's set gizmos)

Gizmos are the level's objects: machinery, trash heaps, consoles, chests,
pillars, portals, spawners. A **template** (`.giz`, class `GGizmo` and its
subclasses) holds the art and the class's settings; a level places
**instances** of templates on its map (set gizmos, `GGizmoPlug` subclasses).

Parsers: `src/formats/giz.cpp` (templates), `chk::parseGizmos` in
`src/formats/chk.cpp` (a level's records). Loader and drawing:
`src/world/level.cpp`. Tests: `tests/test_world.cpp`.

## Which template

`Harbinger.ini [ActN] Gizmos` names the act's `gizmo.mgr`, which maps
template ids to `.giz` paths. A level's act picks the section (the same
mapping as for brushes, `docs/formats/level.md`).

## `.giz` layout (version 1.04, every shipped file)

`gstr` is a u16 length that counts the NUL, then the characters and the NUL.
`str` is the engine string (u32 length without the NUL, characters, NUL).

```
f64  version            1.04
gstr name               FUN_004b7350
u16  flags              low byte: class (table below); high byte: flags
u16  layout             FUN_004b1f10 splits it: low 8 bits, 4 bits, 4 bits + 1
u8   unknown
-- GAnimatedGizmo_v24 (0x430d50); GStaticGizmo_v24 (0x50b540) reads the same --
u16  maskCount
u16  frameCount
repeat maskCount:       FUN_004b5c00
  gstr path             a .tga name; no such image ships (editor art)
  u16  rows             the first mask's value is slot 17: the draw anchor
  u16  cellCount        FUN_004b5ca0
  repeat cellCount:     10 bytes
    i16 dx, dy          half-cell offset from the origin half-cell
    i16 kind            0x0d goes to a separate list
    i16 a               stored after b (cell+8); bit 1 adds the cell to a third list
    i16 b               stored at cell+6
repeat frameCount:
  u16  mask             index into the masks
  i16  x, y             FUN_004b53e0: the frame's place on the first frame, in pixels
  u16  duration         100 on 3,139 of 3,442 frames; 0 to 2500 on the rest
  u16  code             frame+0x1c: a tag (see States); 0 on most frames
  gstr path             the frame's image (.tga; the archives serve the .gim)
  u8   hasRect
  if hasRect == 1:      FUN_004ba840
    u8 rect[8]
if version > 1.03:      FUN_004b2260
  i16 point[2]          gizmo+0x2e/+0x30; -1,-1 on most
class data              vtable slot 4, below
if flags & 0x4000:      light (gizmo+100)
  u8  light[2 + 0x30 + 0x18]
  repeat 2:             FUN_004b3af0
    u16 n, then n * 26 bytes
if flags & 0x8000:      the 3D shape, FUN_004b3310 (see "Shape")
  u16 pieceCount
  repeat pieceCount:
    u16 cell            index into the first mask's cells with a bit 1
    u16 vertexCount
    repeat vertexCount: 16 bytes
      i16 x, y, z       half cells from the cell's corner (* 0.5, 0x5492c8); z up
      i16 n[3]          -1 read as 2, folded into (n2 * 3 + n1) * 3 + n0 (0 -> 9)
      i16 u, v          texture coordinates in 1/16 (* 0.0625, 0x553e18)
    u16 triangleCount
    repeat triangleCount: 12 bytes
      u16 a, b, c       vertex indices
      u16 p, q          not decoded
      i16 r             not decoded
  u16 tail              gizmo+0x32
```

Flags: 0x8000 the 3D shape (set in every shipped file), 0x4000 light, 0x2000 the offset below is read,
0x1000, 0x800, 0x400, 0x200 (set fields whose use is not known yet).

### Class data (vtable slot 4)

Every class's load starts with `GAnimatedGizmo_v4` (0x4b21c0), which reads
`i16 offset[2]` (gizmo+0x34/+0x36) only when flag 0x2000 set them to 0.

| Index | Class | After the offset |
|---|---|---|
| 0, 1, 3, 8, 9, 12, 13, 22 | Static, Looped, Animated, UmbilicalShell, Looped, Breaker, Splitter, TwoWay | nothing |
| 2, 14, 23 | Chest, Barrel, TrashPile | `str label` (version > 1.01; `GBarrel_v4` 0x4bca70) |
| 5, 15, 20 | Console, CompuConsole, KeyConsole | as Barrel (0x453800) |
| 19 | Container | as Barrel, then u16, u16 (0x456020) |
| 4 | Bridge | u16 (0x43c7a0) |
| 6 | ChargingStn | 8 bytes, no offset (0x443e80 -> `FUN_0051a350`) |
| 7 | Umbilical | 8 bytes (0x51d1c0) |
| 10, 17 | Incubator, RandomPause | 8 bytes, no offset (0x4f9790) |
| 11 | Pillar | 8 bytes, no offset (0x4ea800) |
| 16 | Portal | 16 bytes (0x51e850: `FUN_0051a350` twice) |
| 18 | Spawn | u16 n, then n * 6 bytes (0x5085b0); n is gizmo+0xcc |
| 21 | Flicker | 12 bytes (0x490bc0) |

## Set gizmos in a level

After the map cells (`docs/formats/level.md`):

```
repeat variantCount:    FUN_004477e0, one list per variant (0 = base)
  u16 n, then n * u16 instance id, ascending
i32  count              FUN_004495a0
repeat count:
  u16  template         id in the act's gizmo.mgr
  u16  instance         GFakeGizmoPlug_v22 (0x4b9440): plug+8
  u16  x, y             map cell (plug+0x14, +0x16)
  ...  class data       the plug's chunk load (vtable slot 22)
```

The chunk load reads only the selected variant's list and seeks past the
others. `FUN_004495a0` then loads every record, keeps the gizmos whose
instance is the next id in that list, and deletes the rest. The base list
holds every instance in most levels; Outer Labs (both copies) drops eight, so
those eight never appear in the game.

Record class data at chunk version 1.23 (`label` is FUN_004b8bf0: a u8 flag,
then a `str` when the flag is 1):

| Index | Plug load | After the six shared bytes |
|---|---|---|
| 0, 3, 8, 21 | base | nothing |
| 1, 9 | 0x4e1df0 | 6 bytes |
| 2, 14, 23 | 0x445140 | label, 3 u16 (4 before version 1.22), u16 |
| 4, 22 | 0x43c830 | u16 |
| 5 | 0x454480 | 6 bytes, label, u16 |
| 6 | 0x444260 | 8 bytes |
| 7 | 0x51dca0 | 10 bytes |
| 10, 17 | 0x4f9df0 | u16 |
| 11 | 0x4eac30 | 10 bytes |
| 12 | 0x439bf0 | 4 bytes |
| 13 | 0x50a450 | 8 bytes |
| 15, 20 | 0x454eb0, 0x4557f0 | 6 bytes, label, 4 bytes |
| 16 | 0x51f4d0 | 14 bytes |
| 18 | 0x509690 | as Splitter (8 bytes), then a u16 per spawn slot (the template's n) |
| 19 | 0x455d00 | u16, label, u16 |

## Drawing

`FUN_004b3690` turns a pixel of the gizmo's first frame into a map offset:
`X = (px - 64) / 128`, `Y = (py - height + (rows + 1) * 32) / 64`,
`x = X + Y`, `y = Y - X`, with `rows` the first mask's row count (slot 17,
0x431730). So image pixel `(64, height - (rows + 1) * 32)` sits on the
gizmo's map position: the image's left edge is the cell's left corner and its
bottom is `(rows + 1) * 32` below the cell's top corner.

The mask cells agree: a template's footprint, in half cells, starts at the
left half of the gizmo's cell and runs towards +x and -y, and its bounding
diamond is as wide as the image (Trailer: 6 by 8 half cells, 448 px).

**Shape.** Every template carries a 3D shape: triangles in map space, one
piece per cell of the first mask whose `a` has bit 1 (the cells
`FUN_004b5ca0` files in its third list, in file order; the piece's `cell`
indexes them). A vertex `(x, y, z)` of the piece for cell `(dx, dy)` lies at
map `(gx + (x + dx) / 2, gy + (y + dy + 1) / 2)`, height `z / 2`, and the
original textures the triangles with the gizmo's picture at the pixel the
point projects to:

```
px = 64 + 64 * (X - Y),   py = height - (rows + 1) * 32 + 32 * (X + Y) - 64 * Z
```

with `X, Y` the offsets from the gizmo's position and `Z` the height. For
pictures over 256 pixels `FUN_004b25e0` recomputes the texture coordinates
with exactly this projection; for the others the stored `u, v` are this
point in the power-of-two texture (343 of the 344 such templates fit it).
So the shape is the gizmo's picture folded onto walls, panels and boxes,
and the original draws it through Direct3D with a depth buffer
(`FUN_004b2d20` builds the draw lists per piece). Two things follow:

- Only the pixels the shape covers are drawn. What the shapes leave out is
  editor art: a door's first frames are only a red outline of the open door
  (Vantir Storage's door: frames 0 and 1 lie wholly outside its shape,
  frames 2 to 15 wholly inside), the magenta "Flood Fill" markers, the
  umbilical shell's outline. Over the game the shapes cover 96% of the
  first frames' opaque pixels.
- Depth: the camera looks along (-1, -1, -1) (screen y = 32 (x + y) - 64 z),
  so at a given pixel the surface with the larger map `x + y` is nearer;
  comparing `x + y` there orders two surfaces as the depth buffer does.

The rebuild has no depth buffer (SDL's 2D renderer). It cuts each frame to
the shape (`Level::gizmoFrameImage`), keeps a depth per pixel of the shape
(`Level::gizmoShape`), and orders whole pictures: of two gizmos that overlap
on screen, the one nearer at most of the shared pixels draws last
(`Level::gizmoInFront`); a character counts as an upright picture at the
depth of its feet. The rest keeps the order of the x + y of each gizmo's
nearest footprint cell, with the walls drawn by diagonal among them.

**Animation.** The original keeps one picture per gizmo, made from the first
frame (`FUN_004b25e0` cuts it into tiles of at most 256 pixels and
`FUN_004b2bb0` draws frame 0 into them), and draws each new frame over that
picture at the frame's `(x, y)` (`GAnimatedGizmo_v12`), without clearing it. A
frame at (0, 0) as big as the first frame loses its colour key when the
template loads (`GAnimatedGizmo_v24` clears bit 0 of the image flags), so it
replaces the whole picture; any other frame is an overlay (the town
generator's turning core, the crashed ship's flames). The rebuild draws, for
the frame shown, the last frame before it that replaces the picture and every
frame after that up to the one shown (`Level::gizmoLayers`). Which frame is
shown comes from the time: `GAnimatedGizmo_v8` / `FUN_004305b0` walk a frame
range adding up durations and return the first frame whose running total
reaches the time.

An Umbilical Shell's first frame is only an outline of its footprint (editor
art); the original shows nothing there at rest, so the rebuild hides the
class.

## States

Each placed gizmo (its plug) has a state at plug+0x7c. The plug's vtable slot
17 gives the frame range the state plays, slot 28 advances it each tick
through `FUN_004b2300` and `GAnimatedGizmo_v5`, and slot 29 runs when the range
runs out: it can change the state, pick the next frame, or send the
animation back to the start of the range. The state comes from the record's
class data (slot 22).

**Frame tags.** A frame's code marks where a state's frames begin or end.
The names table at `0x57df38` (32 bytes each) reads, in order:

| Code | Name | Code | Name |
|---|---|---|---|
| 0 | None | 6 | End Fire |
| 1 | Begin Idle | 7 | Begin Activate |
| 2 | End Idle | 8 | End Activate |
| 3 | Begin Open | 9 | Begin Close |
| 4 | End Open | 10 | End Close |
| 5 | Begin Fire | | |

The classes look the tags up when the template loads: a chest needs Begin
Open (`FUN_00444710`, which logs "GChest: Chest needs BeginOpen tag."),
consoles find Begin Activate (`0x453800`), breakers End Open and Begin Close
(`FUN_00439650`), random-pause gizmos every Begin Idle (`FUN_004f9670`), looped
gizmos the first Begin Idle (`GLoopedGizmo_v4`).

**Starting states** (`src/world/gizmo_state.cpp`):

| Classes | Record state | Plays |
|---|---|---|
| Looped 1, 9 | u32 phase, u16 mode | see below |
| Bridge 4, TwoWay 22 | u16 state | 0: frame 0; else the last frame (`GBridgePlug_v22`, `_v17`). Doors: frame 0 is the open door, the last the shut one; 206 of the 219 doors and bridges in the game's levels start at the last frame, every Biter Pen (TwoWay) at frame 0 |
| Chest 2, Barrel 14, TrashPile 23 | last u16 | 0 and 4: loop the frames before Begin Open (`GBarrelPlug_v17`, `_v29`); 2: the opening; others: the last frame |
| Console 5 | u16 state, u16, u16 power needed | without enough power: the last frame; 2, 4: the Begin Activate frame; else forth and back over the frames before Begin Activate (`GConsolePlug_v17`, `_v29`), each end shown twice |
| CompuConsole 15 | as Console | as Console, but looping (`GCompuConsolePlug_v17`, `_v29`); 5: the last frame but one |
| Breaker 12 | u16 state, u16 power needed | 0 (off): frame 0; 3 and 5 (on): loop from after End Open to before Begin Close (`GBreakerPlug_v17`) |
| Incubator 10, RandomPause 17 | u16 power needed | with enough power: rest on the frame before the first Begin Idle, wait, play one Begin Idle sequence at random, rest again (`GIncubatorPlug_v28`, `_v29`); else frame 0 |
| the rest | | frame 0 (not decoded yet) |

Looped gizmos (classes 1 and 9) play all the time (`GLoopedGizmoPlug_v28`
advances them every tick). Their record gives a u32 (plug+0x34, 0 in every
level; `GAnimatedGizmoPlug_v28` subtracts it from the start time, so a phase)
and a mode word (plug+0x7c). `GLoopedGizmoPlug_v17` loops mode 3 gizmos from
the first Begin Idle frame to the last and the others over the frames before
it (all frames when there is none). Class 9 also runs its loop backwards at
times (`GLoopedGizmoPlug_v29`, a flag at gizmo+0xb8 set for class 9); the
rebuild plays it forwards.

A random-pause template's class data is u32 min, u32 max: the pause is
`(rand % (max - min) + min) * 10` ms, or `max` ms when the two are equal
(`GIncubatorPlug_v29`). The rebuild draws these numbers from the instance
number instead of a random generator and repeats a run of 64 pauses, so a
level looks the same each time.

**Power.** Gizmos that need power (consoles, breakers, random-pause gizmos)
have a `GIPowerable` part: the power they have (+4) and need (+6), and the
gizmos they pass power to (+8). The level lists, per gizmo, the gizmos it
powers (`docs/formats/level.md`; `0x4d4ef0` adds each as a target) and which
gizmos start with how much power (`GChunk_v3`). A gizmo that gets power adds
it and hands the same amount to its targets (`0x4d4ab0`); a breaker hands it on
only while on, state 3 (`GBreakerPlug_v0`), and switching it on or off gives
or takes its power from every target (`GBreakerPlug_v30`, `_v31`). In the
shipped levels every source is a breaker with level 1 (77 of them); they
power heaters, pillars, incubator spawners and some consoles. 24 of the 308
console records need one unit of power; in the game's levels 8 consoles start
without it and show their last (dark) frame.

## Using gizmos

The player uses a gizmo by walking to its **use point** and activating it
(`GActivateAction`: the agent uses the gizmo once it stands in the cell of
the plug's target point). The target point is the template's offset
(gizmo+0x34, +0x36: a pixel of the first frame), placed on the map by
`FUN_004b3690` as `GFakeGizmoPlug_v22` does:

```
X = (px - 64) / 128,  Y = (py - height + (rows + 1) * 32) / 64
use point = (gizmo x + X + Y, gizmo y + Y - X)
```

`height` is the first frame's height and `rows` the first mask's row count.
Templates without an offset use the middle of the gizmo's cell.

**Consoles** (`GConsolePlug_v1`, GIActivatable slot 1): with enough power, a
console in state 0 or 4 goes to state 2 (its activation frames) and calls
`FUN_004bdaf0`, which calls GIAttachable slot 1 on every gizmo attached to
it: the level's attachments list (`docs/formats/level.md`) links a console to
the doors it opens. A console without power does nothing.

**Doors and bridges** (`GBridgePlug_v1`, the attachable toggle): shut (3)
goes to opening (1), which plays the frames backwards from the last to frame
0; open (0) goes to closing (2), forwards, unless an agent stands on the
door (`FUN_004a01e0`). When the frames run out (`GBridgePlug_v29`), 2 settles
in 3 and 1 in 0, and the door tells the gizmo that toggled it; the console
then goes back from 2 to 0 (`GConsolePlug_v0`).

**Chests, barrels, trash piles** (`GBarrelPlug_v1`): state 0 goes to 2, the
frames from Begin Open to the last, and settles in 3 (`GBarrelPlug_v29`),
where `FUN_004d8dd0` drops what is inside. The rebuild opens them but drops
nothing yet.

**The walk grid follows.** A door's frames carry their own masks, so when a
gizmo shows a frame with another mask the rebuild repaints the half cells
under the old and new masks (floor, then every gizmo that covers them) and
recomputes clearance up to 3 half cells around (`docs/movement.md`). A test
checks that the repainted grid equals one built from scratch.

**Rebuild choices.** A click on a usable gizmo's picture (or E, Space, or the
pad's A button for the nearest usable gizmo within 3 cells) walks the
player to the passable half cell nearest the use point and uses the gizmo
on arrival if it is within 1.5 cells. A console with nothing attached
plays its activation once and goes back to idle.

## Spawners

A **spawner** (class 18, `GSpawnGizmo` / `GSpawnGizmoPlug`: pods, Vantir
stations, bug nests, the NPCs' alcoves) holds agents until a trigger or a
script activates it. The 223 spawners in the levels hold 244 agents.

**What a spawner holds.** The template ends with a list of slots, three
`i16` each (`GSpawnGizmo_v4`): `a`, `b`, a pixel of the first frame, and
`c` (0, 3 or 7; not traced). The level record, after the splitter's eight
bytes, has one `u16` per slot (`GSpawnGizmoPlug_v22`, plug+0x10c). The
agents themselves are placed in the level like any other: an enemy
record's trailing word (`GEnemyPlug_v71`, plug+0x218) names the spawner's
instance in its low 12 bits and the slot in its top 4. On load,
`FUN_0044b6b0` stores the agent's GIActivatable in the spawner's slot
(plug+0x110). In the shipped levels each slot word is the agent.mgr id of
the agent in that slot, and the agent stands on the slot's spawn point:
`(a, b)` placed on the map by `FUN_004b3690`, as for use points (all 244,
two of them 0.13 cells off; test). The automap reads the same points
(`FUN_0044b760`, case 0x12).

**The record.** `GSplitterPlug_v22`: `u16` state (plug+0x7c), `u32`
(plug+0x34; 0 or 5 to 1200, not traced, likely a delay in ms), `u16` power
needed (plug+0xde, 0 in every record); then the slot words. Most records
start in state 0; 17 station spawners start spent (3), their agents
already out.

**States.** Spawners and splitters (class 13, the halves of Vantir
stations) share them:

| State | Frames (slot 17, `0x50a5c0`) | |
|---|---|---|
| 0 | the first | waiting |
| 2 | all, once | activated (`0x50a720`: only from 0) |
| 3 | the last | spent (slot 29, `0x50a620`) |

**Opening** (`GSpawnGizmoPlug_v28`, `GSplitterPlug_v28`): while in state 2
and powered, each frame step tests two frames of the template:

- the Begin Activate frame (tag 7, gizmo+0xb8, `GSpawnGizmo_v34`; frame 0
  if none): slot 30 calls `FUN_004bdaf0`, so every gizmo attached to it
  (the level's attachments list) is activated. Station tops start their
  bases this way (71 Vantir stations, 18 Provost stations) and one Dual
  Hanger the next;
- the End Activate frame (tag 8, gizmo+0xc0, `GSpawnGizmo_v4`; the last
  frame if none): each slot's agent gets GIActivatable slot 1, and the
  slot is emptied.

**What the agents do.** Slot 1 depends on the agent's class:

| Classes | Slot 1 | |
|---|---|---|
| Vantir (3), Khapra (10), Bowel Horse (12) | `0x507f30`, `0x508280` | role 2, policy 9 (VantirAwake) |
| Vantir boss (4), Herps (13, 14), Overlord (39) | `0x507ff0`, `0x4bbc70`, `0x5082f0` | policies 10, 0x15, 0xb |
| Scint boss (8), Boxer (37) | `0x4fcac0`, `0x508060` | the mind's slot 12 |
| Turret (18), Cim drone (40) | `0x5084e0`, `0x507ec0` | action 0 (Idle) |
| NPCs (20-28), Gubiests (15, 16) | `0x40e510` | nothing |

All but the last skip an agent that is dead (`FUN_004e4d70`). VantirAwake
(`GVantirAwakePolicy_v1`) has two steps: action 8 (Awake), which plays the
agent's state 7 animation (Awake, Spawn) through once, then action 0x11
(BlindMove) to the point 2 cells ahead along its facing
(`GVantirAwakePolicy_v2`).

**Before.** `GBowelHorsePlug_v61` activates the agent's own spawner when
its current action is 9 (Stasis), so a held agent waits in Stasis.
`GStasisAction` plays state 7 too, but its tick (slot 2, `0x4041a0`) sets
the agent's animation start (agent+0x58) to now every frame, so the
picture stays on the animation's first frame (likely; where Stasis starts
is not found).

**Rebuild.** `world::Spawners`: agents of the classes that wake (not NPCs
or Gubiests) whose spawner is not spent are held; a held agent is drawn on
the first frame of its state 7 animation (nothing if it has none: drones
wait unseen) and cannot be attacked. Spawners and splitters open on
activation (event triggers' gizmo lists, `ActivateGizmo`); at Begin
Activate the attached gizmos are activated, at End Activate the agents are
let out, and Awake agents play their state 7 animation once. Held and
waking agents are out of the enemies' minds (they do not think and cannot
be hit). Then VantirAwake's second step: classes 3, 10 and 12 walk to the
point 2 cells ahead (`GBlindMoveAction_v1` finds the path as a move does,
`FUN_004239f0`), and their minds take over. Not yet: the other classes'
wake policies (10, 0x15, 0xb, the mind's slot 12), and a held agent waking
when hit. On Om's Outer Sanctum (level 33) the cutscene opens both pods and
the twins rise out of them.

## Portals

A portal (class 16, `GPortalPlug`) is the doorway between two levels. Its
record's 14 bytes of class data (`GPortalPlug_v22`) are seven words:

| Offset | Plug field | Meaning |
|---|---|---|
| 0 | state | 0 in every shipped record |
| 2 | +0xf4 | the level (`level.mgr` id) it leads to |
| 4 | +0xf6 | the entry point it puts the player on there |
| 6 | +0xf8 | the entry point in this level that arrives through it |
| 8 | +0xde | power needed |
| 10 | +0x104 | the inner trigger: a gizmo trigger inside the portal |
| 12 | +0x106 | the outer trigger: a gizmo trigger in front of it |

In 45 of the 49 shipped portals inner = 2·own − 1 and outer = 2·own − 2;
the engine reads the words, not the rule.

**Gizmo triggers.** A level's gizmo triggers (`docs/formats/level.md`) are
circles that call their gizmo when the player walks in
(`GGizmoTrigger_v3`: the trigger goes to state 2 and calls the gizmo's
GIActivatable slot 1 with (−1, trigger id)). `GGizmoTrigger_v4` re-arms it
(state 3) once the gizmo is idle (state 0), and `FUN_0051afa0` arms it
again (state 0) once the player is outside the circle.

**Walking through** (`GPortalPlug_v1`, the trigger id in the call's y; the
portal's phase at +0x100):

- Outer trigger, phase 2 (walking out): phase 0.
- Outer trigger otherwise: phase 1, game state 8 (no orders), and a script
  built in code: `Sound(278, NoWait)`, `Wait(1400)`,
  `MoveTo(inner trigger's point, 0, NoWait)`.
- Inner trigger, phase 1: game state 1, phase 0, then `GGame+0x132` =
  level, `+0x136` = entry, and `FUN_004a1d20` loads that level.
- Inner trigger otherwise (the player arrives standing in it): phase 2,
  game state 8, and the script `Sound(278, NoWait)`,
  `MoveTo(the portal's use point, 0, Wait)`, `Warp(0)`; `Warp(0)`
  (`FUN_004a49e0`) gives play back (game state 1).

The rebuild plays these scripts with its script runner and keeps the
player's money, carried items, hit points and energy across the change.

**Rebuild choice.** A scripted move on the player walks the found path,
then the rest in a straight line if the path stops short. At the player's
clearance limit (2) the path into Om's Outer Sanctum's portal ends a cell
before the inner trigger; the original's walk reaches it (the scene would
never end otherwise), by a route not traced.

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| `.giz` layout, masks, frames, light, 3D shape | proven | functions above; all 796 templates in both archives parse to the last byte |
| Class data per class | proven | slot 4 of each class's vtable; every class present in the files parses to the end |
| Flags 0x8000 / 0x4000 / 0x2000 gate the shape, light, offset | proven | `FUN_004b7350` -> `FUN_004b23e0`, `FUN_004b3730`; `GAnimatedGizmo_v24` control flow |
| Spawn slot count = template spawn list length | proven | `FUN_00508d50` copies the template's +0xcc; `GSpawnGizmo_v4` counts the list into it |
| Set-gizmo record layout and class data | proven | plug loads listed above; all 93 level files (80 unique paths) parse, every record inside its map, instance ids unique |
| Chest record: 3 u16 at version 1.23 | proven | 0x445140 compares the version with 1.22 (`fcomp`, C0 set below 1.22 -> 4 words) |
| Draw anchor | likely | `FUN_004b3690` (proven); Torvus Junction drawn this way matches the original's screen under Wine |
| Instance lists select the gizmos a variant keeps | proven | `FUN_004477e0` (list per variant, seek past the others); `FUN_004495a0` compares each record's instance with the list and deletes the rest |
| Frame lookup by running duration | proven | `GAnimatedGizmo_v8`, `FUN_004305b0` |
| Looped range and code-1 frame | proven | `GLoopedGizmo_v4`, `GLoopedGizmoPlug_v17`; modes 0 (167 records) and 3 (27) in the levels |
| Frame tag names | proven / likely | the names table at `0x57df38`, in code order (likely); code 3 = Begin Open (proven: `FUN_00444710` and its error message), 7 = Begin Activate, 4 and 9 for breakers, 1 for looped and random-pause gizmos (proven: the loaders above) |
| A whole frame replaces the picture, others draw over it | proven / likely | `GAnimatedGizmo_v24` clears the key bit for frames at (0, 0) the size of frame 0; `GAnimatedGizmo_v12` draws each frame into the tiles at (x, y) (proven); that every frame between is drawn in order (likely: the plug steps one frame at a time) |
| Starting states per class | proven | slots 17, 22, 28, 29 of each plug, listed in the table; every level's records give states in range (test) |
| Door frame 0 open, last frame shut | likely | the frame names (`15o.tga` first, `1c.tga` last) and pictures |
| Power flow | proven / likely | `0x4d4ab0`, `GBreakerPlug_v0`, `_v1`, `_v30`, `_v31`, `0x4d4ef0` (proven); that the chunk gives each source its level when the level starts (likely, `GChunk_v3` stores them) |
| Use point | proven | `FUN_004b3690`, `GFakeGizmoPlug_v22`, `GActivateAction` |
| Console activation, door toggle, chest opening | proven | `GConsolePlug_v0`, `_v1`, `FUN_004bdaf0`, `GBridgePlug_v1`, `_v29`, `FUN_004a01e0`, `GBarrelPlug_v1`, `_v29`; Vantir Storage's console 149 opens door 151 and its doorway becomes walkable (test) |
| Spawner slots hold placed agents (agent word = spawner + slot << 12) | proven | `GEnemyPlug_v71`, `FUN_0044b6b0`; every tagged agent names a spawner of its level (test) |
| Slot word = the slot's agent.mgr id; spawn point = slot pixel placed like a use point | proven by survey | all 244 bound agents (test) |
| Spawner states, attach and release frames | proven | `0x50a5c0`, `0x50a720`, `GSpawnGizmoPlug_v28`, `_v29`, `_v30`, `GSpawnGizmo_v4`, `_v34`, `GSplitterPlug_v28` |
| Agent wake per class, VantirAwake steps | proven | the classes' GIActivatable slot 1, `GVantirAwakePolicy_v1`, `_v2`, the action ids (slot 3) and states (slot 4) |
| Held agents stand on the Awake animation's first frame | likely | `GStasisAction` slots 2 and 4, `GBowelHorsePlug_v61`; the start of Stasis is not traced |
| Mask cell fields | proven | `FUN_004b5ca0`, `FUN_004475f0`, `FUN_00447740` (`docs/movement.md`) |
| 3D shape layout | proven | `FUN_004b3310`; every template parses to the last byte |
| Shape placement and projection | proven | `FUN_004b25e0` (texture coordinates from the projection), the stored coordinates of 343 of 344 small templates (affine fit), Vantir Storage's door frames lying exactly inside or outside its shape |
| Portal record words | proven | `GPortalPlug_v22`; every shipped portal leads to an existing level and entry, and both its triggers are gizmo triggers on it (test) |
| Gizmo trigger states | proven | `GGizmoTrigger_v3`, `_v4`, `FUN_0051afa0` |
| Walking through a portal | proven | `GPortalPlug_v1`, `FUN_004a1d20`, `FUN_004a49e0`; Om's Outer Sanctum's portal takes the player to Lower Decks entry 2 and back out (run) |
| Drawn with a depth buffer, only the shape's triangles | likely | `FUN_004b2d20` and the Direct3D quads of `FUN_004e2eb0`; the depth state itself is not traced |

## Open questions

- A portal's state 2 (frames from tag 7 to the last but one) can also
  change level (`GPortalPlug_v29`, `FUN_004a1a20`); no shipped record starts
  there.
- The states of the other classes (Pillar, Portal,
  Umbilical, ChargingStn, Container, KeyConsole, Flicker, Animated), and
  using breakers, key consoles and lifts.
- What a chest drops (`FUN_004d8dd0`).
- Whether the first pause of a random-pause gizmo starts at once.
- Vantir Storage's door console stands inside the doorway, so by the
  shapes the shut door hides it; check against the original in game.
- The vertex normal code and the triangle fields `p`, `q`, `r` (lighting?),
  and the shape's tail word.
- Walls are drawn by diagonal, not with their own 3D quads.
- The mask cells' `kind`, `a`, `b` (walkability or occlusion), and the
  missing mask images.
- The light block.
- The meaning of the record class data (chest contents, console links, spawn
  slots).
