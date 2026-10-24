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
if flags & 0x8000:      FUN_004b3310
  u16 blockCount
  repeat blockCount:
    u16 id
    u16 a, then a * 16 bytes
    u16 b, then b * 12 bytes
  u16 tail
```

Flags: 0x8000 script blocks, 0x4000 light, 0x2000 the offset below is read,
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

The rebuild draws each gizmo's first frame there, sorted with the walls by the
x + y of its nearest footprint cell.

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

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| `.giz` layout, masks, frames, light, script blocks | proven | functions above; all 796 templates in both archives parse to the last byte |
| Class data per class | proven | slot 4 of each class's vtable; every class present in the files parses to the end |
| Flags 0x8000 / 0x4000 / 0x2000 gate script, light, offset | proven | `FUN_004b7350` -> `FUN_004b23e0`, `FUN_004b3730`; `GAnimatedGizmo_v24` control flow |
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
| Mask cell fields | open | see the text |

## Open questions

- The states of the other classes (Pillar, Splitter, Spawn, Portal,
  Umbilical, ChargingStn, Container, KeyConsole, Flicker, Animated), and what
  using a gizmo does (opening, activating; the attachments link a console to
  the door it opens).
- Whether the first pause of a random-pause gizmo starts at once.
- Draw order: a shut door now hides the console beside it in Vantir
  Storage; the sort key may need the footprint of the frame shown.
- Frame 0 of some doors is only a red outline (editor art?); the original
  may hide it the way it hides the umbilical shell.
- The mask cells' `kind`, `a`, `b` (walkability or occlusion), and the
  missing mask images.
- The light block and the script blocks.
- The meaning of the record class data (chest contents, console links, spawn
  slots).
