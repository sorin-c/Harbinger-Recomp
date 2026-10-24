# Combat: the player's blows, enemy hit points and death

This covers what the rebuild has of fighting so far: the player walks up to
an enemy and swings its melee animation at it; each blow rolls to hit and
for damage the way the original does; an enemy whose hit points run out
plays its death animation and stays on the floor as a corpse. Enemies do not
fight back yet (no AI), and there is no ranged attack, HUD or experience.

Code: `src/formats/wpn.*` (weapon files), `src/world/combat.*` (random
numbers, hit points, the attack roll), `src/world/foes.*` (enemies as
targets), `src/world/player.*` (the swing), `src/app/world_view.*` and
`src/app/harbinger.cpp` (input). Tests: `tests/test_combat.cpp`.

## Weapons (`.wpn`)

`[Paths] Weapons` = `weapons.mgr` (865 entries) indexes the `.wpn` files.
An equipment item names its weapon by id (`itm::Socketed::word38`, e.g. the
E-16 Rifle item -> 87 `Weapons\Human\E-16.wpn`); an agent template names its
ranged and melee weapons in stats 3 and 4.

```
f64  version        1.2 (5 files 1.1)
u16  kind           the weapon manager's factory word: 0 melee, 1-3 ranged, 4 armour
version > 1.1 (GWeapon_v7, 0x51f780):
  u8  hitBonus      weapon+0x2c, added to the attack roll
  u8                weapon+0x2d
u16  n              FUN_0051fb20
n x  u16 type, u16 level, u16 extra   -> FUN_005202b0: a map type -> (level, extra)
ranged kinds: 26 or 28 more bytes (energy and projectile; not decoded)
```

All 865 files parse; the melee and armour kinds end exactly after the list.
Damage types as the attack roll uses them: 0 melee, 1 plasma, 2 EMP, 3 and 4
other damage, 5-8 effects (their own table, applied through another call),
9 an armour's class, 10 not damage. The E-16 has melee level 7 and plasma
level 5, and Alpha Advance Armor type 9 value 40: the same as the
original's inventory tooltips ("Melee: Lvl. 7 (6 - 15)", "Plasma: Lvl. 5
(4 - 10)", "AC: 40").

## Hit points

`FUN_00408110`, called when an agent is set up (it fills plug+0x88 and
+0x90): `stats[0]` rolls of `stats[1] + rand % (stats[2] - stats[1])`, or
`stats[0] * stats[2]` when the two are equal. (The template header
`FUN_00406a60` puts stats 0, 1, 2 at template+0x38, +0x3a, +0x3c.) The
player plugs set their own maximum instead (the Human's constructor
`FUN_004f26c0` writes 70.0 to +0x94).

## Random numbers

`FUN_004f9050`: two multiply-with-carry halves, `a = (a & 0xffff) * 18000 +
(a >> 16)`, `b = (b & 0xffff) * 0x78b7 + (b >> 16)`, returning `(b & 0xffff)
+ (a << 16)`. `FUN_004f9030` seeds them with `s | 1` and `s | 2`. The
rebuild seeds from the clock when a level starts.

## The attack roll (`FUN_00452290`, a blow)

1. `roll = rand % 50 + 1`.
2. `total = rating + roll + weapon hitBonus (+ mod-chip skill bonuses)`. The
   rating is the attacker's slot 100 (`GHumanPlayerPlug_v100` returns
   +0x23c for melee, +0x23e for ranged); the player plugs' constructors
   set both to 15.
3. The blow hits if `roll >= 49` or `total >= ` the target's defence
   (template+0x10, the word after the `.agt` version: 20 for a brown biter,
   27 for a Vantir Grunt).
4. For each damage type of the weapon except 5-10: the level's range from
   the table at 0x54ffb0 (rows of min, max: level 1 = 1-4 ... level 7 =
   6-15 ... level 20 = 30-60), `points = min + rand % (max - min)`; then
   the target's resistance to that type in percent (template+0x204, the
   `.agt` list of (type, percent) pairs after the animations, read by
   `FUN_004085a0`; a type not listed does nothing) gives
   `damage += (int)(percent * 0.01 * points)`.
5. If any type got through: at least 1, and doubled on a roll of 49 or 50.

The blow kills when the damage reaches the target's hit points
(`FUN_004a2590` -> `FUN_00420a50`).

The rebuild uses only the weapon's melee type for a blow; whether the
original adds the rifle's plasma to a rifle-butt blow depends on which
weapon object `FUN_004213a0` hands over, not traced (guessed).

## The swing

- Clicking an enemy queues `GAttackAction`; the attack starts by turning to
  the target (`GAgentPlug_v58`) and plays the melee animation (state 4,
  `Dude\Chop\ChopBase.anm` plus the equipment's chop layers).
- The animation's action tags (`GActionTag`, 6 bytes: frame, x, y) drive it
  (`GAgentPlug_v9`, 0x41f330):
  - `x = -3` (ChopBase, frame 9, y = -9): while the attack goes on, move the
    animation's start time by `y` frames (back to frame 0) and face the
    target again: the swing repeats.
  - `x = -2` (ShootBase, frame 0): fire (ranged, not rebuilt yet).
  - any other x: the blow lands (`GAgentPlug_v14` -> slot 5 ->
    `FUN_004a2590`). The Human's rifle layers carry it at frame 6
    (`ChopRifle2.anm`: (6, 159, 103) in direction 0).
- Where it lands (`FUN_0042a6a0`): the tags of one frame, over all
  directions, circle the agent; `mean` is their average |y|. A tag's
  offset is `dx = (x - hotspot x) / 128`, `dy = (|y| - mean) / 64` (a screen
  offset at the circle's height, `(hotspot y - mean) / 64` above the
  ground), turned into the map as `(dy + dx, dy - dx)`. For the Human's
  rifle that is about 0.65 cells ahead.
- `FUN_004a2590` then takes the segment from the attacker to that point and
  hits the first agent whose square (template+0x20, about 0.39, + 0.1 each
  side) it touches (`FUN_0048b510`).

## Death

A dead enemy plays its death animation (state 5) once at 100 ms a frame and
keeps its last frame: the corpse stays.

## Rebuild choices

- **Reach**: the player starts swinging within 0.9 cells of the target's
  centre (the original's approach distance is in the player thinker, not
  traced). A swing repeats while the target is alive and within reach.
- **Picking**: a click counts as on an enemy when it falls in a box around
  its feet's screen point, roughly a body's size; the pad's attack (X /
  Square, the right trigger) and F take the nearest live enemy within 4
  cells, favouring those ahead.
- **Which agents are enemies**: classes 1-19 and 37-40 (not the NPCs 20-28,
  the mines and cameras 29-36 and 41).
- A small red bar over the enemy being fought stands in for the HUD's.
- Walking or steering cuts a swing short.

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| `.wpn` layout | proven | `GWeapon_v7`, `FUN_0051fb20`, `FUN_005202b0`; all 865 files parse; E-16 and Alpha Advance match the tooltips |
| Damage table | proven | 0x54ffb0; levels 5, 6, 7 match the E-16 tooltip ranges |
| Attack roll | proven | `FUN_00452290` disassembly (0x452303-0x45258a) |
| Player rating 15 | proven | `FUN_004f26c0`, `FUN_004baa80` |
| Defence = template+0x10 | proven | `FUN_00406a60` stores the word after the version there; `FUN_00452290` compares it |
| Resistances | likely | `FUN_004085a0` reads template+0x204 by type; the `.agt` pairs fill it (loader not traced) |
| Hit points | proven | `FUN_00408110`; stats offsets from `FUN_00406a60` |
| Swing loop (-3) and blow tags | proven | `GAgentPlug_v9`, `GAgentPlug_v14` |
| Blow point | proven / likely | `FUN_0042a6a0`, `FUN_0042c450`; that the body animation holds the layers' tags is likely |
| Segment against the target's square | likely | `FUN_0048b510`'s outcode tests; the argument order is inferred |
| Melee damage only for a blow | guessed | see above |
| Approach distance, picking | rebuild choice | |

Not compared under Wine yet: the first enemies are a level away from the
start, and the original's save files are not used.
