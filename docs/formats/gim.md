# `.gim` images

All 2D art (10,158 files: floor/wall tiles, sprites, sprite sheets, UI, item
icons) and the save-game thumbnails. 8-bit paletted, with an RLE for sprites.
The game also accepts `.tga` and picks the decoder by a type tag on the file
object (`FUN_004bfe70`: tag `'gim '` goes to `FUN_004c08d0`, tag `'tga '` to `FUN_004c0ba0`).

Decoder: `src/formats/gim.cpp`. Tests: `tests/test_gim.cpp`. Viewer: `gim2tga`.

## Layout

```
u16  width
u16  height
u16  pitch            row stride for raw payloads (width rounded up)
u8   kind             1 for all paletted images; 3 for 14 unpaletted ones
u8   flags            low nibble = encoding, 0x10 = has pixel data, 0x20 = has palette
i16  originX          meaning unknown (non-zero on some character frames)
i16  originY
-- if kind == 1 and flags & 0x20 --
u16  paletteCount
u8   palette[count][4]  B, G, R, A
-- if (encoding & 3) != 0 --
u8   extra[8]         meaning unknown (usually FF FF FF FF FF 00 00 00)
-- if flags & 0x10 --
u32  dataSize
u8   data[dataSize]
```

Encodings seen: 0, 2, 3 = raw (`pitch * height` bytes); 4, 5, 7 = RLE.

RLE row:

```
u16  rowBytes         including this field
u8   segmentCount
repeat segmentCount:
  u8 transparent      pixels to skip
  u8 literal          palette indices that follow; 0xFF means 254
  u8 index[literal]
```

A payload can hold several frames back to back (`height` rows each). All such
sheets have `fsp` in their path; frame counts range from 3 to 73.

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| 12-byte header, palette, extra, sized payload | proven | Loader `FUN_004c08d0` reads exactly these fields and flags; all 10,158 files parse to their exact length |
| RLE row format | proven by test | Every row of every RLE file ends exactly at `rowBytes`, sums to `width`, and every payload is consumed exactly. The decoder routine itself hasn't been read yet |
| Literal `0xFF` = 254 pixels | proven by test | Without this rule 927 files fail; with it, all pass. It looks like an encoder quirk |
| Multi-frame sheets | proven by test | 5,376 payloads decode as 3 to 73 whole frames with no remainder |
| Palette byte order B, G, R, A | proven | The main menu's text colour in a capture of the original, RGB (0,254,137), is entry 4 of StartBkg's palette read as B, G, R, A (the engine colours text with it, see fnt.md) |
| RLE palettes reserve entries 0-1 for pure green and pure red markers | proven by survey | 9,486 palettes have pure green at index 0, 5,141 have pure red at index 1, 372 have them swapped; no other slot carries them often |
| Green marks the shadow, red is a one-pixel rim (hover highlight) | guessed | Green sits under characters' feet; red outlines figures exactly. Renderer code not yet traced. Agent frame sheets use six marker entries instead (agent.md) |
| Raw paletted images use pure green as a colour key | likely | Cursor frames and the character-choice portraits are raw with pure green at index 0; the original shows neither with green |
| Encoding bit 2 means RLE, encoding 7 uses palette alpha | guessed | Encoding 7 palettes carry alpha values other than 0xFF; renderer not yet traced. Interface control images ignore it: the Options slider track (alpha 0x80/0xC0) shows opaque in the original |
| Meaning of origin fields, `extra`, and `kind` 3 | unknown | |
