# Step 1 findings: mapping Harbinger

Target: `Harbinger.exe` from the 2003 retail release (1,597,440 bytes, PE
timestamp 2003-03-17). Analysed with Ghidra 11.4.2 in headless mode plus the
scripts in `research/`. Every claim is labelled **proven** (seen in code or
checked by a test on the real files), **likely** (strong indirect evidence), or
**guessed**.

## 1. Build and dependencies

| Finding | Label | Evidence |
|---|---|---|
| Built with Microsoft Visual C++ 6, not Borland | proven | PE linker version 6.0; MSVC RTTI type descriptors (`.?AV...@@`); MSVC CRT imports. `vcl50.bpl` belongs to `HarbingerCfg.exe`, the Borland-built settings tool |
| 32-bit x86, image base 0x400000, 1.3 MB of code | proven | PE headers |
| Graphics: DirectDraw 7 + Direct3D 7 | proven | Only graphics import is `DirectDrawCreateEx`; classes `GDDMgr`, `GD3DMgr`. The shipped `D3D8/D3D9/DDraw.dll` files are dgVoodoo wrappers, not the game's |
| Input: DirectInput 7 | proven | `DirectInputCreateEx`; class `GDIMgr`, `GInputMgr` |
| Audio: Miles Sound System, 3D samples + streamed MP3 | proven | 44 `_AIL_*` imports incl. `AIL_*_3D_*`; `mssmp3.asi` |
| Video: Bink (intro, logos, ending) | proven | 10 `_Bink*` imports; `Data/*.bik` |
| Config from `Harbinger.ini` via `GetPrivateProfileString` | proven | Imports; ini lists every manager file path |
| The game is 2D isometric: pre-rendered sprites and tiles, no 3D models | likely | All 10,158 images are 8-bit `.gim` sprites/tiles; no mesh formats in the archives; D3D used for blitting/effects (to confirm) |

## 2. Architecture from RTTI

474 class names survive in the binary (`research/rtti-classes.txt`). From
them, 798 vtables and 2,157 virtual functions were named in Ghidra
(`research/ghidra/rtti.py`, `ApplyRtti.py`). Main families:

| Family | Count | Examples / role |
|---|---|---|
| Managers `G*Mgr` | 25 | `GAgentMgr`, `GLevelMgr`, `GItemMgr`, `GQuestMgr`, `GScriptMgr`, `GSoundMgr`, `GInterfaceMgr`, `GTextureMgr`, `GD3DMgr`, `GDIMgr`; each loads one `.mgr` file listed in `Harbinger.ini` |
| UI controls `GCtrl*` | 55 | Buttons, inventory/stash/trade grids, health bars, map, sliders, key config. This is the layer gamepad UI navigation will hook into |
| Entity "plugs" `*Plug` | 86 | Behaviour components for agents and gizmos (`GHumanPlayerPlug`, `GTurretPlug`, `GChestPlug`...) |
| Actions / policies / thinkers | 51 / 23 / 11 | AI: `GAttackAction`, `GFleePolicy`, `GBossThinker`... |
| Items `*Item` | 30 | `GHumanWeaponItem`, `GGladiatorTorsoItem`, `GAmplifierItem`... |
| Script commands `GScriptCmd*` | 23 | `MoveTo`, `Cinematic`, `Wait`, `ActivateGizmo`, `PositionCamera`... matching the text `.gsc` cutscene scripts |
| Interfaces `GI*` | 30 | `GIActivatable`, `GIScriptable`, `GIThinker`... |
| Agent templates | 30 | `GAbstractAgent<AgentTypeHolder<N>, Plug>`: one instantiation per enemy type |

## 3. Data files

| Format | Count (both archives) | Status |
|---|---|---|
| `.sqh` archives | 2 | **Done.** See `docs/formats/sqh.md`. Reader, `sqhtool`, tests |
| `.gim` images | 10,158 | **Done.** See `docs/formats/gim.md`. Decoder, `gim2tga`, tests |
| `.wav` / `.mp3` | 421 / 517 | Standard RIFF PCM and LAME MP3 (proven by signatures) |
| `.gsc` scripts | 205 | Plain text, `Version 0.x` then one command per line (proven) |
| `.dlg` `.qst` `.tre` | 1,390 / 156 / 3 | Binary, start with a version `double`, contain plain strings (likely) |
| `.itm` `.wpn` `.gig` `.gkt` `.gac` | 1,137 / 964 / 408 / 1 / 4 | Item, weapon, item-group, kit and account data |
| `.anm` `.agt` | 550 / 117 | Animation and agent definitions; `.anm` reference sprite sheet paths |
| `.giz` `.brs` `.chk` `.mck` | 798 / 20 / 93 / 93 | Gizmos, brushes, level chunk maps and their collision/marker maps (likely) |
| `.emt` `.itf` `.fnt` `.acr` `.mgr` | 179 / 44 / 19 / 3 / 84 | Particle emitters, interface layouts, fonts, cursors, manager indexes |
| Saves `.har` / `.hpr` / `.gim` | per slot | `.gim` is a 256x192 greyscale thumbnail (decodes, proven). `.har` is binary object state with no strings; `.hpr` holds the quest log text. Both are serialisations of the game classes, so they get reversed alongside those classes in step 2 |

Many binary formats begin with a little-endian `double` version (1.0, 1.2, 1.75...),
which matches the engine's "Unable to read version number" errors (likely).

## 4. Implications for the rebuild

- No 3D renderer to port: a 2D sprite renderer on SDL3 covers the graphics.
  The DirectDraw/Direct3D 7 layer (`GDDMgr`, `GD3DMgr`, `GDrawMgr`) is the
  part to replace. (likely; confirm effects/lighting paths)
- Input goes through `GDIMgr`/`GInputMgr` and the `GCtrl*` controls, so
  gamepad support has a clear seam.
- Miles is used for positional 3D samples and MP3 music; SDL3 audio plus a
  small MP3 decoder (e.g. dr_mp3) covers it.
- Bink videos need a decoder; FFmpeg's `libavcodec` supports Bink 1.

## 5. Open questions

- `res0`/`res1` override order (guessed: `res1` wins). Needs a side-by-side run.
- `.gim` origin fields, the 8 `extra` bytes, palette index 0 handling, alpha mode.
- All the gameplay formats in section 3 except archives, images, audio and scripts.

## Reproducing

```
cmake -S . -B build -G Ninja && cmake --build build
HARBINGER_DATA="/path/to/Harbinger" ctest --test-dir build --output-on-failure
```

Ghidra: `analyzeHeadless <proj> Harbinger -import Harbinger.exe -postScript ExportAll.py`,
then run `research/ghidra/rtti.py Harbinger.exe rtti.tsv` and `ApplyRtti.py`.
Decompiler output stays local (`research/local/`, git-ignored): it is reference
material only and never goes into the source tree.
