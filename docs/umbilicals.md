# Umbilicals: pads, codes and level changes

The umbilicals are the game's main way to travel between levels. The player
steps onto a pad, a shell closes round them, and a list of the places their
codes reach opens; picking one sends them there, where they arrive inside
the destination pad's closed shell. This note also covers the level changes
that are neither portals nor umbilicals: the scripts' `SetLevel` and the
dialogue action Set Level.

Code: `src/world/umbilical.*` (codes, pad records, a trip on a level),
`src/app/level_select.*` (the destination list), `src/app/world_view.*`
(wiring: triggers, the list, arrival), `src/app/cutscene.*` (`SetLevel`).
Tests: `tests/test_world.cpp` (`testUmbilicals`, and real-data checks of
every pad). Portals, the other way between levels, are in
`docs/formats/gizmo.md`, "Portals".

Labels as everywhere: **proven** (read in the code or checked against the
files), **likely**, **guessed**.

## The pad and its shell

A pad is a gizmo of class 7 (`GUmbilical` / `GUmbilicalPlug`); its shell,
the pod that closes over it, is class 8 (`GUmbilicalShell` /
`GUmbilicalShellPlug`), attached to the pad through the level's attachments
list. The pad's record (`GUmbilicalPlug_v22`) is five `u16`:

```
u16  state        plug+0x7c; 0 in every record
u16  level        plug+0xf4: a fixed destination's level.mgr id, 0 in most
u16  entry        plug+0xf6: and its entry point
u16  own entry    plug+0xf8: the entry point of this level on the pad
u16  power needed plug+0xde (GIPowerable)
```

The level files hold 110 pads. Each has exactly one shell attached, and each
stands on an entry point of its own whose two gizmo triggers (ids
`2·own − 2` and `2·own − 1`) sit exactly on the entry point. Seven pads have
a fixed destination (36:1 three times, 38:2, 20:2, 25:1, 6:1); every one of
those exists. One pad (level 46, gizmo 344) needs one unit of power, fed by
gizmo 354.

**Frames.** The pad's slot 17 (`0x51dd90`) takes the template's Begin
Activate frame `act` (tag 7, gizmo+0xbc). At rest it loops frames 1 to
`act − 1` (frame 0 is the base the others draw over); charging (state 2) it
plays `act` to the last frame but one, and slot 29 (`0x51dd30`) holds there.
A pad without the power it needs that is not charging shows its last frame,
the dark one (`GUmbilicalPlug_v28`).

The shell at rest (state 0) shows frame 0, which is only an editor outline
of its footprint, so the game draws nothing. Closing (state 2) it plays from
its Begin Activate frame (gizmo+0xb8) to the last frame but one and holds.

## A trip

1. **Stepping on.** The pad's gizmo trigger calls `GUmbilicalPlug_v1` with
   x = −1. The pad starts only if the player holds some code
   (`FUN_004ef3f0`: any entry of the code map with flag 1) or the pad has a
   fixed destination, and it is idle (state 0) with the power it needs. It
   goes to state 2 and `FUN_004bdaf0` calls the attached shell
   (`GUmbilicalShellPlug_v1`), which goes from 0 to 2. Because x < 0 the pad
   sets +0xfa (started by a trigger).
2. **Charged.** When the pad's charge runs out (`GUmbilicalPlug_v29`) and
   +0xfa is set, `FUN_004a1a20` gives the player the fixed destination's
   code as a gold one (`FUN_004ef160(level, entry, 1)`).
3. **Closed.** The shell's slot 29 runs for the first time (+0xd4 is 0, no
   agent delivery) and calls `FUN_004a1bb0`. If the player is under control
   (player+0x49), the game pauses (`*DAT_005847dc = 1`), the player is
   disabled, `FUN_004eeeb0` marks the code of the pad here as held and known
   and builds the list, and the Level Selection screen opens (ui+0x5c,
   message 0xb).
4. **Picking.** Releasing the mouse over a line of the list's text box
   (`GCtrlStaticTextBox_v7`, case 3) sends message 0x17 with the line index
   (`FUN_004a1a80`, mapped back through `FUN_004ef030`) and then message 5,
   which starts the trip at once (`FUN_004a1d20`): the level loads with the
   chosen entry. Cancel closes the list.
5. **Arriving or cancelling.** `FUN_004a1d20` does not give back control,
   so on arrival the player stands on the destination pad's triggers with
   control off: the pad starts again, its shell closes, and at step 3,
   with the player not under control, `FUN_004a1bb0` only re-enables the
   player. The shell's second call to slot 29 sets it back to 0 and tells
   the pad (GIAttacher slot 0), which goes back to 0. After a Cancel the
   same second call opens the shell. Arriving at entry 0 instead re-enables
   the player straight away (`FUN_00498410`).

`GUmbilicalShellPlug_v1` with x > 0 stores x at +0xd4 and delivers an agent
there instead (`FUN_0041a3f0`); nothing in the shipped data uses it.

## Codes

The player's codes are a map at `GPlayerPlug+0x220`, keyed by
`level * 16 + entry`, each with flags and the entry point's name:

| Flag | Meaning |
| --- | --- |
| 1 Held | the player has the code |
| 2 Open | the umbilical there can be reached |
| 4 Plain | given without the gold hint, or been there; a held code without it is **gold** |

- **Adding a place** (`FUN_004ee9d0`, "AddUmbilical"): opens the level's
  file for its entry points, and names the code after the entry point. Open
  is set unless the entry point is marked closed (its facing word is above
  15, see `docs/formats/level.md`). No shipped entry point an umbilical
  leads to is marked closed. "AddUmbilical() Load failed!!!" if the level
  does not load.
- **Unlocking** (`FUN_004ef160`): a known place gets Held, and with the
  gold hint loses Plain; a new one is added with Held, plus Plain without
  the hint. Returns whether the code is new.
- **Power** (`FUN_004ef270` / `FUN_004ef330`): set or clear Open when the
  pad there gains or loses its power (`GPortalPlug_v0`, which the pad shares
  through its GIPowerable).
- **The list** (`FUN_004eeeb0`, `FUN_004ef030`): the held and open codes
  other than the pad the player stands on, gold first, then the rest, each
  group in key order. Gold lines start with `*`, which the text box draws
  with its third font; lines end with `\n`.
- **Clear Umbilicals** empties the map.

**Dialogue actions** (the names table at `0x552f50`): 2 Unlock Umbilical, 3
Clear Umbilicals, 22 Set Level. Unlock Umbilical's load reads `u16` +4, the
level `u16` +8, then from later versions the gold flag +0xc and the entry
+0xe, and an announce byte +0xd. In the `.dlg` files it reads
`02 00 03 00 LL 00 GG 00 EE 00 MM 00`. A new code with the announce flag
shows "Player received umbilical code." for 3000 ms through `FUN_004a5130`
(a message line; not rebuilt yet). The dialogue rebuild calls
`WorldView::unlockCode`, `WorldView::codes().clear()` and
`WorldView::setLevel`.

## The Level Selection screen

`res\common\interface\level\level.itf`, panel kind 0x0b (centred; at
1024x768 the background moves by (+0x70, +0x5e), `FUN_004c4af0`). Its
controls are offsets from the background, as in every panel: the
background `Screen.tga` (636x460), a Cancel button (off, up and down
images), the Up and Down scroll buttons borrowed from the save screen, and
the text box `Text`, 450x240, fonts 12, 13 and 14 (plain, highlighted, gold),
centred. `Text.tga` next to them is not referenced.

## Other level changes

- **`SetLevel(n)`** (script command 19, case 0x13 at `0x501850`) and the
  dialogue action Set Level store n at GQuestMgr+0x46. On the next frame
  `FUN_004f6780` sees it and `FUN_004a1b30` loads level n with entry
  0xffff. The shipped scripts use it three times, as `SetLevel(13)` in
  `hom.gsc`, `gom.gsc` and `com.gsc`.
- **`SwapLevels(a, b)`** (command 14, case 0xe) swaps the paths of two
  `level.mgr` entries; no shipped script uses it.

## Entry facing

An entry point's facing word is a direction 0..15, which `FUN_0044bfb0`
turns into a facing byte through the table at `0x54fa58`:
`0, 18, 27, 37, 48, 64, 83, 109, 128, 144, 155, 165, 176, 192, 211, 237`
(16 and above give 0). `FUN_0048b2c0` clears bit 4 of a word above 15 and
keeps the mark at entry+0x2c (the "closed" mark above). The rebuild used to
take the word itself as the facing byte.

## Rebuild choices

- The player stands still from stepping onto a pad until the shell has
  opened again; the game's clock stops while the list is open.
- `SetLevel` arrives at entry 0, Default; what the original does with entry
  0xffff is not traced.
- Picking the pad's own level reloads the level.
- The shell's closed state is reported only after the pad's charge has
  played out (both end at about the same time in the shipped templates).
- The list's scroll arrows show disabled when there is nothing to scroll.
- Keys and pad in the list: Up and Down (or W and S, the D-pad, the stick)
  move the highlight, Enter, Space, E or A pick, Escape or B cancel.
- The "Player received umbilical code." message line is not rebuilt.

`harbinger --level 1 --entry 1 --codes 2:1,3:2,24:1 --screenshot out.bmp
--time 4000` stands on Torvus Junction's pad with three codes and shows the
open list; `--travel 0` picks its first line.

## Evidence

| Finding | Label | Where |
| --- | --- | --- |
| Pad record words | proven | `GUmbilicalPlug_v22`; every pad's own entry exists, not closed, and its fixed destination resolves (test) |
| One shell per pad, triggers on the entry point | proven by survey | all 110 pads (test, scan of the levels) |
| Pad and shell frames | proven | `0x51dd90`, `0x51dd30`, `GUmbilicalPlug_v28`, `GUmbilicalShellPlug_v17`, `_v29`, `GUmbilical_v4`, `GUmbilicalShell_v4` |
| Starting a trip | proven | `GUmbilicalPlug_v1`, `FUN_004ef3f0`, `FUN_004bdaf0`, `GUmbilicalShellPlug_v1` |
| Fixed destination given as a gold code | proven | `GUmbilicalPlug_v29`, `FUN_004a1a20`, `FUN_004ef160` |
| Pause, list, travel, arrival | proven | `FUN_004a1bb0`, `FUN_004eeeb0`, `FUN_004a1a80`, `FUN_004ef030`, `GCtrlStaticTextBox_v7`, `FUN_004a1d20`, `FUN_00498410`; the rebuild's trip from Torvus Junction to the Junkyard (run) |
| Code map and flags | proven | `FUN_004ee9d0`, `FUN_004ef160`, `FUN_004ef270`, `FUN_004ef330`, `FUN_004eeeb0` |
| Gold lines drawn with the star font | proven | `FUN_004eeeb0` writes `*`; the text box's third font (`docs/formats/itf.md`) |
| Unlock Umbilical load and `.dlg` bytes | proven | `GUnlockUmbEntityAction_v8`; 292 hits in the `.dlg` files |
| `SetLevel`, `SwapLevels` | proven | script cases 0x13 and 0xe, `FUN_004f6780`, `FUN_004a1b30` |
| Level Selection layout | proven | `level.itf`, `FUN_004c4af0`, `FUN_004c4a70` |
| Entry facing table and closed mark | proven | `FUN_0044bfb0`, `0x54fa58`, `FUN_0048b2c0` |

## Open questions

- Which shipped content powers or cuts the level 46 pad, and how the list
  looks with a code whose pad has no power.
- Agent delivery by shell (`FUN_0041a3f0`).
- Compare the trip frame by frame with the original under Wine.
