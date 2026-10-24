# `.mgr` resource indexes

Each manager (`GGenericMgr` and subclasses: fonts, items, item images,
interfaces, cursors, sounds, weapons, ...) is driven by an index that maps
numeric ids to resource paths. Game data refers to resources by these ids, for
example a main menu button names item 743, which `item.mgr` maps to
`Res\Common\Items\Buttons\NewButton.itm`.

Parser: `src/formats/mgr.cpp`. Tests: `tests/test_resources.cpp`.
Prototype: `research/prototypes/mgr_parse.py`.

## Layout

```
u32  count
u16  ids[count]
repeat count:
  u32  length           counts the terminating NUL (unlike the engine's usual strings)
  char path[length]     NUL-terminated
u32  nameLength         the usual engine string: length without the NUL
char name[nameLength + 1]   e.g. "Items.itm", "Font.fnt"
```

**Negative count** (`GAgentMgr_v13`, `0x4b02b0`): when the count is negative
the loader negates it and skips one more `u16` before the ids. The speech
(`*speech.mgr`), dialog (`*dialog.mgr`) and `datacard.mgr` indexes use this;
the skipped word equals the first id in the speech and datacard indexes (200
in the dialog ones), and their name is `..`.

Ids are not necessarily sorted. Lookup is by id (`FUN_004b0510`, a `std::map`).

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| Layout | proven by test | 76 of 84 `.mgr` files parse to their exact length |
| Id lookup | proven | `FUN_004b0510` returns the path for an id; `FUN_004d81a0` opens it to load an item, `FUN_005138c0` to load each font |
| Negative count | proven | `0x4b02b0`; all seven such files (three speech, three dialog, datacard) parse to their exact length |
| Other layouts | open | the `*animgroup.mgr` files carry extra bytes after the name |
