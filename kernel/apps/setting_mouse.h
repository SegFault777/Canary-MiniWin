/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_APPS_SETTING_MOUSE_H
#define MW_APPS_SETTING_MOUSE_H

/* apps/setting_mouse.h -- the Control Panel's "Mouse Properties" sheet (1.1-alpha-14), drawn like the Windows 95 one: four tabs, and OK / Cancel / Apply.
 *
 *   Buttons   Right-handed / Left-handed (the physical buttons really are swapped: mouse.h mouse_left_handed), what each button does, the picture of the mouse
 *             with the primary button lit, the double-click speed slider and the TEST AREA (double-click the box: the jack-in-the-box pops up if you were
 *             fast enough for the speed that is set on the slider, whether or not it has been applied yet).
 *   Pointers  every pointer the OS has, by role, with its picture; the selected one is shown large at the top right. Roles the icon pack has no pointer for
 *             (Precision Select, Alternate Select) show a question mark. The Scheme box, Save As / Delete / Use Default / Browse are drawn but disabled: there
 *             are no pointer files to load and no schemes to save yet.
 *   Motion    Pointer speed (the slider has the four speeds of mouse.h) and Pointer trails (cursor_trail_n in ui/cursor.h: a check box and a length slider).
 *   General   the name of the mouse the OS found (and a disabled Change button).
 *
 * Like the Date/Time sheet (setting_dt.h) it edits a COPY of the settings (ms_p_*): Apply copies it into the live variables, OK applies and goes back to the
 * icons, Cancel throws the copy away. It is built from the shared UI modules -- the frame, tabs and OK / Cancel / Apply row of ui/sheet.h, the group boxes,
 * buttons, radio buttons and sliders of ui/controls.h, the list of pointers of ui/listbox.h -- and keeps only what is particular to a mouse: the
 * layout of each tab, the test area and the settings themselves. Included by apps/setting.h after setting_dt.h (it uses the window geometry). */

#include "../ui/mouse_pics.h"                /* the three 64x64 mouse pictures (tools/gen_mouse_pics.py) */

static int ms_tab = 0;                       /* 0 Buttons, 1 Pointers, 2 Motion, 3 General */
static int ms_p_left = 0;                    /* pending: left-handed */
static int ms_p_dbl = 2;                     /* pending: double-click speed 0 (slow) .. 4 (fast) */
static int ms_p_speed = 1;                   /* pending: pointer speed 0 .. 3 (mouse.h) */
static int ms_p_trail_on = 0, ms_p_trail = 2;     /* pending: trails on?, their length 0 (short) .. 4 (long) */
static int ms_btn_pressed = 0;               /* the button the mouse went down on (1 OK, 2 Cancel, 3 Apply), 0 = none: it fires on release */
static int ms_drag = 0;                      /* the slider being dragged: 1 double-click, 2 pointer speed, 3 trail length */
static int ms_test_open = 0;                 /* the test area: 1 = the jack-in-the-box is out */
static u32 ms_test_tick = 0; static int ms_test_wait = 0;      /* ... and the first click of a possible double click */
static dd_t ms_dd;                           /* the Scheme box (one entry: "(None)") */
static const char *ms_dd_text(int i) { (void)i; return SKO("(None)", "(없음)"); }

static const u32 ms_dbl_ticks[5] = { 90000, 75000, 60000, 48000, 36000 };      /* double_click_window for the slider's five positions (60000 is the default) */
static const u8 ms_trail_len[5] = { 2, 3, 4, 6, 8 };

static int ms_dbl_pos_of(u32 w) { int best = 2, bd = 1 << 30; for (int i = 0; i < 5; i++) { int d = (int)w - (int)ms_dbl_ticks[i]; if (d < 0) d = -d; if (d < bd) { bd = d; best = i; } } return best; }
static int ms_trail_pos_of(int n) { int best = 2, bd = 1 << 30; for (int i = 0; i < 5; i++) { int d = n - ms_trail_len[i]; if (d < 0) d = -d; if (d < bd) { bd = d; best = i; } } return best; }

/* The pointer roles (Windows' own list), the OS's cursor for each, or -1 = the icon pack has none yet. */
static const struct { const char *en, *ko; int cur; } ms_roles[] = {
    { "Normal Select",        "기본 선택",         MW_CURSOR_ARROW },
    { "Help Select",          "도움말 선택",       MW_CURSOR_HELP },
    { "Working In Background", "백그라운드 작업",  MW_CURSOR_WAIT_ARROW },
    { "Busy",                 "사용 중",           MW_CURSOR_WAIT },
    { "Precision Select",     "정밀도 선택",       -1 },
    { "Text Select",          "텍스트 선택",       MW_CURSOR_IBEAM },
    { "Handwriting",          "필기",              MW_CURSOR_PEN },
    { "Unavailable",          "사용할 수 없음",    MW_CURSOR_NO },
    { "Vertical Resize",      "수직 크기 조절",    MW_CURSOR_VRESIZE },
    { "Horizontal Resize",    "수평 크기 조절",    MW_CURSOR_HRESIZE },
    { "Diagonal Resize 1",    "대각선 크기 조절 1", MW_CURSOR_DIAG_NWSE },
    { "Diagonal Resize 2",    "대각선 크기 조절 2", MW_CURSOR_DIAG_NESW },
    { "Move",                 "이동",              MW_CURSOR_MOVE },
    { "Alternate Select",     "대체 선택",         -1 },
    { "Link Select",          "연결 선택",         MW_CURSOR_HAND },
};
#define MS_ROLES ((int)(sizeof ms_roles / sizeof ms_roles[0]))
#define MS_ROW_H 26
static lbx_t ms_lb = { 0, 0, 0, 0, MS_ROW_H, MS_ROLES, 0, 0 };    /* the Pointers tab's list of roles (ui/listbox.h): the selected role and the first one shown live in it */

/* ---- geometry ---- */
static void ms_geo(sheet_t *g) { sheet_geo(g, setting.x, setting_body_y(), setting.w, setting_status_y(), 330, 232); }   /* smaller than this the sheet does not fit: a hint is shown instead */
static const char *ms_tab_label(int i) {
    static const char *const en[4] = { "Buttons", "Pointers", "Motion", "General" };
    static const char *const ko[4] = { "단추", "포인터", "동작", "일반" };
    return SKO(en[i], ko[i]);
}

/* The pieces of each tab (one function, so drawing and clicking can never disagree). */
typedef struct {
    int g1x, g1y, g1w, g1h;                                               /* Buttons: the Button configuration group */
    int r1x, r2x, ry;                                                     /*   its two radio buttons (13 px) */
    int g2x, g2y, g2w, g2h;                                               /*   the Double-click speed group */
    int sx, sy, sw;                                                       /*   the slider (Buttons) / the speed slider (Motion) */
    int tx, ty, tw, th;                                                   /*   the test area box */
    int s2x, s2y, s2w;                                                    /* Motion: the trail slider */
    int cbx, cby;                                                         /*   the "Show pointer trails" box */
    int g3x, g3y, g3w, g3h;                                               /*   the Pointer trail group */
    int lx, ly, lw, lh;                                                   /* Pointers: the list; its scroll bar is the right 16 px */
    int bx, by;                                                           /*   the buttons under the list */
    int ddx, ddy, ddw;                                                    /*   the scheme box */
    int pvx, pvy, pvw, pvh;                                               /*   the preview box */
} ms_lay_t;
static void ms_lay(const sheet_t *g, ms_lay_t *l) {
    int *p = (int *)l; for (unsigned i = 0; i < sizeof *l / sizeof(int); i++) p[i] = 0;
    /* Buttons */
    l->g1x = g->px + 10; l->g1y = g->py + 16; l->g1w = g->pw - 20; l->g1h = 110;
    l->ry = l->g1y + 11; l->r1x = l->g1x + 12; l->r2x = l->g1x + l->g1w / 2 - 6;
    l->g2x = l->g1x; l->g2y = l->g1y + l->g1h + 16; l->g2w = l->g1w; l->g2h = g->py + g->ph - 8 - l->g2y; if (l->g2h > 100) l->g2h = 100;
    l->tw = 74; l->th = 56; l->tx = l->g2x + l->g2w - l->tw - 10; l->ty = l->g2y + 26;
    l->sx = l->g2x + 44; l->sy = l->g2y + 28; l->sw = l->tx - 12 - 34 - l->sx;
    /* Motion */
    if (ms_tab == 2) {
        l->g1x = g->px + 10; l->g1y = g->py + 16; l->g1w = g->pw - 20; l->g1h = 82;
        l->sx = l->g1x + 92; l->sy = l->g1y + 30; l->sw = l->g1w - 92 - 56 - 20;
        l->g3x = l->g1x; l->g3y = l->g1y + l->g1h + 18; l->g3w = l->g1w; l->g3h = 104;
        l->cbx = l->g3x + 70; l->cby = l->g3y + 14;
        l->s2x = l->g3x + 92; l->s2y = l->g3y + 52; l->s2w = l->g3w - 92 - 50 - 20;
    }
    /* Pointers */
    l->ddx = g->px + 14; l->ddy = g->py + 24; l->ddw = g->pw - 14 - 14 - 110;
    l->pvw = 92; l->pvh = 64; l->pvx = g->px + g->pw - 14 - l->pvw; l->pvy = g->py + 10;
    l->lx = g->px + 14; l->ly = g->py + 82; l->lw = g->pw - 28; l->lh = ((g->py + g->ph - 40 - l->ly) / MS_ROW_H) * MS_ROW_H + 2;
    l->bx = g->px + g->pw - 14; l->by = l->ly + l->lh + 8;
}
/* The list box follows the layout (and the layout is recomputed from the window each time, so this runs before every draw and every click). */
static void ms_lb_sync(const ms_lay_t *l) { ms_lb.x = l->lx; ms_lb.y = l->ly; ms_lb.w = l->lw; ms_lb.h = l->lh; ms_lb.count = MS_ROLES; }

/* ---- drawing helpers (the generic ones are in ui/controls.h) ---- */
static void ms_blit64(int x, int y, int pic) {
    for (int j = 0; j < MOUSE_PIC_H; j++) for (int i = 0; i < MOUSE_PIC_W; i++) {
        unsigned char c = mouse_pic[pic][j * MOUSE_PIC_W + i];
        if (c != MOUSE_PIC_TRANSPARENT) bb_putpixel(x + i, y + j, mouse_pic_pal[c]);
    }
}
static void ms_disc(int cx, int cy, int r, u32 col) { for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i * i + j * j <= r * r) bb_putpixel(cx + i, cy + j, col); }
/* The test area: a box (closed) or the jack-in-the-box with its spring and head (out). */
static void ms_draw_test(int x, int y, int w, int h) {
    ui_sunken(x, y, w, h);                                                              /* a sunken box */
    int cx = x + w / 2, by = y + h - 8;
    if (!ms_test_open) {
        bb_fillrect(cx - 13, by - 18, 26, 18, 0xFFFF00FFu); bb_rect(cx - 13, by - 18, 26, 18, TH_TEXT);
        bb_fillrect(cx - 15, by - 23, 30, 6, 0xFF800080u); bb_rect(cx - 15, by - 23, 30, 6, TH_TEXT);
    } else {
        bb_fillrect(cx - 13, by - 18, 26, 18, 0xFFFF00FFu); bb_rect(cx - 13, by - 18, 26, 18, TH_TEXT);
        for (int k = 0; k < 6; k++) { int sx = cx + ((k & 1) ? 4 : -4); ui_line(sx, by - 18 - k * 4, cx + ((k & 1) ? -4 : 4), by - 22 - k * 4, TH_TEXT); }   /* the spring */
        ms_disc(cx, by - 46, 8, 0xFFFFFF00u);                                           /* the head */
        bb_putpixel(cx - 3, by - 48, TH_TEXT); bb_putpixel(cx + 3, by - 48, TH_TEXT);
        ui_hline(cx - 3, by - 43, 7, TH_TEXT);
        bb_fillrect(cx - 15, by - 26, 30, 5, 0xFF800080u); bb_rect(cx - 15, by - 26, 30, 5, TH_TEXT);   /* the lid, knocked up */
    }
}

/* One row of the Pointers list (called by lbx_draw inside the list's clip): the role's name on the left, its pointer picture (or a question mark) at the right edge. */
static void ms_row_draw(int idx, int x, int y, int w, int h, int selected) {
    (void)h;
    ko_draw_mixed_string(x + 7, y + (MS_ROW_H - 11) / 2, SKO(ms_roles[idx].en, ms_roles[idx].ko), selected ? TH_TEXT_INVERSE : TH_TEXT);
    int c = ms_roles[idx].cur, px = x + w - 1 - 28;
    if (c >= 0) { const cursor_shape_t *s = &cursor_shapes[c]; draw_cursor_shape(px + (28 - s->w) / 2, y + (MS_ROW_H - s->h) / 2, s->rows, s->h); }
    else ic16_blit(px + 6, y + (MS_ROW_H - 16) / 2, IC_HELP);                          /* no pointer for this role in the icon pack yet */
}

/* ---- are there changes to apply? ---- */
static int ms_dirty(void) {
    return ms_p_left != mouse_left_handed || ms_dbl_ticks[ms_p_dbl] != double_click_window || ms_p_speed != (mouse_speed & 3) ||
           (ms_p_trail_on ? ms_trail_len[ms_p_trail] : 0) != cursor_trail_n;
}
static void ms_apply(void) {
    mouse_left_handed = ms_p_left;
    double_click_window = ms_dbl_ticks[ms_p_dbl];
    mouse_speed = ms_p_speed;
    cursor_trail_n = ms_p_trail_on ? ms_trail_len[ms_p_trail] : 0;
}
static void ms_open(void) {
    ms_p_left = mouse_left_handed; ms_p_dbl = ms_dbl_pos_of(double_click_window); ms_p_speed = mouse_speed & 3;
    ms_p_trail_on = cursor_trail_n > 0; ms_p_trail = ms_trail_pos_of(cursor_trail_n > 0 ? cursor_trail_n : 4);
    ms_tab = 0; ms_btn_pressed = 0; ms_drag = 0; ms_lb.sel = 0; ms_lb.top = 0; lbx_detach(&ms_lb); ms_test_open = 0; ms_test_wait = 0;
    dd_init(&ms_dd, 1, ms_dd_text, 1); dd_close(&ms_dd); ms_dd.sel = 0;
}

/* ---- the picture ---- */
static void ms_draw(void) {
    sheet_t g; ms_geo(&g);
    if (!g.ok) { sheet_hint_draw(&g, setting.x, setting_body_y(), ms_btn_pressed); return; }
    ms_lay_t l; ms_lay(&g, &l);
    sheet_draw_frame(&g, ms_tab_label, 4, ms_tab);
    if (ms_tab == 0) {
        ui_group(l.g1x, l.g1y, l.g1w, l.g1h, SKO("Button configuration", "단추 구성"));
        ui_radio(l.r1x, l.ry, !ms_p_left);  ui_label(l.r1x + 18, l.ry, SKO("Right-handed", "오른손잡이용"), 1);
        ui_radio(l.r2x, l.ry, ms_p_left);   ui_label(l.r2x + 18, l.ry, SKO("Left-handed", "왼손잡이용"), 1);
        int ty = l.g1y + 36;
        ui_label(l.g1x + 12, ty, SKO(ms_p_left ? "Right Button:" : "Left Button:", ms_p_left ? "오른쪽 단추:" : "왼쪽 단추:"), 1);
        ui_label(l.g1x + 12, ty + 14, SKO("- Normal Select", "- 기본 선택"), 1);
        ui_label(l.g1x + 12, ty + 28, SKO("- Normal Drag", "- 기본 끌기"), 1);
        const char *r0 = SKO(ms_p_left ? "Left Button:" : "Right Button:", ms_p_left ? "왼쪽 단추:" : "오른쪽 단추:");
        const char *r1 = SKO("- Context Menu", "- 바로 가기 메뉴"), *r2 = SKO("- Special Drag", "- 특수 끌기");
        int rw = ko_string_width(r0); if (ko_string_width(r1) > rw) rw = ko_string_width(r1); if (ko_string_width(r2) > rw) rw = ko_string_width(r2);
        int rx = l.g1x + l.g1w - 12 - rw;
        ui_label(rx, ty, r0, 1); ui_label(rx, ty + 14, r1, 1); ui_label(rx, ty + 28, r2, 1);
        ms_blit64(l.g1x + l.g1w / 2 - 32, l.g1y + 34, ms_p_left ? 2 : 1);              /* the primary button is the lit one */
        ui_group(l.g2x, l.g2y, l.g2w, l.g2h, SKO("Double-click speed", "두 번 누르기 속도"));
        ui_label(l.g2x + 10, l.sy + 4, SKO("Slow", "느리게"), 1);
        ui_slider(l.sx, l.sy, l.sw, ms_p_dbl, 4, 1);
        ui_label(l.sx + l.sw + 8, l.sy + 4, SKO("Fast", "빠르게"), 1);
        ui_label(l.tx, l.g2y + 8, SKO("Test area:", "시험 영역:"), 1);
        ms_draw_test(l.tx, l.ty, l.tw, l.th);
    } else if (ms_tab == 1) {
        ui_label(g.px + 14, g.py + 10, SKO("Scheme", "구성표"), 1);
        dd_place(&ms_dd, l.ddx, l.ddy, l.ddw); dd_draw(&ms_dd);
        ui_pushbutton(l.ddx, l.ddy + 26, 84, 22, SKO("Save As...", "다른 이름으로..."), 0, 0);
        ui_pushbutton(l.ddx + 92, l.ddy + 26, 70, 22, SKO("Delete", "삭제"), 0, 0);
        ui_sunken(l.pvx, l.pvy, l.pvw, l.pvh);                                                  /* the preview of the selected pointer */
        { int c = ms_roles[ms_lb.sel].cur;
          if (c >= 0) { const cursor_shape_t *s = &cursor_shapes[c]; draw_cursor_shape(l.pvx + l.pvw / 2 - s->w / 2, l.pvy + l.pvh / 2 - s->h / 2, s->rows, s->h); }
          else ic16_blit(l.pvx + l.pvw / 2 - 8, l.pvy + l.pvh / 2 - 8, IC_HELP); }
        ms_lb_sync(&l); lbx_draw(&ms_lb, ms_row_draw);                              /* the list of roles, with the standard scroll bar */
        ui_pushbutton(l.bx - 84 - 8 - 96, l.by, 96, 22, SKO("Use Default", "기본값 사용"), 0, 0);
        ui_pushbutton(l.bx - 84, l.by, 84, 22, SKO("Browse...", "찾아보기..."), 0, 0);
        dd_draw_list(&ms_dd);                                                          /* last: the open list covers what is under it */
    } else if (ms_tab == 2) {
        ui_group(l.g1x, l.g1y, l.g1w, l.g1h, SKO("Pointer speed", "포인터 속도"));
        { const u32 *ic = sheet_icon32("CURSOR_SPEED"); if (ic) blit_rgba32(l.g1x + 14, l.g1y + 22, ic); }
        ui_label(l.g1x + 54, l.sy + 4, SKO("Slow", "느리게"), 1);
        ui_slider(l.sx, l.sy, l.sw, ms_p_speed, 3, 1);
        ui_label(l.sx + l.sw + 8, l.sy + 4, SKO("Fast", "빠르게"), 1);
        ui_group(l.g3x, l.g3y, l.g3w, l.g3h, SKO("Pointer trail", "포인터 자국"));
        { const u32 *ic = sheet_icon32("CURSOR_MOVE"); if (ic) blit_rgba32(l.g3x + 14, l.g3y + 22, ic); }
        ui_checkbox(l.cbx, l.cby, ms_p_trail_on);
        ui_label(l.cbx + 18, l.cby + 1, SKO("Show pointer trails", "포인터 자국 표시"), 1);
        ui_label(l.g3x + 54, l.s2y + 4, SKO("Short", "짧게"), ms_p_trail_on);
        ui_slider(l.s2x, l.s2y, l.s2w, ms_p_trail, 4, ms_p_trail_on);
        ui_label(l.s2x + l.s2w + 8, l.s2y + 4, SKO("Long", "길게"), ms_p_trail_on);
    } else {
        { const u32 *ic = sheet_icon32("MOUSE"); if (ic) blit_rgba32(g.px + 16, g.py + 14, ic); }
        ui_label(g.px + 16, g.py + 62, SKO("Name:", "이름:"), 1);
        const char *nm = mouse_abs_valid ? SKO("Absolute pointing device (tablet / virtual)", "절대 좌표 포인터 장치 (태블릿/가상)") : (mouse_packet_len == 4 ? SKO("Standard PS/2 Port Mouse (wheel)", "표준 PS/2 포트 마우스 (휠)") : SKO("Standard PS/2 Port Mouse", "표준 PS/2 포트 마우스"));
        int fw = g.pw - 32 - 96, fx = g.px + 16, fy = g.py + 78;
        ui_field(fx, fy, fw, 18, TH_FIELD, 0);
        bb_fillrect(fx + 3, fy + 3, ko_string_width(nm) + 2 < fw - 6 ? ko_string_width(nm) + 2 : fw - 6, 13, TH_ACCENT);        /* the name, selected, as in Windows */
        bb_clip_set(fx + 2, fy + 2, fw - 4, 14);
        ko_draw_mixed_string(fx + 4, fy + 4, nm, TH_TEXT_INVERSE);
        bb_clip_off();
        ui_pushbutton(g.px + g.pw - 16 - 80, fy - 2, 80, 22, SKO("Change...", "변경..."), 0, 0);
    }
    sheet_footer_draw(&g, ms_btn_pressed, ms_dirty());
}

/* ---- input ---- */
static int ms_btn_hit(int mx, int my) {
    sheet_t g; ms_geo(&g);
    return g.ok ? sheet_footer_hit(&g, mx, my, ms_dirty()) : sheet_hint_hit(&g, mx, my);
}
static void ms_fire(int b) {
    if (b == 1) { ms_apply(); setting_page = -1; setting_armed = -1; lbx_detach(&ms_lb); }
    else if (b == 2) { setting_page = -1; setting_armed = -1; lbx_detach(&ms_lb); }
    else if (b == 3) ms_apply();
}
static void ms_release(int mx, int my) {
    int b = ms_btn_pressed;
    ms_btn_pressed = 0; ms_drag = 0;
    if (b && ms_btn_hit(mx, my) == b) ms_fire(b);
}
/* While a slider is held the pointer drags it (called every frame by the input loop with the pointer and the left button). */
static void ms_poll_drag(int mx, int left_now) {
    if (setting_page != SP_MOUSE || !ms_drag) return;
    if (!left_now) { ms_drag = 0; return; }
    sheet_t g; ms_geo(&g); ms_lay_t l; ms_lay(&g, &l);
    if (ms_drag == 1) ms_p_dbl = ui_slider_pos(l.sx, l.sw, 4, mx);
    else if (ms_drag == 2) ms_p_speed = ui_slider_pos(l.sx, l.sw, 3, mx);
    else if (ms_drag == 3 && ms_p_trail_on) ms_p_trail = ui_slider_pos(l.s2x, l.s2w, 4, mx);
}
static int ms_click(int mx, int my, u32 now) {
    sheet_t g; ms_geo(&g);
    int b = ms_btn_hit(mx, my);
    if (b) { ms_btn_pressed = b; return 1; }
    if (!g.ok) return 1;
    ms_lay_t l; ms_lay(&g, &l);
    if (ms_tab == 1) {                                                    /* the scheme box takes its clicks first */
        dd_place(&ms_dd, l.ddx, l.ddy, l.ddw); ms_dd.sel = 0;
        if (dd_click(&ms_dd, mx, my)) return 1;
    }
    { int t = sheet_tab_at(&g, ms_tab_label, 4, mx, my);
      if (t >= 0) { ms_tab = t; ms_drag = 0; dd_close(&ms_dd); if (t != 1) lbx_detach(&ms_lb); return 1; } }
    if (ms_tab == 0) {
        if (in_rect(mx, my, l.r1x, l.ry - 2, l.r2x - l.r1x - 8, 16)) ms_p_left = 0;
        else if (in_rect(mx, my, l.r2x, l.ry - 2, l.g1x + l.g1w - l.r2x - 8, 16)) ms_p_left = 1;
        else if (in_rect(mx, my, l.sx, l.sy - 2, l.sw, 28)) { ms_p_dbl = ui_slider_pos(l.sx, l.sw, 4, mx); ms_drag = 1; }
        else if (in_rect(mx, my, l.tx, l.ty, l.tw, l.th)) {                /* the test area: two clicks within the PENDING speed pop the jack out (or put it back) */
            if (ms_test_wait && (now - ms_test_tick) < ms_dbl_ticks[ms_p_dbl]) { ms_test_open = !ms_test_open; ms_test_wait = 0; }
            else { ms_test_wait = 1; ms_test_tick = now; }
        }
    } else if (ms_tab == 1) {
        ms_lb_sync(&l); lbx_click(&ms_lb, mx, my);                           /* a row selects it; the scroll bar's arrows, track and thumb work through ui/scrollbar.h */
    } else if (ms_tab == 2) {
        if (in_rect(mx, my, l.sx, l.sy - 2, l.sw, 28)) { ms_p_speed = ui_slider_pos(l.sx, l.sw, 3, mx); ms_drag = 2; }
        else if (in_rect(mx, my, l.cbx, l.cby - 2, l.g3w - 80, 16)) ms_p_trail_on = !ms_p_trail_on;
        else if (ms_p_trail_on && in_rect(mx, my, l.s2x, l.s2y - 2, l.s2w, 28)) { ms_p_trail = ui_slider_pos(l.s2x, l.s2w, 4, mx); ms_drag = 3; }
    }
    return 1;
}
/* The keyboard on the Pointers tab: Up/Down move the selection (and the list follows it). */
static int ms_key(int k) {
    if (ms_tab != 1) return 0;
    sheet_t g; ms_geo(&g); ms_lay_t l; ms_lay(&g, &l); ms_lb_sync(&l);
    return lbx_key(&ms_lb, k);
}
/* The wheel with the pointer over the Pointers list scrolls it (called from the input loop; 1 = used). */
static int ms_wheel(int notches, int mx, int my) {
    if (setting_page != SP_MOUSE || ms_tab != 1) return 0;
    sheet_t g; ms_geo(&g); if (!g.ok) return 0;
    ms_lay_t l; ms_lay(&g, &l); ms_lb_sync(&l);
    if (!in_rect(mx, my, l.lx, l.ly, l.lw, l.lh)) return 0;
    return lbx_wheel(&ms_lb, notches);
}

#endif
