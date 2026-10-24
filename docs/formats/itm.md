# `.itm` item templates

1,105 items are listed in `item.mgr`, created by `GItemMgr` (`FUN_004d81a0`)
through a factory of 0x1a classes (table at 0x584570, filled at start-up by
each class's registration). Decoded so far: the common header, class 25
`GAnimatedItem` (the front-end buttons: main menu ids 743-746, character
choice 1000-1002) and the equipment classes 1-8. All 628 equipment items
parse to their last byte.

| Index | Class | Kind (slot 5) | Sprite layer |
|---|---|---|---|
| 1 | `GHumanWeaponItem` | 5 | 1 |
| 2 | `GHumanArmorItem` | 1 | 0 |
| 3 | `GHumanHelmetItem` | 3 | 2 |
| 4 | `GGladiatorRangedWeaponItem` | 0xb | 2 |
| 5 | `GGladiatorMeleeWeaponItem` | 0xd | 1 |
| 6 | `GGladiatorTorsoItem` | 9 | 0 |
| 7 | `GLeftGauntletItem` | 0xf | none |
| 8 | `GRightGauntletItem` | 0x11 | none |
| 9 | `GInjectorItem` | | |
| 10 | `GMoneyItem` | | |
| 11 | `GModChipItem` | 2, 4, ... | none |
| 12, 19 / 13, 20 | `GMHerpEggItem` / `GFHerpEggItem` | | |
| 17, 18 | `GCameraDropItem`, `GMineDropItem` | | |
| 21, 22 | `GOffensiveAmplifierItem`, `GDefensiveAmplifierItem` | | |
| 23, 24 | `GDataCardItem`, `GSchematicItem` | | |
| 25 | `GAnimatedItem` | | |

The sprite layer is where `GEquipCell_v1` (0x4cb7e0) puts the item's
animation group when it is equipped (`docs/movement.md`).

Parser: `src/formats/itm.cpp`. Animation: `src/ui/item_animation.cpp`.
Tests: `tests/test_resources.cpp`, `tests/test_ui.cpp`.

## Layout

```
u16  classId            factory index (25 = GAnimatedItem)
u16  version            x 1/256 (0x00552a28): 563 = 2.2 in most files, 537 = 2.1
u16  unknown2
u16  image              itemimg.mgr id
u16  frameCount         frames stacked vertically in the image (square for the buttons)
u16  unknown5, unknown6 10, 10 in all animated items
u32  unknown7
str  name
u8   hasBlock           1 = 8 bytes follow (FUN_004d5d10 -> FUN_004d6fc0; all 0xff in the starting items)
...  class body         read by vtable slot 3
u16  trailer            read last by the base loader when version > 1.2; 11 in all animated items
```

Equipment body (classes 1-8, `GSocketedItem_v3` 0x503290, then the class's
own loader):

```
u16  item+0x38
u8   item+0x3c
u16  animGroup          item+0x3e: id in the wearer's *animgroup.mgr
armour, helmet, torso (GArmorItem_v3 0x4357d0):
  u16 item+0x54         when version > 1.1
all (FUN_0048ba50), when version > 2.1:
  u16, u16 n, n x (u16, u16)
```

`GAnimatedItem` body: `u16 loopStart, loopEnd, pressEnd`.

## Animation (`GAnimatedItemPlug_v3`)

The item's instance steps at most one frame per update, once more than 100 ms
have passed:

| State | Set by | Step |
|---|---|---|
| 1 hover | pointer enters (`GCtrlAnimatedBtn_v9`) | `f + 1`, wrapping to `loopStart` at `loopEnd` |
| 0 idle | pointer leaves | inside the loop: play on to `loopEnd`, then jump to `loopStart - 1`; past `loopEnd`: rewind; then rewind to 0 |
| 6 pressed | click (`GCtrlAnimatedBtn_v7`) | `f + 1`, jumping over the hover loop to `loopEnd`; at `frameCount` go to `pressEnd` |
| 5 reset | | frame 0, then idle |

The exact transitions are in `ui::ItemAnimation::step` and its test.

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| Equipment body | proven | `GSocketedItem_v3`, `GArmorItem_v3`, `GGladiatorMeleeWeaponItem_v3` (shared by the weapons), `FUN_0048ba50`; all 628 class 1-8 files parse to their exact length |
| `animGroup` names the sprite | proven | `GEquipCell_v1` passes item+0x3e to `FUN_004efcf0`; Alpha Advance Armor -> 18 "Armor 7" and E-16 Rifle -> 6 "Rifle 2" match the original's sprite |
| Version word | proven | `FUN_004d5c10` scales it by 1/256 and the loaders compare against 1.1, 1.2 and 2.1 |
| Header, name and class-25 body | proven | `FUN_004d5c10` (14 bytes, 4 bytes, string, flag, slot-3 body, u16) and `GAnimatedItem_v3` (6 bytes into +0x40/+0x42/+0x44); all 9 class-25 files parse to their exact length |
| `image` is an itemimg.mgr id | proven by data | 743 → image 295 → `Start\New.tga`, the New Game button art |
| `frameCount` | likely | Equals the strip height / width for all seven buttons |
| State machine | proven | Transcribed from `GAnimatedItemPlug_v3`; hover/press transitions from `GCtrlAnimatedBtn_v7` / `_v9` |
| Options, Load and Exit set `pressEnd = frameCount` | observed | One past the last frame; how the engine draws that is unknown, the rebuild holds the last frame |
