# `.acr` animated cursors

Three cursors are listed in `cursor.mgr`: Main (id 1), Place (2) and NoPlace (3),
each with six `.gim` frames. Loaded by `FUN_004734d0` (`GCursorMgr`).

Parser: `src/formats/acr.cpp`. Tests: `tests/test_resources.cpp`.

## Layout

```
u32  frameDelay         10 in every file
i16  hotX, hotY         Main (18, 5), Place / NoPlace (27, 13)
u16  frameCount
str  frames[frameCount] engine strings (length without the NUL), ".tga" paths served from .gim
```

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| Layout | proven | `FUN_004734d0` reads four fields then `count` strings with `FUN_0050fe60`; all three files parse to their exact length |
| Hotspot | proven | The loader builds the cursor quad at (-hotX, -hotY) |
| `frameDelay` unit | guessed | The rebuild treats it as hundredths of a second (100 ms per frame) |
| Green key on the frames | likely | The frames are raw paletted images with pure green at index 0, and the original draws the cursor without it |
