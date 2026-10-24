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

## Controls at run time

How the front end's controls behave, from their vtables (slot 3 draws, slot 7
takes mouse events, slot 14 picks the image). Code: `src/ui/controls.cpp`,
`src/ui/text.cpp`, `src/app/screen.cpp`.

Mouse events (slot 7): 0 = released elsewhere / cancelled, 1 = button down,
2 = held (every frame), 3 = released over the control (the click). A control
reports to its interface with message 0x18 carrying its action id.

| Class | Images (from 0) | Behaviour |
|---|---|---|
| Button, ScrollButton | 0 disabled, 1 normal, 2 pressed | Down shows 2, the click sends the action. The character screen's Play is disabled (image 0) until a character is picked |
| TextBtn | 0 up, 1 down | The u32 holds two font ids, low half normally and high half while pressed (`0x9000A` = font 10, then 9). Caption centred, baseline `(h - textHeight) / 2 - 1 + ascent`. Draws nothing when disabled |
| CheckBox | 0 on, 1 off | Shows 1 when unchecked, 0 when checked or pressed. The click flips the value and sends the action |
| SliderBar | 0 track off, 1 track on, 2/3 left arrow off/on, 4/5 right arrow off/on | See below |
| StaticTextBox | optional background | Wrapped text, see below |
| Icon | none | Draws an item (u16 id) centred on its position |

**SliderBar** (`GCtrlSliderBar_v1/_v3/_v7`). The track is image 0's width
(400), each arrow image 3's width (25), so the value is a position 0..350
(`0x15e`, hard-coded) shown as `position / 350`. Down on an arrow moves 3 px
and lights it; on the track it jumps to `x - x % 3 - 25`. While held the press
repeats every 25 ms. Drawn on a black surface: dark track, the lit track
clipped to `arrow .. arrow + position`, then the arrows. The handler sets a
value and the action id with message 0x27 and reads changes with 0x28, which
returns -1 when nothing changed.

**StaticTextBox** (`GCtrlStaticTextBox_v3`, `FUN_00466900`). The box grows to
its background image. Text is wrapped to the width with the normal font: words
are added while the line fits, a first word that does not fit is kept whole,
and any character below `!` other than a space ends the line. Line n has its
baseline at `ascent + padY + n * lineHeight` (normal font), and `height /
lineHeight` lines are drawn, so "Swap Mouse Buttons" in a 170 x 18 box shows
only "Swap Mouse", as in the original.

**Colours.** Everything a control draws goes through the interface background's
palette, not the image's own: the slider arrows' "+" is palette index 29,
grey in the arrow's palette but (0x35,0x2a,0x1f) in StartBkg's, and the
original shows the latter. Palette index 1 shows as (4,4,4), as in the fonts
(the Load screen's scroll arrows). Encoding 7's palette alpha is not applied
to control images (the slider track's 0x80/0xC0 entries show opaque). Item art
(animated buttons) keeps its own palette.

**Drawing order.** The background first, then higher z first; buttons
(z = -1000) end up on top. The Load screen's scroll arrows (z 5 and 10) show
over the background, so the background cannot simply sort by z.

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
| `kind` is the interface id | proven | The front-end handler `FUN_004c80c0` switches on it for message 0x18: 0xf main menu, 0x17 character choice, 0x15 Options, 0xd Load |
| `kind` also positions panels at 1024x768 (edge anchoring 0xe0/0xa8, centring 0x70/0x5e) | likely | Switch in `FUN_00461fd0`; not exercised yet |
| Button / TextBtn / CheckBox / ScrollButton states and images | proven | `GCtrlButton_v14`, `GCtrlTextBtn_v3`, `GCtrlCheckBox_v7/_v14`, `GCtrlScrollButton_v7`; Options, Load and the character screen (Play disabled, then normal, then pressed) match captures of the original pixel for pixel |
| Mouse event codes 0-3 | likely | Consistent across `GCtrlButton_v7`, `GCtrlSliderBar_v7` (repeat timer on 2, release on 0/3) and `GCtrlAnimatedBtn_v7` (acts on 3); the dispatcher was not read |
| Slider geometry, steps and repeat | proven | `GCtrlSliderBar_v1/_v3/_v7`; image widths 400 and 25 checked by `test_resources` |
| Word wrap and line placement | proven | `FUN_00466900`, `GCtrlStaticTextBox_v3`; the "Swap Mouse" label matches the original |
| Control art uses the background's palette, index 1 = (4,4,4), no palette alpha | observed | Options, Load and character screens rebuilt this way match captures of the original exactly (outside the save list and thumbnail). The code that does it is not traced |
| Background drawn first | observed | Needed for the Load screen's scroll arrows to show, as they do in the original |
| Icon centred on its position | observed | The Load screen's thumbnail frame (item 860, 256x192) sits at (272, 329) for an Icon at (400, 425) |
| Header meaning | unknown | [0],[1] look like a panel position, [2] like a layer (0 front end, 1000/1500 in game) |
