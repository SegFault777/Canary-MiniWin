/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_UI_STARTMENU_H
#define MW_UI_STARTMENU_H

/* ui/startmenu.h -- the Start menu and its power submenu.
 * (Split out of kernel.c in pre-29; it is still one translation unit, so modules are included by
 * kernel.c in dependency order -- see the module map there.) */

/* ============================================================
 * Start Menu -- pops up above the AM button, Windows-95-style: a dark
 * vertical banner strip down the left side, a short list of items on
 * the right, and a power icon pinned to the bottom behind a divider,
 * which cascades into a small Shut Down / Restart flyout instead of
 * doing anything on its own -- because even a two-app OS deserves a
 * proper "are you sure" ceremony before it turns itself off.
 * ============================================================ */
#define STARTMENU_BANNER_W  18
#define STARTMENU_W         178   /* fits "MiniFiles.mwp" (13 glyphs @ FONT_CELL) plus banner + margin */
#define STARTMENU_H         155
#define STARTMENU_ITEM_H    17
#define STARTMENU_ITEMS     7   /* 0=Notepad.mwp, 1=Setting.mwp, 2=MiniWeb.mwp, 3=MiniShell.mwp, 4=MiniFiles.mwp, 5=Programs >, 6=power */
#define STARTMENU_PROGRAMS_IDX 5
#define STARTMENU_POWER_IDX (STARTMENU_ITEMS - 1)

static inline int start_menu_x(void) { return STARTBTN_X; }
static inline int start_menu_y(void) { return TASKBAR_Y - STARTMENU_H; }

/* Notepad/Setting are grouped near the top; the power item sits glued
 * to the bottom edge behind its own divider, exactly like the real
 * thing -- no matter how few programs you have installed, it never
 * moves. */
static inline int start_menu_item_y(int idx) {
    if (idx == STARTMENU_ITEMS - 1)
        return start_menu_y() + STARTMENU_H - STARTMENU_ITEM_H - 4;
    return start_menu_y() + 6 + idx * STARTMENU_ITEM_H;
}

static int start_menu_item_hit(int px, int py, int idx) {
    int x = start_menu_x() + STARTMENU_BANNER_W + 3;
    int w = STARTMENU_W - STARTMENU_BANNER_W - 6;
    return in_rect(px, py, x, start_menu_item_y(idx), w, STARTMENU_ITEM_H);
}

/* An 11x11 hand-drawn "power" glyph -- a ring with a vertical stroke
 * poking through the gap at the top, same silhouette as the standard
 * IEC power symbol. Drawn the same way font glyphs are (row-by-row
 * bitmask), it just isn't in the text font, so it gets its own
 * function instead of a character code. Scaled up from the original
 * 8x8 version by hand (not auto-generated the way the text font is --
 * this one little glyph didn't justify its own BDF/tool pipeline) to
 * match FONT_CELL so it doesn't look like a shrunken afterthought next
 * to the 11px text sitting right beside it in the Start Menu. */
static const u16 power_icon_bits[11] = {
    0x0C00, 0x1E00, 0x1E00, 0x3300, 0x6180, 0x6180,
    0x6180, 0x3300, 0x1E00, 0x0000, 0x0000
};
static void draw_power_icon(int x, int y, u32 color) {
    for (int row = 0; row < FONT_CELL; row++) {
        u16 bits = power_icon_bits[row];
        for (int col = 0; col < FONT_CELL; col++) {
            if (bits & (0x8000 >> col)) bb_putpixel(x + col, y + row, color);
        }
    }
}

/* Whether the Shut Down / Restart flyout is showing. Only ever true
 * while start_menu_open is also true -- it's a cascade OFF the Start
 * Menu, not a thing that can exist on its own. */
static int power_menu_open = 0;

#define POWERMENU_W       116  /* fits "SHUT DOWN" (9 glyphs @ FONT_CELL) plus margin */
#define POWERMENU_ITEM_H  17
#define POWERMENU_ITEMS   2   /* 0=Shut Down, 1=Restart */
#define POWERMENU_H       (POWERMENU_ITEMS * POWERMENU_ITEM_H + 8)

/* Cascades off the right edge of the Start Menu, biased toward the
 * bottom (flush with the taskbar) rather than centered -- it's growing
 * out of the power item, which is itself pinned to the Start Menu's
 * bottom edge. */
static inline int power_menu_x(void) { return start_menu_x() + STARTMENU_W; }
static inline int power_menu_y(void) { return TASKBAR_Y - POWERMENU_H; }

static inline int power_menu_item_y(int idx) { return power_menu_y() + 3 + idx * POWERMENU_ITEM_H; }

static int power_menu_item_hit(int px, int py, int idx) {
    return in_rect(px, py, power_menu_x() + 2, power_menu_item_y(idx), POWERMENU_W - 4, POWERMENU_ITEM_H);
}

static void draw_power_menu(int mx, int my) {
    int x = power_menu_x(), y = power_menu_y(), w = POWERMENU_W, h = POWERMENU_H;
    ui_panel(x, y, w, h, TH_MENU_SHADOW);

    const char *labels[POWERMENU_ITEMS] = { t(STR_SHUT_DOWN), t(STR_RESTART) };
    for (int i = 0; i < POWERMENU_ITEMS; i++) {
        int iy = power_menu_item_y(i);
        int hover = in_rect(mx, my, x + 2, iy, w - 4, POWERMENU_ITEM_H);
        u32 fg = ui_item(x + 2, iy, w - 4, POWERMENU_ITEM_H, hover);
        ic16_blit(x + 4, iy + 1, i == 0 ? IC_POWER : IC_REFRESH);
        ko_draw_mixed_string(x + 5 + 18, iy + 2, labels[i], fg);
    }
}

/* One 16x16 icon per program row (ui/icon16.h); the power row has its own handling below. */
static const u8 start_menu_icons[6] = { IC_NOTEPAD, IC_SETTING, IC_WEB, IC_TERM, IC_FILES, IC_FOLDER };

/* ================================================================ the Programs flyouts (1.1-alpha-14)
 * "Programs >" opens a flyout that lists /start on the root volume: sub-folders (which open the next flyout to the right, two levels
 * of folders at most) and shortcuts (NAME.LNK files, shown without the extension). A shortcut is a tiny text file -- "app:NOTEPAD"
 * or "prog:HELLO.MWP" -- made by the "Add..." wizard at the bottom of the first flyout (apps/startmenu_add.h), which also creates
 * the folders. The lists are read when a flyout opens, not every frame (the disk is slow). Like the power flyout they open on a click. */
#define SM_FLY_W    138
#define SM_MAXE     14
#define SM_LEVELS   3
typedef struct { char name[34]; u8 dir; } sm_ent_t;
static sm_ent_t (*sm_ent)[SM_MAXE];     /* SM_LEVELS x SM_MAXE entries, on the heap (sys/boot.h boot_heap_init): 1.5 KB that no longer sit in .bss */
static int  sm_cnt[SM_LEVELS];
static char sm_sel[SM_LEVELS][34];       /* the folder opened at level k: the name that leads to level k+1 */
static int  sm_row_y[SM_LEVELS];         /* the y of the row that opened level k */
static int  sm_depth = 0;                /* how many flyouts are showing (0 = none) */
static char sm_target[112];              /* the .LNK a click chose (sm_click() == 2) */

static void sm_path(int lv, char *out, int max) {
    int n = 0; const char *r = "/start";
    while (*r && n < max - 1) out[n++] = *r++;
    for (int i = 0; i < lv; i++) {
        if (n < max - 1) out[n++] = '/';
        for (const char *s = sm_sel[i]; *s && n < max - 1; s++) out[n++] = *s;
    }
    out[n] = 0;
}
static int sm_collect(const mwfs_dirent_t *e, void *ctx) {
    int lv = *(int *)ctx;
    if (sm_cnt[lv] >= SM_MAXE) return 0;
    sm_ent_t *s = &sm_ent[lv][sm_cnt[lv]];
    int n = 0; while (e->name[n] && n < 33) { s->name[n] = e->name[n]; n++; } s->name[n] = 0;
    if (e->type == MWFS_T_DIR) { s->dir = 1; sm_cnt[lv]++; return 1; }
    if (n > 4 && s->name[n - 4] == '.' && (s->name[n - 3] | 32) == 'l' && (s->name[n - 2] | 32) == 'n' && (s->name[n - 1] | 32) == 'k') {
        s->name[n - 4] = 0; s->dir = 0; sm_cnt[lv]++;
    }
    return 1;
}
static void sm_load(int lv) {
    char p[112]; sm_path(lv, p, sizeof p);
    sm_cnt[lv] = 0;
    if (!sm_ent) return;                                             /* no heap: the flyouts stay empty */
    vfs_node_t d;
    if (vfs_stat(p, &d) != MWFS_OK || d.type != MWFS_T_DIR) return;
    vfs_ls_each(&d, sm_collect, &lv);
}
static int sm_rows(int lv) { int n = sm_cnt[lv]; return lv == 0 ? (n ? n : 1) + 1 : (n ? n : 1); }   /* level 0 also has the Add... row */
static int sm_x(int lv)    { return start_menu_x() + STARTMENU_W + lv * SM_FLY_W; }
static int sm_h(int lv)    { return sm_rows(lv) * STARTMENU_ITEM_H + 8; }
static int sm_y(int lv)    { int y = sm_row_y[lv], h = sm_h(lv); if (y + h > TASKBAR_Y) y = TASKBAR_Y - h; return y; }
static int sm_item_y(int lv, int i) { return sm_y(lv) + 4 + i * STARTMENU_ITEM_H; }

static void sm_open(void) { sm_depth = 1; sm_row_y[0] = start_menu_item_y(STARTMENU_PROGRAMS_IDX); sm_load(0); }

/* 0 = the click was not on a flyout; 1 = handled (the menu stays open); 2 = a shortcut was chosen (sm_target); 3 = "Add..." */
static int sm_click(int mx, int my) {
    for (int lv = sm_depth - 1; lv >= 0; lv--) {
        if (!in_rect(mx, my, sm_x(lv), sm_y(lv), SM_FLY_W, sm_h(lv))) continue;
        int row = (my - sm_y(lv) - 4) / STARTMENU_ITEM_H;
        if (my < sm_y(lv) + 4 || row < 0 || row >= sm_rows(lv)) return 1;
        int n = sm_cnt[lv];
        if (lv == 0 && row == (n ? n : 1)) return 3;
        if (row >= n) return 1;                                      /* the "(Empty)" row */
        sm_ent_t *e = &sm_ent[lv][row];
        if (e->dir) {
            if (lv + 1 >= SM_LEVELS) return 1;
            int k = 0; while (e->name[k] && k < 33) { sm_sel[lv][k] = e->name[k]; k++; } sm_sel[lv][k] = 0;
            sm_depth = lv + 2; sm_row_y[lv + 1] = sm_item_y(lv, row); sm_load(lv + 1);
            return 1;
        }
        char p[112]; sm_path(lv, p, sizeof p);
        int q = 0; while (p[q]) q++;
        sm_target[0] = 0; int t = 0;
        for (int i = 0; i < q && t < 100; i++) sm_target[t++] = p[i];
        sm_target[t++] = '/';
        for (int i = 0; e->name[i] && t < 104; i++) sm_target[t++] = e->name[i];
        sm_target[t++] = '.'; sm_target[t++] = 'L'; sm_target[t++] = 'N'; sm_target[t++] = 'K'; sm_target[t] = 0;
        return 2;
    }
    return 0;
}
static void sm_arrow(int x, int y, u32 c) { for (int i = 0; i < 4; i++) ui_vline(x + i, y + i, 7 - 2 * i, c); }
static void sm_draw(int mx, int my) {
    for (int lv = 0; lv < sm_depth; lv++) {
        int x = sm_x(lv), y = sm_y(lv), w = SM_FLY_W, h = sm_h(lv);
        ui_panel(x, y, w, h, TH_MENU_SHADOW);
        int n = sm_cnt[lv], rows = sm_rows(lv);
        for (int i = 0; i < rows; i++) {
            int iy = sm_item_y(lv, i);
            int add = (lv == 0 && i == (n ? n : 1)), empty = (!add && i >= n);
            int open_here = (!add && !empty && sm_ent[lv][i].dir && lv + 1 < sm_depth && sm_ent[lv][i].name[0] && sm_sel[lv][0] == sm_ent[lv][i].name[0]);
            int hover = open_here || in_rect(mx, my, x + 2, iy, w - 4, STARTMENU_ITEM_H);
            u32 fg = ui_item(x + 2, iy, w - 4, STARTMENU_ITEM_H, hover);
            if (add) { ui_hline(x + 4, iy - 2, w - 8, TH_SHADOW); ic16_blit(x + 4, iy + 1, IC_NEXT); ko_draw_mixed_string(x + 24, iy + 2, SKO("Add...", "추가..."), fg); }
            else if (empty) ko_draw_mixed_string(x + 24, iy + 2, SKO("(Empty)", "(비어 있음)"), TH_SHADOW);
            else {
                sm_ent_t *e = &sm_ent[lv][i];
                ic16_blit(x + 4, iy + 1, e->dir ? (open_here ? IC_FOLDER_OPEN : IC_FOLDER) : IC_PROG);
                char s[34]; int k = 0; while (e->name[k] && k < 33) { s[k] = e->name[k]; k++; } s[k] = 0;
                while (k > 0 && ko_string_width(s) > w - 24 - 14) s[--k] = 0;
                ko_draw_mixed_string(x + 24, iy + 2, s, fg);
                if (e->dir) sm_arrow(x + w - 12, iy + 5, fg);
            }
        }
    }
}

static void draw_start_menu(int mx, int my) {
    int x = start_menu_x(), y = start_menu_y();
    int w = STARTMENU_W, h = STARTMENU_H;

    /* same drop-shadow-plus-outline recipe as the File dropdown and the
     * confirm dialog -- three popups, one visual language, zero effort
     * spent reinventing it each time */
    ui_panel(x, y, w, h, TH_MENU_SHADOW);

    /* left banner: solid navy strip with the product name climbing up
     * it bottom-to-top. This is the single most 1995 thing in this
     * entire codebase and we regret nothing. */
    bb_fillrect(x + 1, y + 1, STARTMENU_BANNER_W - 1, h - 2, TH_ACCENT);
    font_draw_string_vertical(x + 3, y + h - 9, "MINIWIN", TH_ACCENT_TEXT);

    const char *labels[6] = {"Notepad.mwp", "Setting.mwp", "MiniWeb.mwp", "MiniShell.mwp", "MiniFiles.mwp", SKO("Programs", "프로그램")};
    int ix = x + STARTMENU_BANNER_W + 2;
    int iw = w - STARTMENU_BANNER_W - 4;
    for (int i = 0; i < STARTMENU_ITEMS; i++) {
        int iy = start_menu_item_y(i);
        /* while the flyout is open, the power row stays highlighted --
         * it's the thing the cascade is coming from, same as a real
         * cascading menu keeps its parent item lit */
        int hover = (i == STARTMENU_POWER_IDX && power_menu_open) || (i == STARTMENU_PROGRAMS_IDX && sm_depth > 0)
                        || in_rect(mx, my, ix, iy, iw, STARTMENU_ITEM_H);
        u32 fg = ui_item(ix, iy, iw, STARTMENU_ITEM_H, hover);
        if (i == STARTMENU_POWER_IDX) {
            if (ic16_ready(IC_POWER)) ic16_blit(ix + 2, iy + 1, IC_POWER);
            else draw_power_icon(ix + 3, iy + 2, fg);          /* a catalog without the pack: the hand-drawn glyph */
        } else {
            ic16_blit(ix + 2, iy + 1, start_menu_icons[i]);
            ko_draw_mixed_string(ix + 3 + 18, iy + 2, labels[i], fg);
            if (i == STARTMENU_PROGRAMS_IDX) sm_arrow(ix + iw - 10, iy + 5, fg);
        }
    }

    /* divider directly above the power item, the traditional "heads up,
     * something drastic is about to happen" line */
    int sep_y = start_menu_item_y(STARTMENU_POWER_IDX) - 2;
    ui_hline(ix, sep_y, iw, TH_SHADOW);

    if (power_menu_open) draw_power_menu(mx, my);
    else if (sm_depth > 0) sm_draw(mx, my);
}

#endif
