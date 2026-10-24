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

## Next

1. Configure Controls (`keyconfig.itf`, KeyField) and button tooltips
   (`GCtrlButton_v10`).
2. Display gamma, Bink video (later or skipped).
3. Gizmo animation (frame sequences and codes) and states.
4. Scripts (`hscript.mgr` and friends) that triggers run; gizmo states.
   The player on screen, walking, using the room graph.
5. The in-game loop, HUD and lighting.
