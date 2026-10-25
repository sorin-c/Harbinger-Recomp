# HUD and inventory

What the rebuild has of the in-game interface: the character's bottom
panels (health, health packs, energy, the CHAR and MAP buttons, the log
button) and the inventory screen the CHAR button opens (equipment slots,
mod chip sockets, the item grid, money, item descriptions). Pickups and the
starting items go into the inventory as the original adds them, and what is
equipped is what the character wears and fights with.

Code: `src/world/inventory.*` (the model), `src/world/inventory_layout.*`
(layout from the .itf), `src/world/item_catalog.*`, `src/world/item_text.*`
(description text), `src/formats/itm.*` (mod chip and injector bodies),
`src/app/hud.*` (drawing, mouse, pad), `src/app/world_view.*`,
`src/app/harbinger.cpp`. Tests: `tests/test_inventory.cpp`.

Labels: **proven** (decomp and the original agree, or a file check), **likely**
(read from the decomp or the art, not seen running), **guessed**.

## Panels

Each character has `res\common\interface\<human|gladiator|culibine>\`
`left.itf`, `right.itf`, `logbuttons.itf`, `inventory.itf` (and `stats.itf`,
`weaponmodleft.itf`, not rebuilt). The art and text take the background
image's palette, as the front end does.

At 1024x768, FUN_004c4af0 moves each panel's background by its itf kind;
the other controls are relative to the background. **Proven** by matching
the rebuild against the original under Wine (same pixels at the same place):

| kind | move | Human result |
|---|---|---|
| 0, 0x10 (left) | y + 168 | (0, 680) |
| 1, 0xa, 0x12 (right) | x + 224, y + 168 | (764, 680) |
| 3 (inventory) | x + 224 | (543, -1) |
| 5, 6, 0xb, 0xd-0xf, 0x13, 0x15-0x17, 0x19 | x + 112, y + 94 | not seen |
| 0x1a (log buttons) | x + 224, y × 1.28 | (658, 706) |

The log buttons' scaled y gives 696, but the original draws them at 706.
The rebuild adds the 10 pixels without knowing where they come from. At
800x600 nothing moves.

## Controls

| control | rebuild | evidence |
|---|---|---|
| HealthBar (Human) | A window half the slider's width (226 → 113) slides along it: column `fraction × 113` onwards. Full health shows the red end, none the spring. | GCtrlHealthBar_v3 (0x4629a0): **likely**. The full bar matches Wine. |
| GladHealthBar | The same window moving the other way (its art has the fill on the left). | **likely**, from the art |
| CulHealthBar | The meter cut to the fraction from the left. | **guessed** |
| Health bar motion | The bar moves toward the real value at 0.25 a second, and shows at least 5% while above 0. | GCtrlHealthBar_v3 / _v7: **likely** |
| WarningLight | Under 21% health (0x5506d8), blinks every 100 ms between image 0 and the background. | **likely** |
| HealthBtn | Shows the bottle plus the count's digit image, `min(count, 10) + 2` for the Human (after its three bottles) and `+ 1` for the others. Nothing when the count is 0. A click uses a pack. | GCtrlHealthBtn: **likely**. The "5" matches Wine. The other characters' offset is **guessed**. |
| Health button popup | "N Health Packs" / "1 Health Pack" after 500 ms. | strings: **likely** |
| EnergyBar | The meter's rows from `height × (1 − fraction)` down. | GCtrlEnergyBar_v3 (0x464830): **likely**. Full matches Wine. |
| InvThreeStateBtn (CHAR) | Nothing while closed, image 1 ("On") while the inventory is open. | **proven** (Wine, both states) |
| ThreeStateBtn (MAP) | Drawn off. A click is reported (there is no map yet). | |
| InjectorBtn | A full bottle when the injector slot holds one. Colour and fill level are not decoded. | **guessed** |
| DropItemBtn, QuestLogButton, Stats | Not drawn (the background shows). | |

## Inventory screen

- **Grid** (GCtrlInventoryGrid_v3): cells 26 px apart, 25 px of art. The
  Human's grid is at background + (129, 299). Covered cells are filled
  (1, 1, 84) inside a one-pixel edge, then each item is drawn from its
  top-left cell. **Proven**: the pixels match Wine with the chip in cell (0, 0).
- **Placement preview**: the cells a held item would take are filled
  (0, 97, 50) where it fits (**proven**, seen in Wine). For a swap (97, 97, 0)
  and blocked (97, 0, 0) are **guessed**.
- **Equipment slots** (GCtrlEquipSlot_v3): an enabled slot draws its picture
  and its item centred on it. A disabled socket shows the background.
  **likely**, matching Wine.
- **Money**: the itf's text box, right-aligned, with the background's palette.
  **proven** ("0").
- **Sizes**: the grid is 12×5 for the Human and Culibine and 12×6 for the
  Gladiator (FUN_004ceaa0). The health pack counter starts at 5 of 10 for the
  Human and Gladiator and 1 of 1 for the Culibine. **likely**.
- **Slot kinds**: an EquipSlot control's class id − 0x10, except the drop
  slots (HumDropItemEquipSlot 0x17 → 0x16, GladDropItemEquipSlot 0x30 → 7,
  from FUN_004cd130). A socket (kinds 2, 4, 6, 0xa, 0xc, 0xe, 0x12) belongs
  to the slot before it. **likely**.
- **Clicks**: these are in the model (`inventory.hpp`, with the FUN_ refs
  there). The Human's starting items end up as Wine shows them (armour,
  rifle, chip in the rifle's socket): **proven**. The Gladiator's and
  Culibine's are all equipped too (test).
- **Sounds**: inventory sound slot 3 for a put and 4 for a swap
  (GCtrlInventoryGrid_v7: **likely**), and 2 for a take (**guessed**).
- **InventoryEquipMask**: holding an item over the top part of the screen
  hands the click to the slot that takes it, an empty one first. **likely**.
- **View**: while the inventory is open, the world view centres on the part
  it leaves free (inventory x / 2). In Wine the player stands at x ≈ 270:
  **proven**.

## Description popup

`descpopup.itf` (FUN_004f5470 / FUN_004f5520):

- **Size**: the text's size plus (22, 18), at most 512 high.
- **Background**: Backgd cropped to that size, with Screen inset by one pixel.
- **Text box**: fonts 7, 16 (lines starting '&', orange) and 11 ('*'), padding (10, 8).
- **Placement**: centred over the item and kept on screen; below the item when
  there is no room above.

The armour's popup matches Wine pixel for pixel: **proven**.

The text (`item_text.cpp`) follows the format strings
`"%s   %s: Lvl. %hu (%hu - %hu) (+%hu)"`, `"Damage:"`, `"Slots: "`, `"AC: "`
and `" mod chip"`:

- The chip: "EMP mod chip (+1)" / "Weapon".
- The rifle: "Melee: Lvl. 7 (6 - 15)", "Plasma: Lvl. 5 (4 - 10) (+1)".

How a chip changes the weapon's lines is **guessed**: one level above the
weapon's best ranged level, in the '&' font. Energy lines for ranged weapons
are not shown yet.

## Controller (rebuild)

This works like Diablo III on a console and Diablo II: Resurrected:

| input | action |
|---|---|
| View / Back | Opens or closes the inventory. |
| D-pad or left stick | Moves a focus frame over the enabled slots and the grid cells. The stick repeats after 280 ms, then every 120 ms. The next target is the nearest one in the pressed direction, with sideways distance counting double. |
| A | Picks up or puts down (the same as a click). A held item follows the focus. |
| X | Quick move: a grid item goes to the first slot that takes it (an empty one first), and what it replaces goes to the grid. An equipped item that can come off goes to the grid. |
| B | Closes the inventory (a held item goes back into the grid first). |
| D-pad up (inventory closed) | Uses a health pack. |

Over an item, the focus shows its description.

Keyboard and mouse:

- I, C or Tab toggle the inventory.
- Esc closes it.
- Q uses a health pack.
- The secondary mouse button does the quick move.

## Not yet

- The heal from a health pack (FUN_00420f80 hands it to the player's plug).
  The counter goes down, but hit points come with the enemy fight-back work.
- Real hit point and energy values: the bars show full.
- The weapon mod panel, stats panel, map and quest log.
- Dropping items on the floor.
- Injector colours.
- Loading original saves.
