# Agents (`.agt` templates, `.anm` animations, a level's placed agents)

Agents are everything that moves and thinks: the player, enemies, NPCs,
mines and cameras. A **template** (`.agt`) holds the class, stats, name and
the list of animations; each **animation** (`.anm`) lists, per direction, the
frames, which live in one `.gim` sheet per direction. A level places agents on
its map with **agent records** after the set gizmos.

Parsers: `src/formats/agt.cpp`, `src/formats/anm.cpp`, `chk::parseAgents` in
`src/formats/chk.cpp`. Loader and drawing: `src/world/level.cpp`. Tests:
`tests/test_world.cpp`.

## Which template

`Harbinger.ini [Paths] Agents` names `res\common\managers\agent.mgr` (112
entries), which maps agent ids to `.agt` paths. The three player templates
(`dude.agt`, `gladiator.agt`, `culibine.agt`) are not in it; the character
screen picks them.

## Classes

The first word of a `.agt` indexes the factory table at `0x5838d0`
(registrations at `0x404b..`):

| Class | Engine class |
|---|---|
| 0 | `GPlayer` |
| 1..19, 29..41 | `GAbstractAgent<...Plug>` enemies and mines; 33..36 are the cameras (Camera, RangedCamera, MeleeCamera, Camerakazi) |
| 20..28 | HumanNPC, GladiatorNPC, CulibineNPC, Ona, Tinker, SpyNPC, Scavenger, Wik, ScintillaCaptive |

## `.agt` layout (version 1.96 in most files)

`str` is the engine string (u32 length without the NUL, characters, NUL).

```
u16  class              read by GAgentMgr_v5 (0x418670)
-- slot 9, 0x405dc0 (every class) --
u16  type               agent+0x54
f64  version            FUN_00406a60 (header)
u16  unknown
f64  unknown, unknown
u16  stats[n]           n = 10 from 1.86, else 9; one fewer below 1.6, one fewer below 1.2
str  name
-- slot 2, the class's own load --
u8   type byte          checked against slot 7
NPC (0x408670):         u16, and a u16 more above 1.6
Player (FUN_0050bc80):  u32 n, n x 6 bytes
others (0x407140):      nothing
u16  unknown            version >= 1.4, agent+0x4c
-- FUN_00406100: animations --
u32  count
repeat count:           GAgentAnimCtr (vtable 0x54a05c)
  i16 kind              kind 2 derives a speed from param
  i16 param
  str path              the .anm
-- after the animations --
6 bytes                 agent+0x218
u16, u16                agent+0x46, +0x48
version > 1.2:  u16 lists, each u32 m and m x u16 (FUN_00406c30)
version > 1.4:  u8 k, k x 4 bytes
version > 1.8:  10 bytes, 11 above 1.86 (FUN_00407170)
```

## `.anm` layout

```
u16  header             FUN_0042ab00; bit 0x10: per-frame words. h = header & ~0x10:
                        h > 1: directions = h & ~1, hotspot = h & 1
                        else:  16 directions, hotspot = h
hotspot:  i16 x, y      FUN_00434b90 (GHotSpotTag)
u32  soundCount         FUN_0042abe0
repeat soundCount:      4 bytes (GSoundTag)
repeat directions:      FUN_0042a420
  u32 frames, u32 actions
  frames x str          .tga names (FUN_0042a850)
  actions x 6 bytes     GActionTag (FUN_0042aa30)
  frames x u16          when the per-frame flag is set (FUN_0042a500)
u32  n
n x str                 extra frames
```

Most character animations have 10 directions; the rest 16.

**Frame sheet** (`FUN_00429f30`): a direction's frames are cut from one sheet
named after the first frame's folder: `...\Idle\0\00.tga` is in
`...\Idle\fsp\Idle0FSP.gim`. This resolves for 5415 directions; 16 name a
`.gim` directly.

## Level agent records

After the set gizmos (`docs/formats/gizmo.md`):

```
repeat variantCount:    agent instance lists (as for gizmos)
  u16 n, n x u16 instance
u32  count
repeat count:           0x420520 and the class's slots
  u16  agent            agent.mgr id
  u16  instance
  u8   facing           plug+0x3a; 256 steps a turn (see Drawing)
  u8   unknown
  u16  n, n x u16       slot 18 (0x4e6590)
  f64  x, y             map position in cells
  u8   hasLabel         1: str label follows (slot 7)
  u8, u16, u16          version >= 1.22
  u16  extra            0x4e6670; absent for the player and the cameras (0, 33..36)
```

Records whose instance is not in the base variant's list are dropped, as for
gizmos. What follows the agents (triggers, sounds, rooms; loaders
`FUN_0051a6a0`, `FUN_00504a90`, `FUN_0044ab80`, `FUN_00436460`,
`FUN_004fb860`) is not decoded yet.

## Drawing

The facing picks the direction through `FUN_004e12d0`, which looks it up in a
256-entry table: `0x556154` for 16-direction animations, `0x556354` for
10-direction ones (any other count gives direction 0). The steps are uneven
because the view is isometric; `anm::directionFor` holds the tables as the
last facing of each direction. `GAgentPlug_v9` calls it with plug+0x3a and the
animation's direction count.

The rebuild draws each agent's first animation in its facing's direction
(falling back to the first direction with frames), frame 0, with the frame's hotspot on the agent's map position (the frame's
bottom centre when there is no hotspot). Agents are sorted with the gizmos and
walls by x + y.

**Marker colours.** Agent sheets reserve palette entries 0 to 5. The layer
compositor `FUN_004c2710` (which stacks a torso sheet on a base sheet) never
lets an entry of 5 or below overwrite art, and every frame sheet has the same
six colours there, in one of two channel orders:

| Entry | Most sheets | Vantir troopers, some torsos | Use (from the pixels) |
|---|---|---|---|
| 0 | green | red | key, never drawn |
| 1 | red | green | the cast shadow (a filled diagonal blob) |
| 2, 3 | 2/3, 1/3 green | 2/3, 1/3 red | the figure's anti-aliased edge |
| 4, 5 | (171,85,0), (85,171,0) | green, green | the shadow's edge |

So the shadow amount of an entry is its share of entry 1's channel. The
rebuild draws that as translucent black and drops the rest
(`render::agentMarkerAlpha`).

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| `.agt` layout, class branches | proven | functions above; every `.agt` in both archives (agent.mgr and the player templates) parses to the last byte |
| Class table and names | proven | factory table `0x5838d0`, RTTI names of the registered classes |
| `.anm` layout | proven | `FUN_0042ab00`, `FUN_0042a420`; all 550 `.anm` files parse to the last byte |
| Sheet name rule | proven | `FUN_00429f30`; the sheet exists for every direction that does not name a `.gim` |
| Level agent records | proven | `0x420520`, slot 18, `0x4e6670`; every level parses, every agent inside its map; Torvus Junction has two agents, the first named "Torvus" |
| No extra word for the player and cameras | likely | with it every level parses to the following section; reading one for a Camerakazi misaligns the next record |
| Hotspot is the ground point | guessed | it sits near the frame bottom in every animation that has one; Vantir Checkpoint troopers stand plausibly |
| Entries 0-5 are markers in agent sheets | proven | `FUN_004c2710` treats indices <= 5 as below art; survey of every frame sheet's first eight palette entries |
| Entry 1 is the shadow, 2-3 figure edge, 4-5 shadow edge | likely | where the pixels sit (shadow blob, outline of the figure, outline of the blob); drawing it so removes the green and red fringes |
| Facing to direction | proven | `FUN_004e12d0` and its two tables; the record's byte reaches plug+0x3a in `0x420520` |

## Open questions

- Which animation an agent starts in (the rebuild takes the first).
- The stats words, the fields after the animations, sound and action tags.
- The rest of the record fields (slot 18 words, the flag byte and words).
