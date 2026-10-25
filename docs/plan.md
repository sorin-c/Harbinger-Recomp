# Plan: from here to a playable, completable game

Written 2026-10-25, after PR #14. It lists what is left, grouped into
milestones in the order they should land, which of them can run as parallel
threads, and a rough size for each.

**Sizes** are counted in PR-sized pieces of work like the ones already merged
(#8 enemy AI, #11 HUD and inventory): **S** is under one such PR, **M** is
about one, **L** is two or three, **XL** is four or more.

## Where we are

In main: every archive and image format, levels, gizmos, agents, walking,
gear, melee and ranged combat, player health and death, enemy AI (Attack
policy only), spawners, portals between levels, trigger scripts and scripted
enemy moves, gizmo use, chest loot, the HUD and the inventory screen with
controller navigation, the front end (main menu, character screen, Options,
Load Game screen).

What the game still lacks falls into three groups:

1. **Systems the story runs on**, not started yet: NPCs, dialogue (1,390
   `.dlg` files), quests (156 `.qst` files, `GQuestMgr`), trade, the EZ-stash,
   umbilicals and their codes, experience and levels, saving. Without these
   the game cannot be finished, so they come first.
2. **Systems that are partly there**: the remaining AI policies, bosses,
   turrets and mines, gadgets (mines, cameras, amps), upgrade chips and the
   weapon mod panel, firing modes, enemy drops, effects and lights,
   environment hazards, the in-game menu, key configuration.
3. **Polish and platforms**: playthrough checks against the original under
   Wine, Bink video, Linux packaging, then Android and iOS.

**Decided 2026-10-25 (Sorin): Human first.** Character-specific work (gadgets,
upgrade chips, skill tree, firing modes) is built for the Human first, and the
first full playthrough is a Human one. The shared systems (dialogue, quests,
trade, saving) are built to work for all three from the start, but are only
tested end to end with the Human until M4b. Gladiator and Culibine follow as
their own pass (M4b plus their parts of 2c, 2d and 1c).

## Milestones

### M1. Story systems (critical path)

| # | Work | Size | Depends on | Notes |
|---|---|---|---|---|
| 1a | **NPCs and dialogue**: `GHumanNPC`/`GGladiatorNPC`/`GCulibineNPC`/`GSpyNPC` plugs, `.dlg` format (`GDialog`, `GDialogGroup`, `GDialogResponse`), the dialogue screen, the entity actions dialogue fires (`GSetQuestEntityAction`, `GGiveExpEntityAction`, `GLaunchTradeAction`, `GSetLevelEntityAction`, `GStatModAction`...) | L | none | Done (docs/dialogue.md). It also decoded `.qst` and runs the quest state, since quests pick each NPC's dialog. Left for others: GiveExp/StatMod (1c), trade and accounts (1d), group monitors |
| 1b | **Quests and the quest log**: `.qst` format (`GQuest`, `GQuestNode`), quest state, `GCtrlQuestLogButton`, the quest log panel (F1) | S | 1a's action list | `.qst` and the quest state came with 1a (`world::Story`, its `log()` holds the Quest String lines); left: the quest log panel and button, and group monitors. The `.hpr` save text shows what the log holds |
| 1c | **Experience and levels**: template+0x214 XP on kills, `GGiveExpEntityAction`, level thresholds, the stats panel (`GCtrlStatsLevel`, `GCtrlStatsSkillName/Value`), `.tre` (not a skill tree: one quest file, see docs/stats.md) | M | none | Done: docs/stats.md, with `GGiveExpEntityAction` |
| 1d | **Trade and EZ-stash**: `GCtrlTradeGrid`, tabs and pages, buy/sell prices, `GStash` shared across stashes, quick move (right-click / X) | M | the inventory (done); 1a for launching trade | |
| 1e | **Umbilicals, codes and the room graph**: umbilical pads, codes as game state, `GScriptCmdSetLevel`/`SwapLevel`, gold area names, level changes that are not portals | M | portals (done) | The readme calls this the main way to travel |
| 1f | **Saving and loading** in our own format: world state per visited level (gizmo states, dead agents, ground items), player, inventory, stash, quests, XP; quick save/load (F6/F7), autosave, the Load Game screen wired up, thumbnails | L | lands last in M1 but its design starts early, since every system above adds state | Original saves are not a goal |

1a, 1c, 1d and 1e can run as four threads at once. 1b follows 1a. 1f needs a
state-ownership design agreed first (a short doc), then grows with the others.

### M2. Combat completeness

| # | Work | Size | Depends on | Notes |
|---|---|---|---|---|
| 2a | **Remaining AI**: LongRange, Flank and Wait policies, squads (plug+0x218), flocking, trooper rest `FUN_00423e40`, `GRoleMind_v13` combat states, fire-blocked flag, weapon mode switching | L | none | Done (docs/enemies.md). "Squads" were the spawner links. Left: troopers opening doors (policy 0x16), alerting friends |
| 2b | **Bosses, turrets and mines**: `GVantirBossPlug`, `GScintBossPlug`, `GCimBossPlug`, `GBossThinker` and their awake policies; `GTurretPlug`, `GTurretMinePlug` | L | 2a helps | Every boss must be beatable for the game to be completable |
| 2c | **Gadgets and firing modes**: Human mines, Gladiator camera (with camera/body toggle), Culibine amps (`GAmplifierItem`, radial mods 7-0), ranged mods 1-4, the Ctrl rigging menu | L | none | Controller: rigging menu as a radial or bumper cycle |
| 2d | **Upgrade chips and the weapon mod panel**: `GModChipItem`, `GCtrlWeaponModBtn`, `weaponmodleft.itf`, stat changes on weapons, energy lines in tooltips, injector colours | M | the inventory | |
| 2e | **Drops and ground items**: enemy loot groups on death, the drop layout `FUN_004d9500`, dropping items from the inventory, the item swirl, Alt to highlight grounded items | M | none | |
| 2f | **Effects**: bolt lights and impacts, lob and bounce bolts, the 50 undecoded emitters, additive blending, level lighting | M | none | |
| 2g | **Environment**: heat, radiation, radiation sinks and poison zones with the HUD warning lights (`GCtrlWarningLight`), charging stations, health pack curing poison | S | none | |

All seven can run in parallel with each other and with M1.

### M3. Game shell

| # | Work | Size | Depends on | Notes |
|---|---|---|---|---|
| 3a | **In-game menu** (Esc / Start): resume, save, load, options, quit; death screen with restart (Space) | S | 1f for save/load buttons | |
| 3b | **Automap**: `GCtrlMap`, Tab, explored cells | M | none | |
| 3c | **Key configuration**: `keyconfig.itf`, KeyField, button tooltips (`GCtrlButton_v10`) | S | none | |
| 3d | **Controller pass**: one consistent D2R/D3-style scheme across every screen from M1-M3 (dialogue, trade, stash, map, quest log, stats, mods, rigging), button glyphs, auto-target for ranged attacks, gadget buttons, on-screen hints | M | each screen as it lands | Each screen thread does its own nav; this pass makes them consistent |
| 3f | **Aspect ratio and custom resolutions** (Sorin, 2026-10-25): an **aspect ratio** setting in Options, **4:3 or 16:9**, and within either a **custom resolution** (any size of that aspect, typed in Options or `display.ini`), for windowed and fullscreen; on a display of another shape the picture is letterboxed or pillarboxed to the chosen aspect. The world is drawn at the real output resolution (not stretched from a 1024x768 buffer) and scaled so the view is 768 world pixels tall, so 4:3 matches the original's view and 16:9 shows more of the level at the sides. HUD panels anchored to the screen edges (generalising the 800 -> 1024 move in `FUN_004c4af0`); the inventory and other full-screen panels centred; the front end and videos stay 4:3, pillarboxed in 16:9. Checks: camera limits at level edges, script camera positions and thought bubbles (given in 800x600 coordinates), and that enemies seen at the screen edge behave fairly (their sight is 9 cells) | M | none | Can start any time; screens added later just follow the anchoring rules. The original 800x600 and 1024x768 stay as 4:3 presets |
| 3e | **Video and ending**: Bink 1 playback for the logos, intro and ending (FFmpeg's decoder, or skip with a still), credits | M | none | The only part needing an outside library |

### M4. Playthrough and fidelity

| # | Work | Size | Depends on |
|---|---|---|---|
| 4a | **Full playthrough, Human**: start to ending in the rebuild, with each act compared to the original under Wine; a bug list per act | L | M1, 2b |
| 4b | **Full playthrough, Gladiator and Culibine** | L | 4a |
| 4c | **Fidelity sweep**: the numbers labelled *guessed* in `docs/` (health pack heal, shoot range, panel offsets...) checked against the original | M | 4a |

Playthroughs can start act by act as soon as M1 covers that act, so 4a overlaps
the end of M1 and M2.

### M5. Platforms

| # | Work | Size | Depends on |
|---|---|---|---|
| 5a | **Windows release**: versioned zip from CI, first-run data folder picker | S | M4 |
| 5b | **Linux**: AppImage or tarball, data path rules, CI artifact; Steam Deck as the controller test case | S | none (can start any time) |
| 5c | **Android** (bonus): SDL3 Android build, touch controls, picking the data folder | L | M4 |
| 5d | **iOS** (bonus) | L | 5c |

## Order and parallel threads

```
now ──► M1 story systems ─────────────────┐
   ├──► M2 combat completeness ────────────┤
   ├──► M3 shell (3b, 3c, 3e, 3f any time) ┼──► M4 playthroughs ──► M5 release, mobile
   └──► 5b Linux packaging (small, any time)
```

A sensible first wave of four threads: **1a dialogue + NPCs**, **1c
experience and stats**, **1e umbilicals and the room graph**, **2a remaining
AI**. Then **1b quests**, **1d trade and stash**, **2b bosses**, **2c
gadgets** as those finish, with the save design (1f) written early so each
thread records its state the same way.

Threads touching the same files should not run at once: 1a and 1b both own the
entity-action list, and 2c and 2d both change weapons and the inventory.

## Rough total

About 30-35 PR-sized pieces. With four threads at a time that is several weeks
of steady work, M1 being the long pole (dialogue and quests carry most of the
story).

## Risks

- **Dialogue and quest formats** are the largest undecoded area; 1a may grow.
- **Original comparison** needs saves or a level skip under Wine to reach late
  acts quickly; the playthrough threads may need a small Wine-side aid (a
  save from Sorin's own playthrough would help most).
- **Bink** may need FFmpeg as a dependency, which is heavy for mobile.
