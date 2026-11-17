/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_UI_LISTBOX_H
#define MW_UI_LISTBOX_H

/* ui/listbox.h -- a LIST BOX (1.1-alpha-14): a white field of equal-height rows, one selected, with the standard scroll bar (ui/scrollbar.h) on the right
 * when there are more rows than fit. The owner keeps an lbx_t and supplies the rows' CONTENT through a callback, so the same box serves a list of
 * pointers (apps/setting_mouse.h) or anything else:
 *
 *     lbx_t b = { x, y, w, h, row_h, count };            (set x, y, w, h and count each frame; top and sel persist)
 *     lbx_draw(&b, draw_row);                            draw_row(index, x, y, w, h, selected) paints one row inside the clip
 *     lbx_click(&b, mx, my)  -> 1 when the click was in the box (it selects a row, or presses the scroll bar: arrows, track, thumb)
 *     lbx_key(&b, KEY_UP / KEY_DOWN) -> 1 when the selection moved (and the list scrolled to show it)
 *     lbx_wheel(&b, notches) -> 1 when the list scrolled
 *     lbx_detach(&b)         when the page that owns the box goes away (the scroll bar then has nothing to scroll)
 * The bar is the standard one (ui/scrollbar.h), registered under SB_OWNER_LISTBOX for whichever box was drawn or clicked last (lbx_cur, as the
 * drop-down does with dd_cur): so the arrows, the page jumps, DRAGGING THE THUMB (the input loop already polls sb_drag) and the wheel all work.
 * Scrolling does not move the selection, and drawing does not pull the selection back into view -- only the keyboard does.
 * Needs ui/scrollbar.h, ui/widgets.h and the clip functions of vga.h. */

#define SB_OWNER_LISTBOX 13           /* ui/scrollbar.h owner ids: windows use 0..WIN_ID_COUNT-1 (12 now), 13 is this, 14 the drop-downs, 15 MiniFiles' tree */

typedef struct { int x, y, w, h, row_h, count; int top, sel; } lbx_t;
typedef void (*lbx_row_fn)(int index, int x, int y, int w, int h, int selected);

static inline int lbx_rows(const lbx_t *b) { int r = (b->h - 2) / b->row_h; return r < 1 ? 1 : r; }
static inline int lbx_has_bar(const lbx_t *b) { return b->count > lbx_rows(b); }
static inline int lbx_inner_w(const lbx_t *b) { return b->w - 2 - (lbx_has_bar(b) ? SB_W + 1 : 0); }
static void lbx_clamp(lbx_t *b) {                                            /* keeps the selection and the first visible row in range (the selection may be scrolled out of view) */
    int maxtop = b->count - lbx_rows(b); if (maxtop < 0) maxtop = 0;
    if (b->sel < 0) b->sel = 0;
    if (b->count > 0 && b->sel >= b->count) b->sel = b->count - 1;
    if (b->top > maxtop) b->top = maxtop;
    if (b->top < 0) b->top = 0;
}
static void lbx_show_sel(lbx_t *b) {                                         /* scrolls so that the selection is in view */
    int rows = lbx_rows(b);
    lbx_clamp(b);
    if (b->sel < b->top) b->top = b->sel;
    if (b->sel >= b->top + rows) b->top = b->sel - rows + 1;
    lbx_clamp(b);
}

/* ---- the scroll bar's side of it: ui/scrollbar.h asks for the geometry and hands back the position ---- */
static lbx_t *lbx_cur = 0;
static int lbx_sb_geom(int *x, int *y, int *h, int *total, int *page, int *pos) {
    lbx_t *b = lbx_cur;
    if (!b || !lbx_has_bar(b)) return 0;
    *x = b->x + b->w - SB_W - 1; *y = b->y + 1; *h = b->h - 2;
    *total = b->count; *page = lbx_rows(b); *pos = b->top;
    return 1;
}
static void lbx_sb_set(int pos) { if (lbx_cur) lbx_cur->top = sb_clamp(pos, lbx_cur->count, lbx_rows(lbx_cur)); }
static void lbx_bind(lbx_t *b) { lbx_cur = b; sb_register(SB_OWNER_LISTBOX, lbx_sb_geom, lbx_sb_set, 1); }
static void lbx_detach(lbx_t *b) { if (lbx_cur == b) { lbx_cur = 0; if (sb_dragging()) sb_release(); } }

static void lbx_draw(lbx_t *b, lbx_row_fn row) {
    lbx_clamp(b); lbx_bind(b);
    ui_field(b->x, b->y, b->w, b->h, TH_FIELD, 0);
    int rows = lbx_rows(b), iw = lbx_inner_w(b);
    bb_clip_set(b->x + 1, b->y + 1, iw, b->h - 2);
    for (int r = 0; r < rows; r++) {
        int idx = b->top + r; if (idx >= b->count) break;
        int ry = b->y + 1 + r * b->row_h;
        if (idx == b->sel) bb_fillrect(b->x + 1, ry, iw, b->row_h, TH_ACCENT);
        row(idx, b->x + 1, ry, iw, b->row_h, idx == b->sel);
    }
    bb_clip_off();
    if (lbx_has_bar(b)) sb_draw_at(b->x + b->w - SB_W - 1, b->y + 1, b->h - 2, b->count, rows, b->top);
}
static int lbx_click(lbx_t *b, int mx, int my) {
    if (!in_rect(mx, my, b->x, b->y, b->w, b->h)) return 0;
    lbx_clamp(b); lbx_bind(b);
    if (mx < b->x + 1 + lbx_inner_w(b)) {                                     /* a row */
        int idx = b->top + (my - b->y - 1) / b->row_h;
        if (idx >= 0 && idx < b->count) b->sel = idx;
    } else if (lbx_has_bar(b)) sb_press(SB_OWNER_LISTBOX, mx, my);            /* the scroll bar: the arrows, the track, the thumb (dragged by the input loop) */
    return 1;
}
static int lbx_key(lbx_t *b, int k) {
    if (k == KEY_UP && b->sel > 0) b->sel--; else if (k == KEY_DOWN && b->sel < b->count - 1) b->sel++; else return 0;
    lbx_show_sel(b);
    return 1;
}
/* The wheel: notches > 0 scroll down. 1 = the list scrolled (it had something to scroll). */
static int lbx_wheel(lbx_t *b, int notches) {
    if (!lbx_has_bar(b)) return 0;
    lbx_bind(b);
    return sb_wheel(SB_OWNER_LISTBOX, notches);
}

#endif
