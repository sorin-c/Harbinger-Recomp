# Experience, levels and the stats panel

Plan item 1c: the player earns experience from kills (and from scripts),
levels up, gets points to spend on skills, and the stats panel shows all of
it. Labels: **proven** (checked against the original running under Wine or
against the game files), **likely** (read from the code, not seen running),
**guessed**.

Code: `src/world/stats.*` (experience, levels, skills, tooltip texts),
`src/world/player.*` (what a level-up and a spent point change),
`src/app/world_view.*` (kill experience, messages, the level-up sound, the
view shift), `src/app/hud.*` (the panel). Tests: `tests/test_stats.cpp`.

## Player plug fields

| Field | What | Evidence |
|---|---|---|
| +0x234 u32 | Experience | FUN_004ef480, FUN_004f18f0 |
| +0x23c u16[7] | Skills: Melee, Ranged, Radial, Rigging, Override, Focus, Life (names at 0x54fed0 + i × 0x20) | FUN_00452790; the order matches the panel under Wine: **proven** |
| +0x24a u16 | Points to spend | FUN_004ef480 (+3 a level), slot 115 (−1) |
| +0x24c u16 | Level, from 1 | the ctors, FUN_004ef480 |
| +0x214 u8 | Level seen: 1 from the ctor, 0 at each level-up, 1 when the panel opens | GCtrlStatsThreeStateBtn_v7: **likely** |

Starting skills (the plug constructors), **proven** for the Human by the
panel under Wine (15 / 15 / 10 / 15):

| Character | Ctor | Skills shown (slot 119) | Values |
|---|---|---|---|
| Human | FUN_004f26c0 | Melee, Ranged, Rigging, Life | 15, 15, 10, 15 |
| Gladiator | FUN_004baa80 | Melee, Ranged, Override, Life | 15, 15, 10, 20 |
| Culibine | FUN_0046fe50 | Ranged, Radial, Focus, Life | 15, 15, 15, 10 |

## Levels

The u32 table at 0x556b9c, read at `[level]`, is the experience that ends
that level: 1000, 2000, 3500, 5000, 10000, 15000, 20000, 25000, 30000,
40000, 50000, 65000, 80000, 100000, 125000, 150000, 175000, 200000, 225000,
250000, 275000, 300000, 325000, 350000, 375000, 400000, 425000, 450000,
500000, 550000, 600000, 650000, 700000, 800000, 900000, and `[36]` =
0xffffffff, so 36 is the last level. **likely** (the Wine panel shows
"Exp: 0 / 1000" at level 1: **proven** for `[1]`).

FUN_004ef480 adds experience, then for each threshold passed: +3 points,
level +1, clears +0x214, sets full hit points from slot 39
(2 × (Life + 20)), refills the ranged weapon's energy, shows "You've reached
level %hu!" for 10000 ms (FUN_004f00d0) and plays `[Paths] LevelUpSnd`.
**likely**. The rebuild does the same, but skips the heal and refill for a
dead player (**guessed**; the original does not check).

FUN_004a5310(game, amount, announce) awards experience and, when asked,
shows "N Experience Awarded." for 3000 ms. **likely**.

## Kill experience

GAgentPlug_v61 (FUN_00420a00) gives the player, when an enemy dies,
`template+0x214 + (int)plug+0x90`: the template's experience word (stat 8 of
the `.agt`, see `docs/formats/agent.md`) plus the enemy's full hit points,
and nothing at all when the word is 0. It is not announced. **likely**.
73 agent templates have a non-zero word (Vantir Infantry 25, the Overlord
10000); the player and the NPCs have 0 or 1. **proven** (the files).

`GGiveExpEntityAction_v8` calls FUN_004a5310 with announce = 1. The rebuild
has `WorldView::awardExperience(amount, announce)` ready for it; the
dialogue work (1a) wires the action.

## Skills

- Slot 120 raises a skill the character has, up to 50 (0x32). Slot 115
  (a plus button) spends one point on it. **likely**.
- Ratings: the attack roll uses Melee or Ranged (FUN_00452290). The panel
  adds the bonus FUN_004f25e0 computes: the melee weapon's hitBonus
  (weapon+0x2c) for Melee and the ranged weapon's +0x2d for Ranged, plus
  mod chip effects 0xd / 0xf (Melee) and 0xe (Ranged). The rebuild has no
  mod chips yet, so their part is missing.
- FUN_004a2010, a gizmo reward, raises skills with the message
  "N added to X skill.". Not wired yet (no gizmo uses it in the levels we
  play).

## The stats panel (`stats.itf`)

Opens with the inventory's CHAR button (inventory control 0x4B), and by
itself when the inventory opens while the level is unseen (FUN_004cb380).
Closing the inventory closes it. On a pad: Y in the inventory opens it,
D-pad up / down picks a skill, A spends a point, B or Y closes it.

| Part | Rebuild | Evidence |
|---|---|---|
| Placement | Top left at (0, 0), 349 × 306; the header's (−349, 306) is slide-in data the rebuild ignores | **proven** (Wine) |
| World view | Centred on y = (306 + 768) / 2 while the panel is open, 153 px lower | **proven** (measured) |
| Background | Encoding 7 with palette alpha: index 5 (magenta) has alpha 0. Holes inside the panel are black, the outside edge shows the world | **proven** (Wine) |
| Level, skill names and values, points | Messages 0x2f, 0x2b + row, 0x30 from FUN_004cb130 | **proven** (pixel match) |
| Skill value with a bonus | "*" + total | **likely** |
| NextLevel bar | Cropped to the progress fraction (FUN_004f18f0), its own palette | **proven** at 0; half full **likely** |
| Plus buttons | 0 disabled, 1 enabled, 2 pressed for 150 ms | disabled **proven**; the rest **guessed** |
| Bonus box | Hidden with no points, else the points count over its image (text box padX −10, signed) | hidden **proven**; shown **likely** |
| Tooltips after 500 ms | "Player name", "Current level", "Level up bonus", "Exp: X / Y", "<Skill> chance to hit +N%" with " (+bonus)", the Rigging and Override gadget / camera texts, "N full health hit points", "Increase <Skill> skill" | **proven** for Melee, Rigging, Life, Exp (Wine); the rest **likely** |
| CHAR button blink | While the level is unseen, `ms % 1000 / 250 == 3` (GCtrlInvThreeStateBtn_v8) | **likely**; which phase is lit is **guessed** |
| Messages | Centred at the top, under the panel when open, popup font, at most 5 lines | **guessed** |

The rebuild's panel matches the Wine capture pixel for pixel except 23
pixels of a faint trace in the bar's window (the capture is 1 px lower than
true).

## `.tre`

`res/common/quests/{human,gladiator,culibine}/gladiator.tre` is one 130-byte
file, the same in all three folders: an f64 version 1.0, a u32 count 10, then
small u16 words. It sits with the quests, not the skills, so it is not a
skill tree (the plan's guess was wrong). Its decode belongs to the quest
work (1b). **likely**.

## Not yet

- Mod chip skill bonuses, the gizmo skill reward, `GGiveExpEntityAction`
  (1a), `GStatModAction` (1a).
- Saving experience and skills with a game.
