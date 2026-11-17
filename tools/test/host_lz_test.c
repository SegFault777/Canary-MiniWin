/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for kernel/lz.h (the LZ4 block decoder) and the compressed Korean font table in the font pack (tools/gen_font_pack.py).
 *   - the decoder reproduces the generator's raw dump byte for byte (argv[1] = file written by
 *     tools/gen_hangul_font.py --dump-raw); the compressed stream under test is face 0 (Galmuri11) of the checked-in font pack
 *     (argv[2] = third_party/galmuri-font/fontpack-min.bin) -- since 1.1-alpha-14 the kernel image no longer carries it;
 *   - hostile inputs (truncation, bad offsets, output overflow, runaway lengths) return 0 and never write past dstcap
 *     (run under ASan/UBSan, with the destination placed in a guarded heap block). */
#include "rd_host.h"
#include "../../kernel/font_ko_data.h"

static int fails = 0, checks = 0;
static u32 packed_n, raw_n;                       /* face 0 of the pack: its packed and inflated sizes */
static u8 *font_ko_packed;                        /* ...and its packed bytes */
#define KO_FONT_PACKED_BYTES packed_n
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char **argv) {
    if (argc < 3) { printf("usage: host_lz_test raw.bin fontpack-min.bin\n"); return 2; }
    FILE *pf = fopen(argv[2], "rb"); if (!pf) { perror("pack"); return 2; }
    static u8 pack[1 << 20];
    size_t pn = fread(pack, 1, sizeof pack, pf); fclose(pf);
    if (pn < 1024 || memcmp(pack, "MWFP", 4)) { printf("not a font pack\n"); return 2; }
    font_ko_packed = pack + *(u32 *)(pack + 32) * 512; raw_n = *(u32 *)(pack + 40); packed_n = *(u32 *)(pack + 44);
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror("raw"); return 2; }
    static u8 raw[KO_FONT_RAW_BYTES + 16];
    size_t n = fread(raw, 1, sizeof raw, f); fclose(f);
    CHECK(n == KO_FONT_RAW_BYTES, "raw dump is %zu bytes, expected %u", n, (unsigned)KO_FONT_RAW_BYTES);

    { u32 got = lz4_block_decode(font_ko_packed, packed_n, (u8 *)g_ko_font, raw_n);
      u32 h = 0x811C9DC5u; for (u32 k = 0; k < KO_FONT_RAW_BYTES; k++) h = (h ^ ((u8 *)g_ko_font)[k]) * 0x01000193u;
      CHECK(got == raw_n && h == KO_FONT_CHECKSUM, "the pack's Galmuri11 face inflates to %u bytes whose syllable table hashes to KO_FONT_CHECKSUM", (unsigned)raw_n); }
    CHECK(memcmp(g_ko_font, raw, KO_FONT_RAW_BYTES) == 0, "inflated font == generator raw dump");
    CHECK(font8x8_ko[0][0] == 0xFC40, "first syllable row 0 is 0xFC40 (got 0x%04X)", font8x8_ko[0][0]);

    /* tiny hand-made streams */
    u8 *out = malloc(64);
    { u8 s[] = { 0x30, 'a', 'b', 'c' };                                                          /* 3 literals, nothing after */
      CHECK(lz4_block_decode(s, sizeof s, out, 64) == 3 && !memcmp(out, "abc", 3), "literals only"); }
    { u8 t[] = { 0x10, 'x', 0x01, 0x00, 0x10, 'y' };                                             /* 'x', match(len 4, off 1) = xxxx, then 1 literal 'y' */
      CHECK(lz4_block_decode(t, sizeof t, out, 64) == 6 && !memcmp(out, "xxxxxy", 6), "overlapping match expands a run"); }
    { u8 s[] = { 0x10, 'x', 0x02, 0x00, 0x00 };  CHECK(lz4_block_decode(s, sizeof s, out, 64) == 0, "offset before start of output rejected"); }
    { u8 s[] = { 0x10, 'x', 0x00, 0x00, 0x00 };  CHECK(lz4_block_decode(s, sizeof s, out, 64) == 0, "offset 0 rejected"); }
    { u8 s[] = { 0x10, 'x', 0x01 };              CHECK(lz4_block_decode(s, sizeof s, out, 64) == 0, "truncated offset rejected"); }
    { u8 s[] = { 0xF0, 255, 255 };               CHECK(lz4_block_decode(s, sizeof s, out, 64) == 0, "literal length running off the input rejected"); }
    { u8 s[] = { 0x50, 'a', 'b', 'c' };          CHECK(lz4_block_decode(s, sizeof s, out, 64) == 0, "literals longer than the input rejected"); }
    { u8 s[] = { 0x30, 'a', 'b', 'c' };          CHECK(lz4_block_decode(s, sizeof s, out, 2) == 0, "output overflow rejected (literals)"); }
    { u8 t[] = { 0x1F, 'x', 0x01, 0x00, 255, 255, 0 };                                           /* a huge match length against a 64-byte output */
                                                  CHECK(lz4_block_decode(t, sizeof t, out, 64) == 0, "output overflow rejected (match)"); }

    /* the real stream with the output cap one byte short, truncated at every 997th length, and with single-byte damage */
    u8 *big = malloc(raw_n);
    CHECK(lz4_block_decode(font_ko_packed, KO_FONT_PACKED_BYTES, big, raw_n - 1) == 0, "real stream, cap one byte short -> 0");
    int bad_accepts = 0;
    for (u32 len = 1; len < KO_FONT_PACKED_BYTES; len += 997) {
        u32 r = lz4_block_decode(font_ko_packed, len, big, raw_n);               /* must not crash; may legitimately succeed on a clean boundary */
        (void)r;
    }
    u8 *copy = malloc(KO_FONT_PACKED_BYTES);
    for (u32 i = 0; i < KO_FONT_PACKED_BYTES; i += 211) {
        memcpy(copy, font_ko_packed, KO_FONT_PACKED_BYTES);
        copy[i] ^= 0xFF;
        u32 r = lz4_block_decode(copy, KO_FONT_PACKED_BYTES, big, raw_n);
        if (r == raw_n) {                                                          /* decoded "fine": then the checksum must be what catches it */
            u32 h = 0x811C9DC5u; for (u32 k = 0; k < KO_FONT_RAW_BYTES; k++) h = (h ^ big[k]) * 0x01000193u;
            if (h == KO_FONT_CHECKSUM && memcmp(big, raw, KO_FONT_RAW_BYTES) != 0) bad_accepts++;   /* (a flipped offset that points at identical bytes legitimately decodes to the same table) */
        }
    }
    CHECK(bad_accepts == 0, "no single-byte corruption decodes to a DIFFERENT table with the right checksum (%d did)", bad_accepts);
    free(out); free(big); free(copy);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
