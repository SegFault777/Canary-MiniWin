/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FONT_H
#define FONT_H
#include "vga.h"
#include "font_latin_data.h"

/* 11x11 bitmap font, Galmuri11-flavored (see third_party/galmuri-font/
 * and tools/gen_latin_font.py) -- covering all of printable ASCII,
 * uppercase AND lowercase both drawn as their own real glyphs now
 * (the old 8x8 font's lowercase table was a separate hand-drawn
 * afterthought bolted onto an uppercase-only base; Galmuri11 just
 * draws both properly because a real font was never missing lowercase
 * to begin with). We didn't suffer for this one -- a font family that
 * already existed suffered for us, and we're deeply grateful.
 *
 * FONT_CELL is the fixed advance width AND height every glyph takes
 * up on screen -- font_draw_string() has no per-character kerning or
 * variable advance, exactly like the 8x8 version it replaces, just
 * bigger (the kerned path for UI labels is further down: font_kern()). Any code doing manual `x += 8` character-spacing math
 * elsewhere in the kernel needs updating to FONT_CELL; see the UI
 * scale-up pass for that sweep. */
#define FONT_CELL font_cell

/* 1.1-alpha-14: the font is no longer one fixed table. A FACE is a cell size plus a Latin table and a Hangul table whose glyphs are
 * `font_cell` rows of u16 (MSB-aligned, so a 7-pixel face just uses fewer columns). The three pointers below select the face in use;
 * they start on the built-in Galmuri11 (this file's tables and the inflated Hangul table), which is the only face the floppy image
 * carries. Everything that reads a glyph goes through font_latin_glyph() / ko_glyph_for_codepoint(), and everything that lays text out
 * uses FONT_CELL, so switching face is: set font_cell and the table pointers. (Galmuri7/9/14 and the Bold face come from the font pack
 * on the full image; see docs/dev.) */
static int font_cell = 11;
static int font_desc = FONT_LATIN_DESC;       /* rows of DESCENDER under each Latin glyph of the face in use: the tails of g j p q y , that do not fit the square cell */

static const u16 *font_cur_latin = &font_latin[0][0];                       /* FONT_LATIN_COUNT glyphs of font_cell + font_desc rows */
/* The pitch of lines of text: the cell and the descender rows under it (13 for the 11-pixel face; it was FONT_CELL + 1 before the tails of g j p q y were
 * kept, which made them touch the next line). */
#define FONT_LINE_H (FONT_CELL + font_desc)
static inline const u16 *font_latin_glyph(int i) { return font_cur_latin + i * (font_cell + font_desc); }   /* a Latin glyph is font_cell + font_desc rows */

static inline char to_upper(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

static inline void font_draw_char(int x, int y, char c, u32 color) {
    if (c < FONT_LATIN_FIRST_CP || c >= FONT_LATIN_FIRST_CP + FONT_LATIN_COUNT) {
        return; /* don't know this character, and not gonna guess */
    }
    const u16 *glyph = font_latin_glyph((int)(unsigned char)c - FONT_LATIN_FIRST_CP);

    /* the cell, then the descender rows under it (1.1-alpha-14: the tail of a "g" is no longer cut off at the cell's last row) */
    for (int row = 0; row < FONT_CELL + font_desc; row++) {
        u16 bits = glyph[row];
        if (!bits) continue;
        for (int col = 0; col < FONT_CELL; col++) {
            if (bits & (0x8000 >> col)) {
                bb_putpixel(x + col, y + row, color);
            }
        }
    }
}

/* ============================================================
 * KERNING (1.1-alpha-12) -- Settings > Text > Kerning.
 *
 * The font is a fixed 11x11 cell, so "AV" sits in two cells with a hole between the shoulders of the A and the V, and an "i" sits
 * in a cell three times as wide as it is. Kerning fixes both without a second font and without a table:
 *   - every glyph's INK (the first and last column that has a pixel in it) is measured straight from its bitmap, and the kerned
 *     advance is ink width + 1 empty column (never wider than the cell, so kerned text is never wider than fixed text);
 *   - for a pair of letters, each of the 11 pixel rows says how far the right edge of the first letter is from the left edge of the
 *     second; the tightest row decides how much the pair may slide together while keeping one empty column there. That is how A/V,
 *     T/o, L/T, P/A close up and how l/l or H/H do not move at all -- the same idea a type designer's kerning table encodes by
 *     hand, computed here from the pixels (11 clz/ctz per pair, nothing stored). A pair never moves more than KERN_MAX pixels.
 * Latin letters are kerned everywhere text is proportional: UI labels (ko_draw_mixed_string / ko_string_width), every text field
 * (ui/textfield.h), the Notepad text area, MiniShell's output (words kept on the column grid, see apps/terminal.h) and web page text
 * (layout.h/render.h through font_kern.h). Hangul syllables are full-cell squares and are never kerned.
 * `font_kerning` is the switch (Settings > Text); off, ko_draw_mixed_string is exactly what it was in alpha-11.
 * ============================================================ */
static int font_kerning = 1;

#include "font_kern.h"          /* the arithmetic below, on explicit glyphs (shared with the HTML engine) */

static inline const u16 *fk_glyph(char c) {
    int i = (int)(unsigned char)c - FONT_LATIN_FIRST_CP;
    return (i < 0 || i >= FONT_LATIN_COUNT) ? 0 : font_latin_glyph(i);
}
static inline int fk_ink_l(const u16 *g) { return fkx_ink_l(g, FONT_CELL); }
static inline int fk_ink_r(const u16 *g) { return fkx_ink_r(g, FONT_CELL); }
static inline int fk_pad(const u16 *g) { return fkx_pad(g, FONT_CELL); }
/* how far to the right the NEXT character starts after `c` (kerned) */
static int font_adv(char c) { return fkx_adv(fk_glyph(c), c, FONT_CELL); }
/* the extra shift (0 .. -KERN_MAX) between `a` and a following `b` */
static int font_kern(char a, char b) { return fkx_kern(fk_glyph(a), fk_glyph(b), FONT_CELL); }
/* one glyph with its ink starting exactly at x (the kerned advance counts from the ink, not from the cell) */
static inline void font_draw_char_k(int x, int y, char c, u32 color) {
    const u16 *g = fk_glyph(c);
    if (!g) return;
    int l = fk_ink_l(g);
    font_draw_char(x + fk_pad(g) - (l == 99 ? 0 : l), y, c, color);
}

/* Plain-ASCII strings in the kerned style, for the many labels that are not Hangul-capable (taskbar, icon captions, title bars, buttons,
 * status bars): font_width_k() is how wide font_draw_k() paints it. With Settings > Text > Kerning off both are exactly the fixed-cell
 * font_draw_string() and FONT_CELL * length. */
/* (1.1-alpha-14) Both are UTF-8 aware now: a 3-byte Hangul syllable/jamo is one full FONT_CELL (drawn through the Korean font), Latin
 * letters are kerned as before -- they ARE ko_string_width() / ko_draw_mixed_string() of font_ko.h, which come later in the include order. */
static inline int ko_string_width(const char *s);
static inline void ko_draw_mixed_string(int x, int y, const char *s, u32 color);
static int font_width_k(const char *s) { return ko_string_width(s); }
static inline void font_draw_string(int x, int y, const char *s, u32 color);
static void font_draw_k(int x, int y, const char *s, u32 color) { ko_draw_mixed_string(x, y, s, color); }

static inline void font_draw_string(int x, int y, const char *s, u32 color) {
    int cx = x;
    while (*s) {
        if (*s == '\n') { cx = x; y += FONT_LINE_H; s++; continue; }
        font_draw_char(cx, y, *s, color);
        cx += FONT_CELL;
        s++;
    }
}

/* Rotates a glyph 90 degrees on the way to the screen, purely by
 * relabeling which bit goes where -- no actual spinning involved, we
 * just read the same 11x11 bitmap sideways. This is exactly the kind of
 * "vertical logo" trick Windows 95 pulled off in its Start Menu, and
 * we're stealing it shamelessly rather than shipping a whole second
 * font just so a handful of letters can stand up straight.
 * (x,y) is the top-left corner of the resulting FONT_CELLxFONT_CELL
 * box, same as font_draw_char -- it just happens to contain a sideways
 * letter. Uppercased first (via to_upper) since the vertical taskbar
 * strip is the one spot in the UI that still wants shouty caps. */
static inline void font_draw_char_vertical(int x, int y, char c, u32 color) {
    char u = to_upper(c);
    if (u < FONT_LATIN_FIRST_CP || u >= FONT_LATIN_FIRST_CP + FONT_LATIN_COUNT) return;
    const u16 *glyph = font_latin_glyph((int)(unsigned char)u - FONT_LATIN_FIRST_CP);

    for (int row = 0; row < FONT_CELL; row++) {
        u16 bits = glyph[row];
        for (int col = 0; col < FONT_CELL; col++) {
            if (bits & (0x8000 >> col)) {
                bb_putpixel(x + row, y + (FONT_CELL - 1 - col), color);
            }
        }
    }
}

/* Stacks rotated characters upward from (x, y_bottom), so the string
 * reads bottom-to-top -- perfect for a thin vertical strip along the
 * left edge of a popup menu, or for silently judging anyone still
 * running this OS at 640x400. */
static inline void font_draw_string_vertical(int x, int y_bottom, const char *s, u32 color) {
    int cy = y_bottom;
    while (*s) {
        font_draw_char_vertical(x, cy, *s, color);
        cy -= FONT_CELL;
        s++;
    }
}

#endif
