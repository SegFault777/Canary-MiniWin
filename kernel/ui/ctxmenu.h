/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_UI_CTXMENU_H
#define MW_UI_CTXMENU_H

/* ui/ctxmenu.h -- the right-click ("context") menu: one popup anywhere on the desktop, with an optional one-level submenu (New >, View >).
 * This file is the machinery -- the state, drawing and hit-testing. WHAT a menu holds and what its items do is ui/ctxmenu_actions.h.
 * Items use the same flags as the menu bar (ui/menubar.h): MBF_SEP (a separator above), MBF_BOLD (the default action), MBF_OFF (greyed:
 * not built yet, does nothing and the menu stays open), plus PMF_SUB (an arrow: opens the item's `sub` list while the pointer is on it).
 * Windows 95 look: raised face, one-pixel outline, accent highlight, the help line of the item under the pointer in the status bar. */

#define PMF_SUB     8
#define PM_ITEM_H   15
#define PM_SEP_H    6
#define PM_MAX      14

typedef struct pm_item { const char *en, *ko; int fl; int id; const char *help; const struct pm_item *sub; int sub_n; } pm_item_t;

static pm_item_t pm_it[PM_MAX];          /* the open menu (copied from a const table so items can be greyed per click) */
static int pm_n = 0, pm_open = 0, pm_x = 0, pm_y = 0;
static int pm_kind = 0, pm_arg = 0;      /* what it was opened on (ui/ctxmenu_actions.h) */
static int pm_prog_owner = 0;            /* for a program's own menu (menu_popup): which program window (0 .. PW_MAX-1) is waiting for the answer */
static int pm_subi = -1;                 /* the item whose submenu is showing */
static int pm_prog_result = 0;           /* the entry a program's pop-up menu (menu_popup, ABI version 4) was answered with: 1-based, 0 = dismissed */
static const char *pm_status_msg = 0;    /* an action's sentence for the desktop status line, picked up by the input loop */

static const char *pm_str(const pm_item_t *it) { return (sys_language == LANG_KOREAN && it->ko) ? it->ko : it->en; }
static int pm_list_w(const pm_item_t *it, int n) {
    int w = 96;
    for (int i = 0; i < n; i++) { int iw = ko_string_width(pm_str(&it[i])) + 12 + 16 + ((it[i].fl & MBF_BOLD) ? 1 : 0); if (iw > w) w = iw; }
    return w;
}
static int pm_row_y(const pm_item_t *it, int i, int y) {       /* top of item i's row; a separator's gap is above it */
    int t = y + 1;
    for (int k = 0; k <= i; k++) { if (it[k].fl & MBF_SEP) t += PM_SEP_H; if (k < i) t += PM_ITEM_H; }
    return t;
}
static int pm_list_h(const pm_item_t *it, int n) { return pm_row_y(it, n - 1, 0) + PM_ITEM_H + 1; }

/* Opens the menu from a const table at (px,py): the copy lands in pm_it, the position is pulled back inside the screen. */
static void pm_begin(const pm_item_t *src, int n, int px, int py, int kind, int arg) {
    if (n > PM_MAX) n = PM_MAX;
    for (int i = 0; i < n; i++) pm_it[i] = src[i];
    pm_n = n; pm_kind = kind; pm_arg = arg; pm_subi = -1; pm_open = 1;
    int w = pm_list_w(pm_it, n), h = pm_list_h(pm_it, n);
    pm_x = px; pm_y = py;
    if (pm_x + w + 2 > VGA_WIDTH) pm_x = VGA_WIDTH - w - 2;
    if (pm_y + h + 2 > TASKBAR_Y) pm_y = py - h;                 /* no room below: open upwards, like Windows */
    if (pm_y < 0) pm_y = 0;
    if (pm_x < 0) pm_x = 0;
}
static void pm_close(void) { pm_open = 0; pm_subi = -1; }
static void pm_set_off(int i, int off) { if (i >= 0 && i < pm_n) { if (off) pm_it[i].fl |= MBF_OFF; else pm_it[i].fl &= ~MBF_OFF; } }

/* Where the submenu of item `i` is drawn: to the right of the menu, or to its left when there is no room. */
static void pm_sub_geo(int i, int *sx, int *sy, int *sw, int *sh) {
    const pm_item_t *s = pm_it[i].sub; int n = pm_it[i].sub_n;
    *sw = pm_list_w(s, n); *sh = pm_list_h(s, n);
    int mw = pm_list_w(pm_it, pm_n);
    *sx = pm_x + mw - 3; if (*sx + *sw + 2 > VGA_WIDTH) *sx = pm_x - *sw + 3;
    *sy = pm_row_y(pm_it, i, pm_y) - 1; if (*sy + *sh + 2 > TASKBAR_Y) *sy = TASKBAR_Y - *sh - 2;
    if (*sy < 0) *sy = 0;
}

/* The submenu follows the pointer: it opens on an item with an arrow and closes when the pointer moves onto an item without one. */
static void pm_update(void) {
    if (!pm_open) return;
    if (pm_kind == 6 /* PM_PROGRAM */ && !win_is_open(WIN_ID_PROG + pm_prog_owner)) { pm_close(); return; }      /* its program's window went away */
    int w = pm_list_w(pm_it, pm_n);
    for (int i = 0; i < pm_n; i++)
        if (in_rect(mx, my, pm_x + 1, pm_row_y(pm_it, i, pm_y), w - 2, PM_ITEM_H)) {
            pm_subi = ((pm_it[i].fl & PMF_SUB) && !(pm_it[i].fl & MBF_OFF)) ? i : -1;
            return;
        }
}

/* The item under (px,py), main list or submenu; 0 = none. *inside tells whether the point is on either popup at all. */
static const pm_item_t *pm_pick(int px, int py, int *inside) {
    *inside = 0;
    if (!pm_open) return 0;
    int w = pm_list_w(pm_it, pm_n), h = pm_list_h(pm_it, pm_n);
    if (pm_subi >= 0) {
        int sx, sy, sw, sh; pm_sub_geo(pm_subi, &sx, &sy, &sw, &sh);
        if (in_rect(px, py, sx, sy, sw, sh)) {
            *inside = 1;
            const pm_item_t *s = pm_it[pm_subi].sub;
            for (int i = 0; i < pm_it[pm_subi].sub_n; i++)
                if (in_rect(px, py, sx + 1, pm_row_y(s, i, sy), sw - 2, PM_ITEM_H)) return &s[i];
            return 0;
        }
    }
    if (in_rect(px, py, pm_x, pm_y, w, h)) {
        *inside = 1;
        for (int i = 0; i < pm_n; i++)
            if (in_rect(px, py, pm_x + 1, pm_row_y(pm_it, i, pm_y), w - 2, PM_ITEM_H)) return &pm_it[i];
    }
    return 0;
}

static const char *pm_help(void) {
    if (!pm_open) return 0;
    int inside; const pm_item_t *it = pm_pick(mx, my, &inside);
    return it ? it->help : 0;
}

static void pm_draw_list(int x, int y, const pm_item_t *it, int n) {
    int w = pm_list_w(it, n), h = pm_list_h(it, n);
    bb_fillrect(x + 2, y + 2, w, h, TH_SHADOW);                              /* a small drop shadow, as menus have in Windows 95 */
    bb_fillrect(x, y, w, h, TH_FACE);
    bb_rect(x, y, w, h, TH_OUTLINE);
    for (int i = 0; i < n; i++) {
        int iy = pm_row_y(it, i, y), fl = it[i].fl;
        if (fl & MBF_SEP) { ui_hline(x + 2, iy - PM_SEP_H / 2 - 1, w - 4, TH_SHADOW); ui_hline(x + 2, iy - PM_SEP_H / 2, w - 4, TH_LIGHT); }
        const char *label = pm_str(&it[i]);
        u32 fg;
        if (fl & MBF_OFF) {
            ko_draw_mixed_string(x + 9, iy + 3, label, TH_LIGHT);            /* greyed: embossed label, no highlight */
            ko_draw_mixed_string(x + 8, iy + 2, label, TH_SHADOW);
            fg = TH_SHADOW;
        } else {
            int hover = in_rect(mx, my, x + 1, iy, w - 2, PM_ITEM_H) || (x == pm_x && y == pm_y && pm_subi == i);
            fg = ui_item(x + 1, iy, w - 2, PM_ITEM_H, hover);
            ko_draw_mixed_string(x + 8, iy + 2, label, fg);
            if (fl & MBF_BOLD) ko_draw_mixed_string(x + 9, iy + 2, label, fg);
        }
        if (fl & PMF_SUB) {                                                  /* the little arrow: a 4-wide triangle pointing right */
            int ax = x + w - 10, ay = iy + PM_ITEM_H / 2 - 3;
            for (int k = 0; k < 4; k++) ui_vline(ax + k, ay + k, 7 - 2 * k, fg);
        }
    }
}

static void draw_popup_menu(void) {
    if (!pm_open) return;
    pm_update();
    pm_draw_list(pm_x, pm_y, pm_it, pm_n);
    if (pm_subi >= 0) { int sx, sy, sw, sh; pm_sub_geo(pm_subi, &sx, &sy, &sw, &sh); pm_draw_list(sx, sy, pm_it[pm_subi].sub, pm_it[pm_subi].sub_n); }
}

static void pm_run(int kind, int arg, int id);      /* ui/ctxmenu_actions.h */

/* A left click while the menu is open. Always consumed. An item runs and closes the menu; a greyed item or one with a submenu leaves it open;
 * a click anywhere else just closes it. */
static void pm_click(int px, int py) {
    int inside; const pm_item_t *it = pm_pick(px, py, &inside);
    if (it) {
        if (it->fl & (MBF_OFF | PMF_SUB)) return;
        int kind = pm_kind, arg = pm_arg, id = it->id;
        pm_close();
        pm_run(kind, arg, id);
        return;
    }
    if (!inside) pm_close();
}

#endif
