/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_UI_SHEET_H
#define MW_UI_SHEET_H

/* ui/sheet.h -- the frame of a PROPERTY SHEET (1.1-alpha-14): a row of tabs, the page under them, and the OK / Cancel / Apply row at the bottom, laid out
 * inside a window's client area the same way for every sheet of the Control Panel (Date/Time, Mouse ...). A sheet keeps its own state and draws what is
 * INSIDE the page; this file does the part they all share:
 *
 *     sheet_t g; sheet_geo(&g, win_x, body_y, win_w, status_y, MIN_W, MIN_PAGE_H);   -- the rectangles; g.ok = 0 when the window is too small
 *     sheet_draw_frame(&g, label_fn, n_tabs, active);                                 -- the page border and the tabs (the active one raised)
 *     sheet_tab_at(&g, label_fn, n_tabs, mx, my)                                      -- the tab under a click, or -1
 *     sheet_footer_draw(&g, pressed, apply_enabled) / sheet_footer_hit(&g, mx, my, apply_enabled)   -- OK (1), Cancel (2), Apply (3)
 *     sheet_hint_draw / sheet_hint_hit                                                -- what is shown instead when the window is too small
 *
 * Buttons fire on RELEASE over the button that was pressed (the sheet remembers which in its own variable and calls sheet_footer_hit() again).
 * Also here: sheet_icon32(), the 32 x 32 icons of the icon catalog looked up BY NAME (and the question mark when the pack lacks one), cached.
 * Needs ui/controls.h (ui_pushbutton) and the icon catalog (ui/icons.h). */

typedef struct { int X, Y, W, H; int px, py, pw, ph; int ok; } sheet_t;
typedef const char *(*sheet_label_fn)(int i);

static void sheet_geo(sheet_t *g, int win_x, int body_y, int win_w, int status_y, int min_w, int min_page_h) {
    g->X = win_x + 6; g->Y = body_y + 4;
    g->W = win_w - 12; g->H = status_y - 4 - g->Y;
    g->px = g->X; g->py = g->Y + 19; g->pw = g->W; g->ph = g->H - 19 - 30;
    g->ok = g->W >= min_w && g->ph >= min_page_h;
}
static int sheet_tab_w(sheet_label_fn lab, int i) { return ko_string_width(lab(i)) + 18; }
static int sheet_tab_x(const sheet_t *g, sheet_label_fn lab, int i) { int x = g->X + 2; for (int k = 0; k < i; k++) x += sheet_tab_w(lab, k); return x; }

/* The page's border, then the tabs on top of it (the active one is taller and opens the page's top edge). */
static void sheet_draw_frame(const sheet_t *g, sheet_label_fn lab, int n, int active) {
    ui_bevel(g->px, g->py, g->pw, g->ph, 0);
    for (int i = 0; i < n; i++) {
        int x = sheet_tab_x(g, lab, i), w = sheet_tab_w(lab, i), act = (i == active);
        int y = g->Y + (act ? 0 : 2), h = (act ? 21 : 19);
        bb_fillrect(x, y, w, h, TH_FACE);
        ui_hline(x + 1, y, w - 2, TH_LIGHT); ui_vline(x, y + 1, h - 1, TH_LIGHT);
        ui_vline(x + w - 1, y + 1, h - 1, TH_SHADOW);
        if (act) ui_hline(x + 1, g->py, w - 2, TH_FACE);
        ko_draw_mixed_string(x + 9, y + 5, lab(i), TH_TEXT);
    }
}
static int sheet_tab_at(const sheet_t *g, sheet_label_fn lab, int n, int mx, int my) {
    for (int i = 0; i < n; i++) if (in_rect(mx, my, sheet_tab_x(g, lab, i), g->Y, sheet_tab_w(lab, i), 21)) return i;
    return -1;
}

/* OK / Cancel / Apply, bottom right. `pressed` = the button the mouse is held down on (1, 2, 3) or 0. */
static void sheet_footer_draw(const sheet_t *g, int pressed, int apply_enabled) {
    int by = g->Y + g->H - 26;
    ui_pushbutton(g->X + g->W - 222, by, 70, 22, "OK", pressed == 1, 1);
    ui_pushbutton(g->X + g->W - 148, by, 70, 22, SKO("Cancel", "취소"), pressed == 2, 1);
    ui_pushbutton(g->X + g->W - 74, by, 70, 22, SKO("Apply", "적용"), pressed == 3, apply_enabled);
}
static int sheet_footer_hit(const sheet_t *g, int mx, int my, int apply_enabled) {
    int by = g->Y + g->H - 26;
    if (in_rect(mx, my, g->X + g->W - 222, by, 70, 22)) return 1;
    if (in_rect(mx, my, g->X + g->W - 148, by, 70, 22)) return 2;
    if (in_rect(mx, my, g->X + g->W - 74, by, 70, 22)) return apply_enabled ? 3 : 0;
    return 0;
}
/* A window too small for the sheet: a sentence and a Cancel button (so it can still be left). Cancel is button 2. */
static void sheet_hint_draw(const sheet_t *g, int win_x, int body_y, int pressed) {
    ko_draw_mixed_string(win_x + 12, body_y + 12, SKO("Make the window bigger.", "창을 더 크게 하세요."), TH_TEXT);
    ui_pushbutton(g->X + 6, g->Y + 30, 70, 22, SKO("Cancel", "취소"), pressed == 2, 1);
}
static int sheet_hint_hit(const sheet_t *g, int mx, int my) { return in_rect(mx, my, g->X + 6, g->Y + 30, 70, 22) ? 2 : 0; }

/* ---------------------------------------------------------------- 32 x 32 icons by name */
/* The icon `name` of the icon catalog as 32 x 32 ARGB pixels (cached), or, when this disk's pack does not have it, the question mark -- so a missing icon is
 * visible and asked for, never a hole. Returns 0 only when there is no memory or no question mark either. Up to 8 different names. */
#define SHEET_ICON_CACHE 16
static struct { char name[20]; u32 *px; } sheet_icon_cache[SHEET_ICON_CACHE];
static const u32 *sheet_icon32(const char *name) {
    int free_i = -1;
    for (int i = 0; i < SHEET_ICON_CACHE; i++) {
        if (!sheet_icon_cache[i].px) { if (free_i < 0) free_i = i; continue; }
        const char *a = sheet_icon_cache[i].name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (*a == *b) return sheet_icon_cache[i].px;
    }
    if (free_i < 0) return 0;
    u32 *px = (u32 *)kzalloc(1024 * 4);
    if (!px) return 0;
    int slot = icon_find_by_name(name);
    if (slot < 0) slot = icon_find_by_name("HELP");
    u8 raw[4096];
    if (slot >= 0 && icon_load_slot(slot, 1, raw))
        for (int k = 0; k < 1024; k++) px[k] = ((u32)raw[k * 4 + 3] << 24) | ((u32)raw[k * 4] << 16) | ((u32)raw[k * 4 + 1] << 8) | raw[k * 4 + 2];
    int n = 0; while (name[n] && n < 19) { sheet_icon_cache[free_i].name[n] = name[n]; n++; }
    sheet_icon_cache[free_i].name[n] = 0; sheet_icon_cache[free_i].px = px;
    return px;
}

#endif
