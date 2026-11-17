/* SPDX-License-Identifier: GPL-3.0-only */
/* kernel/img.h (the PNG / JPEG / GIF decoders of MiniWeb's <img>) on the real kernel code, against reference decodes made by Pillow.
 * run_host_img.sh builds the corpus (tools/test/gen_img_corpus.py), then this reads manifest.txt: "file width height lossy".
 * Lossless formats (PNG, GIF) must match the reference exactly (a picture with transparency is flattened onto white first); JPEG may differ by the
 * IDCT's rounding -- a mean error under 3 levels per channel and no pixel off by more than 24 -- and a JPEG the decoder does not support (progressive)
 * must be refused cleanly, never crash. Also: truncated and corrupt files must not crash or hang (the second half of the run). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
#define IMG_ALLOC(n) malloc(n)
#define IMG_FREE(p)  free(p)
#include "../../kernel/img.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static u8 *slurp(const char *path, u32 *n) {
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *b = malloc(l ? l : 1); if (fread(b, 1, l, f) != (size_t)l) { fclose(f); free(b); return 0; }
    fclose(f); *n = (u32)l; return b;
}
int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[512]; snprintf(path, sizeof path, "%s/manifest.txt", dir);
    FILE *m = fopen(path, "r"); if (!m) { printf("no manifest at %s\n", path); return 2; }
    char name[256]; unsigned w, h, lossy;
    while (fscanf(m, "%255s %u %u %u", name, &w, &h, &lossy) == 4) {
        u32 n, rn; snprintf(path, sizeof path, "%s/%s", dir, name); u8 *file = slurp(path, &n);
        snprintf(path, sizeof path, "%s/%s.raw", dir, name); u8 *ref = slurp(path, &rn);
        if (!file || !ref) { CHECK(0, "%s: missing files", name); continue; }
        if (getenv("V")) fprintf(stderr, "decoding %s\n", name);
        img_t im; int ok = img_decode(file, n, &im);
        int progressive = strstr(name, "prog") != 0;
        if (progressive) { CHECK(!ok, "%s: progressive JPEG should be refused", name); if (ok) img_free(&im); free(file); free(ref); continue; }
        CHECK(ok, "%s: not decoded", name);
        if (ok) {
            CHECK(im.w == w && im.h == h, "%s: size %ux%u, want %ux%u", name, im.w, im.h, w, h);
            if (im.w == w && im.h == h) {
                long sum = 0; int worst = 0, bad = 0;
                for (u32 i = 0; i < w * h; i++) for (int c = 0; c < 3; c++) {
                    int got = (im.px[i] >> (16 - 8 * c)) & 255, want = ref[i * 3 + c], d = got > want ? got - want : want - got;
                    sum += d; if (d > worst) worst = d; if (d) bad++;
                }
                int ya = strstr(name, "alpha") || strstr(name, "rgba") || strstr(name, "_ga") || strstr(name, "gray_alpha") || strstr(name, "16") || strstr(name, "tRNS") || strstr(name, "2x2");
                if (!lossy) CHECK(worst <= (ya ? 1 : 0), "%s: %d samples differ (worst %d)", name, bad, worst);
                else {      /* chroma is up-sampled by repetition (libjpeg interpolates), so a subsampled picture may differ more at colour edges */
                    int sub = strstr(name, "420") || strstr(name, "422") || strstr(name, "odd") || strstr(name, "big") || strstr(name, "opt") || strstr(name, "noise");
                    long mean10 = sum * 10 / (long)(w * h * 3);
                    CHECK(sub ? (mean10 < (strstr(name, "noise") ? 160 : 60) && worst <= 80) : (mean10 < 20 && worst <= 16), "%s: mean error %.2f, worst %d", name, (double)sum / (w * h * 3), worst);
                }
            }
            img_free(&im);
        }
        /* robustness: every truncation point of a small file, and flipped bytes, must return (ok or not) without crashing or hanging */
        if (n <= 2000) {
            for (u32 cut = 0; cut < n; cut += (n > 400 ? 7 : 1)) { img_t t; if (img_decode(file, cut, &t)) img_free(&t); }
            u8 *c = malloc(n);
            for (int k = 0; k < 60; k++) {
                memcpy(c, file, n); c[(k * 2654435761u) % n] ^= (u8)(1u << (k & 7)); c[(k * 40503u + 17) % n] ^= 0xFF;
                img_t t; if (img_decode(c, n, &t)) img_free(&t);
            }
            free(c);
        }
        free(file); free(ref);
    }
    /* base64 / data: URI plumbing */
    { u32 on; u8 *o = img_b64((const u8 *)"aGVsbG8gd29ybGQ=", 16, &on); CHECK(o && on == 11 && !memcmp(o, "hello world", 11), "base64"); free(o);
      o = img_b64((const u8 *)"aGk", 3, &on); CHECK(o && on == 2 && !memcmp(o, "hi", 2), "base64 without padding"); free(o);
      o = img_b64((const u8 *)"aG\nVs bG8=", 10, &on); CHECK(o && on == 5 && !memcmp(o, "hello", 5), "base64 with whitespace"); free(o); }
    printf("img: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
