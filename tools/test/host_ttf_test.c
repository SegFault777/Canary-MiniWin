/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for kernel/ttf.h (the TrueType rasterizer of the Fonts folder).
 *   host_ttf_test galmuri-dir ref-dir [other.ttf ...]
 *   - the Galmuri faces: the TTFs (galmuri-dir/Galmuri7.ttf ...) rasterized at ppem = cell + 1 must reproduce the font pack's faces
 *     (ref-dir/ttf_ref_<NAME>.raw, written by ttf_ref.py) -- Galmuri's outlines lie on the pixel grid, so a correct rasterizer is exact;
 *   - other fonts (any .ttf / .otf given): every glyph renders without a crash, space is blank, CFF .otf is refused with TTF_ERR_CFF;
 *   - hostile input: bit flips, truncation, a random table directory and random glyph data never crash or read outside the file (ASan/UBSan);
 *   - the fallback face fills the glyphs a font lacks. */
#include "rd_host.h"
#include "../../kernel/ttf.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static u8 *slurp(const char *path, u32 *n) {
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *b = malloc((size_t)sz + 1); if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); return 0; }
    fclose(f); *n = (u32)sz; return b;
}
static u32 rng = 12345;
static u32 rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static ttf_work_t *W;

static void galmuri(const char *dir, const char *refdir, const char *file, const char *refname) {
    char p[512]; u32 n, rn;
    snprintf(p, sizeof p, "%s/%s", dir, file);
    u8 *ttf = slurp(p, &n);
    if (!ttf) { printf("SKIP %s (not found)\n", p); return; }
    snprintf(p, sizeof p, "%s/ttf_ref_%s.raw", refdir, refname);
    u8 *ref = slurp(p, &rn);
    CHECK(ref != 0, "reference %s exists", p);
    if (!ref) { free(ttf); return; }
    int cell = ref[0], desc = ref[1];
    ttf_t t; int rc = ttf_open(&t, ttf, n);
    CHECK(rc == TTF_OK, "%s opens (rc %d)", file, rc);
    if (rc == TTF_OK) {
        u32 rows = ttf_face_rows(cell, desc);
        CHECK(rn == 2 + rows * 2, "%s: reference has %u rows, expected %u", refname, (unsigned)((rn - 2) / 2), (unsigned)rows);
        u16 *out = malloc(rows * 2); u32 got[3];
        ttf_make_face(&t, W, cell + 1, cell, desc, 0, out, got);
        CHECK(got[0] == TTF_SYLLABLES && got[1] == TTF_JAMO && got[2] == TTF_LATIN, "%s: glyphs found %u/%u/%u", refname, got[0], got[1], got[2]);
        const u16 *refrows = (const u16 *)(ref + 2);
        u32 bad_glyph[3] = {0, 0, 0}, bad_px = 0, shown = 0;
        u32 pos = 0;
        for (int part = 0; part < 3; part++) {
            int gr = part == 2 ? cell + desc : cell; u32 cnt = part == 0 ? TTF_SYLLABLES : part == 1 ? TTF_JAMO : TTF_LATIN;
            for (u32 i = 0; i < cnt; i++, pos += gr) {
                int diff = 0;
                for (int r = 0; r < gr; r++) { u16 x = out[pos + r] ^ refrows[pos + r]; if (x) { diff = 1; bad_px += __builtin_popcount(x); } }
                if (diff) { bad_glyph[part]++; if (shown < 3 && part == 2) { shown++; printf("  %s latin U+%04X differs\n", refname, 0x20 + i); } }
            }
        }
        printf("  %-11s cell %2d desc %d: differing glyphs: hangul %u/%d, jamo %u/%d, latin %u/%d (%u pixels)\n", refname, cell, desc,
               bad_glyph[0], TTF_SYLLABLES, bad_glyph[1], TTF_JAMO, bad_glyph[2], TTF_LATIN, bad_px);
        CHECK(bad_glyph[0] == 0 && bad_glyph[1] == 0 && bad_glyph[2] == 0, "%s reproduces the font pack face exactly", refname);
        free(out);
    }
    free(ttf); free(ref);
}

/* every glyph of a font, then a space is blank, 'A' has ink, Hangul falls back */
static void other(const char *path) {
    u32 n; u8 *d = slurp(path, &n);
    if (!d) { printf("SKIP %s\n", path); return; }
    ttf_t t; int rc = ttf_open(&t, d, n);
    if (rc == TTF_ERR_CFF) { CHECK(1, "CFF refused"); printf("  %s: CFF outlines, refused as designed\n", path); free(d); return; }
    CHECK(rc == TTF_OK, "%s opens (rc %d)", path, rc);
    if (rc == TTF_OK) {
        u16 rows[24]; int ink = 0;
        for (int cell = 7; cell <= 16; cell += 3)
            for (u32 cp = 0x20; cp < 0x7F; cp++) { u32 g = ttf_gid(&t, cp); if (g) ink += ttf_render(&t, W, g, cell + 1, cell, cell + 2, rows); }
        CHECK(ink > 100, "%s: Latin letters have ink (%d)", path, ink);
        CHECK(ttf_render(&t, W, ttf_gid(&t, 'A'), 12, 11, 13, rows) == 1, "%s: 'A' has ink", path);
        CHECK(ttf_render(&t, W, ttf_gid(&t, ' '), 12, 11, 13, rows) == 0, "%s: space is blank", path);
        for (int cell = 7; cell <= 14; cell += 7) {                      /* a face: Hangul comes from the fallback when the font lacks it */
            int desc = 2; u32 nr = ttf_face_rows(cell, desc); u16 *fb = malloc(nr * 2), *out = malloc(nr * 2); u32 got[3];
            for (u32 i = 0; i < nr; i++) fb[i] = 0x1234;
            ttf_make_face(&t, W, cell + 1, cell, desc, fb, out, got);
            if (got[0] == 0) CHECK(out[0] == 0x1234 && out[(TTF_SYLLABLES - 1) * cell] == 0x1234, "%s: missing Hangul comes from the fallback", path);
            CHECK(got[2] > 60, "%s: Latin glyphs supplied (%u)", path, got[2]);
            free(fb); free(out);
        }
        printf("  %s: ok (Hangul glyphs: %u)\n", path, ({ u32 c = 0; for (u32 i = 0; i < 100; i++) c += ttf_gid(&t, 0xAC00 + i) != 0; c; }));
    }
    free(d);
}

/* hostile input: nothing may crash or read outside the buffer (the buffer is exactly the size of the data, so ASan sees every overrun) */
static void fuzz(const char *path) {
    u32 n; u8 *d = slurp(path, &n);
    if (!d || n < 4096) { free(d); return; }
    if (n > 400000) n = 400000;                                          /* keep the iterations fast; the head of the file holds the tables we mutate */
    int opened = 0;
    for (int it = 0; it < 400; it++) {
        u32 m = (it % 4 == 3) ? (n / 2 + rnd() % (n / 2)) : n;           /* sometimes truncated */
        u8 *c = malloc(m); memcpy(c, d, m);
        int flips = 1 + rnd() % 12;
        for (int k = 0; k < flips; k++) {
            u32 pos = (it % 2) ? rnd() % (m < 600 ? m : 600) : rnd() % m;   /* half the time in the table directory / head */
            c[pos] ^= (u8)(1u << (rnd() % 8));
            if (rnd() % 3 == 0) c[pos] = (u8)rnd();
        }
        ttf_t t;
        if (ttf_open(&t, c, m) == TTF_OK) {
            opened++;
            u16 rows[24];
            for (u32 cp = 0x20; cp < 0x7F; cp++) ttf_render(&t, W, ttf_gid(&t, cp), 12, 11, 13, rows);
            for (u32 cp = 0xAC00; cp < 0xAC00 + 60; cp++) ttf_render(&t, W, ttf_gid(&t, cp), 12, 11, 11, rows);
            for (u32 g = 0; g < 64 && g < t.nglyph; g++) ttf_render(&t, W, g, 12, 11, 13, rows);
        }
        free(c);
    }
    CHECK(1, "fuzz %s survived (%d of 400 mutated copies still opened)", path, opened);
    free(d);
    /* pure noise and tiny buffers */
    for (int it = 0; it < 300; it++) {
        u32 m = rnd() % 700; u8 *c = malloc(m + 1); for (u32 i = 0; i < m; i++) c[i] = (u8)rnd();
        if (m >= 12 && (it & 1)) { c[0] = 0; c[1] = 1; c[2] = 0; c[3] = 0; c[4] = 0; c[5] = (u8)(1 + rnd() % 8); }
        ttf_t t; ttf_open(&t, c, m);
        free(c);
    }
    CHECK(1, "noise survived");
}

int main(int argc, char **argv) {
    if (argc < 3) { printf("usage: host_ttf_test galmuri-dir ref-dir [fonts...]\n"); return 2; }
    W = malloc(sizeof *W);
    printf("work area: %u bytes\n", (unsigned)sizeof *W);
    static const char *gf[] = { "Galmuri11.ttf", "Galmuri7.ttf", "Galmuri9.ttf", "Galmuri14.ttf", "Galmuri11-Bold.ttf" };
    static const char *gr[] = { "GALMURI11", "GALMURI7", "GALMURI9", "GALMURI14", "GALMURI11B" };
    for (int i = 0; i < 5; i++) galmuri(argv[1], argv[2], gf[i], gr[i]);
    for (int i = 3; i < argc; i++) { other(argv[i]); fuzz(argv[i]); }
    { ttf_t t; u8 junk[16] = {0}; CHECK(ttf_open(&t, junk, 16) == TTF_ERR_FORMAT, "junk is refused");
      CHECK(ttf_open(&t, junk, 3) == TTF_ERR_FORMAT, "3 bytes are refused"); }
    printf("ttf: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
