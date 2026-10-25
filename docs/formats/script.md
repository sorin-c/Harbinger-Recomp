# Cutscene scripts (`.gsc`) and the speech index

A level's event triggers (`docs/formats/level.md`) can name a **script**: a
short list of commands that runs a cutscene (moves the camera, walks agents,
shows thought bubbles with voice lines, shakes the screen). Scripts are plain
text. Each character has its own script and speech managers, so the same
trigger plays that character's version of a scene.

Parser: `src/formats/gsc.cpp`. Tests: `tests/test_world.cpp`.

## Which script

`Harbinger.ini` has, for each character section (`[Human]`, `[Gladiator]`,
`[Culibine]`):

| Key | Example | Holds |
|---|---|---|
| `Scripts` | `res\common\managers\hscript.mgr` | trigger script id to `.gsc` path (88 entries each) |
| `Speech` | `res\common\managers\hspeech.mgr` | voice id (50000 and up) to `.mp3` path |

Both use the `.mgr` layout (`docs/formats/mgr.md`); the speech indexes use its
negative-count form.

## Text layout

```
Version 0.1                       first line; the shipped files say 0.0 or 0.1
Command(arg, arg, ...)            one command per line, CRLF line ends
```

`FUN_004fda40` reads the version line and then looks each command name up in
the names table at `0x557554` (22 names, 32 bytes each). The index is also the
slot in the command factory table at `0x5846d8`, and the command's text parse
is vtable slot 6 (`+0x18`), called with the script version.

Arguments are separated by commas:

- numbers (`FUN_004fdeb0`, `FUN_004fdfb0` read integers, `FUN_004fdf30` a
  double),
- quoted strings (`FUN_004fe030`); `\n` in the text is a line break,
- words: `Wait`/`NoWait` (`FUN_004fe060`), `On`/`Off` (`FUN_004fe080`),
  `Visible` (`FUN_004fe0a0`), `Range` (`FUN_004fe0c0`), and names compared in
  place (`Gizmo`, `Rndm`, `MeleeAttack`).

A trailing `Wait` makes the script wait for the command to finish; `NoWait`
starts it and goes on.

## Commands

The argument shapes are proven (each parse function, and all 205 shipped
scripts parse with them). The meanings are from the values in the shipped
scripts and are labelled.

| # | Command | Arguments | Parse | Meaning (label) | Uses |
|---|---|---|---|---|---|
| 0 | `Cinematic` | word, word | `0x501f90` | `On`/`Off`, `Visible`: enter and leave cutscene mode around every script (likely) | 336 |
| 1 | `PositionCamera` | n, n, n, wait | `0x500f70` | move the camera to a map cell at a speed in cells per second; a negative x means onto the player (proven, see below) | 284 |
| 2 | `MoveTo` | x, y, agent, wait | `0x4fed40` | walk an agent (by instance) to a map cell (likely) | 137 |
| 3 | `TurnToward` | x, y, agent, wait | `0x4ff390` | turn an agent to face a cell (likely) | 104 |
| 4 | `Highlight` | word, id, on/off | `0x502170` | `Gizmo, 784, On`: highlight a gizmo instance (likely) | 48 |
| 5 | `ActivateGizmo` | gizmo, wait | `0x500020` | use a gizmo instance, e.g. open a door (likely) | 126 |
| 6 | `Speech` | | | not used by the shipped scripts | 0 |
| 7 | `Attack` | n, n, n, `Range`, wait | `0x4feb90` | an agent attacks another (guessed) | 14 |
| 8 | `ActivateTrigger` | | | not used | 0 |
| 9 | `Lock` | | | not used | 0 |
| 10 | `PlayerSight` | | | not used | 0 |
| 11 | `Sound` | sound, wait | `0x501710` | play a `sound.mgr` sound (proven: ids such as 195, 241 and 282 resolve there) | 89 |
| 12 | `Wait` | ms | `0x501120` | pause (likely: values 250 to 1000) | 126 |
| 13 | `Shake` | n, n, ms, word, word, wait | `0x501310` | shake the screen: amplitude x, y, duration, `Rndm` (likely) | 128 |
| 14 | `SwapLevels` | | | not used | 0 |
| 15 | `Die` | | | not used | 0 |
| 16 | `ThoughtBubble` | text, x, y, n, n, voice (0.1), wait (optional) | `0x4ff980` | a thought bubble at a screen position with a voice line, see below | 720 |
| 17 | `Warp` | n | `0x4a49e0` | `Warp(0)` gives play back (game state 1); only in the portal scripts built in code (`docs/formats/gizmo.md`, "Portals") | 0 |
| 18 | `QuitGame` | none | `0x4d5080` | end the game (the endings; likely) | 3 |
| 19 | `SetLevel` | level | `0x501850` | go to a level (likely) | 3 |
| 20 | `Snapshot` | on/off | `0x5024f0` | freeze the screen (guessed) | 6 |
| 21 | `StrikePose` | agent, word, n, n | `0x4ffc60` | play an animation on an agent, e.g. `MeleeAttack` (likely) | 10 |

**ThoughtBubble** (`GScriptCmdThoughtBubble_v6`, `0x4ff980`): the text, two
integers at +0x14 and +0x16, two at +0x18 and +0x1a; from version 0.1 a voice
id at +0x1c and an optional Wait/NoWait. The first two stay inside 640 x 480
in every script (at most 610, 475), so they are a screen position (likely).
The next two are 0 in all 720 uses. The voice id names a line in the
character's speech index: every voice id in the scripts the levels' triggers
run resolves there, to an `.mp3` that exists (proven by test, 57 of 57).

## Running a script

Parser and runner: `src/world/script.cpp`; the commands themselves:
`src/app/cutscene.cpp`. Tests: `tests/test_world.cpp`.

### Triggers

Every frame `FUN_0051afa0` tests the player's move, from where it stood last
frame to where it stands now, against each event trigger's circle
(`FUN_0048b390`): the trigger is hit when the move starts inside the circle
or its line crosses the edge. A hit calls the trigger's slot 3
(`FUN_0048c2c0`):

1. each gizmo in the trigger's list is activated (GIActivatable slot 1, with
   no position; in the shipped levels these are breakers, spawners and
   bridges),
2. its script starts (`FUN_0048c5d0` -> `FUN_005025a0`), replacing any
   script that is running: one runs at a time, held at `DAT_00584730+0x40`,
3. the trigger is marked fired (state 2) and does not fire again.

### Stepping through commands

`FUN_00502560` runs the current script once a frame through `FUN_004fd8a0`,
which calls each command's handler through `FUN_004fde20`. The handler gets
the command's phase (0 on the first call) and returns:

| Result | Meaning | Runner |
|---|---|---|
| 0 | running | phase 1, call it again next frame |
| 1 | done | go on to the next command in the same frame |
| 2, 3 | skipped (the target is missing) | the same |
| 4 | restart | phase 0 again next frame |

So every command holds the script until it is done, and the trailing
`Wait`/`NoWait` flag (command `+8`) only matters where a handler reads it.
When the last command is done the script's owner is told (slot 1).

### Commands

The handlers are the `GGame_v0` switch at `0x4a4600` (game commands),
`GAgentPlug_v0` (agent commands) and `GFakeGizmoPlug_v0` (gizmo commands).

| Command | Handler | What it does (proven from the handler unless marked) |
|---|---|---|
| `Cinematic On` | `FUN_004a4aa0` | from play (game state 1 or 7) closes the panels and starts the bars (state 3/4); done once the game reaches state 5. The player takes no orders meanwhile (likely: the input code checks the state). |
| `Cinematic Off` | `FUN_004a4aa0` | state 5 to 6, the bars go; done once play resumes (state 1). |
| `PositionCamera` | `FUN_004a4c10` | from the camera's current point (`GGame+0x160/+0x168`, map cells) to the target in a straight line, taking distance / speed seconds; the script waits for it whatever the flag says. Speed 0 jumps. A negative x targets the player's position when the command starts (`+0x148/+0x150`). Afterwards the camera stays put (`+0x431`) unless it went to the player. |
| `ThoughtBubble` | `FUN_00422ba0` | shows the bubble (`FUN_0049f8e0`) and starts its voice line; runs while the line plays; then without `Wait` closes the bubble and is done, with `Wait` (or no voice) waits until the player closes it. A bubble shown over another replaces it. |
| `Sound` | `FUN_004a4e80` | plays a `sound.mgr` sound; with `Wait` the script waits for it to end. |
| `Wait` | case `0xc` | done after the given milliseconds. |
| `Shake` | case `0xd` | shakes the view for the given milliseconds and holds the script that long. Each frame (`FUN_004a2cf0`) each axis moves by its amplitude (`Disp`) or by `rand() % amp - amp / 2` (`Rndm`). |
| `Highlight` | `FUN_004a3270` | marks a gizmo (`Gizmo`) or agent to highlight, or clears it (`Off`). How the highlight is drawn is not read; the rebuild draws the gizmo a second time, added on (guessed). |
| `ActivateGizmo` | `FUN_004b9750` | GIActivatable slot 1 on the gizmo; with `Wait` the script waits until the gizmo is in state 3. |
| `MoveTo`, `TurnToward` | `GAgentPlug_v0` (`0x4225b0`) -> `FUN_004227c0`, `FUN_004228d0` | the target is the agent instance (`FUN_004a50e0`). The first call starts action 1 (Move) or 4 (Turn) towards the cell (`FUN_004e51b0`) and marks a script order (agent+0x1f1 set, +0x1f0 cleared); the next call is done at once without `Wait`, and with `Wait` once the action has ended (+0x1f0). A dead agent (`FUN_004e4d70`) is done at once. |

The trigger scripts use only these, plus `MoveTo` and `TurnToward` on agents
that spawners let out (levels 33 and 70). The rebuild opens spawners
(`docs/formats/gizmo.md`, "Spawners") but skips the agent commands on them
for now. Portals build scripts in code with `MoveTo` on the player
(instance 0) and `Warp`, which the rebuild plays. `ActivateGizmo` on a
gizmo that does not take the activation is done at
once.

### The thought bubble

`res\common\interface\thoughtbub\thoughtbub0.itf`: `Backgd.tga`, a
StaticTextBox at (1, 1) on `Screen.tga` (a green scanline texture) with fonts
7 / 7 / 11 and padding 10, 8, and a Close Button with no image (control 3:
its event `0x16` calls `FUN_0049f9f0`, which closes the bubble).

- Size (`FUN_0049f8e0`): when the script gives 0 x 0, the text is measured in
  font 7 at most 256 wide (`FUN_005146e0`), plus 22 x 18, the height at most 256.
- Place (`FUN_004c97d0`): the script's position is the top left for an
  800 x 600 view. At 1024 x 768 the bubble's centre moves out by 0.28 of
  itself (`x += 0.28 * (x + w / 2)`, the same for y), which is 1024 / 800.
- The window is resized to the bubble and the text box to 2 pixels less.
- The rebuild takes a click anywhere on the bubble as the Close Button (the
  button's size is not read; guessed), and closes it with Space, E or the
  pad's A too (a rebuild control).

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| Version line, one command per line, names table | proven | `FUN_004fda40`, table `0x557554`; all 205 `.gsc` files parse |
| Argument shapes per command | proven | the parse functions above; every shipped line matches, extra or missing arguments are rejected |
| Voice id from version 0.1 | proven | `0x4ff980` reads it only when the version is at least 0.1 (`0x54a308`); 0.0 files have five arguments |
| Trigger script ids resolve | proven by test | every event trigger's script id is in all three script managers and the file exists |
| Voice ids are in the speech index | proven by test | the speech index of the same character resolves every one |
| Command meanings | likely / guessed | names and values, per row, for the commands no trigger script uses |
| Trigger hit test, fire once, one script at a time | proven | `FUN_0051afa0`, `FUN_0048b390`, `FUN_0048c2c0`, `FUN_005025a0`; `entersCircle` and `TriggerWatch` tests |
| Command results and stepping | proven | `FUN_004fde20`, `FUN_004fd8a0`; `ScriptRunner` test |
| Run side of the commands the trigger scripts use | proven | the handlers in the table above |
| Level 11's portal scene | proven by test and run | walking from the portal entry towards (62, 57) sets off trigger 4 (script 11); a headless run plays the bars, the camera pan, the seven bubbles timed to their voice lines and the camera back. Not compared with the original yet (it has no level select). |

## Open questions

- How a highlighted gizmo or agent is drawn, and what `Cinematic`'s
  `Visible` changes (stored at `GGame+0x432`).
- The size of the bubble's Close Button.
- Spawners, and so the agent commands of the trigger scripts.
- The commands no shipped script uses.
- How the speech index id maps to subtitles, if at all.
