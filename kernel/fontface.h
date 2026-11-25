/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_FONTFACE_H
#define MW_FONTFACE_H

/* fontface.h -- changing the system font FACE at run time (1.1-alpha-14).
 *
 * A face is a cell size plus a Latin table and a Hangul table (kernel/font.h). The font pack (kernel/fontpack.h) carries Galmuri 7, 9, 11, 14 and
 * 11 Bold; face 0 -- the built-in Galmuri11 -- lives in kernel memory. fontface_use() loads another face into a heap block and points font.h's
 * variables at it: everything that draws or measures text goes through FONT_CELL, font_desc and the table pointers, so the whole desktop follows.
 * (The HTML engine draws from the built-in tables on purpose, scaled to the page's own font size.)
 *
 * A second, temporary face can be loaded just to be LOOKED AT (the preview in the Fonts folder): fontface_preview_enter() swaps the variables to it,
 * the caller draws, fontface_preview_leave() puts them back.
 * Needs fontpack.h (fp_ent, fp_n, fp_load), font.h, font_ko.h, mm.h. */

static int fontface_pref = 0;                   /* what the user chose, as kept in bits 2-4 of the boot sector's byte 440: 0 = the default for this screen, else pack index + 1 */
static int fontface_cur = 0;                    /* the pack index of the face in use (0 = the built-in Galmuri11) */
static u8 *fontface_buf = 0;                    /* the heap block of the loaded face (0 while face 0 is in use) */
static int fontface_order[FP_MAX + UFT_MAX];     /* the faces in the order the Fonts folder shows them: the pack's by size (Regular before Bold), then the installed TrueType fonts */
static int fontface_count = 0;

/* A face index: 0 .. fp_n-1 is a pack face, fp_n .. fp_n+uft_n-1 an installed TrueType font (fonttf.h). */
static int fontface_is_ttf(int idx) { return idx >= fp_n; }
static int fontface_cell(int idx) { return idx < fp_n ? fp_ent[idx].cell : uft_ent[idx - fp_n].cell; }
/* 1 = this face is the one on screen, at the size it is listed with (a TrueType font whose size was changed since needs building again) */
static int fontface_in_use(int idx) { return idx == fontface_cur && (idx < fp_n || uft_ent[idx - fp_n].cell == font_cell); }

static void fontface_init_order(void) {
    fontface_count = fp_n + uft_n;
    for (int i = 0; i < fp_n; i++) fontface_order[i] = i;
    for (int i = 1; i < fp_n; i++) {                                  /* insertion sort on (cell, bold) */
        int v = fontface_order[i], k = i - 1;
        while (k >= 0 && (fp_ent[fontface_order[k]].cell * 2 + fp_ent[fontface_order[k]].bold) > (fp_ent[v].cell * 2 + fp_ent[v].bold)) { fontface_order[k + 1] = fontface_order[k]; k--; }
        fontface_order[k + 1] = v;
    }
    for (int i = 0; i < uft_n; i++) fontface_order[fp_n + i] = fp_n + i;
}
/* "Galmuri11", "Galmuri11 Bold" ... */
static void fontface_name(int idx, char *out, u32 cap) {
    u32 n = 0; out[0] = 0;
    if (idx >= fp_n) { append_str(out, &n, cap, uft_ent[idx - fp_n].name); return; }
    append_str(out, &n, cap, "Galmuri");
    char d[4]; int c = fp_ent[idx].cell, nd = 0;
    if (c >= 10) d[nd++] = (char)('0' + c / 10);
    d[nd++] = (char)('0' + c % 10); d[nd] = 0;
    append_str(out, &n, cap, d);
    if (fp_ent[idx].bold) append_str(out, &n, cap, " Bold");
}
static int fontface_find(int cell, int bold) {
    for (int i = 0; i < fp_n; i++) if (fp_ent[i].cell == cell && !!fp_ent[i].bold == !!bold) return i;
    return -1;
}

typedef struct { int cell, desc; const u16 *latin, *ko, *jamo; } fontface_tables_t;

static void fontface_set_tables(const fontface_tables_t *t) {
    font_cell = t->cell; font_desc = t->desc;
    font_cur_latin = t->latin; font_cur_ko = t->ko; font_cur_ko_jamo = t->jamo;
}
static void fontface_save_tables(fontface_tables_t *t) {
    t->cell = font_cell; t->desc = font_desc; t->latin = font_cur_latin; t->ko = font_cur_ko; t->jamo = font_cur_ko_jamo;
}
/* The tables of pack face `idx` inside `buf` (face 0 = the built-in ones, buf unused). */
static void fontface_tables_of(int idx, u8 *buf, fontface_tables_t *t) {
    if (idx == 0) {
        t->cell = 11; t->desc = FONT_LATIN_DESC;
        t->latin = &font_latin[0][0]; t->ko = &font8x8_ko[0][0]; t->jamo = &font8x8_ko_jamo[0][0];
        return;
    }
    int cell = fontface_cell(idx);
    t->cell = cell; t->desc = idx < fp_n ? fp_ent[idx].desc : uft_desc_of(cell);
    t->ko = (const u16 *)buf;
    t->jamo = t->ko + (u32)KO_SYLLABLE_COUNT * cell;
    t->latin = t->jamo + (u32)KO_JAMO_COUNT * cell;
}
static u8 *fontface_load(int idx) {
    if (idx >= fp_n) return (idx < fp_n + uft_n) ? uft_build(idx - fp_n) : 0;      /* an installed TrueType font: rasterized now */
    if (idx <= 0) return 0;
    u8 *buf = (u8 *)kmalloc(fp_ent[idx].raw);
    if (!buf) return 0;
    if (!fp_load(idx, buf)) { kfree(buf); return 0; }
    return buf;
}

/* Makes pack face `idx` the system font. 1 = done (or already in use), 0 = it could not be loaded (the old face stays). */
static int fontface_use(int idx) {
    if (idx < 0 || idx >= fp_n + uft_n) return 0;
    if (fontface_in_use(idx)) return 1;
    u8 *nb = 0;
    if (idx != 0) { nb = fontface_load(idx); if (!nb) return 0; }
    fontface_tables_t t; fontface_tables_of(idx, nb, &t);
    fontface_set_tables(&t);
    u8 *old = fontface_buf; fontface_buf = nb; fontface_cur = idx;
    if (old) kfree(old);
    return 1;
}

/* ---- a face loaded only to be looked at ---- */
static u8 *fontface_pv_buf = 0;
static int fontface_pv_idx = -1;
static void fontface_pv_free(void) { if (fontface_pv_buf) kfree(fontface_pv_buf); fontface_pv_buf = 0; fontface_pv_idx = -1; }
static int fontface_pv_load(int idx) {
    if (idx == fontface_pv_idx) return 1;
    fontface_pv_free();
    if (idx == 0 || fontface_in_use(idx)) { fontface_pv_idx = idx; return 1; }          /* nothing to load: it is already in memory */
    fontface_pv_buf = fontface_load(idx);
    if (!fontface_pv_buf) return 0;
    fontface_pv_idx = idx;
    return 1;
}
/* Swap the drawing variables to the previewed face; the caller draws, then calls _leave with the same saved state. */
static int fontface_pv_enter(fontface_tables_t *saved) {
    if (fontface_pv_idx < 0) return 0;
    fontface_save_tables(saved);
    fontface_tables_t t;
    if (fontface_in_use(fontface_pv_idx)) return 1;                                   /* already the system face */
    fontface_tables_of(fontface_pv_idx, fontface_pv_buf, &t);
    fontface_set_tables(&t);
    return 1;
}
static void fontface_pv_leave(const fontface_tables_t *saved) { fontface_set_tables(saved); }

#endif
