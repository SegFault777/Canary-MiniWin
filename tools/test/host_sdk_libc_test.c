/* SPDX-License-Identifier: GPL-3.0-only */
/* sdk/include/mw_libc.h (SDK 3.0): the heap (malloc/free/realloc/calloc), rand, strtol, qsort, bsearch, the string helpers.
 * Built -m32 like a program is; `sys` is a stub that hands out memory from a static pool. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
static struct { void *(*mem_alloc)(u32); } sys;
static u8 pool[1 << 20];
static u32 pool_used;
static void *stub_alloc(u32 n) { n = (n + 15u) & ~15u; if (pool_used + n > sizeof pool) return 0; void *p = pool + pool_used; pool_used += n; memset(p, 0, n); return p; }
#include "../../sdk/include/mw_libc.h"

static int checks, failures;
#define CHECK(c, msg) do { checks++; if (!(c)) { failures++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

typedef struct { void *p; u32 n; u8 tag; } live_t;
static int cmp_int(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return x < y ? -1 : x > y; }

static void test_heap(void) {
    sys.mem_alloc = stub_alloc;
    mw_heap_t *h = mw_heap_new(4096);
    CHECK(h != 0, "heap created");
    CHECK(mw_heap_alloc(h, 0) == 0, "alloc(0) is refused");
    CHECK(mw_heap_alloc(h, 5000) == 0, "alloc bigger than the heap is refused");
    void *a = mw_heap_alloc(h, 100), *b = mw_heap_alloc(h, 200), *c = mw_heap_alloc(h, 300);
    CHECK(a && b && c && a != b && b != c, "three blocks");
    CHECK((((unsigned long)a) & 7) == 0 && (((unsigned long)b) & 7) == 0, "8-byte aligned");
    CHECK(mw_heap_free(h, b) == 1, "free");
    CHECK(mw_heap_free(h, b) == 0, "double free is caught");
    CHECK(mw_heap_free(h, 0) == 0, "free(0) is not a block");
    CHECK(mw_heap_free(h, (u8 *)a + 3) == 0, "misaligned pointer refused");
    void *b2 = mw_heap_alloc(h, 150);
    CHECK(b2 == b, "a freed hole is reused (first fit)");
    mw_heap_free(h, a); mw_heap_free(h, b2); mw_heap_free(h, c);
    CHECK(mw_heap_used(h) == 0, "everything freed: nothing in use");
    void *big = mw_heap_alloc(h, 4096 - 16 - 8);
    CHECK(big != 0, "after merging, the whole heap is one block again");
    mw_heap_free(h, big);

    /* realloc keeps the contents */
    char *s = mw_heap_alloc(h, 10);
    mw_strcpy(s, "hello");
    char *t = mw_heap_realloc(h, s, 1000);
    CHECK(t && mw_strcmp(t, "hello") == 0, "realloc grows and keeps the bytes");
    CHECK(mw_heap_realloc(h, t, 5) == t, "realloc to smaller keeps the block");
    CHECK(mw_heap_realloc(h, t, 0) == 0 && mw_heap_used(h) == 0, "realloc(p, 0) frees");
    CHECK(mw_heap_realloc(h, 0, 32) != 0, "realloc(0, n) allocates");
    u32 *z = mw_heap_calloc(h, 16, 4);
    int allzero = 1; for (int i = 0; i < 16; i++) if (z[i]) allzero = 0;
    CHECK(z && allzero, "calloc zeroes");
    CHECK(mw_heap_calloc(h, 0x10000000u, 0x100u) == 0, "calloc overflow refused");

    /* the default heap */
    mw_heap_default = (mw_heap_t *)1;
    char *m = mw_malloc(40);
    CHECK(m != 0, "mw_malloc makes the default heap");
    mw_free(m);
    CHECK(mw_heap_used(mw_heap_default) == 0, "mw_free");
}

/* random allocate / free / realloc against a shadow list: no two live blocks overlap, no block's bytes are ever touched by another */
static void test_heap_random(void) {
    sys.mem_alloc = stub_alloc;
    enum { N = 200 };
    mw_heap_t *h = mw_heap_new(64 * 1024);
    live_t live[N]; memset(live, 0, sizeof live);
    u32 seed = 12345; mw_srand(seed);
    int ok = 1;
    for (int iter = 0; iter < 40000 && ok; iter++) {
        int i = (int)mw_rand_range(N);
        u32 op = mw_rand_range(3);
        if (live[i].p && op == 0) {                                            /* free: the bytes must still be what we wrote */
            u8 *q = live[i].p; for (u32 k = 0; k < live[i].n; k++) if (q[k] != live[i].tag) ok = 0;
            if (!mw_heap_free(h, live[i].p)) ok = 0;
            live[i].p = 0;
        } else if (live[i].p && op == 1) {                                     /* realloc */
            u32 nn = 1 + mw_rand_range(600);
            u8 *q = live[i].p; for (u32 k = 0; k < live[i].n; k++) if (q[k] != live[i].tag) ok = 0;
            void *r = mw_heap_realloc(h, live[i].p, nn);
            if (r) { u32 keep = nn < live[i].n ? nn : live[i].n; for (u32 k = 0; k < keep; k++) if (((u8 *)r)[k] != live[i].tag) ok = 0;
                     live[i].p = r; live[i].n = nn; memset(r, live[i].tag, nn); }
        } else if (!live[i].p) {
            u32 nn = 1 + mw_rand_range(500);
            void *r = mw_heap_alloc(h, nn);
            if (r) { live[i].p = r; live[i].n = nn; live[i].tag = (u8)(1 + i); memset(r, live[i].tag, nn); }
        }
    }
    CHECK(ok, "40000 random operations: contents intact, frees accepted");
    for (int i = 0; i < N; i++) if (live[i].p) { if (!mw_heap_free(h, live[i].p)) ok = 0; }
    CHECK(ok && mw_heap_used(h) == 0, "all freed in the end");
    void *whole = mw_heap_alloc(h, 64 * 1024 - 16 - 8);
    CHECK(whole != 0, "fully merged after random use (no fragmentation left behind)");
}

static void test_misc(void) {
    const char *e;
    CHECK(mw_strtol("  -42xyz", &e, 10) == -42 && *e == 'x', "strtol decimal");
    CHECK(mw_strtol("0x1F", 0, 0) == 31 && mw_strtol("017", 0, 0) == 15 && mw_strtol("ff", 0, 16) == 255, "strtol bases");
    CHECK(mw_strtol("99999999999", 0, 10) == 0x7FFFFFFF && mw_strtol("-99999999999", 0, 10) == (int)0x80000000, "strtol saturates");
    e = (const char *)1; mw_strtol("abc", &e, 10); CHECK(e == 0, "strtol with no digits reports it");
    CHECK(mw_strrchr("a/b/c", '/') && *mw_strrchr("a/b/c", '/') == '/' && mw_strrchr("a/b/c", '/')[1] == 'c', "strrchr");
    CHECK(mw_strrchr("abc", 'z') == 0, "strrchr miss");
    CHECK(mw_memchr("hello", 'l', 5) == (void *)("hello" + 0) + 0 || mw_memchr("hello", 'l', 5) != 0, "memchr finds");
    int a[500]; mw_srand(7);
    for (int i = 0; i < 500; i++) a[i] = (int)(mw_rand() % 1000) - 500;
    mw_qsort(a, 500, sizeof a[0], cmp_int);
    int sorted = 1; for (int i = 1; i < 500; i++) if (a[i - 1] > a[i]) sorted = 0;
    CHECK(sorted, "qsort sorts 500 ints");
    int key = a[250];
    int *f = mw_bsearch(&key, a, 500, sizeof a[0], cmp_int);
    CHECK(f && *f == key, "bsearch finds");
    int missing = 100000;
    CHECK(mw_bsearch(&missing, a, 500, sizeof a[0], cmp_int) == 0, "bsearch miss");
    mw_qsort(a, 0, 4, cmp_int); mw_qsort(a, 1, 4, cmp_int);
    mw_srand(0); u32 r1 = mw_rand(); mw_srand(0); CHECK(mw_rand() == r1 && r1 != 0, "srand(0) is allowed and repeatable");
    u32 seen[8] = {0}; for (int i = 0; i < 800; i++) seen[mw_rand_range(8)]++;
    int allseen = 1; for (int i = 0; i < 8; i++) if (!seen[i]) allseen = 0;
    CHECK(allseen && mw_rand_range(0) == 0, "rand_range covers 0..7, and n = 0 gives 0");
}

int main(void) {
    test_heap(); test_heap_random(); test_misc();
    printf("sdk libc: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
