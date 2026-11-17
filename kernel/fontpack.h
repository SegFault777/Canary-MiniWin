/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_FONTPACK_H
#define MW_FONTPACK_H

/* fontpack.h -- the FONT PACK reader (1.1-alpha-14).
 *
 * The Hangul table of the built-in face (Galmuri11) used to ride in the kernel image: 62,755 bytes of a kernel that has no room left
 * (it must fit the 512 KB the boot loader reads, below the real-mode ceiling). Now every face -- Galmuri11, the default, and on the full
 * image Galmuri7 / 9 / 14 and Galmuri11 Bold -- lives in the FONT PACK, a region of the system area of the disk image
 * (kernel/fs.h MW_FONT_LBA .. +MW_FONT_SECTORS; tools/gen_font_pack.py writes it, tools/install_fontpack.py puts it in an image).
 *
 * Pack: sector 0 = "MWFP", version, count, then 40-byte entries (name[16], cell, bold, desc, start sector, sectors, raw bytes, packed
 * bytes, crc32 of the raw bytes); each face is ONE LZ4 block (kernel/lz.h) of u16 rows, `cell` rows per Hangul glyph and `cell + desc` per Latin glyph (the tails of g j p q y , under the cell), in this order: the 11172
 * Hangul syllables, the 51 jamo, the 95 Latin glyphs U+0020..U+007E. (Syllables first so face 0 lands exactly where font8x8_ko has
 * always pointed: MW_KFONT_ADDR.)
 *
 * fontpack_boot() runs once, in kernel_boot(), after the heap exists and before the first frame: it loads face 0 into MW_KFONT_ADDR and
 * checks it against KO_FONT_CHECKSUM. The Latin and jamo tables of face 0 stay the built-in ones (font_latin_data.h, font_ko_data.h);
 * the other faces bring their own (font_cur_latin / font_cur_ko / font_cur_ko_jamo, see font.h). A pack that is missing or damaged
 * costs the Hangul glyphs (blank), nothing else -- the boot reports it on the serial port and goes on.
 * Needs: blk.h (ata_read_sector), lz.h, mm.h (kmalloc), mwfs.h (mwfs_crc32_update), font_ko_data.h, fs.h. */

#define FP_MAX      12
#define FP_ENT_OFF  12
#define FP_ENT_SZ   40

typedef struct {
    char name[16];
    u8   cell, bold;
    u16  desc;                    /* rows of descender under each Latin glyph (0 in a pack written before alpha-14's fix) */
    u32  start, sectors, raw, packed, crc;
} fp_entry_t;

static fp_entry_t fp_ent[FP_MAX];
static int        fp_n = 0;

/* Reads the pack's header sector; 0 if there is no valid pack. Fills fp_ent[0..fp_n). */
static int fp_scan(void) {
    u8 hdr[512];
    fp_n = 0;
    if (!ata_read_sector(MW_FONT_LBA, hdr)) return 0;
    if (hdr[0] != 'M' || hdr[1] != 'W' || hdr[2] != 'F' || hdr[3] != 'P') return 0;
    u32 ver = *(u32 *)(hdr + 4), n = *(u32 *)(hdr + 8);
    if (ver != 1 || n == 0 || n > FP_MAX) return 0;
    for (u32 i = 0; i < n; i++) {
        const u8 *src = hdr + FP_ENT_OFF + i * FP_ENT_SZ;
        u8 *dst = (u8 *)&fp_ent[i];
        for (u32 k = 0; k < FP_ENT_SZ; k++) dst[k] = src[k];
        fp_entry_t *e = &fp_ent[i];
        /* every number is checked before it is used: a damaged header must not make us read or write somewhere else */
        if (e->cell < 6 || e->cell > 16 || e->start < 1 || e->sectors == 0 || e->start + e->sectors > MW_FONT_SECTORS) return 0;
        if (e->desc > 4 || e->raw != ((u32)(KO_SYLLABLE_COUNT + KO_JAMO_COUNT) * e->cell + 95u * (e->cell + e->desc)) * 2u || e->packed > e->sectors * 512u) return 0;
    }
    fp_n = (int)n;
    return 1;
}

/* Reads face `i` and inflates it into dst (fp_ent[i].raw bytes); 1 only if the CRC of the result matches. */
static int fp_load(int i, u8 *dst) {
    fp_entry_t *e = &fp_ent[i];
    u8 *packed = (u8 *)kmalloc(e->sectors * 512u);
    if (!packed) return 0;
    int ok = 1;
    for (u32 s = 0; s < e->sectors && ok; s++) ok = ata_read_sector(MW_FONT_LBA + e->start + s, packed + s * 512u);
    if (ok) ok = lz4_block_decode(packed, e->packed, dst, e->raw) == e->raw;
    kfree(packed);
    if (!ok) return 0;
    return ~mwfs_crc32_update(0xFFFFFFFFu, dst, e->raw) == e->crc;
}

/* Boot: face 0 (Galmuri11, 11 rows) into MW_KFONT_ADDR. 1 = loaded and verified. */
static int fontpack_boot(void) {
    if (!fp_scan()) return 0;
    if (fp_ent[0].cell != 11 || fp_ent[0].bold) return 0;
    u8 *dst = (u8 *)MW_KFONT_ADDR;
    if (!fp_load(0, dst)) return 0;
    u32 h = 0x811C9DC5u;                                             /* the syllable table must be exactly the one the build was made with */
    for (u32 i = 0; i < KO_FONT_RAW_BYTES; i++) h = (h ^ dst[i]) * 0x01000193u;
    return h == KO_FONT_CHECKSUM;
}

#endif
