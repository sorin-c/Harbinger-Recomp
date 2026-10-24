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
- `harbinger`, the game: currently the front end (main menu, character
  screen, Options, Load Game), drawn from the original files, with the menu
  music and sounds, and mouse, keyboard and gamepad input
- `harbinger-viewer`, an SDL3 sprite browser (keyboard or gamepad)

```
harbinger --data "C:/Games/Harbinger"
```

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
