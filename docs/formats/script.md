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
| 1 | `PositionCamera` | n, n, n, wait | `0x500f70` | move the camera; often `-1.0, 0.0, speed` (guessed: -1 = onto the player) | 284 |
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
| 17 | `Warp` | | | not used | 0 |
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

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| Version line, one command per line, names table | proven | `FUN_004fda40`, table `0x557554`; all 205 `.gsc` files parse |
| Argument shapes per command | proven | the parse functions above; every shipped line matches, extra or missing arguments are rejected |
| Voice id from version 0.1 | proven | `0x4ff980` reads it only when the version is at least 0.1 (`0x54a308`); 0.0 files have five arguments |
| Trigger script ids resolve | proven by test | every event trigger's script id is in all three script managers and the file exists |
| Voice ids are in the speech index | proven by test | the speech index of the same character resolves every one |
| Command meanings | likely / guessed | names and values, per row; the run code (the other vtable slots) is not read yet |

## Open questions

- The run side of each command (vtable slots other than 6): what the camera
  numbers mean, how `Wait` interacts with the trigger.
- The commands no shipped script uses.
- How the speech index id maps to subtitles, if at all.
