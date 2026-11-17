/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RENDER_H
#define RENDER_H
#include "layout.h"
#include "font_latin_data.h"
#include "font_ko_data.h"

/* ============================================================
 * render.h -- the glue and the paint box of MiniWeb's HTML5 engine.
 *
 *   LOADING    rd_load_html / rd_load_plain / rd_load_message turn bytes into
 *              a DOM; rd_relayout() lays it out for the current window width.
 *   PAINTING   rd_paint() walks the display list and draws what intersects
 *              the viewport: rectangles, glyph runs (the 11x11 bitmap fonts,
 *              scaled with nearest-neighbour for big headings, smeared for
 *              bold, sheared for italic), bullets, image placeholders and form
 *              controls. Clipping is per-pixel against the viewport so a
 *              half-scrolled line is cut exactly where the window ends.
 *   HIT TEST   rd_hit() says what is under a document coordinate: a link,
 *              a form control, or a <summary>.
 *   FORMS      the few pieces of state a static page needs to be usable: which
 *              text field has the keyboard, what was typed, which boxes are
 *              ticked, which <option> is showing -- plus rd_form_query() which
 *              turns a form into the application/x-www-form-urlencoded string
 *              a GET (or POST) wants.
 *
 * The graphics backend is injected by macro so this file runs on the host too
 * (tools/test/host_render.c draws into a PNG-bound array):
 *     RD_PUTPIXEL(x, y, color)   color is whatever RD_COLOR() returned
 *     RD_FILLRECT(x, y, w, h, color)
 *     RD_COLOR(0xRRGGBB)
 * ============================================================ */

#ifndef RD_PUTPIXEL
#include "vga.h"
#define RD_PUTPIXEL(x, y, c) bb_putpixel((x), (y), (c))
#define RD_FILLRECT(x, y, w, h, c) bb_fillrect((x), (y), (w), (h), (c))
#define RD_COLOR(rgb) RGB((u8)(((rgb) >> 16) & 255), (u8)(((rgb) >> 8) & 255), (u8)((rgb) & 255))
#endif

/* ---------- clipping ---------- */
static int rc_x0, rc_y0, rc_x1, rc_y1;                 /* the current clip rectangle, [x0,x1) x [y0,y1) */

static inline void rp_px(int x, int y, u32 c) {
    if (x >= rc_x0 && x < rc_x1 && y >= rc_y0 && y < rc_y1) RD_PUTPIXEL(x, y, c);
}
static inline void rp_fill(int x, int y, int w, int h, u32 c) {
    int x1 = x + w, y1 = y + h;
    if (x < rc_x0) x = rc_x0;
    if (y < rc_y0) y = rc_y0;
    if (x1 > rc_x1) x1 = rc_x1;
    if (y1 > rc_y1) y1 = rc_y1;
    if (x1 > x && y1 > y) RD_FILLRECT(x, y, x1 - x, y1 - y, c);
}

/* ============================================================
 * rounded boxes, gradients, rings
 * ============================================================ */
static inline u32 rp_c(u32 rgb);
static inline int rp_isqrt(int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }
/* how far in from the box's left/right edge a corner of radius r cuts row j (0 = the row on the very edge) */
static inline int rp_inset(int r, int j) {
    if (j >= r || r <= 0) return 0;
    int dy = 2 * (r - j) - 1;
    int dx2 = 4 * r * r - dy * dy;
    return r - (rp_isqrt(dx2) + 1) / 2;
}
static inline u32 rp_lerp(u32 a, u32 b, int num, int den) {
    if (den <= 0) return a;
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255, br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    return (u32)((ar + (br - ar) * num / den) << 16 | (ag + (bg - ag) * num / den) << 8 | (ab + (bb - ab) * num / den));
}
static inline int rp_rad(int r, int w, int h) { int m = (w < h ? w : h) / 2; return r > m ? m : r; }

/* a filled box with rounded corners; c2 != NOCOLOR with grad = a gradient from c to c2 (1 down, 2 across) */
static void rp_rbox(int x, int y, int w, int h, int r, u32 c, u32 c2, int grad) {
    if (w <= 0 || h <= 0) return;
    r = rp_rad(r, w, h);
    if (grad == RF_GRAD_H) {                                   /* across: vertical bands, square (the corners would need a backdrop) */
        int bands = w < 96 ? w : 96;
        for (int i = 0; i < bands; i++) {
            int x0 = x + w * i / bands, x1 = x + w * (i + 1) / bands;
            rp_fill(x0, y, x1 - x0, h, rp_c(rp_lerp(c, c2, i, bands > 1 ? bands - 1 : 1)));
        }
        return;
    }
    if (grad == RF_GRAD_V) {
        int y0 = y < rc_y0 ? rc_y0 : y, y1 = y + h > rc_y1 ? rc_y1 : y + h;
        for (int j = y0 - y; j < y1 - y; j++) {
            int ins = j < r ? rp_inset(r, j) : (h - 1 - j < r ? rp_inset(r, h - 1 - j) : 0);
            rp_fill(x + ins, y + j, w - 2 * ins, 1, rp_c(rp_lerp(c, c2, j, h > 1 ? h - 1 : 1)));
        }
        return;
    }
    if (r <= 0) { rp_fill(x, y, w, h, rp_c(c)); return; }
    for (int j = 0; j < r; j++) {                              /* top and bottom corner rows */
        int ins = rp_inset(r, j);
        rp_fill(x + ins, y + j, w - 2 * ins, 1, rp_c(c));
        rp_fill(x + ins, y + h - 1 - j, w - 2 * ins, 1, rp_c(c));
    }
    rp_fill(x, y + r, w, h - 2 * r, rp_c(c));
}
/* a rounded border: the ring between the outer rounded box and the same box shrunk by bw */
static void rp_ring(int x, int y, int w, int h, int r, int bw, u32 c) {
    if (w <= 0 || h <= 0 || bw <= 0) return;
    r = rp_rad(r, w, h);
    int ir = r > bw ? r - bw : 0;
    int y0 = y < rc_y0 ? rc_y0 : y, y1 = y + h > rc_y1 ? rc_y1 : y + h;
    for (int j = y0 - y; j < y1 - y; j++) {
        int jj = j < r ? j : (h - 1 - j < r ? h - 1 - j : r);
        int oi = jj < r ? rp_inset(r, jj) : 0;
        if (j < bw || j >= h - bw) { rp_fill(x + oi, y + j, w - 2 * oi, 1, rp_c(c)); continue; }
        int ij = j - bw, ih = h - 2 * bw;
        int ii = ij < ir ? rp_inset(ir, ij) : (ih - 1 - ij < ir ? rp_inset(ir, ih - 1 - ij) : 0);
        int lx = x + oi, rx = x + w - oi;                      /* outer span */
        int ilx = x + bw + ii, irx = x + w - bw - ii;          /* inner (hole) span */
        if (ilx > irx) ilx = irx = (lx + rx) / 2;
        rp_fill(lx, y + j, ilx - lx, 1, rp_c(c));
        rp_fill(irx, y + j, rx - irx, 1, rp_c(c));
    }
}

/* ============================================================
 * glyphs
 * ============================================================ */
static inline const u16 *rp_ko_glyph(int cp) {
    if (cp >= KO_SYLLABLE_BASE && cp < KO_SYLLABLE_BASE + KO_SYLLABLE_COUNT) return font8x8_ko[cp - KO_SYLLABLE_BASE];
    if (cp >= KO_JAMO_BASE && cp < KO_JAMO_BASE + KO_JAMO_COUNT) return font8x8_ko_jamo[cp - KO_JAMO_BASE];
    return 0;
}

/* One glyph at (x,y), `cell` pixels square. cell == 11 is the font's native size; anything else is
 * nearest-neighbour scaled. bold: every set pixel is repeated one to the right. italic: rows are
 * sheared right as they go up (a quarter of the cell at the very top). */
static int rp_extra = 0;     /* descender rows under the cell for the glyph being drawn (rp_glyph_latin sets it; Hangul has none) */
static void rp_glyph(int x, int y, const u16 *g, int cell, int bold, int italic, u32 col) {
    if (!g) {                                              /* unknown character: the project's traditional hollow box */
        for (int i = 0; i < cell - 1; i++) { rp_px(x + i, y, col); rp_px(x + i, y + cell - 2, col); rp_px(x, y + i, col); rp_px(x + cell - 2, y + i, col); }
        return;
    }
    int extra = rp_extra ? (cell == 11 ? rp_extra : (rp_extra * cell + 5) / 11) : 0;        /* the descender rows, scaled like the cell */
    if (extra < 1 && rp_extra) extra = 1;
    if (y >= rc_y1 || y + cell + extra <= rc_y0 || x >= rc_x1 || x + cell + cell / 4 + 1 <= rc_x0) return;
    for (int dy = 0; dy < cell + extra; dy++) {
        int py = y + dy;
        if (py < rc_y0 || py >= rc_y1) continue;
        u16 bits = g[dy < cell ? (cell == 11 ? dy : (dy * 11) / cell) : 11 + ((dy - cell) * rp_extra) / extra];
        if (!bits) continue;
        int sh = italic ? ((cell - 1 - dy) * (cell / 4 + 1)) / cell : 0;
        for (int dx = 0; dx < cell; dx++) {
            int sx = cell == 11 ? dx : (dx * 11) / cell;
            if (bits & (0x8000 >> sx)) {
                rp_px(x + dx + sh, py, col);
                if (bold) rp_px(x + dx + sh + 1, py, col);
            }
        }
    }
}

static void rp_glyph_latin(int x, int y, const u16 *g, int cell, int bold, int italic, u32 col) {
    rp_extra = FONT_LATIN_DESC; rp_glyph(x, y, g, cell, bold, italic, col); rp_extra = 0;
}
static inline const u16 *rp_latin(u8 c) {
    if (c < FONT_LATIN_FIRST_CP || c >= FONT_LATIN_FIRST_CP + FONT_LATIN_COUNT) c = '?';
    return font_latin[c - FONT_LATIN_FIRST_CP];
}

/* One text run (already positioned): collapses whitespace the same way layout measured it. Kerned (rd_kern, not for pre runs): every letter
 * is painted with its ink starting at the pen and the pen moves by txt_adv() -- exactly the widths txt_wk()/txt_space() gave the layout. */
static void rp_text_run(int sx, int sy, const u8 *p, u32 n, int cell, int lh, u8 flags, u32 col, int width) {
    int gy = sy + (lh - cell) / 2, x = sx, prev_sp = 1;
    int pre = flags & TF_PRE, bold = (flags & TF_BOLD) != 0, ital = (flags & TF_ITALIC) != 0;
    int kern = TXT_KERNED(flags), spw = kern ? txt_space(cell) : cell;
    for (u32 i = 0; i < n; ) {
        u8 b = p[i];
        if (!pre && dom_is_ws(b)) {
            while (i < n && dom_is_ws(p[i])) i++;
            x += spw; prev_sp = 1; continue;
        }
        if (b >= 0xE0) {
            int cp = ((b & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F);
            i += 3;
            rp_glyph(x, gy, rp_ko_glyph(cp), cell, bold, ital, col);
            x += cell; prev_sp = 0; continue;
        }
        i++;
        if (b == 0x01 || b == ' ' || (pre && dom_is_ws(b))) { x += (b == 0x01 || pre) ? cell : spw; prev_sp = 1; continue; }
        if (flags & TF_UPPER) { if (b >= 'a' && b <= 'z') b = (u8)(b - 32); }
        else if (flags & TF_LOWER) { if (b >= 'A' && b <= 'Z') b = (u8)(b + 32); }
        else if ((flags & TF_CAP) && prev_sp) { if (b >= 'a' && b <= 'z') b = (u8)(b - 32); }
        if (kern) {
            const u16 *kg = wk_glyph((char)b);
            int off = 0;
            if (kg) { int l = fkx_ink_l(kg, WK_CELL); off = txt_scale(fkx_pad(kg, WK_CELL) - (l == 99 ? 0 : l), cell); }   /* the ink starts at the pen */
            rp_glyph_latin(x + off, gy, rp_latin(b), cell, bold, ital, col);
            u8 nb = 0;
            if (i < n && p[i] < 0x80) { nb = p[i]; if (flags & TF_UPPER) { if (nb >= 'a' && nb <= 'z') nb = (u8)(nb - 32); } else if (flags & TF_LOWER) { if (nb >= 'A' && nb <= 'Z') nb = (u8)(nb + 32); } }
            x += txt_adv(b, nb, cell, bold);
        } else {
            rp_glyph_latin(x, gy, rp_latin(b), cell, bold, ital, col);
            x += cell;
        }
        prev_sp = 0;
    }
    int th = cell >= 33 ? 2 : 1;
    if (flags & TF_UNDER) rp_fill(sx, gy + (cell * 10) / 11, width, th, col);
    if (flags & TF_STRIKE) rp_fill(sx, gy + (cell * 6) / 11, width, th, col);
}

/* a plain NUL-terminated ASCII/UTF-8-Hangul string, one cell per character, clipped to max_cells (negative = unlimited) */
static int rp_string(int x, int y, const u8 *s, u32 n, int cell, u32 col, int max_cells, int mask_chars) {
    int cells = 0;
    for (u32 i = 0; i < n && (max_cells < 0 || cells < max_cells); cells++) {
        u8 b = s[i];
        if (b >= 0xE0) {
            int cp = ((b & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
            i += 3;
            rp_glyph(x + cells * cell, y, mask_chars ? rp_latin('*') : rp_ko_glyph(cp), cell, 0, 0, col);
        } else {
            i++;
            if (b != 0x01 && b != ' ') rp_glyph_latin(x + cells * cell, y, rp_latin(mask_chars ? '*' : b), cell, 0, 0, col);
        }
    }
    return cells;
}

/* ============================================================
 * form control state
 * ============================================================ */
#define RD_CTL_MAX 16
#define RD_CTL_TEXT 160
typedef struct { u32 node; int len; int sel; u8 checked, flags; char text[RD_CTL_TEXT]; } rd_ctl_t;   /* flags: 1 = text edited, 2 = checked state set */
static rd_ctl_t rd_ctls[RD_CTL_MAX];
static u32 rd_focus;                  /* the control that has the keyboard (0 = none) */

static inline void rd_state_reset(void) {
    for (int i = 0; i < RD_CTL_MAX; i++) rd_ctls[i].node = 0;
    rd_focus = 0;
    rd_ntoggled = 0;
}

static inline rd_ctl_t *rd_ctl_find(u32 node, int create) {
    rd_ctl_t *free_slot = 0;
    for (int i = 0; i < RD_CTL_MAX; i++) {
        if (rd_ctls[i].node == node) return &rd_ctls[i];
        if (!rd_ctls[i].node && !free_slot) free_slot = &rd_ctls[i];
    }
    if (create && free_slot) { css_zero(free_slot, sizeof(*free_slot)); free_slot->node = node; return free_slot; }
    return 0;
}

/* text of a node and its descendants, whitespace-collapsed, into out (NUL-terminated); returns the length */
static inline int rd_node_text(u32 node, char *out, int cap) {
    int o = 0, sp = 1;
    for (u32 c = RD_NODES[node].first; c && o + 4 < cap; c = RD_NODES[c].next) {
        if (RD_NODES[c].kind == RDK_TEXT) {
            const u8 *t = RD_POOL + RD_NODES[c].a;
            for (u32 i = 0; i < RD_NODES[c].b && o + 4 < cap; i++) {
                if (dom_is_ws(t[i])) { if (!sp) out[o++] = ' '; sp = 1; continue; }
                out[o++] = (char)t[i]; sp = 0;
            }
        } else if (RD_NODES[c].kind == RDK_ELEM && RD_NODES[c].tag != TG_SCRIPT && RD_NODES[c].tag != TG_STYLE) {
            o += rd_node_text(c, out + o, cap - o);
        }
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = 0;
    return o;
}

/* the current text of an <input>/<textarea>: what the user typed, else the markup's value */
static inline const u8 *rd_ctl_text(u32 node, u32 *len) {
    rd_ctl_t *s = rd_ctl_find(node, 0);
    if (s && (s->flags & 1)) { *len = (u32)s->len; return (const u8 *)s->text; }
    if (RD_NODES[node].tag == TG_TEXTAREA) {
        for (u32 c = RD_NODES[node].first; c; c = RD_NODES[c].next)
            if (RD_NODES[c].kind == RDK_TEXT) { *len = RD_NODES[c].b; return RD_POOL + RD_NODES[c].a; }
        *len = 0; return (const u8 *)"";
    }
    const u8 *v = dom_attr(node, AT_VALUE, len);
    if (!v) { *len = 0; return (const u8 *)""; }
    return v;
}
static inline int rd_ctl_is_checked(u32 node) {
    rd_ctl_t *s = rd_ctl_find(node, 0);
    if (s && (s->flags & 2)) return s->checked;
    return dom_has_attr(node, AT_CHECKED);
}
static inline void rd_ctl_set_checked(u32 node, int on) {
    rd_ctl_t *s = rd_ctl_find(node, 1);
    if (!s) return;
    s->checked = (u8)on; s->flags |= 2;
}

/* <select>: the option that is showing */
static inline u32 rd_select_option(u32 sel) {
    rd_ctl_t *s = rd_ctl_find(sel, 0);
    if (s && s->sel) return (u32)s->sel;
    u32 first = 0;
    for (u32 c = RD_NODES[sel].first; c; c = RD_NODES[c].next) {
        u32 o = 0;
        if (RD_NODES[c].tag == TG_OPTION) o = c;
        else if (RD_NODES[c].tag == TG_OPTGROUP) {
            for (u32 d = RD_NODES[c].first; d; d = RD_NODES[d].next) if (RD_NODES[d].tag == TG_OPTION) { if (dom_has_attr(d, AT_SELECTED)) return d; if (!first) first = d; }
            continue;
        }
        if (!o) continue;
        if (dom_has_attr(o, AT_SELECTED)) return o;
        if (!first) first = o;
    }
    return first;
}
static inline void rd_select_next(u32 sel) {
    u32 cur = rd_select_option(sel), nxt = 0, firstopt = 0;
    int take = 0;
    for (u32 c = RD_NODES[sel].first; c; c = RD_NODES[c].next) {
        u32 list[2] = { 0, 0 };
        if (RD_NODES[c].tag == TG_OPTION) list[0] = c;
        else if (RD_NODES[c].tag == TG_OPTGROUP) { for (u32 d = RD_NODES[c].first; d; d = RD_NODES[d].next) if (RD_NODES[d].tag == TG_OPTION) { if (!firstopt) firstopt = d; if (take && !nxt) nxt = d; if (d == cur) take = 1; } continue; }
        if (list[0]) { if (!firstopt) firstopt = list[0]; if (take && !nxt) nxt = list[0]; if (list[0] == cur) take = 1; }
    }
    rd_ctl_t *s = rd_ctl_find(sel, 1);
    if (s) s->sel = (int)(nxt ? nxt : firstopt);
}

/* clicking a text field: it gets the keyboard, and its markup value becomes editable text */
static inline void rd_ctl_focus(u32 node) {
    rd_focus = node;
    rd_ctl_t *s = rd_ctl_find(node, 1);
    if (s && !(s->flags & 1)) {
        u32 n; const u8 *t = rd_ctl_text(node, &n);
        if (n > RD_CTL_TEXT - 4) n = RD_CTL_TEXT - 4;
        for (u32 i = 0; i < n; i++) s->text[i] = (char)t[i];
        s->len = (int)n; s->flags |= 1;
    }
}

/* Keys for the focused field. Returns: 0 = nothing happened, 1 = redraw, 2 = Enter (submit the form).
 * `ch` is an ASCII byte; for a Hangul syllable pass its three UTF-8 bytes through rd_ctl_put_bytes. */
static inline int rd_ctl_put_bytes(const u8 *b, int n) {
    rd_ctl_t *s = rd_focus ? rd_ctl_find(rd_focus, 0) : 0;
    if (!s || s->len + n >= RD_CTL_TEXT - 1) return 0;
    for (int i = 0; i < n; i++) s->text[s->len++] = (char)b[i];
    return 1;
}
static inline int rd_ctl_key(int ch) {
    rd_ctl_t *s = rd_focus ? rd_ctl_find(rd_focus, 0) : 0;
    if (!s) return 0;
    if (ch == '\n' || ch == '\r') {
        if (RD_NODES[s->node].tag == TG_TEXTAREA) { u8 c = '\n'; return rd_ctl_put_bytes(&c, 1); }
        return 2;
    }
    if (ch == '\b' || ch == 127) {
        if (s->len <= 0) return 0;
        int k = s->len - 1;
        while (k > 0 && ((u8)s->text[k] & 0xC0) == 0x80) k--;       /* back over a whole 3-byte Hangul cell */
        s->len = k;
        return 1;
    }
    if (ch >= 32 && ch < 127) { u8 c = (u8)ch; return rd_ctl_put_bytes(&c, 1); }
    return 0;
}

/* ============================================================
 * painting
 * ============================================================ */
static inline u32 rp_c(u32 rgb) { return RD_COLOR(rgb & 0xFFFFFF); }

static void rp_bevel(int x, int y, int w, int h, u32 face, int sunken) {
    u32 hi = rp_c(0xFFFFFF), lo = rp_c(0x404040), mid = rp_c(0x808080);
    rp_fill(x, y, w, h, rp_c(face));
    rp_fill(x, y, w, 1, sunken ? mid : hi); rp_fill(x, y, 1, h, sunken ? mid : hi);
    rp_fill(x, y + h - 1, w, 1, sunken ? hi : lo); rp_fill(x + w - 1, y, 1, h, sunken ? hi : lo);
}

static void rp_bullet(int x, int y, int cell, int kind, u32 col) {
    int cx = x + cell / 2, cy = y + (cell * 6) / 11;
    int r = cell >= 22 ? cell / 5 : (cell >= 14 ? 3 : 2);
    switch (kind) {
        case BUL_DISC: for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) if (dx * dx + dy * dy <= r * r + 1) rp_px(cx + dx, cy + dy, col); break;
        case BUL_CIRCLE: for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) { int d = dx * dx + dy * dy; if (d <= r * r + 1 && d >= (r - 1) * (r - 1)) rp_px(cx + dx, cy + dy, col); } break;
        case BUL_SQUARE: rp_fill(cx - r, cy - r, 2 * r + 1, 2 * r + 1, col); break;
        case BUL_RIGHT: { int hh = r + 2; for (int dx = 0; dx <= hh; dx++) { int e = hh - dx; rp_fill(cx - hh / 2 + dx, cy - e, 1, 2 * e + 1, col); } } break;
        case BUL_DOWN: { int hh = r + 2; for (int dy = 0; dy <= hh; dy++) { int e = hh - dy; rp_fill(cx - e, cy - hh / 2 + dy, 2 * e + 1, 1, col); } } break;
    }
}

/* a decoded picture scaled (nearest, or a small box average when shrinking) into the w*h rectangle at (x,y), cut by the clip */
static void rp_picture(int x, int y, int w, int h, const img_t *im) {
    if (w <= 0 || h <= 0 || !im->px) return;
    int x0 = x > rc_x0 ? x : rc_x0, y0 = y > rc_y0 ? y : rc_y0, x1 = x + w < rc_x1 ? x + w : rc_x1, y1 = y + h < rc_y1 ? y + h : rc_y1;
    for (int py = y0; py < y1; py++) {
        u32 sy0 = (u32)(py - y) * im->h / (u32)h, sy1 = (u32)(py - y + 1) * im->h / (u32)h;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 - sy0 > 4) sy1 = sy0 + 4;
        for (int px = x0; px < x1; px++) {
            u32 sx0 = (u32)(px - x) * im->w / (u32)w, sx1 = (u32)(px - x + 1) * im->w / (u32)w;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 - sx0 > 4) sx1 = sx0 + 4;
            u32 r = 0, g = 0, b = 0, cnt = 0;
            for (u32 yy = sy0; yy < sy1 && yy < im->h; yy++) for (u32 xx = sx0; xx < sx1 && xx < im->w; xx++) {
                u32 c = im->px[yy * im->w + xx]; r += c >> 16 & 255; g += c >> 8 & 255; b += c & 255; cnt++;
            }
            if (cnt) RD_PUTPIXEL(px, py, rp_c((r / cnt) << 16 | (g / cnt) << 8 | (b / cnt)));
        }
    }
}

static void rp_push_clip(int x, int y, int w, int h, int *sv) {
    sv[0] = rc_x0; sv[1] = rc_y0; sv[2] = rc_x1; sv[3] = rc_y1;
    if (x > rc_x0) rc_x0 = x;
    if (y > rc_y0) rc_y0 = y;
    if (x + w < rc_x1) rc_x1 = x + w;
    if (y + h < rc_y1) rc_y1 = y + h;
}
static void rp_pop_clip(const int *sv) { rc_x0 = sv[0]; rc_y0 = sv[1]; rc_x1 = sv[2]; rc_y1 = sv[3]; }

static void rp_control(const rd_item_t *it, int sx, int sy) {
    u32 node = it->ref; int cell = it->cell, w = it->w, h = it->h;
    u32 fg = rp_c(it->color);
    int sv[4];
    switch (it->aux) {
        case CTL_TEXT: case CTL_PASSWORD: case CTL_TEXTAREA: {
            u32 n; const u8 *t = rd_ctl_text(node, &n);
            int focused = (rd_focus == node);
            rp_push_clip(sx, sy, w, h, sv);
            int mask = it->aux == CTL_PASSWORD;
            if (it->aux == CTL_TEXTAREA) {
                int cols = imax(1, w / cell), line = 0, col = 0;
                for (u32 i = 0; i < n; ) {
                    if (t[i] == '\n') { line++; col = 0; i++; continue; }
                    if (col >= cols) { line++; col = 0; }
                    int step = t[i] >= 0xE0 ? 3 : 1;
                    rp_string(sx + col * cell, sy + 1 + line * (cell + 2), t + i, (u32)step, cell, fg, 1, 0);
                    col++; i += (u32)step;
                }
                if (focused) rp_fill(sx + col * cell, sy + 1 + line * (cell + 2), 1, cell, fg);
            } else if (n == 0 && !focused) {
                u32 pl; const u8 *ph = dom_attr(node, AT_PLACEHOLDER, &pl);
                if (ph) rp_string(sx + 1, sy + (h - cell) / 2, ph, pl, cell, rp_c(0x808080), imax(1, w / cell), 0);
            } else {
                int cells = (int)txt_cells(t, n), vis = imax(1, (w - 2) / cell), skip = (focused && cells > vis - 1) ? cells - (vis - 1) : 0;
                u32 off = 0; for (int k = 0; k < skip && off < n; k++) off += t[off] >= 0xE0 ? 3 : 1;
                int drawn = rp_string(sx + 1, sy + (h - cell) / 2, t + off, n - off, cell, fg, vis, mask);
                if (focused) rp_fill(sx + 1 + drawn * cell, sy + (h - cell) / 2, 1, cell, fg);
            }
            rp_pop_clip(sv);
        } break;
        case CTL_BUTTON: {
            if (it->flags & 1) break;                              /* the invisible click-catcher over a <button> */
            u32 n; const u8 *v = dom_attr(node, AT_VALUE, &n);
            u32 tl; const u8 *ty = dom_attr(node, AT_TYPE, &tl);
            const char *dflt = (ty && css_kw(ty, tl, "reset")) ? "Reset" : (ty && css_kw(ty, tl, "file")) ? "Browse..." : "Submit";
            if (!v) { v = (const u8 *)dflt; n = 0; while (dflt[n]) n++; }
            int cells = (int)txt_cells(v, n);
            rp_push_clip(sx, sy, w, h, sv);
            rp_string(sx + (w - cells * cell) / 2, sy + (h - cell) / 2, v, n, cell, fg, imax(1, w / cell), 0);
            rp_pop_clip(sv);
        } break;
        case CTL_CHECK: case CTL_RADIO: {
            int s = imin(w, h);
            if (it->aux == CTL_RADIO) {                                  /* a round well: white disc, shaded rim */
                int r = s / 2;
                for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
                    int d = dx * dx + dy * dy;
                    if (d > r * r) continue;
                    rp_px(sx + r + dx, sy + r + dy, d >= (r - 1) * (r - 1) ? rp_c((dx + dy < 0) ? 0x808080 : 0xFFFFFF) : rp_c(0xFFFFFF));
                }
                if (rd_ctl_is_checked(node)) { int q = s / 4; for (int dy = -q; dy <= q; dy++) for (int dx = -q; dx <= q; dx++) if (dx * dx + dy * dy <= q * q + 1) rp_px(sx + r + dx, sy + r + dy, fg); }
                break;
            }
            rp_fill(sx, sy, s, s, rp_c(0xFFFFFF));
            rp_fill(sx, sy, s, 1, rp_c(0x808080)); rp_fill(sx, sy, 1, s, rp_c(0x808080));
            rp_fill(sx, sy + s - 1, s, 1, rp_c(0xFFFFFF)); rp_fill(sx + s - 1, sy, 1, s, rp_c(0xFFFFFF));
            rp_fill(sx + 1, sy + 1, s - 2, 1, rp_c(0x404040)); rp_fill(sx + 1, sy + 1, 1, s - 2, rp_c(0x404040));
            if (rd_ctl_is_checked(node)) {
                if (it->aux == CTL_CHECK) {
                    for (int i = 0; i < s / 4 + 1; i++) { rp_px(sx + 3 + i, sy + s / 2 + i, fg); rp_px(sx + 3 + i, sy + s / 2 + i + 1, fg); }
                    for (int i = 0; i < s / 2 - 1; i++) { rp_px(sx + 3 + s / 4 + i, sy + s / 2 + s / 4 - i, fg); rp_px(sx + 3 + s / 4 + i, sy + s / 2 + s / 4 - i + 1, fg); }
                } else { int r = s / 4; for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) if (dx * dx + dy * dy <= r * r + 1) rp_px(sx + s / 2 + dx, sy + s / 2 + dy, fg); }
            }
        } break;
        case CTL_SELECT: {
            u32 opt = rd_select_option(node);
            char buf[96]; int n = opt ? rd_node_text(opt, buf, sizeof(buf)) : 0;
            int aw = cell + 2;
            rp_push_clip(sx, sy, w - aw, h, sv);
            rp_string(sx + 1, sy + (h - cell) / 2, (const u8 *)buf, (u32)n, cell, fg, imax(1, (w - aw) / cell), 0);
            rp_pop_clip(sv);
            rp_bevel(sx + w - aw, sy, aw, h, 0xD4D0C8, 0);
            rp_bullet(sx + w - aw, sy + 1, aw - 1, BUL_DOWN, rp_c(0x000000));
        } break;
    }
}

static void rp_item(const rd_item_t *it, int ox, int oy) {
    int sx = ox + it->x, sy = oy + it->y;
    switch (it->kind) {
        case DL_RECT:
            if (it->aux || it->flags) rp_rbox(sx, sy, it->w, it->h, it->aux, it->color, it->color2, it->flags & (RF_GRAD_V | RF_GRAD_H));
            else rp_fill(sx, sy, it->w, it->h, rp_c(it->color));
            break;
        case DL_RING: rp_ring(sx, sy, it->w, it->h, it->aux, it->cell, it->color); break;
        case DL_TEXT:
            if (it->color2 != CSS_NOCOLOR) rp_fill(sx, sy, it->w, it->h, rp_c(it->color2));
            rp_text_run(sx, sy, RD_POOL + it->off, it->len, it->cell, it->h, it->flags, rp_c(it->color), it->w);
            break;
        case DL_BULLET: rp_bullet(sx, sy, it->cell ? it->cell : it->w, it->aux, rp_c(it->color)); break;
        case DL_IMG: if (it->aux) { rp_picture(sx, sy, it->w, it->h, &img_cache[it->aux - 1].im); break; }
        {
            int sv[4];
            rp_fill(sx, sy, it->w, it->h, rp_c(0xF4F4F4));
            rp_fill(sx, sy, it->w, 1, rp_c(0xB0B0B0)); rp_fill(sx, sy + it->h - 1, it->w, 1, rp_c(0xB0B0B0));
            rp_fill(sx, sy, 1, it->h, rp_c(0xB0B0B0)); rp_fill(sx + it->w - 1, sy, 1, it->h, rp_c(0xB0B0B0));
            rp_push_clip(sx + 1, sy + 1, it->w - 2, it->h - 2, sv);
            if (it->len) rp_string(sx + 3, sy + (it->h - it->cell) / 2, RD_POOL + it->off, it->len, it->cell, rp_c(0x606060), -1, 0);
            else { int m = imin(it->w, it->h); for (int i = 3; i < m - 3; i++) { rp_px(sx + i, sy + i, rp_c(0xC0C0C0)); rp_px(sx + m - 1 - i, sy + i, rp_c(0xC0C0C0)); } }
            rp_pop_clip(sv);
        } break;
        case DL_CTL: rp_control(it, sx, sy); break;
    }
}

/* Paints the document into the screen rectangle (vx,vy,vw,vh), scrolled down by scroll_y pixels. */
static void rd_paint(int vx, int vy, int vw, int vh, int scroll_y) {
    rc_x0 = vx; rc_y0 = vy; rc_x1 = vx + vw; rc_y1 = vy + vh;
    RD_FILLRECT(vx, vy, vw, vh, rp_c(L.canvas_bg));
    int ox = vx, oy = vy - scroll_y;
    int clip_sv[8][4]; int clip_d = 0;                           /* overflow: hidden regions (kept balanced even when an item is culled) */
    for (u32 i = 0; i < L.n; i++) {
        const rd_item_t *it = &RD_ITEMS[i];
        if (it->kind == DL_NONE) continue;
        if (it->kind == DL_CLIP) {
            if (clip_d < 8) { rp_push_clip(ox + it->x, oy + it->y, it->w, it->h, clip_sv[clip_d]); }
            clip_d++; continue;
        }
        if (it->kind == DL_CLIP_END) { if (clip_d > 0) { clip_d--; if (clip_d < 8) rp_pop_clip(clip_sv[clip_d]); } continue; }
        int sy = oy + it->y;
        if (sy >= vy + vh || sy + it->h <= vy) continue;
        int sx = ox + it->x;
        if (sx >= vx + vw || sx + it->w <= vx) continue;
        rp_item(it, ox, oy);
    }
}

/* ============================================================
 * hit testing
 * ============================================================ */
#define HIT_NONE 0
#define HIT_LINK 1
#define HIT_CTL  2
#define HIT_TOGGLE 3
/* (x,y) in document coordinates. Returns what is there and puts the element in *node. */
static int rd_hit(int x, int y, u32 *node) {
    *node = 0;
    for (int i = (int)L.n - 1; i >= 0; i--) {
        const rd_item_t *it = &RD_ITEMS[i];
        if (it->kind == DL_NONE || !it->ref) continue;
        if (x < it->x || x >= it->x + it->w || y < it->y || y >= it->y + it->h) continue;
        *node = it->ref;
        if (it->kind == DL_CTL) return HIT_CTL;
        if (RD_NODES[it->ref].tag == TG_SUMMARY) return HIT_TOGGLE;
        return HIT_LINK;
    }
    return HIT_NONE;
}

/* ============================================================
 * forms
 * ============================================================ */
static inline u32 rd_find_form(u32 node) {
    for (u32 p = RD_NODES[node].parent; p; p = RD_NODES[p].parent) if (RD_NODES[p].tag == TG_FORM) return p;
    return 0;
}

/* True when the page on screen is NOT the whole page: the DOM arena or text pool filled up (parsing stopped), the
 * layout item table filled up (the rest of the page was not laid out), or the stylesheet had rules/matches
 * dropped. None of these overran a buffer, but each used to produce a different-looking page with nothing to say so;
 * the browser now shows a notice. */
static inline int rd_page_incomplete(void) {
    return dom_truncated || L.full || css_overflowed;
}

/* Set whenever the encoder had to DROP anything for lack of room. It used to just stop writing, so a long
 * form value produced a shortened query that was sent as if it were the whole form (and a %XX escape that did
 * not fit was skipped while later plain characters still were written, silently reordering the data).
 * rd_form_query() clears it first; the submit code refuses to send when it is set. */
static int rd_query_overflow = 0;

static inline int rd_urlenc(char *out, int o, int cap, const u8 *s, u32 n) {
    static const char hex[] = "0123456789ABCDEF";
    for (u32 i = 0; i < n; i++) {
        u8 c = s[i];
        if (c == 0x01) c = ' ';
        if (rd_query_overflow) break;                       /* already out of room: write nothing more, keep the data's order honest */
        if (dom_is_alnum(c) || c == '-' || c == '_' || c == '.' || c == '~') { if (o + 1 < cap) out[o++] = (char)c; else rd_query_overflow = 1; }
        else if (c == ' ') { if (o + 1 < cap) out[o++] = '+'; else rd_query_overflow = 1; }
        else if (o + 3 < cap) { out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15]; }
        else rd_query_overflow = 1;
    }
    return o;
}

static inline int rd_query_add(char *out, int o, int cap, const u8 *name, u32 nl, const u8 *val, u32 vl) {
    if (rd_query_overflow) return o;
    if (o > 0) { if (o + 1 < cap) out[o++] = '&'; else { rd_query_overflow = 1; return o; } }
    o = rd_urlenc(out, o, cap, name, nl);
    if (rd_query_overflow) return o;
    if (o + 1 < cap) out[o++] = '='; else { rd_query_overflow = 1; return o; }
    return rd_urlenc(out, o, cap, val, vl);
}

static int rd_query_walk(u32 form, u32 node, u32 submitter, char *out, int o, int cap) {
    for (u32 c = RD_NODES[node].first; c; c = RD_NODES[c].next) {
        rd_node_t *e = &RD_NODES[c];
        if (e->kind != RDK_ELEM) continue;
        u32 nl; const u8 *name = dom_attr(c, AT_NAME, &nl);
        if (name && nl && !dom_has_attr(c, AT_DISABLED)) {
            if (e->tag == TG_INPUT) {
                u32 tl; const u8 *ty = dom_attr(c, AT_TYPE, &tl);
                u32 vl; const u8 *v;
                if (ty && (css_kw(ty, tl, "submit") || css_kw(ty, tl, "button") || css_kw(ty, tl, "image") || css_kw(ty, tl, "reset") || css_kw(ty, tl, "file"))) {
                    if (c == submitter) { v = dom_attr(c, AT_VALUE, &vl); o = rd_query_add(out, o, cap, name, nl, v ? v : (const u8 *)"", v ? vl : 0); }
                } else if (ty && (css_kw(ty, tl, "checkbox") || css_kw(ty, tl, "radio"))) {
                    if (rd_ctl_is_checked(c)) { v = dom_attr(c, AT_VALUE, &vl); o = rd_query_add(out, o, cap, name, nl, v ? v : (const u8 *)"on", v ? vl : 2); }
                } else { v = rd_ctl_text(c, &vl); o = rd_query_add(out, o, cap, name, nl, v, vl); }
            } else if (e->tag == TG_TEXTAREA) {
                u32 vl; const u8 *v = rd_ctl_text(c, &vl); o = rd_query_add(out, o, cap, name, nl, v, vl);
            } else if (e->tag == TG_SELECT) {
                u32 opt = rd_select_option(c);
                if (opt) {
                    u32 vl; const u8 *v = dom_attr(opt, AT_VALUE, &vl);
                    char tb[96]; int tn = 0;
                    if (!v) { tn = rd_node_text(opt, tb, sizeof(tb)); v = (const u8 *)tb; vl = (u32)tn; }
                    o = rd_query_add(out, o, cap, name, nl, v, vl);
                }
            } else if (e->tag == TG_BUTTON && c == submitter) {
                u32 vl; const u8 *v = dom_attr(c, AT_VALUE, &vl);
                o = rd_query_add(out, o, cap, name, nl, v ? v : (const u8 *)"", v ? vl : 0);
            }
        }
        if (e->tag != TG_SELECT && e->tag != TG_TEXTAREA) o = rd_query_walk(form, c, submitter, out, o, cap);
    }
    return o;
}
/* the urlencoded name=value&... string for `form`; `submitter` is the button that was pressed (0 = Enter in a field) */
static inline int rd_form_query(u32 form, u32 submitter, char *out, int cap) {
    rd_query_overflow = 0;
    int o = rd_query_walk(form, form, submitter, out, 0, cap);
    out[o] = 0;
    return o;
}

/* radio groups: ticking one unticks the others that share its name (inside the same form, or the whole page) */
static inline void rd_radio_select(u32 node) {
    u32 nl; const u8 *name = dom_attr(node, AT_NAME, &nl);
    u32 form = rd_find_form(node);
    if (name && nl) {
        for (u32 n = 1; n < dom_node_count; n++) {
            if (RD_NODES[n].tag != TG_INPUT || n == node) continue;
            u32 tl; const u8 *ty = dom_attr(n, AT_TYPE, &tl);
            if (!ty || !css_kw(ty, tl, "radio")) continue;
            u32 l2; const u8 *n2 = dom_attr(n, AT_NAME, &l2);
            if (!n2 || l2 != nl) continue;
            int same = 1; for (u32 k = 0; k < nl; k++) if (n2[k] != name[k]) { same = 0; break; }
            if (same && rd_find_form(n) == form) rd_ctl_set_checked(n, 0);
        }
    }
    rd_ctl_set_checked(node, 1);
}

/* ============================================================
 * loading & layout
 * ============================================================ */
static int rd_laid_w = -1, rd_laid_h = -1;

static inline void rd_after_parse(void) {
    L.pool_mark = dom_pool_len;
    L.n = 0; L.doc_h = 0; L.canvas_bg = 0xFFFFFF;
    rd_state_reset();
    rd_laid_w = -1;
}
static inline void rd_load_html(const u8 *h, u32 len) { img_cache_flush(); dom_parse(h, len); rd_after_parse(); }

/* plain text: one <pre> that wraps */
static inline void rd_load_plain(const u8 *h, u32 len) {
    img_cache_flush();
    dom_reset();
    dom_enter_body();
    u32 af = dom_attr_count;
    rd_attr_t *a = &RD_ATTRS[dom_attr_count];
    static const char sty[] = "white-space:pre-wrap;margin:0";
    a->id = AT_STYLE; a->pad = 0; a->voff = dom_pool_len;
    for (u32 i = 0; sty[i]; i++) dom_pool_putc((u8)sty[i]);
    a->vlen = (u16)(dom_pool_len - a->voff);
    dom_attr_count++;
    dom_insert_elem(TG_PRE, af, 1, 1);
    dom_add_text(h, len, 0);
    rd_after_parse();
}

/* a little status page: a heading and up to two paragraphs (all text is escaped) */
static inline void rd_load_message(const char *title, const char *l1, const char *l2) {
    u8 *buf = RD_SCRATCH;                      /* (nothing else is using the scratch arena between layouts) */
    enum { RD_MSG_MAX = 1024 };
    u32 o = 0;
    #define PUTS(s) do { for (const char *q = (s); q && *q && o < RD_MSG_MAX - 8; q++) { \
        if (*q == '<') { buf[o++] = '&'; buf[o++] = 'l'; buf[o++] = 't'; buf[o++] = ';'; } \
        else if (*q == '&') { buf[o++] = '&'; buf[o++] = 'a'; buf[o++] = 'm'; buf[o++] = 'p'; buf[o++] = ';'; } \
        else buf[o++] = (u8)*q; } } while (0)
    #define PUTRAW(s) do { for (const char *q = (s); *q && o < RD_MSG_MAX - 8; q++) buf[o++] = (u8)*q; } while (0)
    PUTRAW("<h2>"); PUTS(title); PUTRAW("</h2>");
    if (l1 && *l1) { PUTRAW("<p>"); PUTS(l1); PUTRAW("</p>"); }
    if (l2 && *l2) { PUTRAW("<p>"); PUTS(l2); PUTRAW("</p>"); }
    #undef PUTS
    #undef PUTRAW
    rd_load_html(buf, o);
}

/* (Re)lays the page out if the width changed or `force`. Returns the document height in pixels. */
static inline int rd_relayout(int vw, int vh, int force) {
    if (!force && vw == rd_laid_w && vh == rd_laid_h && rd_kern == (u8)(font_kerning != 0)) return L.doc_h;     /* (Settings > Text > Kerning flipped: lay out again) */
    rd_laid_w = vw; rd_laid_h = vh;
    return rd_layout(vw, vh);
}
static inline void rd_invalidate(void) { rd_laid_w = -1; }

#endif
