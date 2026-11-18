# Fonts: the font pack, faces, and the TrueType rasterizer

*`kernel/font.h` draws and measures text, `kernel/fontpack.h` reads the pack, `kernel/fontface.h` switches faces, `kernel/ttf.h`
turns a `.ttf` into a face. `tools/gen_font_pack.py` writes the pack.*

## What a face is

A face is a **cell size** (the pixel square a Hangul syllable fills: 7, 9, 11 or 14), a **descender** (`desc`: how many rows of
the tails of `g j p q y ,` hang under the cell), and three glyph tables of `u16` rows, MSB-aligned (bit 15 = the leftmost pixel):

| table | glyphs | rows per glyph |
|---|---|---|
| Hangul syllables U+AC00..U+D7A3 | 11172 | `cell` |
| jamo U+3131..U+3163 | 51 | `cell` |
| Latin U+0020..U+007E | 95 | `cell + desc` |

The baseline lies between row `cell-1` and row `cell`: capitals and every syllable sit flush with the cell, the tails go under it.
Everything that draws or measures text goes through `FONT_CELL`, `font_desc` (so `FONT_LINE_H = FONT_CELL + font_desc`) and the
three table pointers in `font.h`; `fontface_use()` repoints them and the whole desktop follows. The HTML engine draws from the
built-in 11-pixel tables on purpose and scales them to the page's own font size, so it ignores the system face.

## The font pack (`MW_FONT_LBA`, 640 sectors, [DISK-LAYOUT.md](DISK-LAYOUT.md))

Sector 0: `MWFP`, version 1, count, then 40-byte entries (name, cell, bold, desc, start sector, sectors, raw bytes, packed bytes,
CRC-32 of the raw bytes). Each face is one LZ4 block (`kernel/lz.h`) of the three tables above, in that order. Face 0 is
**Galmuri11**, the default; the ISO's full pack adds Galmuri7, 9, 14 and 11 Bold, the floppy and plain builds carry face 0 only.
`fontpack_boot()` loads face 0 at boot and checks it against `KO_FONT_CHECKSUM`; a damaged pack costs the Hangul glyphs, nothing else.
`tools/gen_font_pack.py --check` validates both checked-in packs.

## Which face is used: boot sector byte 440

Bits 0-1 = screen size (1 = 320x200, 2 = 854x480, else 640x480), **bits 2-4 = the chosen face** (pack index + 1, 0 = the default for
this screen: Galmuri7 at 320x200, Galmuri11 otherwise), bit 7 = "screen size not confirmed yet" (the 15-second *Keep this screen
size?* box). `stage2` reads only the size bits. Changing the size keeps the face bits and the other way round. A live (ISO) boot
cannot write the sector, so a choice lasts until the next restart. Write the byte from Python, never with a shell `printf '\x..'`.

## The TrueType rasterizer (`kernel/ttf.h`, 1.1-alpha-14)

`ttf_open()` takes a font in memory (`.ttf`, a TrueType-flavoured `.otf`, the first font of a `.ttc`), `ttf_gid()` maps a code point
through the cmap (formats 4 and 12), `ttf_render()` makes one glyph's rows, `ttf_make_face()` makes a whole face in the pack's layout.

- **Scale.** `ppem = cell + 1` and the baseline on the cell's last row: that is how Galmuri is built (11-pixel cell, 12-pixel em), so
  its outlines land exactly on the pixel grid. A glyph that sticks out to the left of the cell is moved right as a whole, by whole
  pixels (the same rule `tools/bdf_common.py` uses for the bitmap faces).
- **Fill.** Outlines (simple and composite glyphs, quadratic curves flattened) are filled with the non-zero winding rule: exact
  horizontal coverage, 4 sub-scanlines per pixel row, ink where at least 45 % of a pixel is covered. Integer 26.6 arithmetic only.
- **Not done.** Hinting (ordinary outline fonts look soft at 7-14 pixels; pixel fonts are exact), CFF outlines (`.otf` with
  PostScript outlines: `TTF_ERR_CFF`), point-matching composites, font variations (the default instance is drawn), kerning.
- **Safety.** Every read is range-checked and edge/point counts are capped (`TTF_MAX_*`): a damaged font costs its glyphs.
- **Gaps.** `ttf_make_face(..., fallback, ...)` copies the glyphs the font lacks (Hangul in a Latin-only font) from a face of the
  same cell and desc.
- **Cost.** About 4.6 KB of code; the work area is 61 KB (`ttf_work_t`, allocate it from the heap). A whole face is about 11,300 glyphs.
- **Test.** `tools/test/run_host_ttf.sh` rasterizes the five Galmuri TTFs and requires all 56,590 glyphs to equal the pack's bitmap
  faces pixel for pixel, renders other fonts, and fuzzes hostile input under ASan/UBSan.

The Fonts folder in Control Panel does not use the rasterizer yet: installing a `.ttf` (copy to the Fonts folder, rasterize into a
face, list it, apply it) is the next step.
