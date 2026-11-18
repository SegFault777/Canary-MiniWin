/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_APPS_SETTING_FONT_H
#define MW_APPS_SETTING_FONT_H

/* apps/setting_font.h -- Control Panel > Font (1.1-alpha-14): a Fonts folder in the manner of Windows 95.
 *
 * The installed faces (Galmuri 7, 9, 11, 11 Bold, 14) are shown as large icons with their names; the one in use says "System font". Click selects, a
 * double click (or File > Open, or Enter) opens a PREVIEW of the face -- its name, its size and some lines of sample text drawn in the face itself --
 * and "Use as System Font" (a button, and File > Use as System Font) makes it the font of the whole desktop at once (kernel/fontface.h). The choice is
 * kept in the boot sector, next to the screen size (setting_display.h), so it survives a restart; the live disc cannot keep it. The 7-pixel face asks
 * "use it anyway?" first: it is very small. Kerning, the other thing this page always did, is a check box.
 * On a 320 x 200 screen (a small page) the icons are replaced by a plain list. On the floppy image there is only one face: the page then shows it and
 * the button stays grey.
 * Built from ui/sheet.h (geometry), ui/controls.h (buttons, check box) and ui/listbox.h. */

#define FNT_ITEM_W 100
#define FNT_ITEM_H (42 + 2 * FONT_LINE_H)
#define FNT_BOTTOM 46                                  /* the strip under the list: a message line and the buttons */

static int fnt_sel = 0;                                /* the selected face: an index into fontface_order[] */
static int fnt_last_sel = -1;
static u32 fnt_last_tick = 0;
static int fnt_view = 0;                               /* 0 the folder, 1 the preview of the selected face */
static int fnt_warn = -1;                              /* the pack index that waits for "use it anyway?" */
static int fnt_btn_pressed = 0;                        /* 1 Use as System Font, 2 Done, 3 Yes, 4 No, 5 the kerning check box */
static const char *fnt_msg = 0;
static lbx_t fnt_lb = { 0, 0, 0, 0, 15, 0, 0, 0 };     /* the compact list */

static int fnt_idx(void) { return fontface_order[fnt_sel < fontface_count ? fnt_sel : 0]; }
static void fnt_geo(sheet_t *g) { sheet_geo(g, setting.x, setting_body_y(), setting.w, setting_status_y(), 220, 66); }   /* small on purpose: a 320 x 200 screen leaves ~74 pixels */
static void fnt_open(void) {
    fontface_init_order();
    fnt_sel = 0;
    for (int i = 0; i < fontface_count; i++) if (fontface_order[i] == fontface_cur) fnt_sel = i;
    fnt_last_sel = -1; fnt_view = 0; fnt_warn = -1; fnt_btn_pressed = 0; fnt_msg = 0;
    fontface_pv_free(); lbx_detach(&fnt_lb);
}
static void fnt_leave_preview(void) { fnt_view = 0; fontface_pv_free(); }
static void fnt_open_preview(void) { if (fontface_pv_load(fnt_idx())) { fnt_view = 1; fnt_msg = 0; } else fnt_msg = SKO("The font could not be loaded.", "글꼴을 불러올 수 없습니다."); }

/* ---- layout ---- */
static int fnt_list_icons(const sheet_t *g) {                                                        /* the icons only when ALL the rows fit; else the plain list */
    int lw = g->W - 4, lh = g->H - FNT_BOTTOM, cols = (lw - 8) / FNT_ITEM_W; if (cols < 1) cols = 1;
    int rows = (fontface_count + cols - 1) / cols;
    return rows * FNT_ITEM_H + 8 <= lh;
}
static int fnt_btn_y(const sheet_t *g) { return g->Y + g->H - 28; }
static const char *fnt_use_label(void) { return SKO("Use as System Font", "시스템 글꼴로 사용"); }
static int fnt_use_w(void) { return ko_string_width(fnt_use_label()) + 24; }
static int fnt_kern_x(const sheet_t *g) { return g->X + g->W - 8 - 19 - ko_string_width(SKO("Kerning", "커닝")); }
#define FNT_ASK_W 262
#define FNT_ASK_H 84
static void fnt_ask_geo(const sheet_t *g, int *x, int *y) { *x = g->X + (g->W - FNT_ASK_W) / 2; *y = g->Y + (g->H - FNT_ASK_H) / 2; }
static int fnt_can_use(void) { return fontface_count > 1 && fnt_idx() != fontface_cur; }

static int fnt_btn_hit(int mx, int my) {
    sheet_t g; fnt_geo(&g);
    if (!g.ok) return sheet_hint_hit(&g, mx, my);
    if (fnt_warn >= 0) {
        int x, y; fnt_ask_geo(&g, &x, &y);
        if (in_rect(mx, my, x + FNT_ASK_W / 2 - 76, y + FNT_ASK_H - 28, 70, 22)) return 3;
        if (in_rect(mx, my, x + FNT_ASK_W / 2 + 6, y + FNT_ASK_H - 28, 70, 22)) return 4;
        return 0;
    }
    int by = fnt_btn_y(&g);
    if (fnt_can_use() && in_rect(mx, my, g.X + 8, by, fnt_use_w(), 22)) return 1;
    if (fnt_view == 1 && in_rect(mx, my, g.X + 8 + fnt_use_w() + 8, by, 80, 22)) return 2;
    if (fnt_view == 0 && in_rect(mx, my, fnt_kern_x(&g), by + 4, 19 + ko_string_width(SKO("Kerning", "커닝")), 13)) return 5;
    return 0;
}

/* ---- drawing ---- */
static void fnt_row_draw(int idx, int x, int y, int w, int h, int selected) {
    (void)h;
    char nm[40]; fontface_name(fontface_order[idx], nm, sizeof nm);
    ko_draw_mixed_string(x + 6, y + 2, nm, selected ? TH_TEXT_INVERSE : TH_TEXT);
    if (fontface_order[idx] == fontface_cur) {
        const char *t = SKO("System font", "시스템 글꼴");
        ko_draw_mixed_string(x + w - 8 - ko_string_width(t), y + 2, t, selected ? TH_TEXT_INVERSE : TH_ACCENT);
    }
}
static void fnt_draw_samples(int sx, int sy, int sw, int sh) {
    fontface_tables_t sv;
    bb_clip_set(sx + 2, sy + 2, sw - 4, sh - 4);
    if (fontface_pv_enter(&sv)) {
        static const char *const lines[] = { "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz", "0123456789 .,;:!?@#$%&*()", "가나다라마바사아자차카타파하",
                                             "The quick brown fox jumps over the lazy dog." };
        int y = sy + 6;
        for (int i = 0; i < 5; i++) { ko_draw_mixed_string(sx + 8, y, lines[i], TH_FIELD_TEXT); y += FONT_LINE_H + 2; }
        fontface_pv_leave(&sv);
    } else ko_draw_mixed_string(sx + 8, sy + 6, SKO("(cannot show)", "(표시할 수 없음)"), TH_FIELD_TEXT);
    bb_clip_off();
}
static void fnt_draw(void) {
    sheet_t g; fnt_geo(&g);
    if (!g.ok) { sheet_hint_draw(&g, setting.x, setting_body_y(), fnt_btn_pressed); return; }
    char nm[40]; int by = fnt_btn_y(&g);
    if (fnt_view == 0) {
        int lx = g.X + 2, ly = g.Y + 2, lw = g.W - 4, lh = g.H - FNT_BOTTOM;
        ui_field(lx, ly, lw, lh, TH_FIELD, 0);
        if (fnt_list_icons(&g)) {
            int cols = (lw - 8) / FNT_ITEM_W; if (cols < 1) cols = 1;
            bb_clip_set(lx + 1, ly + 1, lw - 2, lh - 2);
            for (int i = 0; i < fontface_count; i++) {
                int ix = lx + 4 + (i % cols) * FNT_ITEM_W, iy = ly + 4 + (i / cols) * FNT_ITEM_H;
                const u32 *ic = sheet_icon32("TTF_OTF"); if (ic) blit_rgba32(ix + (FNT_ITEM_W - 32) / 2, iy, ic);
                fontface_name(fontface_order[i], nm, sizeof nm);
                int tw = ko_string_width(nm), tx = ix + (FNT_ITEM_W - tw) / 2, ty = iy + 36;
                if (i == fnt_sel) bb_fillrect(tx - 2, ty - 1, tw + 4, FONT_CELL + 2, TH_ACCENT);
                ko_draw_mixed_string(tx, ty, nm, i == fnt_sel ? TH_ACCENT_TEXT : TH_FIELD_TEXT);
                if (fontface_order[i] == fontface_cur) {
                    const char *t = SKO("System font", "시스템 글꼴");
                    ko_draw_mixed_string(ix + (FNT_ITEM_W - ko_string_width(t)) / 2, ty + FONT_LINE_H + 1, t, TH_ACCENT);
                }
            }
            bb_clip_off();
        } else {                                                                       /* compact: a plain list */
            fnt_lb.x = lx; fnt_lb.y = ly; fnt_lb.w = lw; fnt_lb.h = lh; fnt_lb.count = fontface_count; fnt_lb.row_h = FONT_CELL + 4; fnt_lb.sel = fnt_sel;
            lbx_draw(&fnt_lb, fnt_row_draw);
        }
        if (fnt_msg) ko_draw_mixed_string(g.X + 8, g.Y + g.H - FNT_BOTTOM + 4, fnt_msg, TH_ACCENT);
        else if (fontface_count < 2) ko_draw_mixed_string(g.X + 8, g.Y + g.H - FNT_BOTTOM + 4, SKO("More fonts are on the full disc.", "더 많은 글꼴은 전체 디스크에 있습니다."), TH_SHADOW);
        ui_checkbox_label(fnt_kern_x(&g), by + 4, SKO("Kerning", "커닝"), font_kerning, 1);
    } else {
        int idx = fnt_idx(), lh = FONT_LINE_H;
        fontface_name(idx, nm, sizeof nm);
        ko_draw_mixed_string(g.X + 10, g.Y + 8, nm, TH_TEXT);
        char info[64]; u32 n = 0; info[0] = 0;
        append_str(info, &n, sizeof(info), SKO("Bitmap font, ", "비트맵 글꼴, "));
        { char d[4]; int c = fp_ent[idx].cell, nd = 0; if (c >= 10) d[nd++] = (char)('0' + c / 10); d[nd++] = (char)('0' + c % 10); d[nd] = 0; append_str(info, &n, sizeof(info), d); }
        append_str(info, &n, sizeof(info), SKO(" pixels", " 픽셀"));
        ko_draw_mixed_string(g.X + 10, g.Y + 8 + lh, info, TH_TEXT);
        int sx = g.X + 8, sy = g.Y + 8 + 2 * lh + 8, sw = g.W - 16, sh = by - 8 - sy;
        ui_field(sx, sy, sw, sh, TH_FIELD, 0);
        fnt_draw_samples(sx, sy, sw, sh);
        ui_pushbutton(g.X + 8 + fnt_use_w() + 8, by, 80, 22, SKO("Done", "닫기"), fnt_btn_pressed == 2, 1);
    }
    ui_pushbutton(g.X + 8, by, fnt_use_w(), 22, fnt_use_label(), fnt_btn_pressed == 1 && fnt_can_use(), fnt_can_use());
    if (fnt_warn >= 0) {                                                               /* "this font is very small: use it anyway?" */
        int x, y; fnt_ask_geo(&g, &x, &y);
        bb_fillrect(x, y, FNT_ASK_W, FNT_ASK_H, TH_FACE); ui_bevel(x, y, FNT_ASK_W, FNT_ASK_H, 0);
        bb_fillrect(x + 3, y + 3, FNT_ASK_W - 6, FONT_CELL + 4, TH_ACCENT);
        ko_draw_mixed_string(x + 7, y + 5, SKO("Font", "글꼴"), TH_TEXT_INVERSE);
        ko_draw_mixed_string(x + 10, y + 5 + FONT_LINE_H + 4, SKO("This font is very small. Use it anyway?", "이 글꼴은 매우 작습니다. 그래도 사용할까요?"), TH_TEXT);
        ui_pushbutton(x + FNT_ASK_W / 2 - 76, y + FNT_ASK_H - 28, 70, 22, SKO("Yes", "예"), fnt_btn_pressed == 3, 1);
        ui_pushbutton(x + FNT_ASK_W / 2 + 6, y + FNT_ASK_H - 28, 70, 22, SKO("No", "아니요"), fnt_btn_pressed == 4, 1);
    }
}

/* ---- actions ---- */
static void fnt_do_apply(int idx) {
    if (!fontface_use(idx)) { fnt_msg = SKO("The font could not be loaded.", "글꼴을 불러올 수 없습니다."); return; }
    fontface_pref = idx + 1;
    fnt_msg = fontface_save_pref() ? 0 : SKO("Used until the computer is restarted.", "컴퓨터를 다시 시작할 때까지만 사용됩니다.");
}
static void fnt_apply(int idx) {
    if (idx == fontface_cur) { fnt_msg = SKO("That font is already in use.", "이미 사용 중인 글꼴입니다."); return; }
    if (fp_ent[idx].cell <= 7) { fnt_warn = idx; return; }
    fnt_do_apply(idx);
}
static void fnt_fire(int b) {
    if (b == 1) fnt_apply(fnt_idx());
    else if (b == 2) fnt_leave_preview();
    else if (b == 3) { int i = fnt_warn; fnt_warn = -1; if (i >= 0) fnt_do_apply(i); }
    else if (b == 4) fnt_warn = -1;
    else if (b == 5) font_kerning = !font_kerning;
}
static void fnt_release(int mx, int my) {
    int b = fnt_btn_pressed;
    fnt_btn_pressed = 0;
    if (b && fnt_btn_hit(mx, my) == b) fnt_fire(b);
}
static int fnt_click(int mx, int my, u32 now) {
    sheet_t g; fnt_geo(&g);
    int b = fnt_btn_hit(mx, my);
    if (b) { if (b != 1 || fnt_can_use()) fnt_btn_pressed = b; return 1; }
    if (!g.ok || fnt_warn >= 0 || fnt_view != 0) return 1;
    fnt_msg = 0;
    int lx = g.X + 2, ly = g.Y + 2, lw = g.W - 4, lh = g.H - FNT_BOTTOM;
    if (!in_rect(mx, my, lx, ly, lw, lh)) return 1;
    int i = -1;
    if (fnt_list_icons(&g)) {
        int cols = (lw - 8) / FNT_ITEM_W; if (cols < 1) cols = 1;
        int cx = mx - lx - 4, cy = my - ly - 4;
        if (cx >= 0 && cy >= 0) { int c = cx / FNT_ITEM_W, r = cy / FNT_ITEM_H; if (c < cols) i = r * cols + c; }
        if (i >= fontface_count) i = -1;
    } else {
        fnt_lb.x = lx; fnt_lb.y = ly; fnt_lb.w = lw; fnt_lb.h = lh; fnt_lb.count = fontface_count; fnt_lb.row_h = FONT_CELL + 4; fnt_lb.sel = fnt_sel;
        lbx_click(&fnt_lb, mx, my); i = fnt_lb.sel;
    }
    if (i < 0) return 1;
    if (i == fnt_last_sel && (now - fnt_last_tick) < double_click_window) { fnt_sel = i; fnt_last_sel = -1; fnt_open_preview(); }
    else { fnt_sel = i; fnt_last_sel = i; fnt_last_tick = now; }
    return 1;
}
/* Arrows move the selection, Enter opens the preview (or uses the face, in it), Esc backs out of the preview or the question. 1 = used. */
static int fnt_key(int k) {
    if (setting_page != SP_TEXT) return 0;
    if (fnt_warn >= 0) { if (k == '\n') fnt_fire(3); else if (k == 27) fnt_fire(4); return 1; }
    if (fnt_view == 1) { if (k == 27) { fnt_leave_preview(); return 1; } if (k == '\n') { if (fnt_can_use()) fnt_apply(fnt_idx()); return 1; } return 0; }
    if ((k == KEY_LEFT || k == KEY_UP) && fnt_sel > 0) { fnt_sel--; fnt_msg = 0; return 1; }
    if ((k == KEY_RIGHT || k == KEY_DOWN) && fnt_sel + 1 < fontface_count) { fnt_sel++; fnt_msg = 0; return 1; }
    if (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_UP || k == KEY_DOWN) return 1;
    if (k == '\n') { fnt_open_preview(); return 1; }
    return 0;
}
static void fnt_menu_action(int menu, int item) {
    if (menu == 0) {
        if (item == 0) { if (fnt_view == 0) fnt_open_preview(); }
        else if (item == 1) { if (fnt_can_use()) fnt_apply(fnt_idx()); }
        else { setting.open = 0; fontface_pv_free(); }
    } else if (menu == 1) {
        if (item == 0) font_kerning = !font_kerning;
        else { fnt_leave_preview(); setting_page = -1; setting_armed = -1; }
    } else { fnt_leave_preview(); setting_sel = SP_SYSTEM; setting_open_item(SP_SYSTEM); }
}

#endif
