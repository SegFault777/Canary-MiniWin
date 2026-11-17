/* SPDX-License-Identifier: GPL-3.0-only */
/* host_crypto_test.c -- compiles MiniWin's REAL crypto headers on the build
 * host (they are freestanding C, so they build fine against a hosted libc) and
 * checks them against vectors from gen_crypto_vectors.py.
 *
 *   tools/test/run_host_crypto.sh
 *
 * Exit status 0 = every vector matched. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../kernel/io.h"
#include "../../kernel/sha512.h"
#include "../../kernel/hmac_sha384.h"
#include "../../kernel/aes.h"
#include "../../kernel/gcm.h"

static int hexval(char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }
static u32 unhex(const char *s, u8 *out) {
    if (s[0] == '-' && s[1] == 0) return 0;
    u32 n = (u32)strlen(s) / 2;
    for (u32 i = 0; i < n; i++) out[i] = (u8)(hexval(s[2*i]) << 4 | hexval(s[2*i+1]));
    return n;
}

#ifdef HAVE_ECC_TESTS
#include "../../kernel/trusted_roots.h"
#include "ecc_tests.h"
#endif

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char **argv) {
    FILE *f = fopen(argc > 1 ? argv[1] : "/tmp/crypto_vectors.txt", "r");
    if (!f) { perror("vectors"); return 2; }
    static char line[400000];
    static u8 a[70000], b[70000], c[70000], d[70000], e[70000], g[70000];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *tok[8]; int nt = 0;
        for (char *p = strtok(line, " "); p && nt < 8; p = strtok(NULL, " ")) tok[nt++] = p;
        if (nt == 0) continue;
        if (!strcmp(tok[0], "SHA384") || !strcmp(tok[0], "SHA512")) {
            int is384 = tok[0][3] == '3';
            u32 n = unhex(tok[1], a), dl = unhex(tok[2], b);
            u8 out[64];
            /* one-shot AND a byte-at-a-time stream (exercises the buffering) */
            if (is384) sha384(a, n, out); else sha512(a, n, out);
            CHECK(memcmp(out, b, dl) == 0, "%s one-shot len=%u", tok[0], n);
            sha512_ctx_t cx; if (is384) sha384_init(&cx); else sha512_init(&cx);
            for (u32 i = 0; i < n; i++) sha512_update(&cx, a + i, 1);
            if (is384) sha384_final(&cx, out); else sha512_final(&cx, out);
            CHECK(memcmp(out, b, dl) == 0, "%s streamed len=%u", tok[0], n);
        } else if (!strcmp(tok[0], "HMAC384")) {
            u32 kl = unhex(tok[1], a), ml = unhex(tok[2], b); unhex(tok[3], c);
            u8 out[48]; hmac_sha384(a, kl, b, ml, out);
            CHECK(memcmp(out, c, 48) == 0, "HMAC384 klen=%u mlen=%u", kl, ml);
        } else if (!strcmp(tok[0], "GCM")) {
            u32 kl = unhex(tok[1], a); unhex(tok[2], b);
            u32 al = unhex(tok[3], c), pl = unhex(tok[4], d);
            unhex(tok[5], e); u8 tag_ref[16]; unhex(tok[6], tag_ref);
            aes_ctx_t ctx; aes_set_key(&ctx, a, (int)kl);
            memcpy(g, d, pl); u8 tag[16];
            gcm_encrypt(&ctx, b, c, al, g, pl, tag);
            CHECK(memcmp(g, e, pl) == 0, "GCM-%u encrypt ct pt=%u aad=%u", kl * 8, pl, al);
            CHECK(memcmp(tag, tag_ref, 16) == 0, "GCM-%u encrypt tag pt=%u aad=%u", kl * 8, pl, al);
            int ok = gcm_decrypt(&ctx, b, c, al, g, pl, tag_ref);
            CHECK(ok && memcmp(g, d, pl) == 0, "GCM-%u decrypt round trip pt=%u", kl * 8, pl);
            u8 bad[16]; memcpy(bad, tag_ref, 16); bad[5] ^= 1;
            memcpy(g, e, pl);
            CHECK(gcm_decrypt(&ctx, b, c, al, g, pl, bad) == 0, "GCM-%u must reject a bad tag pt=%u", kl * 8, pl);
            CHECK(memcmp(g, e, pl) == 0, "GCM-%u must leave ciphertext untouched on a bad tag", kl * 8);
        }
#ifdef HAVE_ECC_TESTS
        else ecc_test_line(tok, nt, &checks, &failures);
#endif
    }
#ifdef HAVE_ECC_TESTS
    /* The compact trust store: walk every entry the way tls_anchored_by_blob() does and check it is well formed -- a subject Name
     * SEQUENCE, a key of a kind we can verify with, an RSA modulus of a sane size, an EC point that really is on its curve. (Whether a
     * root's own signature is good is checked where the table is generated: tools/gen_trusted_roots.py has the full certificates.) */
    {
        int rsa_keys = 0, ec_keys = 0, bad = 0; u32 i = 0; int n = 0;
        while (i < trusted_roots_blob_len) {
            checks++; n++;
            u32 nl = ((u32)trusted_roots_blob[i] << 8) | trusted_roots_blob[i + 1]; i += 2;
            int ok = nl > 8 && trusted_roots_blob[i] == 0x30; i += nl;
            u8 kind = trusted_roots_blob[i++]; const u8 *key = trusted_roots_blob + i;
            if (kind == 1) {
                u32 ml = ((u32)key[0] << 8) | key[1]; bignum_t m; bn_from_bytes_be(&m, key + 2, ml);
                int bits = bn_bit_length(&m); u32 e = ((u32)key[2 + ml] << 24) | ((u32)key[3 + ml] << 16) | ((u32)key[4 + ml] << 8) | key[5 + ml];
                ok = ok && bits >= 2000 && bits <= 4096 && (e & 1) && e >= 3; i += 6 + ml; rsa_keys++;
            } else if (kind == 2 || kind == 3) {
                ecc_curve_t *c = ecc_curve_by_group(kind == 2 ? 23 : 24); u32 pl = kind == 2 ? 65 : 97;
                u8 rnd[48], priv[48], pub[97], shared[48]; for (int q = 0; q < 48; q++) rnd[q] = (u8)(q * 7 + 1);
                ok = ok && c && key[0] == 4 && ecdh_keygen(c, rnd, priv, pub) && ecdh_shared(c, priv, key, pl, shared); i += pl; ec_keys++;   /* ecdh_shared refuses an off-curve point */
            } else ok = 0;
            if (!ok) { bad++; printf("FAIL: trust anchor #%d is malformed\n", n - 1); }
        }
        checks++;
        if (n != TRUSTED_ROOT_COUNT || i != trusted_roots_blob_len) { failures++; printf("FAIL: trust table count/length mismatch (%d vs %d, %u vs %u)\n", n, TRUSTED_ROOT_COUNT, i, trusted_roots_blob_len); }
        failures += bad;
        printf("trust store: %d anchors (%d RSA, %d EC), %d malformed\n", n, rsa_keys, ec_keys, bad);
    }
#endif
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
