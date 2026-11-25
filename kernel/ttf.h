/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_TTF_H
#define MW_TTF_H

/* ttf.h -- a TrueType rasterizer for the Fonts folder (1.1-alpha-14).
 *
 * Reads a .ttf (or a TrueType-flavoured .otf, or the first font of a .ttc) that sits in memory and turns glyphs into the same 1-bit rows the
 * font pack carries: u16 per row, MSB-aligned, bit 15 = the leftmost pixel, the baseline on the last row of the `cell` square and the tails
 * of g j p q y under it (kernel/fontpack.h describes the face layout; ttf_make_face() below builds a whole face).
 *
 * What it does: sfnt table directory, head / maxp / loca / glyf / cmap (formats 4 and 12) / hhea, simple and composite glyphs (scale, x/y
 * scale and 2x2 matrices, x/y offsets), quadratic outlines flattened into line segments, non-zero winding fill with exact horizontal
 * coverage and TTF_SS sub-scanlines, then a coverage threshold. Everything is integer (26.6 fixed point); no 64-bit arithmetic, no libc.
 * What it does NOT do: hinting (so small sizes of ordinary fonts look soft -- pixel fonts such as Galmuri are exact, their outlines lie on
 * the pixel grid), CFF outlines (a .otf with PostScript outlines is refused with TTF_ERR_CFF), point-matching composites, variable fonts'
 * variations (the default instance is drawn), kerning, ligatures, vertical text.
 * Every read is bounds-checked: a damaged or hostile font costs its own glyphs (blank), never memory outside the file or the work area.
 * Needs: nothing but u8/u16/u32/i32 (host tests and the kernel both have them). */

#define TTF_OK          0
#define TTF_ERR_FORMAT  1                       /* not an sfnt font, or the table directory is damaged */
#define TTF_ERR_CFF     2                       /* OpenType with PostScript (CFF) outlines: not supported */
#define TTF_ERR_TABLES  3                       /* a table we need is missing or does not fit */
#define TTF_ERR_CMAP    4                       /* no Unicode character map we can read */

#define TTF_SS          4                       /* sub-scanlines per pixel row */
#define TTF_MAX_EDGES   3072
#define TTF_MAX_PTS     1200
#define TTF_MAX_CROSS   160
#define TTF_MAX_DEPTH   5
#define TTF_MAX_PARTS   64                      /* components in one composite glyph tree */
#define TTF_COV_MIN     (TTF_SS * 64 * 45 / 100)  /* a pixel is ink when at least 45% of it is covered */

typedef struct {
    const u8 *d; u32 n;
    u32 head, maxp, loca, loca_n, glyf, glyf_n, cmap, hhea;
    u32 upm; i32 asc, dsc; u32 nglyph; int loca_long;
    u32 sub4, sub12;                            /* absolute offsets of the cmap subtables we use (0 = none) */
} ttf_t;

typedef struct { i32 x0, y0, x1, y1; } ttf_edge_t;

/* The scratch space of one glyph: the caller allocates it once (about 60 KB) and reuses it for every glyph. */
typedef struct {
    ttf_edge_t e[TTF_MAX_EDGES]; int ne; int over;
    i32 px[TTF_MAX_PTS], py[TTF_MAX_PTS]; u8 on[TTF_MAX_PTS], fl[TTF_MAX_PTS];
    i32 ppem; int parts;
} ttf_work_t;

/* ---- bounds-checked reads (a read outside the file gives 0) ---- */
static inline u32 ttf_u8(const ttf_t *t, u32 o)  { return (o < t->n) ? t->d[o] : 0; }
static inline u32 ttf_u16(const ttf_t *t, u32 o) { return (o + 2 > o && o + 2 <= t->n) ? (u32)((t->d[o] << 8) | t->d[o + 1]) : 0; }
static inline i32 ttf_s16(const ttf_t *t, u32 o) { u32 v = ttf_u16(t, o); return (i32)(v & 0x8000u ? (i32)v - 65536 : (i32)v); }
static inline u32 ttf_u32(const ttf_t *t, u32 o) { return (o + 4 > o && o + 4 <= t->n) ? ((u32)t->d[o] << 24 | (u32)t->d[o + 1] << 16 | (u32)t->d[o + 2] << 8 | t->d[o + 3]) : 0; }

/* Opens a font held in memory. TTF_OK or one of the TTF_ERR_ codes; `t` is only usable after TTF_OK. */
static int ttf_open(ttf_t *t, const u8 *data, u32 n) {
    u8 *z = (u8 *)t; for (u32 i = 0; i < sizeof *t; i++) z[i] = 0;
    t->d = data; t->n = n;
    if (n < 12) return TTF_ERR_FORMAT;
    u32 base = 0, tag = ttf_u32(t, 0);
    if (tag == 0x74746366u) {                                   /* 'ttcf': a collection -- take its first font */
        base = ttf_u32(t, 12);
        if (base == 0 || base + 12 > n) return TTF_ERR_FORMAT;
        tag = ttf_u32(t, base);
    }
    if (tag == 0x4F54544Fu) return TTF_ERR_CFF;                 /* 'OTTO' */
    if (tag != 0x00010000u && tag != 0x74727565u) return TTF_ERR_FORMAT;   /* 0x00010000 or 'true' */
    u32 nt = ttf_u16(t, base + 4);
    if (nt == 0 || nt > 64 || base + 12 + nt * 16 > n) return TTF_ERR_FORMAT;
    u32 hhea = 0, glyf = 0, glyf_n = 0, loca = 0, loca_n = 0, cmap = 0, cmap_n = 0, head = 0, maxp = 0, cff = 0;
    for (u32 i = 0; i < nt; i++) {
        u32 r = base + 12 + i * 16, tg = ttf_u32(t, r), off = ttf_u32(t, r + 8), len = ttf_u32(t, r + 12);
        if (off > n || len > n - off) continue;                 /* a table that does not fit is as good as missing */
        if      (tg == 0x68656164u) head = off;                 /* 'head' */
        else if (tg == 0x6D617870u) maxp = off;                 /* 'maxp' */
        else if (tg == 0x6C6F6361u) { loca = off; loca_n = len; }   /* 'loca' */
        else if (tg == 0x676C7966u) { glyf = off; glyf_n = len; }   /* 'glyf' */
        else if (tg == 0x636D6170u) { cmap = off; cmap_n = len; }   /* 'cmap' */
        else if (tg == 0x68686561u) hhea = off;                 /* 'hhea' */
        else if (tg == 0x43464620u) cff = off;                  /* 'CFF ' */
    }
    if (!glyf && cff) return TTF_ERR_CFF;
    if (!head || !maxp || !loca || !glyf || !cmap) return TTF_ERR_TABLES;
    t->head = head; t->maxp = maxp; t->loca = loca; t->loca_n = loca_n; t->glyf = glyf; t->glyf_n = glyf_n; t->cmap = cmap; t->hhea = hhea;
    t->upm = ttf_u16(t, head + 18);
    if (t->upm < 16 || t->upm > 16384) return TTF_ERR_TABLES;
    t->loca_long = ttf_s16(t, head + 50) != 0;
    t->nglyph = ttf_u16(t, maxp + 4);
    if (t->nglyph == 0 || (t->nglyph + 1) * (t->loca_long ? 4u : 2u) > loca_n) return TTF_ERR_TABLES;
    if (hhea) { t->asc = ttf_s16(t, hhea + 4); t->dsc = ttf_s16(t, hhea + 6); }
    /* cmap: prefer format 12 (all of Unicode), take a format 4 (the BMP) beside it */
    u32 nm = ttf_u16(t, cmap + 2);
    if (nm > 64) nm = 64;
    for (u32 i = 0; i < nm; i++) {
        u32 r = cmap + 4 + i * 8, plat = ttf_u16(t, r), enc = ttf_u16(t, r + 2), off = ttf_u32(t, r + 4);
        if (off >= cmap_n) continue;
        u32 sub = cmap + off, fmt = ttf_u16(t, sub);
        int unicode = (plat == 0) || (plat == 3 && (enc == 1 || enc == 10));
        if (!unicode) continue;
        if (fmt == 12 && !t->sub12) t->sub12 = sub;
        else if (fmt == 4 && !t->sub4) t->sub4 = sub;
    }
    if (!t->sub4 && !t->sub12) return TTF_ERR_CMAP;
    return TTF_OK;
}

/* Character -> glyph index; 0 = the font has no glyph for it (glyph 0 is .notdef). */
static u32 ttf_gid(const ttf_t *t, u32 cp) {
    u32 g = 0;
    if (t->sub12) {
        u32 ng = ttf_u32(t, t->sub12 + 12), lo = 0, hi = ng;
        if (ng > 0x10000u) ng = hi = 0x10000u;
        while (lo < hi) {                                       /* the group with the greatest start <= cp */
            u32 mid = (lo + hi) / 2, gs = ttf_u32(t, t->sub12 + 16 + mid * 12);
            if (gs <= cp) lo = mid + 1; else hi = mid;
        }
        if (lo > 0) {
            u32 r = t->sub12 + 16 + (lo - 1) * 12, gs = ttf_u32(t, r), ge = ttf_u32(t, r + 4), sg = ttf_u32(t, r + 8);
            if (cp >= gs && cp <= ge) g = sg + (cp - gs);
        }
    }
    if (g == 0 && t->sub4 && cp <= 0xFFFFu) {
        u32 sx2 = ttf_u16(t, t->sub4 + 6), seg = sx2 / 2, lo = 0, hi = seg;
        u32 endp = t->sub4 + 14, startp = endp + sx2 + 2, deltap = startp + sx2, rangep = deltap + sx2;
        while (lo < hi) {                                       /* the first segment whose end >= cp */
            u32 mid = (lo + hi) / 2;
            if (ttf_u16(t, endp + mid * 2) >= cp) hi = mid; else lo = mid + 1;
        }
        if (lo < seg) {
            u32 st = ttf_u16(t, startp + lo * 2), delta = ttf_u16(t, deltap + lo * 2), ro = ttf_u16(t, rangep + lo * 2);
            if (cp >= st) {
                if (ro == 0) g = (cp + delta) & 0xFFFFu;
                else {
                    u32 gi = ttf_u16(t, rangep + lo * 2 + ro + (cp - st) * 2);
                    if (gi) g = (gi + delta) & 0xFFFFu;
                }
            }
        }
    }
    return (g < t->nglyph) ? g : 0;
}

/* The glyf-table range of glyph `g`: offset into the file, length; 0 length = no outline (a space). 0 = damaged. */
static int ttf_glyph_range(const ttf_t *t, u32 g, u32 *off, u32 *len) {
    if (g >= t->nglyph) return 0;
    u32 a, b;
    if (t->loca_long) { a = ttf_u32(t, t->loca + g * 4); b = ttf_u32(t, t->loca + g * 4 + 4); }
    else              { a = ttf_u16(t, t->loca + g * 2) * 2u; b = ttf_u16(t, t->loca + g * 2 + 2) * 2u; }
    if (b < a || b > t->glyf_n) return 0;
    *off = t->glyf + a; *len = b - a;
    return 1;
}

/* font units -> 26.6 pixels, rounded to nearest (no 64-bit: |v| <= 2^18 and ppem <= 64 keep v * ppem * 64 inside 32 bits) */
static inline i32 ttf_px64(const ttf_work_t *w, i32 v, u32 upm) {
    i32 num = v * w->ppem * 64, h = (i32)(upm / 2);
    return num >= 0 ? (num + h) / (i32)upm : -((-num + h) / (i32)upm);
}

static void ttf_add_edge(ttf_work_t *w, i32 x0, i32 y0, i32 x1, i32 y1) {
    if (y0 == y1) return;                                       /* horizontal edges never cross a scanline */
    if (w->ne >= TTF_MAX_EDGES) { w->over = 1; return; }
    ttf_edge_t *e = &w->e[w->ne++];
    e->x0 = x0; e->y0 = y0; e->x1 = x1; e->y1 = y1;
}

static inline i32 ttf_abs(i32 v) { return v < 0 ? -v : v; }
/* (a * x + b * y) >> 16 with a 64-bit intermediate: two widening 32x32 multiplies, which the compiler does inline (no libgcc call) */
static inline i32 ttf_mat(i32 a, i32 b, i32 x, i32 y) { return (i32)((((long long)a * x) + ((long long)b * y)) >> 16); }

static void ttf_add_quad(ttf_work_t *w, i32 x0, i32 y0, i32 cx, i32 cy, i32 x1, i32 y1) {
    i32 d = ttf_abs(cx - x0) + ttf_abs(cy - y0) + ttf_abs(x1 - cx) + ttf_abs(y1 - cy);
    i32 n = 1 + d / 192;                                        /* about one segment per three pixels of control polygon */
    if (n > 8) n = 8;
    i32 px = x0, py = y0;
    for (i32 i = 1; i <= n; i++) {
        i32 a = (n - i) * (n - i), b = 2 * i * (n - i), c = i * i, nn = n * n;
        i32 x = (a * x0 + b * cx + c * x1) / nn, y = (a * y0 + b * cy + c * y1) / nn;
        if (i == n) { x = x1; y = y1; }
        ttf_add_edge(w, px, py, x, y);
        px = x; py = y;
    }
}

/* The contours points [s..e] of w->px/py/on (26.6 pixels) become edges. */
static void ttf_contour(ttf_work_t *w, int s, int e) {
    int n = e - s + 1;
    if (n < 2) return;
    int i0 = -1;
    for (int i = s; i <= e; i++) if (w->on[i]) { i0 = i; break; }
    i32 sx, sy; int first, count;
    if (i0 >= 0) { sx = w->px[i0]; sy = w->py[i0]; first = i0 - s + 1; count = n - 1; }
    else         { sx = (w->px[s] + w->px[e]) / 2; sy = (w->py[s] + w->py[e]) / 2; first = 0; count = n; }
    i32 cx = sx, cy = sy, qx = 0, qy = 0; int have = 0;
    for (int j = 0; j < count; j++) {
        int idx = s + (first + j) % n;
        i32 x = w->px[idx], y = w->py[idx];
        if (w->on[idx]) {
            if (have) ttf_add_quad(w, cx, cy, qx, qy, x, y); else ttf_add_edge(w, cx, cy, x, y);
            cx = x; cy = y; have = 0;
        } else {
            if (have) { i32 mx = (qx + x) / 2, my = (qy + y) / 2; ttf_add_quad(w, cx, cy, qx, qy, mx, my); cx = mx; cy = my; }
            qx = x; qy = y; have = 1;
        }
    }
    if (have) ttf_add_quad(w, cx, cy, qx, qy, sx, sy); else ttf_add_edge(w, cx, cy, sx, sy);
}

/* The outline of glyph g into w's edge list. m = 16.16 matrix {xx, xy, yx, yy} (x' = xx*x + xy*y, y' = yx*x + yy*y) and an offset (dx, dy) in font units. */
static int ttf_outline(const ttf_t *t, ttf_work_t *w, u32 g, const i32 m[4], i32 dx, i32 dy, int depth) {
    u32 off, len;
    if (depth > TTF_MAX_DEPTH || !ttf_glyph_range(t, g, &off, &len)) return 0;
    if (len == 0) return 1;                                     /* an empty glyph is fine */
    if (len < 10) return 0;
    i32 nc = ttf_s16(t, off);
    if (nc >= 0) {                                              /* ---- a simple glyph ---- */
        if (nc == 0) return 1;
        u32 endp = off + 10, insn = ttf_u16(t, endp + (u32)nc * 2);
        u32 np = ttf_u16(t, endp + (u32)(nc - 1) * 2) + 1u;
        if (np > TTF_MAX_PTS || endp + (u32)nc * 2 + 2 + insn > off + len) return 0;
        u32 p = endp + (u32)nc * 2 + 2 + insn, end = off + len;
        u8 *fl = w->fl;
        for (u32 i = 0; i < np; ) {                             /* flags, with repeat counts */
            if (p >= end) return 0;
            u8 f = t->d[p++]; fl[i++] = f;
            if (f & 8) {
                if (p >= end) return 0;
                u32 r = t->d[p++];
                while (r-- && i < np) fl[i++] = f;
            }
        }
        i32 vx = 0, vy = 0;
        for (u32 i = 0; i < np; i++) {                          /* x deltas, then y deltas */
            u8 f = fl[i];
            if (f & 2) { if (p >= end) return 0; i32 dv = t->d[p++]; vx += (f & 16) ? dv : -dv; }
            else if (!(f & 16)) { if (p + 2 > end) return 0; vx += ttf_s16(t, p); p += 2; }
            w->px[i] = vx;
        }
        for (u32 i = 0; i < np; i++) {
            u8 f = fl[i];
            if (f & 4) { if (p >= end) return 0; i32 dv = t->d[p++]; vy += (f & 32) ? dv : -dv; }
            else if (!(f & 32)) { if (p + 2 > end) return 0; vy += ttf_s16(t, p); p += 2; }
            w->py[i] = vy;
            w->on[i] = fl[i] & 1;
        }
        for (u32 i = 0; i < np; i++) {                          /* transform (font units, 16.16 matrix), then to pixels */
            i32 x = w->px[i], y = w->py[i];
            i32 tx = ttf_mat(m[0], m[1], x, y) + dx, ty = ttf_mat(m[2], m[3], x, y) + dy;
            w->px[i] = ttf_px64(w, tx, t->upm); w->py[i] = ttf_px64(w, ty, t->upm);
        }
        int s = 0;
        for (i32 c = 0; c < nc; c++) {
            int e = (int)ttf_u16(t, endp + (u32)c * 2);
            if (e < s || (u32)e >= np) return 0;
            ttf_contour(w, s, e);
            s = e + 1;
        }
        return !w->over;
    }
    /* ---- a composite glyph: components, each with a matrix and an offset ---- */
    u32 p = off + 10, end = off + len;
    for (;;) {
        if (p + 4 > end || ++w->parts > TTF_MAX_PARTS) return 0;
        u32 fl = ttf_u16(t, p), cg = ttf_u16(t, p + 2); p += 4;
        i32 a1, a2;
        if (fl & 1) { if (p + 4 > end) return 0; a1 = ttf_s16(t, p); a2 = ttf_s16(t, p + 2); p += 4; }
        else        { if (p + 2 > end) return 0; a1 = (i32)(signed char)t->d[p]; a2 = (i32)(signed char)t->d[p + 1]; p += 2; }
        i32 lm[4] = { 65536, 0, 0, 65536 };
        if (fl & 8)         { if (p + 2 > end) return 0; lm[0] = lm[3] = ttf_s16(t, p) * 4; p += 2; }                      /* F2Dot14 -> 16.16 */
        else if (fl & 0x40) { if (p + 4 > end) return 0; lm[0] = ttf_s16(t, p) * 4; lm[3] = ttf_s16(t, p + 2) * 4; p += 4; }
        else if (fl & 0x80) { if (p + 8 > end) return 0; lm[0] = ttf_s16(t, p) * 4; lm[2] = ttf_s16(t, p + 2) * 4; lm[1] = ttf_s16(t, p + 4) * 4; lm[3] = ttf_s16(t, p + 6) * 4; p += 8; }
        i32 ox = 0, oy = 0;
        if (fl & 2) { ox = a1; oy = a2; }                       /* x/y offsets; point matching (flag 2 clear) is not supported: offset 0 */
        i32 nm[4];                                              /* the child's matrix, then the parent's on top of it */
        nm[0] = ttf_mat(m[0], m[1], lm[0], lm[2]);
        nm[1] = ttf_mat(m[0], m[1], lm[1], lm[3]);
        nm[2] = ttf_mat(m[2], m[3], lm[0], lm[2]);
        nm[3] = ttf_mat(m[2], m[3], lm[1], lm[3]);
        i32 ndx = ttf_mat(m[0], m[1], ox, oy) + dx;
        i32 ndy = ttf_mat(m[2], m[3], ox, oy) + dy;
        if (!ttf_outline(t, w, cg, nm, ndx, ndy, depth + 1)) return 0;
        if (!(fl & 0x20)) break;                                /* MORE_COMPONENTS */
    }
    return !w->over;
}

/* Fills the rows of one glyph: `rows` rows of u16 (zeroed first), `cell` columns, the baseline between row cell-1 and row cell.
 * Returns 1 = ink was drawn, 0 = nothing to draw (a space, a missing or damaged glyph). */
static int ttf_render(const ttf_t *t, ttf_work_t *w, u32 g, int ppem, int cell, int rows, u16 *out) {
    for (int r = 0; r < rows; r++) out[r] = 0;
    if (g == 0 || cell < 1 || cell > 16 || rows < 1 || rows > 24 || ppem < 4 || ppem > 64) return 0;
    w->ne = 0; w->over = 0; w->ppem = ppem; w->parts = 0;
    static const i32 ident[4] = { 65536, 0, 0, 65536 };
    if (!ttf_outline(t, w, g, ident, 0, 0, 0) || w->ne == 0) return 0;
    {   /* a glyph that sticks out to the left of the cell (a "j" with a hook) is moved right as a whole, by whole pixels -- the rule the bitmap faces were built with (tools/bdf_common.py) */
        i32 minx = 0;
        for (int i = 0; i < w->ne; i++) { if (w->e[i].x0 < minx) minx = w->e[i].x0; if (w->e[i].x1 < minx) minx = w->e[i].x1; }
        if (minx < 0) {
            i32 sh = ((-minx + 63) / 64) * 64;
            for (int i = 0; i < w->ne; i++) { w->e[i].x0 += sh; w->e[i].x1 += sh; }
        }
    }
    int any = 0;
    for (int r = 0; r < rows; r++) {
        u16 cov[16];
        for (int c = 0; c < 16; c++) cov[c] = 0;
        for (int k = 0; k < TTF_SS; k++) {
            i32 y = (cell - r) * 64 - ((2 * k + 1) * 64) / (2 * TTF_SS);        /* the sub-scanline's centre, 26.6, up from the baseline */
            i32 xs[TTF_MAX_CROSS]; signed char dir[TTF_MAX_CROSS]; int nx = 0;
            for (int i = 0; i < w->ne; i++) {
                const ttf_edge_t *e = &w->e[i];
                i32 lo = e->y0 < e->y1 ? e->y0 : e->y1, hi = e->y0 < e->y1 ? e->y1 : e->y0;
                if (y < lo || y >= hi || nx >= TTF_MAX_CROSS) continue;
                i32 x = e->x0 + (i32)(((y - e->y0) * (e->x1 - e->x0)) / (e->y1 - e->y0));
                int j = nx++;                                   /* insertion sort by x */
                while (j > 0 && xs[j - 1] > x) { xs[j] = xs[j - 1]; dir[j] = dir[j - 1]; j--; }
                xs[j] = x; dir[j] = (signed char)(e->y1 > e->y0 ? 1 : -1);
            }
            int wind = 0;
            for (int i = 0; i + 1 < nx; i++) {
                wind += dir[i];
                if (wind == 0) continue;                        /* outside: the span from xs[i] to xs[i+1] is inked only while the winding is non-zero */
                i32 xa = xs[i], xb = xs[i + 1];
                if (xa < 0) xa = 0;
                if (xb > cell * 64) xb = cell * 64;
                for (i32 px = xa / 64; xa < xb && px < cell; px++) {
                    i32 l = px * 64 > xa ? px * 64 : xa, rr = (px + 1) * 64 < xb ? (px + 1) * 64 : xb;
                    if (rr > l) cov[px] = (u16)(cov[px] + (rr - l));
                    if (rr >= xb) break;
                }
            }
        }
        u16 bits = 0;
        for (int c = 0; c < cell; c++) if (cov[c] >= TTF_COV_MIN) bits |= (u16)(0x8000u >> c);
        out[r] = bits; if (bits) any = 1;
    }
    return any;
}

/* ---- a whole face, in the font pack's layout (kernel/fontpack.h) ----
 * `out` receives (11172 + 51) * cell + 95 * (cell + desc) rows of u16; `ppem` is the em size in pixels.
 * `fallback` (or 0) is another face of the SAME cell and desc in that layout: the glyphs the font does not have (Hangul in a Latin-only font) are copied from it.
 * Returns the number of glyphs the font supplied in `got[0..2]` = syllables, jamo, Latin. */
#define TTF_SYLLABLES 11172
#define TTF_JAMO      51
#define TTF_LATIN     95
/* If set, ttf_make_face() calls it every 256 glyphs with (glyphs done, glyphs in all) -- a progress bar can hang on it. */
static void (*ttf_progress)(u32 done, u32 total) = 0;
static u32 ttf_face_rows(int cell, int desc) { return (u32)(TTF_SYLLABLES + TTF_JAMO) * (u32)cell + (u32)TTF_LATIN * (u32)(cell + desc); }

static void ttf_make_face(const ttf_t *t, ttf_work_t *w, int ppem, int cell, int desc, const u16 *fallback, u16 *out, u32 got[3]) {
    u16 *p = out; const u16 *fb = fallback;
    got[0] = got[1] = got[2] = 0;
    u32 done = 0;
    for (int part = 0; part < 3; part++) {
        u32 base = part == 0 ? 0xAC00u : part == 1 ? 0x3131u : 0x20u, count = part == 0 ? TTF_SYLLABLES : part == 1 ? TTF_JAMO : TTF_LATIN;
        int rows = part == 2 ? cell + desc : cell;
        for (u32 i = 0; i < count; i++, done++) {
            if (ttf_progress && (done & 255) == 0) ttf_progress(done, TTF_SYLLABLES + TTF_JAMO + TTF_LATIN);
            u32 g = ttf_gid(t, base + i);
            int has = g != 0 || (part == 2 && i == 0);          /* a space has no glyph of its own but is "there" */
            if (g) ttf_render(t, w, g, ppem, cell, rows, p);
            else for (int r = 0; r < rows; r++) p[r] = fb ? fb[r] : 0;
            if (has) got[part]++;
            p += rows; if (fb) fb += rows;
        }
    }
}

#endif
