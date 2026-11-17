/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_UI_CLOCK_POPUP_H
#define MW_UI_CLOCK_POPUP_H

/* ui/clock_popup.h -- what pops up above the taskbar clock: the analog clock face of the Date/Time sheet and, under it, the month's
 * calendar with today in blue (as Windows 95's was a date line, this one shows the whole month). Clicking anywhere on it opens
 * Control Panel > Date/Time; clicking anywhere else just closes it. Needs apps/setting_dt.h (dt_clock, dt_dow, dt_dim). */

typedef struct { int x, y, w, h, cx, cy, R, ty, hy, gy, gx, cw, ch, rows; } clockpop_geo_t;

static void clockpop_geo(clockpop_geo_t *g, int year, int month) {
    g->cw = 19; g->ch = 12;
    g->R = VGA_HEIGHT >= 400 ? 36 : 26;
    g->w = g->cw * 7 + 16;
    g->rows = (dt_dow(year, month, 1) + dt_dim(year, month) + 6) / 7;
    g->h = 8 + (2 * g->R + 4) + 6 + 12 + 2 + 13 + g->rows * g->ch + 6;
    g->x = CLOCK_X + CLOCK_W - g->w; if (g->x < 2) g->x = 2;
    g->y = TASKBAR_Y - g->h; if (g->y < 0) g->y = 0;
    g->cx = g->x + g->w / 2; g->cy = g->y + 8 + g->R + 2;
    g->ty = g->cy + g->R + 6;                   /* "November 2026" */
    g->hy = g->ty + 14;                         /* the weekday bar */
    g->gy = g->hy + 13;                         /* the day grid */
    g->gx = g->x + (g->w - g->cw * 7) / 2;
}

static void clockpop_now(rtc_time_t *l) { rtc_time_t now; rtc_read(&now); *l = tz_local(now); }

static int clockpop_hit(int px, int py) {
    rtc_time_t l; clockpop_now(&l);
    clockpop_geo_t g; clockpop_geo(&g, l.year, l.month);
    return in_rect(px, py, g.x, g.y, g.w, g.h);
}

static void draw_date_popup(void) {
    rtc_time_t l; clockpop_now(&l);
    clockpop_geo_t g; clockpop_geo(&g, l.year, l.month);
    ui_panel(g.x, g.y, g.w, g.h, TH_WINDOW_SHADOW);
    dt_clock(g.cx, g.cy, g.R, &l);

    char buf[40]; u32 n = 0; buf[0] = 0;
    if (sys_language == LANG_KOREAN) {
        append_uint(buf, &n, sizeof buf, l.year); append_str(buf, &n, sizeof buf, "\xeb\x85\x84 ");
        append_uint(buf, &n, sizeof buf, l.month); append_str(buf, &n, sizeof buf, "\xec\x9b\x94");
    } else {
        append_str(buf, &n, sizeof buf, month_names_en[l.month - 1]); append_str(buf, &n, sizeof buf, " "); append_uint(buf, &n, sizeof buf, l.year);
    }
    ko_draw_mixed_string(g.x + (g.w - ko_string_width(buf)) / 2, g.ty, buf, TH_TEXT);

    bb_fillrect(g.gx, g.hy, g.cw * 7, 13, TH_SHADOW);
    static const char *const wd_en[7] = { "S", "M", "T", "W", "T", "F", "S" };
    static const char *const wd_ko[7] = { "\xec\x9d\xbc", "\xec\x9b\x94", "\xed\x99\x94", "\xec\x88\x98", "\xeb\xaa\xa9", "\xea\xb8\x88", "\xed\x86\xa0" };
    for (int c = 0; c < 7; c++) { const char *s = SKO(wd_en[c], wd_ko[c]); ko_draw_mixed_string(g.gx + c * g.cw + (g.cw - ko_string_width(s)) / 2, g.hy, s, TH_LIGHT); }
    bb_fillrect(g.gx, g.gy, g.cw * 7, g.ch * g.rows, TH_FIELD);
    int first = dt_dow(l.year, l.month, 1), dim = dt_dim(l.year, l.month);
    for (int day = 1; day <= dim; day++) {
        int cell = first + day - 1, c = cell % 7, r = cell / 7;
        int x = g.gx + c * g.cw, y = g.gy + r * g.ch;
        char ds[4]; u32 k = 0; ds[0] = 0; append_uint(ds, &k, sizeof ds, day);
        int today = (day == l.day);
        if (today) bb_fillrect(x + 1, y, g.cw - 2, g.ch, TH_ACCENT);              /* today: the blue block */
        ko_draw_mixed_string(x + g.cw - ko_string_width(ds) - 3, y, ds, today ? TH_TEXT_INVERSE : (c == 0 ? COL_RED : TH_TEXT));
    }
}

#endif
