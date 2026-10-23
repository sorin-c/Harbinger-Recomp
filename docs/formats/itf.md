# `.itf` interface layouts

One file per screen or HUD panel (44 files, 41 unique paths; res1 overrides
three). The interface manager loads them all at boot (`FUN_0049eda0` →
`FUN_004c7450` → parser `FUN_004c4af0`).

Parser: `src/formats/itf.cpp`. Tests: `tests/test_resources.cpp`.
Prototype: `research/prototypes/itf_parse.py`.

## Layout

Strings use the engine's encoding (`FUN_0050fe60`): `u32 length`, the
characters, then a NUL the length does not count.

```
i32  header[3]          e.g. (0, 460, 0) for the front end, (540, 688, 1000) for a HUD panel
u16  controlCount
u16  kind               selects how the panel is positioned (see below)
repeat controlCount:
  u16  classId          index into the control factory (table at 0x583c80, 0x57 entries)
  i16  x, y, z
  str  name
  u32  imageCount
  str  images[imageCount]
  str  extra            e.g. TextBtn's hit-mask image
  ...  class fields     read by the class's load method (vtable slot 5), table below
-- if controlCount > 0 --
u8   hasSounds
-- if hasSounds == 1 --
u16  n
u16  soundIds[n]        UI sound per event slot (9 = hover, 10 = click on AnimatedBtn)
```

Class fields (H = u16, I = u32, S = str, T = text-box block):

| Fields | Classes |
|---|---|
| none | Backgd, HealthBtn, InjectorBtn, DropItemBtn, HealthBar, EnergyBar, WarningLight, GladHealthBtn/Bar, InventoryGrid, InventoryEquipMask, Cul*, EnemyHealthBar, CameraHealthBar, GladiatorToggleBtn, GladDropItemBtn, WeaponModBtn, Map, TradeTabBtn, StatsNextLevel |
| I S | Button, ThreeStateBtn, ScrollButton, BlinkBtn, InvThreeStateBtn, Trade Prev/Next/Close, CheckBox, SliderBar, StatsThreeStateBtn, StatsPlusBtn, StashPrev/Next, QuestLogButton |
| T S | StaticTextBox (0x0d, 0x2e, 0x2f), Stats* labels (0x3e-0x48) |
| I | ScanTextBox, PictureSlot |
| I H H | EquipSlot family (0x11-0x25 except 0x24, and 0x30) |
| H H | TradeGrid, StashGrid |
| I I S | TextBtn, KeyField |
| H | PlayerIcon, Icon |
| H I S | AnimatedBtn: item id, action id, string |

For Button the `I` is the action id and the `S` its caption; Button images are
off / on (hover) / pressed.

Text-box block (`GCtrlStaticTextBox_v5`, 8 x u16): width, height, font, fontAlt,
fontStar, align, padX, padY. Fonts are `font.mgr` ids: `font` for ordinary
lines, `fontAlt` for lines starting with `&` (and the highlighted line),
`fontStar` for lines starting with `*`. Align 0 = left, 0x10 = right,
0x20 = centred. Drawing rules are in [fnt.md](fnt.md).

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| Container layout, string encoding, sound trailer | proven | `FUN_004c4af0`; all 44 files parse to their exact length |
| Class ids and names | proven | Factory table read from the running original's memory (0x583c80), names from RTTI |
| Per-class fields | proven | Each class's slot-5 load method; the full-consume test over all 44 files confirms nothing is missing |
| Text-box block fields | proven | `GCtrlStaticTextBox_v5` stores them; `_v3` (draw) uses them as described. The menu labels drawn with these rules match the original's pixels |
| AnimatedBtn: item id, action id | proven | `GCtrlAnimatedBtn_v1` creates the plug from `GItemMgr` by the u16; `_v7` sends the u32 as the action when pressed |
| Sound slots 9/10 = hover/click | proven | `GCtrlAnimatedBtn_v9` plays slot 9 on hover, `_v7` slot 10 on press (`FUN_004c64e0`) |
| Front-end coordinates are 800x600 screen pixels | proven | The main menu labels and background line up with a capture of the original (docs/boot.md) |
| `kind` positions panels at 1024x768 (edge anchoring 0xe0/0xa8, centring 0x70/0x5e) | likely | Switch in `FUN_00461fd0`; not exercised yet |
| Header meaning | unknown | [0],[1] look like a panel position, [2] like a layer (0 front end, 1000/1500 in game) |
