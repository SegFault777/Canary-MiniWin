/* SPDX-License-Identifier: GPL-3.0-only */
/* kernel/tz.h (1.1-alpha-14): the zone list, minute offsets and the daylight-saving rules, run on the real kernel code (only port I/O is stubbed).
 * The switch instants checked below are the real 2026 ones: US 8 March 02:00 local .. 1 November 02:00 local, EU 29 March .. 25 October 01:00 UTC,
 * Australia 4 October 02:00 .. 5 April 03:00 local, New Zealand 27 September 02:00 .. 5 April 03:00 local. */
#include <stdio.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t i32;
#define IO_H
static void outb(u16 p, u8 v) { (void)p; (void)v; }
static u8 inb(u16 p) { (void)p; return 0; }
#include "../../kernel/rtc.h"
#include "../../kernel/tz.h"

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static rtc_time_t T(int y, int mo, int d, int h, int mi) { rtc_time_t t = { 0, mi, h, d, mo, y, 0 }; t.weekday = compute_weekday(y, mo, d); return t; }
static int zone_named(const char *tail) {
    for (int i = 0; i < TZ_N; i++) { const char *n = tz_zones[i].name; int l = 0, k = 0; while (n[l]) l++; while (tail[k]) k++; if (l >= k) { int ok = 1; for (int j = 0; j < k; j++) if (n[l - k + j] != tail[j]) ok = 0; if (ok) return i; } }
    return -1;
}
static void same(rtc_time_t a, int y, int mo, int d, int h, int mi, const char *what) {
    CHECK(a.year == y && a.month == mo && a.day == d && a.hour == h && a.minute == mi, "%s: got %04d-%02d-%02d %02d:%02d, want %04d-%02d-%02d %02d:%02d", what, a.year, a.month, a.day, a.hour, a.minute, y, mo, d, h, mi);
}

int main(void) {
    /* the table */
    CHECK(TZ_N >= 40, "zone count %d", TZ_N);
    for (int i = 1; i < TZ_N; i++) CHECK(tz_zones[i].min >= tz_zones[i - 1].min, "zones are ordered by offset (%d)", i);
    CHECK(zone_named("Seoul") == TZ_SEOUL, "TZ_SEOUL is %d, Seoul is %d", TZ_SEOUL, zone_named("Seoul"));
    CHECK(tz_zones[TZ_SEOUL].min == 540 && tz_zones[TZ_SEOUL].dst == TZD_NONE, "Seoul is UTC+9 with no daylight saving");

    /* tz_shift carries correctly */
    same(tz_shift(T(2026, 12, 31, 20, 0), 540), 2027, 1, 1, 5, 0, "year rollover");
    same(tz_shift(T(2026, 3, 1, 1, 0), -120), 2026, 2, 28, 23, 0, "month rollover back (non-leap February)");
    same(tz_shift(T(2028, 3, 1, 1, 0), -120), 2028, 2, 29, 23, 0, "leap February");
    same(tz_shift(T(2026, 1, 1, 0, 0), 345), 2026, 1, 1, 5, 45, "Nepal +5:45");
    same(tz_shift(T(2026, 1, 1, 0, 10), -210), 2025, 12, 31, 20, 40, "Newfoundland -3:30 back across the year");
    CHECK(tz_shift(T(2026, 11, 5, 12, 0), 0).weekday == 4, "5 November 2026 is a Thursday");

    /* no rule, or the box unticked -> never */
    CHECK(!tz_dst_on(T(2026, 7, 1, 0, 0), TZ_SEOUL, 1), "Seoul never has DST");
    int pac = zone_named("Tijuana");
    CHECK(pac >= 0 && !tz_dst_on(T(2026, 7, 1, 0, 0), pac, 0), "automatic off -> no DST");

    /* US (Pacific, UTC-8) */
    CHECK(!tz_dst_on(T(2026, 3, 8, 9, 59), pac, 1) && tz_dst_on(T(2026, 3, 8, 10, 0), pac, 1), "US start: 8 March 10:00 UTC");
    CHECK(tz_dst_on(T(2026, 11, 1, 8, 59), pac, 1) && !tz_dst_on(T(2026, 11, 1, 9, 0), pac, 1), "US end: 1 November 09:00 UTC");
    same(tz_local_for(T(2026, 7, 4, 19, 0), pac, 1), 2026, 7, 4, 12, 0, "Pacific in July is UTC-7");
    same(tz_local_for(T(2026, 1, 4, 19, 0), pac, 1), 2026, 1, 4, 11, 0, "Pacific in January is UTC-8");
    /* EU (London) */
    int lon = zone_named("London");
    CHECK(!tz_dst_on(T(2026, 3, 29, 0, 59), lon, 1) && tz_dst_on(T(2026, 3, 29, 1, 0), lon, 1), "EU start: 29 March 01:00 UTC");
    CHECK(tz_dst_on(T(2026, 10, 25, 0, 59), lon, 1) && !tz_dst_on(T(2026, 10, 25, 1, 0), lon, 1), "EU end: 25 October 01:00 UTC");
    same(tz_local_for(T(2026, 7, 1, 12, 0), lon, 1), 2026, 7, 1, 13, 0, "London in July is UTC+1");
    /* Australia (Sydney, UTC+10, southern: DST across the new year) */
    int syd = zone_named("Hobart");
    CHECK(!tz_dst_on(T(2026, 10, 3, 15, 59), syd, 1) && tz_dst_on(T(2026, 10, 3, 16, 0), syd, 1), "AU start: 4 October 02:00 standard = 3 October 16:00 UTC");
    CHECK(tz_dst_on(T(2026, 4, 4, 15, 59), syd, 1) && !tz_dst_on(T(2026, 4, 4, 16, 0), syd, 1), "AU end: 5 April 02:00 standard = 4 April 16:00 UTC");
    CHECK(tz_dst_on(T(2026, 1, 15, 0, 0), syd, 1) && !tz_dst_on(T(2026, 7, 15, 0, 0), syd, 1), "AU: summer in January, none in July");
    /* New Zealand */
    int akl = zone_named("Wellington");
    CHECK(!tz_dst_on(T(2026, 9, 26, 13, 59), akl, 1) && tz_dst_on(T(2026, 9, 26, 14, 0), akl, 1), "NZ start: 27 September 02:00 NZST = 26 September 14:00 UTC");
    CHECK(tz_dst_on(T(2026, 4, 4, 13, 59), akl, 1) && !tz_dst_on(T(2026, 4, 4, 14, 0), akl, 1), "NZ end: 5 April 02:00 NZST = 4 April 14:00 UTC");

    /* half-hour zones */
    same(tz_local_for(T(2026, 6, 1, 0, 0), zone_named("New Delhi"), 1), 2026, 6, 1, 5, 30, "India");
    printf("tz: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
