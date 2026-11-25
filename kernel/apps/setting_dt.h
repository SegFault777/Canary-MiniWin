/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_APPS_SETTING_DT_H
#define MW_APPS_SETTING_DT_H

/* apps/setting_dt.h -- the Control Panel's "Date/Time Properties" sheet (1.1-alpha-14), drawn like the Windows 95 one: two tabs
 * (Date & Time, Time Zone -- a drop-down list of zones, a world map and the daylight-saving check box, as in Windows 95), a Date group with a month calendar, a Time group with an analog clock and the time in digits, and the
 * OK / Cancel / Apply buttons along the bottom.
 *
 * What it can and cannot do. The clock and the calendar SHOW the machine's time in the chosen zone. The zone and the 12/24-hour
 * choice are edited on a copy (dt_p_zone, dt_p_dst, dt_p_24): Apply copies them into tz_zone / tz_dst_auto / clock_24h, OK applies and goes back to the
 * icons, Cancel throws the copy away. The date and the time themselves are NOT set -- the CMOS clock is only read (kernel/rtc.h) --
 * so clicking a day only highlights it, like any date picker that has nothing to submit.
 *
 * Built from the shared UI modules: the frame, tabs and OK / Cancel / Apply row of ui/sheet.h, the group boxes and buttons of ui/controls.h (and the zone
 * drop-down of the same file). Included by apps/setting.h after the window geometry (it uses setting_body_y() and setting_status_y()). */

static int dt_tab = 0;                 /* 0 = Date & Time, 1 = Time Zone */
static int dt_vy = 2026, dt_vm = 1;    /* the month the calendar shows */
static int dt_sel = 0;                 /* the highlighted day of that month, 0 = none */
static int dt_btn_pressed = 0;         /* the button the mouse went down on (1 OK, 2 Cancel, 3 Apply, 5 '<', 6 '>'), 0 = none: it fires on release */
static int dt_p_zone = 0, dt_p_dst = 1, dt_p_24 = 0;   /* the pending (not yet applied) zone (index into tz_zones), daylight-saving box and clock style */
static dd_t dt_dd;                                     /* the zone drop-down (ui/controls.h) */
static const char *dt_zone_name(int i) { return tz_zones[i].name; }

static void dt_geo(sheet_t *g) { sheet_geo(g, setting.x, setting_body_y(), setting.w, setting_status_y(), 280, 160); }   /* smaller than this the sheet does not fit: a hint is shown instead */

static int dt_dim(int y, int m) {
    static const u8 d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return d[m - 1] + (m == 2 && leap);
}
static int dt_dow(int y, int m, int d) {                                /* 0 = Sunday (Sakamoto) */
    static const u8 t[12] = { 0, 3, 2, 5, 0, 3, 5, 2, 6, 4, 1, 3 };
    if (m < 3) y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

/* sin(6 * k degrees) * 1000 for k = 0..59 (the clock's 60 minute positions), from a quarter-wave table */
static int dt_sin60(int k) {
    static const short s[16] = { 0, 105, 208, 309, 407, 500, 588, 669, 743, 809, 866, 914, 951, 978, 995, 1000 };
    k %= 60; if (k < 0) k += 60;
    int q = k / 15, r = k % 15;
    return q == 0 ? s[r] : q == 1 ? s[15 - r] : q == 2 ? -s[r] : -s[15 - r];
}


static void dt_today(int *y, int *m, int *d) {
    rtc_time_t now; rtc_read(&now);
    rtc_time_t l = tz_local_for(now, dt_p_zone, dt_p_dst);
    *y = l.year; *m = l.month; *d = l.day;
}

static void dt_open(void) {
    int y, m, d;
    dt_tab = 0; dt_p_zone = tz_zone; dt_p_dst = tz_dst_auto; dt_p_24 = clock_24h; dt_btn_pressed = 0; dd_init(&dt_dd, TZ_N, dt_zone_name, 9); dt_dd.sel = dt_p_zone;
    dt_today(&y, &m, &d);
    dt_vy = y; dt_vm = m; dt_sel = d;
}
static void dt_apply(void) { tz_zone = dt_p_zone; tz_dst_auto = dt_p_dst; clock_24h = dt_p_24; syscfg_touch(); }
static int  dt_dirty(void) { return dt_p_zone != tz_zone || dt_p_dst != tz_dst_auto || dt_p_24 != clock_24h; }
static void dt_back(void)  { setting_page = -1; setting_armed = -1; dt_btn_pressed = 0; dd_close(&dt_dd); }

static const struct { const char *en, *ko; } dt_months[12] = {
    { "January", "1월" }, { "February", "2월" }, { "March", "3월" }, { "April", "4월" }, { "May", "5월" }, { "June", "6월" },
    { "July", "7월" }, { "August", "8월" }, { "September", "9월" }, { "October", "10월" }, { "November", "11월" }, { "December", "12월" },
};

static const char *dt_tab_label(int i) {
    return i == 0 ? SKO("Date & Time", "날짜 및 시간") : SKO("Time Zone", "표준 시간대");
}

/* the clock face: 60 dots, 12 squares, hour / minute hands, a thin red second hand */
static void dt_clock(int cx, int cy, int R, const rtc_time_t *t) {
    for (int k = 0; k < 60; k++) {
        int x = cx + (R * dt_sin60(k)) / 1000, y = cy - (R * dt_sin60(k + 15)) / 1000;
        if (k % 5 == 0) bb_fillrect(x - 1, y - 1, 3, 3, TH_ACCENT);
        else bb_putpixel(x, y, TH_SHADOW);
    }
    int hi = (t->hour % 12) * 5 + t->minute / 12, mi = t->minute, si = t->second;
    int hl = R * 5 / 10, ml = R * 8 / 10, sl = R * 9 / 10;
    int hx = cx + (hl * dt_sin60(hi)) / 1000, hy = cy - (hl * dt_sin60(hi + 15)) / 1000;
    int mx2 = cx + (ml * dt_sin60(mi)) / 1000, my2 = cy - (ml * dt_sin60(mi + 15)) / 1000;
    int sx = cx + (sl * dt_sin60(si)) / 1000, sy = cy - (sl * dt_sin60(si + 15)) / 1000;
    ui_line(cx, cy, hx, hy, TH_TEXT);  ui_line(cx + 1, cy, hx + 1, hy, TH_TEXT);
    ui_line(cx, cy, mx2, my2, TH_TEXT); ui_line(cx, cy + 1, mx2, my2 + 1, TH_TEXT);
    ui_line(cx, cy, sx, sy, COL_RED);
    bb_fillrect(cx - 1, cy - 1, 3, 3, TH_TEXT);
}

static void dt_time_text(char *o, u32 sz, const rtc_time_t *t) {
    u32 l = 0; o[0] = 0;
    int h = t->hour, pm = h >= 12;
    if (!dt_p_24) { h %= 12; if (h == 0) h = 12; }
    append_uint(o, &l, sz, h); append_str(o, &l, sz, ":");
    if (t->minute < 10) append_str(o, &l, sz, "0");
    append_uint(o, &l, sz, t->minute); append_str(o, &l, sz, ":");
    if (t->second < 10) append_str(o, &l, sz, "0");
    append_uint(o, &l, sz, t->second);
    if (!dt_p_24) append_str(o, &l, sz, pm ? " PM" : " AM");
}

/* the calendar: geometry shared by drawing and clicking */
typedef struct { int gx, gy, gw, gh, cx0, cw, cy0, ch, tx, tw; } dt_date_t;
static void dt_date_geo(const sheet_t *g, dt_date_t *d) {
    int px = g->px + 8, py = g->py + 10, pw = g->pw - 16, ph = g->ph - 16 - 14;
    d->gx = px; d->gy = py; d->gw = pw * 58 / 100; d->gh = ph;
    d->cw = (d->gw - 16) / 7; d->cx0 = d->gx + (d->gw - d->cw * 7) / 2;
    d->cy0 = d->gy + 42; d->ch = 13;
    d->tx = d->gx + d->gw + 8; d->tw = pw - d->gw - 8;
}

/* ---- the Time Zone tab: [zone drop-down] / world map / [x] daylight saving ---- */
typedef struct { int cx, cy, cw, ch;  int mx0, my0, mw, mh;  int kx, ky; } dt_tzg_t;
static void dt_tz_geo(const sheet_t *g, dt_tzg_t *z) {
    z->cx = g->px + 8; z->cy = g->py + 8; z->cw = g->pw - 16; z->ch = DD_BOX_H;
    int avail = g->ph - 8 - z->ch - 8 - 24;
    int mw = z->cw, mh = mw * WORLDMAP_H / WORLDMAP_W;
    if (mh > avail) { mh = avail; mw = mh * WORLDMAP_W / WORLDMAP_H; }
    if (mh < 0) { mh = 0; mw = 0; }
    z->mw = mw; z->mh = mh; z->mx0 = g->px + (g->pw - mw) / 2; z->my0 = z->cy + z->ch + 8;
    z->kx = g->px + 12; z->ky = z->my0 + mh + 8;
}

/* The world map, a zone's band of longitude (15 degrees wide around its meridian) in the lighter colours -- as the Windows 95 one does. */
static void dt_draw_map(const dt_tzg_t *z, int zone) {
    if (z->mw <= 0 || z->mh <= 0) return;
    int m = tz_zones[zone].min;                                  /* minutes from UTC == quarter degrees of longitude */
    int b0 = ((m - 30 + 720) * z->mw) / 1440, b1 = ((m + 30 + 720) * z->mw) / 1440;
    if (b0 < 0) b0 = 0;
    if (b1 > z->mw) b1 = z->mw;
    bb_rect(z->mx0 - 1, z->my0 - 1, z->mw + 2, z->mh + 2, TH_SHADOW);
    for (int y = 0; y < z->mh; y++) {
        int sy = y * WORLDMAP_H / z->mh;
        const unsigned char *row = &worldmap_bits[sy * 32];
        for (int x = 0; x < z->mw; x++) {
            int sx = x * WORLDMAP_W / z->mw, land = (row[sx >> 3] >> (7 - (sx & 7))) & 1, in = x >= b0 && x < b1;
            bb_putpixel(z->mx0 + x, z->my0 + y, land ? (in ? COL_LGREEN : COL_GREEN) : (in ? COL_LBLUE : COL_BLUE));
        }
    }
}

/* The zone nearest in longitude to a click on the map. */
static int dt_zone_at_x(const dt_tzg_t *z, int px) {
    int q = ((px - z->mx0) * 1440) / (z->mw > 0 ? z->mw : 1) - 720, best = dt_p_zone, bd = 100000;
    for (int i = 0; i < TZ_N; i++) { int d = tz_zones[i].min - q; if (d < 0) d = -d; if (d < bd) { bd = d; best = i; } }
    if (tz_zones[best].min == tz_zones[dt_p_zone].min) return dt_p_zone;      /* same offset as now: keep the zone, don't jump to the first of the list */
    return best;
}

/* Where the "24-hour clock" box sits (Date & Time tab, bottom right). */
static void dt_24_geo(const sheet_t *g, int *x, int *y, int *w) {
    int lw = ko_string_width(SKO("24-hour clock", "24시간제"));
    *w = 18 + lw; *x = g->px + g->pw - 10 - *w; *y = g->py + g->ph - 17;
}

static void dt_draw(void) {
    sheet_t g; dt_geo(&g);
    if (!g.ok) { sheet_hint_draw(&g, setting.x, setting_body_y(), dt_btn_pressed); return; }
    sheet_draw_frame(&g, dt_tab_label, 2, dt_tab);                        /* the page, then the tabs on top of it */
    rtc_time_t now; rtc_read(&now);
    rtc_time_t t = tz_local_for(now, dt_p_zone, dt_p_dst);
    char buf[48]; u32 l;

    if (dt_tab == 0) {
        dt_date_t d; dt_date_geo(&g, &d);
        ui_group(d.gx, d.gy, d.gw, d.gh, SKO("Date", "날짜"));
        /* month row: [<]  April 2026  [>] */
        ui_pushbutton(d.gx + 8, d.gy + 12, 16, 16, "<", dt_btn_pressed == 5, 1);
        ui_pushbutton(d.gx + d.gw - 24, d.gy + 12, 16, 16, ">", dt_btn_pressed == 6, 1);
        l = 0; buf[0] = 0;
        append_str(buf, &l, sizeof buf, SKO(dt_months[dt_vm - 1].en, dt_months[dt_vm - 1].ko)); append_str(buf, &l, sizeof buf, " "); append_uint(buf, &l, sizeof buf, dt_vy);
        ko_draw_mixed_string(d.gx + (d.gw - ko_string_width(buf)) / 2, d.gy + 15, buf, TH_TEXT);
        /* weekday bar + days */
        bb_fillrect(d.cx0, d.cy0 - 14, d.cw * 7, 13, TH_SHADOW);
        static const char *const wd_en[7] = { "S", "M", "T", "W", "T", "F", "S" };
        static const char *const wd_ko[7] = { "일", "월", "화", "수", "목", "금", "토" };
        for (int c = 0; c < 7; c++) { const char *s = SKO(wd_en[c], wd_ko[c]); ko_draw_mixed_string(d.cx0 + c * d.cw + (d.cw - ko_string_width(s)) / 2, d.cy0 - 13, s, TH_LIGHT); }
        bb_fillrect(d.cx0, d.cy0, d.cw * 7, d.ch * 6, TH_FIELD);
        int first = dt_dow(dt_vy, dt_vm, 1), n = dt_dim(dt_vy, dt_vm);
        int ty, tm, td; dt_today(&ty, &tm, &td);
        for (int day = 1; day <= n; day++) {
            int cell = first + day - 1, c = cell % 7, r = cell / 7;
            int x = d.cx0 + c * d.cw, y = d.cy0 + r * d.ch;
            char ds[4]; u32 k = 0; ds[0] = 0; append_uint(ds, &k, sizeof ds, day);
            int tw = ko_string_width(ds);
            if (day == dt_sel) { bb_fillrect(x + 1, y, d.cw - 2, d.ch, TH_ACCENT); }
            ko_draw_mixed_string(x + d.cw - tw - 3, y + 1, ds, day == dt_sel ? TH_TEXT_INVERSE : (c == 0 ? COL_RED : TH_TEXT));
            if (dt_vy == ty && dt_vm == tm && day == td) bb_rect(x + 1, y, d.cw - 2, d.ch, TH_TEXT);   /* today has a frame */
        }
        /* the time group */
        ui_group(d.tx, d.gy, d.tw, d.gh, SKO("Time", "시간"));
        int R = (d.tw - 18) / 2; if (R > (d.gh - 44) / 2) R = (d.gh - 44) / 2;
        if (R > 8) {
            int cx = d.tx + d.tw / 2, cy = d.gy + 12 + R + 2;
            dt_clock(cx, cy, R, &t);
            dt_time_text(buf, sizeof buf, &t);
            int fw = ko_string_width(buf) + 12, fx = d.tx + (d.tw - fw) / 2, fy = cy + R + 8;
            ui_field(fx, fy, fw, 16, TH_FIELD, 0);
            ko_draw_mixed_string(fx + 6, fy + 2, buf, TH_FIELD_TEXT);
        }
        /* the line under both groups */
        l = 0; buf[0] = 0;
        append_str(buf, &l, sizeof buf, SKO("Current time zone: ", "현재 시간대: ")); build_tz_label(buf + l, sizeof buf - l);
        ko_draw_mixed_string(g.px + 10, g.py + g.ph - 16, buf, TH_TEXT);
        { int kx, ky, kw; dt_24_geo(&g, &kx, &ky, &kw); ui_checkbox(kx, ky, dt_p_24); ko_draw_mixed_string(kx + 18, ky + 1, SKO("24-hour clock", "24시간제"), TH_TEXT); }
    } else {
        dt_tzg_t z; dt_tz_geo(&g, &z);
        dt_draw_map(&z, dt_p_zone);
        ui_checkbox(z.kx, z.ky, dt_p_dst);
        const char *dl = SKO("Automatically adjust clock for daylight saving changes", "일광 절약 시간제에 맞게 시계를 자동으로 조정");
        if (ko_string_width(dl) > g.pw - 12 - 22) dl = SKO("Adjust clock for daylight saving", "일광 절약 시간 자동 조정");
        ko_draw_mixed_string(z.kx + 18, z.ky + 1, dl, TH_TEXT);
        dd_place(&dt_dd, z.cx, z.cy, z.cw); dt_dd.sel = dt_p_zone;
        dd_draw(&dt_dd);
        dd_draw_list(&dt_dd);                                    /* last: the open list covers the map and the box */
    }
    sheet_footer_draw(&g, dt_btn_pressed, dt_dirty());           /* Apply is greyed (and cannot be pressed) until something changed */
}

/* Which button is under the pointer: 1 OK, 2 Cancel (also the button of the "make the window bigger" hint), 3 Apply (only while there is something to apply), 5/6 month arrows; 0 = none. */
static int dt_btn_hit(int mx, int my) {
    sheet_t g; dt_geo(&g);
    if (!g.ok) return sheet_hint_hit(&g, mx, my);
    int f = sheet_footer_hit(&g, mx, my, dt_dirty());
    if (f) return f;
    if (dt_tab == 0) {
        dt_date_t d; dt_date_geo(&g, &d);
        if (in_rect(mx, my, d.gx + 8, d.gy + 12, 16, 16)) return 5;
        if (in_rect(mx, my, d.gx + d.gw - 24, d.gy + 12, 16, 16)) return 6;
    }
    return 0;
}

static void dt_fire(int b) {
    if (b == 1) { dt_apply(); dt_back(); }
    else if (b == 2) dt_back();
    else if (b == 3) dt_apply();
    else if (b == 5) { if (--dt_vm < 1) { dt_vm = 12; dt_vy--; } dt_sel = 0; }
    else if (b == 6) { if (++dt_vm > 12) { dt_vm = 1; dt_vy++; } dt_sel = 0; }
}

/* The mouse button came up: fire the button that was pressed, but only if the pointer is still on it (drag away = cancel). */
static void dt_release(int mx, int my) {
    int b = dt_btn_pressed;
    dt_btn_pressed = 0;
    if (b && dt_btn_hit(mx, my) == b) dt_fire(b);
}

/* 1 = handled; 0 = not mine. A press on a button only sinks it; the action happens in dt_release(). */
static int dt_click(int mx, int my) {
    sheet_t g; dt_geo(&g);
    int b = dt_btn_hit(mx, my);
    if (b) { dt_btn_pressed = b; return 1; }
    if (!g.ok) return 1;
    if (dt_tab == 1) {                                                    /* the zone drop-down (ui/controls.h) takes its clicks first */
        dt_tzg_t z; dt_tz_geo(&g, &z);
        dd_place(&dt_dd, z.cx, z.cy, z.cw); dt_dd.sel = dt_p_zone;
        if (dd_click(&dt_dd, mx, my)) { if (dt_dd.changed) dt_p_zone = dt_dd.sel; return 1; }
    }
    { int t = sheet_tab_at(&g, dt_tab_label, 2, mx, my); if (t >= 0) { dt_tab = t; dd_close(&dt_dd); return 1; } }
    if (dt_tab == 0) {
        dt_date_t d; dt_date_geo(&g, &d);
        if (in_rect(mx, my, d.cx0, d.cy0, d.cw * 7, d.ch * 6)) {
            int c = (mx - d.cx0) / d.cw, r = (my - d.cy0) / d.ch;
            int day = r * 7 + c - dt_dow(dt_vy, dt_vm, 1) + 1;
            if (day >= 1 && day <= dt_dim(dt_vy, dt_vm)) dt_sel = day;
        }
        { int kx, ky, kw; dt_24_geo(&g, &kx, &ky, &kw); if (in_rect(mx, my, kx, ky, kw, 14)) dt_p_24 = !dt_p_24; }
    } else {
        dt_tzg_t z; dt_tz_geo(&g, &z);
        if (z.mw > 0 && in_rect(mx, my, z.mx0, z.my0, z.mw, z.mh)) dt_p_zone = dt_zone_at_x(&z, mx);
        else if (in_rect(mx, my, z.kx, z.ky, g.pw - 20, 14)) dt_p_dst = !dt_p_dst;
    }
    return 1;
}

/* The mouse wheel while the zone list is open scrolls it (called from the input loop; 1 = used). */
static int dt_wheel(int notches) {
    return setting_page == SP_DATETIME && dt_tab == 1 && dd_wheel(&dt_dd, notches);     /* the standard scroll bar's wheel (ui/scrollbar.h) */
}

#endif
