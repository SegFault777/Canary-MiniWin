/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef JS_DOM_H
#define JS_DOM_H
#include "js.h"
#include "netclock.h"

/* ============================================================
 * js_dom.h -- the glue between js.h (the language) and dom.h/render.h (the page).
 *
 * `document`, elements, text nodes, classList, style, events, timers, alert(). The model is deliberately small:
 *   - a JS "element" is a JK_ELEM object whose .a is the DOM node index; the same node can have many wrappers and
 *     ===  compares the node, not the wrapper. `flags` says what it is: 0 element, 1 classList, 2 style.
 *   - changing the page = editing the same node/attr/pool arrays the parser filled, then telling the layout engine
 *     (L.pool_mark moves up so the new text survives the next layout, rd_invalidate() forces that layout).
 *   - a handler that throws gets one line on the serial log and the page keeps living. We are not savages.
 *
 * Not here: external <script src> (the fetch is asynchronous and the page is long since parsed), dataset, getComputedStyle,
 * mouse/keyboard events other than click/input/change/submit/load, and layout queries (offsetWidth & friends).
 * ============================================================ */

#define JD_WINDOW 0x00FFFFFFu                       /* pseudo node id for window-level listeners (node 0 is the document) */
enum { JD_GETBYID = 1000, JD_QS, JD_QSA, JD_BYTAG, JD_BYCLASS, JD_CREATE, JD_CREATETEXT, JD_ADDL, JD_REML, JD_APPEND, JD_REMOVECHILD, JD_REMOVE,
       JD_INSERTB, JD_GETATTR, JD_SETATTR, JD_REMATTR, JD_HASATTR, JD_CLICK, JD_FOCUS, JD_BLUR, JD_PREVENT, JD_STOP, JD_CADD, JD_CREM, JD_CTOG,
       JD_CHAS, JD_WRITE, JD_SETPROP, JD_GETPROP, JD_REMPROP, JD_CONTAINS };
enum { JE_NONE, JE_CLICK, JE_INPUT, JE_CHANGE, JE_SUBMIT, JE_LOAD, JE_DCL };

static int  js_dom_active, js_dom_dirty, js_dom_loading;
static char js_alert_text[96]; static int js_alert_new;
static jv   js_dom_cur_ev;
static u32  js_dom_ms_cache, js_dom_mono_s, js_dom_last_s = 0xFFFFFFFFu, js_dom_pass, js_dom_prev_pass;
#define JD_MAXL 96
typedef struct { u32 node; u8 type, inl, used, pad; jv fn; } jlis;
static jlis js_lis[JD_MAXL];

/* ---------------------------------------------------------------- helpers */
static u32 js_dom_wrap_as(u32 node, u16 sub) { u32 o = js_obj_new(JK_ELEM, node, 0); if (o) js_objs[o].flags = sub; return o; }
static jv  js_dom_wrap(u32 node) { return JS_OBJV(js_dom_wrap_as(node, 0)); }
static int js_dom_nodeof(jv v, u32 *node) {
    if (v.t != JT_OBJ || js_objs[v.v].kind != JK_ELEM || js_objs[v.v].a >= dom_node_count) return 0;
    *node = js_objs[v.v].a; return 1;
}
static void js_dom_mut(void) { js_dom_dirty = 1; }
static void js_dom_commit(void) { if (js_dom_dirty) { L.pool_mark = dom_pool_len; rd_invalidate(); js_dom_dirty = 0; } }
static void js_dom_enter(void) { dom_pool_len = L.pool_mark; }          /* drop the layout engine's temporary marker text */
static u32 js_dom_ms(void) { return js_dom_ms_cache; }
static void js_dom_refresh_clock(void) {                                  /* 1 s RTC + main-loop passes between ticks = a millisecond-ish clock */
    u32 s = net_wall_seconds();
    js_dom_pass++;
    if (s != js_dom_last_s) {
        if (js_dom_last_s != 0xFFFFFFFFu) js_dom_mono_s += (s + 86400u - js_dom_last_s) % 86400u;
        js_dom_last_s = s; js_dom_prev_pass = js_dom_pass; js_dom_pass = 0;
    }
    u32 frac = js_dom_prev_pass ? (js_dom_pass * 1000u) / js_dom_prev_pass : 0;
    js_dom_ms_cache = js_dom_mono_s * 1000u + (frac > 999 ? 999 : frac);
}
static char js_lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int js_dom_ieq(const u8 *a, u32 al, const char *b, u32 bl) { if (al != bl) return 0; for (u32 i = 0; i < al; i++) if (js_lc((char)a[i]) != js_lc(b[i])) return 0; return 1; }

/* text -> pool, reduced to what the bitmap fonts can draw (the same mapping the HTML tokenizer uses) */
static int js_dom_put(const char *s, u32 n, u32 *off, u32 *len) {
    u32 start = dom_pool_len;
    for (u32 i = 0; i < n;) {
        u32 cp; i += js_utf8_dec((const u8 *)s + i, n - i, &cp);
        if (cp == '\r' || (cp < 0x20 && cp != '\n' && cp != '\t')) continue;
        u8 o[4]; u32 k = dom_map_cp(cp, o);
        if (dom_pool_len + k + 8 >= RD_POOL_SIZE) { dom_pool_len = start; return 0; }
        for (u32 j = 0; j < k; j++) RD_POOL[dom_pool_len++] = o[j];
    }
    *off = start; *len = dom_pool_len - start;
    return 1;
}
static int js_dom_set_attr(u32 node, u32 aid, const char *s, u32 n) {
    rd_node_t *e = &RD_NODES[node];
    if (e->kind != RDK_ELEM || !aid) return 0;
    u32 off, len; if (!js_dom_put(s, n, &off, &len)) { js_throw("RangeError", "page text pool is full"); return 0; }
    if (len > 65535) len = 65535;
    int at = dom_attr_find(node, aid);
    if (at < 0) {
        if (dom_attr_count + e->b + 1 > RD_ATTR_MAX) return 0;
        u32 first = dom_attr_count;
        for (u32 i = 0; i < e->b; i++) RD_ATTRS[first + i] = RD_ATTRS[e->a + i];
        at = (int)(first + e->b); RD_ATTRS[at].id = (u8)aid; RD_ATTRS[at].pad = 0;
        e->a = first; e->b++; dom_attr_count = first + e->b;
    }
    RD_ATTRS[at].voff = off; RD_ATTRS[at].vlen = (u16)len;
    js_dom_mut();
    return 1;
}
static void js_dom_del_attr(u32 node, u32 aid) { int at = dom_attr_find(node, aid); if (at >= 0) { RD_ATTRS[at].id = 0; js_dom_mut(); } }
static jv js_dom_attr_str(u32 node, u32 aid) {
    u32 l; const u8 *v = dom_attr(node, aid, &l);
    if (!v) return JS_STRV(0);
    return JS_STRV(js_str_new((const char *)v, l, 0));
}
static void js_dom_unlink(u32 c) {
    u32 p = RD_NODES[c].parent; if (!p && !RD_NODES[0].first) return;
    rd_node_t *pn = &RD_NODES[p];
    u32 prev = 0, cur = pn->first;
    while (cur && cur != c) { prev = cur; cur = RD_NODES[cur].next; }
    if (cur != c) return;
    if (prev) RD_NODES[prev].next = RD_NODES[c].next; else pn->first = RD_NODES[c].next;
    if (pn->last == c) pn->last = prev;
    RD_NODES[c].parent = 0; RD_NODES[c].next = 0;
    js_dom_mut();
}
static int js_dom_is_ancestor(u32 a, u32 n) { for (; n; n = RD_NODES[n].parent) if (n == a) return 1; return 0; }
static void js_dom_insert(u32 parent, u32 c, u32 before) {
    if (c == parent || js_dom_is_ancestor(c, parent)) { js_throw("Error", "cannot insert a node into itself"); return; }
    js_dom_unlink(c);
    rd_node_t *p = &RD_NODES[parent];
    if (!before || RD_NODES[before].parent != parent) { dom_append(parent, c); }
    else {
        u32 prev = 0, cur = p->first; while (cur && cur != before) { prev = cur; cur = RD_NODES[cur].next; }
        RD_NODES[c].parent = parent; RD_NODES[c].next = before;
        if (prev) RD_NODES[prev].next = c; else p->first = c;
    }
    js_dom_mut();
}
static void js_dom_set_text(u32 node, const char *s, u32 n) {
    rd_node_t *e = &RD_NODES[node];
    if (e->kind == RDK_TEXT) { u32 off, len; if (js_dom_put(s, n, &off, &len)) { e->a = off; e->b = len; js_dom_mut(); } return; }
    if (e->kind != RDK_ELEM && e->kind != RDK_DOC) return;
    e->first = e->last = 0;
    if (!n) { js_dom_mut(); return; }
    u32 off, len; if (!js_dom_put(s, n, &off, &len)) { js_throw("RangeError", "page text pool is full"); return; }
    u32 t = dom_new_node(RDK_TEXT, 0); if (!t) return;
    RD_NODES[t].a = off; RD_NODES[t].b = len; dom_append(node, t);
    js_dom_mut();
}
static void js_dom_collect(u32 node) {                                    /* textContent into the js_sb builder */
    for (u32 c = RD_NODES[node].first; c && !js_thr; c = RD_NODES[c].next) {
        if (RD_NODES[c].kind == RDK_TEXT) { for (u32 i = 0; i < RD_NODES[c].b; i++) { char ch = (char)RD_POOL[RD_NODES[c].a + i]; if (ch == 1) ch = ' '; js_sb_put(&ch, 1); } }
        else if (RD_NODES[c].kind == RDK_ELEM && RD_NODES[c].tag != TG_SCRIPT && RD_NODES[c].tag != TG_STYLE) js_dom_collect(c);
    }
}
static jv js_dom_text_of(u32 node) {
    if (RD_NODES[node].kind == RDK_TEXT) return JS_STRV(js_str_new((const char *)RD_POOL + RD_NODES[node].a, RD_NODES[node].b, 0));
    js_sb_n = 0; js_dom_collect(node); if (js_thr) return JS_UNDEF;
    return JS_STRV(js_str_new(js_tmp, js_sb_n, 0));
}
static void js_dom_set_html(u32 node, const char *s, u32 n) {
    RD_NODES[node].first = RD_NODES[node].last = 0;
    dom_parse_fragment(node, (const u8 *)s, n);
    js_dom_mut();
}

/* ---------------------------------------------------------------- selectors: tag #id .class [attr] [attr=v], descendant and > combinators, commas */
typedef struct { char tag[14], id[28], cls[28], attr[12], aval[24]; u8 hasattr, comb, bad; } jsel;
#define JD_SELMAX 6
static int js_sel_group(const char *s, u32 n, jsel *o) {
    int k = 0; u32 i = 0; char comb = ' ';
    while (i < n) {
        while (i < n && s[i] == ' ') i++;
        if (i < n && s[i] == '>') { comb = '>'; i++; continue; }
        if (i >= n) break;
        if (k >= JD_SELMAX) return -1;
        jsel *c = &o[k]; for (u32 q = 0; q < sizeof(*c); q++) ((char *)c)[q] = 0;
        c->comb = (u8)comb; comb = ' ';
        while (i < n && s[i] != ' ' && s[i] != '>') {
            char t = s[i]; u32 st = ++i, len;
            if (t == '*') continue;
            if (t == '#' || t == '.') {
                while (i < n && js_idc((u8)s[i]) && s[i] != '$') i++;
                len = i - st; char *d = t == '#' ? c->id : c->cls; u32 cap = 27, cur = js_slen(d);
                if (t == '.' && cur) { if (cur < cap) d[cur++] = ' '; }
                else cur = 0;
                for (u32 q = 0; q < len && cur < cap; q++) d[cur++] = s[st + q];
                d[cur] = 0; continue;
            }
            if (t == '[') {
                u32 e = st; while (e < n && s[e] != ']') e++;
                u32 q = st; u32 al = 0; while (q < e && s[q] != '=' && al < 11) c->attr[al++] = js_lc(s[q++]);
                c->hasattr = 1;
                if (q < e && s[q] == '=') { q++; if (q < e && (s[q] == '"' || s[q] == '\'')) q++; u32 vl = 0; while (q < e && s[q] != '"' && s[q] != '\'' && vl < 23) c->aval[vl++] = s[q++]; c->hasattr = 2; }
                i = e < n ? e + 1 : e; continue;
            }
            if (js_idc((u8)t)) { i = st - 1; while (i < n && (js_idc((u8)s[i]) || s[i] == '-')) i++; u32 l2 = i - (st - 1); for (u32 q = 0; q < l2 && q < 13; q++) c->tag[q] = js_lc(s[st - 1 + q]); continue; }
            return -1;                                                    /* :pseudo-classes and other fancy stuff: not today */
        }
        k++;
    }
    return k;
}
static int js_sel_one(u32 node, const jsel *c) {
    rd_node_t *e = &RD_NODES[node];
    if (e->kind != RDK_ELEM) return 0;
    if (c->tag[0] && !js_dom_ieq((const u8 *)rd_tag_names[e->tag], js_slen(rd_tag_names[e->tag]), c->tag, js_slen(c->tag))) return 0;
    u32 l;
    if (c->id[0]) { const u8 *v = dom_attr(node, AT_ID, &l); if (!v || l != js_slen(c->id)) return 0; for (u32 i = 0; i < l; i++) if (v[i] != (u8)c->id[i]) return 0; }
    if (c->cls[0]) {
        const u8 *v = dom_attr(node, AT_CLASS, &l); if (!v) return 0;
        u32 i = 0;
        while (c->cls[i]) {
            u32 j = i; while (c->cls[j] && c->cls[j] != ' ') j++;
            u32 cl = j - i; int found = 0;
            for (u32 a = 0; a < l && !found;) {
                while (a < l && v[a] == ' ') a++;
                u32 b = a; while (b < l && v[b] != ' ') b++;
                if (b - a == cl) { u32 q = 0; while (q < cl && v[a + q] == (u8)c->cls[i + q]) q++; found = q == cl; }
                a = b;
            }
            if (!found) return 0;
            i = c->cls[j] ? j + 1 : j;
        }
    }
    if (c->hasattr) {
        u32 aid = dom_attr_id((const u8 *)c->attr, js_slen(c->attr)); if (!aid) return 0;
        const u8 *v = dom_attr(node, aid, &l); if (!v) return 0;
        if (c->hasattr == 2) { if (l != js_slen(c->aval)) return 0; for (u32 i = 0; i < l; i++) if (v[i] != (u8)c->aval[i]) return 0; }
    }
    return 1;
}
static int js_sel_chain(u32 node, const jsel *p, int k) {
    if (!js_sel_one(node, &p[k])) return 0;
    if (k == 0) return 1;
    if (p[k].comb == '>') { u32 q = RD_NODES[node].parent; return q && js_sel_chain(q, p, k - 1); }
    for (u32 q = RD_NODES[node].parent; q; q = RD_NODES[q].parent) if (js_sel_chain(q, p, k - 1)) return 1;
    return 0;
}
static int js_sel_match(u32 node, const char *sel, u32 n) {
    jsel parts[JD_SELMAX]; u32 i = 0;
    while (i <= n) {
        u32 j = i; while (j < n && sel[j] != ',') j++;
        int k = js_sel_group(sel + i, j - i, parts);
        if (k > 0 && js_sel_chain(node, parts, k - 1)) return 1;
        i = j + 1;
    }
    return 0;
}
static jv js_dom_query(u32 root, jv selv, int all) {
    u32 s = js_tostr(selv); u32 arr = all ? js_arr_new() : 0; if (js_thr) return JS_UNDEF;
    char sel[128]; u32 n = js_sl(s); if (n > 127) n = 127; for (u32 i = 0; i < n; i++) sel[i] = js_sp(s)[i];
    for (u32 node = 1; node < dom_node_count; node++) {
        if (root && (node == root || !js_dom_is_ancestor(root, node))) continue;
        if (!js_sel_match(node, sel, n)) continue;
        if (!all) return js_dom_wrap(node);
        js_arr_push(arr, js_dom_wrap(node)); if (js_thr) return JS_UNDEF;
    }
    return all ? JS_OBJV(arr) : JS_NULL;
}

/* ---------------------------------------------------------------- style and classList */
static void js_dom_kebab(const char *s, u32 n, char *o, u32 cap) {
    u32 k = 0;
    if (n == 8 && s[0] == 'c' && s[1] == 's' && s[2] == 's' && s[3] == 'F') { s += 3; n = 5; o[k++] = 'f'; o[k++] = 'l'; o[k++] = 'o'; o[k++] = 'a'; o[k++] = 't'; o[k] = 0; return; }
    for (u32 i = 0; i < n && k + 2 < cap; i++) { if (s[i] >= 'A' && s[i] <= 'Z') { o[k++] = '-'; o[k++] = js_lc(s[i]); } else o[k++] = s[i]; }
    o[k] = 0;
}
static jv js_dom_style_get(u32 node, const char *prop) {
    u32 al, pl = js_slen(prop); const u8 *a = dom_attr(node, AT_STYLE, &al);
    for (u32 i = 0; a && i < al;) {
        u32 j = i; while (j < al && a[j] != ';') j++;
        u32 c = i; while (c < j && a[c] != ':') c++;
        u32 ps = i; while (ps < c && a[ps] == ' ') ps++; u32 pe = c; while (pe > ps && a[pe - 1] == ' ') pe--;
        if (c < j && js_dom_ieq(a + ps, pe - ps, prop, pl)) {
            u32 vs = c + 1; while (vs < j && a[vs] == ' ') vs++; u32 ve = j; while (ve > vs && a[ve - 1] == ' ') ve--;
            return JS_STRV(js_str_new((const char *)a + vs, ve - vs, 0));
        }
        i = j + 1;
    }
    return JS_STRV(0);
}
static void js_dom_style_set(u32 node, const char *prop, const char *val, u32 vl) {
    char buf[512]; u32 o = 0, al, pl = js_slen(prop); const u8 *a = dom_attr(node, AT_STYLE, &al);
    for (u32 i = 0; a && i < al;) {
        u32 j = i; while (j < al && a[j] != ';') j++;
        u32 c = i; while (c < j && a[c] != ':') c++;
        u32 ps = i; while (ps < c && a[ps] == ' ') ps++; u32 pe = c; while (pe > ps && a[pe - 1] == ' ') pe--;
        if (j > i && c < j && !js_dom_ieq(a + ps, pe - ps, prop, pl) && o + (j - i) + 2 < sizeof(buf)) { for (u32 q = i; q < j; q++) buf[o++] = (char)a[q]; buf[o++] = ';'; }
        i = j + 1;
    }
    if (vl && o + pl + vl + 3 < sizeof(buf)) { for (u32 q = 0; q < pl; q++) buf[o++] = prop[q]; buf[o++] = ':'; for (u32 q = 0; q < vl; q++) buf[o++] = val[q]; buf[o++] = ';'; }
    js_dom_set_attr(node, AT_STYLE, buf, o);
}
static jv js_dom_class_op(u32 node, int op, jv arg) {                      /* 0 add, 1 remove, 2 toggle, 3 contains */
    u32 w = js_tostr(arg), wl = js_sl(w), al; const u8 *a = dom_attr(node, AT_CLASS, &al);
    char buf[256]; u32 o = 0; int had = 0;
    for (u32 i = 0; a && i < al;) {
        while (i < al && a[i] == ' ') i++;
        u32 j = i; while (j < al && a[j] != ' ') j++;
        if (j > i) {
            int same = j - i == wl; for (u32 q = 0; same && q < wl; q++) if (a[i + q] != (u8)js_sp(w)[q]) same = 0;
            if (same) had = 1;
            if (!(same && (op == 1 || op == 2)) && o + (j - i) + 1 < sizeof(buf)) { if (o) buf[o++] = ' '; for (u32 q = i; q < j; q++) buf[o++] = (char)a[q]; }
        }
        i = j;
    }
    if (op == 3) return JS_BOOL(had);
    if ((op == 0 || op == 2) && !had && wl && o + wl + 1 < sizeof(buf)) { if (o) buf[o++] = ' '; for (u32 q = 0; q < wl; q++) buf[o++] = js_sp(w)[q]; }
    js_dom_set_attr(node, AT_CLASS, buf, o);
    return op == 2 ? JS_BOOL(!had) : JS_UNDEF;
}

/* ---------------------------------------------------------------- listeners and events */
static int js_dom_evtype(const char *s, u32 n) {
    static const char *const nm[] = { 0, "click", "input", "change", "submit", "load", "DOMContentLoaded" };
    for (int i = 1; i <= 6; i++) if (js_slen(nm[i]) == n) { u32 k = 0; while (k < n && s[k] == nm[i][k]) k++; if (k == n) return i; }
    return 0;
}
static void js_dom_lis_add(u32 node, int type, jv fn, int inl) {
    for (int i = 0; i < JD_MAXL; i++) if (js_lis[i].used && js_lis[i].node == node && js_lis[i].type == type && (inl == 2 ? js_lis[i].inl == 2 : js_strict_eq(js_lis[i].fn, fn))) { js_lis[i].fn = fn; return; }
    for (int i = 0; i < JD_MAXL; i++) if (!js_lis[i].used) { js_lis[i].node = node; js_lis[i].type = (u8)type; js_lis[i].fn = fn; js_lis[i].inl = (u8)inl; js_lis[i].used = 1; return; }
}
static void js_dom_lis_del(u32 node, int type, jv fn, int inl) {
    for (int i = 0; i < JD_MAXL; i++) if (js_lis[i].used && js_lis[i].node == node && js_lis[i].type == type && (inl == 2 ? js_lis[i].inl == 2 : js_strict_eq(js_lis[i].fn, fn))) js_lis[i].used = 0;
}
static void js_dom_roots(void) { for (int i = 0; i < JD_MAXL; i++) if (js_lis[i].used) js_mark_val(js_lis[i].fn); js_mark_val(js_dom_cur_ev); }
static u32 js_dom_target_of(jv thisv) {
    if (thisv.t == JT_OBJ && js_objs[thisv.v].kind == JK_ENV) return JD_WINDOW;
    u32 n; return js_dom_nodeof(thisv, &n) ? n : JD_WINDOW;
}
/* fires `type` at `node` and lets it bubble. Returns 1 if somebody called preventDefault() (or an inline handler returned false). */
static int js_dom_fire(u32 node, int type, const char *tname, int top) {
    if (!js_dom_active || !js_ready || !js_genv) return 0;
    u32 evo = js_obj_new(JK_OBJ, 0, 0); if (!evo) return 0;
    jv ev = JS_OBJV(evo); js_dom_cur_ev = ev;
    js_prop_set(evo, js_s_name, JS_STRV(js_str_c("Event")));
    js_prop_set(evo, js_str_c("type"), JS_STRV(js_str_c(tname)));
    js_prop_set(evo, js_str_c("target"), node == JD_WINDOW ? js_this_global : js_dom_wrap(node));
    js_prop_set(evo, js_str_c("defaultPrevented"), JS_BOOL(0));
    js_prop_set(evo, js_str_c("cancelBubble"), JS_BOOL(0));
    js_prop_set(evo, js_str_c("preventDefault"), JV(JT_NAT, JD_PREVENT));
    js_prop_set(evo, js_str_c("stopPropagation"), JV(JT_NAT, JD_STOP));
    js_prop_set(evo, js_str_c("stopImmediatePropagation"), JV(JT_NAT, JD_STOP));
    if (js_thr) { js_thr = 0; return 0; }
    u32 k_prev = js_str_c("defaultPrevented"), k_stop = js_str_c("cancelBubble");
    int prevented = 0;
    u32 cur = node;
    for (;;) {
        for (int i = 0; i < JD_MAXL; i++) {
            if (!js_lis[i].used || js_lis[i].node != cur || js_lis[i].type != type) continue;
            jv fn = js_lis[i].fn, thisv = cur == JD_WINDOW ? js_this_global : js_dom_wrap(cur), r;
            js_prop_set(evo, js_str_c("currentTarget"), thisv);
            if (top) r = js_run_call(fn, thisv, &ev, 1); else { r = js_call(fn, thisv, &ev, 1); if (js_thr) { js_report(); r = JS_UNDEF; } }
            if (js_lis[i].inl == 1 && r.t == JT_BOOL && r.v == 0) prevented = 1;
        }
        if (js_prop_get(evo, k_stop, 0).v) break;
        if (cur == JD_WINDOW) break;
        if (cur == 0) cur = JD_WINDOW; else cur = RD_NODES[cur].parent;
    }
    if (js_prop_get(evo, k_prev, 0).v) prevented = 1;
    js_dom_cur_ev = JS_UNDEF;
    return prevented;
}

/* ---------------------------------------------------------------- properties of elements (document, window and event objects included) */
enum { PR_ID = 1, PR_CLASSNAME, PR_TAG, PR_TEXT, PR_HTML, PR_VALUE, PR_CHECKED, PR_PARENT, PR_CHILDREN, PR_CHILDNODES, PR_FIRST, PR_LAST, PR_NEXT, PR_PREV, PR_STYLE,
       PR_CLASSLIST, PR_HIDDEN, PR_NODETYPE, PR_BODY, PR_HEAD, PR_DOCEL, PR_TITLE, PR_READY, PR_INNERW, PR_INNERH, PR_COOKIE, PR_DISABLED, PR_FIRSTEL, PR_LASTEL,
       PR_NEXTEL, PR_PREVEL, PR_DOC, PR_NODEVAL, PR_ONX };
static const struct { const char *n; u16 c; } js_dom_props[] = {
    {"id", PR_ID}, {"className", PR_CLASSNAME}, {"tagName", PR_TAG}, {"nodeName", PR_TAG}, {"textContent", PR_TEXT}, {"innerText", PR_TEXT}, {"innerHTML", PR_HTML},
    {"outerHTML", PR_HTML}, {"value", PR_VALUE}, {"checked", PR_CHECKED}, {"parentNode", PR_PARENT}, {"parentElement", PR_PARENT}, {"children", PR_CHILDREN},
    {"childNodes", PR_CHILDNODES}, {"firstChild", PR_FIRST}, {"lastChild", PR_LAST}, {"nextSibling", PR_NEXT}, {"previousSibling", PR_PREV}, {"style", PR_STYLE},
    {"classList", PR_CLASSLIST}, {"hidden", PR_HIDDEN}, {"nodeType", PR_NODETYPE}, {"body", PR_BODY}, {"head", PR_HEAD}, {"documentElement", PR_DOCEL},
    {"title", PR_TITLE}, {"readyState", PR_READY}, {"innerWidth", PR_INNERW}, {"innerHeight", PR_INNERH}, {"cookie", PR_COOKIE}, {"disabled", PR_DISABLED},
    {"firstElementChild", PR_FIRSTEL}, {"lastElementChild", PR_LASTEL}, {"nextElementSibling", PR_NEXTEL}, {"previousElementSibling", PR_PREVEL},
    {"ownerDocument", PR_DOC}, {"nodeValue", PR_NODEVAL}, {"data", PR_NODEVAL}, {"onclick", PR_ONX}, {"oninput", PR_ONX}, {"onchange", PR_ONX}, {"onsubmit", PR_ONX}, {"onload", PR_ONX}, {0, 0}
};
static const struct { const char *n; u16 c; } js_dom_meths[] = {
    {"getElementById", JD_GETBYID}, {"querySelector", JD_QS}, {"querySelectorAll", JD_QSA}, {"getElementsByTagName", JD_BYTAG}, {"getElementsByClassName", JD_BYCLASS},
    {"createElement", JD_CREATE}, {"createTextNode", JD_CREATETEXT}, {"addEventListener", JD_ADDL}, {"removeEventListener", JD_REML}, {"appendChild", JD_APPEND},
    {"append", JD_APPEND}, {"removeChild", JD_REMOVECHILD}, {"remove", JD_REMOVE}, {"insertBefore", JD_INSERTB}, {"getAttribute", JD_GETATTR}, {"setAttribute", JD_SETATTR},
    {"removeAttribute", JD_REMATTR}, {"hasAttribute", JD_HASATTR}, {"click", JD_CLICK}, {"focus", JD_FOCUS}, {"blur", JD_BLUR}, {"write", JD_WRITE}, {"contains", JD_CONTAINS}, {0, 0}
};
static int js_dom_onx(const char *s, u32 n) { return n > 2 && s[0] == 'o' && s[1] == 'n' ? js_dom_evtype(s + 2, n - 2) : 0; }
static jv js_dom_kids(u32 node, int elems_only) {
    u32 arr = js_arr_new(); if (js_thr) return JS_UNDEF;
    for (u32 c = RD_NODES[node].first; c; c = RD_NODES[c].next) if (!elems_only || RD_NODES[c].kind == RDK_ELEM) { js_arr_push(arr, js_dom_wrap(c)); if (js_thr) return JS_UNDEF; }
    return JS_OBJV(arr);
}
static jv js_dom_nodeval(u32 n) { return n ? js_dom_wrap(n) : JS_NULL; }
static u32 js_dom_sib(u32 n, int dir, int el) {
    u32 p = RD_NODES[n].parent; if (!n) return 0;
    if (dir > 0) { u32 c = RD_NODES[n].next; while (c && el && RD_NODES[c].kind != RDK_ELEM) c = RD_NODES[c].next; return c; }
    u32 prev = 0, c = RD_NODES[p].first; u32 best = 0;
    while (c && c != n) { prev = c; if (!el || RD_NODES[c].kind == RDK_ELEM) best = c; c = RD_NODES[c].next; }
    (void)prev; return best;
}
static int js_dom_get(jv obj, jv key, jv *out) {
    jobj *ob = &js_objs[obj.v];
    u32 ks = key.t == JT_STR ? (u32)key.v : js_tostr(key); if (js_thr) return 0;
    if (ob->kind == JK_ENV) {                                       /* window */
        if (js_str_is(ks, "innerWidth")) { *out = JS_NUM(620); return 1; }
        if (js_str_is(ks, "innerHeight")) { *out = JS_NUM(400); return 1; }
        if (js_str_is(ks, "addEventListener")) { *out = JV(JT_NAT, JD_ADDL); return 1; }
        if (js_str_is(ks, "removeEventListener")) { *out = JV(JT_NAT, JD_REML); return 1; }
        return 0;
    }
    u32 node = ob->a, sub = ob->flags;
    if (node >= dom_node_count) return 0;
    if (sub == 1) { static const char *const cm[] = { "add", "remove", "toggle", "contains" }; for (int i = 0; i < 4; i++) if (js_str_is(ks, cm[i])) { *out = JV(JT_NAT, JD_CADD + i); return 1; } return 0; }
    if (sub == 2) {
        if (js_str_is(ks, "setProperty")) { *out = JV(JT_NAT, JD_SETPROP); return 1; }
        if (js_str_is(ks, "getPropertyValue")) { *out = JV(JT_NAT, JD_GETPROP); return 1; }
        if (js_str_is(ks, "removeProperty")) { *out = JV(JT_NAT, JD_REMPROP); return 1; }
        char kb[40]; js_dom_kebab(js_sp(ks), js_sl(ks) > 30 ? 30 : js_sl(ks), kb, sizeof(kb));
        *out = js_dom_style_get(node, kb); return 1;
    }
    for (int i = 0; js_dom_meths[i].n; i++) if (js_str_is(ks, js_dom_meths[i].n)) { *out = JV(JT_NAT, js_dom_meths[i].c); return 1; }
    rd_node_t *e = &RD_NODES[node];
    int pr = 0; for (int i = 0; js_dom_props[i].n; i++) if (js_str_is(ks, js_dom_props[i].n)) { pr = js_dom_props[i].c; break; }
    switch (pr) {
        case PR_ID: *out = js_dom_attr_str(node, AT_ID); return 1;
        case PR_CLASSNAME: *out = js_dom_attr_str(node, AT_CLASS); return 1;
        case PR_TAG: { if (e->kind == RDK_TEXT) { *out = JS_STRV(js_str_c("#text")); return 1; } if (e->kind == RDK_DOC) { *out = JS_STRV(js_str_c("#document")); return 1; }
                       char t[16]; u32 k = 0; const char *nm = rd_tag_names[e->tag]; while (nm[k] && k < 14) { t[k] = (char)(nm[k] >= 'a' && nm[k] <= 'z' ? nm[k] - 32 : nm[k]); k++; } *out = JS_STRV(js_str_new(t, k, 0)); return 1; }
        case PR_TEXT: case PR_NODEVAL: *out = js_dom_text_of(node); return 1;
        case PR_HTML: *out = js_dom_text_of(node); return 1;       /* honest approximation: the text, not the markup */
        case PR_VALUE:
            if (e->tag == TG_INPUT || e->tag == TG_TEXTAREA) { u32 l; const u8 *v = rd_ctl_text(node, &l); *out = JS_STRV(js_str_new((const char *)v, l, 0)); return 1; }
            if (e->tag == TG_SELECT) { u32 o = rd_select_option(node); if (o && dom_has_attr(o, AT_VALUE)) *out = js_dom_attr_str(o, AT_VALUE); else *out = o ? js_dom_text_of(o) : JS_STRV(0); return 1; }
            *out = js_dom_attr_str(node, AT_VALUE); return 1;
        case PR_CHECKED: *out = JS_BOOL(rd_ctl_is_checked(node)); return 1;
        case PR_DISABLED: *out = JS_BOOL(dom_has_attr(node, AT_DISABLED)); return 1;
        case PR_HIDDEN: *out = JS_BOOL(dom_has_attr(node, AT_HIDDEN)); return 1;
        case PR_PARENT: *out = js_dom_nodeval(e->parent); if (!e->parent && node != 0) *out = js_dom_wrap(0); return 1;
        case PR_CHILDREN: *out = js_dom_kids(node, 1); return 1;
        case PR_CHILDNODES: *out = js_dom_kids(node, 0); return 1;
        case PR_FIRST: *out = js_dom_nodeval(e->first); return 1;
        case PR_LAST: *out = js_dom_nodeval(e->last); return 1;
        case PR_FIRSTEL: { u32 c = e->first; while (c && RD_NODES[c].kind != RDK_ELEM) c = RD_NODES[c].next; *out = js_dom_nodeval(c); return 1; }
        case PR_LASTEL: { u32 c = e->last; while (c && RD_NODES[c].kind != RDK_ELEM) { u32 p = js_dom_sib(c, -1, 0); c = p; } *out = js_dom_nodeval(c); return 1; }
        case PR_NEXT: *out = js_dom_nodeval(js_dom_sib(node, 1, 0)); return 1;
        case PR_NEXTEL: *out = js_dom_nodeval(js_dom_sib(node, 1, 1)); return 1;
        case PR_PREV: *out = js_dom_nodeval(js_dom_sib(node, -1, 0)); return 1;
        case PR_PREVEL: *out = js_dom_nodeval(js_dom_sib(node, -1, 1)); return 1;
        case PR_STYLE: *out = JS_OBJV(js_dom_wrap_as(node, 2)); return 1;
        case PR_CLASSLIST: *out = JS_OBJV(js_dom_wrap_as(node, 1)); return 1;
        case PR_NODETYPE: *out = JS_NUM(e->kind == RDK_ELEM ? 1 : e->kind == RDK_TEXT ? 3 : 9); return 1;
        case PR_BODY: *out = js_dom_wrap(dom_body); return 1;
        case PR_HEAD: *out = js_dom_wrap(dom_head); return 1;
        case PR_DOCEL: *out = js_dom_wrap(dom_html); return 1;
        case PR_DOC: *out = js_dom_wrap(0); return 1;
        case PR_TITLE: if (node == 0) { char t[100]; dom_title(t, sizeof(t)); *out = JS_STRV(js_str_c(t)); return 1; } break;
        case PR_READY: *out = JS_STRV(js_str_c(js_dom_loading ? "loading" : "complete")); return 1;
        case PR_COOKIE: *out = JS_STRV(0); return 1;
        case PR_ONX: *out = JS_NULL; return 1;
        default: break;
    }
    u32 aid = dom_attr_id((const u8 *)js_sp(ks), js_sl(ks) > 12 ? 13 : js_sl(ks));         /* href, src, alt, name, type, placeholder... straight from the attribute */
    if (aid && e->kind == RDK_ELEM) { *out = js_dom_attr_str(node, aid); return 1; }
    return 0;
}
static int js_dom_set(jv obj, jv key, jv val) {
    jobj *ob = &js_objs[obj.v];
    u32 ks = key.t == JT_STR ? (u32)key.v : js_tostr(key); if (js_thr) return 1;
    int et = js_dom_onx(js_sp(ks), js_sl(ks));
    if (ob->kind == JK_ENV) { if (et) { js_dom_lis_del(JD_WINDOW, et, JS_UNDEF, 2); if (js_callable(val)) js_dom_lis_add(JD_WINDOW, et, val, 2); return 1; } return 0; }
    u32 node = ob->a, sub = ob->flags;
    if (node >= dom_node_count) return 1;
    u32 vs = js_tostr(val); const char *vp = js_sp(vs); u32 vl = js_sl(vs);
    if (sub == 2) { char kb[40]; js_dom_kebab(js_sp(ks), js_sl(ks) > 30 ? 30 : js_sl(ks), kb, sizeof(kb)); js_dom_style_set(node, kb, vp, vl); return 1; }
    if (sub == 1) return 1;
    if (et) { js_dom_lis_del(node, et, JS_UNDEF, 2); if (js_callable(val)) js_dom_lis_add(node, et, val, 2); return 1; }
    rd_node_t *e = &RD_NODES[node];
    int pr = 0; for (int i = 0; js_dom_props[i].n; i++) if (js_str_is(ks, js_dom_props[i].n)) { pr = js_dom_props[i].c; break; }
    switch (pr) {
        case PR_ID: js_dom_set_attr(node, AT_ID, vp, vl); return 1;
        case PR_CLASSNAME: js_dom_set_attr(node, AT_CLASS, vp, vl); return 1;
        case PR_TEXT: case PR_NODEVAL: js_dom_set_text(node, vp, vl); return 1;
        case PR_HTML: js_dom_set_html(node, vp, vl); return 1;
        case PR_VALUE:
            if (e->tag == TG_INPUT || e->tag == TG_TEXTAREA) {
                rd_ctl_t *c = rd_ctl_find(node, 1);
                if (c) { u32 n = vl < RD_CTL_TEXT - 1 ? vl : RD_CTL_TEXT - 1; for (u32 i = 0; i < n; i++) c->text[i] = vp[i]; c->text[n] = 0; c->len = (int)n; c->sel = (int)n; c->flags |= 1; js_dom_mut(); return 1; }
            }
            js_dom_set_attr(node, AT_VALUE, vp, vl); return 1;
        case PR_CHECKED: rd_ctl_set_checked(node, js_truthy(val)); js_dom_mut(); return 1;
        case PR_HIDDEN: case PR_DISABLED: { u32 a = pr == PR_HIDDEN ? AT_HIDDEN : AT_DISABLED; if (js_truthy(val)) js_dom_set_attr(node, a, "", 0); else js_dom_del_attr(node, a); return 1; }
        case PR_TITLE: if (node == 0) return 1; break;
        default: break;
    }
    u32 aid = dom_attr_id((const u8 *)js_sp(ks), js_sl(ks) > 12 ? 13 : js_sl(ks));
    if (aid && e->kind == RDK_ELEM) { js_dom_set_attr(node, aid, vp, vl); return 1; }
    return 0;                                                       /* unknown property: fall through to a plain JS property on the wrapper (which is just ignored) */
}
static const char *js_dom_ename(u32 node) { (void)node; return "[object HTMLElement]"; }

/* ---------------------------------------------------------------- methods */
static int js_dom_call(u32 id, jv thisv, jv *a, int n, jv *out) {
    u32 node = 0; int isnode = js_dom_nodeof(thisv, &node);
    *out = JS_UNDEF;
    switch (id) {
        case JD_PREVENT: if (thisv.t == JT_OBJ) js_prop_set((u32)thisv.v, js_str_c("defaultPrevented"), JS_BOOL(1)); return 1;
        case JD_STOP: if (thisv.t == JT_OBJ) js_prop_set((u32)thisv.v, js_str_c("cancelBubble"), JS_BOOL(1)); return 1;
        case JD_ADDL: case JD_REML: {
            u32 t = js_tostr(ARG(0)); int et = js_dom_evtype(js_sp(t), js_sl(t));
            if (!et || !js_callable(ARG(1))) return 1;
            u32 target = isnode ? node : js_dom_target_of(thisv);
            if (id == JD_ADDL) js_dom_lis_add(target, et, a[1], 0); else js_dom_lis_del(target, et, a[1], 0);
            return 1;
        }
        case JD_CADD: case JD_CREM: case JD_CTOG: case JD_CHAS: if (isnode) { for (int i = 0; i < n || i == 0; i++) { *out = js_dom_class_op(node, (int)(id - JD_CADD), ARG(i)); if (id == JD_CHAS || id == JD_CTOG) break; } } return 1;
        case JD_SETPROP: if (isnode) { u32 p = js_tostr(ARG(0)), v = js_tostr(ARG(1)); char kb[40]; js_dom_kebab(js_sp(p), js_sl(p) > 30 ? 30 : js_sl(p), kb, sizeof(kb)); js_dom_style_set(node, kb, js_sp(v), js_sl(v)); } return 1;
        case JD_GETPROP: if (isnode) { u32 p = js_tostr(ARG(0)); char kb[40]; js_dom_kebab(js_sp(p), js_sl(p) > 30 ? 30 : js_sl(p), kb, sizeof(kb)); *out = js_dom_style_get(node, kb); } return 1;
        case JD_REMPROP: if (isnode) { u32 p = js_tostr(ARG(0)); char kb[40]; js_dom_kebab(js_sp(p), js_sl(p) > 30 ? 30 : js_sl(p), kb, sizeof(kb)); js_dom_style_set(node, kb, "", 0); } return 1;
        case JD_GETBYID: {
            u32 s = js_tostr(ARG(0)); *out = JS_NULL;
            for (u32 i = 1; i < dom_node_count; i++) { u32 l; const u8 *v = dom_attr(i, AT_ID, &l); if (v && l == js_sl(s) && l) { u32 q = 0; while (q < l && v[q] == (u8)js_sp(s)[q]) q++; if (q == l) { *out = js_dom_wrap(i); break; } } }
            return 1;
        }
        case JD_QS: case JD_QSA: *out = js_dom_query(node, ARG(0), id == JD_QSA); return 1;
        case JD_BYTAG: case JD_BYCLASS: { u32 s = js_tostr(ARG(0)); u32 w = js_str_cat(id == JD_BYCLASS ? js_str_c(".") : 0, s); *out = js_dom_query(node, JS_STRV(w), 1); return 1; }
        case JD_CREATE: {
            u32 s = js_tostr(ARG(0)); char t[16]; u32 l = js_sl(s) > 14 ? 14 : js_sl(s); for (u32 i = 0; i < l; i++) t[i] = js_lc(js_sp(s)[i]); t[l] = 0;
            u32 tag = dom_tag_id((const u8 *)t, l), nn = dom_new_node(RDK_ELEM, (u8)tag);
            if (!nn) { js_throw("RangeError", "too many DOM nodes"); return 1; }
            RD_NODES[nn].a = dom_attr_count; RD_NODES[nn].b = 0; *out = js_dom_wrap(nn); return 1;
        }
        case JD_CREATETEXT: { u32 s = js_tostr(ARG(0)); u32 nn = dom_new_node(RDK_TEXT, 0); if (!nn) { js_throw("RangeError", "too many DOM nodes"); return 1; } js_dom_set_text(nn, js_sp(s), js_sl(s)); *out = js_dom_wrap(nn); return 1; }
        case JD_APPEND: case JD_INSERTB: {
            if (!isnode) return 1;
            for (int i = 0; i < n; i++) {
                u32 c; jv v = a[i];
                if (!js_dom_nodeof(v, &c)) { if (v.t == JT_STR) { u32 tn = dom_new_node(RDK_TEXT, 0); if (!tn) return 1; js_dom_set_text(tn, js_sp((u32)v.v), js_sl((u32)v.v)); c = tn; } else continue; }
                u32 before = 0; if (id == JD_INSERTB) js_dom_nodeof(ARG(1), &before);
                js_dom_insert(node, c, before); if (id == JD_INSERTB) break;
            }
            *out = ARG(0); return 1;
        }
        case JD_REMOVECHILD: { u32 c; if (isnode && js_dom_nodeof(ARG(0), &c) && RD_NODES[c].parent == node) js_dom_unlink(c); *out = ARG(0); return 1; }
        case JD_REMOVE: if (isnode && node) js_dom_unlink(node); return 1;
        case JD_CONTAINS: { u32 c; *out = JS_BOOL(isnode && js_dom_nodeof(ARG(0), &c) && js_dom_is_ancestor(node, c)); return 1; }
        case JD_GETATTR: { u32 s = js_tostr(ARG(0)); u32 aid = dom_attr_id((const u8 *)js_sp(s), js_sl(s) > 12 ? 13 : js_sl(s)); *out = (isnode && aid && dom_has_attr(node, aid)) ? js_dom_attr_str(node, aid) : JS_NULL; return 1; }
        case JD_HASATTR: { u32 s = js_tostr(ARG(0)); u32 aid = dom_attr_id((const u8 *)js_sp(s), js_sl(s) > 12 ? 13 : js_sl(s)); *out = JS_BOOL(isnode && aid && dom_has_attr(node, aid)); return 1; }
        case JD_SETATTR: { u32 s = js_tostr(ARG(0)), v = js_tostr(ARG(1)); u32 aid = dom_attr_id((const u8 *)js_sp(s), js_sl(s) > 12 ? 13 : js_sl(s)); if (isnode && aid) js_dom_set_attr(node, aid, js_sp(v), js_sl(v)); return 1; }
        case JD_REMATTR: { u32 s = js_tostr(ARG(0)); u32 aid = dom_attr_id((const u8 *)js_sp(s), js_sl(s) > 12 ? 13 : js_sl(s)); if (isnode && aid) js_dom_del_attr(node, aid); return 1; }
        case JD_CLICK: if (isnode) js_dom_fire(node, JE_CLICK, "click", 0); return 1;
        case JD_FOCUS: if (isnode && (RD_NODES[node].tag == TG_INPUT || RD_NODES[node].tag == TG_TEXTAREA)) rd_ctl_focus(node); return 1;
        case JD_BLUR: rd_focus = 0; return 1;
        case JD_WRITE: { u32 s = js_tostr(ARG(0)); dom_parse_fragment(dom_body, (const u8 *)js_sp(s), js_sl(s)); js_dom_mut(); return 1; }
        default: return 0;
    }
}

/* ---------------------------------------------------------------- page lifecycle */
static void js_dom_alert(const char *m) {
    u32 i = 0; for (; m[i] && i < sizeof(js_alert_text) - 1; i++) js_alert_text[i] = m[i];
    js_alert_text[i] = 0; js_alert_new = 1;
    JS_LOG("[alert] "); JS_LOG(js_alert_text); JS_LOG("\n");
}
static const char js_dom_prelude[] =
    "function __store(){var d={};return{getItem:function(k){return k in d?d[k]:null;},setItem:function(k,v){d[k]=String(v);},"
    "removeItem:function(k){delete d[k];},clear:function(){d={};},key:function(i){var ks=Object.keys(d);return i<ks.length?ks[i]:null;}};}"
    "var localStorage=__store(),sessionStorage=__store();"
    "var navigator={userAgent:'MiniWeb/1.1 (MiniWin)',language:'ko',platform:'MiniWin'};"
    "function requestAnimationFrame(f){return setTimeout(f,16);}function cancelAnimationFrame(i){clearTimeout(i);}";
static int js_dom_page_wants_js(void) {
    for (u32 n = 1; n < dom_node_count; n++) {
        rd_node_t *e = &RD_NODES[n];
        if (e->kind != RDK_ELEM) continue;
        if (e->tag == TG_SCRIPT) return 1;
        if (dom_has_attr(n, AT_ONCLICK) || dom_has_attr(n, AT_ONINPUT) || dom_has_attr(n, AT_ONCHANGE) || dom_has_attr(n, AT_ONSUBMIT) || dom_has_attr(n, AT_ONLOAD)) return 1;
    }
    return 0;
}
static void js_dom_inline_handlers(void) {
    static const struct { u32 aid; int ev; } on[] = { { AT_ONCLICK, JE_CLICK }, { AT_ONINPUT, JE_INPUT }, { AT_ONCHANGE, JE_CHANGE }, { AT_ONSUBMIT, JE_SUBMIT }, { AT_ONLOAD, JE_LOAD } };
    char buf[640];
    for (u32 nd = 1; nd < dom_node_count; nd++) {
        if (RD_NODES[nd].kind != RDK_ELEM) continue;
        for (int k = 0; k < 5; k++) {
            u32 l; const u8 *v = dom_attr(nd, on[k].aid, &l);
            if (!v || l > 500) continue;
            u32 o = 0; const char *pre = "(function(event){"; while (*pre) buf[o++] = *pre++;
            for (u32 i = 0; i < l; i++) buf[o++] = (char)v[i];
            const char *post = "\n})"; while (*post) buf[o++] = *post++;
            js_top_enter();
            u32 head = js_parse_program(buf, o);
            if (js_perr || !head) { js_perr = 0; js_log("bad inline handler"); continue; }
            jv fn = js_eval((u32)js_ast[head].a);
            if (js_thr) { js_report(); continue; }
            js_dom_lis_add(on[k].ev == JE_LOAD && nd == dom_body ? JD_WINDOW : nd, on[k].ev, fn, 1);
        }
    }
}
/* the page has been parsed: spin up the engine if the page wants it, run every inline <script>, announce DOMContentLoaded and load */
static void js_dom_load(const char *host, const char *path) {
    js_dom_active = 0; js_dom_dirty = 0; js_dom_loading = 1;
    for (int i = 0; i < JD_MAXL; i++) js_lis[i].used = 0;
    js_dom_cur_ev = JS_UNDEF;
    if (!js_dom_page_wants_js()) return;
    if (!js_begin()) return;
    js_clock = js_dom_ms; js_gc_roots_hook = js_dom_roots; js_hook_get = js_dom_get; js_hook_set = js_dom_set; js_hook_call = js_dom_call;
    js_hook_ename = js_dom_ename; js_hook_alert = js_dom_alert;
    js_dom_active = 1;
    js_dom_refresh_clock();
    js_dom_enter();
    js_top_enter();
    js_prop_set(js_genv, js_str_new("document", 8, 1), js_dom_wrap(0));
    u32 loc = js_obj_new(JK_OBJ, 0, 0);
    if (loc) {
        char href[200]; u32 o = 0; const char *p = "https://"; while (*p && o < 190) href[o++] = *p++;
        for (u32 i = 0; host[i] && o < 190; i++) href[o++] = host[i];
        for (u32 i = 0; path[i] && o < 198; i++) href[o++] = path[i];
        href[o] = 0;
        js_prop_set(loc, js_str_c("href"), JS_STRV(js_str_c(href))); js_prop_set(loc, js_str_c("hostname"), JS_STRV(js_str_c(host)));
        js_prop_set(loc, js_str_c("host"), JS_STRV(js_str_c(host))); js_prop_set(loc, js_str_c("pathname"), JS_STRV(js_str_c(path)));
        js_prop_set(js_genv, js_str_new("location", 8, 1), JS_OBJV(loc));
    }
    js_run_source(js_dom_prelude, sizeof(js_dom_prelude) - 1);
    js_dom_inline_handlers();
    for (u32 n = 1; n < dom_node_count; n++) {                         /* scripts, in document order */
        rd_node_t *e = &RD_NODES[n];
        if (e->kind != RDK_ELEM || e->tag != TG_SCRIPT) continue;
        if (dom_has_attr(n, AT_SRC)) { js_log("external <script src> skipped (the page is parsed before it could arrive)"); continue; }
        u32 tl; const u8 *ty = dom_attr(n, AT_TYPE, &tl);
        if (ty && tl && !(js_dom_ieq(ty, tl, "text/javascript", 15) || js_dom_ieq(ty, tl, "application/javascript", 22) || js_dom_ieq(ty, tl, "text/ecmascript", 15))) continue;
        for (u32 c = e->first; c; c = RD_NODES[c].next)
            if (RD_NODES[c].kind == RDK_TEXT && RD_NODES[c].b) { js_dom_enter(); js_run_source((const char *)RD_POOL + RD_NODES[c].a, RD_NODES[c].b); }
    }
    js_dom_loading = 0;
    js_dom_fire(0, JE_DCL, "DOMContentLoaded", 1);
    js_dom_fire(JD_WINDOW, JE_LOAD, "load", 1);
    js_dom_dirty = 1; js_dom_commit();                                  /* always move the mark: the scripts were appending to the pool */
}
static void js_dom_tick(void) {                                         /* once per main-loop pass */
    if (!js_dom_active || !js_timers_pending()) return;
    js_dom_refresh_clock();
    js_dom_enter();
    js_timers_run();
    js_dom_commit();
}
static int js_dom_event(u32 node, int type, const char *name) {
    if (!js_dom_active) return 0;
    js_dom_enter();
    int pv = js_dom_fire(node, type, name, 1);
    js_dom_commit();
    return pv;
}
static int js_dom_click(u32 node)  { return js_dom_event(node, JE_CLICK, "click"); }
static int js_dom_submit(u32 form) { return js_dom_event(form, JE_SUBMIT, "submit"); }
static void js_dom_input(u32 node) { js_dom_event(node, JE_INPUT, "input"); }
static void js_dom_change(u32 node) { js_dom_event(node, JE_CHANGE, "change"); }
#endif
