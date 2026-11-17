/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_IMG_H
#define MW_IMG_H

/* img.h -- picture decoders for MiniWeb: PNG (all colour types and depths, Adam7 included), JPEG (baseline / extended sequential
 * Huffman; progressive is not decoded) and GIF (first frame), plus the base64 + data: URI plumbing that feeds <img src="data:...">.
 *
 * Every decoder turns its input into one W*H array of 0x00RRGGBB pixels. Transparency is flattened onto white right here,
 * because the page canvas is white nine times out of ten and a painter that blends per pixel would cost more than it is worth.
 * No libc, no floating point (the kernel does not touch the FPU): inflate is the puff.c algorithm, the JPEG IDCT is
 * fixed-point. Memory comes from IMG_ALLOC / IMG_FREE (kmalloc / kfree in the kernel, malloc / free in the host tests).
 *
 * Nothing in here knows about the DOM; render.h / layout.h call img_for_src() and read the finished picture. */

#ifndef IMG_ALLOC
static void *kmalloc(u32 n);                 /* defined later, in mm.h */
static int kfree(void *p);
#define IMG_ALLOC(n) kmalloc(n)
#define IMG_FREE(p)  kfree(p)
#endif


#define IMG_MAX_PIXELS (1600u * 1200u)       /* ~7.5MB of pixels: bigger pictures are not decoded (alt text is shown instead) */
#define IMG_MAX_RAW    (12u * 1024u * 1024u)

typedef struct { u32 w, h; u32 *px; } img_t;

#ifdef MW_NO_WEB_IMAGES      /* -DMW_NO_WEB_IMAGES: leave the decoders out (about 12 KB of kernel); <img> then always shows its alt-text box */
static struct { u32 key; u8 state; img_t im; } img_cache[1];
static inline void img_cache_flush(void) {}
static inline int img_for_src(u32 key, const u8 *s, u32 n) { (void)key; (void)s; (void)n; return 0; }
#else
/* ---------------------------------------------------------------- inflate (RFC 1951) */
typedef struct { const u8 *in; u32 inlen, inpos; u32 bitbuf; int bitcnt; u8 *out; u32 outcap, outlen; int err; } inf_t;
typedef struct { u16 count[16]; u16 symbol[288]; } inf_huff_t;

static u32 inf_bits(inf_t *s, int need) {
    u32 v = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos >= s->inlen) { s->err = 1; return 0; }
        v |= (u32)s->in[s->inpos++] << s->bitcnt; s->bitcnt += 8;
    }
    s->bitbuf = need >= 32 ? 0 : v >> need; s->bitcnt -= need;
    return v & ((1u << need) - 1u);
}
static int inf_decode(inf_t *s, const inf_huff_t *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= (int)inf_bits(s, 1);
        if (s->err) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -1;
}
static int inf_construct(inf_huff_t *h, const u8 *length, int n) {
    int offs[16];
    for (int i = 0; i < 16; i++) h->count[i] = 0;
    for (int i = 0; i < n; i++) h->count[length[i]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= 15; len++) { left <<= 1; left -= h->count[len]; if (left < 0) return left; }
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h->count[len];
    for (int i = 0; i < n; i++) if (length[i]) h->symbol[offs[length[i]]++] = (u16)i;
    return left;
}
static int inf_codes(inf_t *s, const inf_huff_t *lc, const inf_huff_t *dc) {
    static const u16 lens[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const u16 lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const u16 dists[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const u16 dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    int sym;
    do {
        sym = inf_decode(s, lc);
        if (sym < 0 || s->err) return -1;
        if (sym < 256) {
            if (s->outlen >= s->outcap) return -1;
            s->out[s->outlen++] = (u8)sym;
        } else if (sym > 256) {
            sym -= 257;
            if (sym >= 29) return -1;
            u32 len = lens[sym] + inf_bits(s, lext[sym]);
            int ds = inf_decode(s, dc);
            if (ds < 0 || ds >= 30) return -1;
            u32 dist = dists[ds] + inf_bits(s, dext[ds]);
            if (s->err || dist > s->outlen || s->outlen + len > s->outcap) return -1;
            while (len--) { s->out[s->outlen] = s->out[s->outlen - dist]; s->outlen++; }
        }
    } while (sym != 256);
    return 0;
}
static int inf_dynamic(inf_t *s) {
    static const u8 order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    u8 lengths[320]; inf_huff_t lc, dc;
    int nlen = (int)inf_bits(s, 5) + 257, ndist = (int)inf_bits(s, 5) + 1, ncode = (int)inf_bits(s, 4) + 4;
    if (s->err || nlen > 286 || ndist > 30) return -1;
    int idx;
    for (idx = 0; idx < ncode; idx++) lengths[order[idx]] = (u8)inf_bits(s, 3);
    for (; idx < 19; idx++) lengths[order[idx]] = 0;
    if (inf_construct(&lc, lengths, 19) != 0) return -1;
    idx = 0;
    while (idx < nlen + ndist) {
        int sym = inf_decode(s, &lc);
        if (sym < 0 || s->err) return -1;
        if (sym < 16) lengths[idx++] = (u8)sym;
        else {
            int len = 0, rep;
            if (sym == 16) { if (idx == 0) return -1; len = lengths[idx - 1]; rep = 3 + (int)inf_bits(s, 2); }
            else if (sym == 17) rep = 3 + (int)inf_bits(s, 3);
            else rep = 11 + (int)inf_bits(s, 7);
            if (idx + rep > nlen + ndist) return -1;
            while (rep--) lengths[idx++] = (u8)len;
        }
    }
    if (lengths[256] == 0) return -1;
    int e = inf_construct(&lc, lengths, nlen);
    if (e < 0 || (e > 0 && nlen - lc.count[0] != 1)) return -1;
    e = inf_construct(&dc, lengths + nlen, ndist);
    if (e < 0 || (e > 0 && ndist - dc.count[0] != 1)) return -1;
    return inf_codes(s, &lc, &dc);
}
/* zlib stream -> raw bytes. Returns the output length, or -1. */
static int inf_zlib(const u8 *in, u32 inlen, u8 *out, u32 outcap) {
    inf_t s; s.in = in; s.inlen = inlen; s.inpos = 0; s.bitbuf = 0; s.bitcnt = 0; s.out = out; s.outcap = outcap; s.outlen = 0; s.err = 0;
    if (inlen < 2 || (in[0] & 15) != 8 || ((in[0] << 8) | in[1]) % 31) return -1;
    s.inpos = 2;
    int last;
    do {
        last = (int)inf_bits(&s, 1);
        int type = (int)inf_bits(&s, 2);
        if (s.err) return -1;
        if (type == 0) {
            s.bitbuf = 0; s.bitcnt = 0;
            if (s.inpos + 4 > s.inlen) return -1;
            u32 len = s.in[s.inpos] | (u32)s.in[s.inpos + 1] << 8;
            u32 nlen = s.in[s.inpos + 2] | (u32)s.in[s.inpos + 3] << 8;
            s.inpos += 4;
            if (len != (~nlen & 0xFFFF) || s.inpos + len > s.inlen || s.outlen + len > s.outcap) return -1;
            for (u32 i = 0; i < len; i++) s.out[s.outlen++] = s.in[s.inpos++];
        } else if (type == 1) {
            u8 l[288]; inf_huff_t lc, dc;
            int i = 0;
            for (; i < 144; i++) l[i] = 8;
            for (; i < 256; i++) l[i] = 9;
            for (; i < 280; i++) l[i] = 7;
            for (; i < 288; i++) l[i] = 8;
            inf_construct(&lc, l, 288);
            for (i = 0; i < 30; i++) l[i] = 5;
            inf_construct(&dc, l, 30);
            if (inf_codes(&s, &lc, &dc) < 0) return -1;
        } else if (type == 2) { if (inf_dynamic(&s) < 0) return -1; }
        else return -1;
    } while (!last);
    return (int)s.outlen;
}

/* ---------------------------------------------------------------- helpers */
static inline u32 img_flat(u32 r, u32 g, u32 b, u32 a) {           /* blend onto white */
    if (a >= 255) return r << 16 | g << 8 | b;
    r = (r * a + 255u * (255u - a) + 127u) / 255u; g = (g * a + 255u * (255u - a) + 127u) / 255u; b = (b * a + 255u * (255u - a) + 127u) / 255u;   /* rounded, like every other blender */
    return r << 16 | g << 8 | b;
}
static inline u32 img_be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static inline u32 img_le16(const u8 *p) { return p[0] | (u32)p[1] << 8; }
static inline u32 img_le32(const u8 *p) { return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static int img_alloc_px(img_t *im, u32 w, u32 h) {
    if (w == 0 || h == 0 || w > 8192 || h > 8192 || w * h > IMG_MAX_PIXELS) return 0;
    im->w = w; im->h = h; im->px = (u32 *)IMG_ALLOC(w * h * 4u);
    return im->px != 0;
}
static void img_free(img_t *im) { if (im->px) IMG_FREE(im->px); im->px = 0; im->w = im->h = 0; }

/* ---------------------------------------------------------------- PNG */
static inline int img_paeth(int a, int b, int c) {
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}
static int img_png(const u8 *d, u32 n, img_t *im) {
    if (n < 33 || img_be32(d) != 0x89504E47u) return 0;
    u32 w = 0, h = 0, depth = 0, ctype = 0, interlace = 0, pos = 8, idat_total = 0;
    u8 pal[256][3]; u8 trans[256]; u32 npal = 0, ntrans = 0; int have_key = 0; u32 key[3] = {0, 0, 0};
    for (int i = 0; i < 256; i++) trans[i] = 255;
    for (int pass = 0; pass < 2; pass++) {                       /* pass 0: headers and total IDAT size; pass 1 (below): gather */
        pos = 8;
        while (pos + 12 <= n) {
            u32 len = img_be32(d + pos), type = img_be32(d + pos + 4);
            if (len > n || pos + 12 + len > n) break;
            const u8 *c = d + pos + 8;
            if (type == 0x49484452u && len >= 13) { w = img_be32(c); h = img_be32(c + 4); depth = c[8]; ctype = c[9]; interlace = c[12]; }
            else if (type == 0x504C5445u) { npal = len / 3 > 256 ? 256 : len / 3; for (u32 i = 0; i < npal; i++) { pal[i][0] = c[i * 3]; pal[i][1] = c[i * 3 + 1]; pal[i][2] = c[i * 3 + 2]; } }
            else if (type == 0x74524E53u) {
                if (ctype == 3) { ntrans = len > 256 ? 256 : len; for (u32 i = 0; i < ntrans; i++) trans[i] = c[i]; }
                else if (ctype == 0 && len >= 2) { have_key = 1; key[0] = (u32)c[0] << 8 | c[1]; }
                else if (ctype == 2 && len >= 6) { have_key = 1; key[0] = (u32)c[0] << 8 | c[1]; key[1] = (u32)c[2] << 8 | c[3]; key[2] = (u32)c[4] << 8 | c[5]; }
            }
            else if (type == 0x49444154u) { if (pass == 0) idat_total += len; }
            else if (type == 0x49454E44u) break;
            pos += 12 + len;
        }
        break;
    }
    u32 channels = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!channels || w == 0 || h == 0 || w > 8192 || h > 8192 || w * h > IMG_MAX_PIXELS || idat_total == 0) return 0;
    if (!(depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)) return 0;
    if (ctype == 3 && npal == 0) return 0;
    u32 bpp_bits = depth * channels, bpp = (bpp_bits + 7) / 8;
    u8 *z = (u8 *)IMG_ALLOC(idat_total);
    if (!z) return 0;
    u32 zl = 0; pos = 8;
    while (pos + 12 <= n) {
        u32 len = img_be32(d + pos), type = img_be32(d + pos + 4);
        if (len > n || pos + 12 + len > n) break;
        if (type == 0x49444154u) { for (u32 i = 0; i < len; i++) z[zl++] = d[pos + 8 + i]; }
        if (type == 0x49454E44u) break;
        pos += 12 + len;
    }
    /* total filtered size over all passes */
    static const u8 ax[7] = {0,4,0,2,0,1,0}, ay[7] = {0,0,4,0,2,0,1}, dx[7] = {8,8,4,4,2,2,1}, dy[7] = {8,8,8,4,4,2,2};
    u32 raw = 0, np = interlace ? 7 : 1;
    for (u32 p = 0; p < np; p++) {
        u32 pw = interlace ? (w - ax[p] + dx[p] - 1) / dx[p] : w, ph = interlace ? (h - ay[p] + dy[p] - 1) / dy[p] : h;
        if (interlace && (w <= ax[p] || h <= ay[p])) continue;
        raw += ph * (1 + (pw * bpp_bits + 7) / 8);
    }
    if (raw == 0 || raw > IMG_MAX_RAW) { IMG_FREE(z); return 0; }
    u8 *buf = (u8 *)IMG_ALLOC(raw);
    if (!buf) { IMG_FREE(z); return 0; }
    int got = inf_zlib(z, zl, buf, raw);
    IMG_FREE(z);
    if (got < 0 || (u32)got < raw) { IMG_FREE(buf); return 0; }
    if (!img_alloc_px(im, w, h)) { IMG_FREE(buf); return 0; }
    for (u32 i = 0; i < w * h; i++) im->px[i] = 0xFFFFFF;
    u32 off = 0;
    for (u32 p = 0; p < np; p++) {
        u32 x0 = interlace ? ax[p] : 0, y0 = interlace ? ay[p] : 0, sx = interlace ? dx[p] : 1, sy = interlace ? dy[p] : 1;
        if (interlace && (w <= x0 || h <= y0)) continue;
        u32 pw = (w - x0 + sx - 1) / sx, ph = (h - y0 + sy - 1) / sy, stride = (pw * bpp_bits + 7) / 8;
        u8 *prev = 0;
        for (u32 y = 0; y < ph; y++) {
            u8 *row = buf + off + y * (1 + stride) + 1; u8 ft = row[-1];
            for (u32 i = 0; i < stride; i++) {
                int a = i >= bpp ? row[i - bpp] : 0, b = prev ? prev[i] : 0, c = (prev && i >= bpp) ? prev[i - bpp] : 0, x = row[i];
                switch (ft) { case 1: x += a; break; case 2: x += b; break; case 3: x += (a + b) / 2; break; case 4: x += img_paeth(a, b, c); break; default: break; }
                row[i] = (u8)x;
            }
            for (u32 x = 0; x < pw; x++) {
                u32 r, g, b, a = 255, v[4];
                for (u32 ch = 0; ch < channels; ch++) {
                    u32 idx = x * channels + ch;
                    if (depth == 8) v[ch] = row[idx];
                    else if (depth == 16) v[ch] = (u32)row[idx * 2] << 8 | row[idx * 2 + 1];
                    else { u32 bit = idx * depth; v[ch] = (row[bit >> 3] >> (8 - depth - (bit & 7))) & ((1u << depth) - 1u); }
                }
                u32 maxv = depth == 16 ? 65535u : (1u << depth) - 1u;
                #define SC(q) (depth == 16 ? (q) >> 8 : (q) * 255u / maxv)
                if (ctype == 3) { u32 pi = v[0] < npal ? v[0] : 0; r = pal[pi][0]; g = pal[pi][1]; b = pal[pi][2]; a = trans[pi]; }
                else if (ctype == 0) { r = g = b = SC(v[0]); if (have_key && v[0] == key[0]) a = 0; }
                else if (ctype == 4) { r = g = b = SC(v[0]); a = SC(v[1]); }
                else if (ctype == 2) { r = SC(v[0]); g = SC(v[1]); b = SC(v[2]); if (have_key && v[0] == key[0] && v[1] == key[1] && v[2] == key[2]) a = 0; }
                else { r = SC(v[0]); g = SC(v[1]); b = SC(v[2]); a = SC(v[3]); }
                #undef SC
                im->px[(y0 + y * sy) * w + x0 + x * sx] = img_flat(r, g, b, a);
            }
            prev = row;
        }
        off += ph * (1 + stride);
    }
    IMG_FREE(buf);
    return 1;
}

/* ---------------------------------------------------------------- GIF (first frame) */
static int img_gif(const u8 *d, u32 n, img_t *im) {
    if (n < 13 || d[0] != 'G' || d[1] != 'I' || d[2] != 'F') return 0;
    u32 w = img_le16(d + 6), h = img_le16(d + 8), fl = d[10], pos = 13;
    u8 gpal[256][3]; u32 gn = 0, trans = 0x100;
    if (fl & 0x80) { gn = 2u << (fl & 7); if (pos + gn * 3 > n) return 0; for (u32 i = 0; i < gn; i++) { gpal[i][0] = d[pos + i * 3]; gpal[i][1] = d[pos + i * 3 + 1]; gpal[i][2] = d[pos + i * 3 + 2]; } pos += gn * 3; }
    if (!img_alloc_px(im, w, h)) return 0;
    for (u32 i = 0; i < w * h; i++) im->px[i] = 0xFFFFFF;
    while (pos < n) {
        u8 b = d[pos++];
        if (b == 0x21) {
            if (pos >= n) break;
            u8 lab = d[pos++];
            if (lab == 0xF9 && pos + 5 < n && d[pos] >= 4) { if (d[pos + 1] & 1) trans = d[pos + 4]; }
            while (pos < n && d[pos]) pos += 1 + d[pos];
            pos++;
        } else if (b == 0x2C) {
            if (pos + 9 > n) break;
            u32 ix = img_le16(d + pos), iy = img_le16(d + pos + 2), iw = img_le16(d + pos + 4), ih = img_le16(d + pos + 6), f = d[pos + 8];
            pos += 9;
            u8 lpal[256][3]; u8 (*pal)[3] = gpal; u32 pn = gn;
            if (f & 0x80) { pn = 2u << (f & 7); if (pos + pn * 3 > n) break; for (u32 i = 0; i < pn; i++) { lpal[i][0] = d[pos + i * 3]; lpal[i][1] = d[pos + i * 3 + 1]; lpal[i][2] = d[pos + i * 3 + 2]; } pos += pn * 3; pal = lpal; }
            if (pos >= n) break;
            u32 mcs = d[pos++];
            if (mcs < 2 || mcs > 8 || iw == 0 || ih == 0 || iw > 8192 || ih > 8192) break;
            u32 ds = pos, dl = 0;                               /* sub-blocks: count, then decode in place */
            u8 *cd = (u8 *)IMG_ALLOC(n - pos + 1);
            u8 *ix_buf = (u8 *)IMG_ALLOC(iw * ih);
            u16 *prefix = (u16 *)IMG_ALLOC(4096 * 2); u8 *suffix = (u8 *)IMG_ALLOC(4096), *stack = (u8 *)IMG_ALLOC(4097);
            if (!cd || !ix_buf || !prefix || !suffix || !stack) { if (cd) IMG_FREE(cd); if (ix_buf) IMG_FREE(ix_buf); if (prefix) IMG_FREE(prefix); if (suffix) IMG_FREE(suffix); if (stack) IMG_FREE(stack); break; }
            (void)ds;
            while (pos < n && d[pos]) { u32 bl = d[pos]; if (pos + 1 + bl > n) break; for (u32 i = 0; i < bl; i++) cd[dl++] = d[pos + 1 + i]; pos += 1 + bl; }
            u32 clear = 1u << mcs, eoi = clear + 1, next = clear + 2, size = mcs + 1, bits = 0, nb = 0, cp = 0, old = 0xFFFFFFFFu, o = 0;
            u8 first = 0;
            for (u32 i = 0; i < clear; i++) { prefix[i] = 0; suffix[i] = (u8)i; }
            while (o < iw * ih) {
                while (nb < size && cp < dl) { bits |= (u32)cd[cp++] << nb; nb += 8; }
                if (nb < size) break;
                u32 code = bits & ((1u << size) - 1u); bits >>= size; nb -= size;
                if (code == clear) { size = mcs + 1; next = clear + 2; old = 0xFFFFFFFFu; continue; }
                if (code == eoi) break;
                u32 cur = code, sp = 0;
                if (old == 0xFFFFFFFFu) { if (code >= clear) break; ix_buf[o++] = (u8)code; old = code; first = (u8)code; continue; }
                if (code >= next) { if (code > next) break; stack[sp++] = first; cur = old; }
                while (cur >= clear && sp < 4096) { stack[sp++] = suffix[cur]; cur = prefix[cur]; }
                first = suffix[cur]; stack[sp++] = first;
                while (sp && o < iw * ih) ix_buf[o++] = stack[--sp];
                if (next < 4096) { prefix[next] = (u16)old; suffix[next] = first; next++; if (next == (1u << size) && size < 12) size++; }
                old = code;
            }
            u32 row = 0, passn = 0;
            for (u32 y = 0; y < ih; y++) {
                u32 dy2;
                if (f & 0x40) { static const u8 st[4] = {8,8,4,2}, s0[4] = {0,4,2,1}; while (row >= ih && passn < 3) { passn++; row = s0[passn]; } dy2 = row; row += st[passn]; }   /* a pass with no rows (a short picture) is skipped */
                else dy2 = y;
                if (iy + dy2 >= h) continue;
                for (u32 x = 0; x < iw; x++) {
                    if (y * iw + x >= o) continue;                  /* the LZW data ended early: what was never decoded stays background, not memory garbage */
                    u32 c = ix_buf[y * iw + x];
                    if (c == trans || c >= pn || ix + x >= w) continue;
                    im->px[(iy + dy2) * w + ix + x] = (u32)pal[c][0] << 16 | (u32)pal[c][1] << 8 | pal[c][2];
                }
            }
            IMG_FREE(cd); IMG_FREE(ix_buf); IMG_FREE(prefix); IMG_FREE(suffix); IMG_FREE(stack);
            return 1;
        } else if (b == 0x3B) break;
        else break;
    }
    img_free(im);
    return 0;
}

/* ---------------------------------------------------------------- JPEG (baseline / extended sequential Huffman) */
typedef struct { u8 bits[17]; u8 val[256]; u16 mincode[17]; int maxcode[18]; u16 valptr[17]; int ok; } jpg_huff_t;
typedef struct { const u8 *d; u32 n, pos; u32 buf; int cnt; int hit_marker; } jpg_bits_t;

static const u8 jpg_zz[64] = {0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
static const short jpg_cos[8][8] = {{1448,1448,1448,1448,1448,1448,1448,1448},{2009,1703,1138,400,-400,-1138,-1703,-2009},{1892,784,-784,-1892,-1892,-784,784,1892},{1703,-400,-2009,-1138,1138,2009,400,-1703},{1448,-1448,-1448,1448,1448,-1448,-1448,1448},{1138,-2009,400,1703,-1703,-400,2009,-1138},{784,-1892,1892,-784,-784,1892,-1892,784},{400,-1138,1703,-2009,2009,-1703,1138,-400}};

static void jpg_build(jpg_huff_t *h) {
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = (u16)k; h->mincode[l] = (u16)code;
        code += h->bits[l]; k += h->bits[l];
        h->maxcode[l] = h->bits[l] ? code - 1 : -1;
        code <<= 1;
    }
    h->maxcode[17] = 0x7FFFFFFF; h->ok = 1;
}
static inline int jpg_bit(jpg_bits_t *b) {
    if (b->cnt == 0) {
        u32 v = 0;
        if (!b->hit_marker && b->pos < b->n) {
            v = b->d[b->pos++];
            if (v == 0xFF) {
                u32 m = b->pos < b->n ? b->d[b->pos] : 0xD9;
                if (m == 0) b->pos++;
                else { b->hit_marker = 1; b->pos--; v = 0; }
            }
        }
        b->buf = v; b->cnt = 8;
    }
    b->cnt--;
    return (int)((b->buf >> b->cnt) & 1);
}
static inline int jpg_recv(jpg_bits_t *b, int n) { int v = 0; while (n--) v = v << 1 | jpg_bit(b); return v; }
static inline int jpg_extend(int v, int t) { return t && v < (1 << (t - 1)) ? v - (1 << t) + 1 : v; }
static int jpg_decode_sym(jpg_bits_t *b, const jpg_huff_t *h) {
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = code << 1 | jpg_bit(b);
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->mincode[l]) return h->val[h->valptr[l] + code - h->mincode[l]];
    }
    return -1;
}
static inline u8 jpg_clamp(int v) { return (u8)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void jpg_idct(const int *in, u8 *out, u32 stride) {
    int tmp[64];
    for (int y = 0; y < 8; y++)                                   /* rows */
        for (int x = 0; x < 8; x++) {
            int s = 0;
            for (int u = 0; u < 8; u++) if (in[y * 8 + u]) s += jpg_cos[u][x] * in[y * 8 + u];
            tmp[y * 8 + x] = s >> 12;
        }
    for (int x = 0; x < 8; x++)                                   /* columns */
        for (int y = 0; y < 8; y++) {
            int s = 0;
            for (int v = 0; v < 8; v++) s += jpg_cos[v][y] * tmp[v * 8 + x];
            out[y * stride + x] = jpg_clamp((s >> 12) + 128);
        }
}

static int img_jpeg(const u8 *d, u32 n, img_t *im) {
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return 0;
    u16 qt[4][64]; jpg_huff_t *hdc = 0, *hac = 0; int ok = 0;
    u32 w = 0, h = 0, nc = 0, ri = 0, pos = 2;
    struct { u8 id, hs, vs, tq, td, ta; int pred; u8 *plane; u32 pw, ph; } comp[3];
    u32 hmax = 1, vmax = 1;
    for (int i = 0; i < 3; i++) { comp[i].plane = 0; comp[i].id = comp[i].hs = comp[i].vs = comp[i].tq = comp[i].td = comp[i].ta = 0; comp[i].pred = 0; comp[i].pw = comp[i].ph = 0; }   /* td/ta were never set when a scan named an unknown component: hdc[garbage] */
    hdc = (jpg_huff_t *)IMG_ALLOC(sizeof(jpg_huff_t) * 8);
    if (!hdc) return 0;
    hac = hdc + 4;
    for (int i = 0; i < 8; i++) hdc[i].ok = 0;
    for (int i = 0; i < 4; i++) for (int k = 0; k < 64; k++) qt[i][k] = 1;
    while (pos + 4 <= n) {
        if (d[pos] != 0xFF) { pos++; continue; }
        u8 m = d[pos + 1];
        if (m == 0xFF || m == 0 || (m >= 0xD0 && m <= 0xD8) || m == 0x01) { pos += (m == 0xFF) ? 1 : 2; continue; }
        if (m == 0xD9) break;
        u32 len = (u32)d[pos + 2] << 8 | d[pos + 3];
        if (len < 2 || pos + 2 + len > n) break;
        const u8 *p = d + pos + 4; u32 pl = len - 2;
        if (m == 0xDB) {
            while (pl >= 65) {
                u32 pq = p[0] >> 4, tq = p[0] & 15;
                if (tq > 3 || (pq && pl < 129)) break;
                for (int k = 0; k < 64; k++) qt[tq][jpg_zz[k]] = pq ? (u16)((p[1 + k * 2] << 8) | p[2 + k * 2]) : p[1 + k];
                u32 used = 1 + (pq ? 128 : 64); p += used; pl -= used;
            }
        } else if (m == 0xC4) {
            while (pl >= 17) {
                u32 tc = p[0] >> 4, th = p[0] & 3; jpg_huff_t *t = (tc ? hac : hdc) + th;
                u32 tot = 0; t->bits[0] = 0;
                for (int i = 1; i <= 16; i++) { t->bits[i] = p[i]; tot += p[i]; }
                if (tot > 256 || 17 + tot > pl) break;
                for (u32 i = 0; i < tot; i++) t->val[i] = p[17 + i];
                jpg_build(t); p += 17 + tot; pl -= 17 + tot;
            }
        } else if (m == 0xC0 || m == 0xC1) {
            if (pl < 6 || p[0] != 8) goto out;
            h = (u32)p[1] << 8 | p[2]; w = (u32)p[3] << 8 | p[4]; nc = p[5];
            if ((nc != 1 && nc != 3) || pl < 6 + nc * 3) goto out;
            for (u32 i = 0; i < nc; i++) {
                comp[i].id = p[6 + i * 3]; comp[i].hs = p[7 + i * 3] >> 4; comp[i].vs = p[7 + i * 3] & 15; comp[i].tq = p[8 + i * 3] & 3; comp[i].pred = 0;
                if (!comp[i].hs || !comp[i].vs || comp[i].hs > 4 || comp[i].vs > 4) goto out;
                if (comp[i].hs > hmax) hmax = comp[i].hs;
                if (comp[i].vs > vmax) vmax = comp[i].vs;
            }
            if (nc == 1) { comp[0].hs = comp[0].vs = 1; hmax = vmax = 1; }     /* one component: the sampling factors mean nothing, an MCU is one block */
        } else if (m == 0xC2 || (m >= 0xC3 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)) goto out;   /* progressive / lossless / arithmetic: not supported */
        else if (m == 0xDD) { if (pl >= 2) ri = (u32)p[0] << 8 | p[1]; }
        else if (m == 0xDA) {
            if (!w || !h || !nc || pl < 1 + 2 * nc || p[0] != nc) goto out;
            for (u32 i = 0; i < nc; i++) {
                int found = 0;
                for (u32 j = 0; j < nc; j++) if (comp[j].id == p[1 + i * 2]) { comp[j].td = p[2 + i * 2] >> 4 & 3; comp[j].ta = p[2 + i * 2] & 3; found = 1; }
                if (!found) goto out;                                          /* the scan names a component the frame does not have */
            }
            if (!img_alloc_px(im, w, h)) goto out;
            u32 mw = 8 * hmax, mh = 8 * vmax, mcx = (w + mw - 1) / mw, mcy = (h + mh - 1) / mh;
            for (u32 i = 0; i < nc; i++) {
                comp[i].pw = mcx * comp[i].hs * 8; comp[i].ph = mcy * comp[i].vs * 8;
                comp[i].plane = (u8 *)IMG_ALLOC(comp[i].pw * comp[i].ph);
                if (!comp[i].plane) { img_free(im); goto out; }
                for (u32 k = 0; k < comp[i].pw * comp[i].ph; k++) comp[i].plane[k] = 128;      /* what a damaged scan never reaches is mid-grey, not heap garbage */
            }
            jpg_bits_t b; b.d = d; b.n = n; b.pos = pos + 2 + len; b.buf = 0; b.cnt = 0; b.hit_marker = 0;
            u32 count = 0; int bad = 0;
            for (u32 my = 0; my < mcy && !bad; my++) for (u32 mx = 0; mx < mcx && !bad; mx++) {
                if (ri && count && count % ri == 0) {                 /* restart marker: byte-align, skip RSTn, reset predictors */
                    b.cnt = 0; b.hit_marker = 0;
                    while (b.pos + 1 < n && !(d[b.pos] == 0xFF && d[b.pos + 1] >= 0xD0 && d[b.pos + 1] <= 0xD7)) b.pos++;
                    b.pos += 2;
                    for (u32 i = 0; i < nc; i++) comp[i].pred = 0;
                }
                count++;
                for (u32 ci = 0; ci < nc; ci++) for (u32 by = 0; by < comp[ci].vs; by++) for (u32 bx = 0; bx < comp[ci].hs; bx++) {
                    int blk[64]; for (int k = 0; k < 64; k++) blk[k] = 0;
                    const jpg_huff_t *dt = &hdc[comp[ci].td], *at = &hac[comp[ci].ta];
                    if (!dt->ok || !at->ok) { bad = 1; break; }
                    int t = jpg_decode_sym(&b, dt);
                    if (t < 0 || t > 11) { bad = 1; break; }
                    comp[ci].pred += jpg_extend(jpg_recv(&b, t), t);
                    blk[0] = comp[ci].pred * qt[comp[ci].tq][0];
                    for (int k = 1; k < 64; ) {
                        int rs = jpg_decode_sym(&b, at);
                        if (rs < 0) { bad = 1; break; }
                        int r = rs >> 4, s = rs & 15;
                        if (s == 0) { if (r == 15) { k += 16; continue; } break; }
                        k += r; if (k > 63) break;
                        blk[jpg_zz[k]] = jpg_extend(jpg_recv(&b, s), s) * qt[comp[ci].tq][jpg_zz[k]];
                        k++;
                    }
                    if (bad) break;
                    u32 px = (mx * comp[ci].hs + bx) * 8, py = (my * comp[ci].vs + by) * 8;
                    jpg_idct(blk, comp[ci].plane + py * comp[ci].pw + px, comp[ci].pw);
                }
                if (b.hit_marker && !(ri)) { /* ran into a marker early: the rest stays grey */ }
            }
            for (u32 y = 0; y < h; y++) for (u32 x = 0; x < w; x++) {
                int Y, Cb = 128, Cr = 128;
                { u32 cx = x * comp[0].hs / hmax, cy = y * comp[0].vs / vmax; Y = comp[0].plane[cy * comp[0].pw + cx]; }
                if (nc == 3) {
                    u32 cx = x * comp[1].hs / hmax, cy = y * comp[1].vs / vmax; Cb = comp[1].plane[cy * comp[1].pw + cx];
                    cx = x * comp[2].hs / hmax; cy = y * comp[2].vs / vmax; Cr = comp[2].plane[cy * comp[2].pw + cx];
                    int r = Y + ((91881 * (Cr - 128)) >> 16), g = Y - ((22554 * (Cb - 128) + 46802 * (Cr - 128)) >> 16), bl = Y + ((116130 * (Cb - 128)) >> 16);
                    im->px[y * w + x] = (u32)jpg_clamp(r) << 16 | (u32)jpg_clamp(g) << 8 | jpg_clamp(bl);
                } else im->px[y * w + x] = (u32)Y << 16 | (u32)Y << 8 | (u32)Y;
            }
            ok = 1;
            goto out;
        }
        pos += 2 + len;
    }
out:
    for (int i = 0; i < 3; i++) if (comp[i].plane) IMG_FREE(comp[i].plane);
    IMG_FREE(hdc);
    if (!ok && im->px) img_free(im);
    return ok;
}

/* ---------------------------------------------------------------- dispatch, base64, data: URIs, cache */
__attribute__((noinline)) static int img_decode(const u8 *d, u32 n, img_t *im) {
    im->px = 0; im->w = im->h = 0;
    if (n >= 8 && d[0] == 0x89 && d[1] == 'P') return img_png(d, n, im);
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8) return img_jpeg(d, n, im);
    if (n >= 6 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F') return img_gif(d, n, im);
    return 0;
}

static int img_b64v(u8 c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}
/* decodes base64 (whitespace and padding tolerated) into a fresh buffer; *outn = byte count */
static u8 *img_b64(const u8 *s, u32 n, u32 *outn) {
    u8 *o = (u8 *)IMG_ALLOC(n / 4 * 3 + 4);
    if (!o) return 0;
    u32 acc = 0, bits = 0, k = 0;
    for (u32 i = 0; i < n; i++) {
        int v = img_b64v(s[i]);
        if (v < 0) { if (s[i] == '=') break; continue; }
        acc = acc << 6 | (u32)v; bits += 6;
        if (bits >= 8) { bits -= 8; o[k++] = (u8)(acc >> bits); acc &= (1u << bits) - 1u; }
    }
    *outn = k;
    return o;
}

#define IMG_CACHE_N 12
static struct { u32 key; u8 state; img_t im; } img_cache[IMG_CACHE_N];     /* state 0 free, 1 decoded, 2 failed */
static void img_cache_flush(void) {
    for (int i = 0; i < IMG_CACHE_N; i++) { if (img_cache[i].state == 1) img_free(&img_cache[i].im); img_cache[i].state = 0; img_cache[i].key = 0; }
}
static inline int img_is_data_uri(const u8 *s, u32 n) { return n > 11 && s[0] == 'd' && s[1] == 'a' && s[2] == 't' && s[3] == 'a' && s[4] == ':' && s[5] == 'i' && s[6] == 'm'; }

/* The picture behind an <img src>. `key` identifies the value (the pool offset of the attribute, stable while a document is
 * loaded). Returns the cache slot + 1 of a decoded picture, or 0 (not a data: URI we can read; the alt text is shown). */
__attribute__((noinline)) static int img_for_src(u32 key, const u8 *s, u32 n) {
    if (!img_is_data_uri(s, n)) return 0;
    int free_slot = -1;
    for (int i = 0; i < IMG_CACHE_N; i++) {
        if (img_cache[i].state && img_cache[i].key == key + 1) return img_cache[i].state == 1 ? i + 1 : 0;
        if (!img_cache[i].state && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return 0;
    u32 i = 0; int b64 = 0;
    while (i < n && s[i] != ',') { if (s[i] == ';' && i + 7 <= n && s[i + 1] == 'b' && s[i + 2] == 'a' && s[i + 3] == 's' && s[i + 4] == 'e') b64 = 1; i++; }
    img_cache[free_slot].key = key + 1; img_cache[free_slot].state = 2;
    if (i >= n || !b64) return 0;                                  /* percent-encoded data (SVG...) is not a bitmap */
    u32 bn = 0; u8 *bytes = img_b64(s + i + 1, n - i - 1, &bn);
    if (!bytes) return 0;
    img_t im;
    int ok = img_decode(bytes, bn, &im);
    IMG_FREE(bytes);
    if (!ok) return 0;
    img_cache[free_slot].im = im; img_cache[free_slot].state = 1;
    return free_slot + 1;
}

#endif   /* MW_NO_WEB_IMAGES */

#endif
