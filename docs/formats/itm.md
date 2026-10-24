# `.itm` item templates

1,105 items are listed in `item.mgr`, created by `GItemMgr` (`FUN_004d81a0`)
through a factory of 0x1a classes (table at 0x584570). So far only the common
header and class 25, `GAnimatedItem`, are decoded: the front-end buttons
(main menu ids 743-746, character choice 1000-1002) are animated items.

Parser: `src/formats/itm.cpp`. Animation: `src/ui/item_animation.cpp`.
Tests: `tests/test_resources.cpp`, `tests/test_ui.cpp`.

## Layout

```
u16  classId            factory index (25 = GAnimatedItem)
u16  unknown1           563 in all animated items
u16  unknown2
u16  image              itemimg.mgr id
u16  frameCount         frames stacked vertically in the image (square for the buttons)
u16  unknown5, unknown6 10, 10 in all animated items
u32  unknown7
str  name
u8   hasBlock           1 = an optional block follows (FUN_004d6fc0, not decoded)
...  class body         read by vtable slot 3
u16  trailer            read last by the base loader (version-gated); 11 in all animated items
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
| Header, name and class-25 body | proven | `FUN_004d5c10` (14 bytes, 4 bytes, string, flag, slot-3 body, u16) and `GAnimatedItem_v3` (6 bytes into +0x40/+0x42/+0x44); all 9 class-25 files parse to their exact length |
| `image` is an itemimg.mgr id | proven by data | 743 → image 295 → `Start\New.tga`, the New Game button art |
| `frameCount` | likely | Equals the strip height / width for all seven buttons |
| State machine | proven | Transcribed from `GAnimatedItemPlug_v3`; hover/press transitions from `GCtrlAnimatedBtn_v7` / `_v9` |
| Options, Load and Exit set `pressEnd = frameCount` | observed | One past the last frame; how the engine draws that is unknown, the rebuild holds the last frame |
