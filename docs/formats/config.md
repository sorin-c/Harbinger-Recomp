# `config.cfg` player settings

67 bytes in the install folder, written when Options is accepted
(`FUN_004a5c60` → `FUN_004ecbe0`) and read at start and on Cancel
(`FUN_004a5d70` → `FUN_004ecc70`). The rebuild keeps the same layout in its own
file (`harbinger-recomp/harbinger/config.cfg` in the user's preference folder),
starting from the install's copy on the first run.

Code: `src/engine/settings.cpp`. Tests: `tests/test_ui.cpp` (synthetic),
`tests/test_audio.cpp` (the shipped file).

```
f64  music              Options slider, 0..1
f64  sound              Options slider, 0..1
f64  gamma              Options slider, 0..1
u8   keys[21]           DirectInput scan codes, primary binding per action
u8   altKeys[21]        alternate bindings
u8   swapMouse
```

Actions, in the key configuration screen's order: Inventory, Automap, Center
Map, Highlight Item, Rigging Menu, Hide All, Use Health, Deploy, Left Plasma,
Left EMP, Left Electricity, Left Disruption, Right Plasma, Right EMP, Right
Electricity, Right Disruption, Quest Log, Quick Save, Quick Load, Screenshot,
Manual Aim. The shipped file binds C, Tab, Home, Alt, Ctrl, Delete, Space,
Enter, 4 3 2 1, 7 8 9 0, F1, F6, F7, F8 and Left Shift.

## How the Options screen uses it

`FUN_004c9180` opens Options and hands the controls their values and action
ids by `.itf` index: Swap Mouse (6) action 1, Music (8) 2, Sound (9) 3, Gamma
(10) 4, High Resolution (14) action 5. The handler (`FUN_004c80c0`, interface
0x15):

| Action | Effect |
|---|---|
| 2 | Music volume, live: Miles gets `value / 12` (`FUN_00505840`); 0 unloads the music |
| 3 | Sound volume, live (3D sample volume), then plays the interface's slot 9 sound unless it is still playing |
| 4 | Gamma, live: the display gamma becomes `1.5 - value` (`FUN_004a5fe0`) |
| 1, 5 | Flip "Swap Mouse changed" / "High Resolution changed" |
| 0 | Opens Configure Controls |
| -2 Accept | Applies the two toggles, writes config.cfg, back to the main menu |
| -3 Cancel | Reads config.cfg again and re-applies it, back to the main menu |

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| Layout and size | proven | `FUN_004ecbe0` writes 0x18 bytes of doubles, two 0x15-byte key maps (`FUN_004e1200`) and 1 byte; the shipped file is 67 bytes and round-trips |
| Order music, sound, gamma | proven | `FUN_004a5f80/_fb0/_fe0` store the sliders' values at +8/+0x10/+0x18; the shipped file (0.5, 1.0, 0.5) matches the sliders in a capture of the original |
| Key maps are scan codes in key-config order | likely | Values decode to a sensible default layout (C = Inventory, Tab = Automap, ...); the key configuration screen is not rebuilt yet |
| Last byte is Swap Mouse | likely | `FUN_004a5d70` flips the live swap flag from it on Cancel |
| Volume and gamma formulas | proven | Constants 1/12 (0x557c18) and 1.5 (0x54a048) read from the exe. Whether Miles' volume is linear amplitude is not checked |
