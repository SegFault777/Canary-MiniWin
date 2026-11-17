/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_TZ_H
#define MW_TZ_H

/* tz.h -- time zones: the list Settings > Date/Time > Time Zone offers (the Windows 95 one, trimmed), each with its offset from UTC in MINUTES
 * (so India, Nepal, Newfoundland ... are right) and the daylight-saving rule of its region. The CMOS clock is read as UTC (rtc.h) and tz_local() turns it
 * into the time on the wall. The rules are the ones in force now: US (2nd Sunday of March 02:00 .. 1st Sunday of November 02:00, local), EU (last Sunday
 * of March .. last Sunday of October, 01:00 UTC), Australia (1st Sunday of October .. 1st Sunday of April, 02:00 standard time), New Zealand (last
 * Sunday of September .. 1st Sunday of April). Every other zone has none. A zone that observes it and has the \"automatically adjust\" box ticked
 * is one hour ahead while its rule says so. Needs rtc.h. */

enum { TZD_NONE = 0, TZD_US, TZD_EU, TZD_AU, TZD_NZ };
typedef struct { const char *name; short min; unsigned char dst; } tz_zone_t;

static const tz_zone_t tz_zones[] = {
    { "(GMT-12:00) Eniwetok, Kwajalein", -720, 0 },
    { "(GMT-11:00) Midway Island, Samoa", -660, 0 },
    { "(GMT-10:00) Hawaii", -600, 0 },
    { "(GMT-09:00) Alaska", -540, 1 },
    { "(GMT-08:00) Pacific Time (US & Canada); Tijuana", -480, 1 },
    { "(GMT-07:00) Mountain Time (US & Canada)", -420, 1 },
    { "(GMT-07:00) Arizona", -420, 0 },
    { "(GMT-06:00) Central Time (US & Canada)", -360, 1 },
    { "(GMT-06:00) Mexico City, Tegucigalpa", -360, 0 },
    { "(GMT-05:00) Eastern Time (US & Canada)", -300, 1 },
    { "(GMT-05:00) Bogota, Lima, Quito", -300, 0 },
    { "(GMT-04:00) Atlantic Time (Canada)", -240, 1 },
    { "(GMT-04:00) Caracas, La Paz", -240, 0 },
    { "(GMT-03:30) Newfoundland", -210, 1 },
    { "(GMT-03:00) Brasilia", -180, 0 },
    { "(GMT-03:00) Buenos Aires, Georgetown", -180, 0 },
    { "(GMT-02:00) Mid-Atlantic", -120, 0 },
    { "(GMT-01:00) Azores, Cape Verde Is.", -60, 2 },
    { "(GMT) Greenwich Mean Time; Dublin, Edinburgh, London", 0, 2 },
    { "(GMT) Casablanca, Monrovia", 0, 0 },
    { "(GMT+01:00) Amsterdam, Berlin, Bern, Rome, Stockholm, Vienna", 60, 2 },
    { "(GMT+01:00) Paris, Madrid, Brussels, Copenhagen", 60, 2 },
    { "(GMT+02:00) Athens, Istanbul, Helsinki, Kiev", 120, 2 },
    { "(GMT+02:00) Cairo, Jerusalem, Harare, Pretoria", 120, 0 },
    { "(GMT+03:00) Moscow, St. Petersburg, Baghdad, Riyadh", 180, 0 },
    { "(GMT+03:30) Tehran", 210, 0 },
    { "(GMT+04:00) Abu Dhabi, Muscat, Baku", 240, 0 },
    { "(GMT+04:30) Kabul", 270, 0 },
    { "(GMT+05:00) Islamabad, Karachi, Tashkent", 300, 0 },
    { "(GMT+05:30) Bombay, Calcutta, Madras, New Delhi", 330, 0 },
    { "(GMT+05:45) Kathmandu", 345, 0 },
    { "(GMT+06:00) Almaty, Dhaka, Colombo", 360, 0 },
    { "(GMT+07:00) Bangkok, Hanoi, Jakarta", 420, 0 },
    { "(GMT+08:00) Beijing, Chongqing, Hong Kong, Urumqi", 480, 0 },
    { "(GMT+08:00) Singapore, Perth, Taipei, Kuala Lumpur", 480, 0 },
    { "(GMT+09:00) Seoul", 540, 0 },
    { "(GMT+09:00) Tokyo, Osaka, Sapporo, Yakutsk", 540, 0 },
    { "(GMT+09:30) Adelaide", 570, 3 },
    { "(GMT+09:30) Darwin", 570, 0 },
    { "(GMT+10:00) Sydney, Melbourne, Canberra, Hobart", 600, 3 },
    { "(GMT+10:00) Brisbane, Guam, Vladivostok", 600, 0 },
    { "(GMT+11:00) Magadan, Solomon Is., New Caledonia", 660, 0 },
    { "(GMT+12:00) Auckland, Wellington", 720, 4 },
    { "(GMT+12:00) Fiji, Kamchatka, Marshall Is.", 720, 0 },
    { "(GMT+13:00) Nuku'alofa", 780, 0 },
};
#define TZ_N        ((int)(sizeof tz_zones / sizeof tz_zones[0]))
#define TZ_SEOUL    35

static int tz_zone = TZ_SEOUL;        /* the zone in use: an index into tz_zones (the default is KST; OOBE and Settings change it) */
static int tz_dst_auto = 1;           /* adjust for daylight saving automatically */

/* t shifted by `minutes` (negative = earlier), carrying through the day, month and year; the weekday is recomputed. */
static rtc_time_t tz_shift(rtc_time_t t, int minutes) {
    int total = t.hour * 60 + t.minute + minutes, dd = 0;
    while (total < 0) { total += 1440; dd--; }
    while (total >= 1440) { total -= 1440; dd++; }
    t.hour = total / 60; t.minute = total % 60;
    while (dd > 0) { t.day++; if (t.day > rtc_days_in_month(t.year, t.month)) { t.day = 1; if (++t.month > 12) { t.month = 1; t.year++; } } dd--; }
    while (dd < 0) { if (--t.day < 1) { if (--t.month < 1) { t.month = 12; t.year--; } t.day = rtc_days_in_month(t.year, t.month); } dd++; }
    t.weekday = compute_weekday(t.year, t.month, t.day);
    return t;
}

/* The day of the month of the n-th Sunday (n >= 1) or of the last one (n = -1). */
static int tz_sunday(int year, int month, int n) {
    int dim = rtc_days_in_month(year, month);
    if (n < 0) { for (int d = dim; d >= 1; d--) if (compute_weekday(year, month, d) == 0) return d; }
    int seen = 0;
    for (int d = 1; d <= dim; d++) if (compute_weekday(year, month, d) == 0 && ++seen == n) return d;
    return 1;
}
#define TZ_KEY(m, d, h) (((m) * 32 + (d)) * 24 + (h))

/* Is daylight saving in force in `zone` at the UTC instant `utc`? */
static int tz_dst_on(rtc_time_t utc, int zone, int automatic) {
    int rule = tz_zones[zone].dst;
    if (!rule || !automatic) return 0;
    rtc_time_t s = tz_shift(utc, tz_zones[zone].min);                 /* the standard-time wall clock */
    int ks = TZ_KEY(s.month, s.day, s.hour), ku = TZ_KEY(utc.month, utc.day, utc.hour);
    if (rule == TZD_US) return ks >= TZ_KEY(3, tz_sunday(s.year, 3, 2), 2) && ks < TZ_KEY(11, tz_sunday(s.year, 11, 1), 1);
    if (rule == TZD_EU) return ku >= TZ_KEY(3, tz_sunday(utc.year, 3, -1), 1) && ku < TZ_KEY(10, tz_sunday(utc.year, 10, -1), 1);
    if (rule == TZD_AU) return ks >= TZ_KEY(10, tz_sunday(s.year, 10, 1), 2) || ks < TZ_KEY(4, tz_sunday(s.year, 4, 1), 2);
    return ks >= TZ_KEY(9, tz_sunday(s.year, 9, -1), 2) || ks < TZ_KEY(4, tz_sunday(s.year, 4, 1), 2);     /* TZD_NZ */
}
static int tz_minutes_for(rtc_time_t utc, int zone, int automatic) { return tz_zones[zone].min + (tz_dst_on(utc, zone, automatic) ? 60 : 0); }
/* The wall-clock time of `utc` in `zone` (what Settings shows for a zone not applied yet) and in the zone in use. */
static rtc_time_t tz_local_for(rtc_time_t utc, int zone, int automatic) { return tz_shift(utc, tz_minutes_for(utc, zone, automatic)); }
static rtc_time_t tz_local(rtc_time_t utc) { return tz_local_for(utc, tz_zone, tz_dst_auto); }

#endif
