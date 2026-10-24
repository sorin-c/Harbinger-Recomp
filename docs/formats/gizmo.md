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
    i16 a
    i16 b               bit 1 adds the cell to a third list
repeat frameCount:
  u16  mask             index into the masks
  i16  x, y             FUN_004b53e0: the frame's place on the first frame, in pixels
  u16  duration         100 on 3,139 of 3,442 frames; 0 to 2500 on the rest
  u16  code             frame+0x1c: 0 on most frames; 1-4, 7-10 on the rest
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
u16  listCount          1 in every shipped level
repeat listCount:
  u16 n, then n * u16 instance id
i32  count              FUN_004495a0
repeat count:
  u16  template         id in the act's gizmo.mgr
  u16  instance         GFakeGizmoPlug_v22 (0x4b9440): plug+8
  u16  x, y             map cell (plug+0x14, +0x16)
  ...  class data       the plug's chunk load (vtable slot 22)
```

The list holds every instance id in most levels; in Outer Labs it misses
eight of them and its order differs from the records'. What it is for is not
known yet.

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
x + y of its nearest footprint cell. Not done yet: animation (the frames after
the first, their durations and codes), states (open, powered), lights.

An Umbilical Shell's first frame is only an outline of its footprint (editor
art); the original shows nothing there at rest, so the rebuild hides the
class until its states are decoded.

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
| Frame codes, mask cell fields, the instance list | guessed / open | see the text |

## Open questions

- Frame codes: the classes look for the frame whose code is 1 (Looped,
  Pillar), 7 (ChargingStn, consoles, Umbilical) or 8 (Spawn), so a code likely
  marks where an animation sequence starts.
- The mask cells' `kind`, `a`, `b` (walkability or occlusion), and the
  missing mask images.
- The light block and the script blocks.
- The meaning of the record class data (chest contents, console links, spawn
  slots).
