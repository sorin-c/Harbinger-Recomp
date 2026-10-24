# Loot: item groups (`.gig`) and what chests drop

Chests, barrels and trash piles (gizmo classes Chest, Barrel, TrashPile;
plug `GBarrelPlug`) drop items when they finish opening. What they drop comes
from an **item group**, a weighted loot table. Enemies name item groups too
(the Enemy folder), for their drops on death, which are not rebuilt yet.

Parser: `src/formats/gig.cpp`. Roll and ground items: `src/world/loot.cpp`.
Drawing and picking up: `src/app/world_view.cpp`. Tests: `tests/test_world.cpp`.

## Which group

`Harbinger.ini` has an `ItemGroups` index in each character's section
(`[Human]` -> `res\common\managers\hitemgroup.mgr`, and so on), and one in
`[Paths]`. `FUN_004d7f90` loads the one of the playing character's section
(it asks the game for the section name, `FUN_004a2530`). The `[Paths]` index
names files that do not exist, so it is unused (likely). A group id means a
different table for each character: group 8 is `gladlow.gig` in the
Gladiator's index and a Human group in the Human's.

## Group layout

Little-endian; `GItemGroup` loader `FUN_004d6ec0`, entries `FUN_00441c30`.

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | read first by the loader; zero in every shipped group |
| 8 | 1 | money minimum (group+0x14) |
| 9 | 1 | money maximum (group+0x15) |
| 10 | 1 | count minimum (group+0x16) |
| 11 | 1 | count maximum (group+0x17) |
| 12 | 4 | entry count n |
| 16 | 4 n | entries: u16 item (item.mgr id), u16 weight |

Every group of the three characters parses to the byte (test).

## What a chest holds

`GBarrelPlug_v22` (0x445140), after the base gizmo fields and the label,
reads three words (four before version 1.22) and the state:

| Word | Field | Meaning |
|---|---|---|
| 0 | plug+0xde | item group id, 0 = none |
| 1 | plug+0xe0 | an item that always drops, 0 = none |
| 2 | flags: bit 0 -> plug+0xdc, bit 1 -> plug+0xdd | with bit 1 and no group, `GBarrelPlug_v29` asks for the level's default group, but `FUN_004a51d0` always answers 0 |
| 3 | plug+0x7c | starting state |

Of the 1197 chests in the shipped levels, 1066 name a group.

## The drop

`GBarrelPlug_v29`: when the opening's frames have played (state 2 -> 3) it
calls `FUN_004d8dd0` with the group, the sure item and the chest's position.

1. Count (`FUN_004d6f30`): `countMin + rand % (countMax - countMin + 1)`.
2. Each pick (`FUN_00441dc0`): `r = rand % total weight`; the first entry
   whose running total of weights reaches `r` (`r <= total so far`). An item
   id of 0 ends the drop.
3. A money item (the item's plug is a `GMoneyPlug`; `money.itm`, class 10,
   named "Uan") gets an amount (`GMoneyPlug+0x38`):
   `moneyMin + rand % (moneyMax - moneyMin)`, or `moneyMin` when they are equal.
4. The sure item drops after the group's items, unless the group already
   dropped it.
5. Each item is queued 500 ms after the one before (the delay argument grows
   by 500).

| Claim | Label | Evidence |
|---|---|---|
| Group layout | proven | loaders above; all groups parse to the byte |
| Chest fields | proven | `GBarrelPlug_v22` disassembly; every chest's group is in the Human index and every sure item in item.mgr (test) |
| Count, pick, money roll, sure item | proven | `FUN_004d8dd0`, `FUN_004d6f30`, `FUN_00441dc0` |
| Money is item class 10 | likely | the only class-10 item is `money.itm`, which the money groups list; the class to plug mapping is not read |
| One item every 500 ms | likely | the delay argument passed per item; what consumes it is not read |

## Rebuild choices and open questions

- Where items land: the original lays them out on a pattern of half cells
  beside the chest (`FUN_004d9500`, side chosen by `FUN_004d96e0`, pattern
  tables at `0x57eac8` = `0, 1, 2, 1, 0, 2` and offsets at `0x57eae0`), and
  checks each spot with `FUN_004a2880`. That is not rebuilt; the rebuild
  takes the free walkable half cells nearest the chest's use point.
- How a dropped item is drawn: not read. An item image holds `frameCount`
  pictures stacked vertically; the rebuild draws the first, centred on the
  item's point, on the floor under everything standing.
- Picking up: a click on the item, or E / Space / the pad's A for the
  nearest within 3 cells, walks there and takes it (a stand-in for
  `GPickUpAction`). Money adds to a counter; other items go on a plain list
  until the inventory is rebuilt.
- One Culibine group (`basegladblade3.gig`) has money 10..2; it lists no money
  item, so it never matters.
- Enemy drops on death (`GAgentPlug` calls `FUN_004d8dd0` with plug+0x1f8)
  are not rebuilt.
