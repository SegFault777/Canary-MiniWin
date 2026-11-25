/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_FONTTF_H
#define MW_FONTTF_H

/* fonttf.h -- the TrueType fonts the user has installed (1.1-alpha-14): a short list, and the building of a face out of one.
 *
 * "Installing" a font (Control Panel > Font > Install New Font) copies the .ttf into /lib/fonts and adds it to this list; the list is rebuilt from
 * /lib/fonts whenever the Fonts folder opens, so what was copied survives a restart on an installed disk. A font installed without the copy
 * (the check box off) is listed by its path only, for this session. The list is also where the face's SIZE lives: the cell a font is drawn at.
 *
 * A face is made when it is needed -- to be looked at or to become the system font -- by ttf.h: the .ttf is read into the heap, every Hangul
 * syllable, jamo and Latin glyph is rasterized at the cell size (ppem = cell + 1, like Galmuri), and what the font lacks (Hangul in a Latin-only
 * font) is taken from the Galmuri face of the same size, which is why only the sizes of the pack's regular faces (7, 9, 11, 14) are offered. The result is a heap block in the font
 * pack's layout, so fontface.h treats it like any other face. It takes a moment (about 11,300 glyphs); nothing is cached on disk.
 *
 * KEEPING one as the system font across a restart: the boot sector's byte 440 (setting_display.h) holds a pack index + 1 in bits 2-4, and the value 7
 * there means "a TrueType font": which one, and at what size, is the little text file /etc/FONT.CFG (the font's path, a new line, the size). Only a
 * font that lives in /lib/fonts can be kept -- a font used from another disk may not be there at boot. At boot the face is built again (about 11,300
 * glyphs, with a progress bar); if that fails (the file is gone, no memory) the default face stays.
 * Needs ttf.h, fontpack.h, font.h, font_ko.h, vfs.h, mm.h. */

#define UFT_MAX       8
#define UFT_DIR       "/lib/fonts"
#define UFT_FILE_MAX  (16u << 20)
#define UFT_CFG       "/etc/FONT.CFG"
#define UFT_PREF      7                                /* the value of bits 2-4 of the boot byte that means "the TrueType font of UFT_CFG" */

typedef struct { char name[24]; char path[VFS_PATH_MAX]; u8 cell; } uft_ent_t;
static uft_ent_t uft_ent[UFT_MAX];
static int uft_n = 0;

/* The descender rows of a TrueType face at `cell` (that of the Galmuri face of the same size), or -1 if that size is not offered. */
static int uft_desc_of(int cell) {
    if (cell == 11) return FONT_LATIN_DESC;
    for (int i = 1; i < fp_n; i++) if (fp_ent[i].cell == cell && !fp_ent[i].bold) return fp_ent[i].desc;
    return -1;
}
/* The next offered size after `cell`: 7, 9, 11, 14, and round again. */
static int uft_next_cell(int cell) {
    static const int c[4] = { 7, 9, 11, 14 };
    int i0 = 0;
    for (int i = 0; i < 4; i++) if (c[i] == cell) i0 = i;
    for (int k = 1; k <= 4; k++) { int cc = c[(i0 + k) % 4]; if (uft_desc_of(cc) >= 0) return cc; }
    return cell;
}

/* "NAME.TTF" / ".otf" / ".ttc" (any case)? */
static int uft_is_font_name(const char *n) {
    u32 l = 0; while (n[l]) l++;
    if (l < 5 || n[l - 4] != '.') return 0;
    char a = (char)(n[l - 3] | 32), b = (char)(n[l - 2] | 32), c = (char)(n[l - 1] | 32);
    return (a == 't' && b == 't' && (c == 'f' || c == 'c')) || (a == 'o' && b == 't' && c == 'f');
}
/* The name without its extension, at most 23 characters. */
static void uft_base(const char *file, char *out) {
    u32 l = 0; while (file[l]) l++;
    if (l > 4 && file[l - 4] == '.') l -= 4;
    if (l > 23) l = 23;
    for (u32 i = 0; i < l; i++) out[i] = file[i];
    out[l] = 0;
}
static void uft_copy(char *dst, const char *src, u32 cap) { u32 i = 0; while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; } dst[i] = 0; }

/* Lists a font (new, or with a fresh path); the index, or -1 when the list is full. */
static int uft_add(const char *name, const char *path) {
    for (int i = 0; i < uft_n; i++) if (mwfs_name_eq(uft_ent[i].name, name)) { uft_copy(uft_ent[i].path, path, VFS_PATH_MAX); return i; }
    if (uft_n >= UFT_MAX) return -1;
    uft_ent_t *e = &uft_ent[uft_n];
    uft_copy(e->name, name, sizeof e->name); uft_copy(e->path, path, VFS_PATH_MAX);
    e->cell = 11;
    return uft_n++;
}

/* Lists every font file in /lib/fonts that is not listed yet. */
static void uft_scan(void) {
    vfs_node_t d; mwfs_dirent_t e;
    if (vfs_stat(UFT_DIR, &d) != MWFS_OK || d.type != MWFS_T_DIR) return;
    for (u32 i = 0; i < 64 && vfs_ls_nth(&d, i, &e) == 1; i++) {
        if (e.type != MWFS_T_FILE || !uft_is_font_name(e.name)) continue;
        char name[24], path[VFS_PATH_MAX]; u32 n = 0;
        uft_base(e.name, name);
        path[0] = 0; append_str(path, &n, sizeof path, UFT_DIR); append_str(path, &n, sizeof path, "/"); append_str(path, &n, sizeof path, e.name);
        uft_add(name, path);
    }
}

/* A whole file into the heap (0 if it is missing, empty, too big or unreadable); *n = its size. */
static u8 *uft_read(const char *path, u32 *n) {
    vfs_node_t nd;
    if (vfs_stat(path, &nd) != MWFS_OK || nd.type != MWFS_T_FILE) return 0;
    u32 sz = vfs_size(&nd);
    if (sz < 64 || sz > UFT_FILE_MAX) return 0;
    u8 *b = (u8 *)kmalloc(sz);
    if (!b) return 0;
    u32 len = 0, aux = 0;
    if (vfs_read(path, b, sz, &len, &aux) != MWFS_OK || len != sz) { kfree(b); return 0; }
    *n = sz;
    return b;
}

/* The Galmuri face of this size (regular), in the pack's layout, as a heap block: the glyphs a TrueType font lacks come from it. */
static u16 *uft_fallback(int cell, int desc) {
    u16 *b = (u16 *)kmalloc(ttf_face_rows(cell, desc) * 2u);
    if (!b) return 0;
    if (cell == 11) {                                            /* the built-in face: three tables in three places, here joined */
        const u16 *ko = &font8x8_ko[0][0], *ja = &font8x8_ko_jamo[0][0], *la = &font_latin[0][0];
        u16 *p = b;
        for (u32 i = 0; i < (u32)KO_SYLLABLE_COUNT * 11u; i++) *p++ = ko[i];
        for (u32 i = 0; i < (u32)KO_JAMO_COUNT * 11u; i++) *p++ = ja[i];
        for (u32 i = 0; i < 95u * (u32)(11 + desc); i++) *p++ = la[i];
        return b;
    }
    for (int i = 1; i < fp_n; i++)
        if (fp_ent[i].cell == cell && !fp_ent[i].bold) { if (fp_load(i, (u8 *)b)) return b; break; }
    kfree(b);
    return 0;
}

/* "Loading font..." and a bar, drawn straight onto the screen while a face is being rasterized (ttf_progress). */
static void uft_progress_draw(u32 done, u32 total) {
    int w = 240, h = 58, x = (VGA_WIDTH - w) / 2, y = (VGA_HEIGHT - h) / 2;
    bb_fillrect(x, y, w, h, 0xC0C0C0); bb_rect(x, y, w, h, 0x000000); bb_rect(x + 1, y + 1, w - 2, h - 2, 0xFFFFFF);
    ko_draw_mixed_string(x + 16, y + 10, SKO("Loading font...", "글꼴을 불러오는 중..."), 0x000000);
    bb_rect(x + 16, y + 32, w - 32, 14, 0x808080);
    bb_fillrect(x + 17, y + 33, (int)((done * (u32)(w - 34)) / total), 12, 0x000080);
    vga_present();
}

/* The face of listed font k at its size, a heap block in the pack's layout; 0 if the file is gone or damaged or there is no memory. */
static u8 *uft_build(int k) {
    if (k < 0 || k >= uft_n) return 0;
    const uft_ent_t *e = &uft_ent[k];
    int cell = e->cell, desc = uft_desc_of(cell);
    if (desc < 0) return 0;
    u32 fn = 0; u8 *file = uft_read(e->path, &fn);
    if (!file) return 0;
    ttf_t t;
    if (ttf_open(&t, file, fn) != TTF_OK) { kfree(file); return 0; }
    u16 *fb = uft_fallback(cell, desc);
    ttf_work_t *w = (ttf_work_t *)kmalloc(sizeof *w);
    u16 *out = (u16 *)kmalloc(ttf_face_rows(cell, desc) * 2u);
    u32 got[3] = { 0, 0, 0 };
    if (w && out) { ttf_progress = uft_progress_draw; ttf_make_face(&t, w, cell + 1, cell, desc, fb, out, got); ttf_progress = 0; }
    if (w) kfree(w);
    kfree(file);
    if (fb) kfree(fb);
    if (out && got[0] + got[1] + got[2] == 0) { kfree(out); out = 0; }      /* a font with none of our characters is no use */
    return (u8 *)out;
}

/* Remembers listed font k (with its size) as the system font of the next boots. 1 = written; 0 = it cannot be kept (the font is not in /lib/fonts, or the write failed). */
static int uft_save_cfg(int k) {
    if (k < 0 || k >= uft_n) return 0;
    const char *p = uft_ent[k].path, *d = UFT_DIR "/";
    for (int i = 0; d[i]; i++) if (((p[i] | 32) != (d[i] | 32)) && !(p[i] == '/' && d[i] == '/')) return 0;
    char buf[VFS_PATH_MAX + 8]; u32 n = 0; buf[0] = 0;
    append_str(buf, &n, sizeof buf, p); append_str(buf, &n, sizeof buf, "\n");
    { char c[4]; int v = uft_ent[k].cell, m = 0; if (v >= 10) c[m++] = (char)('0' + v / 10); c[m++] = (char)('0' + v % 10); c[m] = 0; append_str(buf, &n, sizeof buf, c); }
    append_str(buf, &n, sizeof buf, "\n");
    int was = vfs_priv; vfs_priv = 1;                              /* /etc is a system directory */
    int e = vfs_write(UFT_CFG, (const u8 *)buf, n, 0);
    vfs_priv = was;
    return e == MWFS_OK;
}
/* At boot: lists /lib/fonts, reads UFT_CFG and returns the index of the font it names (its size set), or -1. */
static int uft_load_cfg(void) {
    uft_scan();
    u8 buf[VFS_PATH_MAX + 8]; u32 len = 0, aux = 0;
    if (vfs_read(UFT_CFG, buf, sizeof buf - 1, &len, &aux) != MWFS_OK || len < 3) return -1;
    buf[len] = 0;
    char path[VFS_PATH_MAX]; u32 i = 0;
    while (i < len && buf[i] != '\n' && i + 1 < sizeof path) { path[i] = (char)buf[i]; i++; }
    path[i] = 0;
    int cell = 0;
    for (i++; i < len && buf[i] >= '0' && buf[i] <= '9'; i++) cell = cell * 10 + (buf[i] - '0');
    for (int k = 0; k < uft_n; k++)
        if (mwfs_name_eq(uft_ent[k].path, path)) { if (uft_desc_of(cell) >= 0) uft_ent[k].cell = (u8)cell; return k; }
    return -1;
}

#endif
