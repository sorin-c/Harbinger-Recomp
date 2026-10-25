# NPC dialogues (`.dlg`) and entity actions

A `.dlg` file is one conversation: a list of nodes (what the NPC says) with
the player's responses, each response leading to another node or ending the
talk, and the **entity actions** a response fires (give an item, set a
quest, unlock an umbilical code...). Quests (`.qst`, see
[qst.md](qst.md)) use the same action lists.

Parser: `src/formats/dlg.cpp`. Python prototype:
`research/prototypes/dlg_parse.py`. Tests: `tests/test_story.cpp`.

Which dialog an NPC opens is not in the NPC: the active quest names it (see
[qst.md](qst.md), "Entity dialogs"). The files are found through the
character's `Dialog` index in `Harbinger.ini` (`[Human] Dialog =
res\common\managers\hdialog.mgr`, and so on).

## Layout

Little-endian. Strings are a u32 length (without the terminator), the
characters, then a NUL. The engine's class names: `GDialogGroup` (the file),
`GDialog` (a node), `GDialogResponse`.

| Field | Type | Notes | Label | Evidence |
|---|---|---|---|---|
| version | f64 | 1.25 or 1.75 in the shipped files | proven | every file parses (test) |
| node count | u32 | | proven | `FUN_0047f5c0` |
| per node: text | str | what the NPC says | proven | `FUN_0047f5c0`; shown on screen |
| per node: name | str | responses name their target by it | proven | `FUN_0047ee70` looks targets up by name |
| per node: response count | u32 | | proven | `FUN_0047f3a0` |
| per response: text | str | what the player says; a leading `*` greys it (`GCtrlTextBtn_v7`, state 3); no shipped response has one | proven | `GCtrlTextBtn_v7` message 0x26 |
| per response: target | str | a node name, or `END_DIALOG` | proven | `FUN_0047ee70` |
| per response: actions | action list | see below | proven | `FUN_00489320` |
| per response, version > 1.0: condition | u16 | 0 none, 1 has item, 2 lacks item, 3 money >= param, 4 money < param | proven | `FUN_0047ea50` |
| per response, version > 1.0: param | u16 | the item id or the amount | proven | `FUN_0047ea50` |
| version > 1.0: portrait | u16 | an item.mgr id (19 `Torvus.itm`, 16 `Ona.itm`, 742 `Leisha.itm`, 852 `Om.itm`...), 0 for none | proven | `FUN_0047ed70` reads it to +0x14; `FUN_0049f710` makes the item and hands it to the picture slot |
| version > 1.0: actions | action list | the dialog's own; empty in every shipped file | proven | `FUN_0047ed70`; test |

All 1,390 shipped `.dlg` files (1,071 unique by content) parse to the last
byte. The responses offered are the first four whose condition holds
(`FUN_0047ea50`, the screen has four buttons).

## Entity action lists

`FUN_00489320`: a u32 count, then per action a u16 type (the factory at
`DAT_005840c8`, `FUN_0048a400`, keyed by each class's slot 2), a u16
character (3 = any; otherwise the action runs only for that character,
`GActivateGizmoEntityAction_v9` compares it with `DAT_0058437c+0x5848c`; every
shipped action says 3), then the type's own fields (slot 5).

| Type | Class | Fields | What it does (slot 8) | Label |
|---|---|---|---|---|
| 0 | `GGiveItemEntityAction` | u16 item | "Player received <name>", then the player picks it up (`FUN_00489090`) | proven |
| 1 | `GTakeItemEntityAction` | u16 item | if it is in the grid (`FUN_004ee600` -> `FUN_004cf9e0`): "Player lost <name>", removed | proven |
| 2 | `GUnlockUmbEntityAction` | u16 level; version > 1.0: u16 gold, u16 entry; version > 1.5: u16 announce | an umbilical code (see `docs/umbilicals.md`) | proven |
| 3 | `GClearUmbsEntityAction` | none | forgets the codes | proven |
| 4 | `GGiveExpEntityAction` | u32 amount | experience (plan 1c) | proven read, not rebuilt |
| 5 | `GLaunchScriptEntityAction` | u16 script | starts a script (`FUN_004a45c0`) | proven |
| 6 | `GUnlockGizmoEntityAction` | u16 | nothing (its slot 8 is empty) | proven |
| 7 | `GActivateGizmoEntityAction` | u16 gizmo instance | uses that gizmo (`FUN_004a28e0`) | proven |
| 8 | `GGiveMoneyEntityAction` | u16 amount | a money item of that amount, picked up (`FUN_00489090`) | proven |
| 9 | `GTakeMoneyEntityAction` | u16 amount | if there is that much (`FUN_004ee620`): "Player lost N" + `DAT_0057d34c` | proven |
| 10 | `GGiveHealthEntityAction` | u16 count | that many of the character's health item, one message | proven |
| 11 | `GSetQuestEntityAction` | u16 quest | the active quest (`FUN_004f6c50`) | proven |
| 12 | `GEndQuestEntityAction` | u16 result | ends the active quest (`FUN_004f8160`) | proven |
| 13 | `GQuestStringEntityAction` | str | a quest log line; "Quest Log updated." | proven |
| 14 | `GSetDialogEntityAction` | u16 dialog | opens a dialog on top (`FUN_004f7ca0`, no NPC) | proven |
| 15 | `GLaunchTradeAction` | none | the trade screen (plan 1d) | proven read, not rebuilt |
| 16 | `GStatModAction` | u16 stat, u16 value | a stat change, "+N to <stat>" (plan 1c) | proven read, not rebuilt |
| 17 | `GAddItemToAccountEntityAction` | u16 item (+8), u16 account (+0xc) | puts an item in a stash account (plan 1d) | proven read, not rebuilt |
| 18 | `GTakeItemFromAccountEntityAction` | u16 item, u16 account | | proven read, not rebuilt |
| 19, 20 | `GPrevQuestDialog`, `GNextQuestDialog` | none | not used by the shipped files | proven read |
| 21 | `GPopDialogGroupEntityAction` | none | drops the top dialog of the stack (flag +0x30) | proven |
| 22 | `GSetLevelEntityAction` | u16 level | loads that level | proven |
| 23 | `GDisplayTextEntityAction` | str | a message line (`FUN_004a5130`, 3000 ms) | proven |
| 24 | `GClearAccountEntityAction` | u16 account | empties a stash account (plan 1d) | proven read, not rebuilt |

Version thresholds: `DAT_005492c0` = 1.0, `DAT_0054a048` = 1.5 (proven, the
constants).

## Known holes in the shipped data

| What | Label |
|---|---|
| `(L1)Vantir Commander.dlg` (dialog 44) is named by quests but missing from all three dialog indexes | proven (test) |
| The Culibine's `(T2)Vantir Commander.dlg` (dialog 47) is missing | proven (test) |
| `Act 2 Begin.qst` names dialogs 48-50, which no index has | proven (test) |
