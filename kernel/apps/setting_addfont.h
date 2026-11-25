/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_APPS_SETTING_ADDFONT_H
#define MW_APPS_SETTING_ADDFONT_H

/* apps/setting_addfont.h -- "Add Fonts", the dialog of Control Panel > Font > Install New Font (1.1-alpha-14), in the manner of Windows 95.
 *
 *   List of fonts:   the font files (.ttf .otf .ttc) of the open folder; a click selects or deselects one, Select All takes them all.
 *   Folders:         the open folder; double-click a folder to go into it, ".." to go up.
 *   Drives:          the system disk ("MiniWin") and every volume mounted under /mnt.
 *   Copy fonts to Fonts folder:  on, OK copies each .ttf into /lib/fonts (the system folder: the dialog may write there, a user in MiniFiles may not),
 *                    so the font stays after a restart; off, the font is listed by its path for this session only.
 *   OK installs the selected fonts (fonttf.h lists them, the Fonts folder shows them), Cancel / Esc leaves. Help says what to do. Network... is
 *   there and grey: MiniWin has no network folders.
 * A font is checked when it is installed: it must open as TrueType (an .otf with PostScript outlines does not). The dialog is modal over the Fonts
 * folder: it needs a window of about 300 x 230 pixels and does not open on the small 320 x 200 page. Built from ui/controls.h and ui/listbox.h.
 * Needs setting_font.h (its forward declarations and `fnt_*` state), fonttf.h, vfs.h. */

#define AFD_MAXF 64
#define AFD_MAXD 48
#define AFD_MAXV 8
#define AFD_BW   88                                    /* the button column */
#define AFD_NAME (MWFS_NAME_MAX + 1)

static char afd_dir[VFS_PATH_MAX];                     /* the open folder: a canonical absolute path */
static char afd_fn[AFD_MAXF][AFD_NAME]; static u8 afd_fsel[AFD_MAXF]; static int afd_nf;   /* the font files, and which are selected */
static char afd_dn[AFD_MAXD][AFD_NAME]; static int afd_nd;                                   /* the folders (".." first) */
static char afd_vn[AFD_MAXV][AFD_NAME]; static int afd_nv;                                   /* the volumes under /mnt */
static int afd_drive = 0;                              /* 0 = the system disk, k = afd_vn[k-1] */
static int afd_drop = 0;                               /* the drive list is open */
static int afd_copy = 1;                               /* "Copy fonts to Fonts folder" */
static int afd_btn = 0;                                /* the pressed control: 1 OK, 2 Cancel, 3 Select All, 4 Help, 5 the check box */
static const char *afd_msg = 0;
static int afd_last = -1; static u32 afd_last_tick = 0;  /* for the double click in the folder list */
static lbx_t afd_lf = { 0, 0, 0, 0, 15, 0, 0, -1 };
static lbx_t afd_ld = { 0, 0, 0, 0, 15, 0, 0, -1 };
static char afd_note[96];

typedef struct {
    int dx, dy, dw, dh;                                /* the dialog */
    int rowh, lh;
    int fl_x, fl_y, fl_w, fl_h, fr;                    /* the font list and its rows */
    int bx, by;                                        /* the first button */
    int fd_x, fd_y, fd_w, fd_h, dr;                    /* the folder list */
    int dd_x, dd_y, dd_w, dd_h;                        /* the drive drop-down */
    int cb_x, cb_y;                                    /* the check box */
    int msg_y;
} afd_geo_t;

static int afd_cmp(const char *a, const char *b) {
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
}
static void afd_sort(char (*v)[AFD_NAME], int from, int n) {
    for (int i = from + 1; i < n; i++) {
        char t[AFD_NAME]; mwfs_strcpy(t, v[i], AFD_NAME);
        int k = i - 1;
        while (k >= from && afd_cmp(v[k], t) > 0) { mwfs_strcpy(v[k + 1], v[k], AFD_NAME); k--; }
        mwfs_strcpy(v[k + 1], t, AFD_NAME);
    }
}
static void afd_root_path(char *out) {
    u32 n = 0; out[0] = 0;
    if (afd_drive <= 0 || afd_drive > afd_nv) { out[0] = '/'; out[1] = 0; return; }
    append_str(out, &n, VFS_PATH_MAX, "/mnt/"); append_str(out, &n, VFS_PATH_MAX, afd_vn[afd_drive - 1]);
}
static int afd_at_root(void) { char r[VFS_PATH_MAX]; afd_root_path(r); return afd_cmp(afd_dir, r) == 0; }

static void afd_load_vols(void) {
    vfs_node_t d; mwfs_dirent_t e;
    afd_nv = 0;
    if (vfs_stat("/mnt", &d) != MWFS_OK) return;
    for (u32 i = 0; i < 32 && afd_nv < AFD_MAXV && vfs_ls_nth(&d, i, &e) == 1; i++)
        if (e.type == MWFS_T_DIR) mwfs_strcpy(afd_vn[afd_nv++], e.name, AFD_NAME);
}
/* Reads the open folder: its sub-folders and its font files. */
static void afd_scan(void) {
    vfs_node_t d; mwfs_dirent_t e;
    afd_nf = afd_nd = 0;
    for (int i = 0; i < AFD_MAXF; i++) afd_fsel[i] = 0;
    if (!afd_at_root()) mwfs_strcpy(afd_dn[afd_nd++], "..", AFD_NAME);
    int first = afd_nd;
    if (vfs_stat(afd_dir, &d) == MWFS_OK && d.type == MWFS_T_DIR)
        for (u32 i = 0; i < 512 && vfs_ls_nth(&d, i, &e) == 1; i++) {
            if (e.type == MWFS_T_DIR) { if (afd_nd < AFD_MAXD) mwfs_strcpy(afd_dn[afd_nd++], e.name, AFD_NAME); }
            else if (uft_is_font_name(e.name) && afd_nf < AFD_MAXF) mwfs_strcpy(afd_fn[afd_nf++], e.name, AFD_NAME);
        }
    afd_sort(afd_dn, first, afd_nd); afd_sort(afd_fn, 0, afd_nf);
    afd_lf.count = afd_nf; afd_lf.top = 0; afd_lf.sel = -1;
    afd_ld.count = afd_nd; afd_ld.top = 0; afd_ld.sel = -1;
    afd_last = -1;
}
static void afd_join(char *out, const char *dir, const char *leaf) {
    u32 n = 0; out[0] = 0;
    append_str(out, &n, VFS_PATH_MAX, dir);
    if (!(dir[0] == '/' && dir[1] == 0)) append_str(out, &n, VFS_PATH_MAX, "/");
    append_str(out, &n, VFS_PATH_MAX, leaf);
}
static void afd_enter(const char *name) {
    if (name[0] == '.' && name[1] == '.' && !name[2]) {
        u32 l = 0; while (afd_dir[l]) l++;
        while (l > 1 && afd_dir[l - 1] != '/') l--;
        if (l > 1) l--;                                 /* the slash too, unless it is the root's */
        afd_dir[l] = 0;
        if (!afd_dir[0]) { afd_dir[0] = '/'; afd_dir[1] = 0; }
    } else {
        char p[VFS_PATH_MAX]; afd_join(p, afd_dir, name);
        u32 l = 0; while (p[l]) l++;
        if (l >= VFS_PATH_MAX - 1) { afd_msg = SKO("That path is too long.", "경로가 너무 깁니다."); return; }
        mwfs_strcpy(afd_dir, p, VFS_PATH_MAX);
    }
    afd_msg = 0;
    afd_scan();
}
static void afd_set_drive(int k) {
    afd_drive = k;
    afd_root_path(afd_dir);
    afd_msg = 0;
    afd_scan();
}

/* ---- geometry ---- */
static int afd_geo(const sheet_t *g, afd_geo_t *o) {
    o->rowh = FONT_CELL + 4; o->lh = FONT_LINE_H;
    int title_h = FONT_CELL + 4;
    o->dw = 348; if (o->dw > g->W - 4) o->dw = g->W - 4;
    if (o->dw < 300) return 0;
    static const int rows[3][2] = { { 6, 4 }, { 4, 3 }, { 3, 2 } };
    int k;
    for (k = 0; k < 3; k++) {
        o->fr = rows[k][0]; o->dr = rows[k][1];
        o->fl_h = o->fr * o->rowh + 2; o->fd_h = o->dr * o->rowh + 2;
        o->dh = 3 + title_h + 6 + o->lh + o->fl_h + 6 + 2 * o->lh + o->fd_h + 6 + o->lh + 4;
        if (o->dh <= g->H - 4) break;
    }
    if (k == 3) return 0;
    o->dx = g->X + (g->W - o->dw) / 2; o->dy = g->Y + (g->H - o->dh) / 2;
    int y = o->dy + 3 + title_h + 6;
    o->fl_x = o->dx + 8; o->fl_y = y + o->lh; o->fl_w = o->dw - 8 - AFD_BW - 16;
    o->bx = o->dx + o->dw - 8 - AFD_BW; o->by = o->fl_y;
    int y2 = o->fl_y + o->fl_h + 6;
    o->fd_x = o->dx + 8; o->fd_w = 132; o->fd_y = y2 + 2 * o->lh;
    o->dd_x = o->fd_x + o->fd_w + 8; o->dd_y = y2 + o->lh - 2; o->dd_w = o->bx - 8 - o->dd_x; o->dd_h = FONT_CELL + 6;
    o->cb_x = o->dd_x; o->cb_y = o->fd_y + o->fd_h - 15;
    o->msg_y = o->fd_y + o->fd_h + 6;
    return 1;
}
static void afd_place(const afd_geo_t *o) {
    afd_lf.x = o->fl_x; afd_lf.y = o->fl_y; afd_lf.w = o->fl_w; afd_lf.h = o->fl_h; afd_lf.row_h = o->rowh; afd_lf.count = afd_nf;
    afd_ld.x = o->fd_x; afd_ld.y = o->fd_y; afd_ld.w = o->fd_w; afd_ld.h = o->fd_h; afd_ld.row_h = o->rowh; afd_ld.count = afd_nd;
}
static int afd_btn_y(const afd_geo_t *o, int i) { static const int dy[5] = { 0, 28, 62, 90, 118 }; return o->by + dy[i]; }
static int afd_cb_w(void) { return 19 + ko_string_width(SKO("Copy fonts to Fonts folder", "글꼴을 글꼴 폴더에 복사")); }
static int afd_hit(const afd_geo_t *o, int mx, int my) {
    for (int i = 0; i < 4; i++) if (in_rect(mx, my, o->bx, afd_btn_y(o, i), AFD_BW, 22)) return i + 1;
    if (in_rect(mx, my, o->cb_x, o->cb_y, afd_cb_w(), 13)) return 5;
    return 0;
}

/* ---- drawing ---- */
static void afd_row_font(int idx, int x, int y, int w, int h, int sel) {
    (void)sel;
    int on = afd_fsel[idx];
    if (on) bb_fillrect(x, y, w, h, TH_ACCENT);
    char t[64]; u32 n = 0; t[0] = 0;
    append_str(t, &n, sizeof t, afd_fn[idx]); append_str(t, &n, sizeof t, " (TrueType)");
    ko_draw_mixed_string(x + 4, y + 2, t, on ? TH_TEXT_INVERSE : TH_FIELD_TEXT);
}
static void afd_row_dir(int idx, int x, int y, int w, int h, int sel) {
    (void)w; (void)h;
    ko_draw_mixed_string(x + 4, y + 2, afd_dn[idx], sel ? TH_TEXT_INVERSE : TH_FIELD_TEXT);
}
/* the tail of `s` that fits `maxw` pixels, with ".." in front when it had to be cut */
static void afd_fit(const char *s, int maxw, char *out, u32 cap) {
    if (ko_string_width(s) <= maxw) { mwfs_strcpy(out, s, cap); return; }
    u32 l = 0; while (s[l]) l++;
    for (u32 skip = 1; skip < l; skip++) {
        u32 n = 0; out[0] = 0; append_str(out, &n, cap, ".."); append_str(out, &n, cap, s + skip);
        if (ko_string_width(out) <= maxw) return;
    }
    mwfs_strcpy(out, s, cap);
}
static void afd_draw(const sheet_t *g) {
    afd_geo_t o;
    if (!afd_geo(g, &o)) return;
    afd_place(&o);
    bb_fillrect(o.dx, o.dy, o.dw, o.dh, TH_FACE); ui_bevel(o.dx, o.dy, o.dw, o.dh, 0);
    bb_fillrect(o.dx + 3, o.dy + 3, o.dw - 6, FONT_CELL + 4, TH_ACCENT);
    ko_draw_mixed_string(o.dx + 7, o.dy + 5, SKO("Add Fonts", "글꼴 추가"), TH_TEXT_INVERSE);
    ko_draw_mixed_string(o.fl_x, o.fl_y - o.lh, SKO("List of fonts:", "글꼴 목록:"), TH_TEXT);
    afd_lf.sel = -1;
    lbx_draw(&afd_lf, afd_row_font);
    if (afd_nf == 0) ko_draw_mixed_string(o.fl_x + 6, o.fl_y + 4, SKO("(no font files here)", "(여기에는 글꼴 파일이 없습니다)"), TH_FIELD_TEXT);
    ko_draw_mixed_string(o.fd_x, o.fd_y - 2 * o.lh, SKO("Folders:", "폴더:"), TH_TEXT);
    char p[VFS_PATH_MAX + 4]; afd_fit(afd_dir, o.fd_w, p, sizeof p);
    ko_draw_mixed_string(o.fd_x, o.fd_y - o.lh, p, TH_TEXT);
    lbx_draw(&afd_ld, afd_row_dir);
    ko_draw_mixed_string(o.dd_x, o.dd_y - o.lh + 2, SKO("Drives:", "드라이브:"), TH_TEXT);
    ui_field(o.dd_x, o.dd_y, o.dd_w, o.dd_h, TH_FIELD, 0);
    { char lab[40]; u32 n = 0; lab[0] = 0;
      if (afd_drive > 0 && afd_drive <= afd_nv) append_str(lab, &n, sizeof lab, afd_vn[afd_drive - 1]); else append_str(lab, &n, sizeof lab, "MiniWin (/)");
      bb_clip_set(o.dd_x + 2, o.dd_y + 1, o.dd_w - 22, o.dd_h - 2);
      ko_draw_mixed_string(o.dd_x + 4, o.dd_y + 3, lab, TH_FIELD_TEXT);
      bb_clip_off();
      ui_pushbutton(o.dd_x + o.dd_w - 17, o.dd_y + 1, 16, o.dd_h - 2, "", 0, 1);                 /* the arrow */
      int ax = o.dd_x + o.dd_w - 13, ay = o.dd_y + o.dd_h / 2 - 1;
      bb_fillrect(ax, ay, 9, 1, TH_TEXT); bb_fillrect(ax + 1, ay + 1, 7, 1, TH_TEXT); bb_fillrect(ax + 2, ay + 2, 5, 1, TH_TEXT);
      bb_fillrect(ax + 3, ay + 3, 3, 1, TH_TEXT); bb_fillrect(ax + 4, ay + 4, 1, 1, TH_TEXT); }
    ui_checkbox_label(o.cb_x, o.cb_y, SKO("Copy fonts to Fonts folder", "글꼴을 글꼴 폴더에 복사"), afd_copy, 1);
    ui_pushbutton(o.bx, afd_btn_y(&o, 0), AFD_BW, 22, "OK", afd_btn == 1, 1);
    ui_pushbutton(o.bx, afd_btn_y(&o, 1), AFD_BW, 22, SKO("Cancel", "취소"), afd_btn == 2, 1);
    ui_pushbutton(o.bx, afd_btn_y(&o, 2), AFD_BW, 22, SKO("Select All", "모두 선택"), afd_btn == 3, 1);
    ui_pushbutton(o.bx, afd_btn_y(&o, 3), AFD_BW, 22, SKO("Help", "도움말"), afd_btn == 4, 1);
    ui_pushbutton(o.bx, afd_btn_y(&o, 4), AFD_BW, 22, SKO("Network...", "네트워크..."), 0, 0);
    if (afd_msg) ko_draw_mixed_string(o.dx + 8, o.msg_y, afd_msg, TH_ACCENT);
    if (afd_drop) {                                                                       /* the open drive list, over the folder list */
        int rows = afd_nv + 1, ry = o.dd_y + o.dd_h;
        bb_fillrect(o.dd_x, ry, o.dd_w, rows * o.rowh + 2, TH_FIELD); ui_bevel(o.dd_x, ry, o.dd_w, rows * o.rowh + 2, 1);
        for (int i = 0; i < rows; i++) {
            const char *t = i == 0 ? "MiniWin (/)" : afd_vn[i - 1];
            if (i == afd_drive) bb_fillrect(o.dd_x + 1, ry + 1 + i * o.rowh, o.dd_w - 2, o.rowh, TH_ACCENT);
            ko_draw_mixed_string(o.dd_x + 4, ry + 3 + i * o.rowh, t, i == afd_drive ? TH_TEXT_INVERSE : TH_FIELD_TEXT);
        }
    }
}

/* ---- the installing ---- */
static void afd_put_num(char *out, u32 *n, int v) {
    char d[8]; int k = 0;
    if (v == 0) d[k++] = '0';
    while (v > 0 && k < 7) { d[k++] = (char)('0' + v % 10); v /= 10; }
    while (k) { char c[2] = { d[--k], 0 }; append_str(out, n, 96, c); }
}
static void afd_install(void) {
    int sel = 0;
    for (int i = 0; i < afd_nf; i++) sel += afd_fsel[i];
    if (!sel) { afd_msg = SKO("Select a font first.", "먼저 글꼴을 선택하세요."); return; }
    int ok = 0, bad = 0, first = -1; const char *why = 0;
    for (int i = 0; i < afd_nf; i++) {
        if (!afd_fsel[i]) continue;
        char src[VFS_PATH_MAX], dst[VFS_PATH_MAX], name[24];
        afd_join(src, afd_dir, afd_fn[i]); afd_join(dst, UFT_DIR, afd_fn[i]); uft_base(afd_fn[i], name);
        u32 n = 0; u8 *buf = uft_read(src, &n);
        if (!buf) { bad++; why = SKO("The file could not be read.", "파일을 읽을 수 없습니다."); continue; }
        ttf_t t; int rc = ttf_open(&t, buf, n);
        if (rc != TTF_OK) {
            kfree(buf); bad++;
            why = rc == TTF_ERR_CFF ? SKO("PostScript (.otf) fonts are not supported.", "포스트스크립트(.otf) 글꼴은 지원하지 않습니다.") : SKO("That is not a usable TrueType font.", "사용할 수 있는 트루타입 글꼴이 아닙니다.");
            continue;
        }
        const char *use = src;
        if (afd_cmp(src, dst) == 0) use = dst;                                           /* it is in the Fonts folder already */
        else if (afd_copy) {
            int was = vfs_priv; vfs_priv = 1;                                            /* /lib is a system folder; installing a font is the one thing a user may do there */
            int e = vfs_mkdir(UFT_DIR);
            e = (e == MWFS_OK || e == MWFS_E_EXIST) ? vfs_write(dst, buf, n, 0) : e;
            vfs_priv = was;
            if (e == MWFS_OK) use = dst; else why = SKO("No room to copy it; listed for now.", "복사할 공간이 없어 이번에만 목록에 넣었습니다.");
        }
        kfree(buf);
        int k = uft_add(name, use);
        if (k >= 0) { ok++; first = fp_n + k; } else { bad++; why = SKO("Too many fonts are installed.", "설치된 글꼴이 너무 많습니다."); }
    }
    if (ok == 0) { afd_msg = why ? why : SKO("Nothing was installed.", "설치된 것이 없습니다."); return; }
    afd_on = 0; afd_btn = 0; afd_drop = 0;
    fontface_init_order();
    for (int i = 0; i < fontface_count; i++) if (fontface_order[i] == first) fnt_sel = i;
    fnt_last_sel = -1;
    u32 m = 0; afd_note[0] = 0;
    afd_put_num(afd_note, &m, ok); append_str(afd_note, &m, sizeof afd_note, SKO(ok == 1 ? " font installed." : " fonts installed.", "개의 글꼴을 설치했습니다."));
    if (why) { append_str(afd_note, &m, sizeof afd_note, " "); append_str(afd_note, &m, sizeof afd_note, why); }
    fnt_msg = afd_note;
}

/* ---- events ---- */
static void afd_open_dialog(const sheet_t *g) {
    afd_geo_t o;
    if (!afd_geo(g, &o)) { fnt_msg = SKO("The window is too small for that.", "창이 너무 작습니다."); return; }
    afd_load_vols();
    if (afd_drive > afd_nv) afd_drive = 0;
    if (!afd_dir[0]) afd_root_path(afd_dir);
    afd_on = 1; afd_btn = 0; afd_drop = 0; afd_msg = 0;
    afd_scan();
}
static void afd_fire(int b) {
    if (b == 1) afd_install();
    else if (b == 2) { afd_on = 0; afd_drop = 0; }
    else if (b == 3) { for (int i = 0; i < afd_nf; i++) afd_fsel[i] = 1; afd_msg = 0; }
    else if (b == 4) afd_msg = SKO("Pick font files, then press OK.", "글꼴 파일을 고른 뒤 OK를 누르세요.");
    else if (b == 5) afd_copy = !afd_copy;
}
static int afd_click(int mx, int my, u32 now) {
    sheet_t g; fnt_geo(&g);
    afd_geo_t o;
    if (!g.ok || !afd_geo(&g, &o)) { afd_on = 0; return 1; }
    afd_place(&o);
    if (afd_drop) {
        int rows = afd_nv + 1, ry = o.dd_y + o.dd_h;
        if (in_rect(mx, my, o.dd_x, ry, o.dd_w, rows * o.rowh + 2)) { int i = (my - ry - 1) / o.rowh; if (i >= 0 && i < rows && i != afd_drive) afd_set_drive(i); }
        afd_drop = 0;
        return 1;
    }
    int b = afd_hit(&o, mx, my);
    if (b) { afd_btn = b; return 1; }
    if (in_rect(mx, my, o.dd_x, o.dd_y, o.dd_w, o.dd_h)) { afd_drop = 1; return 1; }
    afd_lf.sel = -1;
    if (lbx_click(&afd_lf, mx, my)) {
        if (afd_lf.sel >= 0) { afd_fsel[afd_lf.sel] ^= 1; afd_lf.sel = -1; afd_msg = 0; }
        return 1;
    }
    if (lbx_click(&afd_ld, mx, my)) {
        int i = afd_ld.sel;
        if (i >= 0) {
            if (i == afd_last && (now - afd_last_tick) < double_click_window) { char nm[AFD_NAME]; mwfs_strcpy(nm, afd_dn[i], AFD_NAME); afd_enter(nm); }
            else { afd_last = i; afd_last_tick = now; }
        }
        return 1;
    }
    return 1;                                                                              /* modal: a click elsewhere does nothing */
}
static void afd_release(int mx, int my) {
    sheet_t g; fnt_geo(&g);
    afd_geo_t o;
    int b = afd_btn;
    afd_btn = 0;
    if (!b || !g.ok || !afd_geo(&g, &o)) return;
    if (afd_hit(&o, mx, my) == b) afd_fire(b);
}
static int afd_key(int k) {
    if (k == 27) { afd_on = 0; afd_drop = 0; }
    else if (k == '\n') afd_install();
    return 1;
}

#endif
