# Particle emitters (`.emt`)

`[Paths] Effects` = `effects.mgr` (177 entries, name `Effects.emt`) indexes
the emitter files. Every ranged weapon names its bolt's emitter by id (the
`.wpn` effect word, `docs/combat.md`); blood, sparks, impacts, amp effects
and the portal are emitters too. `GEffectsMgr_v5` loads one: a `GEmitter`
(0x130 bytes) reads itself with slot 6, `GEmitter_v6` (0x485b90).

Parser: `src/formats/emt.cpp`. Tests: `tests/test_combat.cpp` (all 177
files parse; 129 are decoded to the last byte).

## Layout

```
u16  version            1 or 2
u16                     emitter+0xd0
u32                     particles per burst? (0 or 1 in the weapons' files)
f64                     +0x100 (100.0 in the weapons' files)
f64                     +0x108
u32                     not kept
u8                      +0xd2
u8   flags              bit 0 -> +0xd3; bit 1 clears +0xd5
u16  physics type       FUN_00485ea0: 0 GPhysicsDesc, 1 GBasicDesc,
                        2 GCircularDesc, 3 GLinearDesc
...  physics description (its slot 3)
u16  particle type      FUN_00486130: 1 GBillboardDesc, 2 GRadialDesc, 3 GLineDesc
...  particle description (its slot 4)
version 2:
  u16  trail            +0xd6: another emitter started with this one (effects.mgr id)
  f64, f64              +0x110, +0x118
```

Files by type: 116 basic + billboard, 26 basic + radial, 18 circular,
13 plain physics, 4 linear. The rebuild decodes the plain physics
description, the basic one and the billboard; the circular, linear, radial
and line descriptions are not decoded yet.

### GPhysicsDesc (type 0, `GPhysicsDesc_v3` 0x4e8c90)

```
u16  version
u32  life               +8, ms
u32  life variation     +0xc, ms
```

### GBasicDesc (type 1, `GBasicDesc_v3` 0x4e93a0)

```
u16  version
u32  life, u32 variation      +8, +0xc (ms)
u8   polar angle              +0x10   angles are 256ths of a turn (0x556950 = pi / 128)
u8   polar variation          +0x12
u8   azimuth                  +0x11
u8   azimuth variation        +0x13   above 128 it counts down: 128 - value
f64  speed, speed variation   +0x18, +0x20   map cells per ms
f64  x3 start spread          +0x28, +0x30, +0x38
f64  x3 acceleration          +0x40, +0x48, +0x50   per ms
i16  fan                      +0x5c
u8   flags                    bit 0 -> +0x58, bit 1 -> +0x59, bit 2 -> +0x5a
```

How a particle starts (`GBasicDesc_v1`): polar = +0x10 + +0x12 * r and
azimuth = +0x11 + var * r, where r is the effects' random number in
[-1, 1) (`FUN_004f90b0`); with flag +0x59 the azimuth steps evenly through
the fan (+0x5c particles from +0x11 - var to +0x11 + var). The velocity is
(cos az sin p, sin az sin p, cos p) * (scale * +0x18 + r * +0x20), turned to
the emitter's heading (`FUN_004e9150`); +0x60 is the scale a weapon sets
(1 + 0.01 * its type 10 mods, at most 2). Without +0x5a the start offset is
a random point in the +0x28..+0x38 box. How it moves (`GBasicDesc_v2`):
velocity += acceleration * ms (or, with +0x58, towards the emitter), then
position += velocity * ms. Slot 8 gives how many particles a burst has:
+0x5c, or 1 (a weapon fires that many bolts); slot 7 the speed,
+0x60 * +0x18.

The weapons' bolts are level (polar 64) with no spread or acceleration;
HPlasma1 (the E-16's) flies at 0.005 cells a ms (5 cells a second) for
3000 ms.

### GBillboardDesc (type 1, `GBillboardDesc_v4` 0x4e7980)

First its picture (`GAnimatedImageDesc_v4` 0x4e70d0):

```
u16  version
str  image              a .tga path, served from the .gim of the same name
f64                     +0x20 (1.0 or 3.0 in the weapons' files)
u8   flags              bit 0 -> +0x2a, bit 1 -> +0x28, bit 2 -> +0x29
i16  frame w, frame h   +0x14, +0x16
i16  columns, rows      +0x18, +0x1a (the frame count is their product)
i16                     +0x1c
```

If the picture holds fewer frames than columns * rows it is taken whole as
one frame. Then:

```
f64 x4                  +0x30..+0x48 (0.4, 0, 1.0, 0 for HPlasma1)
u8                      +0x50
f64 x4                  +0x58..+0x70
u8   flags              bit 0 -> +0x78; bit 1 clear -> +0x79
```

HPlasma1's picture is `Effects\Images\Player\Human\1_Plasma\LV1.tga`, a
256 x 256 sheet of eight 128 x 64 frames of a bolt pointing along +x.

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| Header, type switches, version 2 tail | proven | `GEmitter_v6`, `FUN_00485ea0`, `FUN_00486130`; 129 files end exactly |
| GBasicDesc layout | proven | `GBasicDesc_v3`; constructor `FUN_004e8d70` defaults (life 1000, var 100) |
| How a particle starts and moves | proven / likely | `GBasicDesc_v1`, `GBasicDesc_v2`; the rotation `FUN_004e9150` is read, its axis order is likely |
| Burst count, speed | proven | `GBasicDesc` slots 7 and 8 (0x4e8e10, 0x4e8e30) |
| Picture layout | proven | `GAnimatedImageDesc_v4` disassembly |
| Billboard doubles: the first is the drawn size | guessed | 0.4 x 128 px looks right on screen; not traced |
| Frame time 100 ms x +0x20 | guessed | |
