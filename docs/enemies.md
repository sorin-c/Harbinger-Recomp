# Enemies: how they notice the player, fight and flee

An enemy's mind is a `GRoleMind` (the GMind family) with a thinker (the
agent's template picks it) and a set of policies, each a list of actions.
Function names and addresses are from the original `Harbinger.exe`.

## The template's AI words

Version 1.8 and up end the `.agt` template with a block read by
`FUN_00407170` (proven, disassembly):

| Bytes | Where | What |
|---|---|---|
| 4 bytes | template+0xa..+0xd | flags; +0xc = 1 makes an enemy hostile to factions 1 and 2 |
| u16 | template+0x21c | faction: 1 player side (and its cameras and mines), 2 NPCs, 3 cimdrones and khapra, 4 Vantir, 5 and 6 Scint, 7 enemy cameras, mines and the repairbot, 8 biters and the overlord, 9 herps |
| u16 | template+0x220 | not traced |
| u16 | template+0x58 | thinker id |
| u8 (1.86 and up) | template+0xe | not traced |

Thinker ids (`GIThinker` factory, slot 1): 0 Player, 1 Stationary, 2 Poke,
3 Repair, 5 Animal, 6 Predator (biters), 7 Trooper (Vantir, walkers),
8 Commander, 9 Boss, 10 Mechanized (proven: factory switch and RTTI names).
The faction words are proven as read; the meaning of each number is likely
(from which templates carry it).

Policies (table 0x583b30): 0 Move, 1 Activate, 2 Talk, 3 Respond, 4 Attack,
5 Repair, 6 Poke, 7 Pickup, 8 Deploy, 9 VantirAwake, 0xa VantirBossAwake,
0xb ScintBossAwake, 0xc Inspect, 0xd Flee, 0xe Wander, 0xf StationaryAttack,
0x10 LongRangeAttack, 0x11 HardFlee, 0x12 FakeDie, 0x13 Flank, 0x14 Wait,
0x15 HerpAwake. Actions (table 0x583ab0): 0 Idle, 1 Move, 2 Activate,
3 Attack, 4 Turn, 5 Talk, 6 Die, 7 Repair, 8 Awake, 9 Stasis, 0xa Poke,
0xb Pickup, 0xc Deploy, 0xd AwakeTwo, 0xe Meditate, 0xf FakeDie,
0x10 FakeIdle, 0x11 BlindMove, 0x12 BlindIdle, 0x13 BlindTurn, 0x14 Wait
(proven: the tables and RTTI names).

## The mind

Mind fields: +0x2c action, +0x30 policy, +0x34 target, +0x3c policy step,
+0x40 time of the last think, +0x50 `GInstinctCtr` with three emotion bytes
at +0x54..+0x56 (clamped 0-255), +0x5c three roles and +0x68 the one in
play, +0x70 the thinker, +0x74 combat state, +0x78 the last target.

**Roles.** The level record's slot words (`chk::AgentRecord::slots`, read by
`GBugZapperPlug_v18`, record slot 18) fill the three roles; nothing found
changes the index at +0x68 from 0, so the first word plays. In the shipped
levels it is 0 (guarding: most), 1 (in stasis: the agents in spawners, and
turrets), 2 (not guarding), 3 (biters, bugs, skeeters: like 2) or 0xf
(meditating: some Scintilla NRG). `GRoleMind_v1` turns role 1 into the
Stasis action (9) and 0x12 into FakeIdle (0x10). Stasis never ends and
cannot be interrupted (`GStasisAction_v9` -> plug slot 0x170, false), so
the mind never thinks; it holds the first frame of the Awake animation
(state 7, `GStasisAction_v2` keeps restarting the clock). A spawner's wake
(`GVantirPlug_v1`, `GCimDronePlug_v1`, the plugs' `GIActivatable` slot 1)
sets all three roles to 2.

- **Rhythm** (`GMind_v6`): the mind thinks when more than 249 ms have passed
  since its last think; the first call only starts the clock.
- **Thinking** (`GRoleMind_v3`): when the action can be interrupted (slot 9:
  every action but stasis) the thinker decides, told whether the action is
  blind (slot 12: the Blind actions 0x11-0x13, Wait, Awake, Stasis, Die);
  if that starts no new policy, the policy's own tick (slot 5) runs. A blind
  agent picks no target (`FUN_00424700` with its last argument set), so a
  target it was fighting counts as lost: `FUN_004fac70` forgets it (combat
  state 0) without walking after it.
- When an action finishes the policy moves to its next step; when the
  policy runs out, `GRoleMind_v1` asks the thinker to decide, and if
  nothing starts, the thinker rests (slot 5).
- **Emotions** (`0x517530`, for Animal, Predator, Trooper and Mechanized):
  emotion 0, courage, starts at 80; emotion 1, restlessness, at rand % 200;
  emotion 2 at 127, the visibility another agent must exceed to be seen.
  Commanders and bosses (`GBossThinker_v2`) start with courage and
  restlessness at 255.
  Being hit (`GRoleMind_v8`) adds 60 courage and turns the agent on its
  attacker; each engage adds 1.

## Noticing (`FUN_00424700`)

An agent sees a candidate when all of these hold:

- distance squared within range squared: 9 cells (`DAT_00552ea0`), 5 while
  meditating (role 0xf);
- the candidate's visibility (plug+0x38) is above emotion 2;
- the line of sight is clear: `FUN_0044cb80` looks it up in a table built by
  `FUN_0044c330`/`FUN_0044c510`, which covers fewer than 16 half cells
  each way; a ray stops at a half cell whose height byte is above 3;
- the plug says it is hostile (slot 0x184): `GEnemyPlug_v97` is hostile to
  factions 1, 2 (only with template+0xc = 1) and 9; the player plug to
  every faction above 1.

**Friends.** The same pass looks at the agents it is not hostile to. Any in
sight (the line of sight, a square of the sight range; the original's box,
`FUN_0044ccd0` / `FUN_004e1410`, is not read) makes the pass report a flock
point (`FUN_00423750`): starting from the agent's facing as a unit vector,
each friend adds, per axis, a value looked up by the distance in quarter
cells (0..8, interpolated) in the table at 0x54df78: -1, -0.63, -0.29, 0.29,
0.68, 0.91, 0.27, 0.09, 0. The distances are taken unsigned (`fabs`), so
the push or pull does not depend on which side the friend is on. The point
is 0.75 cells (0x54dfe0) along the sum.

**Visibility** (plug+0x38, base +0x39 = 128): each loop of the idle
animation lowers it by 10 down to the base; each loop of the move animation
raises it by 10 up to 176; attacking or turning (`GAgentPlug_v58`) by 25 up
to 220; firing (`GAgentPlug_v19`) by 30 up to 255. It is 0 once hit points
reach 0. A player who stands still is seen less than one who runs.

## Deciding (thinker slot 4)

`GTrooperThinker_v4` (troopers) and the identical `GBossThinker_v4`
(commanders, bosses), and `GMechanizedThinker_v4` (predators, mechanized):

1. A target in sight: the weapon may switch (`FUN_00423160`, below), then
   engage it (troopers through `FUN_004fa8d0`).
2. Else a target lost (`FUN_004fac70`): unless blind, walk to where it is
   now (the Move policy); either way forget it (`GRoleMind_v14` clears +0x74
   and +0x78).
3. Else, not guarding (role 0) or meditating (0xf): troopers first look for
   a shut door with a console within 5 cells to open (`FUN_004254a0`:
   doors register in a map at world+0xc while their cells are mostly
   blocked, `FUN_004251e0` from `FUN_0044a7c0`; the console's target names
   the policy, id 0x16 = "ask the target"); then, with a flock point, the
   Move policy there. The Move policy always starts afresh (`FUN_004e5140`),
   so an agent with friends in view walks on at every think.

## Engaging (`FUN_004fa850`, `FUN_004fa9a0`)

A target of faction 2 (NPCs) is ignored. Courage goes up by 1 and a guarding
or meditating role becomes 2. Troopers (`FUN_004fa8d0`) then flee a target
brighter than 240; in combat state 6 they look for a door as above and,
finding none, go to state 5; and in state 0, a target whose base
visibility (+0x39) is under 128 makes them alert their friends in sight
(`FUN_004249d0` -> slot 0x1a8 -> `FUN_004fad00`: courage -15 and, at state
0, the player as target with state 9, which `GRoleMind_v3` turns into an
engage at the next think). Then `FUN_004fa9a0`, troopers never fleeing for
their wounds:

- A new target (+0x78) starts at state 1 (and, 65% of the time, a bark:
  slot 0x1a4). Template+0xc = 1 never flees.
- 1: flee (state 3, the Flee policy) when courage < 220 and hit points
  < 40% (0x5570c8 = 0.4); else attack.
- 2: the same flee test; else LongRangeAttack (0x10), or for thinker 1
  StationaryAttack.
- 3: flee while courage < 251, else back to 1 and attack.
- 4: HardFlee (0x11) while courage < 251, else back to 1 and attack.
- 7: state 8 and Flank (0x13). 8: state 0 and Wait (0x14).
- Anything else (0, 5, 6, 9): state 0 and Wander, started afresh each time.

"Attack" is the Attack policy (StationaryAttack for thinker 1). Asking for
a policy with a target that is already running (`FUN_004e50c0`) changes
nothing.

**How the state moves** (`GRoleMind_v13`, slot 13, called by the Move
action when it finds no path, `GMoveAction_v1` = `GBlindMoveAction_v1`):
1 -> 2, 2 and 4 -> 6, 3 -> 4, 5 -> 0, 6 -> 7, 7 -> 8. And `GRoleMind_v15`
(slot 15, called by the attack policy when the ranged reach says 3, out of
energy): 7 after anything but 7, 8 after 7. So: a target that cannot be
walked to is fought from the full shooting range; a shooter that runs dry
side-steps (Flank). Because Flank's actions are blind, the next think
forgets the target (state 0), so the Wait policy of state 8 does not come
in practice: once it sees the target again it is a new target, state 1.

## Resting (thinker slot 5)

- `GMechanizedThinker_v5` (predators, mechanized): role 0 idles (the Idle
  action, `GIThinker_v5`), role 0xf meditates (action 0xe: the state 1
  animation, looping, never ending, `GMeditateAction_v2`/`_v4`); others
  wander when restless (emotion 1 above 40, or plug+0x47 set, not traced),
  else idle.
- `GBossThinker_v5` (troopers, commanders, bosses): the same, but a
  restless one wanders only half the time (rand % 100 < 50); otherwise it
  lowers emotion 1 by 40 and patrols (`FUN_00423e40`): from the room it
  stands in, among the rooms its room's beacon links to (in link id order),
  it walks (the Move policy) to the beacon visited least (the first of
  equals), whose count goes up by 1; with no room or links it wanders.
- `GAnimalThinker_v5`: wanders when restless whatever the role.

**Rooms** (`FUN_004fad80`): a u16 id, four words into room+0x10 (not
traced), then four words into room+8..+0xe: x0, y0, width, height in walk
half cells. `FUN_004fb6e0` stamps the id into every half cell of that
rectangle (byte 2 of the 8-byte walk cell) and `FUN_0044b470` reads it back
for a point. room+0x18 is the beacon with the same id (`FUN_00436b30`);
the beacons' links resolve to rooms. Each beacon keeps a visit count
(beacon+0x20) that starts at 10 + rand % 6 (`FUN_004363b0`, from the
constructor) and starts again that way past 255. Across the 77 levels
23125 of 23211 beacons lie in their own room's rectangle (the rest sit on
its edge) and 98.9% of walkable half cells are in a room.

## Policies

- **Attack**: [Move, Attack]. The move is skipped when the target is already
  in reach; at the attack step a target out of reach sends it back to the
  move (`GAttackPolicy_v2`). The tick (`v5`) starts the attack once in reach
  and finds a new path when the target has moved. Reach 2 (no target) ends
  it, 3 (out of energy) tells the mind (slot 15) and ends it.
- **Wander**: [Move, Idle]. Heading = facing ± rand % 33 (the sign by
  rand % 100 > 49), distance rand % 3 + 2 cells. `FUN_00426bd0` tries 8
  headings around it, {0, +32, -32, +64, -64, +96, -96, +128}, weighted
  {10, 8, 8, 6, 6, 4, 4, 2}, keeps those it can walk straight, and breaks
  ties with a coin; with none it goes 1 cell along its facing.
- **Flee** and **HardFlee**: [Move] (`GMovePolicy_v1`'s list). Within 6
  cells (36 = 0x54e008 squared) of the threat, unless the agent's facing
  plus the policy's slot 8 (0, HardFlee 5) already points straight away, a
  step away from it: slot 9, 1.5 cells (0x54a048), HardFlee 0.75
  (0x54dfe0). No step ends the policy.
- **LongRangeAttack**: the Attack policy with the full shooting range.
- **Flank**: [BlindMove, BlindTurn, BlindIdle] (`GFlankPolicy_v1`). The
  move (`GFlankPolicy_v2`): within the attack range squared (slot 0x1ac,
  plug+0x220) a 1.25-cell step away from the target with the straight
  heading's weight lowered by 4 (to 6, so the diagonals' 8 win); outside it
  a step towards the target lowered by 9 (to 1). Then it turns to the
  target and idles one loop.
- **Wait**: [BlindTurn, BlindIdle] (`GWaitPolicy_v1`) at the target.
- **Headings** (`FUN_00426bd0`, used by Wander, Flee, Flank): besides the
  straight line (`FUN_00423aa0`), the point must not be taken by another
  agent it sees: one within the two radii of it (`FUN_00424f60`).
- **Moves**: a move that finds no path fails at once (after slot 13) and
  ends its policy (`FUN_004bd1b0`/`FUN_004bd160`).
- **Idle** lasts one loop of the idle animation (`GAgentPlug_v79` ->
  `FUN_0041dd10`).

## Reach and the swing

- Enemy (`GEnemyPlug_v3`): in reach when distance squared is at most
  (target radius + plug+0x230) squared. `FUN_00486b90` sets +0x230 to the
  melee animation's parameter / 90.51, or the agent's own radius + 0.1 when
  the parameter is 0. Ranged reach squared is +0x228: (the shoot animation's
  parameter / 90.51) squared; Infantry's 600 gives 6.63 cells.
- Player (`GAgentPlug_v3`): (own radius + target radius) squared + 0.1.
- The Attack action (`GAttackAction_v1`) calls slot 0xe8; `GAmpEnemyPlug_v82`
  swings again while the target is still in reach at the end of the
  animation. The blow lands as the player's do (`docs/combat.md`).
- An enemy's rating (slot 100, `GAgentPlug_v100`) is its stats[0] x 5.

## Firing

- Weapons: the template header (`FUN_00406a60`) keeps stats[0..3] at
  +0x38, +0x3a, +0x3c, +0x40 and stats[4] at +0x3e. `FUN_0041d4e0` fills the
  weapon holder (plug+0x1c0): slot 0 the melee weapon from +0x3e
  (`FUN_00526550`), slot 1 the ranged weapon from +0x40 (`FUN_00526740`),
  and sets the holder's mode (+0x10) to ranged when there is one. So stat 3
  names the ranged weapon; every stat 3 in the levels names a `.wpn` with a
  ranged tail (Infantry 4 `SmVantir.wpn`, Walker 5 `Walker1.wpn`).
- Reach (`FUN_00421940`): blocked when plug+0x4b is set; in melee mode the
  melee reach above; in ranged mode (`GAmpEnemyPlug_v4`) 3 when the weapon
  cannot fire (slot 0xd8, `GAgentPlug_v54`: the energy for a shot), 2 when
  the target is gone, else the distance squared against +0x228 for the
  LongRangeAttack policy and against +0x220 for the others. `FUN_00486b90` sets +0x228 = (shoot parameter / 90.51) squared and
  +0x220 = slot 111 (0.25, 0x54a310) x +0x228: half the range. Infantry and
  Walkers (600) shoot from 3.3 cells under the Attack policy.
- The shoot animation (state 3) runs like the player's: a -2 tag aims,
  leading a walking target (`FUN_0041f8d0`); the muzzle tag fires a bolt
  costing the weapon's energy, which recharges as the player's.
- **Blocked fire** (plug+0x4b): a shot whose line from the muzzle (its
  height + 0.1) to the aim point is blocked (`FUN_00402120`) sets it, for an
  agent that has move animations (`GAgentPlug_v19`); while set the target
  is out of reach, so the attack policy walks; `GAgentPlug_v80` clears it as
  the agent walks.
- **Switching weapons** (`FUN_00423160`, each time a thinker sees its
  target; the holder's slots 0 melee, 1 ranged, 2 a third kind, 3 either):
  never during an attack. Once more than 1.5 s (0x5dc) have passed since
  the last switch, each call has a 40% chance (rand % 100 > 59) of a
  "random" switch; a miss restarts the wait. In melee mode: within 1 cell
  never, otherwise only on that chance (or for target types 0x1d-0x20,
  0x24, 0x29), to ranged. In ranged mode: to melee within 2.75 cells
  (7.5625 squared), on the chance, or out of energy; never for those target
  types.
- `GLongRangeAttackPolicy` is `GAttackPolicy` with its own id and slot 8
  true (the full range).

## In the Junkyard (level 2)

| Enemy | Thinker | Defence | Melee weapon |
|---|---|---|---|
| Vantir Grunt | 7 Trooper | 27 | `VantirGrunt.wpn`, melee level 9, hit bonus 4 |
| Biter Yellow | 6 Predator | 40 | `Biter.wpn`, melee level 9 (+2) |

Level 3 (Storage) adds Vantir Infantry (26, stats 4 6 8 4 0) and Walker
M-86U (4, stats 5 5 7 5 0), which fire.

## Rebuild choices

- A walk toward the target stops once it is in reach (the original walks
  the whole path and then checks).
- Only the player is a target; NPCs are not attacked and do not fight.
- There are no squads: the record's extra word (plug+0x218) links the agent
  to its spawner's slot (`FUN_00419a50` -> `FUN_0044b6b0`: gizmo id in the
  low 12 bits, slot above; see docs/formats/gizmo.md, "Spawners").
- Troopers do not open doors (`FUN_004254a0`, policy 0x16): state 6 always
  becomes 5. Friends are not alerted (`FUN_004249d0` needs a target whose
  base visibility is under 128; the player's is 128).
- Thinkers 7, 8 and 9 use the trooper's logic, 1 the attack policy for
  StationaryAttack, the rest the predator's. Herps (5) rest as animals but
  do not inspect.
- Enemies' visibility is taken as its base (128); a flock point's sight
  test uses a square of the sight range. Turns are instant. plug+0x47
  (forces wandering) is taken as unset.
- `--at x,y` starts the player at a map point (for testing).
- `--at x,y` starts the player at a map point (for testing).

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| Template AI block | proven | `FUN_00407170` disassembly; every `.agt` parses to the last byte |
| Thinker, policy, action ids | proven | factory switch, tables 0x583b30 and 0x583ab0, RTTI names |
| Faction meanings | likely | which templates carry each number |
| Think every 250 ms | proven | `GMind_v6` |
| Emotion start values, +60 when hit | proven | `0x517530`, `GRoleMind_v8` |
| Sight range, visibility test, hostility | proven | `FUN_00424700`, `GEnemyPlug_v97` |
| Line of sight: height above 3 | likely | `FUN_0044c330`/`FUN_0044c510` build the table; the lookup is `FUN_0044cb80` |
| Visibility rules | proven | `GAgentPlug_v19`, `v58`, the idle and move loop handlers |
| Engage and flee thresholds | proven | `FUN_004fa850`, `FUN_004fa8d0`, `FUN_004fa9a0`; constants 0x5570c8 |
| Wander heading and weights | proven | `FUN_00426bd0` |
| Flee distances | proven | constants 0x54e008, 0x54a048 |
| Reach | proven | `GEnemyPlug_v3`, `FUN_00486b90`, `GAgentPlug_v3` |
| Trooper rest: patrol to the least visited neighbouring beacon | proven | `GBossThinker_v5`, `FUN_00423e40` disassembly, `FUN_004363b0` |
| Room rectangles in half cells | proven | `FUN_004fad80`, `FUN_004fb6e0`, `FUN_0044b470`; 99.6% of beacons inside their room |
| Roles from the record, stasis, meditation, wake sets 2 | proven | `GBugZapperPlug_v18`, `GRoleMind_v1`, `GStasisAction_v9`, `GMeditateAction_v2`, `GVantirPlug_v1` |
| Role index stays 0 | guessed | no writer of mind+0x68 found |
| Combat state machine, Flank and Wait | proven | `FUN_004fa9a0`, `GRoleMind_v13` (called by `GMoveAction_v1`), `GRoleMind_v15` (by `GAttackPolicy_v2`/`_v5`), `GFlankPolicy_v1`/`_v2`, `GWaitPolicy_v1` |
| Blind actions forget the target | proven | `GRoleMind_v3` slot 12, `FUN_00424700` last argument, `FUN_004fac70` |
| Flocking table and step | proven | `FUN_00423750`, table 0x54df78, 0x54dfe0 |
| Flock sight box | guessed | `FUN_0044ccd0` / `FUN_004e1410` not read |
| Weapon switching | proven | `FUN_00423160`, 0x54df68, 0x5492c0 |
| Fire-blocked flag | likely | `GAgentPlug_v19` sets it; `GAgentPlug_v80` clears it (when it runs is not read) |
| Occupied points, HardFlee numbers | proven | `FUN_00424f60`, `GHardFleePolicy_v8`/`_v9`, `FUN_00427810` |
| Stat 3 is the ranged weapon | proven | `FUN_00406a60`, `FUN_0041d4e0`; every level's stat 3 names a ranged `.wpn` |
| Ranged reach, attack policy at a quarter | proven | `FUN_00421940`, `GAmpEnemyPlug_v4`, `FUN_00486b90`, 0x54a310 |

Not compared under Wine yet. `tests/test_combat` runs Junkyard enemies
against a player standing still and checks that they notice, walk up, hit
and stop once the player is dead, and Storage Infantry against a player
five cells off: they come to 3.3 cells and fire until it is dead, and a
Storage walker that shoots itself dry side-steps and fights again.
