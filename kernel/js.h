/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef JS_H
#define JS_H
#include "io.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"

/* ============================================================
 * js.h -- MiniWeb's JavaScript engine. Yes, an actual one. In a kernel. On purpose. Don't look at me like that.
 *
 *   lexer -> recursive-descent parser -> AST in a fixed arena -> tree-walking evaluator.
 *
 * WHAT YOU GET: var/let/const, functions, arrows, closures, template strings, objects, arrays, try/catch/finally,
 * switch, for/for-in/for-of/while/do, ?:, ??, ?., spread-free call syntax, Math/JSON/Object/Array/String helpers,
 * timers, and a mark-sweep GC that does not give a single shit about your feelings.
 *
 * WHAT YOU DON'T GET (the honest part, because lying in a comment is how kernels die):
 *   - NUMBERS ARE 32-BIT INTEGERS. This kernel has never initialised the FPU, links no libgcc, and I refuse to
 *     bet the whole OS on a CR0 bit I can't see. So 7/2 is 3, 1.5 is 1, and NaN / +-Infinity are sentinel values.
 *   - no classes, no prototypes, no regex literals, no destructuring, no generators, no async/await, no modules.
 *   - strings are UTF-8, and length/charAt/slice count CODE POINTS (so Hangul behaves). Everything else is ASCII-ish.
 *
 * MEMORY: one lazy kmalloc'd block per table, never touched until a page actually has a <script>. A runaway script
 * hits a step limit and gets shot in the head. Out of memory throws instead of corrupting something. Both uncatchable.
 *
 * GC: mark-sweep with a CONSERVATIVE scan of the C stack (so the evaluator can hold values in C locals like a normal
 * human) followed by sliding compaction of the strings/slots. It only runs at safe points (loop iterations and
 * call entry). Rule for native code: never keep a raw pointer into js_cp/js_hp across anything that can call JS.
 * ============================================================ */

#ifndef JS_ALLOC
#define JS_ALLOC(n) kmalloc(n)
#define JS_FREE(p)  kfree(p)
#endif
#ifndef JS_LOG
#define JS_LOG(s) serial_puts(s)
#endif

#define JS_MAX_AST   8192
#define JS_MAX_OBJ   3072
#define JS_MAX_STR   4096
#define JS_HEAP_JV   32768
#define JS_HEAP_CH   (128u * 1024u)
#define JS_SCRATCH   8192
#define JS_MAX_DEPTH 60
#define JS_STEPS     3000000
#define JS_MAXARGS   12
#define JS_NAN   ((int)0x80000000)
#define JS_PINF  ((int)0x7FFFFFFF)
#define JS_NINF  ((int)0x80000001)

typedef struct { u32 t; int v; } jv;
enum { JT_UNDEF, JT_NULL, JT_BOOL, JT_NUM, JT_STR, JT_OBJ, JT_NAT };
enum { JK_FREE, JK_OBJ, JK_ARR, JK_FUNC, JK_ENV, JK_ELEM };
typedef struct { u8 kind, mark; u16 flags; u32 poff, pn, pcap, a, b; } jobj;
typedef struct { u32 off, len, hash; u8 mark, ascii, pin, used; } jstr;
typedef struct { u8 t, f; u16 line; int a, b, c, d; u32 next; } jnode;

static jstr  *js_strs;  static u32 js_nstr;
static jobj  *js_objs;  static u32 js_nobj;
static jnode *js_ast;   static u32 js_nast;
static jv    *js_hp;    static u32 js_hp_n;
static char  *js_cp;    static u32 js_cp_n;
static char  *js_tmp;                       /* scratch for string building */
static u32   *js_gcq;                       /* GC worklist / sort buffer */
static int    js_ready, js_dead;            /* tables allocated / allocation failed once, never try again */
static u32    js_genv;                      /* the global environment (also what `window` is) */
static u32    js_env;                       /* the environment code is running in right now */
static int    js_thr;                       /* 0 = fine, 1 = exception in flight, 2 = abort (uncatchable) */
static jv     js_exc;
static int    js_depth, js_steps;
static const u32 *js_stack_base;
static char   js_msg[160];                  /* last error text, for the status line / serial log */
static jv     js_this_global;

#define JV(tt, vv) ((jv){ (tt), (vv) })
static const jv JS_UNDEF = { JT_UNDEF, 0 };
#define JS_NULL JV(JT_NULL, 0)
#define JS_BOOL(b) JV(JT_BOOL, (b) ? 1 : 0)
#define JS_NUM(n) JV(JT_NUM, (n))
#define JS_STRV(i) JV(JT_STR, (int)(i))
#define JS_OBJV(i) JV(JT_OBJ, (int)(i))

/* ---- tiny libc, because we have no libc and we're not sorry ---- */
static void js_mcpy(void *d, const void *s, u32 n) {
    u8 *dd = (u8 *)d; const u8 *ss = (const u8 *)s;
    if (dd < ss) { for (u32 i = 0; i < n; i++) dd[i] = ss[i]; }
    else { for (u32 i = n; i > 0; i--) dd[i - 1] = ss[i - 1]; }
}
static u32 js_slen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static int js_seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* ---- errors ---- */
static void js_log(const char *s) { JS_LOG("[JS] "); JS_LOG(s); JS_LOG("\n"); }
static jv js_newerr(const char *name, const char *msg);
static jv js_throw(const char *name, const char *msg) {
    if (js_thr == 2) return JS_UNDEF;
    js_exc = js_newerr(name, msg);
    if (js_thr == 0) js_thr = 1;
    return JS_UNDEF;
}
static jv js_abort(const char *msg) {                       /* uncatchable: out of memory, out of patience */
    u32 n = 0; while (msg[n] && n < sizeof(js_msg) - 1) { js_msg[n] = msg[n]; n++; } js_msg[n] = 0;
    js_thr = 2; js_exc = JS_UNDEF;
    return JS_UNDEF;
}

/* ============================================================ strings ============================================================ */
static u32 js_hash(const char *p, u32 n) { u32 h = 2166136261u; for (u32 i = 0; i < n; i++) h = (h ^ (u8)p[i]) * 16777619u; return h; }
static const char *js_sp(u32 id) { return js_cp + js_strs[id].off; }
static u32 js_sl(u32 id) { return js_strs[id].len; }

static u32 js_str_new(const char *p, u32 n, int pin) {
    static u32 hint = 1;
    if (n == 0 && !pin) return 0;                           /* string 0 is "", forever */
    if (js_cp_n + n + 1 > JS_HEAP_CH) { js_abort("out of memory (strings)"); return 0; }
    u32 id = 0;
    for (u32 k = 0; k < JS_MAX_STR; k++) {
        u32 i = 1 + (hint - 1 + k) % (JS_MAX_STR - 1);
        if (!js_strs[i].used) { id = i; hint = i + 1; break; }
    }
    if (!id) { js_abort("out of memory (string slots)"); return 0; }
    jstr *s = &js_strs[id];
    s->off = js_cp_n; s->len = n; s->hash = js_hash(p, n); s->mark = 0; s->pin = (u8)pin; s->used = 1; s->ascii = 1;
    for (u32 i = 0; i < n; i++) { js_cp[js_cp_n + i] = p[i]; if ((u8)p[i] >= 0x80) s->ascii = 0; }
    js_cp[js_cp_n + n] = 0;
    js_cp_n += n + 1;
    if (id >= js_nstr) js_nstr = id + 1;
    return id;
}
static u32 js_str_c(const char *c) { return js_str_new(c, js_slen(c), 0); }
static int js_streq(u32 a, u32 b) {
    if (a == b) return 1;
    jstr *x = &js_strs[a], *y = &js_strs[b];
    if (x->hash != y->hash || x->len != y->len) return 0;
    for (u32 i = 0; i < x->len; i++) if (js_cp[x->off + i] != js_cp[y->off + i]) return 0;
    return 1;
}
static int js_str_is(u32 id, const char *lit) {
    jstr *x = &js_strs[id]; u32 i = 0;
    for (; i < x->len; i++) if (!lit[i] || js_cp[x->off + i] != lit[i]) return 0;
    return lit[i] == 0;
}
static u32 js_str_cat(u32 a, u32 b) {
    if (!js_strs[a].len) return b;
    if (!js_strs[b].len) return a;
    u32 la = js_strs[a].len, lb = js_strs[b].len;
    if (js_cp_n + la + lb + 2 > JS_HEAP_CH) { js_abort("out of memory (strings)"); return 0; }
    u32 id = js_str_new("", 0, 1);                          /* grab a slot (pin so the empty-string shortcut can't hand us 0)... */
    if (!id) return 0;
    jstr *s = &js_strs[id];
    s->pin = 0; s->off = js_cp_n; s->len = la + lb;
    js_mcpy(js_cp + js_cp_n, js_cp + js_strs[a].off, la);
    js_mcpy(js_cp + js_cp_n + la, js_cp + js_strs[b].off, lb);
    js_cp[js_cp_n + la + lb] = 0;
    s->hash = js_hash(js_cp + s->off, s->len);
    s->ascii = (u8)(js_strs[a].ascii && js_strs[b].ascii);
    js_cp_n += la + lb + 1;
    return id;
}
/* --- code points: ASCII strings take the fast path, everything else walks the UTF-8 --- */
static u32 js_utf8_dec(const u8 *p, u32 avail, u32 *cp) {
    u8 c = p[0];
    if (c < 0x80 || avail < 2) { *cp = c; return 1; }
    if ((c & 0xE0) == 0xC0 && avail >= 2) { *cp = ((u32)(c & 0x1F) << 6) | (p[1] & 0x3F); return 2; }
    if ((c & 0xF0) == 0xE0 && avail >= 3) { *cp = ((u32)(c & 0x0F) << 12) | ((u32)(p[1] & 0x3F) << 6) | (p[2] & 0x3F); return 3; }
    if ((c & 0xF8) == 0xF0 && avail >= 4) { *cp = ((u32)(c & 7) << 18) | ((u32)(p[1] & 0x3F) << 12) | ((u32)(p[2] & 0x3F) << 6) | (p[3] & 0x3F); return 4; }
    *cp = c; return 1;
}
static u32 js_utf8_enc(u32 cp, char *o) {
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) { o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}
static u32 js_cplen(u32 id) {
    jstr *s = &js_strs[id];
    if (s->ascii) return s->len;
    u32 n = 0;
    for (u32 i = 0; i < s->len; i++) if (((u8)js_cp[s->off + i] & 0xC0) != 0x80) n++;
    return n;
}
static u32 js_cpoff(u32 id, u32 idx) {                      /* byte offset of code point #idx (clamped to the end) */
    jstr *s = &js_strs[id];
    if (s->ascii) return idx < s->len ? idx : s->len;
    u32 o = 0;
    while (idx > 0 && o < s->len) { u32 cp; o += js_utf8_dec((const u8 *)js_cp + s->off + o, s->len - o, &cp); idx--; }
    return o;
}
static u32 js_byte2cp(u32 id, u32 boff) {
    jstr *s = &js_strs[id];
    if (s->ascii) return boff;
    u32 n = 0;
    for (u32 i = 0; i < boff && i < s->len; i++) if (((u8)js_cp[s->off + i] & 0xC0) != 0x80) n++;
    return n;
}
static u32 js_substr(u32 id, u32 from_cp, u32 to_cp) {       /* [from,to) in code points */
    u32 a = js_cpoff(id, from_cp), b = js_cpoff(id, to_cp);
    if (b <= a) return 0;
    return js_str_new(js_cp + js_strs[id].off + a, b - a, 0);
}

/* ============================================================ numbers ============================================================ */
static int js_fin(long long x) { if (x > 2147483646LL) return JS_PINF; if (x < -2147483646LL) return JS_NINF; return (int)x; }
static int js_isnum_special(int v) { return v == JS_NAN || v == JS_PINF || v == JS_NINF; }
static u32 js_num_str(int v) {
    char b[16]; u32 n = 0;
    if (v == JS_NAN) return js_str_c("NaN");
    if (v == JS_PINF) return js_str_c("Infinity");
    if (v == JS_NINF) return js_str_c("-Infinity");
    u32 u = v < 0 ? (u32)(-(v + 1)) + 1u : (u32)v;
    do { b[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    char o[16]; u32 k = 0;
    if (v < 0) o[k++] = '-';
    while (n) o[k++] = b[--n];
    return js_str_new(o, k, 0);
}
static u32 js_num_radix(int v, int radix) {
    if (radix < 2 || radix > 36 || js_isnum_special(v)) return js_num_str(v);
    char b[40], o[40]; u32 n = 0, k = 0;
    u32 u = v < 0 ? (u32)(-(v + 1)) + 1u : (u32)v;
    do { u32 d = u % (u32)radix; b[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); u /= (u32)radix; } while (u);
    if (v < 0) o[k++] = '-';
    while (n) o[k++] = b[--n];
    return js_str_new(o, k, 0);
}
/* string -> number the way Number("...") does it, minus the fractions we can't hold */
static int js_str_num(u32 id) {
    const char *p = js_sp(id); u32 n = js_sl(id), i = 0;
    while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n' || p[i] == '\r')) i++;
    while (n > i && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '\n' || p[n - 1] == '\r')) n--;
    if (i == n) return 0;
    int neg = 0; long long v = 0;
    if (p[i] == '-' || p[i] == '+') { neg = p[i] == '-'; i++; }
    if (i + 1 < n && p[i] == '0' && (p[i + 1] == 'x' || p[i + 1] == 'X')) {
        i += 2; if (i == n) return JS_NAN;
        for (; i < n; i++) {
            int d = p[i] >= '0' && p[i] <= '9' ? p[i] - '0' : (p[i] | 32) >= 'a' && (p[i] | 32) <= 'f' ? (p[i] | 32) - 'a' + 10 : -1;
            if (d < 0) return JS_NAN;
            v = v * 16 + d; if (v > 2147483646LL) return neg ? JS_NINF : JS_PINF;
        }
        return neg ? -(int)v : (int)v;
    }
    if (n - i == 8 && p[i] == 'I' && js_seq(p + i, "Infinity")) return neg ? JS_NINF : JS_PINF;
    int digits = 0;
    for (; i < n && p[i] >= '0' && p[i] <= '9'; i++) { digits++; v = v * 10 + (p[i] - '0'); if (v > 2147483646LL) v = 2147483647LL; }
    if (i < n && p[i] == '.') { i++; for (; i < n && p[i] >= '0' && p[i] <= '9'; i++) digits++; }
    if (!digits || i != n) return JS_NAN;
    if (v > 2147483646LL) return neg ? JS_NINF : JS_PINF;
    return neg ? -(int)v : (int)v;
}

/* ============================================================ the heap ============================================================ */
static u32 js_obj_new(u8 kind, u32 a, u32 b) {
    static u32 hint = 1;
    for (u32 k = 0; k < JS_MAX_OBJ; k++) {
        u32 i = 1 + (hint - 1 + k) % (JS_MAX_OBJ - 1);
        if (js_objs[i].kind == JK_FREE) {
            jobj *o = &js_objs[i];
            o->kind = kind; o->mark = 0; o->flags = 0; o->poff = o->pn = o->pcap = 0; o->a = a; o->b = b;
            hint = i + 1; if (i >= js_nobj) js_nobj = i + 1;
            return i;
        }
    }
    js_abort("out of memory (objects)");
    return 0;
}
static u32 js_hp_alloc(u32 n) {
    if (js_hp_n + n > JS_HEAP_JV) { js_abort("out of memory (heap)"); return 0xFFFFFFFFu; }
    u32 o = js_hp_n; js_hp_n += n; return o;
}
#define JS_UNIT(o) ((o)->kind == JK_ARR ? 1u : 2u)          /* a jv per slot (arrays) or a key/value pair (everything else) */
static jv *js_pv(jobj *o) { return js_hp + o->poff; }
static int js_grow(u32 id, u32 want) {                       /* make room for `want` slots/pairs */
    jobj *o = &js_objs[id];
    if (want <= o->pcap) return 1;
    u32 nc = o->pcap ? o->pcap * 2 : 4; while (nc < want) nc *= 2;
    u32 unit = JS_UNIT(o);
    u32 off = js_hp_alloc(nc * unit);
    if (off == 0xFFFFFFFFu) return 0;
    o = &js_objs[id];
    if (o->pn) js_mcpy(js_hp + off, js_hp + o->poff, o->pn * unit * (u32)sizeof(jv));
    o->poff = off; o->pcap = nc;
    return 1;
}
static int js_prop_find(u32 id, u32 key) {
    jobj *o = &js_objs[id]; jv *p = js_pv(o);
    for (u32 i = 0; i < o->pn; i++) if (js_streq((u32)p[2 * i].v, key)) return (int)i;
    return -1;
}
static jv js_prop_get(u32 id, u32 key, int *found) {
    int i = js_prop_find(id, key);
    if (i < 0) { if (found) *found = 0; return JS_UNDEF; }
    if (found) *found = 1;
    return js_pv(&js_objs[id])[2 * i + 1];
}
static void js_prop_set_ex(u32 id, u32 key, jv val, int konst) {
    int i = js_prop_find(id, key);
    if (i >= 0) { js_pv(&js_objs[id])[2 * i + 1] = val; return; }
    if (!js_grow(id, js_objs[id].pn + 1)) return;
    jobj *o = &js_objs[id]; jv *p = js_pv(o);
    p[2 * o->pn] = JV(JT_STR | (konst ? 0x100u : 0u), (int)key); p[2 * o->pn + 1] = val; o->pn++;
}
static void js_prop_set(u32 id, u32 key, jv val) { js_prop_set_ex(id, key, val, 0); }
static int js_arr_push(u32 id, jv v) {
    if (!js_grow(id, js_objs[id].pn + 1)) return 0;
    jobj *o = &js_objs[id]; js_pv(o)[o->pn++] = v; return 1;
}
static u32 js_arr_new(void) { return js_obj_new(JK_ARR, 0, 0); }

/* ---- environments: an env is just an object whose props are the variables and whose .b is the parent ---- */
static u32 js_env_new(u32 parent) { return js_obj_new(JK_ENV, 0, parent); }
static jv *js_env_slot(u32 env, u32 key, int *konst) {
    for (u32 e = env; e; e = js_objs[e].b) {
        int i = js_prop_find(e, key);
        if (i >= 0) { jv *p = js_pv(&js_objs[e]); if (konst) *konst = (p[2 * i].t & 0x100) != 0; return &p[2 * i + 1]; }
    }
    return 0;
}

/* ============================================================ GC ============================================================ */
static void (*js_gc_roots_hook)(void);
static void js_mark_obj(u32 id);
static void js_mark_val(jv v) {
    if (v.t == JT_STR) { if (v.v > 0 && (u32)v.v < js_nstr) js_strs[v.v].mark = 1; }
    else if (v.t == JT_OBJ) js_mark_obj((u32)v.v);
}
static void js_mark_obj(u32 id) {
    static u32 qn;
    if (id == 0 || id >= js_nobj || js_objs[id].kind == JK_FREE || js_objs[id].mark) return;
    js_objs[id].mark = 1;
    js_gcq[qn++] = id;
    if (qn > 1) return;                                      /* someone up the stack is already draining the queue */
    while (qn) {
        u32 cur = js_gcq[--qn]; jobj *o = &js_objs[cur];
        if (o->kind == JK_FUNC || o->kind == JK_ENV) js_mark_obj(o->b);
        if (o->kind == JK_ARR) { for (u32 i = 0; i < o->pn; i++) js_mark_val(js_pv(o)[i]); }
        else if (o->kind != JK_ELEM) {
            for (u32 i = 0; i < o->pn; i++) { jv *p = js_pv(o); js_mark_val(JV(JT_STR, p[2 * i].v)); js_mark_val(p[2 * i + 1]); }
        }
    }
}
static void js_sort_by(u32 *ids, u32 n, int what) {          /* shell sort; what 0 = objects by poff, 1 = strings by off */
    for (u32 gap = n / 2; gap > 0; gap /= 2)
        for (u32 i = gap; i < n; i++) {
            u32 t = ids[i], j = i;
            while (j >= gap && (what ? js_strs[ids[j - gap]].off : js_objs[ids[j - gap]].poff) > (what ? js_strs[t].off : js_objs[t].poff)) { ids[j] = ids[j - gap]; j -= gap; }
            ids[j] = t;
        }
}
static void js_mark_timers(void);
static void js_gc(void) {
    for (u32 i = 0; i < js_nobj; i++) js_objs[i].mark = 0;
    for (u32 i = 0; i < js_nstr; i++) js_strs[i].mark = (u8)(js_strs[i].pin && js_strs[i].used);
    js_mark_obj(js_genv); js_mark_obj(js_env); js_mark_val(js_exc); js_mark_val(js_this_global);
    js_mark_timers();
    if (js_gc_roots_hook) js_gc_roots_hook();
    /* conservative C stack scan: any word that smells like an id keeps that string/object alive. False positives only cost RAM. */
    __builtin_unwind_init();                                 /* spill callee-saved registers into this frame so the scan sees them */
    const u32 *lo = (const u32 *)__builtin_frame_address(0), *hi = js_stack_base;
    if (hi > lo) for (const u32 *w = lo; w < hi; w++) {
        u32 x = *w;
        if (x > 0 && x < js_nobj) js_mark_obj(x);
        if (x > 0 && x < js_nstr && js_strs[x].used) js_strs[x].mark = 1;
    }
    /* sweep */
    for (u32 i = 1; i < js_nobj; i++) if (js_objs[i].kind != JK_FREE && !js_objs[i].mark) { js_objs[i].kind = JK_FREE; js_objs[i].pn = js_objs[i].pcap = 0; }
    for (u32 i = 1; i < js_nstr; i++) if (js_strs[i].used && !js_strs[i].mark) js_strs[i].used = 0;
    /* compact the jv heap: slide live chunks down in address order */
    u32 n = 0;
    for (u32 i = 1; i < js_nobj; i++) if (js_objs[i].kind != JK_FREE && js_objs[i].pcap) js_gcq[n++] = i;
    js_sort_by(js_gcq, n, 0);
    u32 dst = 0;
    for (u32 k = 0; k < n; k++) {
        jobj *o = &js_objs[js_gcq[k]]; u32 unit = JS_UNIT(o);
        if (o->pn == 0) { o->pcap = 0; o->poff = 0; continue; }
        if (o->poff != dst) js_mcpy(js_hp + dst, js_hp + o->poff, o->pn * unit * (u32)sizeof(jv));
        o->poff = dst; o->pcap = o->pn; dst += o->pn * unit;
    }
    js_hp_n = dst;
    /* ...and the string heap */
    n = 0;
    for (u32 i = 1; i < js_nstr; i++) if (js_strs[i].used) js_gcq[n++] = i;
    js_sort_by(js_gcq, n, 1);
    dst = 0;
    for (u32 k = 0; k < n; k++) {
        jstr *s = &js_strs[js_gcq[k]];
        if (s->off != dst) js_mcpy(js_cp + dst, js_cp + s->off, s->len + 1);
        s->off = dst; dst += s->len + 1;
    }
    js_cp_n = dst;
}
static void js_gc_safe(void) {                                /* call me at loop heads and call entry; I only bite when we're nearly full */
    u32 ou = 0, su = 0;
    if (js_hp_n * 10 < JS_HEAP_JV * 6 && js_cp_n * 10 < JS_HEAP_CH * 6) {
        for (u32 i = 1; i < js_nobj; i += 4) if (js_objs[i].kind != JK_FREE) ou++;     /* sampled: counting everything every time would be silly */
        for (u32 i = 1; i < js_nstr; i += 4) if (js_strs[i].used) su++;
        if (ou * 4 * 10 < JS_MAX_OBJ * 6 && su * 4 * 10 < JS_MAX_STR * 6) return;
    }
    js_gc();
}

/* ============================================================ init ============================================================ */
static int js_init_tables(void) {
    if (js_ready) return 1;
    if (js_dead) return 0;
    js_strs = (jstr *)JS_ALLOC(JS_MAX_STR * sizeof(jstr));
    js_objs = (jobj *)JS_ALLOC(JS_MAX_OBJ * sizeof(jobj));
    js_ast  = (jnode *)JS_ALLOC(JS_MAX_AST * sizeof(jnode));
    js_hp   = (jv *)JS_ALLOC(JS_HEAP_JV * sizeof(jv));
    js_cp   = (char *)JS_ALLOC(JS_HEAP_CH);
    js_tmp  = (char *)JS_ALLOC(JS_SCRATCH);
    js_gcq  = (u32 *)JS_ALLOC((JS_MAX_OBJ > JS_MAX_STR ? JS_MAX_OBJ : JS_MAX_STR) * sizeof(u32) + 64);
    if (!js_strs || !js_objs || !js_ast || !js_hp || !js_cp || !js_tmp || !js_gcq) {
        JS_FREE(js_strs); JS_FREE(js_objs); JS_FREE(js_ast); JS_FREE(js_hp); JS_FREE(js_cp); JS_FREE(js_tmp); JS_FREE(js_gcq);
        js_strs = 0; js_objs = 0; js_ast = 0; js_hp = 0; js_cp = 0; js_tmp = 0; js_gcq = 0;
        js_dead = 1; js_log("not enough RAM for the JS heap, scripts are off"); return 0;
    }
    js_ready = 1;
    return 1;
}
static void js_heap_reset(void) {                            /* a new page: forget everything */
    for (u32 i = 0; i < JS_MAX_STR; i++) js_strs[i].used = 0;
    for (u32 i = 0; i < JS_MAX_OBJ; i++) { js_objs[i].kind = JK_FREE; js_objs[i].mark = 0; }
    js_nstr = 1; js_nobj = 1; js_nast = 1; js_hp_n = 0; js_cp_n = 0;
    js_strs[0].used = 1; js_strs[0].pin = 1; js_strs[0].len = 0; js_strs[0].off = 0; js_strs[0].ascii = 1; js_strs[0].hash = js_hash("", 0);
    js_cp[0] = 0; js_cp_n = 1;
    js_thr = 0; js_exc = JS_UNDEF; js_depth = 0; js_env = 0; js_genv = 0; js_msg[0] = 0;
}

/* ============================================================ lexer ============================================================ */
enum { TK_EOF, TK_NUM, TK_STR, TK_TPL, TK_ID, TK_KW, TK_P };
enum {
    P_NONE, P_LP, P_RP, P_LB, P_RB, P_LC, P_RC, P_SEMI, P_COMMA, P_DOT, P_QDOT, P_COLON, P_Q, P_ARROW, P_ELLIP,
    P_PLUS, P_MINUS, P_STAR, P_SLASH, P_PCT, P_POW, P_INC, P_DEC, P_AND, P_OR, P_XOR, P_TILDE, P_NOT, P_SHL, P_SHR, P_USHR,
    P_LT, P_GT, P_LE, P_GE, P_EQ, P_NE, P_SEQ, P_SNE, P_LAND, P_LOR, P_NULLISH,
    P_ASSIGN, P_ADDA, P_SUBA, P_MULA, P_DIVA, P_MODA, P_ANDA, P_ORA, P_XORA, P_SHLA, P_SHRA, P_USHRA, P_IN, P_INSTANCEOF
};
enum { K_VAR = 1, K_LET, K_CONST, K_FUNCTION, K_RETURN, K_IF, K_ELSE, K_FOR, K_WHILE, K_DO, K_BREAK, K_CONTINUE, K_SWITCH, K_CASE,
       K_DEFAULT, K_THROW, K_TRY, K_CATCH, K_FINALLY, K_NEW, K_DELETE, K_TYPEOF, K_VOID, K_IN, K_INSTANCEOF, K_THIS, K_NULL, K_TRUE, K_FALSE, K_CLASS };
static const char *const js_kwnames[] = { 0, "var", "let", "const", "function", "return", "if", "else", "for", "while", "do", "break", "continue",
    "switch", "case", "default", "throw", "try", "catch", "finally", "new", "delete", "typeof", "void", "in", "instanceof", "this", "null", "true", "false", "class" };
typedef struct { u8 type, nl; u16 op; int num; u32 sid, pos; } jtok;

static const char *js_src; static u32 js_len, js_pos, js_line;
static jtok js_t;
static int js_perr;

static void js_perror(const char *m) {
    if (js_perr) return;
    js_perr = 1;
    u32 n = 0; const char *pre = "SyntaxError: ";
    while (*pre && n < sizeof(js_msg) - 1) js_msg[n++] = *pre++;
    while (*m && n < sizeof(js_msg) - 14) js_msg[n++] = *m++;
    const char *at = " (line "; while (*at) js_msg[n++] = *at++;
    char b[12]; int k = 0; u32 l = js_line; do { b[k++] = (char)('0' + l % 10); l /= 10; } while (l);
    while (k) js_msg[n++] = b[--k];
    js_msg[n++] = ')'; js_msg[n] = 0;
}
static int js_idc(u8 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' || c >= 0x80; }
static int js_hexv(u8 c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }

/* reads a quoted string body (or template chunk) with escapes into js_tmp; returns the byte count, -1 on error */
static int js_lex_escapes(u32 *pos, u32 end, u8 q, int tpl) {
    u32 n = 0, i = *pos;
    while (i < end) {
        u8 c = (u8)js_src[i];
        if (c == q && !tpl) break;
        if (tpl && c == '`') break;
        if (tpl && c == '$' && i + 1 < end && js_src[i + 1] == '{') break;
        if (c == '\n' && !tpl) return -1;
        if (n + 6 >= JS_SCRATCH) return -1;
        if (c == '\\' && i + 1 < end) {
            u8 e = (u8)js_src[++i]; i++;
            switch (e) {
                case 'n': js_tmp[n++] = '\n'; break;  case 't': js_tmp[n++] = '\t'; break;  case 'r': js_tmp[n++] = '\r'; break;
                case 'b': js_tmp[n++] = '\b'; break;  case 'f': js_tmp[n++] = '\f'; break;  case 'v': js_tmp[n++] = '\v'; break;
                case '0': js_tmp[n++] = 0; break;
                case '\n': js_line++; break;
                case 'x': { int h1 = i < end ? js_hexv((u8)js_src[i]) : -1, h2 = i + 1 < end ? js_hexv((u8)js_src[i + 1]) : -1;
                            if (h1 < 0 || h2 < 0) return -1; n += js_utf8_enc((u32)(h1 * 16 + h2), js_tmp + n); i += 2; break; }
                case 'u': { u32 cp = 0;
                            if (i < end && js_src[i] == '{') { i++; while (i < end && js_src[i] != '}') { int h = js_hexv((u8)js_src[i++]); if (h < 0) return -1; cp = cp * 16 + (u32)h; } i++; }
                            else { for (int k = 0; k < 4; k++) { int h = i < end ? js_hexv((u8)js_src[i]) : -1; if (h < 0) return -1; cp = cp * 16 + (u32)h; i++; } }
                            n += js_utf8_enc(cp, js_tmp + n); break; }
                default: js_tmp[n++] = (char)e; break;
            }
            continue;
        }
        if (c == '\n') js_line++;
        js_tmp[n++] = (char)c; i++;
    }
    *pos = i;
    return (int)n;
}
static void js_next(void) {
    js_t.nl = 0; js_t.op = 0; js_t.num = 0; js_t.sid = 0;
    for (;;) {                                               /* whitespace and comments */
        while (js_pos < js_len) {
            u8 c = (u8)js_src[js_pos];
            if (c == '\n') { js_t.nl = 1; js_line++; js_pos++; }
            else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') js_pos++;
            else if (c == 0xC2 && js_pos + 1 < js_len && (u8)js_src[js_pos + 1] == 0xA0) js_pos += 2;      /* nbsp */
            else if (c == 0xEF && js_pos + 2 < js_len && (u8)js_src[js_pos + 1] == 0xBB && (u8)js_src[js_pos + 2] == 0xBF) js_pos += 3;   /* BOM */
            else break;
        }
        if (js_pos + 1 < js_len && js_src[js_pos] == '/' && js_src[js_pos + 1] == '/') { while (js_pos < js_len && js_src[js_pos] != '\n') js_pos++; continue; }
        if (js_pos + 1 < js_len && js_src[js_pos] == '/' && js_src[js_pos + 1] == '*') {
            js_pos += 2;
            while (js_pos + 1 < js_len && !(js_src[js_pos] == '*' && js_src[js_pos + 1] == '/')) { if (js_src[js_pos] == '\n') { js_t.nl = 1; js_line++; } js_pos++; }
            js_pos = js_pos + 2 <= js_len ? js_pos + 2 : js_len; continue;
        }
        /* html comment openers some old pages still wrap scripts in: <!-- ... */
        if (js_pos + 3 < js_len && js_src[js_pos] == '<' && js_src[js_pos + 1] == '!' && js_src[js_pos + 2] == '-' && js_src[js_pos + 3] == '-') { while (js_pos < js_len && js_src[js_pos] != '\n') js_pos++; continue; }
        break;
    }
    js_t.pos = js_pos;
    if (js_pos >= js_len) { js_t.type = TK_EOF; return; }
    u8 c = (u8)js_src[js_pos];
    if ((c >= '0' && c <= '9') || (c == '.' && js_pos + 1 < js_len && js_src[js_pos + 1] >= '0' && js_src[js_pos + 1] <= '9')) {
        long long v = 0; js_t.type = TK_NUM;
        if (c == '0' && js_pos + 1 < js_len && (js_src[js_pos + 1] | 32) == 'x') {
            js_pos += 2; while (js_pos < js_len && js_hexv((u8)js_src[js_pos]) >= 0) { v = v * 16 + js_hexv((u8)js_src[js_pos++]); if (v > 2147483646LL) v = 2147483647LL; }
        } else {
            while (js_pos < js_len && js_src[js_pos] >= '0' && js_src[js_pos] <= '9') { v = v * 10 + (js_src[js_pos++] - '0'); if (v > 2147483646LL) v = 2147483647LL; }
            if (js_pos < js_len && js_src[js_pos] == '.') { js_pos++; while (js_pos < js_len && js_src[js_pos] >= '0' && js_src[js_pos] <= '9') js_pos++; }   /* fractions: noted, discarded, no hard feelings */
            if (js_pos < js_len && (js_src[js_pos] | 32) == 'e') {
                u32 save = js_pos; js_pos++; int sg = 1;
                if (js_pos < js_len && (js_src[js_pos] == '+' || js_src[js_pos] == '-')) { sg = js_src[js_pos] == '-' ? -1 : 1; js_pos++; }
                if (js_pos < js_len && js_src[js_pos] >= '0' && js_src[js_pos] <= '9') {
                    int ex = 0; while (js_pos < js_len && js_src[js_pos] >= '0' && js_src[js_pos] <= '9') { ex = ex * 10 + (js_src[js_pos++] - '0'); if (ex > 20) ex = 20; }
                    if (sg > 0) { while (ex-- > 0 && v < 2147483647LL) { v *= 10; if (v > 2147483646LL) v = 2147483647LL; } } else { while (ex-- > 0) v = (long long)((u32)v / 10u); }
                } else js_pos = save;
            }
        }
        js_t.num = v > 2147483646LL ? JS_PINF : (int)v;
        return;
    }
    if (js_idc(c)) {
        u32 s = js_pos; while (js_pos < js_len && js_idc((u8)js_src[js_pos])) js_pos++;
        u32 n = js_pos - s;
        js_t.type = TK_ID;
        for (int k = 1; k <= K_CLASS; k++) if (js_slen(js_kwnames[k]) == n) { u32 i = 0; while (i < n && js_src[s + i] == js_kwnames[k][i]) i++; if (i == n) { js_t.type = TK_KW; js_t.op = (u16)k; return; } }
        js_t.sid = js_str_new(js_src + s, n, 1);
        return;
    }
    if (c == '"' || c == '\'') {
        u32 p = js_pos + 1; int n = js_lex_escapes(&p, js_len, c, 0);
        if (n < 0 || p >= js_len) { js_perror("bad string literal"); js_t.type = TK_EOF; return; }
        js_pos = p + 1; js_t.type = TK_STR; js_t.sid = js_str_new(js_tmp, (u32)n, 1);
        return;
    }
    if (c == '`') {                                           /* template: remember where the raw body is, the parser takes it from there */
        u32 p = js_pos + 1, depth = 0;
        while (p < js_len) {
            if (js_src[p] == '\\') { p += 2; continue; }
            if (js_src[p] == '\n') js_line++;
            if (js_src[p] == '`' && depth == 0) break;
            if (js_src[p] == '$' && p + 1 < js_len && js_src[p + 1] == '{') { depth++; p += 2; continue; }
            if (js_src[p] == '}' && depth) depth--;
            if (depth && (js_src[p] == '"' || js_src[p] == '\'')) { u8 q = (u8)js_src[p++]; while (p < js_len && (u8)js_src[p] != q) { if (js_src[p] == '\\') p++; p++; } }
            p++;
        }
        if (p >= js_len) { js_perror("unterminated template"); js_t.type = TK_EOF; return; }
        js_t.type = TK_TPL; js_t.num = (int)(js_pos + 1); js_t.sid = p;        /* num = body start, sid = body end (abusing the fields, forgive me) */
        js_pos = p + 1;
        return;
    }
    static const struct { const char *s; u16 op; } ops[] = {
        {">>>=", P_USHRA}, {"===", P_SEQ}, {"!==", P_SNE}, {"**=", P_NONE}, {">>>", P_USHR}, {"<<=", P_SHLA}, {">>=", P_SHRA}, {"...", P_ELLIP},
        {"=>", P_ARROW}, {"==", P_EQ}, {"!=", P_NE}, {"<=", P_LE}, {">=", P_GE}, {"&&", P_LAND}, {"||", P_LOR}, {"??", P_NULLISH}, {"?.", P_QDOT},
        {"++", P_INC}, {"--", P_DEC}, {"+=", P_ADDA}, {"-=", P_SUBA}, {"*=", P_MULA}, {"/=", P_DIVA}, {"%=", P_MODA}, {"&=", P_ANDA}, {"|=", P_ORA}, {"^=", P_XORA},
        {"<<", P_SHL}, {">>", P_SHR}, {"**", P_POW},
        {"(", P_LP}, {")", P_RP}, {"[", P_LB}, {"]", P_RB}, {"{", P_LC}, {"}", P_RC}, {";", P_SEMI}, {",", P_COMMA}, {".", P_DOT}, {":", P_COLON}, {"?", P_Q},
        {"+", P_PLUS}, {"-", P_MINUS}, {"*", P_STAR}, {"/", P_SLASH}, {"%", P_PCT}, {"&", P_AND}, {"|", P_OR}, {"^", P_XOR}, {"~", P_TILDE}, {"!", P_NOT},
        {"<", P_LT}, {">", P_GT}, {"=", P_ASSIGN}, { 0, 0 }
    };
    for (int k = 0; ops[k].s; k++) {
        u32 n = js_slen(ops[k].s), i = 0;
        while (i < n && js_pos + i < js_len && js_src[js_pos + i] == ops[k].s[i]) i++;
        if (i != n || !ops[k].op) continue;
        if (ops[k].op == P_QDOT && js_pos + 2 < js_len && js_src[js_pos + 2] >= '0' && js_src[js_pos + 2] <= '9') continue;   /* a?.5:1 is a ternary, not a dare */
        js_pos += n; js_t.type = TK_P; js_t.op = ops[k].op; return;
    }
    js_perror("unexpected character"); js_pos++; js_t.type = TK_EOF;
}

/* ============================================================ parser ============================================================ */
enum { N_NONE, N_NUM, N_STR, N_TPL, N_ID, N_THIS, N_ARR, N_OBJ, N_PROP, N_FUNC, N_UN, N_UPD, N_BIN, N_LOG, N_ASG, N_COND, N_CALL, N_NEW, N_MEM, N_SEQ,
       N_SPREAD, N_NULL, N_TRUE, N_FALSE,
       N_VAR, N_DECL, N_EXPR, N_BLOCK, N_IF, N_FOR, N_FORIN, N_FOROF, N_WHILE, N_DO, N_RET, N_BRK, N_CONT, N_THROW, N_TRY, N_SWITCH, N_CASE, N_FDECL, N_EMPTY };
#define NF_ARROW   1
#define NF_EXPRBODY 2
#define NF_LEX     1        /* block: contains let/const/function declarations, so it needs its own scope */
#define NF_COMPUTED 1
#define NF_OPT     2        /* member/call: ?. */
#define NF_PREFIX  1
#define NF_DEFAULT 1        /* case: the default clause */

static u32 js_nn(u8 t) {
    if (js_nast >= JS_MAX_AST) { js_perror("script too big"); return 0; }
    u32 i = js_nast++;
    jnode *n = &js_ast[i]; n->t = t; n->f = 0; n->line = (u16)js_line; n->a = n->b = n->c = n->d = 0; n->next = 0;
    return i;
}
static int js_is_p(u16 op) { return js_t.type == TK_P && js_t.op == op; }
static int js_is_kw(u16 k) { return js_t.type == TK_KW && js_t.op == k; }
static int js_accept_p(u16 op) { if (js_is_p(op)) { js_next(); return 1; } return 0; }
static void js_expect_p(u16 op, const char *what) { if (!js_accept_p(op)) js_perror(what); }
static void js_semi(void) {
    if (js_accept_p(P_SEMI)) return;
    if (js_is_p(P_RC) || js_t.type == TK_EOF || js_t.nl) return;
    js_perror("expected ;");
}
static void js_append(u32 *head, u32 *tail, u32 n) { if (!n) return; if (*tail) js_ast[*tail].next = n; else *head = n; *tail = n; }

static u32 js_parse_assign(void);
static u32 js_parse_stmt(void);
static u32 js_parse_expr(void);
static u32 js_parse_block_body(void);

static u32 js_parse_template(u32 bs, u32 be) {
    u32 head = 0, tail = 0, n = js_nn(N_TPL), p = bs;
    while (p <= be && !js_perr) {
        u32 q = p; int len = js_lex_escapes(&q, be, '`', 1);
        if (len < 0) { js_perror("bad template"); break; }
        u32 sn = js_nn(N_STR); js_ast[sn].a = (int)js_str_new(js_tmp, (u32)len, 1); js_append(&head, &tail, sn);
        if (q >= be || js_src[q] != '$') break;
        u32 es = q + 2, depth = 1, e = es;                    /* find the matching } */
        while (e < be && depth) {
            if (js_src[e] == '{') depth++; else if (js_src[e] == '}') { depth--; if (!depth) break; }
            else if (js_src[e] == '"' || js_src[e] == '\'' || js_src[e] == '`') { u8 qq = (u8)js_src[e++]; while (e < be && (u8)js_src[e] != qq) { if (js_src[e] == '\\') e++; e++; } }
            e++;
        }
        if (e >= be && depth) { js_perror("unterminated ${"); break; }
        const char *s0 = js_src; u32 l0 = js_len, p0 = js_pos, ln0 = js_line; jtok t0 = js_t;
        js_len = e; js_pos = es; js_next();
        u32 ex = js_parse_expr();
        if (js_t.type != TK_EOF) js_perror("bad ${} expression");
        js_src = s0; js_len = l0; js_pos = p0; js_line = ln0; js_t = t0;
        js_append(&head, &tail, ex);
        p = e + 1;
    }
    js_ast[n].a = (int)head;
    return n;
}
static u32 js_parse_func(int arrow_prefix_done, u32 name_sid, u32 params_head, int arrow);
static u32 js_parse_params(void) {                            /* after '(' : ident list up to ')' */
    u32 head = 0, tail = 0;
    while (!js_is_p(P_RP) && !js_perr) {
        if (js_t.type != TK_ID) { js_perror("expected parameter name"); break; }
        u32 id = js_nn(N_ID); js_ast[id].a = (int)js_t.sid; js_next();
        if (js_accept_p(P_ASSIGN)) js_ast[id].b = (int)js_parse_assign();              /* default value */
        js_append(&head, &tail, id);
        if (!js_accept_p(P_COMMA)) break;
    }
    js_expect_p(P_RP, "expected )");
    return head;
}
static u32 js_parse_func(int unused, u32 name_sid, u32 params_head, int arrow) {
    (void)unused;
    u32 f = js_nn(N_FUNC);
    js_ast[f].a = (int)params_head; js_ast[f].c = (int)name_sid;
    if (arrow) js_ast[f].f |= NF_ARROW;
    if (arrow && !js_is_p(P_LC)) { js_ast[f].f |= NF_EXPRBODY; js_ast[f].b = (int)js_parse_assign(); return f; }
    js_expect_p(P_LC, "expected {");
    js_ast[f].b = (int)js_parse_block_body();                  /* statement list head, up to and including the '}' */
    return f;
}
static u32 js_parse_object(void) {
    u32 o = js_nn(N_OBJ), head = 0, tail = 0;
    while (!js_is_p(P_RC) && !js_perr) {
        u32 p = js_nn(N_PROP), key = 0;
        if (js_accept_p(P_ELLIP)) { js_ast[p].f |= 4; js_ast[p].b = (int)js_parse_assign(); js_append(&head, &tail, p); if (!js_accept_p(P_COMMA)) break; continue; }
        if (js_t.type == TK_ID || js_t.type == TK_KW || js_t.type == TK_STR) {
            key = js_nn(N_STR);
            if (js_t.type == TK_KW) js_ast[key].a = (int)js_str_c(js_kwnames[js_t.op]); else js_ast[key].a = (int)js_t.sid;
            int shorthand_ok = js_t.type == TK_ID; u32 sid = js_t.sid; js_next();
            if (js_is_p(P_LP)) {                                /* method shorthand */
                js_next(); u32 ps = js_parse_params();
                js_ast[p].b = (int)js_parse_func(0, sid, ps, 0);
            } else if (js_accept_p(P_COLON)) js_ast[p].b = (int)js_parse_assign();
            else if (shorthand_ok) { u32 id = js_nn(N_ID); js_ast[id].a = (int)sid; js_ast[p].b = (int)id; }
            else js_perror("expected :");
        } else if (js_t.type == TK_NUM) {
            key = js_nn(N_NUM); js_ast[key].a = js_t.num; js_ast[p].f |= NF_COMPUTED; js_next();
            js_expect_p(P_COLON, "expected :"); js_ast[p].b = (int)js_parse_assign();
        } else if (js_accept_p(P_LB)) {
            key = js_parse_assign(); js_expect_p(P_RB, "expected ]"); js_ast[p].f |= NF_COMPUTED;
            js_expect_p(P_COLON, "expected :"); js_ast[p].b = (int)js_parse_assign();
        } else { js_perror("bad object literal"); break; }
        js_ast[p].a = (int)key;
        js_append(&head, &tail, p);
        if (!js_accept_p(P_COMMA)) break;
    }
    js_expect_p(P_RC, "expected }");
    js_ast[o].a = (int)head;
    return o;
}
static u32 js_parse_args(void) {                              /* after '(' */
    u32 head = 0, tail = 0;
    while (!js_is_p(P_RP) && !js_perr && js_t.type != TK_EOF) {
        u32 a;
        if (js_accept_p(P_ELLIP)) { a = js_nn(N_SPREAD); js_ast[a].a = (int)js_parse_assign(); } else a = js_parse_assign();
        js_append(&head, &tail, a);
        if (!js_accept_p(P_COMMA)) break;
    }
    js_expect_p(P_RP, "expected )");
    return head;
}
static int js_try_arrow_params(u32 *params) {                 /* at '(' : is this `(a, b) =>` ? backtrack if not */
    const char *s0 = js_src; u32 l0 = js_len, p0 = js_pos, ln0 = js_line, nn0 = js_nast; jtok t0 = js_t; int e0 = js_perr;
    js_next();
    u32 head = 0, tail = 0; int ok = 1;
    while (!js_is_p(P_RP)) {
        if (js_t.type != TK_ID) { ok = 0; break; }
        u32 id = js_nn(N_ID); js_ast[id].a = (int)js_t.sid; js_next();
        if (js_is_p(P_ASSIGN)) { js_next(); js_ast[id].b = (int)js_parse_assign(); if (js_perr) { ok = 0; break; } }
        js_append(&head, &tail, id);
        if (!js_accept_p(P_COMMA)) break;
    }
    if (ok && js_is_p(P_RP)) { js_next(); if (js_is_p(P_ARROW)) { js_next(); *params = head; return 1; } }
    js_src = s0; js_len = l0; js_pos = p0; js_line = ln0; js_nast = nn0; js_t = t0; js_perr = e0;
    return 0;
}
static u32 js_parse_primary(void) {
    u32 n;
    switch (js_t.type) {
        case TK_NUM: n = js_nn(N_NUM); js_ast[n].a = js_t.num; js_next(); return n;
        case TK_STR: n = js_nn(N_STR); js_ast[n].a = (int)js_t.sid; js_next(); return n;
        case TK_TPL: { u32 bs = (u32)js_t.num, be = js_t.sid; js_next(); return js_parse_template(bs, be); }
        case TK_ID: {
            u32 sid = js_t.sid;
            const char *s0 = js_src; u32 l0 = js_len, p0 = js_pos, ln0 = js_line; jtok t0 = js_t;
            js_next();
            if (js_is_p(P_ARROW)) {                             /* x => ... */
                js_next(); u32 id = js_nn(N_ID); js_ast[id].a = (int)sid;
                return js_parse_func(0, 0, id, 1);
            }
            (void)s0; (void)l0; (void)p0; (void)ln0; (void)t0;
            n = js_nn(N_ID); js_ast[n].a = (int)sid; return n;
        }
        case TK_KW:
            switch (js_t.op) {
                case K_THIS: js_next(); return js_nn(N_THIS);
                case K_NULL: js_next(); return js_nn(N_NULL);
                case K_TRUE: js_next(); return js_nn(N_TRUE);
                case K_FALSE: js_next(); return js_nn(N_FALSE);
                case K_FUNCTION: {
                    js_next(); u32 name = 0;
                    if (js_t.type == TK_ID) { name = js_t.sid; js_next(); }
                    js_expect_p(P_LP, "expected ("); u32 ps = js_parse_params();
                    return js_parse_func(0, name, ps, 0);
                }
                case K_CLASS: js_perror("classes are not supported"); return 0;
                default: js_perror("unexpected keyword"); return 0;
            }
        case TK_P:
            if (js_is_p(P_LP)) {
                u32 params;
                if (js_try_arrow_params(&params)) return js_parse_func(0, 0, params, 1);
                js_next(); n = js_parse_expr(); js_expect_p(P_RP, "expected )"); return n;
            }
            if (js_is_p(P_LB)) {
                js_next(); n = js_nn(N_ARR); u32 head = 0, tail = 0;
                while (!js_is_p(P_RB) && !js_perr && js_t.type != TK_EOF) {
                    u32 e;
                    if (js_is_p(P_COMMA)) { e = js_nn(N_ID); js_ast[e].a = -1; }   /* hole: reads as undefined */
                    else if (js_accept_p(P_ELLIP)) { e = js_nn(N_SPREAD); js_ast[e].a = (int)js_parse_assign(); }
                    else e = js_parse_assign();
                    js_append(&head, &tail, e);
                    if (!js_accept_p(P_COMMA)) break;
                }
                js_expect_p(P_RB, "expected ]"); js_ast[n].a = (int)head; return n;
            }
            if (js_is_p(P_LC)) { js_next(); return js_parse_object(); }
            js_perror("unexpected token"); return 0;
        default: js_perror(js_t.type == TK_EOF ? "unexpected end of script" : "unexpected token"); return 0;
    }
}
static u32 js_parse_postfix_chain(u32 e, int allow_call) {
    for (;;) {
        if (js_perr) return e;
        if (js_is_p(P_DOT) || js_is_p(P_QDOT)) {
            int opt = js_is_p(P_QDOT); js_next();
            if (opt && js_is_p(P_LP)) { js_next(); u32 c = js_nn(N_CALL); js_ast[c].a = (int)e; js_ast[c].b = (int)js_parse_args(); js_ast[c].f |= NF_OPT; e = c; continue; }
            if (opt && js_is_p(P_LB)) { js_next(); u32 m = js_nn(N_MEM); js_ast[m].a = (int)e; js_ast[m].b = (int)js_parse_expr(); js_ast[m].f |= NF_COMPUTED | NF_OPT; js_expect_p(P_RB, "expected ]"); e = m; continue; }
            if (js_t.type != TK_ID && js_t.type != TK_KW) { js_perror("expected property name"); return e; }
            u32 k = js_nn(N_STR); js_ast[k].a = js_t.type == TK_KW ? (int)js_str_new(js_kwnames[js_t.op], js_slen(js_kwnames[js_t.op]), 1) : (int)js_t.sid;
            js_next();
            u32 m = js_nn(N_MEM); js_ast[m].a = (int)e; js_ast[m].b = (int)k; if (opt) js_ast[m].f |= NF_OPT; e = m;
        } else if (js_is_p(P_LB)) {
            js_next(); u32 m = js_nn(N_MEM); js_ast[m].a = (int)e; js_ast[m].b = (int)js_parse_expr(); js_ast[m].f |= NF_COMPUTED;
            js_expect_p(P_RB, "expected ]"); e = m;
        } else if (allow_call && js_is_p(P_LP)) {
            js_next(); u32 c = js_nn(N_CALL); js_ast[c].a = (int)e; js_ast[c].b = (int)js_parse_args(); e = c;
        } else if (js_t.type == TK_TPL) { js_perror("tagged templates are not supported"); return e; }
        else return e;
    }
}
static u32 js_parse_unary(void) {
    if (js_t.type == TK_P) {
        u16 op = js_t.op;
        if (op == P_NOT || op == P_MINUS || op == P_PLUS || op == P_TILDE) { js_next(); u32 n = js_nn(N_UN); js_ast[n].f = (u8)op; js_ast[n].a = (int)js_parse_unary(); return n; }
        if (op == P_INC || op == P_DEC) { js_next(); u32 n = js_nn(N_UPD); js_ast[n].f = (u8)op; js_ast[n].c = NF_PREFIX; js_ast[n].a = (int)js_parse_unary(); return n; }
    } else if (js_t.type == TK_KW && (js_t.op == K_TYPEOF || js_t.op == K_VOID || js_t.op == K_DELETE)) {
        u16 k = js_t.op; js_next(); u32 n = js_nn(N_UN); js_ast[n].f = (u8)(100 + k); js_ast[n].a = (int)js_parse_unary(); return n;
    } else if (js_is_kw(K_NEW)) {
        js_next();
        u32 callee = js_parse_primary();
        callee = js_parse_postfix_chain(callee, 0);
        u32 n = js_nn(N_NEW); js_ast[n].a = (int)callee;
        if (js_accept_p(P_LP)) js_ast[n].b = (int)js_parse_args();
        return js_parse_postfix_chain(n, 1);
    }
    u32 e = js_parse_primary();
    e = js_parse_postfix_chain(e, 1);
    if (!js_t.nl && (js_is_p(P_INC) || js_is_p(P_DEC))) { u32 n = js_nn(N_UPD); js_ast[n].f = (u8)js_t.op; js_ast[n].a = (int)e; js_next(); return n; }
    return e;
}
static int js_binprec(u16 *op) {
    if (js_t.type == TK_KW) { if (js_t.op == K_IN) { *op = P_IN; return 8; } if (js_t.op == K_INSTANCEOF) { *op = P_INSTANCEOF; return 8; } return 0; }
    if (js_t.type != TK_P) return 0;
    *op = js_t.op;
    switch (js_t.op) {
        case P_NULLISH: return 1; case P_LOR: return 2; case P_LAND: return 3; case P_OR: return 4; case P_XOR: return 5; case P_AND: return 6;
        case P_EQ: case P_NE: case P_SEQ: case P_SNE: return 7;
        case P_LT: case P_GT: case P_LE: case P_GE: return 8;
        case P_SHL: case P_SHR: case P_USHR: return 9;
        case P_PLUS: case P_MINUS: return 10; case P_STAR: case P_SLASH: case P_PCT: return 11; case P_POW: return 12;
        default: return 0;
    }
}
static int js_no_in;                                           /* parsing a for(...) head: `in` is not an operator there */
static u32 js_parse_bin(int minp) {
    u32 l = js_parse_unary();
    for (;;) {
        u16 op = 0; int p = js_binprec(&op);
        if (!p || p < minp || js_perr) return l;
        if (op == P_IN && js_no_in) return l;
        js_next();
        u32 r = js_parse_bin(op == P_POW ? p : p + 1);
        u32 n = js_nn(op == P_LAND || op == P_LOR || op == P_NULLISH ? N_LOG : N_BIN);
        js_ast[n].f = (u8)op; js_ast[n].a = (int)l; js_ast[n].b = (int)r; l = n;
    }
}
static u32 js_parse_assign(void) {
    u32 l = js_parse_bin(1);
    if (js_perr) return l;
    if (js_is_p(P_Q)) {
        js_next(); u32 n = js_nn(N_COND); js_ast[n].a = (int)l;
        int s = js_no_in; js_no_in = 0; js_ast[n].b = (int)js_parse_assign(); js_no_in = s;
        js_expect_p(P_COLON, "expected :"); js_ast[n].c = (int)js_parse_assign(); return n;
    }
    if (js_t.type == TK_P && js_t.op >= P_ASSIGN && js_t.op <= P_USHRA) {
        u16 op = js_t.op;
        if (js_ast[l].t != N_ID && js_ast[l].t != N_MEM) { js_perror("invalid assignment target"); return l; }
        js_next(); u32 n = js_nn(N_ASG); js_ast[n].f = (u8)op; js_ast[n].a = (int)l; js_ast[n].b = (int)js_parse_assign(); return n;
    }
    return l;
}
static u32 js_parse_expr(void) {
    u32 e = js_parse_assign();
    while (js_is_p(P_COMMA) && !js_perr) { js_next(); u32 n = js_nn(N_SEQ); js_ast[n].a = (int)e; js_ast[n].b = (int)js_parse_assign(); e = n; }
    return e;
}
static u32 js_parse_vardecl(int kind) {                      /* after var/let/const; stops before ; */
    u32 v = js_nn(N_VAR), head = 0, tail = 0; js_ast[v].f = (u8)kind;
    for (;;) {
        if (js_t.type != TK_ID) { js_perror("expected variable name (destructuring is not supported)"); break; }
        u32 d = js_nn(N_DECL); js_ast[d].a = (int)js_t.sid; js_next();
        if (js_accept_p(P_ASSIGN)) js_ast[d].b = (int)js_parse_assign();
        js_append(&head, &tail, d);
        if (!js_accept_p(P_COMMA)) break;
    }
    js_ast[v].a = (int)head;
    return v;
}
static u32 js_parse_block_body(void) {                       /* after '{' : statements up to and including '}' ; returns the first one */
    u32 head = 0, tail = 0;
    while (!js_is_p(P_RC) && js_t.type != TK_EOF && !js_perr) js_append(&head, &tail, js_parse_stmt());
    js_expect_p(P_RC, "expected }");
    return head;
}
static int js_has_lex(u32 head) {
    for (u32 s = head; s; s = js_ast[s].next)
        if (js_ast[s].t == N_FDECL || (js_ast[s].t == N_VAR && js_ast[s].f != 0)) return 1;
    return 0;
}
static u32 js_parse_stmt(void) {
    u32 n;
    if (js_perr) return 0;
    if (js_t.type == TK_P) {
        if (js_is_p(P_LC)) { js_next(); n = js_nn(N_BLOCK); js_ast[n].a = (int)js_parse_block_body(); if (js_has_lex((u32)js_ast[n].a)) js_ast[n].f |= NF_LEX; return n; }
        if (js_is_p(P_SEMI)) { js_next(); return js_nn(N_EMPTY); }
    }
    if (js_t.type == TK_KW) {
        switch (js_t.op) {
            case K_VAR: case K_LET: case K_CONST: { int k = js_t.op == K_VAR ? 0 : js_t.op == K_LET ? 1 : 2; js_next(); n = js_parse_vardecl(k); js_semi(); return n; }
            case K_FUNCTION: {
                js_next();
                if (js_t.type != TK_ID) { js_perror("function statement needs a name"); return 0; }
                u32 name = js_t.sid; js_next(); js_expect_p(P_LP, "expected ("); u32 ps = js_parse_params();
                n = js_nn(N_FDECL); js_ast[n].a = (int)name; js_ast[n].b = (int)js_parse_func(0, name, ps, 0); return n;
            }
            case K_RETURN: { js_next(); n = js_nn(N_RET); if (!js_is_p(P_SEMI) && !js_is_p(P_RC) && js_t.type != TK_EOF && !js_t.nl) js_ast[n].a = (int)js_parse_expr(); js_semi(); return n; }
            case K_IF: {
                js_next(); n = js_nn(N_IF); js_expect_p(P_LP, "expected ("); js_ast[n].a = (int)js_parse_expr(); js_expect_p(P_RP, "expected )");
                js_ast[n].b = (int)js_parse_stmt();
                if (js_is_kw(K_ELSE)) { js_next(); js_ast[n].c = (int)js_parse_stmt(); }
                return n;
            }
            case K_WHILE: { js_next(); n = js_nn(N_WHILE); js_expect_p(P_LP, "expected ("); js_ast[n].a = (int)js_parse_expr(); js_expect_p(P_RP, "expected )"); js_ast[n].b = (int)js_parse_stmt(); return n; }
            case K_DO: {
                js_next(); n = js_nn(N_DO); js_ast[n].b = (int)js_parse_stmt();
                if (!js_is_kw(K_WHILE)) { js_perror("expected while"); return n; }
                js_next(); js_expect_p(P_LP, "expected ("); js_ast[n].a = (int)js_parse_expr(); js_expect_p(P_RP, "expected )"); js_accept_p(P_SEMI); return n;
            }
            case K_FOR: {
                js_next(); js_expect_p(P_LP, "expected ("); u32 init = 0; int kind = -1;
                js_no_in = 1;
                if (js_is_kw(K_VAR) || js_is_kw(K_LET) || js_is_kw(K_CONST)) { kind = js_t.op == K_VAR ? 0 : js_t.op == K_LET ? 1 : 2; js_next(); init = js_parse_vardecl(kind); }
                else if (!js_is_p(P_SEMI)) init = js_parse_expr();
                js_no_in = 0;
                if (init && (js_is_kw(K_IN) || (js_t.type == TK_ID && js_str_is(js_t.sid, "of")))) {
                    int of = !js_is_kw(K_IN); js_next();
                    n = js_nn(of ? N_FOROF : N_FORIN);
                    if (kind >= 0) { js_ast[n].a = js_ast[js_ast[init].a].a; js_ast[n].f = (u8)(kind + 1); }
                    else if (js_ast[init].t == N_ID) js_ast[n].a = js_ast[init].a; else { js_perror("bad for-in/of target"); return n; }
                    js_ast[n].b = (int)(of ? js_parse_assign() : js_parse_expr()); js_expect_p(P_RP, "expected )"); js_ast[n].c = (int)js_parse_stmt(); return n;
                }
                n = js_nn(N_FOR); js_ast[n].a = (int)init; if (kind > 0) js_ast[n].f = 1;
                js_expect_p(P_SEMI, "expected ;");
                if (!js_is_p(P_SEMI)) js_ast[n].b = (int)js_parse_expr();
                js_expect_p(P_SEMI, "expected ;");
                if (!js_is_p(P_RP)) js_ast[n].c = (int)js_parse_expr();
                js_expect_p(P_RP, "expected )"); js_ast[n].d = (int)js_parse_stmt(); return n;
            }
            case K_BREAK: js_next(); n = js_nn(N_BRK); if (js_t.type == TK_ID && !js_t.nl) js_next(); js_semi(); return n;
            case K_CONTINUE: js_next(); n = js_nn(N_CONT); if (js_t.type == TK_ID && !js_t.nl) js_next(); js_semi(); return n;
            case K_THROW: js_next(); n = js_nn(N_THROW); js_ast[n].a = (int)js_parse_expr(); js_semi(); return n;
            case K_TRY: {
                js_next(); n = js_nn(N_TRY);
                js_expect_p(P_LC, "expected {"); u32 b = js_nn(N_BLOCK); js_ast[b].a = (int)js_parse_block_body(); js_ast[n].a = (int)b;
                if (js_is_kw(K_CATCH)) {
                    js_next();
                    if (js_accept_p(P_LP)) { if (js_t.type != TK_ID) { js_perror("expected catch variable"); return n; } js_ast[n].b = (int)js_t.sid; js_next(); js_expect_p(P_RP, "expected )"); }
                    js_expect_p(P_LC, "expected {"); u32 c = js_nn(N_BLOCK); js_ast[c].a = (int)js_parse_block_body(); js_ast[n].c = (int)c; js_ast[n].f |= 1;
                }
                if (js_is_kw(K_FINALLY)) { js_next(); js_expect_p(P_LC, "expected {"); u32 fb = js_nn(N_BLOCK); js_ast[fb].a = (int)js_parse_block_body(); js_ast[n].d = (int)fb; js_ast[n].f |= 2; }
                if (!(js_ast[n].f & 3)) js_perror("try needs catch or finally");
                return n;
            }
            case K_SWITCH: {
                js_next(); n = js_nn(N_SWITCH); js_expect_p(P_LP, "expected ("); js_ast[n].a = (int)js_parse_expr(); js_expect_p(P_RP, "expected )"); js_expect_p(P_LC, "expected {");
                u32 head = 0, tail = 0;
                while (!js_is_p(P_RC) && js_t.type != TK_EOF && !js_perr) {
                    u32 c = js_nn(N_CASE);
                    if (js_is_kw(K_CASE)) { js_next(); js_ast[c].a = (int)js_parse_expr(); }
                    else if (js_is_kw(K_DEFAULT)) { js_next(); js_ast[c].f |= NF_DEFAULT; }
                    else { js_perror("expected case"); break; }
                    js_expect_p(P_COLON, "expected :");
                    u32 sh = 0, st = 0;
                    while (!js_is_p(P_RC) && !js_is_kw(K_CASE) && !js_is_kw(K_DEFAULT) && js_t.type != TK_EOF && !js_perr) js_append(&sh, &st, js_parse_stmt());
                    js_ast[c].b = (int)sh; js_append(&head, &tail, c);
                }
                js_expect_p(P_RC, "expected }"); js_ast[n].b = (int)head; return n;
            }
            case K_CLASS: js_perror("classes are not supported"); return 0;
            default: break;
        }
    }
    n = js_nn(N_EXPR); js_ast[n].a = (int)js_parse_expr(); js_semi();
    return n;
}
/* parses a whole source text into a statement list; returns the head (0 + js_perr on failure) */
static u32 js_parse_program(const char *src, u32 len) {
    js_src = src; js_len = len; js_pos = 0; js_line = 1; js_perr = 0; js_no_in = 0;
    u32 save = js_nast;
    js_next();
    u32 head = 0, tail = 0;
    while (js_t.type != TK_EOF && !js_perr) js_append(&head, &tail, js_parse_stmt());
    if (js_perr) { js_nast = save; return 0; }
    return head;
}

/* ============================================================ evaluator ============================================================ */
enum { JC_NORMAL, JC_BREAK, JC_CONTINUE, JC_RETURN };
/* the step counter: every eval/exec pays a toll, and when the toll booth runs dry the script gets shot */
static int js_tick(void) { if (--js_steps <= 0 && js_thr != 2) js_abort("script took too long, killed it"); return js_thr; }
static jv js_eval(u32 n);
static int js_exec(u32 n, jv *rv);
static int js_exec_list(u32 head, jv *rv);
static jv js_call(jv f, jv thisv, jv *args, int argc);
static jv js_native(u32 id, jv thisv, jv *a, int n);
static u32 js_s_this, js_s_length, js_s_message, js_s_name;
/* hooks for the DOM layer (js_dom.h). get/set return 1 when they handled it. */
static int (*js_hook_get)(jv obj, jv key, jv *out);
static int (*js_hook_set)(jv obj, jv key, jv val);
static int (*js_hook_call)(u32 id, jv thisv, jv *a, int n, jv *out);
static const char *(*js_hook_ename)(u32 node);

#define CHK() do { if (js_thr) return JS_UNDEF; } while (0)
#define ARG(i) ((i) < n ? a[i] : JS_UNDEF)

static int js_truthy(jv v) {
    switch (v.t) {
        case JT_UNDEF: case JT_NULL: return 0;
        case JT_BOOL: return v.v != 0;
        case JT_NUM: return v.v != 0 && v.v != JS_NAN;
        case JT_STR: return js_strs[v.v].len != 0;
        default: return 1;
    }
}
static int js_callable(jv v) { return v.t == JT_NAT || (v.t == JT_OBJ && js_objs[v.v].kind == JK_FUNC); }
static u32 js_tostr(jv v);
static u32 js_arr_join(u32 arr, u32 sep) {
    u32 acc = 0;
    for (u32 i = 0; i < js_objs[arr].pn && !js_thr; i++) {
        jv e = js_pv(&js_objs[arr])[i];
        u32 s = (e.t == JT_UNDEF || e.t == JT_NULL) ? 0 : js_tostr(e);
        if (i) acc = js_str_cat(acc, sep);
        acc = js_str_cat(acc, s);
    }
    return acc;
}
static u32 js_tostr(jv v) {
    switch (v.t) {
        case JT_UNDEF: return js_str_c("undefined");
        case JT_NULL: return js_str_c("null");
        case JT_BOOL: return js_str_c(v.v ? "true" : "false");
        case JT_NUM: return js_num_str(v.v);
        case JT_STR: return (u32)v.v;
        case JT_NAT: return js_str_c("function () { [native code] }");
        case JT_OBJ: {
            jobj *o = &js_objs[v.v];
            if (o->kind == JK_ARR) return js_arr_join((u32)v.v, js_str_c(","));
            if (o->kind == JK_FUNC) return js_str_c("function () { [code] }");
            if (o->kind == JK_ELEM) return js_str_c(js_hook_ename ? js_hook_ename(o->a) : "[object Element]");
            int f1, f2; jv m = js_prop_get((u32)v.v, js_s_message, &f1);
            if (f1) {
                jv nm = js_prop_get((u32)v.v, js_s_name, &f2);
                u32 r = f2 ? js_tostr(nm) : js_str_c("Error");
                r = js_str_cat(r, js_str_c(": ")); return js_str_cat(r, js_tostr(m));
            }
            return js_str_c("[object Object]");
        }
    }
    return 0;
}
static int js_tonum(jv v) {
    switch (v.t) {
        case JT_UNDEF: return JS_NAN;
        case JT_NULL: return 0;
        case JT_BOOL: case JT_NUM: return v.v;
        case JT_STR: return js_str_num((u32)v.v);
        default: return JS_NAN;
    }
}
static int js_strict_eq(jv a, jv b) {
    if (a.t != b.t) return 0;
    switch (a.t) {
        case JT_UNDEF: case JT_NULL: return 1;
        case JT_BOOL: return a.v == b.v;
        case JT_NUM: return a.v == b.v && a.v != JS_NAN;
        case JT_STR: return js_streq((u32)a.v, (u32)b.v);
        case JT_OBJ: return a.v == b.v || (js_objs[a.v].kind == JK_ELEM && js_objs[b.v].kind == JK_ELEM && js_objs[a.v].a == js_objs[b.v].a);
        default: return a.v == b.v;
    }
}
static int js_loose_eq(jv a, jv b) {
    if (a.t == b.t) return js_strict_eq(a, b);
    int an = a.t == JT_UNDEF || a.t == JT_NULL, bn = b.t == JT_UNDEF || b.t == JT_NULL;
    if (an || bn) return an && bn;
    if (a.t == JT_OBJ || b.t == JT_OBJ || a.t == JT_NAT || b.t == JT_NAT) {
        if (a.t == JT_OBJ && b.t != JT_OBJ && b.t != JT_NAT) { a = JS_STRV(js_tostr(a)); return js_loose_eq(a, b); }
        if (b.t == JT_OBJ && a.t != JT_OBJ && a.t != JT_NAT) { b = JS_STRV(js_tostr(b)); return js_loose_eq(a, b); }
        return 0;
    }
    int x = js_tonum(a), y = js_tonum(b);
    return x == y && x != JS_NAN;
}
static int js_cmp_lt(jv a, jv b, int *undef) {                /* a < b ; *undef = comparison involved NaN */
    *undef = 0;
    if (a.t == JT_STR && b.t == JT_STR) {
        const char *p = js_sp((u32)a.v), *q = js_sp((u32)b.v); u32 la = js_sl((u32)a.v), lb = js_sl((u32)b.v), i = 0;
        while (i < la && i < lb && p[i] == q[i]) i++;
        if (i == la || i == lb) return la < lb;
        return (u8)p[i] < (u8)q[i];
    }
    int x = js_tonum(a), y = js_tonum(b);
    if (x == JS_NAN || y == JS_NAN) { *undef = 1; return 0; }
    return x < y;
}
static int js_toint32(jv v) { int x = js_tonum(v); if (x == JS_NAN || x == JS_PINF || x == JS_NINF) return 0; return x; }
static jv js_arith(u16 op, jv l, jv r) {
    if (op == P_PLUS) {
        if (l.t == JT_STR || r.t == JT_STR || l.t == JT_OBJ || r.t == JT_OBJ || l.t == JT_NAT || r.t == JT_NAT) {
            u32 a = js_tostr(l); CHK(); u32 b = js_tostr(r); CHK();
            u32 s = js_str_cat(a, b); CHK(); return JS_STRV(s);
        }
    }
    int x = js_tonum(l), y = js_tonum(r);
    switch (op) {
        case P_PLUS: case P_MINUS: case P_STAR: case P_SLASH: case P_PCT: case P_POW: break;
        case P_AND: return JS_NUM(js_toint32(l) & js_toint32(r));
        case P_OR:  return JS_NUM(js_toint32(l) | js_toint32(r));
        case P_XOR: return JS_NUM(js_toint32(l) ^ js_toint32(r));
        case P_SHL: return JS_NUM((int)((u32)js_toint32(l) << (js_toint32(r) & 31)));
        case P_SHR: return JS_NUM(js_toint32(l) >> (js_toint32(r) & 31));
        case P_USHR: { u32 u = (u32)js_toint32(l) >> (js_toint32(r) & 31); return JS_NUM(u > 2147483646u ? JS_PINF : (int)u); }
        default: return JS_UNDEF;
    }
    if (x == JS_NAN || y == JS_NAN) return JS_NUM(JS_NAN);
    int xi = x == JS_PINF || x == JS_NINF, yi = y == JS_PINF || y == JS_NINF;
    switch (op) {
        case P_PLUS:
            if (xi && yi) return JS_NUM(x == y ? x : JS_NAN);
            if (xi) return JS_NUM(x); if (yi) return JS_NUM(y);
            return JS_NUM(js_fin((long long)x + y));
        case P_MINUS:
            if (xi && yi) return JS_NUM(x != y ? x : JS_NAN);
            if (xi) return JS_NUM(x); if (yi) return JS_NUM(y == JS_PINF ? JS_NINF : JS_PINF);
            return JS_NUM(js_fin((long long)x - y));
        case P_STAR:
            if (xi || yi) { if (x == 0 || y == 0) return JS_NUM(JS_NAN); return JS_NUM(((x < 0) != (y < 0)) ? JS_NINF : JS_PINF); }
            return JS_NUM(js_fin((long long)x * y));
        case P_SLASH:
            if (xi && yi) return JS_NUM(JS_NAN);
            if (xi) return JS_NUM((x == JS_PINF) == (y >= 0) ? JS_PINF : JS_NINF);
            if (yi) return JS_NUM(0);
            if (y == 0) return JS_NUM(x == 0 ? JS_NAN : x > 0 ? JS_PINF : JS_NINF);
            return JS_NUM(x / y);                                     /* integer division: 7/2 === 3. 32-bit idiv only: 64-bit division wants libgcc and we have no libgcc */
        case P_PCT:
            if (xi || y == 0) return JS_NUM(JS_NAN);
            if (yi) return JS_NUM(x);
            return JS_NUM(x % y);
        case P_POW: {
            if (xi || yi) return JS_NUM(JS_NAN);
            if (y < 0) return JS_NUM(x == 1 ? 1 : x == -1 ? ((y & 1) ? -1 : 1) : 0);
            long long acc = 1; for (int i = 0; i < y && i < 64; i++) { acc *= x; if (acc > 2147483646LL || acc < -2147483646LL) return JS_NUM(acc > 0 ? JS_PINF : JS_NINF); }
            return JS_NUM((int)acc);
        }
    }
    return JS_UNDEF;
}
static jv js_binop(u16 op, jv l, jv r) {
    int un;
    switch (op) {
        case P_SEQ: return JS_BOOL(js_strict_eq(l, r));
        case P_SNE: return JS_BOOL(!js_strict_eq(l, r));
        case P_EQ:  return JS_BOOL(js_loose_eq(l, r));
        case P_NE:  return JS_BOOL(!js_loose_eq(l, r));
        case P_LT:  return JS_BOOL(js_cmp_lt(l, r, &un));
        case P_GT:  return JS_BOOL(js_cmp_lt(r, l, &un));
        case P_LE:  { int res = js_cmp_lt(r, l, &un); return JS_BOOL(!un && !res); }
        case P_GE:  { int res = js_cmp_lt(l, r, &un); return JS_BOOL(!un && !res); }
        case P_INSTANCEOF: return JS_BOOL(r.t == JT_NAT ? (r.v == 0) : 0);
        default: return js_arith(op, l, r);
    }
}

/* ---- property access ---- */
static jv js_method(u32 keystr);                               /* a native method looked up by name, or undefined */
static jv js_get(jv o, jv key) {
    if (o.t == JT_UNDEF || o.t == JT_NULL) {
        u32 k = key.t == JT_STR ? (u32)key.v : js_tostr(key); char b[100]; u32 n = 0;
        const char *p1 = "Cannot read properties of ", *p2 = o.t == JT_NULL ? "null" : "undefined", *p3 = " (reading '";
        while (*p1) b[n++] = *p1++; while (*p2) b[n++] = *p2++; while (*p3) b[n++] = *p3++;
        for (u32 i = 0; i < js_sl(k) && n < 90; i++) b[n++] = js_sp(k)[i];
        b[n++] = '\''; b[n++] = ')'; b[n] = 0;
        return js_throw("TypeError", b);
    }
    if (o.t == JT_OBJ) {
        jobj *ob = &js_objs[o.v];
        if (ob->kind == JK_ELEM) { jv out; if (js_hook_get && js_hook_get(o, key, &out)) return out; return JS_UNDEF; }
        if (ob->kind == JK_ARR) {
            if (key.t == JT_NUM) { if (key.v >= 0 && (u32)key.v < ob->pn) return js_pv(ob)[key.v]; return JS_UNDEF; }
            u32 k = js_tostr(key);
            if (js_str_is(k, "length")) return JS_NUM((int)ob->pn);
            const char *p = js_sp(k); u32 l = js_sl(k); int idx = 0, num = l > 0 && l < 9;
            for (u32 i = 0; i < l && num; i++) { if (p[i] < '0' || p[i] > '9') num = 0; else idx = idx * 10 + (p[i] - '0'); }
            if (num) { ob = &js_objs[o.v]; return (u32)idx < ob->pn ? js_pv(ob)[idx] : JS_UNDEF; }
            return js_method(k);
        }
        u32 k = key.t == JT_STR ? (u32)key.v : js_tostr(key);
        int found; jv v = js_prop_get((u32)o.v, k, &found);
        if (found) return v;
        if (ob->kind == JK_ENV || ob->kind == JK_FUNC) { jv out; if (js_hook_get && ob->kind == JK_ENV && js_hook_get(o, key, &out)) return out; }
        return js_method(k);
    }
    if (o.t == JT_STR) {
        if (key.t == JT_NUM) { if (key.v < 0 || (u32)key.v >= js_cplen((u32)o.v)) return JS_UNDEF; return JS_STRV(js_substr((u32)o.v, (u32)key.v, (u32)key.v + 1)); }
        u32 k = js_tostr(key);
        if (js_str_is(k, "length")) return JS_NUM((int)js_cplen((u32)o.v));
        return js_method(k);
    }
    if (o.t == JT_NUM || o.t == JT_BOOL) return js_method(js_tostr(key));
    return JS_UNDEF;
}
static void js_put(jv o, jv key, jv val) {
    if (o.t == JT_UNDEF || o.t == JT_NULL) { js_throw("TypeError", "Cannot set properties of undefined or null"); return; }
    if (o.t != JT_OBJ) return;
    jobj *ob = &js_objs[o.v];
    if (ob->kind == JK_ELEM) { if (js_hook_set) js_hook_set(o, key, val); return; }
    if (ob->kind == JK_ARR) {
        int idx = -1;
        if (key.t == JT_NUM) idx = key.v;
        else { u32 k = js_tostr(key); const char *p = js_sp(k); u32 l = js_sl(k);
               if (js_str_is(k, "length")) { int nl = js_tonum(val); if (nl >= 0 && (u32)nl <= ob->pn) ob->pn = (u32)nl; return; }
               int num = l > 0 && l < 9; idx = 0; for (u32 i = 0; i < l && num; i++) { if (p[i] < '0' || p[i] > '9') num = 0; else idx = idx * 10 + (p[i] - '0'); }
               if (!num) return; }
        if (idx < 0 || idx > 20000) return;
        while ((u32)idx >= js_objs[o.v].pn) { if (!js_arr_push((u32)o.v, JS_UNDEF)) return; }
        js_pv(&js_objs[o.v])[idx] = val;
        return;
    }
    u32 k = key.t == JT_STR ? (u32)key.v : js_tostr(key);
    if (js_thr) return;
    if (ob->kind == JK_ENV) { jv out; (void)out; if (js_hook_set && js_hook_set(o, key, val)) return; }
    js_prop_set((u32)o.v, k, val);
}

static jv js_errname(const char *name, u32 sid, const char *post) {
    char b[120]; u32 n = 0;
    for (u32 i = 0; i < js_sl(sid) && n < 80; i++) b[n++] = js_sp(sid)[i];
    while (*post && n < 118) b[n++] = *post++;
    b[n] = 0;
    return js_throw(name, b);
}
static jv js_lookup(u32 sid) {
    jv *slot = js_env_slot(js_env, sid, 0);
    if (slot) return *slot;
    return js_errname("ReferenceError", sid, " is not defined");
}
static void js_set_name(u32 sid, jv val) {
    int k; jv *slot = js_env_slot(js_env, sid, &k);
    if (slot) { if (k) { js_throw("TypeError", "Assignment to constant variable."); return; } *slot = val; return; }
    js_prop_set(js_genv, sid, val);                           /* sloppy mode: assigning to an undeclared name makes a global, like the bad old days */
}
static void js_def_name(u32 sid, jv val, int konst) { js_prop_set_ex(js_env, sid, val, konst); }

static int js_short;                                          /* an ?. link bailed out: the rest of the chain is skipped, like the spec says */
static jv js_member_key(jnode *m) {
    if (m->f & NF_COMPUTED) { jv k = js_eval((u32)m->b); return k; }
    return JS_STRV((u32)js_ast[m->b].a);
}
static const u16 js_asg_base[] = { P_NONE, P_PLUS, P_MINUS, P_STAR, P_SLASH, P_PCT, P_AND, P_OR, P_XOR, P_SHL, P_SHR, P_USHR };

static jv js_eval_assign(u32 n) {
    jnode *as = &js_ast[n]; u16 op = as->f; jnode *tg = &js_ast[as->a];
    jv val, obj = JS_UNDEF, key = JS_UNDEF;
    if (tg->t == N_MEM) {
        obj = js_eval((u32)tg->a); CHK();
        key = js_member_key(tg); CHK();
    }
    if (op == P_ASSIGN) { val = js_eval((u32)as->b); CHK(); }
    else {
        jv cur = tg->t == N_ID ? js_lookup((u32)tg->a) : js_get(obj, key); CHK();
        jv r = js_eval((u32)as->b); CHK();
        val = js_binop(js_asg_base[op - P_ASSIGN], cur, r); CHK();
    }
    if (tg->t == N_ID) js_set_name((u32)tg->a, val); else js_put(obj, key, val);
    CHK();
    return val;
}
static jv js_eval_update(u32 n) {
    jnode *u = &js_ast[n]; jnode *tg = &js_ast[u->a];
    jv obj = JS_UNDEF, key = JS_UNDEF, cur;
    if (tg->t == N_MEM) { obj = js_eval((u32)tg->a); CHK(); key = js_member_key(tg); CHK(); cur = js_get(obj, key); }
    else if (tg->t == N_ID) cur = js_lookup((u32)tg->a);
    else return js_throw("SyntaxError", "invalid ++/-- target");
    CHK();
    int old = js_tonum(cur);
    jv nv = js_arith(u->f == P_INC ? P_PLUS : P_MINUS, JS_NUM(old), JS_NUM(1));
    if (tg->t == N_ID) js_set_name((u32)tg->a, nv); else js_put(obj, key, nv);
    CHK();
    return (u->c & NF_PREFIX) ? nv : JS_NUM(old);
}
static int js_collect_args(u32 head, jv *av) {
    int ac = 0;
    for (u32 x = head; x; x = js_ast[x].next) {
        if (js_ast[x].t == N_SPREAD) {
            jv sp = js_eval((u32)js_ast[x].a); if (js_thr) return ac;
            if (sp.t == JT_OBJ && js_objs[sp.v].kind == JK_ARR)
                for (u32 i = 0; i < js_objs[sp.v].pn && ac < JS_MAXARGS; i++) av[ac++] = js_pv(&js_objs[sp.v])[i];
            continue;
        }
        jv v = js_eval(x); if (js_thr) return ac;
        if (ac < JS_MAXARGS) av[ac++] = v;
    }
    return ac;
}
static jv js_eval_call(u32 n) {
    jnode *c = &js_ast[n]; jnode *cal = &js_ast[c->a];
    jv thisv = JS_UNDEF, f; jv av[JS_MAXARGS];
    if (cal->t == N_MEM) {
        jv o = js_eval((u32)cal->a); CHK();
        if (js_short) return JS_UNDEF;
        if ((cal->f & NF_OPT) && (o.t == JT_UNDEF || o.t == JT_NULL)) { js_short = 1; return JS_UNDEF; }
        js_short = 0;
        jv k = js_member_key(cal); CHK();
        f = js_get(o, k); CHK();
        thisv = o;
        if (!js_callable(f)) {
            if ((c->f & NF_OPT) && (f.t == JT_UNDEF || f.t == JT_NULL)) { js_short = 1; return JS_UNDEF; }
            return js_errname("TypeError", k.t == JT_STR ? (u32)k.v : js_tostr(k), " is not a function");
        }
    } else {
        f = js_eval((u32)c->a); CHK();
        if (js_short) return JS_UNDEF;
        if (!js_callable(f)) {
            if ((c->f & NF_OPT) && (f.t == JT_UNDEF || f.t == JT_NULL)) { js_short = 1; return JS_UNDEF; }
            return cal->t == N_ID ? js_errname("TypeError", (u32)cal->a, " is not a function") : js_throw("TypeError", "value is not a function");
        }
    }
    int ac = js_collect_args((u32)c->b, av); CHK();
    jv r = js_call(f, thisv, av, ac);
    js_short = 0;
    return r;
}
static jv js_make_obj(void) { u32 o = js_obj_new(JK_OBJ, 0, 0); return JS_OBJV(o); }
static jv js_eval_inner(u32 n) {
    if (js_tick()) return JS_UNDEF;
    jnode *x = &js_ast[n];
    switch (x->t) {
        case N_NUM: return JS_NUM(x->a);
        case N_STR: return JS_STRV((u32)x->a);
        case N_NULL: return JS_NULL;
        case N_TRUE: return JS_BOOL(1);
        case N_FALSE: return JS_BOOL(0);
        case N_ID: if (x->a < 0) return JS_UNDEF; return js_lookup((u32)x->a);
        case N_THIS: { jv *s = js_env_slot(js_env, js_s_this, 0); return s ? *s : js_this_global; }
        case N_TPL: {
            u32 acc = 0;
            for (u32 p = (u32)x->a; p; p = js_ast[p].next) {
                u32 piece;
                if (js_ast[p].t == N_STR) piece = (u32)js_ast[p].a;
                else { jv v = js_eval(p); CHK(); piece = js_tostr(v); }
                CHK(); acc = js_str_cat(acc, piece); CHK();
            }
            return JS_STRV(acc);
        }
        case N_ARR: {
            u32 arr = js_arr_new(); CHK();
            for (u32 e = (u32)x->a; e; e = js_ast[e].next) {
                if (js_ast[e].t == N_SPREAD) {
                    jv sp = js_eval((u32)js_ast[e].a); CHK();
                    if (sp.t == JT_OBJ && js_objs[sp.v].kind == JK_ARR) { for (u32 i = 0; i < js_objs[sp.v].pn; i++) if (!js_arr_push(arr, js_pv(&js_objs[sp.v])[i])) return JS_UNDEF; }
                    else if (sp.t == JT_STR) { u32 cl = js_cplen((u32)sp.v); for (u32 i = 0; i < cl; i++) if (!js_arr_push(arr, JS_STRV(js_substr((u32)sp.v, i, i + 1)))) return JS_UNDEF; }
                    continue;
                }
                jv v = js_eval(e); CHK();
                if (!js_arr_push(arr, v)) return JS_UNDEF;
            }
            return JS_OBJV(arr);
        }
        case N_OBJ: {
            jv ov = js_make_obj(); CHK();
            for (u32 p = (u32)x->a; p; p = js_ast[p].next) {
                jnode *pn = &js_ast[p];
                if (pn->f & 4) {                              /* ...spread */
                    jv sp = js_eval((u32)pn->b); CHK();
                    if (sp.t == JT_OBJ && js_objs[sp.v].kind != JK_ARR && js_objs[sp.v].kind != JK_ELEM)
                        for (u32 i = 0; i < js_objs[sp.v].pn; i++) { jv *pp = js_pv(&js_objs[sp.v]); js_prop_set((u32)ov.v, (u32)pp[2 * i].v, pp[2 * i + 1]); }
                    continue;
                }
                jv key;
                if (pn->f & NF_COMPUTED) { key = js_eval((u32)pn->a); CHK(); key = JS_STRV(js_tostr(key)); }
                else key = JS_STRV((u32)js_ast[pn->a].a);
                jv v = js_eval((u32)pn->b); CHK();
                js_prop_set((u32)ov.v, (u32)key.v, v); CHK();
            }
            return ov;
        }
        case N_FUNC: { u32 f = js_obj_new(JK_FUNC, n, js_env); CHK(); return JS_OBJV(f); }
        case N_UN: {
            u16 op = x->f;
            if (op == 100 + K_TYPEOF) {
                jnode *t = &js_ast[x->a]; jv v;
                if (t->t == N_ID && t->a >= 0 && !js_env_slot(js_env, (u32)t->a, 0)) return JS_STRV(js_str_c("undefined"));
                v = js_eval((u32)x->a); CHK();
                const char *r = "undefined";
                switch (v.t) { case JT_NULL: r = "object"; break; case JT_BOOL: r = "boolean"; break; case JT_NUM: r = "number"; break; case JT_STR: r = "string"; break; case JT_NAT: r = "function"; break;
                               case JT_OBJ: r = js_objs[v.v].kind == JK_FUNC ? "function" : "object"; break; default: break; }
                return JS_STRV(js_str_c(r));
            }
            if (op == 100 + K_DELETE) {
                jnode *t = &js_ast[x->a];
                if (t->t == N_MEM) {
                    jv o = js_eval((u32)t->a); CHK(); jv k = js_member_key(t); CHK();
                    if (o.t == JT_OBJ && js_objs[o.v].kind == JK_OBJ) {
                        u32 ks = js_tostr(k); int i = js_prop_find((u32)o.v, ks);
                        if (i >= 0) { jobj *ob = &js_objs[o.v]; jv *p = js_pv(ob); js_mcpy(p + 2 * i, p + 2 * (i + 1), (ob->pn - (u32)i - 1) * 2 * (u32)sizeof(jv)); ob->pn--; }
                    }
                }
                return JS_BOOL(1);
            }
            jv v = js_eval((u32)x->a); CHK();
            switch (op) {
                case P_NOT: return JS_BOOL(!js_truthy(v));
                case P_MINUS: { int q = js_tonum(v); if (q == JS_NAN) return JS_NUM(q); if (q == JS_PINF) return JS_NUM(JS_NINF); if (q == JS_NINF) return JS_NUM(JS_PINF); return JS_NUM(-q); }
                case P_PLUS: return JS_NUM(js_tonum(v));
                case P_TILDE: return JS_NUM(~js_toint32(v));
                case 100 + K_VOID: return JS_UNDEF;
            }
            return JS_UNDEF;
        }
        case N_UPD: return js_eval_update(n);
        case N_BIN: {
            jv l = js_eval((u32)x->a); CHK();
            jv r = js_eval((u32)x->b); CHK();
            if (x->f == P_IN) {
                if (r.t != JT_OBJ) return js_throw("TypeError", "right side of 'in' is not an object");
                u32 k = js_tostr(l); CHK();
                if (js_objs[r.v].kind == JK_ARR) { int idx = js_str_num(k); return JS_BOOL(idx >= 0 && idx != JS_NAN && (u32)idx < js_objs[r.v].pn); }
                return JS_BOOL(js_prop_find((u32)r.v, k) >= 0);
            }
            return js_binop(x->f, l, r);
        }
        case N_LOG: {
            jv l = js_eval((u32)x->a); CHK();
            if (x->f == P_LAND) return js_truthy(l) ? js_eval((u32)x->b) : l;
            if (x->f == P_LOR) return js_truthy(l) ? l : js_eval((u32)x->b);
            return (l.t == JT_UNDEF || l.t == JT_NULL) ? js_eval((u32)x->b) : l;
        }
        case N_ASG: return js_eval_assign(n);
        case N_COND: { jv t = js_eval((u32)x->a); CHK(); return js_eval((u32)(js_truthy(t) ? x->b : x->c)); }
        case N_SEQ: { js_eval((u32)x->a); CHK(); return js_eval((u32)x->b); }
        case N_MEM: {
            jv o = js_eval((u32)x->a); CHK();
            if (js_short) return JS_UNDEF;
            if ((x->f & NF_OPT) && (o.t == JT_UNDEF || o.t == JT_NULL)) { js_short = 1; return JS_UNDEF; }
            jv k = js_member_key(x); CHK();
            js_short = 0;
            return js_get(o, k);
        }
        case N_CALL: return js_eval_call(n);
        case N_NEW: {
            jv f = js_eval((u32)x->a); CHK(); jv av[JS_MAXARGS];
            int ac = js_collect_args((u32)x->b, av); CHK();
            if (f.t == JT_OBJ && js_objs[f.v].kind == JK_FUNC) {
                jv ov = js_make_obj(); CHK();
                jv r = js_call(f, ov, av, ac); CHK();
                return r.t == JT_OBJ ? r : ov;
            }
            if (f.t == JT_NAT) return js_native((u32)f.v, JV(JT_UNDEF, 1), av, ac);        /* thisv.v == 1 : "called with new" */
            return js_throw("TypeError", "not a constructor");
        }
        case N_SPREAD: return js_throw("SyntaxError", "unexpected spread");
    }
    return JS_UNDEF;
}

/* wrapper: only member/call nodes may hand a live ?. short-circuit to their parent; everything else wipes it on the way out */
static jv js_eval(u32 n) {
    jv r = js_eval_inner(n);
    u8 t = js_ast[n].t;
    if (t != N_MEM && t != N_CALL) js_short = 0;
    return r;
}

/* ---- hoisting: var names and function declarations show up before the code that uses them, as the JS gods intended ---- */
static void js_hoist_fdecls(u32 head) {
    for (u32 s = head; s && !js_thr; s = js_ast[s].next)
        if (js_ast[s].t == N_FDECL) { u32 f = js_obj_new(JK_FUNC, (u32)js_ast[s].b, js_env); if (js_thr) return; js_prop_set(js_env, (u32)js_ast[s].a, JS_OBJV(f)); }
}
static void js_hoist_vars(u32 head) {
    for (u32 s = head; s && !js_thr; s = js_ast[s].next) {
        jnode *x = &js_ast[s];
        switch (x->t) {
            case N_VAR: if (x->f == 0) for (u32 d = (u32)x->a; d; d = js_ast[d].next) if (js_prop_find(js_env, (u32)js_ast[d].a) < 0) js_prop_set(js_env, (u32)js_ast[d].a, JS_UNDEF); break;
            case N_IF: if (x->b) js_hoist_vars((u32)x->b); if (x->c) js_hoist_vars((u32)x->c); break;
            case N_FOR: if (x->a) js_hoist_vars((u32)x->a); if (x->d) js_hoist_vars((u32)x->d); break;
            case N_FORIN: case N_FOROF: if (x->f == 1 && js_prop_find(js_env, (u32)x->a) < 0) js_prop_set(js_env, (u32)x->a, JS_UNDEF); js_hoist_vars((u32)x->c); break;
            case N_WHILE: case N_DO: js_hoist_vars((u32)x->b); break;
            case N_BLOCK: js_hoist_vars((u32)x->a); break;
            case N_TRY: js_hoist_vars((u32)js_ast[x->a].a); if (x->c) js_hoist_vars((u32)js_ast[x->c].a); if (x->d) js_hoist_vars((u32)js_ast[x->d].a); break;
            case N_SWITCH: for (u32 c = (u32)x->b; c; c = js_ast[c].next) js_hoist_vars((u32)js_ast[c].b); break;
            default: break;
        }
        if (x->t == N_IF || x->t == N_WHILE || x->t == N_DO) { /* single-statement bodies are statements, not lists: the next hoist call handled their own `next` chain (empty) */ }
    }
}

/* ---- statements ---- */
static int js_exec_list(u32 head, jv *rv) {
    for (u32 s = head; s; s = js_ast[s].next) {
        int c = js_exec(s, rv);
        if (js_thr) return JC_NORMAL;
        if (c != JC_NORMAL) return c;
    }
    return JC_NORMAL;
}
static int js_exec_scoped(u32 head, jv *rv, int lex) {
    if (!lex) return js_exec_list(head, rv);
    u32 saved = js_env; js_env = js_env_new(saved); if (js_thr) { js_env = saved; return JC_NORMAL; }
    js_hoist_fdecls(head);
    int c = js_exec_list(head, rv);
    js_env = saved;
    return c;
}
static void js_each_bind(jnode *s, jv v, u32 saved_env) {       /* for-in/of: bind the loop variable for this iteration */
    if (s->f >= 2) { js_env = js_env_new(saved_env); if (js_thr) return; js_def_name((u32)s->a, v, s->f == 3); }
    else js_set_name((u32)s->a, v);
}
static int js_exec(u32 n, jv *rv) {
    if (!n) return JC_NORMAL;
    if (js_tick()) return JC_NORMAL;
    jnode *s = &js_ast[n];
    switch (s->t) {
        case N_EMPTY: case N_FDECL: return JC_NORMAL;
        case N_EXPR: *rv = js_eval((u32)s->a); return JC_NORMAL;
        case N_VAR:
            for (u32 d = (u32)s->a; d; d = js_ast[d].next) {
                jv v = JS_UNDEF; int has = js_ast[d].b != 0;
                if (has) { v = js_eval((u32)js_ast[d].b); if (js_thr) return JC_NORMAL; }
                if (s->f == 0) { if (has) js_set_name((u32)js_ast[d].a, v); else if (!js_env_slot(js_env, (u32)js_ast[d].a, 0)) js_def_name((u32)js_ast[d].a, JS_UNDEF, 0); }
                else js_def_name((u32)js_ast[d].a, v, s->f == 2);
                if (js_thr) return JC_NORMAL;
            }
            return JC_NORMAL;
        case N_BLOCK: return js_exec_scoped((u32)s->a, rv, s->f & NF_LEX);
        case N_IF: {
            jv t = js_eval((u32)s->a); if (js_thr) return JC_NORMAL;
            if (js_truthy(t)) return js_exec((u32)s->b, rv);
            return s->c ? js_exec((u32)s->c, rv) : JC_NORMAL;
        }
        case N_WHILE: {
            for (;;) {
                js_gc_safe();
                jv t = js_eval((u32)s->a); if (js_thr || !js_truthy(t)) break;
                int c = js_exec((u32)s->b, rv); if (js_thr) break; if (c == JC_BREAK) break; if (c == JC_RETURN) return c;
            }
            return JC_NORMAL;
        }
        case N_DO: {
            for (;;) {
                js_gc_safe();
                int c = js_exec((u32)s->b, rv); if (js_thr) break; if (c == JC_BREAK) break; if (c == JC_RETURN) return c;
                jv t = js_eval((u32)s->a); if (js_thr || !js_truthy(t)) break;
            }
            return JC_NORMAL;
        }
        case N_FOR: {
            u32 saved = js_env; int lex = s->f;
            if (lex) { js_env = js_env_new(saved); if (js_thr) { js_env = saved; return JC_NORMAL; } }
            if (s->a) { if (js_ast[s->a].t == N_VAR) js_exec((u32)s->a, rv); else js_eval((u32)s->a); }
            while (!js_thr) {
                js_gc_safe();
                if (s->b) { jv t = js_eval((u32)s->b); if (js_thr || !js_truthy(t)) break; }
                int c = js_exec((u32)s->d, rv); if (js_thr) break;
                if (c == JC_BREAK) break;
                if (c == JC_RETURN) { js_env = saved; return c; }
                if (lex) {                                    /* fresh scope per iteration, so closures capture THIS i and not the final one */
                    u32 old = js_env, ne = js_env_new(saved); if (js_thr) break;
                    for (u32 i = 0; i < js_objs[old].pn; i++) { jv *p = js_pv(&js_objs[old]); js_prop_set_ex(ne, (u32)p[2 * i].v, p[2 * i + 1], (p[2 * i].t & 0x100) != 0); }
                    js_env = ne;
                }
                if (s->c) js_eval((u32)s->c);
            }
            js_env = saved;
            return JC_NORMAL;
        }
        case N_FORIN: case N_FOROF: {
            u32 saved = js_env;
            jv it = js_eval((u32)s->b); if (js_thr) return JC_NORMAL;
            int of = s->t == N_FOROF; u32 keys = 0;
            if (!of) {                                        /* snapshot the keys first; mutating while iterating stays sane */
                keys = js_arr_new(); if (js_thr) return JC_NORMAL;
                if (it.t == JT_OBJ && js_objs[it.v].kind == JK_ARR) { for (u32 i = 0; i < js_objs[it.v].pn; i++) js_arr_push(keys, JS_STRV(js_num_str((int)i))); }
                else if (it.t == JT_OBJ && js_objs[it.v].kind != JK_ELEM) { for (u32 i = 0; i < js_objs[it.v].pn; i++) js_arr_push(keys, JS_STRV((u32)js_pv(&js_objs[it.v])[2 * i].v)); }
                else if (it.t == JT_STR) { u32 cl = js_cplen((u32)it.v); for (u32 i = 0; i < cl; i++) js_arr_push(keys, JS_STRV(js_num_str((int)i))); }
                if (js_thr) return JC_NORMAL;
            } else if (!(it.t == JT_STR || (it.t == JT_OBJ && js_objs[it.v].kind == JK_ARR))) { js_throw("TypeError", "value is not iterable"); return JC_NORMAL; }
            for (u32 i = 0; !js_thr; i++) {
                js_gc_safe();
                jv v;
                if (of) {
                    if (it.t == JT_STR) { if (i >= js_cplen((u32)it.v)) break; v = JS_STRV(js_substr((u32)it.v, i, i + 1)); }
                    else { if (i >= js_objs[it.v].pn) break; v = js_pv(&js_objs[it.v])[i]; }
                } else { if (i >= js_objs[keys].pn) break; v = js_pv(&js_objs[keys])[i]; }
                js_env = saved; js_each_bind(s, v, saved); if (js_thr) break;
                int c = js_exec((u32)s->c, rv); if (js_thr) break;
                if (c == JC_BREAK) break;
                if (c == JC_RETURN) { js_env = saved; return c; }
            }
            js_env = saved;
            return JC_NORMAL;
        }
        case N_RET: { jv v = s->a ? js_eval((u32)s->a) : JS_UNDEF; if (js_thr) return JC_NORMAL; *rv = v; return JC_RETURN; }
        case N_BRK: return JC_BREAK;
        case N_CONT: return JC_CONTINUE;
        case N_THROW: { jv v = js_eval((u32)s->a); if (js_thr) return JC_NORMAL; js_exc = v; js_thr = 1; return JC_NORMAL; }
        case N_TRY: {
            u32 saved = js_env; int saved_depth = js_depth;
            int c = js_exec((u32)s->a, rv);
            if (js_thr == 1 && (s->f & 1)) {
                jv e = js_exc; js_thr = 0; js_exc = JS_UNDEF; js_env = saved; js_depth = saved_depth;
                js_env = js_env_new(saved);
                if (!js_thr) { if (s->b) js_def_name((u32)s->b, e, 0); c = js_exec((u32)s->c, rv); }
            }
            js_env = saved;
            if ((s->f & 2) && js_thr != 2) {
                int st = js_thr; jv ex = js_exc; jv rsave = *rv;
                js_thr = 0; js_exc = JS_UNDEF;
                int fc = js_exec((u32)s->d, rv);
                if (js_thr || fc != JC_NORMAL) return fc;     /* finally went nuclear (throw/return/break): it wins */
                js_thr = st; js_exc = ex; *rv = rsave;
            }
            return c;
        }
        case N_SWITCH: {
            jv d = js_eval((u32)s->a); if (js_thr) return JC_NORMAL;
            u32 saved = js_env; js_env = js_env_new(saved); if (js_thr) { js_env = saved; return JC_NORMAL; }
            u32 start = 0, def = 0;
            for (u32 c = (u32)s->b; c && !start; c = js_ast[c].next) {
                if (js_ast[c].f & NF_DEFAULT) { def = c; continue; }
                jv t = js_eval((u32)js_ast[c].a); if (js_thr) { js_env = saved; return JC_NORMAL; }
                if (js_strict_eq(d, t)) start = c;
            }
            if (!start) start = def;
            int res = JC_NORMAL;
            for (u32 c = start; c && !js_thr; c = js_ast[c].next) {
                js_hoist_fdecls((u32)js_ast[c].b);
                int r = js_exec_list((u32)js_ast[c].b, rv);
                if (r == JC_BREAK) break;
                if (r != JC_NORMAL) { res = r; break; }
            }
            js_env = saved;
            return res;
        }
        default: return JC_NORMAL;
    }
}

/* ---- calls ---- */
static jv js_call(jv f, jv thisv, jv *args, int argc) {
    if (f.t == JT_NAT) return js_native((u32)f.v, thisv, args, argc);
    if (f.t != JT_OBJ || js_objs[f.v].kind != JK_FUNC) return js_throw("TypeError", "value is not a function");
    if (js_depth >= JS_MAX_DEPTH) return js_throw("RangeError", "Maximum call stack size exceeded");
    u32 fo = (u32)f.v; u32 fn = js_objs[fo].a, closure = js_objs[fo].b;
    js_depth++;
    js_gc_safe();
    u32 saved = js_env;
    u32 env = js_env_new(closure);
    if (js_thr) { js_depth--; return JS_UNDEF; }
    js_env = env;
    jnode *fd = &js_ast[fn];
    if (!(fd->f & NF_ARROW)) js_prop_set(env, js_s_this, thisv);
    int i = 0;
    for (u32 p = (u32)fd->a; p && !js_thr; p = js_ast[p].next, i++) {
        jv v = i < argc ? args[i] : JS_UNDEF;
        if (v.t == JT_UNDEF && js_ast[p].b) v = js_eval((u32)js_ast[p].b);
        js_prop_set(env, (u32)js_ast[p].a, v);
    }
    jv rv = JS_UNDEF;
    if (!js_thr) {
        if (fd->f & NF_EXPRBODY) rv = js_eval((u32)fd->b);
        else { js_hoist_vars((u32)fd->b); js_hoist_fdecls((u32)fd->b); jv r = JS_UNDEF; int c = js_exec_list((u32)fd->b, &r); if (c == JC_RETURN) rv = r; }
    }
    js_env = saved; js_depth--;
    return js_thr ? JS_UNDEF : rv;
}

/* ============================================================ natives ============================================================ */
#define JS_NATIVES(X) \
 X(PARSEINT,"parseInt") X(PARSEFLOAT,"parseFloat") X(ISNAN,"isNaN") X(ISFINITE,"isFinite") X(STRING,"String") X(NUMBER,"Number") X(BOOLEAN,"Boolean") \
 X(ERROR,"Error") X(TYPEERROR,"TypeError") X(RANGEERROR,"RangeError") X(ALERT,"alert") X(CONFIRM,"confirm") X(PROMPT,"prompt") X(SETTIMEOUT,"setTimeout") \
 X(SETINTERVAL,"setInterval") X(CLEARTIMER,"clearTimeout") X(LOG,"log") X(FLOOR,"floor") X(CEIL,"ceil") X(ROUND,"round") X(ABS,"abs") X(MAX,"max") \
 X(MIN,"min") X(POW,"pow") X(SQRT,"sqrt") X(RANDOM,"random") X(SIGN,"sign") X(TRUNC,"trunc") X(KEYS,"keys") X(VALUES,"values") X(ENTRIES,"entries") \
 X(ASSIGN,"assign") X(HASOWN,"hasOwnProperty") X(ISARRAY,"isArray") X(STRINGIFY,"stringify") X(PARSE,"parse") X(NOW,"now") X(FROMCHARCODE,"fromCharCode") \
 X(CHARAT,"charAt") X(CHARCODEAT,"charCodeAt") X(INDEXOF,"indexOf") X(LASTINDEXOF,"lastIndexOf") X(INCLUDES,"includes") X(STARTSWITH,"startsWith") \
 X(ENDSWITH,"endsWith") X(SLICE,"slice") X(SUBSTRING,"substring") X(SUBSTR,"substr") X(TOUPPER,"toUpperCase") X(TOLOWER,"toLowerCase") X(TRIM,"trim") \
 X(SPLIT,"split") X(REPLACE,"replace") X(REPLACEALL,"replaceAll") X(REPEAT,"repeat") X(PADSTART,"padStart") X(PADEND,"padEnd") X(CONCAT,"concat") \
 X(AT,"at") X(TOSTRING,"toString") X(TOFIXED,"toFixed") X(PUSH,"push") X(POP,"pop") X(SHIFT,"shift") X(UNSHIFT,"unshift") X(SPLICE,"splice") \
 X(REVERSE,"reverse") X(JOIN,"join") X(FOREACH,"forEach") X(MAP,"map") X(FILTER,"filter") X(REDUCE,"reduce") X(FIND,"find") X(FINDINDEX,"findIndex") \
 X(SOME,"some") X(EVERY,"every") X(SORT,"sort") X(FILL,"fill") X(ISINTEGER,"isInteger") X(VALUEOF,"valueOf")
enum { JN_NONE = 0,
#define X(id, nm) JN_##id,
    JS_NATIVES(X)
#undef X
    JN__COUNT };
static const char *const js_nat_names[JN__COUNT] = { 0,
#define X(id, nm) nm,
    JS_NATIVES(X)
#undef X
};
static const struct { const char *name; u32 id; } js_aliases[] = {
    {"info", JN_LOG}, {"warn", JN_LOG}, {"error", JN_LOG}, {"debug", JN_LOG}, {"clearInterval", JN_CLEARTIMER}, {"trimStart", JN_TRIM}, {"trimEnd", JN_TRIM},
    {"trimLeft", JN_TRIM}, {"trimRight", JN_TRIM}, {"parseFloat", JN_PARSEINT}, {0, 0}
};
static jv js_method(u32 keystr) {
    for (u32 i = 1; i < JN__COUNT; i++) if (js_str_is(keystr, js_nat_names[i])) return JV(JT_NAT, (int)i);
    for (int i = 0; js_aliases[i].name; i++) if (js_str_is(keystr, js_aliases[i].name)) return JV(JT_NAT, (int)js_aliases[i].id);
    return JS_UNDEF;
}
static jv js_newerr(const char *name, const char *msg) {
    u32 o = js_obj_new(JK_OBJ, 0, 0);
    if (!o) return JS_UNDEF;
    int sv = js_thr; js_thr = 0;
    js_prop_set(o, js_s_name, JS_STRV(js_str_c(name)));
    js_prop_set(o, js_s_message, JS_STRV(js_str_c(msg)));
    js_thr = sv ? sv : js_thr;
    return JS_OBJV(o);
}

/* ---- timers ---- */
#define JS_MAX_TIMERS 32
typedef struct { int id; u32 due, every; jv fn; u8 active; } jtimer;
static jtimer js_timers[JS_MAX_TIMERS];
static int js_timer_seq;
static u32 (*js_clock)(void);
static u32 js_now(void) { return js_clock ? js_clock() : 0; }
static void js_mark_timers(void) { for (int i = 0; i < JS_MAX_TIMERS; i++) if (js_timers[i].active) js_mark_val(js_timers[i].fn); }
static int js_timer_add(jv fn, int ms, int repeat) {
    for (int i = 0; i < JS_MAX_TIMERS; i++) if (!js_timers[i].active) {
        if (ms < 1) ms = 1;
        js_timers[i].id = ++js_timer_seq; js_timers[i].fn = fn; js_timers[i].due = js_now() + (u32)ms; js_timers[i].every = repeat ? (u32)ms : 0; js_timers[i].active = 1;
        return js_timers[i].id;
    }
    return 0;
}

/* ---- helpers for the natives ---- */
static int js_clampi(jv v, int len, int def) {               /* slice-style index: negatives count from the end */
    if (v.t == JT_UNDEF) return def;
    int x = js_tonum(v);
    if (x == JS_NAN) return 0;
    if (x == JS_PINF) return len; if (x == JS_NINF) return 0;
    if (x < 0) { x += len; if (x < 0) x = 0; }
    return x > len ? len : x;
}
static int js_find(u32 hay, u32 needle, u32 from_b, int last) {          /* byte index or -1 */
    u32 hl = js_sl(hay), nl = js_sl(needle);
    if (nl > hl) return -1;
    if (!last) { for (u32 i = from_b; i + nl <= hl; i++) { u32 k = 0; while (k < nl && js_cp[js_strs[hay].off + i + k] == js_cp[js_strs[needle].off + k]) k++; if (k == nl) return (int)i; } }
    else { for (u32 i = hl - nl + 1; i-- > 0;) { u32 k = 0; while (k < nl && js_cp[js_strs[hay].off + i + k] == js_cp[js_strs[needle].off + k]) k++; if (k == nl) return (int)i; } }
    return -1;
}
static u32 js_sb_n;                                           /* string builder over js_tmp (JSON.stringify) */
static void js_sb_put(const char *p, u32 n) { if (js_sb_n + n >= JS_SCRATCH) { js_abort("JSON too big"); return; } for (u32 i = 0; i < n; i++) js_tmp[js_sb_n++] = p[i]; }
static void js_json_str(u32 s) {
    js_sb_put("\"", 1);
    for (u32 i = 0; i < js_sl(s) && !js_thr; i++) {
        char c = js_sp(s)[i];
        if (c == '"' || c == '\\') { char e[2] = { '\\', c }; js_sb_put(e, 2); }
        else if (c == '\n') js_sb_put("\\n", 2); else if (c == '\t') js_sb_put("\\t", 2); else if (c == '\r') js_sb_put("\\r", 2);
        else if ((u8)c < 0x20) js_sb_put("?", 1); else js_sb_put(&c, 1);
    }
    js_sb_put("\"", 1);
}
static void js_json(jv v, int depth) {
    if (js_thr) return;
    if (depth > 12) { js_sb_put("null", 4); return; }
    switch (v.t) {
        case JT_NULL: js_sb_put("null", 4); return;
        case JT_BOOL: js_sb_put(v.v ? "true" : "false", v.v ? 4 : 5); return;
        case JT_NUM: if (js_isnum_special(v.v)) js_sb_put("null", 4); else { u32 s = js_num_str(v.v); js_sb_put(js_sp(s), js_sl(s)); } return;
        case JT_STR: js_json_str((u32)v.v); return;
        case JT_OBJ: {
            jobj *o = &js_objs[v.v];
            if (o->kind == JK_ARR) {
                js_sb_put("[", 1);
                for (u32 i = 0; i < js_objs[v.v].pn && !js_thr; i++) { if (i) js_sb_put(",", 1); jv e = js_pv(&js_objs[v.v])[i]; if (e.t == JT_UNDEF || js_callable(e)) js_sb_put("null", 4); else js_json(e, depth + 1); }
                js_sb_put("]", 1); return;
            }
            if (o->kind == JK_OBJ || o->kind == JK_ENV) {
                js_sb_put("{", 1); int first = 1;
                for (u32 i = 0; i < js_objs[v.v].pn && !js_thr; i++) {
                    jv *p = js_pv(&js_objs[v.v]); jv val = p[2 * i + 1];
                    if (val.t == JT_UNDEF || js_callable(val)) continue;
                    if (!first) js_sb_put(",", 1);
                    first = 0; js_json_str((u32)p[2 * i].v); js_sb_put(":", 1); js_json(val, depth + 1);
                }
                js_sb_put("}", 1); return;
            }
            js_sb_put("{}", 2); return;
        }
        default: js_sb_put("null", 4); return;
    }
}
static u32 js_stringify(jv v) {
    js_sb_n = 0; js_json(v, 0); if (js_thr) return 0;
    return js_str_new(js_tmp, js_sb_n, 0);
}
static u32 js_rng = 2463534242u;
static void (*js_hook_alert)(const char *msg);

static jv js_str_split(u32 s, jv sep) {
    u32 arr = js_arr_new(); if (js_thr) return JS_UNDEF;
    if (sep.t == JT_UNDEF) { js_arr_push(arr, JS_STRV(s)); return JS_OBJV(arr); }
    u32 sp = js_tostr(sep);
    if (js_sl(sp) == 0) { u32 cl = js_cplen(s); for (u32 i = 0; i < cl && !js_thr; i++) js_arr_push(arr, JS_STRV(js_substr(s, i, i + 1))); return JS_OBJV(arr); }
    u32 from = 0;
    for (;;) {
        int at = js_find(s, sp, from, 0);
        u32 end = at < 0 ? js_sl(s) : (u32)at;
        js_arr_push(arr, JS_STRV(js_str_new(js_sp(s) + from, end - from, 0)));
        if (js_thr || at < 0) break;
        from = (u32)at + js_sl(sp);
    }
    return JS_OBJV(arr);
}
static jv js_str_replace(u32 s, jv pat, jv rep, int all) {
    u32 p = js_tostr(pat), acc = 0, from = 0;
    if (js_sl(p) == 0) return JS_STRV(s);
    for (;;) {
        int at = js_find(s, p, from, 0);
        if (at < 0) break;
        acc = js_str_cat(acc, js_str_new(js_sp(s) + from, (u32)at - from, 0));
        if (js_callable(rep)) { jv av[1] = { JS_STRV(p) }; jv r = js_call(rep, JS_UNDEF, av, 1); if (js_thr) return JS_UNDEF; acc = js_str_cat(acc, js_tostr(r)); }
        else acc = js_str_cat(acc, js_tostr(rep));
        if (js_thr) return JS_UNDEF;
        from = (u32)at + js_sl(p);
        if (!all) break;
    }
    acc = js_str_cat(acc, js_str_new(js_sp(s) + from, js_sl(s) - from, 0));
    return JS_STRV(acc);
}
static jv js_sort(u32 arr, jv cmp) {                           /* shell sort; element access goes through ids so callbacks can't invalidate anything */
    int n = (int)js_objs[arr].pn;
    for (int gap = n / 2; gap > 0; gap /= 2)
        for (int i = gap; i < n; i++) {
            jv t = js_pv(&js_objs[arr])[i]; int j = i;
            while (j >= gap && !js_thr) {
                jv u = js_pv(&js_objs[arr])[j - gap]; int gt;
                if (js_callable(cmp)) { jv av[2] = { u, t }; jv r = js_call(cmp, JS_UNDEF, av, 2); gt = js_tonum(r) > 0 && js_tonum(r) != JS_NAN; }
                else { int un; jv su = JS_STRV(js_tostr(u)), st = JS_STRV(js_tostr(t)); gt = js_cmp_lt(st, su, &un); }
                if (js_thr || !gt) break;
                js_pv(&js_objs[arr])[j] = u; j -= gap;
            }
            if (js_thr) return JS_UNDEF;
            js_pv(&js_objs[arr])[j] = t;
        }
    return JS_OBJV(arr);
}
static jv js_parse_json(u32 text) {
    u32 wrapped = js_str_cat(js_str_cat(js_str_c("("), text), js_str_c(")"));
    if (js_thr) return JS_UNDEF;
    u32 nast = js_nast; const char *s0 = js_src; u32 l0 = js_len, p0 = js_pos, ln0 = js_line; jtok t0 = js_t; int e0 = js_perr;
    u32 head = js_parse_program(js_sp(wrapped), js_sl(wrapped));
    int bad = js_perr || !head;
    js_src = s0; js_len = l0; js_pos = p0; js_line = ln0; js_t = t0; js_perr = e0;
    if (bad) { js_nast = nast; return js_throw("SyntaxError", "Unexpected token in JSON"); }
    jv r = js_eval((u32)js_ast[head].a);
    js_nast = nast;
    return r;
}

static jv js_native(u32 id, jv thisv, jv *a, int n) {
    jv out;
    if (id >= 1000) { if (js_hook_call && js_hook_call(id, thisv, a, n, &out)) return out; return JS_UNDEF; }
    if (js_tick()) return JS_UNDEF;
    switch (id) {
        case JN_PARSEINT: {
            u32 s = js_tostr(ARG(0)); const char *p = js_sp(s); u32 l = js_sl(s), i = 0; int radix = ARG(1).t == JT_UNDEF ? 10 : js_tonum(ARG(1)), neg = 0;
            while (i < l && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n')) i++;
            if (i < l && (p[i] == '-' || p[i] == '+')) { neg = p[i] == '-'; i++; }
            if ((radix == 16 || radix == 10 || radix == 0) && i + 1 < l && p[i] == '0' && (p[i + 1] | 32) == 'x' && radix != 10) { radix = 16; i += 2; }
            if (radix == 0 || radix == JS_NAN) radix = 10;
            if (radix < 2 || radix > 36) return JS_NUM(JS_NAN);
            long long v = 0; int any = 0;
            for (; i < l; i++) {
                int d = p[i] >= '0' && p[i] <= '9' ? p[i] - '0' : (p[i] | 32) >= 'a' && (p[i] | 32) <= 'z' ? (p[i] | 32) - 'a' + 10 : 99;
                if (d >= radix) break;
                v = v * radix + d; any = 1; if (v > 2147483646LL) { v = 2147483647LL; }
            }
            if (!any) return JS_NUM(JS_NAN);
            if (v > 2147483646LL) return JS_NUM(neg ? JS_NINF : JS_PINF);
            return JS_NUM(neg ? -(int)v : (int)v);
        }
        case JN_ISNAN: return JS_BOOL(js_tonum(ARG(0)) == JS_NAN);
        case JN_ISFINITE: { int x = js_tonum(ARG(0)); return JS_BOOL(!js_isnum_special(x)); }
        case JN_ISINTEGER: return JS_BOOL(ARG(0).t == JT_NUM && !js_isnum_special(ARG(0).v));
        case JN_STRING: return JS_STRV(n ? js_tostr(a[0]) : 0);
        case JN_NUMBER: return JS_NUM(n ? js_tonum(a[0]) : 0);
        case JN_BOOLEAN: return JS_BOOL(js_truthy(ARG(0)));
        case JN_ERROR: case JN_TYPEERROR: case JN_RANGEERROR: {
            u32 m = ARG(0).t == JT_UNDEF ? 0 : js_tostr(ARG(0));
            return js_newerr(id == JN_ERROR ? "Error" : id == JN_TYPEERROR ? "TypeError" : "RangeError", js_sp(m));
        }
        case JN_ALERT: { u32 s = js_tostr(ARG(0)); if (js_hook_alert) js_hook_alert(js_sp(s)); else js_log(js_sp(s)); return JS_UNDEF; }
        case JN_CONFIRM: { u32 s = js_tostr(ARG(0)); if (js_hook_alert) js_hook_alert(js_sp(s)); return JS_BOOL(1); }
        case JN_PROMPT: return JS_NULL;
        case JN_SETTIMEOUT: case JN_SETINTERVAL: {
            if (!js_callable(ARG(0))) return js_throw("TypeError", "timer callback is not a function");
            int ms = js_tonum(ARG(1)); if (ms == JS_NAN || ms < 0) ms = 0; if (ms > 86400000) ms = 86400000;
            return JS_NUM(js_timer_add(a[0], ms, id == JN_SETINTERVAL));
        }
        case JN_CLEARTIMER: { int t = js_tonum(ARG(0)); for (int i = 0; i < JS_MAX_TIMERS; i++) if (js_timers[i].active && js_timers[i].id == t) js_timers[i].active = 0; return JS_UNDEF; }
        case JN_LOG: {
            for (int i = 0; i < n; i++) {
                u32 s = (a[i].t == JT_OBJ && js_objs[a[i].v].kind != JK_ELEM && js_objs[a[i].v].kind != JK_FUNC && !(js_objs[a[i].v].kind == JK_OBJ && js_prop_find((u32)a[i].v, js_s_message) >= 0)) ? js_stringify(a[i]) : js_tostr(a[i]);
                if (js_thr) return JS_UNDEF;
                if (i == 0) JS_LOG("[console] "); else JS_LOG(" ");
                JS_LOG(js_sp(s));
            }
            JS_LOG("\n"); return JS_UNDEF;
        }
        case JN_FLOOR: case JN_CEIL: case JN_ROUND: case JN_TRUNC: return JS_NUM(js_tonum(ARG(0)));      /* integers are already as floored as they get */
        case JN_ABS: { int x = js_tonum(ARG(0)); if (x == JS_NAN) return JS_NUM(x); if (x == JS_NINF) return JS_NUM(JS_PINF); return JS_NUM(x < 0 ? -x : x); }
        case JN_SIGN: { int x = js_tonum(ARG(0)); if (x == JS_NAN) return JS_NUM(x); return JS_NUM(x > 0 ? 1 : x < 0 ? -1 : 0); }
        case JN_MAX: case JN_MIN: {
            int best = id == JN_MAX ? JS_NINF : JS_PINF;
            for (int i = 0; i < n; i++) { int x = js_tonum(a[i]); if (x == JS_NAN) return JS_NUM(JS_NAN); if (id == JN_MAX ? x > best : x < best) best = x; }
            return JS_NUM(best);
        }
        case JN_POW: return js_arith(P_POW, JS_NUM(js_tonum(ARG(0))), JS_NUM(js_tonum(ARG(1))));
        case JN_SQRT: { int x = js_tonum(ARG(0)); if (x == JS_NAN || x < 0) return JS_NUM(JS_NAN); if (x == JS_PINF) return JS_NUM(x); int r = 0; while ((long long)(r + 1) * (r + 1) <= x) r++; return JS_NUM(r); }
        case JN_RANDOM: { js_rng ^= js_rng << 13; js_rng ^= js_rng >> 17; js_rng ^= js_rng << 5; js_rng += js_now(); return JS_NUM((int)(js_rng % 1000000u)); }  /* 0..999999: no fractions, but it IS random-ish */
        case JN_NOW: return JS_NUM((int)(js_now() & 0x7FFFFFFF));
        case JN_KEYS: case JN_VALUES: case JN_ENTRIES: {
            jv o = ARG(0); u32 arr = js_arr_new(); if (js_thr) return JS_UNDEF;
            if (o.t == JT_OBJ && js_objs[o.v].kind == JK_ARR) {
                for (u32 i = 0; i < js_objs[o.v].pn && !js_thr; i++) js_arr_push(arr, id == JN_KEYS ? JS_STRV(js_num_str((int)i)) : js_pv(&js_objs[o.v])[i]);
            } else if (o.t == JT_OBJ && js_objs[o.v].kind != JK_ELEM) {
                for (u32 i = 0; i < js_objs[o.v].pn && !js_thr; i++) {
                    jv *p = js_pv(&js_objs[o.v]); jv k = JS_STRV((u32)p[2 * i].v), v = p[2 * i + 1];
                    if (id == JN_KEYS) js_arr_push(arr, k);
                    else if (id == JN_VALUES) js_arr_push(arr, v);
                    else { u32 pr = js_arr_new(); if (js_thr) break; js_arr_push(pr, k); js_arr_push(pr, js_pv(&js_objs[o.v])[2 * i + 1]); js_arr_push(arr, JS_OBJV(pr)); }
                }
            }
            return JS_OBJV(arr);
        }
        case JN_ASSIGN: {
            jv t = ARG(0); if (t.t != JT_OBJ) return t;
            for (int k = 1; k < n; k++) if (a[k].t == JT_OBJ && js_objs[a[k].v].kind == JK_OBJ)
                for (u32 i = 0; i < js_objs[a[k].v].pn; i++) { jv *p = js_pv(&js_objs[a[k].v]); js_put(t, JS_STRV((u32)p[2 * i].v), p[2 * i + 1]); }
            return t;
        }
        case JN_HASOWN: { if (thisv.t != JT_OBJ) return JS_BOOL(0); u32 k = js_tostr(ARG(0)); if (js_objs[thisv.v].kind == JK_ARR) { int i = js_str_num(k); return JS_BOOL(i >= 0 && (u32)i < js_objs[thisv.v].pn); } return JS_BOOL(js_prop_find((u32)thisv.v, k) >= 0); }
        case JN_ISARRAY: return JS_BOOL(ARG(0).t == JT_OBJ && js_objs[a[0].v].kind == JK_ARR);
        case JN_STRINGIFY: { u32 s = js_stringify(ARG(0)); if (js_thr) return JS_UNDEF; return JS_STRV(s); }
        case JN_PARSE: return js_parse_json(js_tostr(ARG(0)));
        case JN_FROMCHARCODE: { char b[48]; u32 k = 0; for (int i = 0; i < n && k < 40; i++) { int c = js_tonum(a[i]); if (c < 0 || c == JS_NAN) c = 0; k += js_utf8_enc((u32)c, b + k); } return JS_STRV(js_str_new(b, k, 0)); }
        case JN_VALUEOF: return thisv;
        case JN_TOSTRING: {
            if (thisv.t == JT_NUM && n > 0) return JS_STRV(js_num_radix(thisv.v, js_tonum(a[0])));
            return JS_STRV(js_tostr(thisv));
        }
        case JN_TOFIXED: {
            int d = js_tonum(ARG(0)); if (d == JS_NAN || d < 0) d = 0; if (d > 20) d = 20;
            u32 s = js_num_str(js_tonum(thisv)); if (js_isnum_special(thisv.v) || d == 0) return JS_STRV(s);
            char b[48]; u32 k = 0; for (u32 i = 0; i < js_sl(s) && k < 20; i++) b[k++] = js_sp(s)[i];
            b[k++] = '.'; for (int i = 0; i < d; i++) b[k++] = '0';
            return JS_STRV(js_str_new(b, k, 0));
        }
        default: break;
    }
    /* ---- everything below is a method on a string, an array, or both ---- */
    int isarr = thisv.t == JT_OBJ && js_objs[thisv.v].kind == JK_ARR;
    if (isarr) {
        u32 arr = (u32)thisv.v;
        switch (id) {
            case JN_PUSH: for (int i = 0; i < n; i++) if (!js_arr_push(arr, a[i])) return JS_UNDEF; return JS_NUM((int)js_objs[arr].pn);
            case JN_POP: { jobj *o = &js_objs[arr]; if (!o->pn) return JS_UNDEF; return js_pv(o)[--o->pn]; }
            case JN_SHIFT: { jobj *o = &js_objs[arr]; if (!o->pn) return JS_UNDEF; jv f = js_pv(o)[0]; js_mcpy(js_pv(o), js_pv(o) + 1, (o->pn - 1) * (u32)sizeof(jv)); o->pn--; return f; }
            case JN_UNSHIFT: {
                if (!js_grow(arr, js_objs[arr].pn + (u32)n)) return JS_UNDEF;
                jobj *o = &js_objs[arr]; js_mcpy(js_pv(o) + n, js_pv(o), o->pn * (u32)sizeof(jv));
                for (int i = 0; i < n; i++) js_pv(o)[i] = a[i];
                o->pn += (u32)n; return JS_NUM((int)o->pn);
            }
            case JN_SPLICE: {
                int len = (int)js_objs[arr].pn, st = js_clampi(ARG(0), len, 0), del = n < 2 ? len - st : js_tonum(a[1]);
                if (n == 0) del = 0; if (del == JS_NAN || del < 0) del = 0; if (del > len - st) del = len - st;
                int ins = n > 2 ? n - 2 : 0;
                u32 rem = js_arr_new(); if (js_thr) return JS_UNDEF;
                for (int i = 0; i < del; i++) js_arr_push(rem, js_pv(&js_objs[arr])[st + i]);
                if (!js_grow(arr, (u32)(len - del + ins))) return JS_UNDEF;
                jobj *o = &js_objs[arr]; jv *p = js_pv(o);
                js_mcpy(p + st + ins, p + st + del, (u32)(len - st - del) * (u32)sizeof(jv));
                for (int i = 0; i < ins; i++) p[st + i] = a[2 + i];
                o->pn = (u32)(len - del + ins);
                return JS_OBJV(rem);
            }
            case JN_REVERSE: { jobj *o = &js_objs[arr]; jv *p = js_pv(o); for (u32 i = 0; i < o->pn / 2; i++) { jv t = p[i]; p[i] = p[o->pn - 1 - i]; p[o->pn - 1 - i] = t; } return thisv; }
            case JN_JOIN: { u32 sep = ARG(0).t == JT_UNDEF ? js_str_c(",") : js_tostr(a[0]); u32 r = js_arr_join(arr, sep); if (js_thr) return JS_UNDEF; return JS_STRV(r); }
            case JN_INDEXOF: case JN_INCLUDES: {
                for (u32 i = 0; i < js_objs[arr].pn; i++) if (js_strict_eq(js_pv(&js_objs[arr])[i], ARG(0))) return id == JN_INDEXOF ? JS_NUM((int)i) : JS_BOOL(1);
                return id == JN_INDEXOF ? JS_NUM(-1) : JS_BOOL(0);
            }
            case JN_LASTINDEXOF: { for (u32 i = js_objs[arr].pn; i-- > 0;) if (js_strict_eq(js_pv(&js_objs[arr])[i], ARG(0))) return JS_NUM((int)i); return JS_NUM(-1); }
            case JN_SLICE: {
                int len = (int)js_objs[arr].pn, s = js_clampi(ARG(0), len, 0), e = js_clampi(ARG(1), len, len);
                u32 r = js_arr_new(); if (js_thr) return JS_UNDEF;
                for (int i = s; i < e; i++) js_arr_push(r, js_pv(&js_objs[arr])[i]);
                return JS_OBJV(r);
            }
            case JN_CONCAT: {
                u32 r = js_arr_new(); if (js_thr) return JS_UNDEF;
                for (u32 i = 0; i < js_objs[arr].pn; i++) js_arr_push(r, js_pv(&js_objs[arr])[i]);
                for (int k = 0; k < n; k++) {
                    if (a[k].t == JT_OBJ && js_objs[a[k].v].kind == JK_ARR) { for (u32 i = 0; i < js_objs[a[k].v].pn; i++) js_arr_push(r, js_pv(&js_objs[a[k].v])[i]); }
                    else js_arr_push(r, a[k]);
                }
                return JS_OBJV(r);
            }
            case JN_AT: { int i = js_tonum(ARG(0)); int len = (int)js_objs[arr].pn; if (i < 0) i += len; return i >= 0 && i < len ? js_pv(&js_objs[arr])[i] : JS_UNDEF; }
            case JN_FILL: { int len = (int)js_objs[arr].pn, s = js_clampi(ARG(1), len, 0), e = js_clampi(ARG(2), len, len); for (int i = s; i < e; i++) js_pv(&js_objs[arr])[i] = ARG(0); return thisv; }
            case JN_SORT: return js_sort(arr, ARG(0));
            case JN_FOREACH: case JN_MAP: case JN_FILTER: case JN_FIND: case JN_FINDINDEX: case JN_SOME: case JN_EVERY: case JN_REDUCE: {
                jv fn = ARG(0);
                if (!js_callable(fn)) return js_throw("TypeError", "callback is not a function");
                u32 res = (id == JN_MAP || id == JN_FILTER) ? js_arr_new() : 0; if (js_thr) return JS_UNDEF;
                jv acc = JS_UNDEF; u32 i = 0;
                if (id == JN_REDUCE) {
                    if (n >= 2) acc = a[1]; else { if (!js_objs[arr].pn) return js_throw("TypeError", "Reduce of empty array with no initial value"); acc = js_pv(&js_objs[arr])[0]; i = 1; }
                }
                for (; i < js_objs[arr].pn; i++) {
                    jv e = js_pv(&js_objs[arr])[i]; jv av[4]; int ac;
                    if (id == JN_REDUCE) { av[0] = acc; av[1] = e; av[2] = JS_NUM((int)i); av[3] = thisv; ac = 4; } else { av[0] = e; av[1] = JS_NUM((int)i); av[2] = thisv; ac = 3; }
                    jv r = js_call(fn, ARG(1), av, ac); if (js_thr) return JS_UNDEF;
                    switch (id) {
                        case JN_MAP: js_arr_push(res, r); break;
                        case JN_FILTER: if (js_truthy(r)) js_arr_push(res, e); break;
                        case JN_FIND: if (js_truthy(r)) return e; break;
                        case JN_FINDINDEX: if (js_truthy(r)) return JS_NUM((int)i); break;
                        case JN_SOME: if (js_truthy(r)) return JS_BOOL(1); break;
                        case JN_EVERY: if (!js_truthy(r)) return JS_BOOL(0); break;
                        case JN_REDUCE: acc = r; break;
                        default: break;
                    }
                }
                switch (id) { case JN_MAP: case JN_FILTER: return JS_OBJV(res); case JN_FINDINDEX: return JS_NUM(-1); case JN_SOME: return JS_BOOL(0); case JN_EVERY: return JS_BOOL(1); case JN_REDUCE: return acc; default: return JS_UNDEF; }
            }
            default: break;
        }
        return js_throw("TypeError", "not a function on arrays");
    }
    if (thisv.t == JT_STR || (thisv.t == JT_NUM && id == JN_TOSTRING)) {
        u32 s = js_tostr(thisv); u32 cl = js_cplen(s);
        switch (id) {
            case JN_CHARAT: { int i = js_tonum(ARG(0)); if (i == JS_NAN) i = 0; if (i < 0 || (u32)i >= cl) return JS_STRV(0); return JS_STRV(js_substr(s, (u32)i, (u32)i + 1)); }
            case JN_AT: { int i = js_tonum(ARG(0)); if (i < 0) i += (int)cl; if (i < 0 || (u32)i >= cl) return JS_UNDEF; return JS_STRV(js_substr(s, (u32)i, (u32)i + 1)); }
            case JN_CHARCODEAT: { int i = js_tonum(ARG(0)); if (i == JS_NAN) i = 0; if (i < 0 || (u32)i >= cl) return JS_NUM(JS_NAN); u32 cp; js_utf8_dec((const u8 *)js_sp(s) + js_cpoff(s, (u32)i), js_sl(s) - js_cpoff(s, (u32)i), &cp); return JS_NUM((int)cp); }
            case JN_INDEXOF: case JN_INCLUDES: {
                u32 nd = js_tostr(ARG(0)); int from = js_clampi(ARG(1), (int)cl, 0);
                int at = js_find(s, nd, js_cpoff(s, (u32)from), 0);
                if (id == JN_INCLUDES) return JS_BOOL(at >= 0);
                return JS_NUM(at < 0 ? -1 : (int)js_byte2cp(s, (u32)at));
            }
            case JN_LASTINDEXOF: { int at = js_find(s, js_tostr(ARG(0)), 0, 1); return JS_NUM(at < 0 ? -1 : (int)js_byte2cp(s, (u32)at)); }
            case JN_STARTSWITH: { u32 nd = js_tostr(ARG(0)); return JS_BOOL(js_sl(nd) <= js_sl(s) && js_find(s, nd, 0, 0) == 0); }
            case JN_ENDSWITH: { u32 nd = js_tostr(ARG(0)); if (js_sl(nd) > js_sl(s)) return JS_BOOL(0); u32 st = js_sl(s) - js_sl(nd); int at = js_find(s, nd, st, 0); return JS_BOOL(at == (int)st); }
            case JN_SLICE: { int st = js_clampi(ARG(0), (int)cl, 0), e = js_clampi(ARG(1), (int)cl, (int)cl); return JS_STRV(e > st ? js_substr(s, (u32)st, (u32)e) : 0); }
            case JN_SUBSTRING: {
                int st = ARG(0).t == JT_UNDEF ? 0 : js_tonum(a[0]), e = ARG(1).t == JT_UNDEF ? (int)cl : js_tonum(a[1]);
                if (st == JS_NAN || st < 0) st = 0; if (e == JS_NAN || e < 0) e = 0; if (st > (int)cl) st = (int)cl; if (e > (int)cl) e = (int)cl;
                if (st > e) { int t = st; st = e; e = t; }
                return JS_STRV(js_substr(s, (u32)st, (u32)e));
            }
            case JN_SUBSTR: { int st = js_clampi(ARG(0), (int)cl, 0), len = ARG(1).t == JT_UNDEF ? (int)cl : js_tonum(a[1]); if (len == JS_NAN || len < 0) len = 0; if (len > (int)cl - st) len = (int)cl - st; return JS_STRV(js_substr(s, (u32)st, (u32)(st + len))); }
            case JN_TOUPPER: case JN_TOLOWER: {
                u32 r = js_str_new(js_sp(s), js_sl(s), 0); if (js_thr || !r) return JS_STRV(r);
                char *p = js_cp + js_strs[r].off;
                for (u32 i = 0; i < js_sl(r); i++) { if (id == JN_TOUPPER && p[i] >= 'a' && p[i] <= 'z') p[i] = (char)(p[i] - 32); else if (id == JN_TOLOWER && p[i] >= 'A' && p[i] <= 'Z') p[i] = (char)(p[i] + 32); }
                js_strs[r].hash = js_hash(p, js_sl(r));
                return JS_STRV(r);
            }
            case JN_TRIM: {
                const char *p = js_sp(s); u32 b = 0, e = js_sl(s);
                while (b < e && (p[b] == ' ' || p[b] == '\t' || p[b] == '\n' || p[b] == '\r')) b++;
                while (e > b && (p[e - 1] == ' ' || p[e - 1] == '\t' || p[e - 1] == '\n' || p[e - 1] == '\r')) e--;
                return JS_STRV(js_str_new(p + b, e - b, 0));
            }
            case JN_SPLIT: return js_str_split(s, ARG(0));
            case JN_REPLACE: return js_str_replace(s, ARG(0), ARG(1), 0);
            case JN_REPLACEALL: return js_str_replace(s, ARG(0), ARG(1), 1);
            case JN_REPEAT: { int k = js_tonum(ARG(0)); if (k < 0 || k == JS_NAN || (long long)k * js_sl(s) > 60000) return js_throw("RangeError", "Invalid count value"); u32 acc = 0; for (int i = 0; i < k && !js_thr; i++) acc = js_str_cat(acc, s); return JS_STRV(acc); }
            case JN_PADSTART: case JN_PADEND: {
                int want = js_tonum(ARG(0)); u32 pad = ARG(1).t == JT_UNDEF ? js_str_c(" ") : js_tostr(a[1]);
                if (want == JS_NAN || (u32)want <= cl || !js_sl(pad) || want > 60000) return JS_STRV(s);
                u32 fill = 0, pc = js_cplen(pad), need = (u32)want - cl;
                while (js_cplen(fill) + pc <= need && !js_thr) fill = js_str_cat(fill, pad);
                if (js_cplen(fill) < need) fill = js_str_cat(fill, js_substr(pad, 0, need - js_cplen(fill)));
                return JS_STRV(id == JN_PADSTART ? js_str_cat(fill, s) : js_str_cat(s, fill));
            }
            case JN_CONCAT: { u32 acc = s; for (int i = 0; i < n && !js_thr; i++) acc = js_str_cat(acc, js_tostr(a[i])); return JS_STRV(acc); }
            case JN_TOSTRING: return JS_STRV(s);
            default: break;
        }
    }
    if (thisv.t == JT_UNDEF && id == JN_TOSTRING) return JS_STRV(js_str_c("undefined"));
    return js_throw("TypeError", "method called on the wrong kind of value");
}

/* ============================================================ globals & the public API ============================================================ */
static void js_defn(u32 obj, const char *name, u32 id) { js_prop_set(obj, js_str_new(name, js_slen(name), 1), JV(JT_NAT, (int)id)); }
static u32 js_defo(u32 obj, const char *name) { u32 o = js_obj_new(JK_OBJ, 0, 0); if (o) js_prop_set(obj, js_str_new(name, js_slen(name), 1), JS_OBJV(o)); return o; }
static void js_install_builtins(void) {
    js_s_this = js_str_new("this", 4, 1); js_s_length = js_str_new("length", 6, 1); js_s_message = js_str_new("message", 7, 1); js_s_name = js_str_new("name", 4, 1);
    js_genv = js_env_new(0); js_env = js_genv;
    js_this_global = JS_OBJV(js_genv);
    js_prop_set(js_genv, js_s_this, js_this_global);
    js_prop_set(js_genv, js_str_new("window", 6, 1), js_this_global);
    js_prop_set(js_genv, js_str_new("self", 4, 1), js_this_global);
    js_prop_set(js_genv, js_str_new("globalThis", 10, 1), js_this_global);
    js_prop_set(js_genv, js_str_new("undefined", 9, 1), JS_UNDEF);
    js_prop_set(js_genv, js_str_new("NaN", 3, 1), JS_NUM(JS_NAN));
    js_prop_set(js_genv, js_str_new("Infinity", 8, 1), JS_NUM(JS_PINF));
    static const struct { const char *n; u32 id; } globals[] = {
        {"parseInt", JN_PARSEINT}, {"parseFloat", JN_PARSEINT}, {"isNaN", JN_ISNAN}, {"isFinite", JN_ISFINITE}, {"String", JN_STRING}, {"Number", JN_NUMBER}, {"Boolean", JN_BOOLEAN},
        {"Error", JN_ERROR}, {"TypeError", JN_TYPEERROR}, {"RangeError", JN_RANGEERROR}, {"alert", JN_ALERT}, {"confirm", JN_CONFIRM}, {"prompt", JN_PROMPT},
        {"setTimeout", JN_SETTIMEOUT}, {"setInterval", JN_SETINTERVAL}, {"clearTimeout", JN_CLEARTIMER}, {"clearInterval", JN_CLEARTIMER}, {0, 0}
    };
    for (int i = 0; globals[i].n; i++) js_defn(js_genv, globals[i].n, globals[i].id);
    u32 o = js_defo(js_genv, "console");
    static const char *const cn[] = { "log", "info", "warn", "error", "debug", 0 };
    for (int i = 0; cn[i]; i++) js_defn(o, cn[i], JN_LOG);
    o = js_defo(js_genv, "Math");
    static const struct { const char *n; u32 id; } mt[] = { {"floor", JN_FLOOR}, {"ceil", JN_CEIL}, {"round", JN_ROUND}, {"trunc", JN_TRUNC}, {"abs", JN_ABS}, {"max", JN_MAX}, {"min", JN_MIN},
        {"pow", JN_POW}, {"sqrt", JN_SQRT}, {"random", JN_RANDOM}, {"sign", JN_SIGN}, {0, 0} };
    for (int i = 0; mt[i].n; i++) js_defn(o, mt[i].n, mt[i].id);
    o = js_defo(js_genv, "JSON"); js_defn(o, "stringify", JN_STRINGIFY); js_defn(o, "parse", JN_PARSE);
    o = js_defo(js_genv, "Object"); js_defn(o, "keys", JN_KEYS); js_defn(o, "values", JN_VALUES); js_defn(o, "entries", JN_ENTRIES); js_defn(o, "assign", JN_ASSIGN);
    o = js_defo(js_genv, "Array"); js_defn(o, "isArray", JN_ISARRAY);
    o = js_defo(js_genv, "Date"); js_defn(o, "now", JN_NOW);
    js_defn(js_genv, "isInteger", JN_ISINTEGER);
}
static int js_begin(void) {                                    /* a new page: fresh heap, fresh globals. 0 = scripts are unavailable. */
    if (!js_init_tables()) return 0;
    js_heap_reset();
    for (int i = 0; i < JS_MAX_TIMERS; i++) js_timers[i].active = 0;
    js_install_builtins();
    return !js_thr;
}
static void js_report(void) {                                  /* an exception escaped to the top: say so, then forget it */
    if (js_thr == 1) {
        jv e = js_exc; js_thr = 0; js_exc = JS_UNDEF;
        u32 s = js_tostr(e); const char *p = js_sp(s); u32 n = 0, l = js_sl(s);
        const char *pre = "Uncaught "; while (*pre) js_msg[n++] = *pre++;
        for (u32 i = 0; i < l && n < sizeof(js_msg) - 1; i++) js_msg[n++] = p[i];
        js_msg[n] = 0;
    }
    js_thr = 0; js_exc = JS_UNDEF;
    if (js_msg[0]) { js_log(js_msg); }
}
static void js_top_enter(void) {
    js_stack_base = (const u32 *)__builtin_frame_address(0) + 8;
    js_steps = JS_STEPS; js_short = 0; js_thr = 0; js_depth = 0; js_msg[0] = 0; js_env = js_genv;
}
/* run a script's source text at global scope. returns 1 if it ran clean. */
static int js_run_source(const char *src, u32 len) {
    if (!js_ready || !js_genv) return 0;
    js_top_enter();
    js_stack_base = (const u32 *)__builtin_frame_address(0) + 8;
    u32 head = js_parse_program(src, len);
    if (js_perr) { js_log(js_msg); js_perr = 0; return 0; }
    jv r = JS_UNDEF;
    js_hoist_vars(head); js_hoist_fdecls(head);
    if (!js_thr) js_exec_list(head, &r);
    int ok = !js_thr;
    if (!ok) js_report();
    js_env = js_genv;
    return ok;
}
/* call a function value from outside (event handler, timer) */
static jv js_run_call(jv f, jv thisv, jv *args, int argc) {
    if (!js_ready || !js_genv) return JS_UNDEF;
    js_top_enter();
    jv av[JS_MAXARGS]; int n = argc > JS_MAXARGS ? JS_MAXARGS : argc;
    for (int i = 0; i < n; i++) av[i] = args[i];
    jv r = js_call(f, thisv, av, n);
    if (js_thr) { js_report(); r = JS_UNDEF; }
    js_env = js_genv;
    return r;
}
static void js_timers_run(void) {
    if (!js_ready) return;
    u32 now = js_now();
    for (int i = 0; i < JS_MAX_TIMERS; i++) {
        if (!js_timers[i].active || (int)(now - js_timers[i].due) < 0) continue;
        jv fn = js_timers[i].fn; int id = js_timers[i].id;
        if (js_timers[i].every) js_timers[i].due = now + js_timers[i].every; else js_timers[i].active = 0;
        js_run_call(fn, JS_UNDEF, 0, 0);
        (void)id;
    }
}
static int js_timers_pending(void) { for (int i = 0; i < JS_MAX_TIMERS; i++) if (js_timers[i].active) return 1; return 0; }
#pragma GCC diagnostic pop
#endif
