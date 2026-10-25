# Step 2: rebuilding the engine

## Layout

| Path | Role | Depends on |
|---|---|---|
| `src/formats/` | File formats (`.sqh`, `.gim`, `.mgr`, `.itf`, `.fnt`, `.acr`, `.itm`, `.chk`, `.brs`, `.giz`, `.agt`, `.anm`) | C++17 only |
| `src/engine/` | Engine core: `Vfs` (archive mounting, path rules), `Ini`, `Settings` (config.cfg) | formats |
| `src/audio/` | WAV and MP3 decoding (dr_mp3, downloaded and hash-checked by CMake) | C++17 only |
| `src/render/` | Platform-neutral rendering helpers (`.gim` frame to RGBA) | formats |
| `src/world/` | The game world: `Level` (a level's cells, brushes, gizmos, agents and projection) | formats, engine, render |
| `src/ui/` | Interface pieces with the engine's rules: text boxes and wrapping, buttons, check boxes, sliders, animated items | formats, render |
| `src/app/` | SDL3 programs | everything + SDL3 |

Core code stays free of SDL so it can be tested headless and reused on other
platforms; SDL3 is used only at the edges (window, GPU textures, input, audio).

SDL3 comes from the system if CMake can find it (`-DCMAKE_PREFIX_PATH=...`),
otherwise CMake downloads and builds SDL 3.2.24 (`HARBINGER_FETCH_SDL`, on by default).

## Milestone 1: sprites on screen (done)

`harbinger-viewer` mounts the archives through `Vfs`, decodes sprites and
draws them with SDL3, animating sprite sheets at 10 fps. Arrow keys or the
D-pad step through files and K / Y cycles how the marker colours are shown.
`--screenshot out.bmp` renders one frame headless with SDL's offscreen
driver, which is how the cloud builds check it.

```
harbinger-viewer --data "C:/Games/Harbinger" --prefix res/common/agents/
```

What it showed:

- Character sheets carry dotted outlines of the held weapon, and Gladiator
  frames show separate floating parts. Equipment is probably drawn as separate
  layers on the body (guessed; the readme's "torso and weapons" equipment fits).
  This gets traced together with `.anm`.
- The pure green/red marker colours are handled by `render::KeyMode` until the
  real renderer is traced.

## Milestone 2: the front end (done)

`harbinger` boots to the main menu and the character screen, built entirely
from the original's interface layouts (`startup.itf`, `new.itf`), fonts, item
templates and cursor. Everything is drawn at the original's 800x600 and scaled
to the window.

```
harbinger --data "C:/Games/Harbinger"
harbinger --data <dir> --screenshot menu.bmp [--screen new] [--time ms] [--mouse x,y]
```

- Hovering a button plays its animation exactly as the original's state
  machine does (one frame per 100 ms), and clicking plays the press section.
- New Game opens the character screen; Back / Esc / B returns; Exit quits.
  Picking Human, Gladiator or Culibine holds that portrait in its pressed pose.
  Load, Options and Play only log a message for now.
- Keyboard (arrows, Enter, Esc) and gamepad (D-pad or left stick, A, B) move a
  focus through the buttons, which shows as the hover animation.

Compared with a capture of the original (pixels, not by eye): the background
matches, the labels match in position and colour except a 1-pixel dark fringe
(see fnt.md), and the buttons match 90-99.5%. The button residue is not
explained yet: two of the four line up best one pixel right and up of where the
`.itf` places them, the other two fit no simple offset or filtering.

## Milestone 3: music and sounds (done)

The front end plays `Intro.mp3` on a loop and the buttons play their
interface's hover and press sounds (docs/formats/audio.md). `--no-audio` turns
sound off; screenshot mode never opens an audio device.

## Milestone 4: Options and Load Game (done)

Options and Load Game open from the main menu and are rebuilt from
`options.itf` and `load.itf` with the engine's TextBtn, CheckBox, SliderBar,
ScrollButton and word-wrapping text box (docs/formats/itf.md). Compared with
captures of the original, both screens and the character screen are identical
pixel for pixel, apart from the original's saved game in the Load lists.

```
harbinger --data <dir> --screen options|load [--settings file.cfg]
```

- The music and sound sliders change the volume live (music at `value / 12`,
  as the original hands it to Miles; the sound slider plays a sound check),
  Accept writes `config.cfg` in the original's layout (docs/formats/config.md)
  to the user's preference folder, Cancel restores the old values.
- Swap Mouse Buttons works. Gamma and High Resolution are stored but not
  applied yet; Configure Controls only logs.
- Load Game lists nothing: the original's saves are not supported and the
  rebuild has no saves of its own yet.
- The character screen now matches the original: Play is disabled until a
  character is picked and the name box reads "Player".
- Keyboard and gamepad: up/down move a focus outline through the controls,
  left/right step a focused slider by 5%.
- Display (a rebuild addition, in the free row above Options' buttons, in the
  labels' font): 4:3 window sizes that fit the screen, or fullscreen
  (borderless, at the desktop resolution). Applied on Accept and kept in
  `display.ini` next to `config.cfg`; Alt+Enter toggles fullscreen anywhere.
  The 800x600 screens are scaled to fit with black bars.

## Milestone 5: the first level (done)

Play on the character screen opens level 1, Torvus Junction, the level the
original starts a new game in. The floor and walls are drawn from the level's
map cells and the act's brushes (`docs/formats/level.md`) at the original's
in-game size, 1024x768, with the level's own music. The camera starts on the
level's Default entry point and moves with the arrows, WASD, the screen edges
or the left stick; Esc or B returns to the menu. `--screen game --level N`
opens any level directly, and `levelshot` renders a whole level to an image.

Not there yet: objects (gizmos), characters, lighting, the HUD.

## Milestone 6: level objects (done)

The level's gizmos (machinery, trash, consoles, the umbilical pit, the town
generator) are decoded from their `.giz` templates and the level's set-gizmo
records (`docs/formats/gizmo.md`) and drawn with the floor and walls, in place
as the original shows them. Every template and every level's records parse to
the byte. Each gizmo shows its first frame; animation, states and lights come
later.

## Milestone 7: characters on the map (done)

The level's agents (the player's start, enemies, NPCs, mines, cameras) are
decoded from their `.agt` templates, `.anm` animations and the level's agent
records (`docs/formats/agent.md`) and drawn in place, sorted with the objects
and walls. Every template, animation and level parses to the byte. Each agent
shows one still frame of its first animation; facing, animation and behaviour
come next.

## Milestone 8: things move (done)

Characters play their idle animations at the original's 10 frames a second,
and looped gizmos (194 of them, the Torvus Junction generator among them) run, with the frame
timing decoded from the original (`docs/formats/agent.md`,
`docs/formats/gizmo.md`). `levelshot` takes a time to render a level at.
Agents still stand still: states, movement and AI come with the game loop.

The rest of the level file (triggers, attachments, power links, beacons and
rooms, `docs/formats/level.md`) is decoded too: every shipped level now parses
to its last byte. Nothing uses those sections yet.

## Milestone 9: cutscene scripts read (done)

The scripts that the levels' triggers run (`.gsc`, `docs/formats/script.md`)
parse: all 205, with each command's arguments checked against the original's
parse function. Every script a trigger names exists for all three characters,
and every thought bubble's voice line resolves in that character's speech
index. Nothing runs them yet.

## Milestone 10: gizmos start in their states (done)

Placed gizmos now start in the state their level gives them, decoded from
each class's state code in the original (`docs/formats/gizmo.md`, "States"):
doors and bridges shut or open, chests and consoles idling, consoles without
power dark, breakers on or off, and lights, monitors and incubators pausing
and playing short sequences at random. Power flows from the level's breakers
to what they feed, as in the original. Frames draw over the gizmo's picture
the way the original builds it. Using a gizmo (opening a door) comes with the
game loop.

## Milestone 11: walking (done)

New Game puts the chosen character (Human, Gladiator or Culibine) on the
level's Default entry point, facing its way, drawn from its body, armour and
weapon layers (`*animgroup.mgr`). It walks at the original's speed (3.18 cells
a second for the Human, measured under Wine) to where the mouse clicks, around
walls and drops on the original's half-cell walk grid and path finder, or
along a gamepad stick or the keys, sliding along walls. The camera follows it.
Evidence and the parts still guessed: `docs/movement.md`.

Each character starts in the equipment the original gives it (the start-item
lists in each player plug), drawn from those items' animation groups.

## Milestone 12: hitting enemies (done)

Clicking an enemy (or F, the pad's X / Square or the right trigger) walks the
player up to it and swings the melee animation, again and again, as the
original's tags loop it. Each blow lands where the weapon's tag puts it and
rolls to hit and for damage with the original's generator, damage table,
defences and resistances; enemies get the original's hit-point rolls and,
when those run out, play their death animation and stay as corpses.
Evidence: `docs/combat.md`. Enemies do not fight back yet.

## Milestone 13: using gizmos (done)

The player uses consoles and chests: a click on one (or E, Space or the
pad's A button for the nearest) walks there and uses it. A console with
power plays its activation and opens or shuts the doors attached to it,
which play their frames and settle; a door does not shut on someone standing
in it. The walk grid follows the door, so the player can walk through once
it is open. Chests open (no loot yet). Evidence: `docs/formats/gizmo.md`,
"Using gizmos".

## Milestone 14: gizmos in 3D order (done)

Every gizmo template carries a 3D shape that the original draws with a depth
buffer. The rebuild reads it, draws only what the shape covers (so editor
outlines and markers no longer show) and orders gizmos and characters by
comparing depths where their pictures overlap, so a character walking past
a door or a machine goes behind or in front of the right parts. Evidence:
`docs/formats/gizmo.md`, "Shape".

## Milestone 15: trigger scripts play (done)

Walking into an event trigger sets it off once: its gizmos are activated
and its cutscene script runs. The rebuild plays the commands the shipped
trigger scripts use: cinematic bars (the player takes no orders), camera
moves, thought bubbles with their voice lines (closed with a click, Space,
E or the pad's A), screen shakes, sounds, waits, gizmo activation and
highlights. Agent commands (MoveTo, TurnToward) are skipped: they move
agents that spawners let out, and agents do not walk yet. On Refuse
Disposal (level 11), walking up to the portal plays the whole scene.
Evidence: `docs/formats/script.md`, "Running a script".

## Milestone 16: chest loot (done)

Chests drop what the original rolls for them: a number of picks from the
chest's item group, by weight, money with its amount, and the chest's own
item, one every half second, on the floor beside the chest. A click on an
item, or E, Space or the pad's A, walks there and picks it up; money adds
to a counter shown at the top left. Evidence: `docs/formats/loot.md`.

## Milestone 17: enemies fight back (done)

Enemies think every quarter second as the original's minds do. They notice
the player within 9 cells when it is visible enough (running and fighting
make it more visible) and nothing tall is in the way, walk up and swing;
their blows roll against the player's defence and armour. Biters flee when
hurt, enemies wander when restless and go to where they last saw the player.
At 0 hit points the player dies; a press starts the game again. Evidence:
`docs/enemies.md`.

## Milestone 18: shooting (done)

The player fires the ranged weapon: the other mouse button on an enemy (or
G, the pad's Y / Triangle or the left trigger) walks into range and plays
the shoot animation, which aims (leading a walking target) and fires a
bolt from the rifle's muzzle every 0.4 s. Bolts are the weapon's particle
emitter (`.emt`, now read): they fly at its speed, stop at walls and hit
the first hostile agent in their way with the original's ranged roll. Each
shot costs energy, which comes back over time. Evidence: `docs/combat.md`
("Ranged attacks"), `docs/formats/emitter.md`.

## Milestone 19: enemies fire (done)

Infantry and Walkers carry their ranged weapon (template stat 3), come to
half their shoot animation's range and fire the weapon's bolts at the
player, leading it when it walks, while their energy lasts. Bolts are drawn
added to the picture under them, as the original's particle pass does.
Evidence: `docs/enemies.md` ("Firing").

## Milestone 20: spawners (done)

Pods, Vantir stations and nests hold their agents until a trigger or a
script opens them. Held agents wait on the first frame of their Awake
animation (or unseen), do not think and cannot be attacked; a spawner plays its frames,
starts the gizmos attached to it at its Begin Activate frame, and lets its
agents out at its End Activate frame, where they play their Awake
animation. On Om's Outer Sanctum (level 33) the cutscene opens two pods
and Khapra and Croton rise out of them. Evidence: `docs/formats/gizmo.md`,
"Spawners".

## Next

1. Configure Controls (`keyconfig.itf`, KeyField) and button tooltips
   (`GCtrlButton_v10`).
2. Display gamma, Bink video (later or skipped).
3. The other classes' states (breakers, key consoles, lifts); the inventory.
4. The scripts' agent commands (MoveTo, TurnToward) and the woken agents'
   first steps, on the enemies' minds. The room graph.
5. Experience; the other particle descriptions; the long range and flanking
   policies.
6. The in-game loop, HUD and lighting.
