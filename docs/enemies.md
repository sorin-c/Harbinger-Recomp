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
at +0x54..+0x56 (clamped 0-255), +0x5c the role list (its +0x68: 0 guarding,
0xf meditating, other values not guarding), +0x74 combat state, +0x78 the
last target.

- **Rhythm** (`GMind_v6`): the mind thinks when more than 249 ms have passed
  since its last think; the first call only starts the clock.
- **Thinking** (`GRoleMind_v3`): every action the rebuilt enemies use can be
  interrupted, so the thinker decides; if that starts no new policy, the
  policy's own tick (slot 5) runs.
- When an action finishes the policy moves to its next step; when the
  policy runs out, `GRoleMind_v1` asks the thinker to decide, and if
  nothing starts, the thinker rests (slot 5).
- **Emotions** (`0x517530`, for Animal, Predator, Trooper and Mechanized):
  emotion 0, courage, starts at 80; emotion 1, restlessness, at rand % 200;
  emotion 2 at 127, the visibility another agent must exceed to be seen.
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

**Visibility** (plug+0x38, base +0x39 = 128): each loop of the idle
animation lowers it by 10 down to the base; each loop of the move animation
raises it by 10 up to 176; attacking or turning (`GAgentPlug_v58`) by 25 up
to 220; firing (`GAgentPlug_v19`) by 30 up to 255. It is 0 once hit points
reach 0. A player who stands still is seen less than one who runs.

## Engaging (`FUN_004fa850`, `FUN_004fa9a0`)

Courage goes up by 1 and a guarding or meditating role becomes 2. Then:

- Predators (biters): in combat state 1 they flee when courage is below 220
  and hit points below 40% (0x5570c8 = 0.4).
- Troopers (`FUN_004fa8d0`): flee when the target's visibility is above
  240; otherwise hit points never make them flee.
- Combat state 3 keeps fleeing while courage is below 251.
- Otherwise the Attack policy (StationaryAttack for thinker 1). Starting the
  policy already running (`FUN_004e50c0`) does not count as a new plan.

A target that is no longer seen (`FUN_004fac70`): once, the agent walks to
where the target is now, then forgets it (`GRoleMind_v14` clears +0x74 and
+0x78).

## Resting (thinker slot 5)

Role 0 idles, role 0xf wanders; otherwise the agent wanders when emotion 1
is above 40. Troopers (`GBossThinker_v5`) wander only half the time; the
other half they lower emotion 1 by 40 and call `FUN_00423e40` (not traced).

## Policies

- **Attack**: [Move, Attack]. The move is skipped when the target is already
  in reach. The tick (`v5`) starts the attack once in reach and finds a new
  path when the target has moved.
- **Wander**: [Move, Idle]. Heading = facing ± rand % 33 (the sign by
  rand % 100 > 49), distance rand % 3 + 2 cells. `FUN_00426bd0` tries 8
  headings around it, {0, +32, -32, +64, -64, +96, -96, +128}, weighted
  {10, 8, 8, 6, 6, 4, 4, 2}, keeps those it can walk straight, and breaks
  ties with a coin; with none it goes 1 cell along its facing.
- **Flee**: [Move]. Within 6 cells (36 = 0x54e008 squared) it moves 1.5
  cells (0x54a048) away from the threat.
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
  melee reach above; in ranged mode (`GAmpEnemyPlug_v4`) the distance squared
  against +0x228 for the LongRangeAttack policy and against +0x220 for the
  others. `FUN_00486b90` sets +0x228 = (shoot parameter / 90.51) squared and
  +0x220 = slot 111 (0.25, 0x54a310) x +0x228: half the range. Infantry and
  Walkers (600) shoot from 3.3 cells under the Attack policy.
- The shoot animation (state 3) runs like the player's: a -2 tag aims,
  leading a walking target (`FUN_0041f8d0`); the muzzle tag fires a bolt
  costing the weapon's energy, which recharges as the player's.
- Combat states (`FUN_004fa9a0`): 1 Attack, 2 LongRangeAttack (0x10; thinker 1
  StationaryAttack 0xf), 3 and 4 Flee and HardFlee (0x11), 7 Flank (0x13),
  8 Wait (0x14), others Wander. `GRoleMind_v13` steps them 1->2, 2 and 4->6,
  3->4, 5->0, 6->7, 7->8, but nothing in the binary calls it except through
  its vtable (0x557074 slot 13, offset 0x34) and no `call [reg+0x34]` site
  is a mind's. The rebuild keeps state 1, so enemies fight under Attack.
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
- No flocking and no following of a squad leader (the record's extra word,
  plug+0x218, is the squad id: `FUN_00419a50`).
- Thinkers other than 6, 7 and 10 use the predator's logic.
- What blocks firing (plug+0x4b) is not traced; the rebuild checks the line
  of fire at 0.7 cells (`Foes::kMuzzleGuess` + 0.1).
- A shooter short of energy ends its Attack policy and starts it again at
  the next think.
- An agent with both weapons always shoots (the holder's mode switches are
  not traced).
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
| Trooper rest | likely | `GBossThinker_v5`; `FUN_00423e40` not traced |
| Stat 3 is the ranged weapon | proven | `FUN_00406a60`, `FUN_0041d4e0`; every level's stat 3 names a ranged `.wpn` |
| Ranged reach, attack policy at a quarter | proven | `FUN_00421940`, `GAmpEnemyPlug_v4`, `FUN_00486b90`, 0x54a310 |
| Combat state 1 never advances | likely | no caller of `GRoleMind_v13` found besides its vtable |

Not compared under Wine yet. `tests/test_combat` runs Junkyard enemies
against a player standing still and checks that they notice, walk up, hit
and stop once the player is dead, and Storage Infantry against a player
five cells off: they come to 3.3 cells and fire until it is dead.
