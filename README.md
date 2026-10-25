# Harbinger-Recomp

A clean-room, portable reimplementation of **Harbinger** (Silverback
Entertainment, 2003), in the spirit of DevilutionX: modern Windows and Linux
first, other platforms later, with full controller support.

This repository contains **no game data**. You need your own copy of the game;
the engine reads the original `res0.sqh` / `res1.sqh` archives.

## Status

- Step 1, mapping the game: first pass done. See [docs/findings.md](docs/findings.md).
- Step 2, rebuilding the engine: under way. See [docs/engine.md](docs/engine.md).

What exists so far:

- `.sqh` archive reader and `sqhtool` (list / extract)
- `.gim` image decoder and `gim2tga`
- `Vfs` (archive mounting and the game's path rules)
- Parsers for interface layouts (`.itf`), fonts (`.fnt`), resource indexes
  (`.mgr`), cursors (`.acr`) and the animated button items (`.itm`)
- Audio: `.wav` and `.mp3` decoding, `Harbinger.ini`, `config.cfg`
- `harbinger`, the game: the front end (main menu, character screen,
  Options, Load Game), drawn from the original files, with the menu music and
  sounds, and mouse, keyboard and gamepad input. Play opens the first level
  (floor, walls, objects, characters and music so far) to look around in,
  with the HUD and the inventory screen (CHAR, I or the pad's View button;
  D-pad / stick, A, X and B drive it on a controller, see `docs/inventory.md`)
- Level (`.chk`), brush (`.brs`), gizmo (`.giz`), agent (`.agt`), animation
  (`.anm`) and cutscene script (`.gsc`) parsers, and `levelshot`, which renders a whole level with its
  objects and characters to an image
- `harbinger-viewer`, an SDL3 sprite browser (keyboard or gamepad)
- Gameplay in main so far: walking (mouse, keyboard or gamepad), gear and
  melee and ranged combat, player health and death, enemy AI with melee and
  ranged attacks, spawners, portals between levels, scripted cutscenes and
  enemy moves, gizmo use (doors, consoles), chest loot, and the HUD and
  inventory screen with full controller navigation

```
harbinger --data "C:/Games/Harbinger"
```

Without `--data`, `harbinger` looks for `Harbinger.ini` next to itself and
then in the current folder, so on Windows you can copy `harbinger.exe` and
`SDL3.dll` into your Harbinger folder and double-click it. A ready-built
Windows zip (`harbinger-windows`) is attached to every CI run under the
repository's Actions tab. Your settings are kept in
`%APPDATA%\harbinger-recomp\harbinger\` (`config.cfg`, and `display.ini` for the
window size or fullscreen, set in Options or with Alt+Enter); the game folder is only read.

## Build

```
cmake -S . -B build -G Ninja      # fetches SDL3 if it isn't installed
cmake --build build
HARBINGER_DATA="/path/to/Harbinger" ctest --test-dir build --output-on-failure
```

Tests that need the original files are skipped when `HARBINGER_DATA` is unset.

## Approach

Every format and behaviour is documented with its evidence and labelled
proven, likely or guessed (see `docs/`). Decompiler output is used only as a
reference and never copied into the source tree.
