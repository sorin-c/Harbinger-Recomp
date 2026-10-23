# Step 2: rebuilding the engine

## Layout

| Path | Role | Depends on |
|---|---|---|
| `src/formats/` | File formats (`.sqh`, `.gim`) | C++17 only |
| `src/engine/` | Engine core: `Vfs` (archive mounting, path rules) | formats |
| `src/render/` | Platform-neutral rendering helpers (`.gim` frame to RGBA) | formats |
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

## Next: boot path to the main menu

1. Trace `WinMain` → `GGame` start-up: ini reading, manager load order, the
   intro videos and the first interface screen.
2. Reverse `.itf` (interface layouts) and `.fnt` (fonts), which the menu needs.
3. Rebuild the main loop and draw the main menu from the original files.
