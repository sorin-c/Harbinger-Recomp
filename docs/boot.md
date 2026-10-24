# Boot path: launch to main menu

## Running the original for reference

The retail `Harbinger.exe` runs headless under Wine 9 on Linux, with no GPU
(llvmpipe), and reaches the main menu. `research/wine/run-original.sh` holds
the recipe: no dgVoodoo DLLs, a Wine virtual desktop, and a null ALSA device.
That gives the project a reference to compare the rebuild against, frame by
frame, on any machine.

Screenshots of the original are not committed, because they are game art.
Generate them locally with the script.

The menu renders at 800x600 in the top-left of the 1024x768 desktop, as the
readme describes ("the resolution in the startup menu screens is fixed at
800x600").

## Entry point (static)

| Address | What | Label |
|---|---|---|
| `0x52d9de` | CRT entry, calls WinMain | proven |
| `0x528d00` | WinMain: creates the `GGame` singleton (`0x58510` bytes, global `0x585670`) and runs an outer loop that can rebuild the game (e.g. after a video-mode change, ini `Video/HiRes`) | proven |
| `0x4935d0` | `GGame` constructor | likely |
| `0x496900` | `GGame` init: window, DirectDraw/D3D, managers, interfaces | likely |
| `0x4973d0` | Front-end step, called until it returns non-zero (1 = start game) | likely |
| `0x497850` / `0x4a6d20` / `0x498a20` | Load a game / per-frame tick / frame update in the in-game loop | guessed |

## Resource load order (dynamic, proven)

Captured with `strace` while the original booted to the main menu. Reads were
mapped back to archive entries with `research/wine/map-load-order.py`: 1,970
archive entries and 17 loose files.

1. Loose: `Harbinger.ini`, `Data/stringlist.str`
2. `font.mgr` → 16 `.fnt` fonts
3. `weapons.mgr` → 865 `.wpn` files
4. `sound.mgr`, Miles providers from `Data/Miles/`, `config.cfg`
5. Videos `Data/dcIntro.bik`, then `Data/SBLogo.bik`
6. `res/common/music/intro.mp3` (menu music) starts
7. `effects.mgr`, `itemimg.mgr`, `item.mgr` → 912 `.itm` files
8. `selection.mgr`, `invfeedback.mgr`
9. `interface.mgr` → every interface up front: console, npcdialog, thoughtbub0/1,
   level, descpopup, load, save, map, weaponmodleft/right, trade, datacard,
   stash, options, keyconfig, new, **startup**, warning (`.itf` plus their `.gim` art)
10. `cursor.mgr` → three animated cursors (`.acr` + 6 frames each)
11. Main menu button art (`start/load.gim`, `option.gim`, `exit.gim`, `new.gim`)

Side result: every path present in both archives was read from `res1`
(9 of 9), so **res1 overriding res0 is now proven**. Agent, level and
gizmo data is not touched before the menu.

## What the rebuild needs for the main menu

`.fnt`, `.itf` (startup.itf: background + 4 text labels + 4 buttons), `.acr`
cursors, the menu MP3, and `.bik` playback (can be skipped at first).
