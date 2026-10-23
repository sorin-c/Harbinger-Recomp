# `.fnt` bitmap fonts

16 fonts are listed in `font.mgr` (20 files exist). Glyphs cover ASCII
0x20-0x7F. Loaded by `FUN_00491580` into a 0x2a18-byte font object owned by
`GTextMgr`.

Parser and layout: `src/formats/fnt.cpp`. Text boxes: `src/ui/text.cpp`.
Tests: `tests/test_resources.cpp`, `tests/test_ui.cpp`.
Prototype: `research/prototypes/fnt_parse.py`.

## Layout

```
repeat 96:              characters 0x20..0x7F
  i16  width            0 = no glyph (skipped, takes no space)
  i16  height
  i16  pitch            row stride of raw glyph pixels
  i16  top              rows above the baseline
  i16  left             pulled back from the pen position before drawing
  u32  offset           into pixels
u32  pixelBytes
u8   pixels[pixelBytes] palette indices; raw rows, or .gim RLE rows when rle = 1
i16  paletteCount
u8   palette[count][4]  B, G, R, A
i8   kerning[96][96]    [char][previous char]; -128 = use defaultKerning
i16  lineHeight
i8   defaultKerning
u8   rle
```

## Drawing text

From `FUN_00514550` (measure), `FUN_00513c20` (draw) and `FUN_00491710` (glyph):

- For each character with a glyph: `x = pen + kern(previous, c) - left`, draw
  at `(x, baseline - top)`, then `pen = x + width`. The first character has
  no kerning. Characters without a glyph do not change the pen or "previous".
- The text width is the final pen position.
- `ascent` = the largest `top` over all glyphs.

`GCtrlStaticTextBox` draws into a surface the size of its box: baseline at
`ascent + padY`; left-aligned text starts at `padX`, right-aligned text ends at
`padX + width`, centred text starts at `width / 2 - textWidth / 2`.

Colours: the glyph's palette indices are looked up in the palette the caller
passes. Interfaces pass their own copy of the background image's palette
(`FUN_004c4eb0` copies 256 entries from the background), so the same font
takes each screen's colours. Index 0 is transparent.

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| File layout | proven | `FUN_00491580` reads exactly these fields; all 20 fonts parse to their exact length |
| RLE glyphs use .gim RLE rows | proven by test | Every row of every glyph in the 3 RLE fonts decodes exactly to the glyph width |
| Layout and kerning rules | proven | Read from the three routines above. The four main menu labels rendered with them match the original's pixels exactly in position (9,303 glyph pixels compared) |
| Text colour from the background palette | proven | Code above; "New Game" in the original is RGB (0,254,137), which is entry 4 of StartBkg's palette |
| Index 1 drawn as a near-black fringe (4,4,4) | observed | All 534 differing pixels in that comparison are palette index 1, and the original shows (4,4,4) for every one of them whatever the background. The code responsible is not found yet |
| `lineHeight` (offset 0x2a12) used for line spacing | likely | `FUN_005144f0` returns it to the text box, which steps lines by it |
