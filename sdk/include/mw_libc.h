/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_LIBC_H
#define MW_LIBC_H

/* mw_libc.h -- the small C library a MiniWin program does not otherwise have (SDK 3.0: + malloc/free, rand, strtol, qsort, bsearch). Everything is `static inline` or a macro, so
 * only what a program uses ends up in its file (the linker drops the rest), and nothing needs a startup routine or the kernel.
 * No 64-bit division and no floating point (the program is built without libgcc): mw_muldiv() and the fixed-point helpers are the
 * way to do arithmetic that does not fit in 32 bits. */

typedef int            mw_i32;
typedef unsigned long long mw_u64;
#define MW_NULL ((void *)0)

/* ---- memory and strings ---- */
static inline void *mw_memset(void *d, int c, u32 n) { u8 *p = (u8 *)d; while (n--) *p++ = (u8)c; return d; }
static inline void *mw_memcpy(void *d, const void *s, u32 n) { u8 *p = (u8 *)d; const u8 *q = (const u8 *)s; while (n--) *p++ = *q++; return d; }
static inline void *mw_memmove(void *d, const void *s, u32 n) {
    u8 *p = (u8 *)d; const u8 *q = (const u8 *)s;
    if (p < q) { while (n--) *p++ = *q++; } else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}
static inline int mw_memcmp(const void *a, const void *b, u32 n) { const u8 *p = (const u8 *)a, *q = (const u8 *)b; while (n--) { if (*p != *q) return (int)*p - (int)*q; p++; q++; } return 0; }
static inline int mw_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static inline int mw_strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (int)(u8)*a - (int)(u8)*b; }
static inline int mw_strncmp(const char *a, const char *b, u32 n) { while (n && *a && *a == *b) { a++; b++; n--; } return n ? (int)(u8)*a - (int)(u8)*b : 0; }
static inline char *mw_strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) { } return r; }
/* copies at most cap-1 characters and ALWAYS terminates (unlike C's strncpy) */
static inline char *mw_strlcpy(char *d, const char *s, u32 cap) { u32 i = 0; if (!cap) return d; while (i + 1 < cap && s[i]) { d[i] = s[i]; i++; } d[i] = 0; return d; }
static inline char *mw_strcat(char *d, const char *s) { char *r = d; while (*d) d++; while ((*d++ = *s++)) { } return r; }
static inline char *mw_strchr(const char *s, int c) { for (; *s; s++) if (*s == (char)c) return (char *)s; return (c == 0) ? (char *)s : 0; }
static inline char *mw_strstr(const char *h, const char *n) {
    if (!*n) return (char *)h;
    for (; *h; h++) { const char *a = h, *b = n; while (*a && *b && *a == *b) { a++; b++; } if (!*b) return (char *)h; }
    return 0;
}
static inline int mw_isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int mw_isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static inline int mw_isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static inline int mw_toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static inline int mw_tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
/* "  -42abc" -> -42 (stops at the first non-digit; no overflow check) */
static inline int mw_atoi(const char *s) { int neg = 0, v = 0; while (mw_isspace(*s)) s++; if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++; while (mw_isdigit(*s)) v = v * 10 + (*s++ - '0'); return neg ? -v : v; }

/* ---- numbers ---- */
static inline int mw_abs(int v) { return v < 0 ? -v : v; }
#define mw_min(a, b) ((a) < (b) ? (a) : (b))
#define mw_max(a, b) ((a) > (b) ? (a) : (b))
static inline int mw_clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
/* floor(sqrt(n)) for a 64-bit value, by the digit-by-digit method (shifts and subtracts only) */
static inline u32 mw_isqrt64(mw_u64 n) {
    mw_u64 res = 0, bit = 1ull << 62;
    while (bit > n) bit >>= 2;
    while (bit) { if (n >= res + bit) { n -= res + bit; res = (res >> 1) + bit; } else res >>= 1; bit >>= 2; }
    return (u32)res;
}
static inline u32 mw_isqrt(u32 n) { return mw_isqrt64(n); }
/* (a * b) / c with a 64-bit intermediate (one imul, one idiv), rounded toward zero; saturates to INT_MIN/MAX when the result does
 * not fit in 32 bits (and c == 0), instead of faulting like a bare idiv would. */
static inline int mw_muldiv(int a, int b, int c) {
    long long p = (long long)a * (long long)b;
    int neg = (p < 0) != (c < 0);
    mw_u64 ap = (mw_u64)(p < 0 ? -p : p);
    u32 ac = (u32)(c < 0 ? -c : c);
    if (ac == 0 || (u32)(ap >> 32) >= ac) return neg ? (int)0x80000000 : 0x7FFFFFFF;
    u32 q, r; u32 hi = (u32)(ap >> 32), lo = (u32)ap;
    __asm__("divl %4" : "=a"(q), "=d"(r) : "a"(lo), "d"(hi), "r"(ac) : "cc");
    if (q > 0x7FFFFFFFu) return neg ? (int)0x80000000 : 0x7FFFFFFF;
    return neg ? -(int)q : (int)q;
}
/* sine and cosine of an angle in DEGREES, as fixed point: the result is -256..256 (256 = 1.0) */
static inline int mw_sin(int deg) {
    static const unsigned short t[91] = { 0,4,9,13,18,22,27,31,36,40,44,49,53,58,62,66,71,75,79,83,88,92,96,100,104,108,112,116,120,124,128,132,136,139,143,147,150,154,158,161,165,168,171,175,178,181,184,187,190,193,196,199,202,204,207,210,212,215,217,219,222,224,226,228,230,232,234,236,237,239,241,242,243,245,246,247,248,249,250,251,252,253,254,254,255,255,255,256,256,256,256 };
    deg %= 360; if (deg < 0) deg += 360;
    int sign = 1; if (deg >= 180) { sign = -1; deg -= 180; }
    if (deg > 90) deg = 180 - deg;
    return sign * (int)t[deg];
}
static inline int mw_cos(int deg) { return mw_sin(deg + 90); }

/* ---- text formatting ----
 * mw_snprintf(buf, cap, fmt, ...): %d %i %u %x %X %c %s %% with an optional '-' (left align), '0' (zero pad) and width, e.g. "%02d:%02d",
 * "%-8s", "%5u". Always terminates; returns the length it WOULD have written (like C99), so a caller can detect truncation. */
static inline int mw_vsnprintf(char *buf, u32 cap, const char *fmt, __builtin_va_list ap) {
    u32 n = 0;
    #define MW_PUT(ch) do { if (n + 1 < cap) buf[n] = (char)(ch); n++; } while (0)
    for (; *fmt; fmt++) {
        if (*fmt != '%') { MW_PUT(*fmt); continue; }
        fmt++;
        int left = 0, zero = 0, width = 0;
        for (;; fmt++) { if (*fmt == '-') left = 1; else if (*fmt == '0') zero = 1; else break; }
        while (mw_isdigit(*fmt)) width = width * 10 + (*fmt++ - '0');
        char tmp[16]; int tl = 0; const char *s = 0; int neg = 0;
        char c = *fmt;
        if (c == 'd' || c == 'i') { int v = __builtin_va_arg(ap, int); u32 u = v < 0 ? (u32)(-v) : (u32)v; neg = v < 0; do { tmp[tl++] = (char)('0' + u % 10u); u /= 10u; } while (u); }
        else if (c == 'u') { u32 u = __builtin_va_arg(ap, u32); do { tmp[tl++] = (char)('0' + u % 10u); u /= 10u; } while (u); }
        else if (c == 'x' || c == 'X') { u32 u = __builtin_va_arg(ap, u32); do { u32 d = u & 15u; tmp[tl++] = (char)(d < 10 ? '0' + d : (c == 'x' ? 'a' : 'A') + d - 10); u >>= 4; } while (u); }
        else if (c == 'c') { tmp[tl++] = (char)__builtin_va_arg(ap, int); }
        else if (c == 's') { s = __builtin_va_arg(ap, const char *); if (!s) s = "(null)"; }
        else if (c == '%') { MW_PUT('%'); continue; }
        else if (!c) break;
        else { MW_PUT('%'); MW_PUT(c); continue; }
        int len = s ? mw_strlen(s) : tl + neg;
        int pad = width > len ? width - len : 0;
        if (!left && !(zero && !s)) while (pad--) MW_PUT(' ');
        if (!s && neg) MW_PUT('-');
        if (!left && zero && !s) while (pad--) MW_PUT('0');
        if (s) { for (int i = 0; s[i]; i++) MW_PUT(s[i]); } else while (tl) MW_PUT(tmp[--tl]);
        if (left) while (pad--) MW_PUT(' ');
    }
    if (cap) buf[n < cap ? n : cap - 1] = 0;
    #undef MW_PUT
    return (int)n;
}
static inline int mw_snprintf(char *buf, u32 cap, const char *fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    int r = mw_vsnprintf(buf, cap, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

/* ---- sorting: a plain insertion sort (fine for a few dozen items; mw_qsort below for more) ---- */
static inline void mw_sort(void *base, u32 count, u32 size, int (*cmp)(const void *, const void *)) {
    u8 *a = (u8 *)base, tmp[64];
    if (size > sizeof tmp) return;
    for (u32 i = 1; i < count; i++) {
        mw_memcpy(tmp, a + i * size, size);
        u32 j = i;
        while (j > 0 && cmp(a + (j - 1) * size, tmp) > 0) { mw_memcpy(a + j * size, a + (j - 1) * size, size); j--; }
        mw_memcpy(a + j * size, tmp, size);
    }
}

/* ---- more strings and memory (SDK 3.0) ---- */
static inline void *mw_memchr(const void *s, int c, u32 n) { const u8 *p = (const u8 *)s; while (n--) { if (*p == (u8)c) return (void *)p; p++; } return 0; }
static inline char *mw_strrchr(const char *s, int c) { const char *r = 0; for (;; s++) { if (*s == (char)c) r = s; if (!*s) break; } return (char *)r; }
/* like C's strtol: optional blanks and sign, base 2..36 (0 = decimal, or 0x.. hex, or 0.. octal); *end (if not 0) is left after the last digit used.
 * Saturates to INT_MIN / INT_MAX instead of wrapping. */
static inline int mw_strtol(const char *s, const char **end, int base) {
    while (mw_isspace(*s)) s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; base = 16; }
    else if (base == 0) base = (s[0] == '0') ? 8 : 10;
    u32 v = 0; int any = 0, over = 0;
    for (;; s++) {
        int d = (*s >= '0' && *s <= '9') ? *s - '0' : (*s >= 'a' && *s <= 'z') ? *s - 'a' + 10 : (*s >= 'A' && *s <= 'Z') ? *s - 'A' + 10 : 99;
        if (d >= base) break;
        any = 1;
        if (v > (0x7FFFFFFFu - (u32)d) / (u32)base) over = 1; else v = v * (u32)base + (u32)d;
    }
    if (end) *end = any ? s : 0;
    if (over) return neg ? (int)0x80000000 : 0x7FFFFFFF;
    return neg ? -(int)v : (int)v;
}

/* ---- random numbers: xorshift32. The state starts non-zero ON PURPOSE: a global that is zero would be .bss, which a classic program does not have. ---- */
static u32 mw_rng_state = 0x9E3779B9u;
static inline void mw_srand(u32 seed) { mw_rng_state = seed ? seed : 0x9E3779B9u; }
static inline u32 mw_rand(void) { u32 x = mw_rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; mw_rng_state = x; return x; }
static inline u32 mw_rand_range(u32 n) { return n ? mw_rand() % n : 0; }          /* 0 .. n-1 */

/* ---- searching and sorting: mw_qsort is a heap sort (n log n, no recursion, no extra memory); mw_sort above stays for tiny lists ---- */
static inline void mw_swap_bytes(u8 *a, u8 *b, u32 n) { while (n--) { u8 t = *a; *a++ = *b; *b++ = t; } }
static inline void mw_sift_down(u8 *a, u32 size, u32 i, u32 n, int (*cmp)(const void *, const void *)) {
    for (;;) {
        u32 l = 2 * i + 1, big = i;
        if (l >= n) return;
        if (cmp(a + l * size, a + big * size) > 0) big = l;
        if (l + 1 < n && cmp(a + (l + 1) * size, a + big * size) > 0) big = l + 1;
        if (big == i) return;
        mw_swap_bytes(a + i * size, a + big * size, size);
        i = big;
    }
}
static inline void mw_qsort(void *base, u32 count, u32 size, int (*cmp)(const void *, const void *)) {
    u8 *a = (u8 *)base;
    if (count < 2 || !size) return;
    for (u32 i = count / 2; i-- > 0; ) mw_sift_down(a, size, i, count, cmp);
    for (u32 n = count; n-- > 1; ) { mw_swap_bytes(a, a + n * size, size); mw_sift_down(a, size, 0, n, cmp); }
}
static inline void *mw_bsearch(const void *key, const void *base, u32 count, u32 size, int (*cmp)(const void *, const void *)) {
    u32 lo = 0, hi = count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        int c = cmp(key, (const u8 *)base + mid * size);
        if (c == 0) return (void *)((const u8 *)base + mid * size);
        if (c < 0) hi = mid; else lo = mid + 1;
    }
    return 0;
}

/* ---- a heap of your own: malloc / free / realloc inside ONE block from sys.mem_alloc ----
 * sys.mem_alloc is the kernel's page-sized allocator: 64 blocks at most per program, and every request costs whole pages. A program that
 * makes many small objects (a list, a tree, a game's pieces) takes one big block once and carves it up here.
 *     mw_heap_t *h = mw_heap_new(64 * 1024);        // 0 = out of memory
 *     char *s = mw_heap_alloc(h, 100);              // 8-byte aligned, NOT zeroed (mw_heap_calloc zeroes); 0 = no room
 *     s = mw_heap_realloc(h, s, 400);               // keeps the contents (the block may move)
 *     mw_heap_free(h, s);                           // 1 = freed, 0 = not a live block of this heap (a double free is caught)
 *     sys.mem_free(h);                              // optional: the kernel frees it when the program returns
 * The state lives in the heap's own memory, so there is no global (see mw_malloc below for the version that hides the handle).
 * Blocks next to each other that are freed merge again; the allocator is first-fit. It does not check that a pointer you free really came
 * from alloc: pass it only what alloc gave you. */
typedef struct { u32 bytes, magic, used, reserved; } mw_heap_t;           /* 16 bytes at the start of the block; the blocks follow */
typedef struct { u32 size, prev; } mw_hblk_t;                             /* size: the whole block with this header, a multiple of 8, bit 0 = in use; prev: size of the block before (0 = first) */
#define MW_HEAP_MAGIC 0x4D574850u
static inline u8 *mw_heap_first_(mw_heap_t *h) { return (u8 *)h + sizeof(mw_heap_t); }
static inline u8 *mw_heap_end_(mw_heap_t *h) { return (u8 *)h + sizeof(mw_heap_t) + ((h->bytes - sizeof(mw_heap_t)) & ~7u); }
static inline mw_heap_t *mw_heap_init(void *mem, u32 bytes) {
    if (!mem || bytes < sizeof(mw_heap_t) + 32) return 0;
    mw_heap_t *h = (mw_heap_t *)mem;
    h->bytes = bytes; h->magic = MW_HEAP_MAGIC; h->used = 0; h->reserved = 0;
    mw_hblk_t *b = (mw_hblk_t *)mw_heap_first_(h);
    b->size = (bytes - (u32)sizeof(mw_heap_t)) & ~7u; b->prev = 0;
    return h;
}
#ifndef MW_LIBC_NO_SYS                                                      /* (a host test that includes this header without a syscall table defines MW_LIBC_NO_SYS) */
static inline mw_heap_t *mw_heap_new(u32 bytes) { return mw_heap_init(sys.mem_alloc(bytes), bytes); }
#endif
static inline void *mw_heap_alloc(mw_heap_t *h, u32 n) {
    if (!h || h->magic != MW_HEAP_MAGIC || n == 0 || n > h->bytes) return 0;
    u32 need = ((n + 7u) & ~7u) + (u32)sizeof(mw_hblk_t);
    u8 *p = mw_heap_first_(h), *end = mw_heap_end_(h);
    while (p < end) {
        mw_hblk_t *b = (mw_hblk_t *)p;
        u32 sz = b->size & ~7u;
        if (sz == 0) return 0;                                            /* damaged: stop rather than loop */
        if (!(b->size & 1u) && sz >= need) {
            if (sz - need >= 16) {                                        /* split: the rest stays free */
                mw_hblk_t *r = (mw_hblk_t *)(p + need);
                r->size = sz - need; r->prev = need;
                if (p + sz < end) ((mw_hblk_t *)(p + sz))->prev = sz - need;
                sz = need;
            }
            b->size = sz | 1u;
            h->used += sz;
            return p + sizeof(mw_hblk_t);
        }
        p += sz;
    }
    return 0;
}
static inline void *mw_heap_calloc(mw_heap_t *h, u32 count, u32 size) {
    u32 n = count * size;
    if (size && n / size != count) return 0;                              /* overflow */
    void *p = mw_heap_alloc(h, n);
    if (p) mw_memset(p, 0, n);
    return p;
}
static inline int mw_heap_free(mw_heap_t *h, void *q) {
    if (!h || h->magic != MW_HEAP_MAGIC || !q) return 0;
    u8 *first = mw_heap_first_(h), *end = mw_heap_end_(h), *p = (u8 *)q - sizeof(mw_hblk_t);
    if (p < first || p >= end || ((u32)(p - first) & 7u)) return 0;
    mw_hblk_t *b = (mw_hblk_t *)p;
    if (!(b->size & 1u)) return 0;                                        /* already free */
    u32 sz = b->size & ~7u;
    h->used -= sz;
    u8 *nx = p + sz;
    if (nx < end && !(((mw_hblk_t *)nx)->size & 1u)) { sz += ((mw_hblk_t *)nx)->size & ~7u; nx = p + sz; }      /* merge with the free block after */
    b->size = sz;
    if (b->prev) {                                                        /* ... and with the free block before */
        mw_hblk_t *pb = (mw_hblk_t *)(p - b->prev);
        if (!(pb->size & 1u)) { pb->size += sz; sz = pb->size; b = pb; }
    }
    if (nx < end) ((mw_hblk_t *)nx)->prev = sz;
    return 1;
}
static inline void *mw_heap_realloc(mw_heap_t *h, void *q, u32 n) {
    if (!q) return mw_heap_alloc(h, n);
    if (n == 0) { mw_heap_free(h, q); return 0; }
    mw_hblk_t *b = (mw_hblk_t *)((u8 *)q - sizeof(mw_hblk_t));
    u32 have = (b->size & ~7u) - (u32)sizeof(mw_hblk_t);
    if (n <= have) return q;
    void *r = mw_heap_alloc(h, n);
    if (!r) return 0;                                                     /* the old block is still yours, as in C */
    mw_memcpy(r, q, have);
    mw_heap_free(h, q);
    return r;
}
static inline u32 mw_heap_used(mw_heap_t *h) { return h && h->magic == MW_HEAP_MAGIC ? h->used : 0; }      /* bytes in live blocks (headers included) */

/* The version without a handle: one default heap, made on the first mw_malloc. Define MW_HEAP_BYTES before including miniwin.h to size it
 * (default 64 KB). The pointer starts as 1 (not 0) because a zero global would be .bss -- see the rules in miniwin.h. */
#ifndef MW_LIBC_NO_SYS
#ifndef MW_HEAP_BYTES
#define MW_HEAP_BYTES 65536
#endif
static mw_heap_t *mw_heap_default = (mw_heap_t *)1;
static inline mw_heap_t *mw_heap_get(void) { if (mw_heap_default == (mw_heap_t *)1) mw_heap_default = mw_heap_new(MW_HEAP_BYTES); return mw_heap_default; }
static inline void *mw_malloc(u32 n) { return mw_heap_alloc(mw_heap_get(), n); }
static inline void *mw_calloc(u32 count, u32 size) { return mw_heap_calloc(mw_heap_get(), count, size); }
static inline void *mw_realloc(void *p, u32 n) { return mw_heap_realloc(mw_heap_get(), p, n); }
static inline void mw_free(void *p) { mw_heap_free(mw_heap_get(), p); }
#endif /* MW_LIBC_NO_SYS */

#endif
