# Harbinger-Recomp

A clean-room, portable reimplementation of **Harbinger** (Silverback
Entertainment, 2003), in the spirit of DevilutionX: modern Windows and Linux
first, other platforms later, with full controller support.

This repository contains **no game data**. You need your own copy of the game;
the engine reads the original `res0.sqh` / `res1.sqh` archives.

## Status

Step 1 (mapping the game) is underway. See [docs/findings.md](docs/findings.md).

- `.sqh` archive reader and `sqhtool` (list / extract)
- `.gim` image decoder and `gim2tga` viewer

## Build

```
cmake -S . -B build -G Ninja
cmake --build build
HARBINGER_DATA="/path/to/Harbinger" ctest --test-dir build --output-on-failure
```

Tests that need the original files are skipped when `HARBINGER_DATA` is unset.

## Approach

Every format and behaviour is documented with its evidence and labelled
proven, likely or guessed (see `docs/`). Decompiler output is used only as a
reference and never copied into the source tree.
