/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_APPS_OOBE_H
#define MW_APPS_OOBE_H

/* apps/oobe.h -- the first-run setup ("out of the box experience") and the logon prompt (1.1-alpha-14).
 *
 * THE SETUP runs once, on the first boot of an INSTALLED system (the installer formats a fresh root volume: no /etc/SYSTEM.CFG yet, see syscfg.h).
 * A live disc or a floppy has no setup (nothing can be kept there): the user is simply "LIVE". Nine pages, each a question, Back and Next:
 *     Language -> Input methods -> License agreement -> User name -> Password (may stay empty) -> Desktop colour -> Screen size -> Time zone -> Date and time (Finish)
 * The look follows the Windows 98 first-run screens: the whole screen, a header with the name, a bar of four phases (Getting Started, License Agreement,
 * Your Account, Final Settings) with the current one in bold, a blue panel on the left whose pictures change with the phase, a dark red heading, the text, a
 * thin brown line and the gold Back / Next buttons at the bottom right.
 * THE LICENCE is the GNU GPL 3.0, whole: /etc/LICENSE.TXT (the English original) or, when the language chosen is Korean, /etc/LICENSE_KO.TXT (the Korean
 * translation); the installer copies both into the new root volume (os_inst.h). The setup does not go on until "I accept" is chosen.
 * The language, the input methods and the colour take effect at once (the pages after a choice are in the new language, the desktop colour shows in a small
 * preview); Finish writes /etc/SYSTEM.CFG and /etc/USER.PWD, sets the clock if the date or time was changed, and -- if another screen size was chosen --
 * restarts into it (Settings > Display's own mechanism: the boot byte, then the "Keep this screen size?" question). The screen sizes offered are the three
 * MiniWin has; the kernel cannot read the monitor's own resolution (the boot loader picks the VBE mode), so the one running now is preselected.
 *
 * THE LOGON PROMPT: on a set-up system whose user has a password (/etc/USER.PWD), a "Welcome" screen covers everything at boot until the password is typed.
 * No lock-out, no timeout: it is a door, not a vault (nothing stops someone who can write the disk).
 *
 * Both are modal: while oobe_open is 1 they own the mouse (oobe_mouse) and the keyboard (oobe_key), and oobe_draw() paints over everything.
 * Needs syscfg.h, textfield.h, controls.h, listbox.h, tz.h, rtc.h, setting.h / setting_display.h (colours, screen sizes), terminal.h (whoami). */

enum { OB_LANG, OB_IME, OB_LICENSE, OB_USER, OB_PASS, OB_DESK, OB_SCREEN, OB_ZONE, OB_DATE, OB_N };
enum { OBM_NONE = 0, OBM_SETUP, OBM_LOGON };

static int oobe_open = 0, oobe_mode = OBM_NONE, oobe_page = OB_LANG;
static int ob_btn = 0;                                 /* the pressed button: 1 Back, 2 Next / Finish / OK */
static const char *ob_msg = 0;
static int ob_ime = 2;                                 /* 0 English only, 1 Korean only, 2 both (English first), 3 both (Korean first) */
static char ob_user[SYS_USER_MAX + 1]; static tf_t ob_user_tf;
static char ob_pw1[33], ob_pw2[33]; static tf_t ob_pw1_tf, ob_pw2_tf;
static int ob_desk = 0, ob_screen = 1, ob_zone = 0, ob_dst = 1, ob_accept = 0;     /* ob_accept: 0 not chosen, 1 accepted, 2 not accepted */
static lbx_t ob_zlb = { 0, 0, 0, 0, 15, 0, 0, 0 };
static char ob_dt[5][6]; static tf_t ob_dt_tf[5]; static int ob_dt_orig[5];     /* year month day hour minute, as typed; what the clock showed */
static int ob_focus = 0;                               /* which field of the page has the keys */
static char ob_logon_pw[33]; static tf_t ob_logon_tf;

typedef struct { int dx, dy, dw, dh, lh, rowh, bx, by, cx, cw, cy, logo, bar, panel, hd; } ob_geo_t;

#define OB_BG   0xFFFFFF                               /* the page */
#define OB_INK  0x000000
#define OB_RED  0x800000                               /* the heading, dark red */
#define OB_LINE 0x8C6B2C                               /* the thin brown line above the buttons */

/* ---- the licence text, wrapped into lines for the box on the licence page ---- */
#define OB_LMAX 3200
static char *ob_ltext = 0; static u32 ob_llen = 0;
static u32 *ob_lstart = 0; static u16 *ob_lcnt = 0; static int ob_nlines = 0;
static lbx_t ob_llb = { 0, 0, 0, 0, 15, 0, 0, -1 };
static void ob_license_free(void) {
    if (ob_ltext) kfree(ob_ltext);
    if (ob_lstart) kfree(ob_lstart);
    if (ob_lcnt) kfree(ob_lcnt);
    ob_ltext = 0; ob_lstart = 0; ob_lcnt = 0; ob_nlines = 0; ob_llen = 0;
}
/* Cuts the text into lines no wider than w pixels (at a space when it can). */
static void ob_wrap_license(int w) {
    ob_nlines = 0;
    if (!ob_ltext || !ob_lstart || !ob_lcnt) return;
    u32 p = 0;
    while (p <= ob_llen && ob_nlines < OB_LMAX) {
        u32 e = p; while (e < ob_llen && ob_ltext[e] != '\n') e++;              /* one paragraph line: [p, e) */
        if (e == p) { ob_lstart[ob_nlines] = p; ob_lcnt[ob_nlines++] = 0; }
        else {
            u32 a = p;
            while (a < e && ob_nlines < OB_LMAX) {
                u32 last = a, q = a;
                char tmp[256];
                while (q < e) {                                                 /* add words while the piece still fits */
                    u32 r = q; while (r < e && ob_ltext[r] != ' ') r++;
                    u32 n = r - a; if (n > 250) n = 250;
                    for (u32 i = 0; i < n; i++) tmp[i] = ob_ltext[a + i];
                    tmp[n] = 0;
                    if (ko_string_width(tmp) > w) { if (last == a) last = r; break; }       /* a single word wider than the box is cut by the clip */
                    last = r; q = r; while (q < e && ob_ltext[q] == ' ') q++;
                }
                if (last == a) last = e;
                u32 len = last - a; if (len > 250) len = 250;
                ob_lstart[ob_nlines] = a; ob_lcnt[ob_nlines++] = (u16)len;
                a = a + len; while (a < e && ob_ltext[a] == ' ') a++;
            }
        }
        p = e + 1;
    }
    ob_llb.count = ob_nlines; ob_llb.top = 0; ob_llb.sel = -1;
}
/* Loads the licence for the language in use (English: LICENSE.TXT, Korean: LICENSE_KO.TXT). 0 if there is none. */
static int ob_license_load(void) {
    ob_license_free();
    const char *path = sys_language == LANG_KOREAN ? "/etc/LICENSE_KO.TXT" : "/etc/LICENSE.TXT";
    vfs_node_t n;
    if (vfs_stat(path, &n) != MWFS_OK || n.type != MWFS_T_FILE) return 0;
    u32 sz = vfs_size(&n);
    if (sz == 0 || sz > 98304u) return 0;
    ob_ltext = (char *)kmalloc(sz + 1); ob_lstart = (u32 *)kmalloc(OB_LMAX * 4u); ob_lcnt = (u16 *)kmalloc(OB_LMAX * 2u);
    u32 len = 0, aux = 0;
    if (!ob_ltext || !ob_lstart || !ob_lcnt || vfs_read(path, (u8 *)ob_ltext, sz, &len, &aux) != MWFS_OK || len != sz) { ob_license_free(); return 0; }
    ob_ltext[len] = 0; ob_llen = len;
    for (u32 i = 0; i < len; i++) if (ob_ltext[i] == '\r') ob_ltext[i] = ' ';
    if (sys_language != LANG_KOREAN)                                      /* the English original is hard-wrapped at 72 columns: inside a paragraph a line break becomes a space, */
        for (u32 i = 1; i + 1 < len; i++)                                 /* so the lines fill the box (an indented line, as a paragraph or a list item starts, and a blank line still break) */
            if (ob_ltext[i] == '\n' && ob_ltext[i - 1] != '\n' && ob_ltext[i + 1] != '\n' && ob_ltext[i + 1] != ' ' && ob_ltext[i + 1] != '\t') ob_ltext[i] = ' ';
    return 1;
}

/* Wraps `s` into lines of at most w pixels starting at (x,y); draws them if `draw`; returns the y below the last one. */
static int ob_wrap(int x, int y, int w, const char *s, int draw, u32 col) {
    char line[200]; line[0] = 0; u32 ln = 0;
    const char *p = s;
    int lh = FONT_LINE_H;
    while (*p) {
        char word[80]; u32 wl = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && wl < 79) word[wl++] = *p++;
        word[wl] = 0;
        if (!wl) break;
        char trial[300]; u32 tn = 0; trial[0] = 0;
        append_str(trial, &tn, sizeof trial, line); if (ln) append_str(trial, &tn, sizeof trial, " "); append_str(trial, &tn, sizeof trial, word);
        if (ln && ko_string_width(trial) > w) {
            if (draw) ko_draw_mixed_string(x, y, line, col);
            y += lh;
            mwfs_strcpy(line, word, sizeof line);
        } else mwfs_strcpy(line, trial, sizeof line);
        ln = tf_len(line);
    }
    if (ln) { if (draw) ko_draw_mixed_string(x, y, line, col); y += lh; }
    return y;
}

/* The whole screen: a header with the name, a bar of the four phases, a panel on the left (when the screen is wide), the page on the right, the line and
 * the two buttons at the bottom. */
static void ob_geo(ob_geo_t *g) {
    g->lh = FONT_LINE_H; g->rowh = FONT_CELL + 6;
    g->dx = 0; g->dy = 0; g->dw = VGA_WIDTH; g->dh = VGA_HEIGHT;
    g->logo = VGA_HEIGHT >= 400 ? 52 : 0;
    g->bar = FONT_CELL + 10;
    g->panel = VGA_WIDTH >= 480 ? VGA_WIDTH / 4 : 0;
    g->cx = g->panel + 16; g->cw = g->dw - g->cx - 16;
    g->hd = VGA_HEIGHT >= 400 ? 2 * FONT_LINE_H : FONT_LINE_H;                     /* the heading is drawn twice as big when there is room */
    g->cy = g->logo + g->bar + 8;
    g->by = g->dh - 32; g->bx = g->dw - 16 - 2 * 88 - 6;
}

/* ---- page texts ---- */
static const char *ob_title(int pg) {
    switch (pg) {
        case OB_LANG: return SKO("Language", "언어");
        case OB_IME: return SKO("Input methods", "입력기");
        case OB_LICENSE: return SKO("MiniWin License Agreement", "MiniWin 사용권 계약서");
        case OB_USER: return SKO("User name", "사용자 이름");
        case OB_PASS: return SKO("Password", "비밀번호");
        case OB_DESK: return SKO("Desktop colour", "바탕 화면 색");
        case OB_SCREEN: return SKO("Screen size", "화면 크기");
        case OB_ZONE: return SKO("Time zone", "시간대");
        default: return SKO("Date and time", "날짜와 시간");
    }
}
static const char *ob_hint(int pg) {
    switch (pg) {
        case OB_LANG: return SKO("Choose the language of the menus and messages. You can change it later in Control Panel > Regional Settings.", "메뉴와 메시지의 언어를 고르세요. 나중에 제어판 > 국가 및 언어 옵션에서 바꿀 수 있습니다.");
        case OB_IME: return SKO("Choose the input methods. The Right Alt key switches between them while you type.", "입력기를 고르세요. 글을 쓰는 중 오른쪽 Alt 키로 서로 바꿉니다.");
        case OB_LICENSE: return SKO("Please read the license agreement. To use MiniWin you must accept it. To see the rest, press the Down arrow or the Space bar, or use the scroll bar.", "사용권 계약서를 읽어 주세요. MiniWin을 사용하려면 계약에 동의해야 합니다. 나머지를 보려면 아래쪽 화살표나 스페이스 키, 또는 스크롤 막대를 사용하세요.");
        case OB_USER: return SKO("What should MiniWin call you?", "MiniWin이 어떻게 불러야 할까요?");
        case OB_PASS: return SKO("You can protect this computer with a password. Leave both boxes empty to use none.", "비밀번호로 이 컴퓨터를 보호할 수 있습니다. 쓰지 않으려면 두 칸을 모두 비워 두세요.");
        case OB_DESK: return SKO("Pick the colour of the desktop. The small screen below shows it.", "바탕 화면 색을 고르세요. 아래의 작은 화면에 바로 보입니다.");
        case OB_SCREEN: return SKO("Choose the screen size. MiniWin cannot read your monitor, so it keeps the size it started with unless you pick another. A different size restarts the screen when you finish.", "화면 크기를 고르세요. 모니터 정보를 읽을 수 없어서 고르지 않으면 지금 크기를 유지합니다. 다른 크기를 고르면 마칠 때 화면을 다시 시작합니다.");
        case OB_ZONE: return SKO("Choose the time zone you are in.", "지금 계신 곳의 시간대를 고르세요.");
        default: return SKO("Check the date and the time. Change them if they are wrong.", "날짜와 시간을 확인하세요. 틀리면 고치세요.");
    }
}

/* ---- the choices that take effect at once ---- */
static void ob_apply_ime(void) {
    ime_enabled[IME_ENGLISH] = ob_ime != 1; ime_enabled[IME_KOREAN] = ob_ime != 0;
    current_ime = ob_ime == 1 || ob_ime == 3 ? IME_KOREAN : IME_ENGLISH;
    tf_ime_flush();
}
static void ob_apply_desk(void) { setting_desk_idx = ob_desk; setting_desk_apply(); }

/* ---- the clock ---- */
static void ob_dt_fill(const rtc_time_t *l) {
    int v[5] = { l->year, l->month, l->day, l->hour, l->minute };
    for (int i = 0; i < 5; i++) {
        char *b = ob_dt[i]; u32 n = 0; b[0] = 0;
        char d[8]; int k = 0, x = v[i];
        if (x == 0) d[k++] = '0';
        while (x > 0 && k < 7) { d[k++] = (char)('0' + x % 10); x /= 10; }
        if (i > 0 && k < 2) d[k++] = '0';                                   /* 03, not 3 */
        while (k) b[n++] = d[--k];
        b[n] = 0; ob_dt_orig[i] = v[i];
        tf_to_end(&ob_dt_tf[i], b);
    }
}
static int ob_dt_val(int i) { int r = 0; for (const char *s = ob_dt[i]; *s; s++) { if (*s < '0' || *s > '9') return -1; r = r * 10 + (*s - '0'); } return ob_dt[i][0] ? r : -1; }
/* The date and time as typed, validated; 0 if it is not a real moment. */
static int ob_dt_get(rtc_time_t *t) {
    int y = ob_dt_val(0), mo = ob_dt_val(1), d = ob_dt_val(2), h = ob_dt_val(3), mi = ob_dt_val(4);
    if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || h < 0 || h > 23 || mi < 0 || mi > 59 || d < 1) return 0;
    if (d > rtc_days_in_month(y, mo)) return 0;
    t->year = y; t->month = mo; t->day = d; t->hour = h; t->minute = mi; t->second = 0; t->weekday = compute_weekday(y, mo, d);
    return 1;
}
static int ob_dt_changed(void) { for (int i = 0; i < 5; i++) if (ob_dt_val(i) != ob_dt_orig[i]) return 1; return 0; }

/* ---- the fields of a page ---- */
static int ob_nfields(void) { return oobe_mode == OBM_LOGON ? 1 : oobe_page == OB_USER ? 1 : oobe_page == OB_PASS ? 2 : oobe_page == OB_DATE ? 5 : 0; }
static char *ob_fbuf(int i, u32 *cap, tf_t **tf, int *fl) {
    if (oobe_mode == OBM_LOGON) { *cap = sizeof ob_logon_pw; *tf = &ob_logon_tf; *fl = TF_PASS; return ob_logon_pw; }
    if (oobe_page == OB_USER) { *cap = sizeof ob_user; *tf = &ob_user_tf; *fl = TF_NAME; return ob_user; }
    if (oobe_page == OB_PASS) { *cap = sizeof ob_pw1; *tf = i ? &ob_pw2_tf : &ob_pw1_tf; *fl = TF_PASS; return i ? ob_pw2 : ob_pw1; }
    *cap = sizeof ob_dt[0]; *tf = &ob_dt_tf[i]; *fl = TF_ASCII | TF_NOKO; return ob_dt[i];
}
/* where field i is (x, y, w, h) on this page */
static void ob_frect(const ob_geo_t *g, int i, int *x, int *y, int *w, int *h) {
    int fh = FONT_CELL + 8;
    *h = fh;
    if (oobe_mode == OBM_LOGON) { *x = g->cx; *y = g->cy + g->hd + 8 + 2 * g->lh + 8; *w = g->cw > 260 ? 260 : g->cw; return; }
    int hy = ob_wrap(g->cx, g->cy + g->hd + 4, g->cw, ob_hint(oobe_page), 0, 0) + 6;          /* below the description */
    if (oobe_page == OB_USER) { *x = g->cx; *y = hy + g->lh + 2; *w = g->cw; return; }
    if (oobe_page == OB_PASS) { *x = g->cx; *w = g->cw; *y = hy + g->lh + 2 + i * (g->lh + fh + 8); return; }
    static const int wd[5] = { 48, 32, 32, 32, 32 };
    int xx = g->cx; for (int k = 0; k < i; k++) xx += wd[k] + 10;
    *x = xx; *w = wd[i]; *y = hy + g->lh + 2;
}
static int ob_field_at(const ob_geo_t *g, int mx, int my) {
    for (int i = 0; i < ob_nfields(); i++) { int x, y, w, h; ob_frect(g, i, &x, &y, &w, &h); if (in_rect(mx, my, x, y, w, h)) return i; }
    return -1;
}
/* the licence page: the text box and the two radio buttons under it */
static void ob_lic_rect(const ob_geo_t *g, int y, int *bx, int *by, int *bw, int *bh, int *ry) {
    int hint = VGA_HEIGHT >= 400 ? g->lh + 8 : 0;                                    /* room for the line "When you are ready..." */
    *ry = g->by - 6 - hint - 2 * g->rowh;
    *bx = g->cx; *by = y + 2; *bw = g->cw; *bh = *ry - 8 - *by;
    if (*bh < 3 * 15) *bh = 3 * 15;
}

/* ---- drawing ---- */
static void ob_radio(int x, int y, int on, const char *label) { ui_radio(x, y, on); ko_draw_mixed_string(x + 20, y, label, OB_INK); }
static void ob_draw_field(const ob_geo_t *g, int i) {
    int x, y, w, h, fl; u32 cap; tf_t *tf;
    char *buf = ob_fbuf(i, &cap, &tf, &fl);
    ob_frect(g, i, &x, &y, &w, &h);
    ui_field(x, y, w, h, TH_FIELD, 0);
    tf_draw(tf, buf, fl, x + 3, y + 3, w - 6, 0, ob_focus == i, TH_FIELD_TEXT);
}
static const char *ob_ime_name(int i) {
    switch (i) {
        case 0: return SKO("English only", "영어만");
        case 1: return SKO("Korean only", "한국어만");
        case 2: return SKO("English and Korean (start with English)", "영어와 한국어 (영어로 시작)");
        default: return SKO("English and Korean (start with Korean)", "영어와 한국어 (한국어로 시작)");
    }
}
static const char *ob_screen_name(int i) {
    return i == 0 ? "320 x 200" : i == 1 ? "640 x 480" : SKO("854 x 480 (widescreen)", "854 x 480 (와이드)");
}
static void ob_row_zone(int idx, int x, int y, int w, int h, int sel) {
    (void)w; (void)h;
    ko_draw_mixed_string(x + 4, y + 2, tz_zones[idx].name, sel ? TH_TEXT_INVERSE : TH_FIELD_TEXT);
}
static void ob_row_lic(int idx, int x, int y, int w, int h, int sel) {
    (void)w; (void)h; (void)sel;
    char tmp[256]; u32 n = ob_lcnt[idx];
    if (n > 250) n = 250;
    for (u32 i = 0; i < n; i++) tmp[i] = ob_ltext[ob_lstart[idx] + i];
    tmp[n] = 0;
    if (n) ko_draw_mixed_string(x + 3, y + 2, tmp, OB_INK);
}
static void ob_page_open(int pg) {
    oobe_page = pg; ob_focus = 0; ob_msg = 0; ob_btn = 0;
    if (pg != OB_LICENSE) ob_license_free();
    if (pg == OB_LICENSE) {
        ob_geo_t g; ob_geo(&g);
        int y = ob_wrap(g.cx, g.cy + g.hd + 4, g.cw, ob_hint(pg), 0, 0) + 6, bx, by, bw, bh, ry;
        ob_lic_rect(&g, y, &bx, &by, &bw, &bh, &ry);
        ob_license_load();
        ob_wrap_license(bw - SB_W - 10);
    }
    if (pg == OB_ZONE) { ob_zlb.count = TZ_N; ob_zlb.sel = ob_zone; ob_zlb.top = ob_zone > 1 ? ob_zone - 1 : 0; }
    if (pg == OB_DATE) { rtc_time_t u; rtc_read(&u); rtc_time_t l = tz_local_for(u, ob_zone, ob_dst); ob_dt_fill(&l); }
    if (pg == OB_USER || pg == OB_PASS || pg == OB_DATE) { u32 cap; tf_t *tf; int fl; char *b = ob_fbuf(0, &cap, &tf, &fl); tf_to_end(tf, b); }
}

/* ---- the look ---- */
static const char *ob_phase_name(int i) {
    switch (i) {
        case 0: return SKO("Getting Started", "시작하기");
        case 1: return SKO("License Agreement", "사용권 계약서");
        case 2: return SKO("Your Account", "사용자 계정");
        default: return SKO("Final Settings", "마무리");
    }
}
static int ob_phase(int pg) { return pg <= OB_IME ? 0 : pg == OB_LICENSE ? 1 : pg <= OB_PASS ? 2 : 3; }
static u32 ob_mix(u32 a, u32 b, int t, int n) {                                    /* a at t = 0 ... b at t = n */
    u32 r = (((a >> 16) & 255) * (u32)(n - t) + ((b >> 16) & 255) * (u32)t) / (u32)n;
    u32 g = (((a >> 8) & 255) * (u32)(n - t) + ((b >> 8) & 255) * (u32)t) / (u32)n;
    u32 bl = ((a & 255) * (u32)(n - t) + (b & 255) * (u32)t) / (u32)n;
    return (r << 16) | (g << 8) | bl;
}
/* The text twice as big (each pixel a 2 x 2 block): drawn small, read back, drawn again. */
static void ob_text2x(int x, int y, const char *s, u32 col, u32 bg) {
    static u8 m[20][260];
    int w = ko_string_width(s), h = FONT_LINE_H;
    if (w > 258) w = 258;
    if (h > 20) h = 20;
    bb_fillrect(x, y, w, h, bg);
    ko_draw_mixed_string(x, y, s, col);
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
        int px = x + i, py = y + j;
        m[j][i] = (px >= 0 && py >= 0 && px < VGA_WIDTH && py < VGA_HEIGHT) ? backbuf[py * VGA_WIDTH + px] != bg : 0;
    }
    bb_fillrect(x, y, w, h, bg);
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) if (m[j][i]) bb_fillrect(x + i * 2, y + j * 2, 2, 2, col);
}
/* The 32 x 32 icon `idx` magnified `k` times (a pixel that is more than half opaque). */
static void ob_icon_scaled(int x, int y, int idx, int k) {
    if (idx < 0 || idx >= ICONC_COUNT || !icon_cache_ok[idx]) return;
    const u32 *src = icon_cache[idx];
    for (int j = 0; j < 32; j++) for (int i = 0; i < 32; i++) { u32 p = src[j * 32 + i]; if ((p >> 24) > 127) bb_fillrect(x + i * k, y + j * k, k, k, p & 0x00FFFFFFu); }
}
/* The two pictures of the left panel for a phase (the icons magnified: MiniWin has no photographs). */
static void ob_panel_art(const ob_geo_t *g, int phase) {
    static const int top[4] = { ICONC_WEB, ICONC_DOC, ICONC_NOTEPAD, ICONC_SETTING };
    static const int bot[4] = { ICONC_TERM, ICONC_FILES, ICONC_SETTING, ICONC_INSTALL };
    int k = g->panel >= 150 ? 4 : 2, sz = 32 * k, y0 = g->logo + g->bar + 24;
    ob_icon_scaled((g->panel - sz) / 2 - 4, y0, top[phase], k);
    if (g->dh - 34 - 64 > y0 + sz + 8) ob_icon_scaled((g->panel - 64) / 2 - 4, g->dh - 34 - 64 - 6, bot[phase], 2);
}
/* A gold button with rounded corners: `dir` -1 puts a left arrow before the label, +1 a right arrow after it, 0 none. */
static void ob_gbtn(int x, int y, int w, int h, const char *label, int pressed, int enabled, int dir) {
    u32 top = enabled ? (pressed ? 0xC9AE5E : 0xF6EBC4) : 0xEDE8D6, bot = enabled ? (pressed ? 0xE6D190 : 0xD8BE72) : 0xDAD3B8;
    u32 edge = enabled ? 0x8A6A1C : 0xB4AD92, ink = enabled ? 0x4A3300 : 0xA39C84;
    for (int j = 0; j < h; j++) bb_fillrect(x + 1, y + j, w - 2, 1, ob_mix(top, bot, j, h));
    bb_fillrect(x + 2, y, w - 4, 1, edge); bb_fillrect(x + 2, y + h - 1, w - 4, 1, edge);
    bb_fillrect(x, y + 2, 1, h - 4, edge); bb_fillrect(x + w - 1, y + 2, 1, h - 4, edge);
    bb_fillrect(x + 1, y + 1, 1, 1, edge); bb_fillrect(x + w - 2, y + 1, 1, 1, edge); bb_fillrect(x + 1, y + h - 2, 1, 1, edge); bb_fillrect(x + w - 2, y + h - 2, 1, 1, edge);
    int tw = ko_string_width(label), ax = 0, off = pressed ? 1 : 0;
    int total = tw + (dir ? 14 : 0), tx = x + (w - total) / 2 + off + (dir < 0 ? 14 : 0), ty = y + (h - FONT_CELL) / 2 + off;
    ko_draw_mixed_string(tx, ty, label, ink);
    if (dir) {                                                                        /* a small solid triangle */
        int cy = y + h / 2 + off; ax = dir < 0 ? x + (w - total) / 2 + off : tx + tw + 6;
        for (int i = 0; i < 4; i++) bb_fillrect(dir > 0 ? ax + i : ax + 3 - i, cy - 3 + i, 1, 7 - 2 * i, ink);
    }
}
static void ob_backdrop(const ob_geo_t *g, int phase) {
    bb_fillrect(0, 0, g->dw, g->dh, OB_BG);
    if (g->logo) {                                                                    /* the header: white on the left, deep blue on the right, the name */
        for (int x = 0; x < g->dw; x++) bb_fillrect(x, 0, 1, g->logo, ob_mix(0xF4F8FD, 0x1F4C96, x * x, g->dw * g->dw));
        for (int i = 0; i < 3; i++) bb_fillrect(g->dw - 150 + i * 46, 6 + i * 4, 40, 40 - i * 6, i == 0 ? 0x6C9AD8 : i == 1 ? 0x3F72BD : 0x2A5AA5);
        ob_text2x(16, 8, "MiniWin", 0x1B3C78, 0xE6EDF8);
        ko_draw_mixed_string(18, 8 + 2 * FONT_LINE_H + 1, MW_VERSION, 0x1B3C78);
    }
    for (int y = 0; y < g->bar; y++) bb_fillrect(0, g->logo + y, g->dw, 1, ob_mix(0x2D5FA8, 0xBFD6F0, y, g->bar));     /* the phase bar */
    bb_fillrect(0, g->logo + g->bar, g->dw, 1, 0x1C3F70);
    int n = 4;
    for (int i = 0; i < n; i++) {
        const char *t = ob_phase_name(i);
        int w = ko_string_width(t), x = (g->dw / n) * i + (g->dw / n - w) / 2;
        ko_draw_mixed_string(x, g->logo + 5, t, 0x000000);
        if (i == phase) ko_draw_mixed_string(x + 1, g->logo + 5, t, 0x000000);                                  /* the phase you are in is bold */
    }
    if (g->panel) {                                                                                      /* the blue panel, fading to white on its right edge */
        int top = g->logo + g->bar + 1;
        for (int x = 0; x < g->panel; x++) bb_fillrect(x, top, 1, g->dh - top, ob_mix(0x2A66C4, 0xFFFFFF, x * x, g->panel * g->panel));
        ob_panel_art(g, phase);
    }
    bb_fillrect(g->cx, g->by + 12, g->bx - 10 - g->cx, 1, OB_LINE);                                      /* the thin line, then the buttons */
}
static void ob_buttons(const ob_geo_t *g, int logon) {
    if (logon) { ob_gbtn(g->bx + 94, g->by, 88, 24, SKO("OK", "확인"), ob_btn == 2, 1, 1); return; }
    ob_gbtn(g->bx, g->by, 88, 24, SKO("Back", "뒤로"), ob_btn == 1, oobe_page > 0, -1);
    ob_gbtn(g->bx + 94, g->by, 88, 24, oobe_page == OB_N - 1 ? SKO("Finish", "마침") : SKO("Next", "다음"), ob_btn == 2, oobe_page != OB_LICENSE || ob_accept == 1, 1);
}
static void oobe_draw(void) {
    if (!oobe_open) return;
    ob_geo_t g; ob_geo(&g);
    int phase = oobe_mode == OBM_LOGON ? 0 : ob_phase(oobe_page);
    ob_backdrop(&g, phase);
    if (oobe_mode == OBM_LOGON) {
        if (g.hd > g.lh) ob_text2x(g.cx, g.cy, SKO("Welcome", "환영합니다"), OB_RED, OB_BG); else ko_draw_mixed_string(g.cx, g.cy, SKO("Welcome", "환영합니다"), OB_RED);
        char w[80]; u32 n = 0; w[0] = 0;
        append_str(w, &n, sizeof w, SKO("User name: ", "사용자 이름: ")); append_str(w, &n, sizeof w, sys_user);
        ko_draw_mixed_string(g.cx, g.cy + g.hd + 8, w, OB_INK);
        ko_draw_mixed_string(g.cx, g.cy + g.hd + 8 + g.lh + 4, SKO("Type your password:", "비밀번호를 입력하세요:"), OB_INK);
        ob_draw_field(&g, 0);
        if (ob_msg) ko_draw_mixed_string(g.cx, g.cy + g.hd + 8 + 2 * g.lh + 8 + FONT_CELL + 8 + 4, ob_msg, OB_RED);
        ko_draw_mixed_string(g.cx, g.by - g.lh - 6, SKO("Press the ENTER key on your keyboard or click OK to continue.", "계속하려면 키보드의 ENTER 키를 누르거나 확인을 누르세요."), OB_INK);
        ob_buttons(&g, 1);
        return;
    }
    if (g.hd > g.lh) ob_text2x(g.cx, g.cy, ob_title(oobe_page), OB_RED, OB_BG); else { ko_draw_mixed_string(g.cx, g.cy, ob_title(oobe_page), OB_RED); ko_draw_mixed_string(g.cx + 1, g.cy, ob_title(oobe_page), OB_RED); }
    int y = ob_wrap(g.cx, g.cy + g.hd + 4, g.cw, ob_hint(oobe_page), 1, OB_INK) + 6;
    switch (oobe_page) {
    case OB_LANG:
        ob_radio(g.cx + 8, y + 2, sys_language == LANG_ENGLISH, "English");
        ob_radio(g.cx + 8, y + 2 + g.rowh, sys_language == LANG_KOREAN, "한국어 (Korean)");
        break;
    case OB_IME:
        for (int i = 0; i < 4; i++) ob_radio(g.cx + 8, y + 2 + i * g.rowh, ob_ime == i, ob_ime_name(i));
        break;
    case OB_LICENSE: {
        int bx, by, bw, bh, ry;
        ob_lic_rect(&g, y, &bx, &by, &bw, &bh, &ry);
        ob_llb.x = bx; ob_llb.y = by; ob_llb.w = bw; ob_llb.h = bh; ob_llb.row_h = 15; ob_llb.count = ob_nlines;
        ob_llb.sel = -1;                                                              /* a text box: no line is "selected" */
        if (ob_nlines) lbx_draw(&ob_llb, ob_row_lic);
        else { ui_field(bx, by, bw, bh, TH_FIELD, 0); ko_draw_mixed_string(bx + 6, by + 6, SKO("The license text was not found on this disk. It is the GNU General Public License, version 3: https://www.gnu.org/licenses/gpl-3.0.html", "이 디스크에서 사용권 텍스트를 찾지 못했습니다. GNU 일반 공중 라이선스 버전 3입니다: https://www.gnu.org/licenses/gpl-3.0.html"), OB_INK); }
        ob_radio(g.cx + 4, ry + 2, ob_accept == 1, SKO("I accept the agreement", "계약에 동의합니다"));
        ob_radio(g.cx + 4, ry + 2 + g.rowh, ob_accept == 2, SKO("I do not accept the agreement", "계약에 동의하지 않습니다"));
        break; }
    case OB_USER:
        ko_draw_mixed_string(g.cx, y, SKO("User name:", "사용자 이름:"), OB_INK);
        ob_draw_field(&g, 0);
        break;
    case OB_PASS:
        ko_draw_mixed_string(g.cx, y, SKO("Password:", "비밀번호:"), OB_INK);
        ko_draw_mixed_string(g.cx, y + g.lh + 2 + (FONT_CELL + 8) + 8, SKO("Type it again:", "한 번 더 입력:"), OB_INK);
        ob_draw_field(&g, 0); ob_draw_field(&g, 1);
        break;
    case OB_DESK: {
        int bw = (g.cw - 24) / 3, bh = FONT_CELL + 14;
        for (int i = 0; i < SETTING_DESK_N; i++) {
            int bx = g.cx + 4 + (i % 3) * (bw + 8), by = y + 2 + (i / 3) * (bh + 8);
            u32 bg = i == 0 ? TH_DESKTOP : setting_desk[i].bg, fg = i == 0 ? TH_DESKTOP_TEXT : setting_desk[i].fg;
            bb_fillrect(bx, by, bw, bh, bg); bb_rect(bx, by, bw, bh, 0x606060);
            if (i == ob_desk) { bb_rect(bx - 2, by - 2, bw + 4, bh + 4, OB_INK); bb_rect(bx - 3, by - 3, bw + 6, bh + 6, OB_INK); }
            ko_draw_mixed_string(bx + 6, by + 7, sys_language == LANG_KOREAN ? setting_desk[i].ko : setting_desk[i].en, fg);
        }
        { int py = y + 2 + 2 * (bh + 8) + 6, ph = g.by - (VGA_HEIGHT >= 400 ? g.lh + 14 : 10) - py;                                      /* a small desktop in the chosen colour */
          if (ph >= 28) { int pw = ph * 4 / 3 > g.cw - 8 ? g.cw - 8 : ph * 4 / 3;
              bb_fillrect(g.cx + 4, py, pw, ph, desktop_bg); bb_rect(g.cx + 4, py, pw, ph, 0x606060);
              bb_fillrect(g.cx + 5, py + ph - 8, pw - 2, 7, 0xC0C0C0); bb_fillrect(g.cx + 9, py + 6, 10, 10, desktop_fg); } }
        break; }
    case OB_SCREEN:
        for (int i = 0; i < 3; i++) {
            char t[64]; u32 n = 0; t[0] = 0;
            append_str(t, &n, sizeof t, ob_screen_name(i));
            if (i == disp_size_pos()) append_str(t, &n, sizeof t, SKO("  (now)", "  (지금)"));
            ob_radio(g.cx + 8, y + 2 + i * g.rowh, ob_screen == i, t);
        }
        break;
    case OB_ZONE: {
        int lh = g.by - 10 - (y + 4 + g.rowh), rows = lh / 15; if (rows > 8) rows = 8; if (rows < 2) rows = 2;
        ob_zlb.x = g.cx; ob_zlb.y = y + 2; ob_zlb.w = g.cw > 320 ? 320 : g.cw; ob_zlb.h = rows * 15 + 2; ob_zlb.row_h = 15; ob_zlb.count = TZ_N;
        lbx_draw(&ob_zlb, ob_row_zone);
        ui_checkbox_label(g.cx + 4, y + 2 + rows * 15 + 8, SKO("Adjust the clock for daylight saving automatically", "일광 절약 시간제 자동 조정"), ob_dst, 1);
        break; }
    default: {
        static const char *lab_en[5] = { "Year", "Month", "Day", "Hour", "Minute" }, *lab_ko[5] = { "년", "월", "일", "시", "분" };
        for (int i = 0; i < 5; i++) { int x, fy, w, h; ob_frect(&g, i, &x, &fy, &w, &h); ko_draw_mixed_string(x, fy - g.lh - 1, sys_language == LANG_KOREAN ? lab_ko[i] : lab_en[i], OB_INK); ob_draw_field(&g, i); }
        ko_draw_mixed_string(g.cx, y + g.lh + 2 + (FONT_CELL + 8) + 14, tz_zones[ob_zone].name, OB_INK);
        break; }
    }
    if (ob_msg) ko_draw_mixed_string(g.cx, g.by - g.lh - 4, ob_msg, OB_RED);
    else if (VGA_HEIGHT >= 400 && oobe_page != OB_LICENSE) ko_draw_mixed_string(g.cx, g.by - g.lh - 6, oobe_page == OB_N - 1 ? SKO("When you are ready, press the ENTER key or click Finish.", "준비가 되면 ENTER 키를 누르거나 마침을 누르세요.") : SKO("When you are ready to continue, press the ENTER key or click Next.", "계속하려면 ENTER 키를 누르거나 다음을 누르세요."), OB_INK);
    else if (VGA_HEIGHT >= 400) ko_draw_mixed_string(g.cx, g.by - g.lh - 6, SKO("To continue, choose one of the above and click Next.", "계속하려면 위 항목 중 하나를 고른 뒤 다음을 누르세요."), OB_INK);
    ob_buttons(&g, 0);
}

/* ---- leaving a page, finishing ---- */
static int ob_fields_nonempty_name(void) { return syscfg_name_ok(ob_user); }
static void ob_finish(void) {
    rtc_time_t t; int set_clock = ob_dt_changed();
    if (!ob_dt_get(&t)) { ob_msg = SKO("That is not a real date and time.", "올바른 날짜와 시간이 아닙니다."); return; }
    mwfs_strcpy(sys_user, ob_user, sizeof sys_user);
    upw_store(ob_pw1);
    tz_zone = ob_zone; tz_dst_auto = ob_dst;
    if (set_clock) {                                                      /* typed as local time: the chip keeps UTC */
        rtc_time_t guess = tz_shift(t, -tz_zones[ob_zone].min);
        int off = tz_minutes_for(guess, ob_zone, ob_dst);
        rtc_time_t utc = tz_shift(t, -off);
        rtc_write(&utc);
    }
    syscfg_save();
    ob_license_free();
    oobe_open = 0; oobe_mode = OBM_NONE;
    if (ob_screen != disp_size_pos()) {                                    /* another screen size: the boot byte, then a restart (Settings > Display does the same) */
        if (disp_write_byte((u8)(disp_pos_to_byte(ob_screen) | (fontface_pref << 2) | DISP_PENDING))) disp_black_and_restart();
    }
}
static void ob_next(void) {
    ob_msg = 0;
    switch (oobe_page) {
    case OB_IME: ob_apply_ime(); break;
    case OB_LICENSE:
        if (ob_accept != 1) { ob_msg = ob_accept == 2 ? SKO("MiniWin cannot be set up unless you accept the license agreement.", "사용권 계약에 동의하지 않으면 MiniWin을 설정할 수 없습니다.") : SKO("Choose \"I accept the agreement\" to continue.", "계속하려면 \"계약에 동의합니다\"를 고르세요."); return; }
        break;
    case OB_USER: if (!ob_fields_nonempty_name()) { ob_msg = SKO("Type a name (letters, digits, Hangul).", "이름을 입력하세요 (글자, 숫자, 한글)."); return; } break;
    case OB_PASS: { u32 a = tf_len(ob_pw1), b = tf_len(ob_pw2), same = a == b; for (u32 i = 0; same && i < a; i++) same = ob_pw1[i] == ob_pw2[i];
                    if (!same) { ob_msg = SKO("The two passwords do not match.", "두 비밀번호가 서로 다릅니다."); return; } break; }
    case OB_DESK: ob_apply_desk(); break;
    case OB_ZONE: tz_zone = ob_zone; tz_dst_auto = ob_dst; break;
    case OB_DATE: ob_finish(); return;
    default: break;
    }
    ob_page_open(oobe_page + 1);
}
static void ob_back(void) { if (oobe_page > 0) ob_page_open(oobe_page - 1); }
static void ob_logon_ok(void) {
    if (upw_check(ob_logon_pw)) { oobe_open = 0; oobe_mode = OBM_NONE; ob_logon_pw[0] = 0; return; }
    ob_logon_pw[0] = 0; tf_to_end(&ob_logon_tf, ob_logon_pw);
    ob_msg = SKO("That password is not right.", "비밀번호가 맞지 않습니다.");
}

/* ---- boot ---- */
static void oobe_start_setup(void) {
    oobe_open = 1; oobe_mode = OBM_SETUP; ob_btn = 0; ob_msg = 0; ob_accept = 0;
    ob_desk = setting_desk_idx; ob_screen = disp_size_pos(); ob_zone = tz_zone; ob_dst = tz_dst_auto;
    ob_ime = ime_enabled[IME_ENGLISH] && ime_enabled[IME_KOREAN] ? 2 : ime_enabled[IME_KOREAN] ? 1 : 0;
    mwfs_strcpy(ob_user, "User", sizeof ob_user);
    ob_pw1[0] = ob_pw2[0] = 0;
    ob_page_open(OB_LANG);
}
static void oobe_start_logon(void) {
    oobe_open = 1; oobe_mode = OBM_LOGON; ob_btn = 0; ob_msg = 0; ob_focus = 0;
    ob_logon_pw[0] = 0; tf_to_end(&ob_logon_tf, ob_logon_pw);
}
/* Once, at the first frame (after the system font was applied): a live disc or floppy has nothing to do; an installed system reads its settings and
 * either runs the setup (none yet) or asks for the password (there is one). */
static void oobe_tick(void) {
    static int done = 0;
    if (done) return;
    done = 1;
    if (sys_is_live()) return;
    if (!syscfg_load()) { oobe_start_setup(); return; }
    if (upw_is_set()) oobe_start_logon();
}

/* ---- input ---- */
static int ob_btn_at(const ob_geo_t *g, int mx, int my) {
    if (oobe_mode == OBM_LOGON) return in_rect(mx, my, g->bx + 94, g->by, 88, 24) ? 2 : 0;
    if (oobe_page > 0 && in_rect(mx, my, g->bx, g->by, 88, 24)) return 1;
    return in_rect(mx, my, g->bx + 94, g->by, 88, 24) ? 2 : 0;
}
static void oobe_mouse(int mx, int my, int clicked) {
    ob_geo_t g; ob_geo(&g);
    int left = mouse_left;
    tf_poll_drag(mx, left);
    if (mouse_wheel) { if (oobe_mode == OBM_SETUP && oobe_page == OB_LICENSE) lbx_wheel(&ob_llb, mouse_wheel); else if (oobe_mode == OBM_SETUP && oobe_page == OB_ZONE) lbx_wheel(&ob_zlb, mouse_wheel); mouse_wheel = 0; }
    if (ob_btn && !left) {
        int b = ob_btn; ob_btn = 0;
        if (ob_btn_at(&g, mx, my) == b) { if (oobe_mode == OBM_LOGON) ob_logon_ok(); else if (b == 1) ob_back(); else ob_next(); }
        return;
    }
    if (!clicked) return;
    tf_ime_flush();
    int b = ob_btn_at(&g, mx, my);
    if (b) { ob_btn = b; return; }
    int f = ob_field_at(&g, mx, my);
    if (f >= 0) {
        int fl; u32 cap; tf_t *tf; char *buf = ob_fbuf(f, &cap, &tf, &fl);
        ob_focus = f; tf_mouse_down(tf, buf, fl, mx, keyboard_shift_held());
        return;
    }
    if (oobe_mode == OBM_LOGON) return;
    int y = ob_wrap(g.cx, g.cy + g.hd + 4, g.cw, ob_hint(oobe_page), 0, 0) + 6;
    switch (oobe_page) {
    case OB_LANG:
        for (int i = 0; i < 2; i++) if (in_rect(mx, my, g.cx + 8, y + 2 + i * g.rowh - 2, g.cw - 16, g.lh + 4)) { sys_language = i == 0 ? LANG_ENGLISH : LANG_KOREAN; ob_msg = 0; }
        break;
    case OB_IME:
        for (int i = 0; i < 4; i++) if (in_rect(mx, my, g.cx + 8, y + 2 + i * g.rowh - 2, g.cw - 16, g.lh + 4)) ob_ime = i;
        break;
    case OB_LICENSE: {
        int bx, by, bw, bh, ry;
        ob_lic_rect(&g, y, &bx, &by, &bw, &bh, &ry);
        if (ob_nlines && lbx_click(&ob_llb, mx, my)) { ob_llb.sel = -1; break; }
        for (int i = 0; i < 2; i++) if (in_rect(mx, my, g.cx + 4, ry + 2 + i * g.rowh - 2, g.cw - 8, g.lh + 4)) { ob_accept = i + 1; ob_msg = 0; }
        break; }
    case OB_DESK: {
        int bw = (g.cw - 24) / 3, bh = FONT_CELL + 14;
        for (int i = 0; i < SETTING_DESK_N; i++) {
            int bx = g.cx + 4 + (i % 3) * (bw + 8), by = y + 2 + (i / 3) * (bh + 8);
            if (in_rect(mx, my, bx, by, bw, bh)) { ob_desk = i; ob_apply_desk(); }
        }
        break; }
    case OB_SCREEN:
        for (int i = 0; i < 3; i++) if (in_rect(mx, my, g.cx + 8, y + 2 + i * g.rowh - 2, g.cw - 16, g.lh + 4)) ob_screen = i;
        break;
    case OB_ZONE: {
        if (lbx_click(&ob_zlb, mx, my)) { if (ob_zlb.sel >= 0) ob_zone = ob_zlb.sel; break; }
        if (in_rect(mx, my, g.cx + 4, ob_zlb.y + ob_zlb.h + 6, g.cw - 8, g.lh + 4)) ob_dst = !ob_dst;
        break; }
    default: break;
    }
}
static void oobe_key(int k) {
    if (k == KEY_NONE) return;
    if (k == KEY_RALT) { tf_ime_flush(); ime_cycle_next(); return; }          /* no window is open at boot, so the global handler does not run */
    if (k == '\n') { if (oobe_mode == OBM_LOGON) ob_logon_ok(); else ob_next(); return; }
    if (k == 27) return;                                                        /* the setup cannot be skipped */
    if (oobe_mode == OBM_SETUP && oobe_page == OB_LICENSE) {                    /* the licence box scrolls with the keys */
        if (k == KEY_UP) lbx_wheel(&ob_llb, -1); else if (k == KEY_DOWN) lbx_wheel(&ob_llb, 1);      /* the wheel counts notches DOWN as positive */
        else if (k == ' ') lbx_wheel(&ob_llb, 4);
        else if (k == KEY_HOME) ob_llb.top = 0;
        else if (k == KEY_END) ob_llb.top = ob_nlines > 0 ? ob_nlines - 1 : 0;
        else if (k == 'a' || k == 'A') { ob_accept = 1; ob_msg = 0; }
        else if (k == 'd' || k == 'D') { ob_accept = 2; ob_msg = 0; }
        return;
    }
    int nf = ob_nfields();
    if (k == '\t' && nf > 1) { tf_ime_flush(); ob_focus = (ob_focus + 1) % nf; return; }
    if (nf == 0) return;
    int fl; u32 cap; tf_t *tf; char *buf = ob_fbuf(ob_focus, &cap, &tf, &fl);
    if (oobe_mode == OBM_SETUP && oobe_page == OB_DATE && k >= 32 && k < 127 && (k < '0' || k > '9')) return;      /* digits only */
    ob_msg = 0;
    tf_key(tf, buf, cap, fl, k, 0);
}

#endif
