# Step 2: rebuilding the engine

## Layout

| Path | Role | Depends on |
|---|---|---|
| `src/formats/` | File formats (`.sqh`, `.gim`, `.mgr`, `.itf`, `.fnt`, `.acr`, `.itm`) | C++17 only |
| `src/engine/` | Engine core: `Vfs` (archive mounting, path rules) | formats |
| `src/render/` | Platform-neutral rendering helpers (`.gim` frame to RGBA) | formats |
| `src/ui/` | Interface pieces with the engine's rules: text boxes, animated items | formats, render |
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

## Next

1. Music and UI sounds (`sound.mgr`, Miles-era `.wav`/`.mp3`), and Bink video
   later or skipped.
2. Options and Load screens (`options.itf`, `load.itf`: CheckBox, SliderBar,
   TextBtn, KeyField).
3. Start a game: levels, agents and `.anm` animation, the in-game loop.
