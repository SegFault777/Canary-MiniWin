/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_FONT_KERN_H
#define MW_FONT_KERN_H

/* font_kern.h -- the kerning arithmetic of font.h (see the long comment there), on an EXPLICIT glyph and cell size instead of the font face
 * that happens to be selected. font.h's font_adv()/font_kern() call these with the current face and FONT_CELL; the HTML engine (layout.h,
 * render.h) calls them with the built-in 11-pixel table it always paints from, so a page is measured with the very glyphs it is painted
 * with, and so that a host test can build the engine without the screen code of font.h. Only needs the u16 type. */

#define KERN_MAX 2

static inline int fk_first(u16 b) { return b ? __builtin_clz((u32)b << 16) : 99; }     /* column (0 = leftmost) of the first pixel in a row */
static inline int fk_last(u16 b)  { return b ? 15 - __builtin_ctz((u32)b) : -99; }     /* column of the last pixel in a row */
static int fkx_ink_l(const u16 *g, int cell) { int m = 99; for (int r = 0; r < cell; r++) { int l = fk_first(g[r]); if (l < m) m = l; } return m; }
static int fkx_ink_r(const u16 *g, int cell) { int m = -99; for (int r = 0; r < cell; r++) { int l = fk_last(g[r]); if (l > m) m = l; } return m; }
/* A glyph only one or two columns wide (. , : ; i l) gets one extra empty column on EACH side: without it a full stop sits right
 * up against the curve of the letter before it and reads as part of that letter ("SETTING.MWP" looked like "SETTINGMWP"). */
static int fkx_pad(const u16 *g, int cell) { int l = fkx_ink_l(g, cell); return (l != 99 && fkx_ink_r(g, cell) - l + 1 <= 2) ? 1 : 0; }
/* how far to the right the NEXT character starts after glyph g (kerned); g == 0: a character the font does not have */
static int fkx_adv(const u16 *g, char c, int cell) {
    if (!g) return cell;
    int l = fkx_ink_l(g, cell);
    if (l == 99) return c == ' ' ? 4 : cell;                          /* a blank glyph: only the space is narrow */
    int w = fkx_ink_r(g, cell) - l + 2 + 2 * fkx_pad(g, cell);        /* ink columns + 1 empty column (+ the narrow glyph's side columns) */
    return w > cell ? cell : w;
}
/* the extra shift (0 .. -KERN_MAX) between glyph ga and a following glyph gb */
static int fkx_kern(const u16 *ga, const u16 *gb, int cell) {
    if (!ga || !gb) return 0;
    int la = fkx_ink_l(ga, cell), lb = fkx_ink_l(gb, cell);
    if (la == 99 || lb == 99) return 0;                               /* a space or blank: nothing to tuck under */
    int adv = fkx_adv(ga, 0, cell), pa = fkx_pad(ga, cell), pb = fkx_pad(gb, cell), best = 99;
    for (int r = 0; r < cell; r++) {
        if (!ga[r] || !gb[r]) continue;                               /* only rows where BOTH letters have ink can collide */
        /* ...but a pixel also touches the pixels one row above and below it: a full stop tucked beside the last pixel row of a "G"
         * would otherwise slide up against the G's right-hand stroke (it ended up looking like part of the letter). So each side's
         * edge in this row is the widest/narrowest of rows r-1..r+1. */
        int ar = -99, bl = 99;
        for (int q = r - 1; q <= r + 1; q++) {
            if (q < 0 || q >= cell) continue;
            int e = fk_last(ga[q]), f = fk_first(gb[q]);
            if (e > ar) ar = e;
            if (f < bl) bl = f;
        }
        int gap = (adv + pb + bl - lb) - (pa + ar - la) - 1;          /* empty columns between them around this row */
        if (gap < best) best = gap;
    }
    int want = (pa | pb) ? 2 : 1;                                     /* the tightest row keeps 1 empty column -- 2 next to a . , : i l, which read as part of the neighbour at 1 */
    if (best == 99 || best <= want) return 0;                         /* no shared row, or already as tight as wanted */
    int k = best - want;
    return -(k > KERN_MAX ? KERN_MAX : k);
}

#endif
