# NPCs, dialogue and the story in the game

How talking to NPCs works, from the click on an NPC to the conversation
screen and the actions it fires. The file formats are in
[formats/dlg.md](formats/dlg.md) (dialogues and entity actions) and
[formats/qst.md](formats/qst.md) (quests).

Code: `src/world/story.*` (the active quest, which dialog an NPC opens, the
conversation stack, events; no drawing), `src/app/dialogue.*` (the screen),
`src/app/world_view.cpp` (NPC picking, events from the world, what the
actions do), `src/app/harbinger.cpp` (input while talking). Tests:
`tests/test_story.cpp`, `tests/test_inventory.cpp`.

Set `HARBINGER_STORY_LOG=1` to see the quest states, inputs, dialogs and
actions on stderr.

## Talking to an NPC

| Finding | Label | Evidence |
|---|---|---|
| NPCs are the agent classes 20-28 (`GHumanNPC`, `GGladiatorNPC`, `GCulibineNPC`, Ona, Tinker, `GSpyNPC`...) | proven | `agt::Template::isNpc`, class table |
| Talking (`GCulibineNPCPlug_v76`) asks the quest manager for the NPC's dialog (by level and instance) and opens it; it is refused while game+0x584fb is set | proven | `GCulibineNPCPlug_v76`, `FUN_004f7220` |
| Then the NPC fires event 2 (talked) (`GCulibineNPCPlug_v77`) | proven | |
| With no dialog the NPC says a line of its own (`FUN_004f2090`, template+0x228) | proven, not rebuilt | |
| The player walks up to the NPC first: click on its picture, or the use button picks the nearest within 3 cells (after items) | rebuild choice | |
| The player stops within 2 cells, on the walkable half cell nearest the NPC on its side | rebuild choice | |

## The conversation

| Finding | Label | Evidence |
|---|---|---|
| Open dialogs are a stack (quest manager +0x5c); a new one goes on top, each at its current node | proven | `FUN_004f7ca0` |
| A dialog opens at its first node and runs its own actions | proven | `FUN_0047ec70` |
| Picking a response: leave the node, run the response's actions, go to the node its target names in the same dialog; `END_DIALOG` or an unknown name ends that dialog | proven | `FUN_0047ee70`, `FUN_004f8000` |
| Pop Dialog Group drops the top dialog, never the last (+0x30); a stack whose top has no node is the end of the conversation | proven | `FUN_004f7f40` |
| A quest that ends during the pick (+0x31) hands the NPC straight on to its dialog in the next quest | proven | `FUN_004f8000` |
| Set Dialog opens a dialog with no NPC | proven | `FUN_004f7ca0` called with null |
| The responses offered are the first four whose condition holds; "has item" looks in the inventory grid only (not the equipment) | proven | `FUN_0047ea50`, `FUN_004cf9e0` |
| The world stands still while a conversation is open | rebuild choice (the original's game state while talking is not read) | |
| The conversation can be closed without a response (Escape, the pad's B) | rebuild choice (the .itf has a "Close Button" with no picture) | |

## The screen

`res\common\interface\dialog\npcdialog.itf`, interface kind 6.

| Control | Class | Position (from the background's corner) | Notes | Label |
|---|---|---|---|---|
| Background | Backgd | `Screen.tga` at (82, 50), 636 x 460 | at 1024x768 the interface moves by (+0x70, +0x5e); the rebuild centres x and scales that y move for its other sizes | proven at 800x600 and 1024x768 |
| Close Button | Button | (0, 0), no picture | | proven |
| Up / Down | ScrollUp / ScrollDown | (573, 71) / (573, 214) | images off, light, dark: shown disabled, normal, pressed | proven layout, likely images |
| Text | StaticTextBox | (178, 40), 373 x 306, fonts 12 / 13 / 1 | the NPC's words (message 0x0b, mode 2) | proven |
| Portrait | PictureSlot | (40, 40), 120 x 146 | the top dialog's portrait item, its first frame | proven |
| Response 1-4 | TextBtn | (40, 363), `ButtonUp.tga` / `ButtonDn.tga` 553 x 20, fonts 14 normal, 13 pressed | | proven |

| Finding | Label | Evidence |
|---|---|---|
| Laying out a node: message 0x26 with "" empties the buttons; then one 0x26 per response, which every filled button answers by moving by its size times (0, -1) and the first empty one by taking the text; so the last response sits at (40, 363) and the others stack above it | proven | `FUN_004c9650`, `GCtrlTextBtn_v7` |
| Then 0x15 with -n shortens the text box by n lines of its font | proven | `FUN_004c9650`, `GCtrlStaticTextBox_v7` case 0x15 |
| A click on a response sends 0x18 with its index, which picks it | proven | `GCtrlTextBtn_v7`, `FUN_0049f6d0`, `FUN_0049f790` |
| The text scrolls a line per arrow press | likely | |

Controls added by the rebuild: up / down (or W / S, Tab, the D-pad, the
stick) move a focus along the responses and past the ends scroll the words;
Enter, Space, E or A picks the focused response (a lone response needs no
aiming); 1-4 pick directly; Page Up / Down, the wheel and the shoulder
buttons scroll; Escape or B close.

## Messages

Actions report on a message list (game+0xb4, `FUN_004e4520`): newest first,
each for 3000 ms, the oldest dropped past the list's length. Texts:
"Player received <item>", "Player lost <item>", "Player lost N" + " Uan"
(likely), "Quest Log updated.", "Player received umbilical code.", and
Display Text's own. Where the original draws them and in which font is not
read: the rebuild centres them along the top of the view in font 7, at most
four (guessed).

## Events from the world

| Event | Where the rebuild fires it |
|---|---|
| Trigger fired (6, trigger id) | each event trigger that fires (`Cutscene::setTriggerFired`) |
| Agent died (0, instance) | an enemy's death |
| Talked (2, NPC instance) | `Story::talk` |
| Picked (9, item id) | a pick-up off the ground, and Give Item / Money / Health |

In level 1 the Human starts inside trigger 1, so Begin Game moves to its
state 1 at once, which starts the opening script (script 1, the thought
bubbles) and makes Torvus (instance 3) open dialog 116, then 117.

## Not rebuilt yet

Experience and stat actions (plan 1c), trade and stash accounts (1d), the
quest log screen (1b), group monitors, the NPC's own line when it has
nothing to say, and how the original refuses a gift when the inventory is
full (the rebuild drops it at the player's feet).
