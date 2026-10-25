# Levels (`.chk`) and brushes (`.brs`)

A level ("chunk", class `GChunk`) is a grid of map cells. Each cell paints one
or two tiles from the act's **brushes**: a floor, and on top of it a wall or an
edge piece. Objects (gizmos), characters (agents), triggers and the rest are
stored after the cells. Every section is decoded to the last byte of every
shipped level: set gizmos (`docs/formats/gizmo.md`), agents
(`docs/formats/agent.md`), and triggers, sounds, attachments, power,
beacons and rooms (below).

Parsers: `src/formats/chk.cpp`, `src/formats/brs.cpp`. Loader and drawing:
`src/world/level.cpp`. Tests: `tests/test_world.cpp`. Tool: `levelshot`
renders a whole level to a `.tga`.

## Which level, which brushes

- `Harbinger.ini [Paths] Levels` names `level.mgr`, which maps level ids to
  `.chk` paths (80 levels across both archives).
- A new game starts in **level 1**, `(G)Torvus Junction.chk`: `FUN_00497850`
  calls `FUN_00497cd0(this, 1)`, which looks the id up in `level.mgr`.
  Confirmed by running the original: after the intro the player stands in
  Torvus Junction.
- The level header names its **act**. `[ActN] Brushes` in `Harbinger.ini`
  names that act's `brush.mgr`, which maps brush ids to `.brs` files
  (`FUN_0049e2a0`, `GBrushMgr_v5`).

## `.chk` layout (version 1.23, every shipped level)

```
f64  version            1.23
u16  act                read when version >= 1.16 (FUN_0049e160)
u16  width, height      cells along x and y
u16  entryCount         FUN_004491c0 (version >= 1.16 branch)
repeat entryCount:      GEntryPt, FUN_0048b2c0
  u16  id
  f64  x, y             map cells
  u16  facing           a direction 0..15 (world::entryFacing, table 0x54fa58);
                        above 15, bit 4 is cleared and the entry is marked
                        closed for umbilicals (entry+0x2c), docs/umbilicals.md
  str  name             "Default", "Torvus Junction", ...
  i32  link             -1 on "Default"; meaning not confirmed
u16  unknown1          chunk+0x30
u16  music             chunk+0x32: sound.mgr id of the level music
u16  unknown3           version >= 1.17, chunk+0x20
str  name               version > 1.2: the level's display name
u16  unknown4           version > 1.21, chunk+0x36
repeat width*height:    map cells, FUN_00448080 / FUN_004e2d60
  cell
u16  variantCount       1 in every shipped level (the base level only)
repeat variantCount-1:  variants 1.., FUN_00448080
  u32  size             bytes to skip when the variant is not selected
  str  name
  u16  n
  repeat n:
    u16  index          cell index (x * height + y)
    cell                replaces that cell
...                     per-variant instance lists and set gizmos
                        (docs/formats/gizmo.md)
...                     per-variant agent lists and agents
                        (docs/formats/agent.md)
...                     triggers, sounds, attachments, power, beacons,
                        rooms (see "After the agents")
```

`str` is the engine string (u32 length without the NUL, the characters, a NUL).
A `cell` is a u16 layer count (0..2) and that many (u16 brush, u16 tile)
pairs: the brush id in the act's brush.mgr and the brush's tile id.

Cells are stored **column-major**: cell (x, y) is at index `x * height + y`
(`FUN_00447360` indexes `cells[a * dim24 + b]` and `FUN_004e2eb0` puts vertex
x at `a`).

## After the agents

`chk::parseRest`. The chunk load (`0x448..`, after `FUN_00418c40` loads the
agents) calls these in order:

```
-- FUN_0051a6a0: triggers --
u32  eventTriggers
u32  gizmoTriggers
repeat eventTriggers:   GEventTrigger, FUN_0048d200
  trigger               GGizmoTrigger_v6 (0x51a440): f64 x, y, radius; u16; u16 id
  f64  radius2          trigger+0x48 (equal to radius in the files seen)
  -- GEvent, FUN_0048c980 --
  u16  n                FUN_0048ca20
  n x u16               gizmo instances
  n x 8 bytes           undecoded
  u32, u8, u16          event+0x2c, +0x28, +0x34
  u16  n                FUN_0048cd20
  n x (u16 id, sound)   sound: see below
  u16  n                FUN_0048cbc0
  n x u16               instances
  u16  script           version > 1.19 (FUN_0048ce30): a script id, 0 = none;
                        1.18-1.19 kept the script inline, 1.17 and older nothing
repeat gizmoTriggers:   GGizmoTrigger, FUN_004baa50
  trigger
  u16  gizmo            the gizmo instance it fires (GIActivatable slot 1 with
                        (-1, id); portals read the id: docs/formats/gizmo.md)
-- FUN_00504a90: placed sounds (none in the shipped levels) --
u32  n
n x sound               GSoundInfo, FUN_005037c0: f64 x, y; u16 sound; u16; u32
-- FUN_0044ab80: links between gizmos --
repeat until id == 0:   GIAttacher (FUN_004bdcc0)
  u16 id, u32 n, n x u16    the gizmos it carries
repeat until id == 0:   GIPowerable (0x4d4ef0)
  u16 id, u16 n, n x u16    the gizmos it powers (slot 1 adds each as a target)
-- GChunk_v3 (0x44b200), the chunk's slot 3 --
u16                     chunk+6
u16  n
n x (u16 id, u16 level) power sources: gizmos that start with `level` power
-- FUN_00436460: beacons (GRoomBeacon) --
u32  n
repeat n:               GBeacon_v4 (0x436410), GRoomBeacon_v4 (0x4fb010)
  f64  x, y
  u16  room
  u32  m
  m x u16               linked beacons
  m x u32               a cost per link
-- FUN_004fb860: rooms (GRoom, FUN_004fad80) --
u32  n
n x (u16 id, 4 x u16, 4 x u16)
```

Every level has as many beacons as rooms, one beacon per room, each linked
to its neighbours: a room graph, most likely for path finding across the map.
The script ids name scripts in the character's script manager
(`[Human|Gladiator|Culibine] Scripts`, e.g. `res\common\managers\hscript.mgr`),
see `docs/formats/script.md`.

## `.brs` layout

```
str  name
u16  kind               1 for wall sets, 0 for floors
u16  height             wall height in 64 px units (brush+6 = height << 6)
u8   walkable           1 = floor the player can walk on (FUN_0044a230)
repeat 13 groups:       FUN_0043f290
  u16  count
  repeat count:
    u16  tileId         (group << 12) | index
    str  path           ".tga"; the archives serve the ".gim"
u16  categoryCount      read by the engine only in its editor build
repeat categoryCount:
  u16  id
  str  name             "Chests", "Wall 1 Basic", ...
```

The 13 groups are, by their file names: centre (`ca`, `cb`), N, NE, E, SE, S,
SW, W, NW, FN, FE, FS, FW.

**Centre tiles** are seamless 256x256 textures. One centre entry with id
`0xN0` registers 16 tile ids, `0xN0` to `0xNF` (a low nibble `k` other than 0
registers only `k` ids). The low nibble picks where the cell's 128x64 diamond
sits in the texture: `FUN_004e2eb0` scales the diamond's UVs by (0.5, 0.25)
and offsets them by `0.5*(k&1) + 0.25*((k>>1)&1)` in u and `0.125*(k>>1)` in
v. In pixels the diamond's box starts at
`(128*(k&1) + 64*((k>>1)&1), 32*(k>>1))`, wrapping. Level files pick `k` per
cell with no pattern (the editor randomises it).

**Other tiles** are whole images: 128x64 for floor edges, 128x256 for walls.

## Drawing

The original builds Direct3D quads in map space (`FUN_004e2eb0`): vertex `i`
of a cell is at (x + (i&1), y + (i>>1)), with UVs that put the image's top
centre at (x, y), its right corner at (x+1, y), its left corner at (x, y+1)
and its bottom at (x+1, y+1). So on screen +x runs down-right and +y
down-left, and a cell is a 128x64 diamond:

```
screen = ((x - y) * 64, (x + y) * 32)
```

The rebuild draws each tile with its bottom on the bottom of the cell's
diamond, floors first and then the second layer back to front. That matches
the original on screen; the original's walls are 3D quads with a depth
buffer, which the rebuild does not reproduce yet.

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| Header, entry points, level name | proven | `FUN_0049e160`, `FUN_004491c0`; all 80 levels parse; every entry point lies inside its map |
| Version constants 1.16, 1.17, 1.2, 1.21, 1.23 | proven | doubles at `0x54fb60`, `0x54fb58`, `0x54a040`, `0x54fb50`, `0x54fb48` |
| Variants: count, then per variant size, name, replaced cells | proven (layout) | `FUN_00448080` reads and seeks; the chunk load passes the selected variant to it and to `FUN_004477e0`; every shipped level has the base only, so the variant path is untested on data |
| Cells: count then (brush, tile) pairs, at most 2 | proven | `FUN_004e2d60` (cell struct 0x28 bytes holds 2 layers); every level parses with width*height cells |
| Column-major order | likely | `FUN_00447360` index math; entry points (x < width, y < height) on non-square maps; rendered levels join seamlessly |
| `.brs` layout, 13 groups, editor categories | proven | `FUN_0043f290`; all 20 brushes parse to the last byte |
| Centre ids cover 16 tiles; UV offsets | proven | `FUN_0043f290` loop; `FUN_004e2eb0` constants 0.5, 0.25, 0.125 at `0x552370`, `0x556684`, `0x54a310`, `0x556688` |
| Brush height = wall height / 64 | likely | `brush+6 = height << 6`; wall images are 128x256 (64 floor + 192 = 3 * 64 wall) on brushes with height 3 |
| Projection | proven / likely | vertex and UV order in `FUN_004e2eb0` (proven); the 64/32 pixel scale matches the 128x64 tiles and the original's screenshots (likely) |
| Header word at chunk+0x32 is the level music | proven | `FUN_00497cd0` passes it to `FUN_005056c0`; it is a sound.mgr id of an MP3 in every level (Torvus Junction: 302 = `Haven.mp3`) |
| Sections after the agents | proven | the functions above; all 93 level files parse to their last byte |
| Beacons and rooms form a room graph | likely | counts equal in every level; links name nearby beacons |
| Level 1 starts a new game | proven | `FUN_00497850` -> `FUN_00497cd0(this, 1)`; the original under Wine shows Torvus Junction |

## Open questions

- The undecoded fields after the agents: the 8-byte entries and words of an
  event, the room words, the meaning of the beacon costs.
- What the trigger fields around the script id do (the scripts themselves:
  `docs/formats/script.md`).
- The three unknown header fields.
- Lighting: the original darkens the floor away from lights and the player.
- What the F* tile groups are.
