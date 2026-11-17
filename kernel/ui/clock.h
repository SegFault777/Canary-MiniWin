/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_UI_CLOCK_H
#define MW_UI_CLOCK_H

/* ui/clock.h -- the taskbar clock (CMOS time, time zone) and its date popup.
 * (Split out of kernel.c in pre-29; it is still one translation unit, so modules are included by
 * kernel.c in dependency order -- see the module map there.) */

/* ============================================================
 * Clock -- bottom-right of the taskbar. Real CMOS hardware time (see
 * rtc.h), not a simulated tick counter. There's deliberately no
 * "automatic" timezone-by-location here: that would need a working
 * HTTP client to ask some geolocation service where in the world this
 * machine is, and this kernel's network stack (see kernel/net_stack.h)
 * goes up through ARP/IP/ICMP/UDP/DHCP but doesn't speak TCP or HTTP
 * yet. So instead, the timezone is a plain manual UTC offset, set in
 * SETTING.MWP > Date/Time > Time Zone (a list of zones, tz.h), and applied to
 * the CMOS reading via tz_local(). Honest > fake.
 * ============================================================ */
/* the zone in use (tz_zone, tz_dst_auto) lives in tz.h */
static int clock_popup_open = 0;

static const char *weekday_names_en[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *month_names_en[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};
/* Korean dates are conventionally numeric ("9월 14일"), so there's no
 * month-name table to localize -- just the weekday name and the
 * 년/월/일 particles, built directly into format_full_date() below. */
static const char *weekday_names_ko[7] = {
    "\xec\x9d\xbc\xec\x9a\x94\xec\x9d\xbc", "\xec\x9b\x94\xec\x9a\x94\xec\x9d\xbc", "\xed\x99\x94\xec\x9a\x94\xec\x9d\xbc",
    "\xec\x88\x98\xec\x9a\x94\xec\x9d\xbc", "\xeb\xaa\xa9\xec\x9a\x94\xec\x9d\xbc", "\xea\xb8\x88\xec\x9a\x94\xec\x9d\xbc",
    "\xed\x86\xa0\xec\x9a\x94\xec\x9d\xbc"
}; /* 일요일 월요일 화요일 수요일 목요일 금요일 토요일 */

static void append_str(char *out, u32 *len, u32 outsz, const char *s) {
    while (*s) kstrcpy_append(out, len, outsz, *s++);
}
static void append_uint(char *out, u32 *len, u32 outsz, int v) {
    char tmp[12];
    int n = 0;
    if (v == 0) { kstrcpy_append(out, len, outsz, '0'); return; }
    while (v > 0 && n < (int)sizeof(tmp)) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n > 0) kstrcpy_append(out, len, outsz, tmp[--n]);
}
static void append_uint2(char *out, u32 *len, u32 outsz, int v) { /* zero-padded to 2 digits */
    kstrcpy_append(out, len, outsz, (char)('0' + (v / 10) % 10));
    kstrcpy_append(out, len, outsz, (char)('0' + v % 10));
}

/* "3:45 PM" (English) or "오후 3:45" (Korean) -- what the taskbar shows; "15:45" in both with Settings > Display > Clock set to 24-hour. */
static int clock_24h = 0;
static void format_clock_time(rtc_time_t *t, char *out, u32 outsz) {
    u32 len = 0;
    if (clock_24h) {
        append_uint2(out, &len, outsz, t->hour);
        kstrcpy_append(out, &len, outsz, ':');
        append_uint2(out, &len, outsz, t->minute);
        return;
    }
    int h12 = t->hour % 12; if (h12 == 0) h12 = 12;
    int is_pm = t->hour >= 12;

    if (sys_language == LANG_KOREAN) {
        append_str(out, &len, outsz, is_pm ? "\xec\x98\xa4\xed\x9b\x84" : "\xec\x98\xa4\xec\xa0\x84"); /* 오후/오전 */
        kstrcpy_append(out, &len, outsz, ' ');
    }
    append_uint(out, &len, outsz, h12);
    kstrcpy_append(out, &len, outsz, ':');
    append_uint2(out, &len, outsz, t->minute);
    if (sys_language == LANG_ENGLISH) {
        kstrcpy_append(out, &len, outsz, ' ');
        append_str(out, &len, outsz, is_pm ? "PM" : "AM");
    }
}

/* "Monday, September 14, 2026" (English) or "2026년 9월 14일 월요일"
 * (Korean) -- shown in the popup when the clock is clicked. */
static void format_full_date(rtc_time_t *t, char *out, u32 outsz) {
    u32 len = 0;
    if (sys_language == LANG_KOREAN) {
        append_uint(out, &len, outsz, t->year);
        append_str(out, &len, outsz, "\xeb\x85\x84 "); /* 년 */
        append_uint(out, &len, outsz, t->month);
        append_str(out, &len, outsz, "\xec\x9b\x94 "); /* 월 */
        append_uint(out, &len, outsz, t->day);
        append_str(out, &len, outsz, "\xec\x9d\xbc "); /* 일 */
        append_str(out, &len, outsz, weekday_names_ko[t->weekday]);
    } else {
        append_str(out, &len, outsz, weekday_names_en[t->weekday]);
        append_str(out, &len, outsz, ", ");
        append_str(out, &len, outsz, month_names_en[t->month - 1]);
        kstrcpy_append(out, &len, outsz, ' ');
        append_uint(out, &len, outsz, t->day);
        append_str(out, &len, outsz, ", ");
        append_uint(out, &len, outsz, t->year);
    }
}

/* "UTC+9" / "UTC-5" / "UTC+5:30" (+ " (DST)" while daylight saving is in force) for a zone -- shown in Settings > Date/Time. */
static void tz_label_for(char *out, u32 outsz, int zone, int automatic, rtc_time_t utc) {
    int m = tz_minutes_for(utc, zone, automatic), a = m < 0 ? -m : m;
    u32 len = 0;
    out[0] = 0;
    append_str(out, &len, outsz, "UTC");
    kstrcpy_append(out, &len, outsz, m >= 0 ? '+' : '-');
    append_uint(out, &len, outsz, a / 60);
    if (a % 60) { kstrcpy_append(out, &len, outsz, ':'); if (a % 60 < 10) kstrcpy_append(out, &len, outsz, '0'); append_uint(out, &len, outsz, a % 60); }
    if (tz_dst_on(utc, zone, automatic)) append_str(out, &len, outsz, " (DST)");
}
static void build_tz_label(char *out, u32 outsz) { rtc_time_t now; rtc_read(&now); tz_label_for(out, outsz, tz_zone, tz_dst_auto, now); }

#define CLOCK_W  88
#define CLOCK_H  14
#define CLOCK_X  (VGA_WIDTH - CLOCK_W - 3)
#define CLOCK_Y  (TASKBAR_Y + 2)

static int clock_hit(int px, int py) {
    return in_rect(px, py, CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H);
}

/* The little yellow loudspeaker in front of the time (the Windows 95 tray look). 11x8; '#' outline, 'y' body, 'w' sound waves.
 * It is only a picture: MiniWin has no sound driver yet. */
static void draw_tray_speaker(int x, int y) {
    static const char *const spk[8] = {
        "....#......",
        "...##...w..",
        "##.y#....w.",
        "#yyy#.w..w.",
        "#yyy#.w..w.",
        "##.y#....w.",
        "...##...w..",
        "....#......"
    };
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 11; c++) {
            char ch = spk[r][c];
            if (ch == '#') bb_putpixel(x + c, y + r, TH_TEXT);
            else if (ch == 'y') bb_putpixel(x + c, y + r, COL_YELLOW);
            else if (ch == 'w') bb_putpixel(x + c, y + r, TH_SHADOW);
        }
}

#endif
