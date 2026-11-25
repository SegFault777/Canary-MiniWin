/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_APPS_SETTING_DISPLAY_H
#define MW_APPS_SETTING_DISPLAY_H

/* apps/setting_display.h -- Display Properties (1.1-alpha-14): two tabs, Background (the desktop colour) and Settings (the screen area), like Windows 95.
 *
 * The screen size is chosen by the BIOS before the kernel runs, so changing it means writing one byte of the boot sector (offset 440: 1 = 320x200,
 * anything else = 640x480, read by boot/stage2.asm) and restarting. Bit 7 of that byte means "not confirmed yet": a boot that finds it set shows
 * "Keep this screen size?" with a 15-second countdown -- Yes (or Enter) clears the bit, No (Esc) or the time running out writes 640x480 and restarts.
 * (A screen that stays black still comes back by itself.) If the BIOS has no mode of the asked size, stage 2 boots 640x480 and the question is
 * replaced by a notice. The live disc cannot remember anything, so it cannot change the size.
 * The COLOUR DEPTH is not chosen here: stage 2 takes the best one the BIOS offers (32, 24, 16 bits, else 256 colours; never fewer) and this page only shows it.
 *
 * Built from the shared modules: ui/sheet.h (frame, tabs, OK / Cancel / Apply), ui/controls.h (group, slider, label), ui/listbox.h (the colour list).
 * It must also work AT 320x200 -- that is where a user who picked the small size ends up -- so its minimum size is small. */

/* ---- the size byte in the boot sector ---- */
#define DISP_BYTE_OFF 440
#define DISP_PENDING  0x80
static u8 disp_sec[512];
static int disp_size_pos(void) { return VGA_WIDTH <= 320 ? 0 : VGA_WIDTH >= 854 ? 2 : 1; }  /* the slider position now running: 0 = 320x200, 1 = 640x480, 2 = 854x480 */
static const int disp_sizes[3][2] = { { 320, 200 }, { 640, 480 }, { 854, 480 } };
static u8 disp_pos_to_byte(int pos) { return pos == 0 ? 1 : pos == 2 ? 2 : 0; }             /* what stage 2 reads at offset 440 */
static const char *disp_depth_name(void) {                                                    /* what boot/stage2.asm found: 32, then 24, 16 and 256 colours */
    return vga_bpp == 32 ? SKO("True Color (32 bit)", "트루 컬러 (32비트)") : vga_bpp == 24 ? SKO("True Color (24 bit)", "트루 컬러 (24비트)")
         : vga_bpp == 16 ? SKO("High Color (16 bit)", "하이 컬러 (16비트)") : SKO("256 Colors", "256 색");
}
static int disp_read_byte(int *v) {
    if (live_boot || !ata_read_sector(0, disp_sec)) return 0;
    *v = disp_sec[DISP_BYTE_OFF]; return 1;
}
static int disp_write_byte(u8 v) {
    if (live_boot || !ata_read_sector(0, disp_sec)) return 0;
    disp_sec[DISP_BYTE_OFF] = v;
    return ata_write_sector(0, disp_sec);
}
static void disp_black_and_restart(void) {
    bb_fillrect(0, 0, VGA_WIDTH, VGA_HEIGHT, 0x000000); vga_present();                  /* the screen is black while the mode changes */
    system_restart();
}

/* ---- "Keep this screen size?" after a restart ---- */
#define DISP_CONFIRM_SECS 15
static int disp_confirm_open = 0, disp_boot_checked = 0, disp_unavail = 0;      /* unavail: the asked-for size is not offered by this BIOS (a notice, not a question) */
static u32 disp_confirm_start = 0;
static u32 disp_secs_now(void) { rtc_time_t t; rtc_read(&t); return (u32)(t.hour * 3600 + t.minute * 60 + t.second); }
static int disp_confirm_left(void) {
    int e = (int)((disp_secs_now() + 86400u - disp_confirm_start) % 86400u);
    return e >= DISP_CONFIRM_SECS ? 0 : DISP_CONFIRM_SECS - e;
}
static void disp_keep(void) { int v; if (disp_read_byte(&v)) disp_write_byte(disp_unavail ? (u8)(fontface_pref << 2) : (u8)(v & ~DISP_PENDING)); disp_confirm_open = 0; disp_unavail = 0; }   /* (an unavailable size is forgotten: the byte goes back to 640x480) */
static void disp_revert(void) { disp_confirm_open = 0; disp_write_byte((u8)(fontface_pref << 2)); disp_black_and_restart(); }     /* back to 640x480 */
/* ---- the system font face is kept in the same byte (bits 2-4: pack index + 1, 0 = the default, 7 = the TrueType font of /etc/FONT.CFG) ---- */
/* At boot: the face the byte names; with none chosen, a 320 x 200 screen starts on the 7-pixel face (more fits), anything else on the built-in 11. */
static void fontface_boot_apply(int pref) {
    fontface_init_order();
    if (pref == UFT_PREF) {                                                             /* a TrueType font (/etc/FONT.CFG): built again now, with a progress bar */
        int k = uft_load_cfg();
        if (k >= 0) { fontface_init_order(); if (fontface_use(fp_n + k)) { fontface_pref = UFT_PREF; return; } }
        pref = 0;                                                                       /* gone or broken: the default face */
    }
    if (fp_n < 2) return;                                                               /* the floppy's pack has one face: nothing to switch */
    int idx = -1;
    if (pref >= 1 && pref - 1 < fp_n) { idx = pref - 1; fontface_pref = pref; }
    else if (VGA_WIDTH <= 320) idx = fontface_find(7, 0);
    if (idx > 0) fontface_use(idx);
}
/* Writes the choice into the boot sector. 0 = it could not be written (the live disc): the face still holds until the next restart. */
static int fontface_save_pref(void) {
    int v;
    if (!disp_read_byte(&v)) return 0;
    return disp_write_byte((u8)((v & ~0x1C) | ((fontface_pref & 7) << 2)));
}

/* Once per frame (render_frame): the first call looks at the boot sector; while the question is open, the time runs out here. */
static void disp_tick(void) {
    if (!disp_boot_checked) {
        disp_boot_checked = 1;
        int v;
        int have = disp_read_byte(&v);
        fontface_boot_apply(have ? (v >> 2) & 7 : 0);                                      /* the system font the user chose (or the default for this screen) */
        if (have && (v & DISP_PENDING)) {
            int req = v & 0x03, run = disp_size_pos() == 0 ? 1 : disp_size_pos() == 2 ? 2 : 0;
            disp_confirm_open = 1; disp_confirm_start = disp_secs_now();
            if ((req == 1 || req == 2) && req != run) disp_unavail = 1;                  /* stage 2 had to fall back to 640x480 */
        }
    }
    if (disp_confirm_open && !disp_unavail && disp_confirm_left() == 0) disp_revert();
}
#define DISP_DLG_W 236
#define DISP_DLG_H 96
static inline int disp_dlg_x(void) { return (VGA_WIDTH - DISP_DLG_W) / 2; }
static inline int disp_dlg_y(void) { return (VGA_HEIGHT - TASKBAR_H - DISP_DLG_H) / 2; }
static int disp_dlg_btn(int mx, int my) {                                              /* 1 = Yes, 2 = No */
    int x = disp_dlg_x(), y = disp_dlg_y() + DISP_DLG_H - 30;
    if (disp_unavail) return in_rect(mx, my, x + DISP_DLG_W / 2 - 35, y, 70, 22) ? 1 : 0;       /* the notice has one button */
    if (in_rect(mx, my, x + DISP_DLG_W / 2 - 76, y, 70, 22)) return 1;
    if (in_rect(mx, my, x + DISP_DLG_W / 2 + 6, y, 70, 22)) return 2;
    return 0;
}
static void disp_confirm_draw(void) {
    int x = disp_dlg_x(), y = disp_dlg_y();
    bb_fillrect(x, y, DISP_DLG_W, DISP_DLG_H, TH_FACE); ui_bevel(x, y, DISP_DLG_W, DISP_DLG_H, 0);
    bb_fillrect(x + 3, y + 3, DISP_DLG_W - 6, 15, TH_ACCENT);
    ko_draw_mixed_string(x + 7, y + 4, SKO("Display Properties", "화면 속성"), TH_TEXT_INVERSE);
    if (disp_unavail) {
        ko_draw_mixed_string(x + 12, y + 26, SKO("This computer cannot show that size.", "이 컴퓨터는 그 크기를 표시할 수 없습니다."), TH_TEXT);
        ko_draw_mixed_string(x + 12, y + 42, SKO("The screen stays at 640 x 480.", "화면은 640 x 480으로 유지됩니다."), TH_TEXT);
        ui_pushbutton(x + DISP_DLG_W / 2 - 35, y + DISP_DLG_H - 30, 70, 22, "OK", 0, 1);
        return;
    }
    ko_draw_mixed_string(x + 12, y + 26, SKO("Keep this screen size?", "이 화면 크기를 유지할까요?"), TH_TEXT);
    char b[64]; u32 n = 0; b[0] = 0; int left = disp_confirm_left();
    append_str(b, &n, sizeof(b), SKO("Going back to 640 x 480 in ", "640 x 480으로 돌아갑니다: "));
    char d[4] = { (char)('0' + left / 10), (char)('0' + left % 10), 0, 0 };
    append_str(b, &n, sizeof(b), left >= 10 ? d : d + 1);
    append_str(b, &n, sizeof(b), SKO(" s", "초"));
    ko_draw_mixed_string(x + 12, y + 42, b, TH_TEXT);
    int by = y + DISP_DLG_H - 30;
    ui_pushbutton(x + DISP_DLG_W / 2 - 76, by, 70, 22, SKO("Yes", "예"), 0, 1);
    ui_pushbutton(x + DISP_DLG_W / 2 + 6, by, 70, 22, SKO("No", "아니요"), 0, 1);
}
static void disp_confirm_mouse(int mx, int my, int clicked) {
    if (!clicked) return;
    int b = disp_dlg_btn(mx, my);
    if (b == 1) disp_keep(); else if (b == 2 && !disp_unavail) disp_revert();
}

/* ---- the sheet ---- */
static int dp_tab = 0;                        /* 0 Background, 1 Settings */
static int dp_p_pos = 1;                      /* edited copy of the screen area: 0 = 320 x 200 (Less), 1 = 640 x 480, 2 = 854 x 480 (More) */
static lbx_t dp_lb = { 0, 0, 0, 0, 15, SETTING_DESK_N, 0, 0 };       /* the list of desktop colours: its selection is the edited colour */
static int dp_btn_pressed = 0;                /* 1 OK, 2 Cancel, 3 Apply; 11 Restart, 12 Cancel of the question */
static int dp_drag = 0;
static int dp_ask = 0;                        /* the "restart to change the size" question is open */
static const char *dp_msg = 0;                /* why the size could not be changed */

static void dp_open(void) {
    dp_tab = 0; dp_p_pos = disp_size_pos(); dp_btn_pressed = 0; dp_drag = 0; dp_ask = 0; dp_msg = 0;
    dp_lb.sel = setting_desk_idx; dp_lb.top = 0; lbx_detach(&dp_lb);
}
static void dp_geo(sheet_t *g) { sheet_geo(g, setting.x, setting_body_y(), setting.w, setting_status_y(), 250, 66); }   /* small on purpose: at 320 x 200 the page is only ~74 pixels high and this is where a user who chose the small size has to be able to change it back */
static const char *dp_tab_label(int i) { return i == 0 ? SKO("Background", "배경") : SKO("Settings", "설정"); }
static int dp_dirty(void) { return dp_lb.sel != setting_desk_idx || dp_p_pos != disp_size_pos(); }

/* Two layouts: the BIG one (a page at least DP_BIG_PH high: the usual window) has the monitor picture on top like Windows 95 and the groups below it;
 * the COMPACT one (a 320 x 200 screen leaves a page of ~74 pixels) has no picture and shows the same controls in a tight space. */
#define DP_BIG_PH 258
typedef struct {
    int big;
    int g1x, g1y, g1w, g1h;                   /* the group of the Background tab (compact) / of the Screen area (compact) */
    int lx, ly, lw, lh;                       /* the colour list */
    int pvx, pvy, pvw, pvh;                   /* compact Background: the sample box */
    int sx, sy, sw;                           /* the slider */
    int mx, my, mw, mh;                       /* big: the monitor */
    int pgx, pgy, pgw, pgh;                   /* big Settings: the "Color palette" group */
    int sgx, sgy, sgw, sgh;                   /* big Settings: the "Screen area" group */
} dp_lay_t;
static void dp_lay(const sheet_t *g, dp_lay_t *l) {
    l->big = g->ph >= DP_BIG_PH;
    l->g1x = g->px + 8; l->g1y = g->py + 2; l->g1w = g->pw - 16; l->g1h = g->ph - 4;
    l->lx = l->g1x + 8; l->ly = l->g1y + 16; l->lh = l->g1h - 24;
    l->lw = (l->g1w - 16) * 11 / 20; if (l->lw > 160) l->lw = 160;
    l->pvx = l->lx + l->lw + 8; l->pvy = l->ly; l->pvw = l->g1x + l->g1w - 8 - l->pvx; l->pvh = l->lh;
    l->sx = l->g1x + 46; l->sw = l->g1w - 92; l->sy = l->g1y + 12;
    l->mw = 172; l->mh = 142; l->mx = g->px + (g->pw - l->mw) / 2; l->my = g->py + 8;
    l->pgx = g->px + 10; l->pgy = l->my + l->mh + 8; l->pgw = (g->pw - 30) / 2; l->pgh = 96;
    l->sgx = l->pgx + l->pgw + 10; l->sgy = l->pgy; l->sgw = g->pw - 20 - l->pgw - 10; l->sgh = 96;
    if (l->big) {
        l->sx = l->sgx + 38; l->sw = l->sgw - 76; l->sy = l->sgy + 16;                    /* the slider between "Less" and "More" */
        l->lx = g->px + 18; l->ly = l->pgy + 16; l->lw = g->pw - 36; l->lh = g->py + g->ph - 10 - l->ly;   /* Background: the list under the monitor */
        l->g1x = g->px + 10; l->g1y = l->pgy; l->g1w = g->pw - 20; l->g1h = g->py + g->ph - 6 - l->pgy;
    }
}
static void dp_lb_sync(const dp_lay_t *l) { dp_lb.x = l->lx; dp_lb.y = l->ly; dp_lb.w = l->lw; dp_lb.h = l->lh; dp_lb.count = SETTING_DESK_N; }
static void dp_row_draw(int idx, int x, int y, int w, int h, int selected) {
    (void)w; (void)h;
    ko_draw_mixed_string(x + 6, y + 2, SKO(setting_desk[idx].en, setting_desk[idx].ko), selected ? TH_TEXT_INVERSE : TH_TEXT);
}
static const char *dp_size_text(int pos) {
    return pos == 0 ? SKO("320 by 200 pixels", "320 x 200 픽셀") : pos == 1 ? SKO("640 by 480 pixels", "640 x 480 픽셀") : SKO("854 by 480 pixels", "854 x 480 픽셀");
}

/* The monitor of the Display Properties pictures: a bezel, a sunken screen in the shape of the chosen size, a foot. With `mini`, a tiny desktop
 * (icons, a window, a task bar) in the screen. The colours are the chosen desktop colour and its text colour. */
static void dp_monitor(int x, int y, int w, int h, u32 bg, u32 fg, int pw, int ph, int mini) {
    int bh = h - 14;
    bb_fillrect(x, y, w, bh, TH_FACE); ui_bevel(x, y, w, bh, 0);
    int aw = w - 24, ah = bh - 24, sw = aw, sh = aw * ph / pw;
    if (sh > ah) { sh = ah; sw = ah * pw / ph; }
    int sx = x + (w - sw) / 2, sy = y + (bh - sh) / 2;
    ui_bevel(sx - 2, sy - 2, sw + 4, sh + 4, 1);
    bb_fillrect(sx, sy, sw, sh, bg);
    if (mini) {
        int ty = sy + sh - 6;                                                            /* the task bar */
        bb_fillrect(sx, ty, sw, 6, TH_FACE); bb_fillrect(sx + 1, ty + 1, 8, 4, TH_SHADOW);
        for (int i = 0; i < 3; i++) { int ix = sx + 4, iy = sy + 4 + i * 11; bb_fillrect(ix, iy, 6, 6, fg); bb_fillrect(ix + 1, iy + 1, 4, 4, bg); }
        int wx = sx + sw / 4, wy = sy + sh / 6, ww = sw / 2, wh = sh / 2;                /* a window */
        bb_fillrect(wx, wy, ww, wh, TH_FACE); ui_bevel(wx, wy, ww, wh, 0);
        bb_fillrect(wx + 2, wy + 2, ww - 4, 4, TH_ACCENT);
        for (int j = 0; j < 2; j++) for (int i = 0; i < 3; i++) bb_fillrect(wx + 5 + i * 9, wy + 10 + j * 9, 5, 5, TH_SHADOW);
    }
    bb_fillrect(x + w / 2 - 14, y + bh, 28, 4, TH_SHADOW);                              /* the neck and the foot */
    bb_fillrect(x + w / 2 - 34, y + bh + 4, 68, 6, TH_FACE); ui_bevel(x + w / 2 - 34, y + bh + 4, 68, 6, 0);
}
/* A strip of the rainbow (the colour palette's picture). */
static void dp_rainbow(int x, int y, int w, int h) {
    for (int i = 0; i < w; i++) {
        int hue = i * 6 * 256 / w, sec = hue >> 8, f = hue & 255, r = 0, g = 0, b = 0;
        switch (sec) { case 0: r = 255; g = f; break; case 1: r = 255 - f; g = 255; break; case 2: g = 255; b = f; break;
                       case 3: g = 255 - f; b = 255; break; case 4: r = f; b = 255; break; default: r = 255; b = 255 - f; break; }
        bb_fillrect(x + i, y, 1, h, RGB((u8)r, (u8)g, (u8)b));
    }
}

#define DP_ASK_W 252
#define DP_ASK_H 86
static void dp_ask_geo(const sheet_t *g, int *x, int *y) { *x = g->X + (g->W - DP_ASK_W) / 2; *y = g->Y + (g->H - DP_ASK_H) / 2; }

static void dp_colours(int *bg, int *fg) {
    int i = dp_lb.sel;
    *bg = (int)(i == 0 ? TH_DESKTOP : setting_desk[i].bg); *fg = (int)(i == 0 ? TH_DESKTOP_TEXT : setting_desk[i].fg);
}
static void dp_draw(void) {
    sheet_t g; dp_geo(&g);
    if (!g.ok) { sheet_hint_draw(&g, setting.x, setting_body_y(), dp_btn_pressed); return; }
    dp_lay_t l; dp_lay(&g, &l);
    sheet_draw_frame(&g, dp_tab_label, 2, dp_tab);
    int bg, fg; dp_colours(&bg, &fg);
    if (dp_tab == 0) {
        if (l.big) dp_monitor(l.mx, l.my, l.mw, l.mh, (u32)bg, (u32)fg, 4, 3, 0);
        ui_group(l.g1x, l.g1y, l.g1w, l.g1h, SKO("Desktop colour", "바탕화면 색"));
        dp_lb_sync(&l); lbx_draw(&dp_lb, dp_row_draw);
        if (!l.big) {                                                                  /* compact: a sample box beside the list */
            ui_sunken(l.pvx, l.pvy, l.pvw, l.pvh);
            bb_fillrect(l.pvx + 2, l.pvy + 2, l.pvw - 4, l.pvh - 4, (u32)bg);
            const char *s = SKO("Sample", "보기"); ko_draw_mixed_string(l.pvx + (l.pvw - ko_string_width(s)) / 2, l.pvy + l.pvh / 2 - 5, s, (u32)fg);
        }
    } else if (l.big) {
        dp_monitor(l.mx, l.my, l.mw, l.mh, (u32)bg, (u32)fg, disp_sizes[dp_p_pos][0], disp_sizes[dp_p_pos][1], 1);
        ui_group(l.pgx, l.pgy, l.pgw, l.pgh, SKO("Color palette", "색 팔레트"));
        ui_sunken(l.pgx + 10, l.pgy + 18, l.pgw - 20, 20);
        ko_draw_mixed_string(l.pgx + 16, l.pgy + 23, disp_depth_name(), TH_TEXT);
        dp_rainbow(l.pgx + 10, l.pgy + 46, l.pgw - 20, 14);
        ko_draw_mixed_string(l.pgx + 10, l.pgy + 68, SKO("Chosen by the computer.", "컴퓨터가 자동으로 고릅니다."), TH_SHADOW);
        ui_group(l.sgx, l.sgy, l.sgw, l.sgh, SKO("Screen area", "화면 영역"));
        ui_label(l.sgx + 8, l.sy + 4, SKO("Less", "작게"), 1);
        ui_label(l.sgx + l.sgw - 8 - ko_string_width(SKO("More", "크게")), l.sy + 4, SKO("More", "크게"), 1);
        ui_slider(l.sx, l.sy, l.sw, dp_p_pos, 2, 1);
        { const char *s = dp_size_text(dp_p_pos); ko_draw_mixed_string(l.sgx + (l.sgw - ko_string_width(s)) / 2, l.sgy + 50, s, TH_TEXT); }
        if (dp_msg) ko_draw_mixed_string(l.sgx + 8, l.sgy + 70, dp_msg, TH_ACCENT);
        else ko_draw_mixed_string(l.sgx + 8, l.sgy + 70, SKO("Changing it restarts.", "바꾸면 다시 시작합니다."), TH_SHADOW);
    } else {                                                                           /* compact Settings */
        ui_group(l.g1x, l.g1y, l.g1w, l.g1h > 78 ? 78 : l.g1h, SKO("Screen area", "화면 영역"));
        ui_label(l.g1x + 8, l.sy + 4, SKO("Less", "작게"), 1);
        ui_label(l.g1x + l.g1w - 8 - ko_string_width(SKO("More", "크게")), l.sy + 4, SKO("More", "크게"), 1);
        ui_slider(l.sx, l.sy, l.sw, dp_p_pos, 2, 1);
        { const char *s = dp_size_text(dp_p_pos); ko_draw_mixed_string(l.g1x + (l.g1w - ko_string_width(s)) / 2, l.g1y + 38, s, TH_TEXT); }
        if (dp_msg) ko_draw_mixed_string(l.g1x + 8, l.g1y + 51, dp_msg, TH_ACCENT);
        else ko_draw_mixed_string(l.g1x + 8, l.g1y + 51, disp_depth_name(), TH_SHADOW);
    }
    sheet_footer_draw(&g, dp_btn_pressed, dp_dirty());
    if (dp_ask) {                                                                       /* the question: restart now? */
        int x, y; dp_ask_geo(&g, &x, &y);
        bb_fillrect(x, y, DP_ASK_W, DP_ASK_H, TH_FACE); ui_bevel(x, y, DP_ASK_W, DP_ASK_H, 0);
        bb_fillrect(x + 3, y + 3, DP_ASK_W - 6, 15, TH_ACCENT);
        ko_draw_mixed_string(x + 7, y + 4, SKO("Display Properties", "화면 속성"), TH_TEXT_INVERSE);
        ko_draw_mixed_string(x + 10, y + 24, SKO("The computer must restart to use", "새 크기를 쓰려면 컴퓨터를"), TH_TEXT);
        ko_draw_mixed_string(x + 10, y + 37, SKO("the new screen area. Restart now?", "다시 시작해야 합니다. 지금 시작할까요?"), TH_TEXT);
        ui_pushbutton(x + DP_ASK_W / 2 - 76, y + DP_ASK_H - 28, 70, 22, SKO("Restart", "다시 시작"), dp_btn_pressed == 11, 1);
        ui_pushbutton(x + DP_ASK_W / 2 + 6, y + DP_ASK_H - 28, 70, 22, SKO("Cancel", "취소"), dp_btn_pressed == 12, 1);
    }
}

static int dp_btn_hit(int mx, int my) {
    sheet_t g; dp_geo(&g);
    if (dp_ask) {
        int x, y; dp_ask_geo(&g, &x, &y);
        if (in_rect(mx, my, x + DP_ASK_W / 2 - 76, y + DP_ASK_H - 28, 70, 22)) return 11;
        if (in_rect(mx, my, x + DP_ASK_W / 2 + 6, y + DP_ASK_H - 28, 70, 22)) return 12;
        return 0;
    }
    return g.ok ? sheet_footer_hit(&g, mx, my, dp_dirty()) : sheet_hint_hit(&g, mx, my);
}
/* Writes what was chosen. The colour is applied at once; a new screen area asks first (it restarts the computer). */
static void dp_apply(void) {
    setting_desk_idx = dp_lb.sel; setting_desk_apply(); syscfg_touch();
    if (dp_p_pos != disp_size_pos()) {
        if (live_boot) { dp_msg = SKO("The live disc cannot change the screen size.", "라이브 디스크에서는 화면 크기를 바꿀 수 없습니다."); dp_p_pos = disp_size_pos(); }
        else dp_ask = 1;
    }
}
static void dp_close(void) { setting_page = -1; setting_armed = -1; lbx_detach(&dp_lb); dp_ask = 0; }
static void dp_fire(int b) {
    if (b == 1) { dp_apply(); if (!dp_ask) dp_close(); }
    else if (b == 2) dp_close();
    else if (b == 3) dp_apply();
    else if (b == 12) { dp_ask = 0; dp_p_pos = disp_size_pos(); }
    else if (b == 11) {
        if (disp_write_byte((u8)(disp_pos_to_byte(dp_p_pos) | (fontface_pref << 2) | DISP_PENDING))) disp_black_and_restart();
        dp_ask = 0; dp_p_pos = disp_size_pos();
        dp_msg = SKO("The boot sector could not be written.", "부트 섹터에 쓸 수 없습니다.");
    }
}
static void dp_release(int mx, int my) {
    int b = dp_btn_pressed;
    dp_btn_pressed = 0; dp_drag = 0;
    if (b && dp_btn_hit(mx, my) == b) dp_fire(b);
}
static void dp_poll_drag(int mx, int left_now) {
    if (setting_page != SP_DISPLAY || !dp_drag) return;
    if (!left_now) { dp_drag = 0; return; }
    sheet_t g; dp_geo(&g); dp_lay_t l; dp_lay(&g, &l);
    dp_p_pos = ui_slider_pos(l.sx, l.sw, 2, mx);
}
static int dp_click(int mx, int my) {
    sheet_t g; dp_geo(&g);
    int b = dp_btn_hit(mx, my);
    if (b) { dp_btn_pressed = b; return 1; }
    if (!g.ok || dp_ask) return 1;
    dp_lay_t l; dp_lay(&g, &l);
    dp_msg = 0;
    { int t = sheet_tab_at(&g, dp_tab_label, 2, mx, my);
      if (t >= 0) { dp_tab = t; dp_drag = 0; if (t != 0) lbx_detach(&dp_lb); return 1; } }
    if (dp_tab == 0) { dp_lb_sync(&l); lbx_click(&dp_lb, mx, my); }
    else if (in_rect(mx, my, l.sx, l.sy - 2, l.sw, 28)) { dp_p_pos = ui_slider_pos(l.sx, l.sw, 2, mx); dp_drag = 1; }
    return 1;
}
/* Up / Down in the colour list; Left / Right move the slider; Esc closes the question. 1 = used. */
static int dp_key(int k) {
    if (setting_page != SP_DISPLAY) return 0;
    if (dp_ask) { if (k == 27) { dp_ask = 0; dp_p_pos = disp_size_pos(); } return 1; }
    if (dp_tab == 0) {
        sheet_t g; dp_geo(&g); dp_lay_t l; dp_lay(&g, &l); dp_lb_sync(&l);
        return lbx_key(&dp_lb, k);
    }
    if (k == KEY_LEFT && dp_p_pos > 0) { dp_p_pos--; dp_msg = 0; return 1; }
    if (k == KEY_RIGHT && dp_p_pos < 2) { dp_p_pos++; dp_msg = 0; return 1; }
    if (k == KEY_LEFT || k == KEY_RIGHT) return 1;
    return 0;
}
static int dp_wheel(int notches, int mx, int my) {
    if (setting_page != SP_DISPLAY || dp_tab != 0 || dp_ask) return 0;
    sheet_t g; dp_geo(&g); if (!g.ok) return 0;
    dp_lay_t l; dp_lay(&g, &l); dp_lb_sync(&l);
    if (!in_rect(mx, my, l.lx, l.ly, l.lw, l.lh)) return 0;
    return lbx_wheel(&dp_lb, notches);
}

#endif
