/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_UI_CONTROLS_H
#define MW_UI_CONTROLS_H

/* ui/controls.h -- reusable dialog controls (1.1-alpha-14): the DROP-DOWN LIST (combo box), the CHECK BOX, and the small things every property sheet is made of:
 * ui_group (a titled group box), ui_pushbutton (a push button that can be pressed or disabled), ui_radio, ui_slider (a trackbar with tick marks) +
 * ui_slider_pos (the value under the pointer), ui_label (a text that can be greyed out, embossed like Windows' disabled text) and ui_sunken (a sunken panel).
 * The Date/Time and Mouse sheets (apps/setting_dt.h, apps/setting_mouse.h) are built from these and from ui/sheet.h; nothing here is specific to them.
 *
 * A drop-down is a dd_t: the closed box (a sunken field with the chosen entry and an arrow button) and, while it is open, a list under it.
 * The list's scroll bar is NOT drawn here: it is the same bar every window uses (ui/scrollbar.h -- sb_draw_at, sb_press, sb_drag, sb_wheel), registered
 * under SB_OWNER_DROPDOWN, so it looks and behaves exactly like the others (arrows, page jumps, a draggable thumb, the wheel). Only one list
 * is open at a time; dd_cur names it for the scroll bar's callbacks.
 *
 * Use: keep a dd_t; every frame dd_place() it (x, y, w) and dd_draw() it -- and dd_draw_list() LAST, so the open list covers what is under it;
 * dd_click() on a left press, dd_wheel() on the wheel. dd_click() returns 1 when it used the click and sets d->changed when the choice changed. */

#define SB_OWNER_DROPDOWN 14          /* ui/scrollbar.h owner ids: windows use 0..WIN_ID_COUNT-1, 15 is MiniFiles' tree, 16+ are the horizontal bars */
#define DD_ROW   14                   /* the height of one row of the list */
#define DD_BOX_H 18                   /* the height of the closed box */

typedef struct {
    int x, y, w;                      /* the closed box: left, top, width (height is DD_BOX_H) */
    int n, sel, top, rows;            /* entries, the chosen one, the first visible one, rows to show when open */
    const char *(*text)(int i);       /* the label of entry i */
    int open, changed;
} dd_t;

static dd_t *dd_cur = 0;

static inline int dd_rows_shown(const dd_t *d) { return d->n < d->rows ? d->n : d->rows; }
static inline int dd_list_h(const dd_t *d) { return dd_rows_shown(d) * DD_ROW + 2; }
static inline int dd_has_bar(const dd_t *d) { return d->n > d->rows; }

/* The scroll bar's geometry, for ui/scrollbar.h: the right edge of the list, inside its frame. */
static int dd_sb_geom(int *x, int *y, int *h, int *total, int *page, int *pos) {
    dd_t *d = dd_cur;
    if (!d || !d->open || !dd_has_bar(d)) return 0;
    *x = d->x + d->w - 1 - SB_W; *y = d->y + DD_BOX_H + 1; *h = dd_list_h(d) - 2;
    *total = d->n; *page = d->rows; *pos = d->top;
    return 1;
}
static void dd_sb_set(int pos) { if (dd_cur) dd_cur->top = sb_clamp(pos, dd_cur->n, dd_cur->rows); }

static void dd_init(dd_t *d, int n, const char *(*text)(int), int rows) {
    d->n = n; d->text = text; d->rows = rows; d->sel = 0; d->top = 0; d->open = 0; d->changed = 0;
    sb_register(SB_OWNER_DROPDOWN, dd_sb_geom, dd_sb_set, 1);
}
static void dd_place(dd_t *d, int x, int y, int w) { d->x = x; d->y = y; d->w = w; }
static void dd_close(dd_t *d) { d->open = 0; if (dd_cur == d) dd_cur = 0; }
static void dd_open(dd_t *d) {
    if (dd_cur && dd_cur != d) dd_cur->open = 0;
    d->open = 1; dd_cur = d;
    d->top = sb_clamp(d->sel - d->rows / 2, d->n, d->rows);            /* with the chosen entry in view, about the middle */
}

/* The closed box. */
static void dd_draw(const dd_t *d) {
    ui_field(d->x, d->y, d->w, DD_BOX_H, TH_FIELD, 0);
    bb_clip_set(d->x + 2, d->y + 2, d->w - 6 - SB_W, DD_BOX_H - 4);
    ko_draw_mixed_string(d->x + 4, d->y + 3, d->text(d->sel), TH_FIELD_TEXT);
    bb_clip_off();
    int bx = d->x + d->w - 2 - SB_W;
    ui_bevel(bx, d->y + 2, SB_W, DD_BOX_H - 4, d->open);                 /* the arrow button, as wide as a scroll bar */
    int o = d->open ? 1 : 0;
    for (int k = 0; k < 4; k++) ui_hline(bx + 2 + k + o, d->y + 6 + k + o, 8 - 2 * k, TH_TEXT);   /* the down arrow */
}
/* The open list (draw it after everything it covers). */
static void dd_draw_list(const dd_t *d) {
    if (!d->open) return;
    int ly = d->y + DD_BOX_H, lh = dd_list_h(d), bar = dd_has_bar(d), tw = d->w - 2 - (bar ? SB_W : 0);
    ui_field(d->x, ly, d->w, lh, TH_FIELD, 0);
    for (int r = 0; r < dd_rows_shown(d); r++) {
        int i = d->top + r; if (i >= d->n) break;
        int y = ly + 1 + r * DD_ROW, hot = in_rect(mx, my, d->x + 1, y, tw, DD_ROW) || i == d->sel;
        if (hot) bb_fillrect(d->x + 1, y, tw, DD_ROW, TH_ACCENT);
        bb_clip_set(d->x + 1, y, tw, DD_ROW);
        ko_draw_mixed_string(d->x + 3, y + 1, d->text(i), hot ? TH_TEXT_INVERSE : TH_FIELD_TEXT);
        bb_clip_off();
    }
    if (bar) sb_draw_at(d->x + d->w - 1 - SB_W, ly + 1, lh - 2, d->n, d->rows, d->top);   /* the standard scroll bar */
}

/* A left press. 1 = used (opened or closed the list, picked an entry, or pressed the scroll bar); d->changed is set when the choice changed. */
static int dd_click(dd_t *d, int px, int py) {
    d->changed = 0;
    if (d->open) {
        if (dd_has_bar(d) && sb_press(SB_OWNER_DROPDOWN, px, py)) return 1;
        int ly = d->y + DD_BOX_H, tw = d->w - 2 - (dd_has_bar(d) ? SB_W : 0);
        if (in_rect(px, py, d->x + 1, ly + 1, tw, dd_list_h(d) - 2)) {
            int i = d->top + (py - ly - 1) / DD_ROW;
            if (i < d->n) { if (i != d->sel) d->changed = 1; d->sel = i; }
            dd_close(d);
            return 1;
        }
        int on_box = in_rect(px, py, d->x, d->y, d->w, DD_BOX_H);
        dd_close(d);                                                    /* a click anywhere else only closes it (and a click on the box itself too) */
        return on_box || 1;
    }
    if (in_rect(px, py, d->x, d->y, d->w, DD_BOX_H)) { dd_open(d); return 1; }
    return 0;
}
/* The wheel while the list is open scrolls it; 1 = used. */
static int dd_wheel(dd_t *d, int notches) { return d->open && dd_has_bar(d) && sb_wheel(SB_OWNER_DROPDOWN, notches); }

/* ---- a plain line (Bresenham) and the check box ---- */
static void ui_line(int x0, int y0, int x1, int y1, u32 c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (int n = 0; n < 400; n++) {
        bb_putpixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
/* A 13x13 check box at (x, y). */
static void ui_checkbox(int x, int y, int on) {
    bb_fillrect(x, y, 13, 13, TH_FIELD); bb_rect(x, y, 13, 13, TH_SHADOW);
    if (on) { ui_line(x + 3, y + 6, x + 5, y + 9, TH_TEXT); ui_line(x + 5, y + 9, x + 10, y + 3, TH_TEXT); }
}


/* ---------------------------------------------------------------- the small widgets of a property sheet */
/* A text that may be disabled: greyed, with the light "emboss" shadow one pixel down and right, as Windows draws it. */
static void ui_label(int x, int y, const char *s, int enabled) {
    if (!enabled) ko_draw_mixed_string(x + 1, y + 1, s, TH_LIGHT);
    ko_draw_mixed_string(x, y, s, enabled ? TH_TEXT : TH_SHADOW);
}
/* A group box: an etched frame with its title sitting in a gap of the top edge. (x, y, w, h) is the frame. */
static void ui_group(int x, int y, int w, int h, const char *label) {
    bb_rect(x + 1, y + 1, w, h, TH_LIGHT);
    bb_rect(x, y, w, h, TH_SHADOW);
    int lw = ko_string_width(label) + 6;
    bb_fillrect(x + 6, y - 1, lw, 3, TH_FACE);
    ko_draw_mixed_string(x + 9, y - 6, label, TH_TEXT);
}
/* A push button. Held down (pressed): the bevel sinks and the label moves one pixel. Disabled: the label is greyed and it never looks pressed. */
static void ui_pushbutton(int x, int y, int w, int h, const char *label, int pressed, int enabled) {
    int p = enabled && pressed;
    ui_bevel(x, y, w, h, p);
    ui_label(x + (w - ko_string_width(label)) / 2 + p, y + (h - FONT_CELL) / 2 + p, label, enabled);
}
/* A sunken panel filled with the dialog colour (the preview box of a pointer, a test area). */
static void ui_sunken(int x, int y, int w, int h) { bb_fillrect(x, y, w, h, TH_FACE); ui_bevel(x, y, w, h, 1); }
/* A check box with its label to the right (the box is ui_checkbox() above). */
static void ui_checkbox_label(int x, int y, const char *label, int checked, int enabled) {
    ui_checkbox(x, y, checked);
    ui_label(x + 19, y + (13 - FONT_CELL) / 2, label, enabled);
}
/* A radio button, 12 x 12 at (x, y): a sunken round field, a dot when on. Round: the distance from the centre in half pixels picks the ring. */
static void ui_radio(int x, int y, int on) {
    for (int j = 0; j < 12; j++) for (int i = 0; i < 12; i++) {
        int a = 2 * i - 11, b = 2 * j - 11, d = a * a + b * b;
        if (d > 144) continue;
        u32 c;
        if (d > 100) c = (a + b < 0) ? TH_SHADOW : TH_LIGHT;             /* outer ring: dark top-left, light bottom-right */
        else if (d > 64) c = (a + b < 0) ? TH_TEXT : TH_FACE;            /* inner ring */
        else c = TH_FIELD;
        if (on && d <= 20) c = TH_TEXT;                                  /* the dot */
        bb_putpixel(x + i, y + j, c);
    }
}
/* A trackbar: the groove, tick marks under it for the values 0 .. max, and the thumb. It is 26 pixels high (hit-test (x, y - 2, w, 28)). */
#define UI_SLIDER_H 26
static void ui_slider(int x, int y, int w, int pos, int max, int enabled) {
    ui_hline(x, y + 9, w, TH_SHADOW); ui_hline(x, y + 10, w, TH_TEXT);
    ui_hline(x, y + 11, w, TH_LIGHT); ui_hline(x, y + 12, w, TH_FACE);
    for (int i = 0; i <= max; i++) ui_vline(x + 4 + (w - 9) * i / max, y + 22, 3, enabled ? TH_TEXT : TH_SHADOW);
    int tx = x + (w - 9) * pos / max;
    ui_bevel(tx, y, 9, 20, 0);
    if (!enabled) ui_vline(tx + 4, y + 3, 14, TH_SHADOW);
}
/* The value 0 .. max the pointer at mx means on a slider drawn at (x, w). */
static int ui_slider_pos(int x, int w, int max, int mx) {
    int span = w - 9; if (span < 1) span = 1;
    int p = ((mx - x - 4) * max + span / 2) / span;
    return p < 0 ? 0 : p > max ? max : p;
}

#endif
