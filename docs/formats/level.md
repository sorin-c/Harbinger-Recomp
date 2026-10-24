# Levels (`.chk`) and brushes (`.brs`)

A level ("chunk", class `GChunk`) is a grid of map cells. Each cell paints one
or two tiles from the act's **brushes**: a floor, and on top of it a wall or an
edge piece. Objects (gizmos), characters (agents), triggers and the rest are
stored after the cells and are not decoded yet.

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
  u16  facing
  str  name             "Default", "Torvus Junction", ...
  i32  link             -1 on "Default"; meaning not confirmed
u16  unknown1          chunk+0x30
u16  music             chunk+0x32: sound.mgr id of the level music
u16  unknown3           version >= 1.17, chunk+0x20
str  name               version > 1.2: the level's display name
u16  unknown4           version > 1.21, chunk+0x36
repeat width*height:    map cells, FUN_00448080 / FUN_004e2d60
  u16  layerCount       0..2
  repeat layerCount:
    u16  brush          id in the act's brush.mgr
    u16  tile           brush tile id
...                     the brush list, gizmos, agents, triggers, sounds,
                        attachments, rooms, path cells (not decoded)
```

`str` is the engine string (u32 length without the NUL, the characters, a NUL).

Cells are stored **column-major**: cell (x, y) is at index `x * height + y`
(`FUN_00447360` indexes `cells[a * dim24 + b]` and `FUN_004e2eb0` puts vertex
x at `a`).

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
| Cells: count then (brush, tile) pairs, at most 2 | proven | `FUN_004e2d60` (cell struct 0x28 bytes holds 2 layers); every level parses with width*height cells |
| Column-major order | likely | `FUN_00447360` index math; entry points (x < width, y < height) on non-square maps; rendered levels join seamlessly |
| `.brs` layout, 13 groups, editor categories | proven | `FUN_0043f290`; all 20 brushes parse to the last byte |
| Centre ids cover 16 tiles; UV offsets | proven | `FUN_0043f290` loop; `FUN_004e2eb0` constants 0.5, 0.25, 0.125 at `0x552370`, `0x556684`, `0x54a310`, `0x556688` |
| Brush height = wall height / 64 | likely | `brush+6 = height << 6`; wall images are 128x256 (64 floor + 192 = 3 * 64 wall) on brushes with height 3 |
| Projection | proven / likely | vertex and UV order in `FUN_004e2eb0` (proven); the 64/32 pixel scale matches the 128x64 tiles and the original's screenshots (likely) |
| Header word at chunk+0x32 is the level music | proven | `FUN_00497cd0` passes it to `FUN_005056c0`; it is a sound.mgr id of an MP3 in every level (Torvus Junction: 302 = `Haven.mp3`) |
| Level 1 starts a new game | proven | `FUN_00497850` -> `FUN_00497cd0(this, 1)`; the original under Wine shows Torvus Junction |

## Open questions

- The rest of the `.chk`: brush list, set gizmos (`FUN_004495a0`: i32 count,
  then per gizmo a u16 class and the class's own data), agents
  (`FUN_00418c40`), triggers, set sounds, attachments, rooms, path cells.
- The three unknown header fields.
- Lighting: the original darkens the floor away from lights and the player.
- What the F* tile groups are.
