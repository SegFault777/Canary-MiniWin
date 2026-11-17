/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CSS_H
#define CSS_H
#include "dom.h"

/* ============================================================
 * css.h -- the middle of MiniWeb's HTML5 engine: stylesheets in,
 * "what does this element look like" out.
 *
 *   PARSING   <style> blocks, the built-in user-agent sheet, and style=""
 *             attributes: rules, selector lists, declarations, !important,
 *             @media (min/max-width, screen/print, prefers-color-scheme),
 *             @supports/@layer (looked inside, not evaluated), custom
 *             properties on :root/html/body with var(--x, fallback), and
 *             calc()/min()/max()/clamp() in the sizes people actually write.
 *   SELECTORS type, *, .class, #id, [attr], [attr=v] [~=] [|=] [^=] [$=] [*=],
 *             descendant, >, +, ~, :first-child :last-child :only-child
 *             :nth-child(odd|even|an+b) :not(simple) :root :empty :link
 *             :checked :disabled. The dynamic ones (:hover :focus :active
 *             :visited) and every ::pseudo-element simply never match --
 *             a static page has no pointer to hover with, and no
 *             generated content to put before or after.
 *   CASCADE   UA sheet < presentational attributes (<font color>,
 *             bgcolor, cellpadding...) < author rules by specificity and
 *             source order < style="" ; !important flips that order.
 *   UNITS     everything is converted to DEVICE pixels at the moment it is
 *             parsed. The browser runs at a 11/16 "zoom": 16 CSS px of body
 *             text is exactly one 11x11 glyph cell, so a 960px-wide site
 *             design fits a 640px screen the way it would in a browser
 *             zoomed out to 69%. Percentages stay percentages until layout.
 *
 * No malloc: rules, selector compounds and declarations live in fixed
 * arrays (kernel: memmap.h, host tests: plain arrays).
 * ============================================================ */

#ifndef RD_CSS_RULES
#define RD_CSS_RULE_MAX  MW_RD_CSS_RULE_MAX
#define RD_CSS_COMP_MAX  MW_RD_CSS_COMP_MAX
#define RD_CSS_DECL_MAX  MW_RD_CSS_DECL_MAX
#define RD_CSS_RULES ((css_rule_t *)MW_RD_CSS_RULES_ADDR)
#define RD_CSS_COMPS ((css_comp_t *)MW_RD_CSS_COMPS_ADDR)
#define RD_CSS_DECLS ((css_decl_t *)MW_RD_CSS_DECLS_ADDR)
#endif

/* ---------- the zoom: CSS px -> device px ---------- */
#define CSS_ZOOM_NUM 11
#define CSS_ZOOM_DEN 16
#define CSS_BASE_FONT_PX 16            /* the initial `medium` font size, in CSS px */
#define CSS_MIN_CELL 11                /* the smallest glyph cell we own */
#define CSS_MAX_CELL 66

static int css_viewport_w = 620;       /* device px, set by the renderer before each layout */
static int css_viewport_h = 400;

/* ---------- lengths ---------- */
#define CSS_AUTO 0x7FFF
typedef struct { short px; short pct; } css_len_t;       /* value = px + pct/10 percent of the containing width; pct==CSS_AUTO -> auto */
static inline css_len_t css_len_px(int px) { css_len_t l; l.px = (short)(px > 32000 ? 32000 : px < -32000 ? -32000 : px); l.pct = 0; return l; }
static inline css_len_t css_len_auto(void) { css_len_t l; l.px = 0; l.pct = CSS_AUTO; return l; }
static inline int css_len_is_auto(css_len_t l) { return l.pct == CSS_AUTO; }
/* resolves against the containing block's width `base` */
static inline int css_len_resolve(css_len_t l, int base) {
    if (l.pct == CSS_AUTO) return 0;
    if (base > 20000) base = 20000; else if (base < -20000) base = -20000;
    int v = l.px + (base * (int)l.pct) / 1000;
    return v > 20000 ? 20000 : v < -20000 ? -20000 : v;
}

/* ---------- colors: 0x00RRGGBB, or CSS_NOCOLOR for transparent ---------- */
#define CSS_NOCOLOR   0x80000000u
#define CSS_CURRENT   0x40000000u      /* "currentcolor", resolved while applying */

/* ---------- the computed style of one element ---------- */
enum { DISP_NONE = 0, DISP_INLINE, DISP_BLOCK, DISP_LIST_ITEM, DISP_INLINE_BLOCK, DISP_TABLE, DISP_INLINE_TABLE,
       DISP_TABLE_ROW, DISP_TABLE_CELL, DISP_TABLE_GROUP, DISP_TABLE_CAPTION, DISP_FLEX, DISP_INLINE_FLEX, DISP_GRID };
enum { TA_LEFT = 0, TA_CENTER, TA_RIGHT, TA_JUSTIFY };
enum { WS_NORMAL = 0, WS_NOWRAP, WS_PRE, WS_PRE_WRAP, WS_PRE_LINE };
enum { BS_NONE = 0, BS_SOLID, BS_DASHED, BS_DOTTED, BS_DOUBLE, BS_INSET, BS_OUTSET, BS_GROOVE, BS_RIDGE };
enum { VA_BASELINE = 0, VA_TOP, VA_MIDDLE, VA_BOTTOM, VA_SUB, VA_SUPER };
enum { LS_NONE = 0, LS_DISC, LS_CIRCLE, LS_SQUARE, LS_DECIMAL, LS_LOWER_ALPHA, LS_UPPER_ALPHA, LS_LOWER_ROMAN, LS_UPPER_ROMAN };
enum { TT_NONE = 0, TT_UPPER, TT_LOWER, TT_CAP };
enum { FD_ROW = 0, FD_COLUMN };
enum { JC_START = 0, JC_END, JC_CENTER, JC_BETWEEN, JC_AROUND, JC_EVENLY };
enum { AI_STRETCH = 0, AI_START, AI_END, AI_CENTER, AI_AUTO = 9 };
enum { POS_STATIC = 0, POS_RELATIVE, POS_ABSOLUTE, POS_FIXED };
#define SF_BOLD   1
#define SF_ITALIC 2
#define SF_UNDER  4
#define SF_STRIKE 8

typedef struct {
    u8 display, position, floating, clear;
    u8 text_align, valign, white_space, visibility;
    u8 text_transform, list_style, overflow_hidden, box_sizing_border;
    u8 flags, cell, border_collapse, hidden_text;
    u8 bstyle[4], bw[4];                 /* top, right, bottom, left */
    u8 flex_dir, flex_wrap, justify, align_items, align_self, order;
    u8 lh_mode;                          /* 0 normal, 1 = lh is px, 2 = lh is a factor x100 */
    u8 pad8;
    short fpx;                           /* font-size in CSS px (what `em` multiplies) */
    short lh;
    short gap_row, gap_col, spacing_x, spacing_y, text_indent;
    short flex_grow, flex_shrink;        /* x10 */
    u32 color, bg, bcolor[4];
    css_len_t margin[4], padding[4];
    css_len_t width, height, minw, maxw, minh, maxh, flex_basis;
    u32 gtc_off; u16 gtc_len;            /* grid-template-columns text (pool offset) */
    u16 vscope;                          /* the custom-property scope of this element (css_sc_*): inherited, see css_compute */
    u32 bg2, sh_color, backdrop;         /* gradient end colour; box-shadow colour; the colour behind this box (for opacity) */
    css_len_t off[4];                    /* top, right, bottom, left (position offsets; auto = not given) */
    short sh_dx, sh_dy;                  /* box-shadow offset */
    u8 sh_blur, radius, grad, alpha;     /* blur px; border-radius px (255 = as round as possible); 0 none / 1 vertical / 2 horizontal gradient; opacity 0..255 */
    u32 content_off, reset_off, incr_off;/* `content`, `counter-reset`, `counter-increment`: the value text (pool offset) -- turned into text by the generated-content pass */
    u16 content_len, reset_len, incr_len, aspect;   /* aspect-ratio x256 (0 = auto) */
    short zindex; u8 has_z, has_content;
    css_len_t tx, ty;                    /* transform: translate() */
    u8 has_tr, text_overflow, pad24[2];  /* text_overflow: 1 = ellipsis */
} css_style_t;

/* ---------- rules, compounds, declarations ---------- */
#define PS_FIRST     0x00000001u
#define PS_LAST      0x00000002u
#define PS_ONLY      0x00000004u
#define PS_ROOT      0x00000008u
#define PS_EMPTY     0x00000010u
#define PS_LINK      0x00000020u
#define PS_CHECKED   0x00000040u
#define PS_DISABLED  0x00000080u
#define PS_NTH       0x00000100u
#define PS_TFIRST    0x00000400u      /* :first-of-type  -- counted among siblings with the SAME tag, not all siblings */
#define PS_TLAST     0x00000800u
#define PS_TONLY     0x00001000u
#define PS_NTHT      0x00002000u      /* the an+b above counts same-tag siblings (nth-of-type) */
#define PS_NTHL      0x00004000u      /* ... and counts from the END (nth-last-child / nth-last-of-type) */
#define PS_GROUP     0x00008000u      /* the compound carries :is() / :where() / :not() / :has() groups (see grp) */
#define PS_ENABLED   0x00010000u
#define PS_PHSHOWN   0x00020000u      /* :placeholder-shown */
#define PS_READONLY  0x00040000u

enum { AOP_NONE = 0, AOP_EXISTS, AOP_EQ, AOP_WORD, AOP_DASH, AOP_PREFIX, AOP_SUFFIX, AOP_SUBSTR };
enum { GK_NONE = 0, GK_IS, GK_NOT, GK_HAS };         /* kinds of selector-list pseudo-classes */
enum { PE_NONE = 0, PE_BEFORE, PE_AFTER };            /* generated-content pseudo-elements */

/* One compound selector (tag#id.class[attr]:pseudo). 40 bytes -- the slot size memmap.h reserves.
 *
 * The array holds two kinds of things. Complex selectors (a b > c) live at the BOTTOM, one compound per slot, in order, so
 * "the compound to my left" is just index-1. The arguments of :is()/:where()/:not()/:has() live at the TOP (allocated
 * downwards, see css_comp_top): a compound with PS_GROUP set keeps in `grp` the index of its first GROUP DESCRIPTOR; a
 * descriptor is a slot of its own (alt_kind = GK_*, alt_n members starting at `grp`, `attr_voff` = the next descriptor of
 * the same compound or 0xFFFFFFFF). Chaining descriptors is what lets `a:not(.x):not(.y)` work without a slot per pseudo. */
typedef struct {
    u32 id_hash;
    u32 cls[2];
    u32 grp;
    u32 attr_voff;
    u32 pseudo;
    u16 tag;
    u16 attr_vlen;
    u8 ncls, comb;                       /* comb: the combinator joining this compound to the one on its left (for :has members: its relation to the subject) */
    u8 attr_id, attr_op;
    u8 alt_kind, alt_n;
    signed char nth_a, nth_b;
    u8 attr_icase, pe;                   /* pe: PE_BEFORE / PE_AFTER when the selector ends in ::before / ::after */
    u8 never, pad;                       /* never: :hover, :focus... -- a static page has no pointer to hover with */
} css_comp_t;

typedef struct {
    u32 comp_first, decl_first, spec, order;
    u16 ndecl, next;                     /* next: chain inside its lookup bucket */
    u8 ncomp, key_kind;
    u8 pe;                               /* the rule styles ::before / ::after of the matched element, not the element itself */
    u8 pad[3];
    u32 key;
} css_rule_t;

typedef struct { u16 prop; u8 imp, pad; u32 voff; u32 vlen; } css_decl_t;

#define CSS_BUCKET_N 256
static u32 css_rule_count, css_comp_count, css_decl_count, css_order;
static u32 css_comp_top;     /* the group-descriptor / :is()-member region of the compound array grows DOWN from RD_CSS_COMP_MAX */
static int css_has_pe;       /* some rule styles ::before / ::after: the generated-content pass is worth running */
static int css_has_counters; /* some rule uses counter-reset / counter-increment */
#define CSS_INTERN_MAX 48
static u32 css_intern_hash[CSS_INTERN_MAX], css_intern_off[CSS_INTERN_MAX], css_intern_n;   /* values interned into the pool (see css_intern) */
static int css_overflowed;   /* some rules / matches were dropped for lack of room: the page may look different from what its author intended */
static u16 css_by_tag[TG__COUNT];
static u16 css_by_cls[CSS_BUCKET_N];
static u16 css_by_id[CSS_BUCKET_N];
static u16 css_universal;
#define CSS_NIL 0xFFFF

/* ---------- small string helpers ---------- */
static inline int css_is_ws(u8 c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static inline u8 css_lc(u8 c) { return (c >= 'A' && c <= 'Z') ? (u8)(c + 32) : c; }
static inline int css_is_digit(u8 c) { return c >= '0' && c <= '9'; }
static inline int css_is_ident(u8 c) { return dom_is_alnum(c) || c == '-' || c == '_' || c >= 0x80; }
/* case-insensitive compare of s[0..n) with a lower-case literal */
static inline int css_kw(const u8 *s, u32 n, const char *lit) {
    u32 i = 0;
    for (; lit[i]; i++) { if (i >= n || css_lc(s[i]) != (u8)lit[i]) return 0; }
    return i == n;
}
static inline u32 css_hash(const u8 *s, u32 n) {               /* FNV-1a, case-sensitive (class and id names are) */
    u32 h = 2166136261u;
    for (u32 i = 0; i < n; i++) { h ^= s[i]; h *= 16777619u; }
    return h ? h : 1;
}
static inline u32 css_hash_lc(const u8 *s, u32 n) {
    u32 h = 2166136261u;
    for (u32 i = 0; i < n; i++) { h ^= css_lc(s[i]); h *= 16777619u; }
    return h ? h : 1;
}

/* ---------- numbers: fixed point x100 ---------- */
/* Parses [+-]digits[.digits] at s[0..n); returns the chars consumed (0 = not a number). */
static inline u32 css_num(const u8 *s, u32 n, int *out100) {
    u32 i = 0; int neg = 0, v = 0, frac = 0, fd = 0, any = 0;
    if (i < n && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; i++; }
    while (i < n && css_is_digit(s[i])) { if (v < 1000) v = v * 10 + (s[i] - '0'); i++; any = 1; }     /* saturates near 9999: nobody needs more, and the math downstream stays in 32 bits */
    if (i < n && s[i] == '.') {
        i++;
        while (i < n && css_is_digit(s[i])) { if (fd < 2) { frac = frac * 10 + (s[i] - '0'); fd++; } i++; any = 1; }
    }
    if (!any) return 0;
    while (fd < 2) { frac *= 10; fd++; }
    v = v * 100 + frac;
    *out100 = neg ? -v : v;
    return i;
}

/* ---------- custom properties (var(--x)) ----------
 * Two layers. Declarations on :root / html / body / * are collected into the small global table at parse time (they are the
 * overwhelmingly common case and cost nothing per element). Declarations on any OTHER selector are real per-element, inherited
 * custom properties: css_compute() appends them to the scope arena below -- a linked list per element whose tail is the element's
 * `vscope` -- and var() looks the chain up from the element outwards before it falls back to the global table. */
#define CSS_VAR_MAX 48
static u32 css_var_hash[CSS_VAR_MAX], css_var_off[CSS_VAR_MAX], css_var_len[CSS_VAR_MAX];
static u32 css_var_count;

#define CSS_SCOPE_MAX 1024
static u32 css_sc_hash[CSS_SCOPE_MAX], css_sc_off[CSS_SCOPE_MAX], css_sc_len[CSS_SCOPE_MAX];
static u16 css_sc_parent[CSS_SCOPE_MAX];
static u32 css_sc_count = 1;                 /* entry 0 is "no scope" */
static u16 css_cur_scope;                    /* the scope var() is being expanded in (set by css_compute) */
#define CSS_SCOPE_UNSET 0xFFFFu
static u16 css_node_scope[RD_NODE_MAX];      /* per element: the scope computed for it (CSS_SCOPE_UNSET = not yet) -- css_compute runs many times per node */

static inline void css_scope_reset(void) { css_sc_count = 1; css_cur_scope = 0; for (u32 i = 0; i < RD_NODE_MAX; i++) css_node_scope[i] = CSS_SCOPE_UNSET; }
static inline u16 css_scope_push(u16 parent, u32 h, u32 off, u32 len) {
    if (css_sc_count >= CSS_SCOPE_MAX) return parent;
    u32 i = css_sc_count++;
    css_sc_hash[i] = h; css_sc_off[i] = off; css_sc_len[i] = len; css_sc_parent[i] = parent;
    return (u16)i;
}

static inline void css_var_set(u32 h, u32 off, u32 len) {
    for (u32 i = 0; i < css_var_count; i++) if (css_var_hash[i] == h) { css_var_off[i] = off; css_var_len[i] = len; return; }
    if (css_var_count < CSS_VAR_MAX) { css_var_hash[css_var_count] = h; css_var_off[css_var_count] = off; css_var_len[css_var_count] = len; css_var_count++; }
}
static inline int css_var_get(u32 h, u32 *off, u32 *len) {
    for (u32 sc = css_cur_scope; sc; sc = css_sc_parent[sc]) if (css_sc_hash[sc] == h) { *off = css_sc_off[sc]; *len = css_sc_len[sc]; return 1; }
    for (u32 i = 0; i < css_var_count; i++) if (css_var_hash[i] == h) { *off = css_var_off[i]; *len = css_var_len[i]; return 1; }
    return 0;
}

/* Expands every var(--name[, fallback]) in v[0..n) into out (NUL-terminated); returns the length. */
static inline u32 css_expand(const u8 *v, u32 n, u8 *out, u32 cap, int depth) {
    u32 o = 0;
    for (u32 i = 0; i < n && o + 1 < cap; ) {
        if (i + 4 <= n && css_lc(v[i]) == 'v' && css_lc(v[i + 1]) == 'a' && css_lc(v[i + 2]) == 'r' && v[i + 3] == '(' && depth < 4) {
            u32 j = i + 4, nest = 1;
            while (j < n && nest) { if (v[j] == '(') nest++; else if (v[j] == ')') nest--; j++; }
            u32 end = nest ? n : j - 1;                          /* index of the closing ')' */
            u32 ns = i + 4;
            while (ns < end && css_is_ws(v[ns])) ns++;
            u32 ne = ns;
            while (ne < end && v[ne] != ',' && !css_is_ws(v[ne])) ne++;
            u32 voff, vlen;
            if (css_var_get(css_hash(v + ns, ne - ns), &voff, &vlen)) {
                o += css_expand(RD_POOL + voff, vlen, out + o, cap - o, depth + 1);
            } else {
                u32 fb = ne;
                while (fb < end && (v[fb] != ',')) fb++;
                if (fb < end) { fb++; while (fb < end && css_is_ws(v[fb])) fb++; o += css_expand(v + fb, end - fb, out + o, cap - o, depth + 1); }
            }
            i = nest ? n : j;
            continue;
        }
        out[o++] = v[i++];
    }
    out[o] = 0;
    return o;
}

/* ---------- colors ---------- */
typedef struct { const char *name; u32 rgb; } css_namedcolor_t;
static const css_namedcolor_t css_named[] = {
    {"black",0x000000},{"white",0xFFFFFF},{"red",0xFF0000},{"green",0x008000},{"blue",0x0000FF},{"yellow",0xFFFF00},
    {"gray",0x808080},{"grey",0x808080},{"silver",0xC0C0C0},{"maroon",0x800000},{"purple",0x800080},{"fuchsia",0xFF00FF},
    {"magenta",0xFF00FF},{"lime",0x00FF00},{"olive",0x808000},{"navy",0x000080},{"teal",0x008080},{"aqua",0x00FFFF},
    {"cyan",0x00FFFF},{"orange",0xFFA500},{"pink",0xFFC0CB},{"brown",0xA52A2A},{"gold",0xFFD700},{"tan",0xD2B48C},
    {"beige",0xF5F5DC},{"ivory",0xFFFFF0},{"khaki",0xF0E68C},{"coral",0xFF7F50},{"salmon",0xFA8072},{"crimson",0xDC143C},
    {"indigo",0x4B0082},{"violet",0xEE82EE},{"orchid",0xDA70D6},{"plum",0xDDA0DD},{"lavender",0xE6E6FA},{"azure",0xF0FFFF},
    {"turquoise",0x40E0D0},{"chocolate",0xD2691E},{"firebrick",0xB22222},{"forestgreen",0x228B22},{"darkgreen",0x006400},
    {"darkblue",0x00008B},{"darkred",0x8B0000},{"darkgray",0xA9A9A9},{"darkgrey",0xA9A9A9},{"lightgray",0xD3D3D3},
    {"lightgrey",0xD3D3D3},{"lightblue",0xADD8E6},{"lightgreen",0x90EE90},{"lightyellow",0xFFFFE0},{"lightpink",0xFFB6C1},
    {"dimgray",0x696969},{"dimgrey",0x696969},{"whitesmoke",0xF5F5F5},{"gainsboro",0xDCDCDC},{"snow",0xFFFAFA},
    {"honeydew",0xF0FFF0},{"mintcream",0xF5FFFA},{"aliceblue",0xF0F8FF},{"ghostwhite",0xF8F8FF},{"steelblue",0x4682B4},
    {"royalblue",0x4169E1},{"skyblue",0x87CEEB},{"dodgerblue",0x1E90FF},{"cornflowerblue",0x6495ED},{"slategray",0x708090},
    {"slategrey",0x708090},{"midnightblue",0x191970},{"seagreen",0x2E8B57},{"limegreen",0x32CD32},{"darkorange",0xFF8C00},
    {"tomato",0xFF6347},{"orangered",0xFF4500},{"hotpink",0xFF69B4},{"deeppink",0xFF1493},{"goldenrod",0xDAA520},
    {"sienna",0xA0522D},{"peru",0xCD853F},{"wheat",0xF5DEB3},{"linen",0xFAF0E6},{"oldlace",0xFDF5E6},{"seashell",0xFFF5EE},
    {"lightcyan",0xE0FFFF},{"lightsalmon",0xFFA07A},{"rebeccapurple",0x663399},{"darkslategray",0x2F4F4F},
    {"darkslategrey",0x2F4F4F},{"cadetblue",0x5F9EA0},{"mediumseagreen",0x3CB371},{"darkviolet",0x9400D3},
    {"mediumpurple",0x9370DB},{"lightsteelblue",0xB0C4DE},{"palegreen",0x98FB98},{"yellowgreen",0x9ACD32},
    {"darkcyan",0x008B8B},{"lightgoldenrodyellow",0xFAFAD2},{"navajowhite",0xFFDEAD},{"bisque",0xFFE4C4},
    {"mistyrose",0xFFE4E1},{"lemonchiffon",0xFFFACD},{"cornsilk",0xFFF8DC},{"antiquewhite",0xFAEBD7},
    {"aquamarine",0x7FFFD4},{"blanchedalmond",0xFFEBCD},{"blueviolet",0x8A2BE2},{"burlywood",0xDEB887},
    {"chartreuse",0x7FFF00},{"darkgoldenrod",0xB8860B},{"darkkhaki",0xBDB76B},{"darkmagenta",0x8B008B},
    {"darkolivegreen",0x556B2F},{"darkorchid",0x9932CC},{"darksalmon",0xE9967A},{"darkseagreen",0x8FBC8F},
    {"darkslateblue",0x483D8B},{"darkturquoise",0x00CED1},{"deepskyblue",0x00BFFF},{"floralwhite",0xFFFAF0},
    {"greenyellow",0xADFF2F},{"indianred",0xCD5C5C},{"lavenderblush",0xFFF0F5},{"lawngreen",0x7CFC00},
    {"lightcoral",0xF08080},{"lightseagreen",0x20B2AA},{"lightskyblue",0x87CEFA},{"lightslategray",0x778899},
    {"lightslategrey",0x778899},{"mediumaquamarine",0x66CDAA},{"mediumblue",0x0000CD},{"mediumorchid",0xBA55D3},
    {"mediumslateblue",0x7B68EE},{"mediumspringgreen",0x00FA9A},{"mediumturquoise",0x48D1CC},{"mediumvioletred",0xC71585},
    {"moccasin",0xFFE4B5},{"olivedrab",0x6B8E23},{"palegoldenrod",0xEEE8AA},{"paleturquoise",0xAFEEEE},
    {"palevioletred",0xDB7093},{"papayawhip",0xFFEFD5},{"peachpuff",0xFFDAB9},{"powderblue",0xB0E0E6},
    {"rosybrown",0xBC8F8F},{"saddlebrown",0x8B4513},{"sandybrown",0xF4A460},{"slateblue",0x6A5ACD},{"springgreen",0x00FF7F},
    {"thistle",0xD8BFD8},{"canvas",0xFFFFFF},{"canvastext",0x000000},{"linktext",0x0000EE},{"visitedtext",0x551A8B},
    {"activetext",0xFF0000},{"buttonface",0xEFEFEF},{"buttontext",0x000000},{"buttonborder",0x767676},{"field",0xFFFFFF},
    {"fieldtext",0x000000},{"graytext",0x808080},{"highlight",0x3390FF},{"highlighttext",0xFFFFFF},{"mark",0xFFFF00},
    {"marktext",0x000000},{"accentcolor",0x0075FF},{"accentcolortext",0xFFFFFF},
};

static inline int css_hexv(u8 c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
/* The colour a translucent colour is composited over. css_apply() points it at the box's backdrop (the colour behind the box), so
 * rgb(255 255 255 / 20%) on a dark page is a light grey rather than the white-on-white it used to be. Default: a white page. */
static u32 css_blend_base = 0xFFFFFFu;
static inline u32 css_blend_white(u32 rgb, int a255) {          /* alpha over css_blend_base */
    if (a255 >= 255) return rgb;
    u32 bs = css_blend_base;
    int br = (int)((bs >> 16) & 255), bg = (int)((bs >> 8) & 255), bb = (int)(bs & 255);
    int r = (int)((rgb >> 16) & 255), g = (int)((rgb >> 8) & 255), b = (int)(rgb & 255);
    r = (r * a255 + br * (255 - a255)) / 255; g = (g * a255 + bg * (255 - a255)) / 255; b = (b * a255 + bb * (255 - a255)) / 255;
    return (u32)((r << 16) | (g << 8) | b);
}
static inline int css_clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static inline int css_hsl_comp(int t, int m1, int m2) {          /* t in 0..360*, m in 0..1000 */
    if (t < 0) t += 360;
    if (t >= 360) t -= 360;
    if (t < 60) return m1 + (m2 - m1) * t / 60;
    if (t < 180) return m2;
    if (t < 240) return m1 + (m2 - m1) * (240 - t) / 60;
    return m1;
}


/* ---- modern colour notations: oklab() oklch() hwb() color() color-mix() light-dark() ----
 * Everything is fixed point (Q12 = 4096 is 1.0); there is no floating point in this kernel. Out-of-gamut results are clamped, not
 * gamut-mapped, and color-mix() mixes in sRGB whatever `in <space>` says -- close enough for the pairs designers actually write. */
static const u16 css_lin_tab[256] = {   /* sRGB 0..255 -> linear light, Q16 (generated; the inverse is a binary search) */
    0,20,40,60,80,99,119,139,159,179,199,219,241,264,288,313,
    340,367,396,427,458,491,526,562,599,637,677,718,761,805,851,898,
    947,997,1048,1101,1156,1212,1270,1330,1391,1453,1517,1583,1651,1720,1790,1863,
    1937,2013,2090,2170,2250,2333,2418,2504,2592,2681,2773,2866,2961,3058,3157,3258,
    3360,3464,3570,3678,3788,3900,4014,4129,4247,4366,4488,4611,4736,4864,4993,5124,
    5257,5392,5530,5669,5810,5953,6099,6246,6395,6547,6700,6856,7014,7174,7335,7500,
    7666,7834,8004,8177,8352,8528,8708,8889,9072,9258,9445,9635,9828,10022,10219,10417,
    10619,10822,11028,11235,11446,11658,11873,12090,12309,12530,12754,12980,13209,13440,13673,13909,
    14146,14387,14629,14874,15122,15371,15623,15878,16135,16394,16656,16920,17187,17456,17727,18001,
    18277,18556,18837,19121,19407,19696,19987,20281,20577,20876,21177,21481,21787,22096,22407,22721,
    23038,23357,23678,24002,24329,24658,24990,25325,25662,26001,26344,26688,27036,27386,27739,28094,
    28452,28813,29176,29542,29911,30282,30656,31033,31412,31794,32179,32567,32957,33350,33745,34143,
    34544,34948,35355,35764,36176,36591,37008,37429,37852,38278,38706,39138,39572,40009,40449,40891,
    41337,41785,42236,42690,43147,43606,44069,44534,45002,45473,45947,46423,46903,47385,47871,48359,
    48850,49344,49841,50341,50844,51349,51858,52369,52884,53401,53921,54445,54971,55500,56032,56567,
    57105,57646,58190,58737,59287,59840,60396,60955,61517,62082,62650,63221,63795,64372,64952,65535
};
static const u16 css_sin_tab[91] = {    /* sin(0..90 degrees), Q12 */
    0,71,143,214,286,357,428,499,570,641,711,782,852,921,991,
    1060,1129,1198,1266,1334,1401,1468,1534,1600,1666,1731,1796,1860,1923,1986,
    2048,2110,2171,2231,2290,2349,2408,2465,2522,2578,2633,2687,2741,2793,2845,
    2896,2946,2996,3044,3091,3138,3183,3228,3271,3314,3355,3396,3435,3474,3511,
    3547,3582,3617,3650,3681,3712,3742,3770,3798,3824,3849,3873,3896,3917,3937,
    3956,3974,3991,4006,4021,4034,4046,4056,4065,4074,4080,4086,4090,4094,4095,
    4096
};
static inline int css_sin12(int deg) {
    deg %= 360; if (deg < 0) deg += 360;
    if (deg <= 90) return css_sin_tab[deg];
    if (deg <= 180) return css_sin_tab[180 - deg];
    if (deg <= 270) return -(int)css_sin_tab[deg - 180];
    return -(int)css_sin_tab[360 - deg];
}
static inline int css_cos12(int deg) { return css_sin12(deg + 90); }
static inline int css_lin_to_srgb(int lin12) {               /* linear light Q12 -> 0..255 */
    if (lin12 <= 0) return 0;
    if (lin12 >= 4096) return 255;
    int t = lin12 << 4, lo = 0, hi = 255;
    while (hi - lo > 1) { int mid = (lo + hi) >> 1; if (css_lin_tab[mid] <= t) lo = mid; else hi = mid; }
    return (t - css_lin_tab[lo] <= css_lin_tab[hi] - t) ? lo : hi;
}
static inline u32 css_oklab_rgb(int L, int a, int b) {       /* all Q12 */
    int l_ = L + ((1623 * a + 884 * b) >> 12), m_ = L - ((432 * a + 262 * b) >> 12), s_ = L - ((367 * a + 5290 * b) >> 12);
    int l = (((l_ * l_) >> 12) * l_) >> 12, m = (((m_ * m_) >> 12) * m_) >> 12, s = (((s_ * s_) >> 12) * s_) >> 12;
    int r = (16698 * l - 13548 * m + 946 * s) >> 12;
    int g = (-5195 * l + 10689 * m - 1398 * s) >> 12;
    int bl = (-17 * l - 2881 * m + 6994 * s) >> 12;
    return (u32)((css_lin_to_srgb(r) << 16) | (css_lin_to_srgb(g) << 8) | css_lin_to_srgb(bl));
}

/* One channel of a space-separated colour function: a number (x100), whether it had a %, an angle unit (0 none/deg, 1 turn, 2 rad, 3 grad) */
static inline int css_chan(const u8 **pp, const u8 *e, int *v, int *pct, int *unit, int *none) {
    const u8 *p = *pp;
    while (p < e && (css_is_ws(*p) || *p == ',' )) p++;
    if (p >= e) { *pp = p; return 0; }
    *pct = 0; *unit = 0; *none = 0;
    if (*p == '/') { p++; while (p < e && css_is_ws(*p)) p++; *none = -1; }          /* -1: this channel is the alpha that follows a slash */
    if (p + 4 <= e && css_kw(p, 4, "none")) { *v = 0; p += 4; *pp = p; if (*none == 0) *none = 1; return 1; }
    int num; u32 c = css_num(p, (u32)(e - p), &num);
    if (!c) return -1;
    p += c; *v = num;
    if (p < e && *p == '%') { *pct = 1; p++; }
    else {
        const u8 *u = p; while (p < e && dom_is_alpha(*p)) p++;
        u32 ul = (u32)(p - u);
        if (css_kw(u, ul, "turn")) *unit = 1; else if (css_kw(u, ul, "rad")) *unit = 2; else if (css_kw(u, ul, "grad")) *unit = 3;
    }
    *pp = p; return 1;
}
static inline int css_hue_deg(int v100, int unit) {          /* an angle with its unit -> whole degrees */
    if (unit == 1) return v100 * 360 / 100;
    if (unit == 2) return v100 * 5730 / 10000;
    if (unit == 3) return v100 * 9 / 1000;
    return v100 / 100;
}
static inline int css_alpha255(int v, int pct) { int a = pct ? v * 255 / 10000 : v * 255 / 100; return css_clamp255(a); }

static inline int css_parse_color(const u8 *s, u32 n, u32 *out);

/* Splits the arguments of a function at its top-level commas; returns how many (at most `max`). */
static inline int css_split_commas(const u8 *a, u32 n, u32 *st, u32 *ln, int max) {
    int k = 0, depth = 0; u32 b = 0;
    for (u32 i = 0; i <= n; i++) {
        if (i < n) { if (a[i] == '(') depth++; else if (a[i] == ')') { if (depth) depth--; } }
        if (i == n || (a[i] == ',' && depth == 0)) { if (k < max) { st[k] = b; ln[k] = i - b; k++; } b = i + 1; }
    }
    return k;
}

/* "<colour> [<percentage>]" or "<percentage> <colour>" -> the colour's span and the percentage (-1 if none) */
static inline void css_mix_arg(const u8 *a, u32 n, u32 *cs, u32 *cn, int *pct) {
    u32 b = 0, e = n;
    while (b < e && css_is_ws(a[b])) b++;
    while (e > b && css_is_ws(a[e - 1])) e--;
    *pct = -1;
    u32 k = e; while (k > b && !css_is_ws(a[k - 1])) k--;            /* last word */
    if (k > b && e > k && a[e - 1] == '%') { int v; if (css_num(a + k, e - k - 1, &v)) { *pct = v / 100; e = k; while (e > b && css_is_ws(a[e - 1])) e--; } }
    else {
        u32 f = b; while (f < e && !css_is_ws(a[f]) && a[f] != '(') f++;      /* first word */
        if (f > b && f < e && a[f - 1] == '%') { int v; if (css_num(a + b, f - b - 1, &v)) { *pct = v / 100; b = f; while (b < e && css_is_ws(a[b])) b++; } }
    }
    *cs = b; *cn = e - b;
}

/* 0 = not a colour function of ours, 1 = parsed, 2 = ours but malformed */
static inline int css_parse_color_fn(const u8 *s, u32 n, u32 *out) {
    u32 lp = 0; while (lp < n && s[lp] != '(') lp++;
    if (lp == 0 || lp >= n || s[n - 1] != ')') return 0;
    const u8 *a = s + lp + 1; u32 an = n - lp - 2;
    if (css_kw(s, lp, "light-dark")) {                                           /* we report prefers-color-scheme: light */
        u32 st[2], ln[2];
        if (css_split_commas(a, an, st, ln, 2) < 2) return 2;
        return css_parse_color(a + st[0], ln[0], out) ? 1 : 2;
    }
    if (css_kw(s, lp, "color-mix")) {
        u32 st[3], ln[3];
        if (css_split_commas(a, an, st, ln, 3) < 3) return 2;
        u32 c1s, c1n, c2s, c2n; int p1, p2;
        css_mix_arg(a + st[1], ln[1], &c1s, &c1n, &p1); css_mix_arg(a + st[2], ln[2], &c2s, &c2n, &p2);
        u32 c1, c2;
        if (!css_parse_color(a + st[1] + c1s, c1n, &c1) || !css_parse_color(a + st[2] + c2s, c2n, &c2)) return 2;
        if (p1 < 0 && p2 < 0) p1 = p2 = 50; else if (p1 < 0) p1 = 100 - p2; else if (p2 < 0) p2 = 100 - p1;
        if (p1 < 0) p1 = 0;
        if (p2 < 0) p2 = 0;
        int sum = p1 + p2; if (sum <= 0) return 2;
        int w1 = p1 * 100 / sum, w2 = 100 - w1;                                  /* normalised weights */
        if (c1 == CSS_CURRENT || c2 == CSS_CURRENT) { *out = CSS_CURRENT; return 1; }   /* cannot be resolved here */
        u32 res;
        if (c1 == CSS_NOCOLOR && c2 == CSS_NOCOLOR) { *out = CSS_NOCOLOR; return 1; }
        else if (c2 == CSS_NOCOLOR) res = css_blend_white(c1, w1 * 255 / 100);        /* the other colour is transparent: only alpha is mixed */
        else if (c1 == CSS_NOCOLOR) res = css_blend_white(c2, w2 * 255 / 100);
        else {
            int r = (int)((c1 >> 16) & 255) * w1 + (int)((c2 >> 16) & 255) * w2, g = (int)((c1 >> 8) & 255) * w1 + (int)((c2 >> 8) & 255) * w2,
                b = (int)(c1 & 255) * w1 + (int)(c2 & 255) * w2;
            res = (u32)((((r + 50) / 100) << 16) | (((g + 50) / 100) << 8) | ((b + 50) / 100));
        }
        if (sum < 100) res = css_blend_white(res, sum * 255 / 100);                   /* percentages that do not add up to 100% make it translucent */
        *out = res; return 1;
    }
    int kind = css_kw(s, lp, "oklab") ? 1 : css_kw(s, lp, "oklch") ? 2 : css_kw(s, lp, "hwb") ? 3 : css_kw(s, lp, "color") ? 4 : 0;
    if (!kind) return 0;
    const u8 *p = a, *e = a + an;
    if (kind == 4) {                                                             /* color(srgb 1 0 0 / .5): every RGB-ish space is treated as sRGB */
        while (p < e && css_is_ws(*p)) p++;
        const u8 *w = p; while (p < e && !css_is_ws(*p)) p++;
        u32 wl = (u32)(p - w);
        if (!(css_kw(w, wl, "srgb") || css_kw(w, wl, "display-p3") || css_kw(w, wl, "rec2020") || css_kw(w, wl, "a98-rgb") || css_kw(w, wl, "prophoto-rgb"))) return 2;
    }
    int v[4] = {0, 0, 0, 10000}, pc[4] = {0, 0, 0, 1}, un[4] = {0, 0, 0, 0}, nv = 0, none;
    int tv, tp, tu;
    while (nv < 4) {
        int r = css_chan(&p, e, &tv, &tp, &tu, &none);
        if (r < 0) return 2;
        if (!r) break;
        if (none == -1) nv = 3;                                                  /* after a slash: this is the alpha */
        v[nv] = tv; pc[nv] = tp; un[nv] = tu; nv++;
    }
    if (nv < 3) return 2;
    int a255 = nv == 4 ? css_alpha255(v[3], pc[3]) : 255;
    u32 rgb;
    if (kind == 4) {
        int c[3];
        for (int i = 0; i < 3; i++) c[i] = css_clamp255(pc[i] ? v[i] * 255 / 10000 : v[i] * 255 / 100);
        rgb = (u32)((c[0] << 16) | (c[1] << 8) | c[2]);
    } else if (kind == 3) {                                                      /* hwb(h w% b%) */
        int h = css_hue_deg(v[0], un[0]); int w = v[1] / 10, bk = v[2] / 10;       /* 0..1000 */
        if (w < 0) w = 0;
        if (bk < 0) bk = 0;
        if (w + bk >= 1000) { int g = css_clamp255(w * 255 / (w + bk)); rgb = (u32)((g << 16) | (g << 8) | g); }
        else {
            int r = css_hsl_comp(h + 120, 0, 1000), g = css_hsl_comp(h, 0, 1000), b = css_hsl_comp(h - 120, 0, 1000);
            int k = 1000 - w - bk;
            r = (r * k / 1000 + w) * 255 / 1000; g = (g * k / 1000 + w) * 255 / 1000; b = (b * k / 1000 + w) * 255 / 1000;
            rgb = (u32)((css_clamp255(r) << 16) | (css_clamp255(g) << 8) | css_clamp255(b));
        }
    } else {                                                                     /* oklab(L a b) / oklch(L C h) */
        int L = pc[0] ? v[0] * 4096 / 10000 : v[0] * 4096 / 100;
        int x, y;
        if (kind == 1) {
            x = pc[1] ? v[1] * 1638 / 10000 : v[1] * 4096 / 100;               /* 100% is 0.4 */
            y = pc[2] ? v[2] * 1638 / 10000 : v[2] * 4096 / 100;
        } else {
            int C = pc[1] ? v[1] * 1638 / 10000 : v[1] * 4096 / 100, h = css_hue_deg(v[2], un[2]);
            x = (C * css_cos12(h)) >> 12; y = (C * css_sin12(h)) >> 12;
        }
        rgb = css_oklab_rgb(L < 0 ? 0 : L, x, y);
    }
    if (a255 < 8) { *out = CSS_NOCOLOR; return 1; }
    *out = css_blend_white(rgb, a255);
    return 1;
}

/* returns 1 and fills *out on success */
static inline int css_parse_color(const u8 *s, u32 n, u32 *out) {
    while (n && css_is_ws(s[0])) { s++; n--; }
    while (n && css_is_ws(s[n - 1])) n--;
    if (n == 0) return 0;
    if (s[0] == '#') {
        int d[8]; u32 k = n - 1;
        if (k != 3 && k != 4 && k != 6 && k != 8) return 0;
        for (u32 i = 0; i < k; i++) { d[i] = css_hexv(s[1 + i]); if (d[i] < 0) return 0; }
        int r, g, b, a = 255;
        if (k <= 4) { r = d[0] * 17; g = d[1] * 17; b = d[2] * 17; if (k == 4) a = d[3] * 17; }
        else { r = d[0] * 16 + d[1]; g = d[2] * 16 + d[3]; b = d[4] * 16 + d[5]; if (k == 8) a = d[6] * 16 + d[7]; }
        if (a < 8) { *out = CSS_NOCOLOR; return 1; }
        *out = css_blend_white((u32)((r << 16) | (g << 8) | b), a);
        return 1;
    }
    if (css_kw(s, n, "transparent")) { *out = CSS_NOCOLOR; return 1; }
    if (css_kw(s, n, "currentcolor")) { *out = CSS_CURRENT; return 1; }
    { int fr = css_parse_color_fn(s, n, out); if (fr) return fr == 1; }
    if (n > 4 && (css_kw(s, 3, "rgb") || css_kw(s, 3, "hsl")) && (s[3] == '(' || (css_lc(s[3]) == 'a' && s[4] == '('))) {
        int hsl = css_lc(s[0]) == 'h';
        u32 i = (s[3] == '(') ? 4 : 5;
        int v[4] = {0, 0, 0, 100}; int pctf[4] = {0, 0, 0, 0}; int nv = 0;
        while (i < n && nv < 4) {
            while (i < n && (css_is_ws(s[i]) || s[i] == ',' || s[i] == '/')) i++;
            if (i >= n || s[i] == ')') break;
            int num; u32 c = css_num(s + i, n - i, &num);
            if (!c) return 0;
            i += c;
            if (i < n && s[i] == '%') { pctf[nv] = 1; i++; }
            else if (hsl && nv == 0) {                                                      /* deg, turn, rad, grad */
                u32 us = i; while (i < n && dom_is_alpha(s[i])) i++;
                if (css_kw(s + us, i - us, "turn")) num = num * 360; else if (css_kw(s + us, i - us, "rad")) num = num * 5730 / 100;
                else if (css_kw(s + us, i - us, "grad")) num = num * 9 / 10;
            }
            v[nv++] = num;
        }
        if (nv < 3) return 0;
        int r, g, b;
        if (!hsl) {
            r = pctf[0] ? (v[0] * 255 + 5000) / 10000 : v[0] / 100; g = pctf[1] ? (v[1] * 255 + 5000) / 10000 : v[1] / 100; b = pctf[2] ? (v[2] * 255 + 5000) / 10000 : v[2] / 100;   /* 50% is 128, rounded */
        } else {
            int h = v[0] / 100; int sat = v[1] / 10; int l = v[2] / 10;       /* 0..1000 */
            if (sat < 0) sat = 0;
            if (sat > 1000) sat = 1000;
            if (l < 0) l = 0;
            if (l > 1000) l = 1000;
            int m2 = l <= 500 ? l * (1000 + sat) / 1000 : l + sat - l * sat / 1000;
            int m1 = 2 * l - m2;
            r = css_hsl_comp(h + 120, m1, m2) * 255 / 1000; g = css_hsl_comp(h, m1, m2) * 255 / 1000; b = css_hsl_comp(h - 120, m1, m2) * 255 / 1000;
        }
        int a = 255;
        if (nv == 4) a = pctf[3] ? v[3] * 255 / 10000 : v[3] * 255 / 100;
        if (a < 8) { *out = CSS_NOCOLOR; return 1; }
        *out = css_blend_white((u32)((css_clamp255(r) << 16) | (css_clamp255(g) << 8) | css_clamp255(b)), css_clamp255(a));
        return 1;
    }
    for (u32 k = 0; k < sizeof(css_named) / sizeof(css_named[0]); k++)
        if (css_kw(s, n, css_named[k].name)) { *out = css_named[k].rgb; return 1; }
    return 0;
}

/* ---------- tokens inside a declaration value ---------- */
static inline int css_tok(const u8 **pp, const u8 *e, const u8 **ts, u32 *tn) {
    const u8 *p = *pp;
    while (p < e && css_is_ws(*p)) p++;
    if (p >= e) { *pp = p; return 0; }
    const u8 *st = p;
    if (*p == ',' || *p == '/') { p++; }
    else if (*p == '"' || *p == '\'') { u8 q = *p++; while (p < e && *p != q) p++; if (p < e) p++; }
    else {
        while (p < e && !css_is_ws(*p) && *p != ',' && *p != '/') {
            if (*p == '(') { int d = 0; while (p < e) { if (*p == '(') d++; else if (*p == ')') { d--; if (d == 0) { p++; break; } } p++; } }
            else p++;
        }
    }
    *ts = st; *tn = (u32)(p - st); *pp = p;
    return 1;
}

/* ---------- lengths with units ---------- */
/* Parses one token as a length/percentage/number-with-unit into *out (device px). `fpx` is the font size that
 * `em` refers to. `unitless_ok`: a bare number is accepted as px (0 always is). Returns 1 on success. */
static inline int css_calc(const u8 *s, u32 n, css_len_t *out, int fpx);

static inline int css_parse_len(const u8 *s, u32 n, css_len_t *out, int fpx, int unitless_ok) {
    if (n == 0) return 0;
    if (css_kw(s, n, "auto") || css_kw(s, n, "none") || css_kw(s, n, "normal") || css_kw(s, n, "initial") || css_kw(s, n, "inherit") || css_kw(s, n, "unset")) {
        *out = css_len_auto(); return 1;
    }
    if (n > 5 && css_kw(s, 5, "calc(")) return css_calc(s + 5, n - 5, out, fpx);
    if (n > 4 && (css_kw(s, 4, "min(") || css_kw(s, 4, "max("))) return css_calc(s + 4, n - 4, out, fpx);
    if (n > 6 && css_kw(s, 6, "clamp(")) return css_calc(s + 6, n - 6, out, fpx);
    int v; u32 c = css_num(s, n, &v);
    if (!c) return 0;
    const u8 *u = s + c; u32 un = n - c;
    int px100;
    if (un == 0) { if (v != 0 && !unitless_ok) return 0; px100 = v; }
    else if (css_kw(u, un, "px")) px100 = v;
    else if (css_kw(u, un, "em")) px100 = v * fpx;
    else if (css_kw(u, un, "rem")) px100 = v * CSS_BASE_FONT_PX;
    else if (css_kw(u, un, "ex")) px100 = v * fpx / 2;
    else if (css_kw(u, un, "ch")) px100 = v * fpx * 6 / 10;
    else if (css_kw(u, un, "pt")) px100 = v * 4 / 3;
    else if (css_kw(u, un, "pc")) px100 = v * 16;
    else if (css_kw(u, un, "in")) px100 = v * 96;
    else if (css_kw(u, un, "cm")) px100 = v * 3780 / 100;
    else if (css_kw(u, un, "mm")) px100 = v * 378 / 100;
    else if (css_kw(u, un, "q")) px100 = v * 95 / 100;
    else if (css_kw(u, un, "%")) { out->px = 0; out->pct = (short)(v / 10 > 30000 ? 30000 : v / 10 < -30000 ? -30000 : v / 10); return 1; }
    else if (css_kw(u, un, "vw")) { out->px = (short)(css_viewport_w * v / 10000); out->pct = 0; return 1; }
    else if (css_kw(u, un, "vh")) { out->px = (short)(css_viewport_h * v / 10000); out->pct = 0; return 1; }
    else if (css_kw(u, un, "vmin")) { int m = css_viewport_w < css_viewport_h ? css_viewport_w : css_viewport_h; out->px = (short)(m * v / 10000); out->pct = 0; return 1; }
    else if (css_kw(u, un, "vmax")) { int m = css_viewport_w > css_viewport_h ? css_viewport_w : css_viewport_h; out->px = (short)(m * v / 10000); out->pct = 0; return 1; }
    else return 0;
    /* CSS px (x100) -> device px, rounded to nearest */
    int neg = px100 < 0; if (neg) px100 = -px100;
    int dev = (px100 * CSS_ZOOM_NUM + (CSS_ZOOM_DEN * 100) / 2) / (CSS_ZOOM_DEN * 100);
    out->px = (short)(neg ? -dev : dev); out->pct = 0;
    return 1;
}

/* calc()/min()/max()/clamp(): enough for `calc(100% - 2em)`, `min(100%, 600px)`, `clamp(10px, 5vw, 40px)`.
 * `s` points just after the opening '('. */
static inline int css_calc(const u8 *s, u32 n, css_len_t *out, int fpx) {
    /* find the matching close paren */
    u32 depth = 1, e = 0;
    while (e < n && depth) { if (s[e] == '(') depth++; else if (s[e] == ')') { depth--; if (!depth) break; } e++; }
    n = e;
    /* which function was it? look back one char: the caller passed us the text after "calc(" etc, so we sniff
     * by checking for top-level commas (min/max/clamp) vs operators (calc) */
    int has_comma = 0; depth = 0;
    for (u32 i = 0; i < n; i++) { if (s[i] == '(') depth++; else if (s[i] == ')') depth--; else if (s[i] == ',' && depth == 0) has_comma = 1; }
    if (has_comma) {
        css_len_t best = css_len_auto(); int nargs = 0; css_len_t args[3];
        u32 i = 0;
        while (i < n && nargs < 3) {
            while (i < n && css_is_ws(s[i])) i++;
            u32 st = i; depth = 0;
            while (i < n && !(s[i] == ',' && depth == 0)) { if (s[i] == '(') depth++; else if (s[i] == ')') depth--; i++; }
            u32 en = i; while (en > st && css_is_ws(s[en - 1])) en--;
            if (!css_parse_len(s + st, en - st, &args[nargs], fpx, 1)) return 0;
            nargs++; if (i < n) i++;
        }
        if (nargs == 3) { *out = args[1]; return 1; }                  /* clamp(min, preferred, max): the preferred value */
        if (nargs == 2) {                                              /* min()/max(): prefer the pure-px operand (a cap) */
            if (args[0].pct == 0 && args[1].pct != 0) best = args[0];
            else if (args[1].pct == 0 && args[0].pct != 0) best = args[1];
            else best = args[0].px < args[1].px ? args[0] : args[1];
            *out = best; return 1;
        }
        *out = args[0]; return nargs >= 1;
    }
    css_len_t acc = css_len_px(0);
    int sign = 1; u32 i = 0; int first = 1;
    while (i < n) {
        while (i < n && css_is_ws(s[i])) i++;
        if (i >= n) break;
        if (!first) {
            if (s[i] == '+') sign = 1; else if (s[i] == '-') sign = -1; else return 0;
            i++;
            while (i < n && css_is_ws(s[i])) i++;
        }
        u32 st = i; depth = 0;
        while (i < n && !(depth == 0 && css_is_ws(s[i]))) { if (s[i] == '(') depth++; else if (s[i] == ')') depth--; i++; }
        css_len_t t;
        if (!css_parse_len(s + st, i - st, &t, fpx, 1)) return 0;
        acc.px = (short)(acc.px + sign * t.px);
        acc.pct = (short)(acc.pct + sign * (t.pct == CSS_AUTO ? 0 : t.pct));
        first = 0; sign = 1;
    }
    *out = acc; return 1;
}

/* ---------- property table ---------- */
#define CSS_PROP_LIST(X) \
 X(DISPLAY,"display") X(COLOR,"color") X(BACKGROUND,"background") X(BACKGROUND_COLOR,"background-color") \
 X(FONT,"font") X(FONT_SIZE,"font-size") X(FONT_WEIGHT,"font-weight") X(FONT_STYLE,"font-style") X(LINE_HEIGHT,"line-height") \
 X(TEXT_ALIGN,"text-align") X(TEXT_DECORATION,"text-decoration") X(TEXT_DECORATION_LINE,"text-decoration-line") \
 X(TEXT_TRANSFORM,"text-transform") X(TEXT_INDENT,"text-indent") X(WHITE_SPACE,"white-space") X(VISIBILITY,"visibility") \
 X(VERTICAL_ALIGN,"vertical-align") \
 X(MARGIN,"margin") X(MARGIN_TOP,"margin-top") X(MARGIN_RIGHT,"margin-right") X(MARGIN_BOTTOM,"margin-bottom") X(MARGIN_LEFT,"margin-left") \
 X(PADDING,"padding") X(PADDING_TOP,"padding-top") X(PADDING_RIGHT,"padding-right") X(PADDING_BOTTOM,"padding-bottom") X(PADDING_LEFT,"padding-left") \
 X(BORDER,"border") X(BORDER_TOP,"border-top") X(BORDER_RIGHT,"border-right") X(BORDER_BOTTOM,"border-bottom") X(BORDER_LEFT,"border-left") \
 X(BORDER_WIDTH,"border-width") X(BORDER_STYLE,"border-style") X(BORDER_COLOR,"border-color") \
 X(BORDER_TOP_WIDTH,"border-top-width") X(BORDER_RIGHT_WIDTH,"border-right-width") X(BORDER_BOTTOM_WIDTH,"border-bottom-width") X(BORDER_LEFT_WIDTH,"border-left-width") \
 X(BORDER_TOP_STYLE,"border-top-style") X(BORDER_RIGHT_STYLE,"border-right-style") X(BORDER_BOTTOM_STYLE,"border-bottom-style") X(BORDER_LEFT_STYLE,"border-left-style") \
 X(BORDER_TOP_COLOR,"border-top-color") X(BORDER_RIGHT_COLOR,"border-right-color") X(BORDER_BOTTOM_COLOR,"border-bottom-color") X(BORDER_LEFT_COLOR,"border-left-color") \
 X(WIDTH,"width") X(HEIGHT,"height") X(MIN_WIDTH,"min-width") X(MAX_WIDTH,"max-width") X(MIN_HEIGHT,"min-height") X(MAX_HEIGHT,"max-height") \
 X(FLOAT,"float") X(CLEAR,"clear") X(POSITION,"position") X(OVERFLOW,"overflow") X(OVERFLOW_X,"overflow-x") X(OVERFLOW_Y,"overflow-y") \
 X(BOX_SIZING,"box-sizing") X(OPACITY,"opacity") X(LIST_STYLE,"list-style") X(LIST_STYLE_TYPE,"list-style-type") \
 X(BORDER_COLLAPSE,"border-collapse") X(BORDER_SPACING,"border-spacing") \
 X(FLEX,"flex") X(FLEX_DIRECTION,"flex-direction") X(FLEX_WRAP,"flex-wrap") X(FLEX_FLOW,"flex-flow") X(FLEX_GROW,"flex-grow") \
 X(FLEX_SHRINK,"flex-shrink") X(FLEX_BASIS,"flex-basis") X(JUSTIFY_CONTENT,"justify-content") X(ALIGN_ITEMS,"align-items") \
 X(ALIGN_SELF,"align-self") X(GAP,"gap") X(ROW_GAP,"row-gap") X(COLUMN_GAP,"column-gap") X(GRID_GAP,"grid-gap") X(ORDER,"order") \
 X(GRID_TEMPLATE_COLUMNS,"grid-template-columns") X(TOP,"top") X(LEFT,"left") X(RIGHT,"right") X(BOTTOM,"bottom") \
 X(CLIP,"clip") X(CLIP_PATH,"clip-path") \
 X(BORDER_RADIUS,"border-radius") X(BOX_SHADOW,"box-shadow") X(INSET,"inset") \
 X(CONTENT,"content") X(COUNTER_RESET,"counter-reset") X(COUNTER_INCREMENT,"counter-increment") X(Z_INDEX,"z-index") \
 X(TEXT_OVERFLOW,"text-overflow") X(ASPECT_RATIO,"aspect-ratio") X(TRANSFORM,"transform") \
 X(MARGIN_INLINE,"margin-inline") X(MARGIN_BLOCK,"margin-block") X(MARGIN_INLINE_START,"margin-inline-start") X(MARGIN_INLINE_END,"margin-inline-end") \
 X(MARGIN_BLOCK_START,"margin-block-start") X(MARGIN_BLOCK_END,"margin-block-end") \
 X(PADDING_INLINE,"padding-inline") X(PADDING_BLOCK,"padding-block") X(PADDING_INLINE_START,"padding-inline-start") X(PADDING_INLINE_END,"padding-inline-end") \
 X(PADDING_BLOCK_START,"padding-block-start") X(PADDING_BLOCK_END,"padding-block-end") \
 X(INSET_INLINE,"inset-inline") X(INSET_BLOCK,"inset-block") X(INSET_INLINE_START,"inset-inline-start") X(INSET_INLINE_END,"inset-inline-end") \
 X(INSET_BLOCK_START,"inset-block-start") X(INSET_BLOCK_END,"inset-block-end") \
 X(INLINE_SIZE,"inline-size") X(BLOCK_SIZE,"block-size") X(MIN_INLINE_SIZE,"min-inline-size") X(MAX_INLINE_SIZE,"max-inline-size") \
 X(MIN_BLOCK_SIZE,"min-block-size") X(MAX_BLOCK_SIZE,"max-block-size") \
 X(BORDER_TOP_LEFT_RADIUS,"border-top-left-radius") X(BORDER_TOP_RIGHT_RADIUS,"border-top-right-radius") \
 X(BORDER_BOTTOM_LEFT_RADIUS,"border-bottom-left-radius") X(BORDER_BOTTOM_RIGHT_RADIUS,"border-bottom-right-radius")

enum {
    CP_NONE = 0,
#define X(id, nm) CP_##id,
    CSS_PROP_LIST(X)
#undef X
    CP__COUNT
};
#define CSS_CUSTOM_PROP 0xFFF0u         /* css_decl_t.prop of a per-element custom property (--name) -- never a real property id */
static const char *const css_prop_names[CP__COUNT] = {
    0,
#define X(id, nm) nm,
    CSS_PROP_LIST(X)
#undef X
};
static inline u32 css_prop_id(const u8 *s, u32 n) {
    if (n < 3 || n > 28) return CP_NONE;
    for (u32 i = 1; i < CP__COUNT; i++) {
        const char *nm = css_prop_names[i];
        u32 k = 0;
        while (k < n && nm[k] && css_lc(s[k]) == (u8)nm[k]) k++;
        if (k == n && nm[k] == 0) return i;
    }
    return CP_NONE;
}

/* ============================================================
 * selectors
 * ============================================================ */
static inline void css_reset(void) {
    css_rule_count = css_comp_count = css_decl_count = css_order = 0;
    css_comp_top = RD_CSS_COMP_MAX; css_has_pe = css_has_counters = 0;
    css_overflowed = 0;
    css_var_count = 0; css_intern_n = 0;
    css_scope_reset();
    for (u32 i = 0; i < TG__COUNT; i++) css_by_tag[i] = CSS_NIL;
    for (u32 i = 0; i < CSS_BUCKET_N; i++) { css_by_cls[i] = CSS_NIL; css_by_id[i] = CSS_NIL; }
    css_universal = CSS_NIL;
}

static inline u32 css_ident_end(const u8 *s, u32 i, u32 n) { while (i < n && css_is_ident(s[i])) i++; return i; }

/* an+b from "odd", "even", "3", "2n+1", "-n+3", "n" */
static inline int css_parse_nth(const u8 *s, u32 n, int *a, int *b) {
    while (n && css_is_ws(s[0])) { s++; n--; }
    while (n && css_is_ws(s[n - 1])) n--;
    if (css_kw(s, n, "odd")) { *a = 2; *b = 1; return 1; }
    if (css_kw(s, n, "even")) { *a = 2; *b = 0; return 1; }
    u32 npos = n;
    for (u32 i = 0; i < n; i++) if (css_lc(s[i]) == 'n') { npos = i; break; }
    if (npos == n) { int v; if (!css_num(s, n, &v)) return 0; *a = 0; *b = v / 100; return 1; }
    int av = 100;
    if (npos > 0) {
        if (npos == 1 && s[0] == '-') av = -100; else if (npos == 1 && s[0] == '+') av = 100;
        else if (!css_num(s, npos, &av)) return 0;
    }
    *a = av / 100; *b = 0;
    u32 i = npos + 1;
    while (i < n && css_is_ws(s[i])) i++;
    if (i < n) {
        int sign = 1;
        if (s[i] == '+') i++; else if (s[i] == '-') { sign = -1; i++; } else return 0;
        while (i < n && css_is_ws(s[i])) i++;
        int bv; if (!css_num(s + i, n - i, &bv)) return 0;
        *b = sign * (bv / 100);
    }
    return 1;
}

/* ---- identifiers with CSS escapes: .md\:flex  .w-1\/2  #\31 0 (Tailwind and friends live on these) ---- */
static inline u32 css_ident_end_e(const u8 *s, u32 i, u32 n) {
    while (i < n) {
        if (s[i] == '\\' && i + 1 < n) {
            i++;
            if (css_hexv(s[i]) >= 0) { int k = 0; while (i < n && k < 6 && css_hexv(s[i]) >= 0) { i++; k++; } if (i < n && css_is_ws(s[i])) i++; }
            else i++;
            continue;
        }
        if (css_is_ident(s[i])) i++; else break;
    }
    return i;
}
static inline u32 css_hash_e(const u8 *s, u32 n) {              /* css_hash() of the identifier with its escapes resolved */
    u32 h = 2166136261u;
    for (u32 i = 0; i < n; ) {
        u32 ch;
        if (s[i] == '\\' && i + 1 < n) {
            i++;
            if (css_hexv(s[i]) >= 0) { ch = 0; int k = 0; while (i < n && k < 6 && css_hexv(s[i]) >= 0) { ch = ch * 16 + (u32)css_hexv(s[i]); i++; k++; } if (i < n && css_is_ws(s[i])) i++; }
            else ch = s[i++];
        } else ch = s[i++];
        h ^= (ch & 0xFF); h *= 16777619u;
    }
    return h ? h : 1;
}

typedef struct { u32 ids, cls, tags; } css_spec_t;
static inline u32 css_spec_val(const css_spec_t *p) { return (p->ids > 255 ? 255u : p->ids) << 16 | (p->cls > 255 ? 255u : p->cls) << 8 | (p->tags > 255 ? 255u : p->tags); }

/* pseudo-classes that need a pointer, a focus, a history or a fullscreen to be true: on a static page they are never true */
static inline int css_pseudo_never(const u8 *nm, u32 ln) {
    static const char *const names[] = { "hover", "focus", "active", "visited", "focus-within", "focus-visible", "target", "target-within", "fullscreen",
        "modal", "popover-open", "picture-in-picture", "indeterminate", "invalid", "user-invalid", "user-valid", "autofill", "-webkit-autofill", "default",
        "current", "past", "future", "playing", "paused", "local-link", "out-of-range", "-moz-focusring", "open-in-new" };
    for (u32 i = 0; i < sizeof(names) / sizeof(names[0]); i++) if (css_kw(nm, ln, names[i])) return 1;
    return 0;
}

static int css_parse_compound(const u8 *s, u32 n, u32 *pi, css_comp_t *c, css_spec_t *sp, int depth);

/* Parses the argument text of :is()/:where()/:not()/:has() (a comma list of compounds) into a descriptor chained onto `host`. */
static inline int css_parse_group(const u8 *a, u32 al, int kind, int where, css_comp_t *host, css_spec_t *sp, int depth) {
    css_comp_t tmp[12]; int nm = 0; css_spec_t best = {0, 0, 0}; u32 bestv = 0;
    if (depth >= 3) return 0;
    u32 i = 0;
    while (i < al) {
        while (i < al && css_is_ws(a[i])) i++;
        if (i >= al) break;
        if (nm >= 12) return 0;
        css_comp_t *m = &tmp[nm];
        u8 *z = (u8 *)m; for (u32 k = 0; k < sizeof(*m); k++) z[k] = 0;
        if (kind == GK_HAS) {                                    /* relative selector: [>+~] compound */
            m->comb = ' ';
            if (a[i] == '>' || a[i] == '+' || a[i] == '~') { m->comb = a[i]; i++; while (i < al && css_is_ws(a[i])) i++; }
        }
        css_spec_t ms = {0, 0, 0};
        if (!css_parse_compound(a, al, &i, m, &ms, depth + 1)) return 0;
        if (m->pe) return 0;
        u32 v = css_spec_val(&ms);
        if (nm == 0 || v > bestv) { best = ms; bestv = v; }
        nm++;
        while (i < al && css_is_ws(a[i])) i++;
        if (i < al) { if (a[i] == ',') i++; else return 0; }      /* a combinator inside the argument: not supported */
    }
    if (nm == 0) return 0;
    if (css_comp_top < css_comp_count + (u32)nm + 1) return 0;
    css_comp_top -= (u32)nm;
    for (int k = 0; k < nm; k++) RD_CSS_COMPS[css_comp_top + (u32)k] = tmp[k];
    u32 mfirst = css_comp_top;
    css_comp_top -= 1;
    css_comp_t *d = &RD_CSS_COMPS[css_comp_top];
    u8 *z = (u8 *)d; for (u32 k = 0; k < sizeof(*d); k++) z[k] = 0;
    d->grp = mfirst; d->alt_n = (u8)nm; d->alt_kind = (u8)kind; d->attr_voff = 0xFFFFFFFFu;
    if (!(host->pseudo & PS_GROUP)) { host->pseudo |= PS_GROUP; host->grp = css_comp_top; }
    else { u32 g = host->grp; while (RD_CSS_COMPS[g].attr_voff != 0xFFFFFFFFu) g = RD_CSS_COMPS[g].attr_voff; RD_CSS_COMPS[g].attr_voff = css_comp_top; }
    if (!where) { sp->ids += best.ids; sp->cls += best.cls; sp->tags += best.tags; }       /* :is/:not/:has take their most specific argument; :where() is free */
    return 1;
}

/* Parses one compound selector at s[*pi..n): stops at whitespace, a combinator, a comma or the end. 0 = unsupported. */
static int css_parse_compound(const u8 *s, u32 n, u32 *pi, css_comp_t *c, css_spec_t *sp, int depth) {
    u32 i = *pi; int any = 0;
    for (;;) {
        if (i >= n) break;
        u8 ch = s[i];
        if (ch == '*') { i++; any = 1; continue; }
        if (ch == '|' ) return 0;                                           /* namespaces */
        if (css_is_ident(ch) && !css_is_digit(ch) && !any) {
            u32 e = css_ident_end(s, i, n);
            u8 nm[16]; u32 ln = e - i; if (ln > 15) ln = 15;
            for (u32 k = 0; k < ln; k++) nm[k] = css_lc(s[i + k]);
            u32 t = dom_tag_id(nm, ln);
            if (t == TG_UNKNOWN) return 0;
            c->tag = (u16)t; sp->tags++; i = e; any = 1; continue;
        }
        if (ch == '.') {
            u32 e = css_ident_end_e(s, i + 1, n);
            if (e == i + 1) return 0;
            if (c->ncls < 2) c->cls[c->ncls++] = css_hash_e(s + i + 1, e - i - 1);
            sp->cls++; i = e; any = 1; continue;
        }
        if (ch == '#') {
            u32 e = css_ident_end_e(s, i + 1, n);
            if (e == i + 1) return 0;
            c->id_hash = css_hash_e(s + i + 1, e - i - 1); sp->ids++; i = e; any = 1; continue;
        }
        if (ch == '[') {
            u32 e = i + 1, nst = e;
            while (e < n && s[e] != ']') { if (s[e] == '"' || s[e] == '\'') { u8 q = s[e++]; while (e < n && s[e] != q) e++; } e++; }
            if (e >= n) return 0;
            u32 j = nst; while (j < e && css_is_ws(s[j])) j++;
            u32 ae = j; while (ae < e && css_is_ident(s[ae])) ae++;
            u8 nm[16]; u32 ln = ae - j; if (ln > 15) ln = 15;
            for (u32 k = 0; k < ln; k++) nm[k] = css_lc(s[j + k]);
            u32 aid = dom_attr_id(nm, ln);
            if (aid == AT_NONE) return 0;
            if (c->attr_id) return 0;                                       /* two attribute tests on one compound: one slot only */
            c->attr_id = (u8)aid;
            while (ae < e && css_is_ws(s[ae])) ae++;
            if (ae >= e) c->attr_op = AOP_EXISTS;
            else {
                u8 op = s[ae];
                if (op == '=') { c->attr_op = AOP_EQ; ae++; }
                else if (ae + 1 < e && s[ae + 1] == '=') {
                    c->attr_op = op == '~' ? AOP_WORD : op == '|' ? AOP_DASH : op == '^' ? AOP_PREFIX : op == '$' ? AOP_SUFFIX : op == '*' ? AOP_SUBSTR : 0;
                    ae += 2;
                }
                if (!c->attr_op) return 0;
                while (ae < e && css_is_ws(s[ae])) ae++;
                u32 vs = ae, ve;
                if (ae < e && (s[ae] == '"' || s[ae] == '\'')) { u8 q = s[ae++]; vs = ae; while (ae < e && s[ae] != q) ae++; ve = ae; if (ae < e) ae++; }
                else { while (ae < e && !css_is_ws(s[ae])) ae++; ve = ae; }
                while (ae < e && css_is_ws(s[ae])) ae++;
                if (ae < e && (s[ae] == 'i' || s[ae] == 'I')) c->attr_icase = 1;
                c->attr_voff = (u32)(s + vs - RD_POOL); c->attr_vlen = (u16)(ve - vs);
            }
            sp->cls++; i = e + 1; any = 1; continue;
        }
        if (ch == ':') {
            int dbl = (i + 1 < n && s[i + 1] == ':');
            u32 st = i + 1 + (dbl ? 1 : 0);
            u32 e = css_ident_end(s, st, n);
            u32 ln = e - st;
            const u8 *nm = s + st;
            u32 as = 0, ae2 = 0; int has_arg = 0;
            if (e < n && s[e] == '(') {
                u32 d = 1; as = e + 1; u32 k = as;
                while (k < n && d) { if (s[k] == '(') d++; else if (s[k] == ')') d--; k++; }
                if (d) return 0;
                ae2 = k - 1; has_arg = 1; e = k;
            }
            if (!has_arg && (css_kw(nm, ln, "before") || css_kw(nm, ln, "after"))) {      /* ::before / ::after (and the CSS2 single-colon spelling) */
                if (c->pe) return 0;
                c->pe = css_kw(nm, ln, "before") ? PE_BEFORE : PE_AFTER; sp->tags++;
                i = e; any = 1; continue;
            }
            if (dbl) return 0;                                              /* ::marker ::selection ::placeholder ::-webkit-...: not drawn */
            if (css_kw(nm, ln, "first-child")) c->pseudo |= PS_FIRST;
            else if (css_kw(nm, ln, "last-child")) c->pseudo |= PS_LAST;
            else if (css_kw(nm, ln, "only-child")) c->pseudo |= PS_ONLY;
            else if (css_kw(nm, ln, "first-of-type")) c->pseudo |= PS_TFIRST;
            else if (css_kw(nm, ln, "last-of-type")) c->pseudo |= PS_TLAST;
            else if (css_kw(nm, ln, "only-of-type")) c->pseudo |= PS_TONLY;
            else if (css_kw(nm, ln, "root") || css_kw(nm, ln, "scope")) c->pseudo |= PS_ROOT;
            else if (css_kw(nm, ln, "empty")) c->pseudo |= PS_EMPTY;
            else if (css_kw(nm, ln, "link") || css_kw(nm, ln, "any-link")) c->pseudo |= PS_LINK;
            else if (css_kw(nm, ln, "checked")) c->pseudo |= PS_CHECKED;
            else if (css_kw(nm, ln, "disabled")) c->pseudo |= PS_DISABLED;
            else if (css_kw(nm, ln, "enabled")) c->pseudo |= PS_ENABLED;
            else if (css_kw(nm, ln, "placeholder-shown")) c->pseudo |= PS_PHSHOWN;
            else if (css_kw(nm, ln, "read-only")) c->pseudo |= PS_READONLY;
            else if (css_kw(nm, ln, "defined") || css_kw(nm, ln, "valid") || css_kw(nm, ln, "in-range") || css_kw(nm, ln, "optional")) { }   /* true of every element we have */
            else if (has_arg && (css_kw(nm, ln, "nth-child") || css_kw(nm, ln, "nth-of-type") || css_kw(nm, ln, "nth-last-child") || css_kw(nm, ln, "nth-last-of-type"))) {
                int a, b;
                u32 al2 = ae2 - as;
                for (u32 k = 0; k + 1 < al2; k++) if (css_lc(s[as + k]) == 'o' && css_lc(s[as + k + 1]) == 'f' && k > 0 && css_is_ws(s[as + k - 1])) { al2 = k; break; }   /* "2n+1 of .x": the filter is ignored */
                if (!css_parse_nth(s + as, al2, &a, &b) || a > 100 || a < -100 || b > 100 || b < -100) return 0;
                c->pseudo |= PS_NTH; c->nth_a = (signed char)a; c->nth_b = (signed char)b;
                if (css_kw(nm, ln, "nth-of-type") || css_kw(nm, ln, "nth-last-of-type")) c->pseudo |= PS_NTHT;
                if (css_kw(nm, ln, "nth-last-child") || css_kw(nm, ln, "nth-last-of-type")) c->pseudo |= PS_NTHL;
            } else if (has_arg && (css_kw(nm, ln, "is") || css_kw(nm, ln, "matches") || css_kw(nm, ln, "-webkit-any") || css_kw(nm, ln, "-moz-any"))) {
                if (!css_parse_group(s + as, ae2 - as, GK_IS, 0, c, sp, depth)) return 0;
            } else if (has_arg && css_kw(nm, ln, "where")) {
                if (!css_parse_group(s + as, ae2 - as, GK_IS, 1, c, sp, depth)) return 0;
            } else if (has_arg && css_kw(nm, ln, "not")) {
                if (!css_parse_group(s + as, ae2 - as, GK_NOT, 0, c, sp, depth)) return 0;
            } else if (has_arg && css_kw(nm, ln, "has")) {
                if (!css_parse_group(s + as, ae2 - as, GK_HAS, 0, c, sp, depth)) return 0;
            } else if (has_arg && (css_kw(nm, ln, "dir"))) {
                if (!css_kw(s + as, ae2 - as, "ltr")) c->never = 1;
            } else if (css_pseudo_never(nm, ln)) c->never = 1;
            else return 0;                                                  /* :lang() :host ... unsupported: the rule is dropped */
            sp->cls++; i = e; any = 1; continue;
        }
        break;
    }
    if (!any) return 0;
    *pi = i;
    return 1;
}

/* Parses ONE complex selector (no commas) into compounds at the end of RD_CSS_COMPS. Returns the number of
 * compounds, or 0 if it uses something unsupported (the whole rule is then dropped). *pe_out = ::before/::after of the last compound. */
static inline int css_parse_selector(const u8 *s, u32 n, u32 *spec_out, u8 *pe_out) {
    u32 first = css_comp_count, top0 = css_comp_top, i = 0; int ncomp = 0; u8 comb = 0;
    css_spec_t sp = {0, 0, 0};
    *pe_out = 0;
    while (i < n && css_is_ws(s[i])) i++;
    while (n && css_is_ws(s[n - 1])) n--;
    while (i < n) {
        if (css_comp_count + 1 >= css_comp_top) goto fail;
        css_comp_t c;
        u8 *z = (u8 *)&c; for (u32 k = 0; k < sizeof(c); k++) z[k] = 0;
        c.comb = comb;
        if (!css_parse_compound(s, n, &i, &c, &sp, 0)) goto fail;
        if (c.never) goto fail;                                              /* a hover rule on a static page: nothing to do */
        RD_CSS_COMPS[css_comp_count++] = c; ncomp++;
        if (c.pe) { if (i < n) goto fail; *pe_out = c.pe; }                  /* ::before must end the selector */
        int saw_ws = 0;
        while (i < n && css_is_ws(s[i])) { i++; saw_ws = 1; }
        if (i >= n) break;
        if (s[i] == '>' || s[i] == '+' || s[i] == '~') { comb = s[i]; i++; while (i < n && css_is_ws(s[i])) i++; }
        else if (saw_ws) comb = ' ';
        else goto fail;
    }
    if (ncomp == 0) goto fail;
    *spec_out = css_spec_val(&sp);
    return ncomp;
fail:
    css_comp_count = first; css_comp_top = top0;
    return 0;
}

static inline void css_bucket_add(u32 ri) {
    css_rule_t *r = &RD_CSS_RULES[ri];
    css_comp_t *last = &RD_CSS_COMPS[r->comp_first + r->ncomp - 1];
    u16 *head;
    if (last->id_hash) { r->key_kind = 3; r->key = last->id_hash; head = &css_by_id[last->id_hash & (CSS_BUCKET_N - 1)]; }
    else if (last->ncls) { r->key_kind = 2; r->key = last->cls[0]; head = &css_by_cls[last->cls[0] & (CSS_BUCKET_N - 1)]; }
    else if (last->tag) { r->key_kind = 1; r->key = last->tag; head = &css_by_tag[last->tag]; }
    else { r->key_kind = 0; r->key = 0; head = &css_universal; }
    r->next = *head; *head = (u16)ri;
}

/* ============================================================
 * declarations & sheets
 * ============================================================ */
/* Walks `name: value [!important];` items in base[*pi..end). Returns 0 when there are no more. */
static inline int css_next_decl(const u8 *base, u32 *pi, u32 end, u32 *ns, u32 *nn, u32 *vs, u32 *vn, int *imp) {
    u32 i = *pi;
    for (;;) {
        while (i < end && (css_is_ws(base[i]) || base[i] == ';')) i++;
        if (i >= end) { *pi = i; return 0; }
        u32 ds = i, colon = end; int depth = 0; u8 q = 0; u32 stop = end;
        for (u32 k = i; k < end; k++) {
            u8 c = base[k];
            if (q) { if (c == q) q = 0; continue; }
            if (c == '"' || c == '\'') q = c;
            else if (c == '(') depth++;
            else if (c == ')') { if (depth) depth--; }
            else if (c == ':' && colon == end && depth == 0) colon = k;
            else if (c == ';' && depth == 0) { stop = k; break; }
            else if (c == '{' && depth == 0) {                         /* CSS nesting: `& a { ... }` is a rule, not a declaration -- skip all of it */
                int bd = 0; u8 bq = 0; u32 m = k;
                for (; m < end; m++) {
                    u8 d = base[m];
                    if (bq) { if (d == bq) bq = 0; continue; }
                    if (d == '"' || d == '\'') bq = d; else if (d == '{') bd++; else if (d == '}' && --bd == 0) break;
                }
                colon = end; stop = m < end ? m + 1 : end; break;
            }
        }
        i = stop;
        if (colon >= stop) continue;
        u32 a = ds, b = colon;
        while (b > a && css_is_ws(base[b - 1])) b--;
        u32 v0 = colon + 1, v1 = stop;
        while (v0 < v1 && css_is_ws(base[v0])) v0++;
        while (v1 > v0 && css_is_ws(base[v1 - 1])) v1--;
        *imp = 0;
        if (v1 - v0 >= 10) {                                  /* strip a trailing !important */
            u32 k = v1;
            while (k > v0 && dom_is_alpha(base[k - 1])) k--;
            if (k > v0 + 1 && base[k - 1] == '!' && css_kw(base + k, v1 - k, "important")) {
                *imp = 1; v1 = k - 1;
                while (v1 > v0 && css_is_ws(base[v1 - 1])) v1--;
            }
        }
        *ns = a; *nn = b - a; *vs = v0; *vn = v1 - v0; *pi = i;
        return 1;
    }
}

static inline u32 css_match_brace(const u8 *b, u32 open, u32 end) {
    u32 d = 0; u8 q = 0;
    for (u32 i = open; i < end; i++) {
        u8 c = b[i];
        if (q) { if (c == q) q = 0; continue; }
        if (c == '"' || c == '\'') q = c;
        else if (c == '{') d++;
        else if (c == '}') { if (--d == 0) return i; }
    }
    return end;
}

static inline int css_is_rootish(u32 first, int ncomp) {
    if (ncomp != 1) return 0;
    css_comp_t *c = &RD_CSS_COMPS[first];
    if (c->pseudo & PS_ROOT) return 1;
    if (c->tag == TG_HTML || c->tag == TG_BODY) return !c->ncls && !c->id_hash;
    return c->tag == 0 && !c->ncls && !c->id_hash && !c->pseudo && !c->attr_id;
}

static inline int css_media_eval(const u8 *s, u32 n);
static inline void css_add_rule(u8 *b, u32 ss, u32 se, u32 ds, u32 de, int origin);

/* ---- CSS nesting: .card { color: red; & h3 { ... } .dark & { ... } > p { ... } @media (...) { ... } } ----
 * A nested rule is turned into an ordinary one by writing its full selector into the pool (the parent's text stands in for each
 * `&`; with no `&` the parent is prepended as an ancestor) and handing that to css_add_rule(), which means all the matching, the
 * specificity and the :is() grouping machinery are the existing ones. A parent that is a selector list is wrapped in :is(). */
static int css_nest_depth;
static inline int css_has_top_comma(const u8 *s, u32 n) {
    int depth = 0; u8 q = 0;
    for (u32 i = 0; i < n; i++) {
        u8 c = s[i];
        if (q) { if (c == q) q = 0; }
        else if (c == '"' || c == '\'') q = c;
        else if (c == '(' || c == '[') depth++;
        else if (c == ')' || c == ']') { if (depth) depth--; }
        else if (c == ',' && depth == 0) return 1;
    }
    return 0;
}
static inline void css_add_nested(u8 *b, u32 ss, u32 se, u32 ds, u32 de, int origin) {
    if (css_nest_depth >= 4) return;
    u32 i = ds, seg = ds; int depth = 0; u8 q = 0;
    for (; i < de; i++) {
        u8 c = b[i];
        if (q) { if (c == q) q = 0; continue; }
        if (c == '"' || c == '\'') q = c;
        else if (c == '(' || c == '[') depth++;
        else if (c == ')' || c == ']') { if (depth) depth--; }
        else if (c == ';' && depth == 0) seg = i + 1;
        else if (c == '{' && depth == 0) {
            u32 k = css_match_brace(b, i, de);
            u32 ps = seg, pn = i;
            while (ps < pn && css_is_ws(b[ps])) ps++;
            while (pn > ps && css_is_ws(b[pn - 1])) pn--;
            u32 body0 = i + 1, body1 = k;
            i = k; seg = k + 1;
            if (pn <= ps) continue;
            if (b[ps] == '@') {                                              /* @media / @supports / @container / @layer inside a rule */
                u32 ne = css_ident_end(b, ps + 1, pn), nl = ne - ps - 1; int ok = 0;
                if (css_kw(b + ps + 1, nl, "media")) ok = css_media_eval(b + ne, pn - ne);
                else if (css_kw(b + ps + 1, nl, "container")) { u32 qq = ne; while (qq < pn && b[qq] != '(') qq++; ok = css_media_eval(b + qq, pn - qq); }
                else if (css_kw(b + ps + 1, nl, "supports") || css_kw(b + ps + 1, nl, "layer") || css_kw(b + ps + 1, nl, "scope")) ok = 1;
                if (ok) { css_nest_depth++; css_add_rule(b, ss, se, body0, body1, origin); css_nest_depth--; }
                continue;
            }
            /* the parent selector, trimmed */
            u32 pa = ss, pz = se;
            while (pa < pz && css_is_ws(b[pa])) pa++;
            while (pz > pa && css_is_ws(b[pz - 1])) pz--;
            if (pz <= pa) continue;
            int wrap = css_has_top_comma(b + pa, pz - pa);
            u32 start = dom_pool_len, limit = start + 700;
            if (limit + 16 >= RD_POOL_SIZE) { css_overflowed = 1; continue; }
            u32 o = start; int ok = 1;
            u32 cs = ps;
            while (cs <= pn && ok) {                                         /* each selector of the nested rule's own list */
                u32 ce = cs; int d2 = 0; u8 q2 = 0;
                while (ce < pn) {
                    u8 d = b[ce];
                    if (q2) { if (d == q2) q2 = 0; }
                    else if (d == '"' || d == '\'') q2 = d;
                    else if (d == '(' || d == '[') d2++;
                    else if (d == ')' || d == ']') { if (d2) d2--; }
                    else if (d == ',' && d2 == 0) break;
                    ce++;
                }
                u32 a = cs, z = ce;
                while (a < z && css_is_ws(b[a])) a++;
                while (z > a && css_is_ws(b[z - 1])) z--;
                if (z > a) {
                    int has_amp = 0;
                    for (u32 k2 = a; k2 < z; k2++) if (b[k2] == '&') { has_amp = 1; break; }
                    if (o > start) RD_POOL[o++] = ',';
                    u32 need = (z - a) + (pz - pa + 4) * (has_amp ? 4 : 1) + 4;
                    if (o + need >= limit) { ok = 0; break; }
                    #define PUT_PARENT() do { if (wrap) { RD_POOL[o++] = ':'; RD_POOL[o++] = 'i'; RD_POOL[o++] = 's'; RD_POOL[o++] = '('; } \
                        { for (u32 k3 = pa; k3 < pz; k3++) { RD_POOL[o++] = b[k3]; } } if (wrap) { RD_POOL[o++] = ')'; } } while (0)
                    if (has_amp) { for (u32 k2 = a; k2 < z; k2++) { if (b[k2] == '&') PUT_PARENT(); else RD_POOL[o++] = b[k2]; } }
                    else { PUT_PARENT(); RD_POOL[o++] = ' '; for (u32 k2 = a; k2 < z; k2++) RD_POOL[o++] = b[k2]; }
                    #undef PUT_PARENT
                }
                cs = ce + 1;
            }
            if (!ok || o == start) { css_overflowed = ok ? css_overflowed : 1; continue; }
            dom_pool_len = o; RD_POOL[dom_pool_len++] = ' ';
            u32 bo = (u32)(b - RD_POOL);
            css_nest_depth++; css_add_rule(RD_POOL, start, o, bo + body0, bo + body1, origin); css_nest_depth--;
        }
    }
}

static inline void css_add_rule(u8 *b, u32 ss, u32 se, u32 ds, u32 de, int origin) {
    u32 r0 = css_rule_count; int rootish = 0;
    u32 i = ss;
    while (i < se) {
        u32 st = i; int depth = 0; u8 q = 0;
        while (i < se) {
            u8 c = b[i];
            if (q) { if (c == q) q = 0; }
            else if (c == '"' || c == '\'') q = c;
            else if (c == '(' || c == '[') depth++;
            else if (c == ')' || c == ']') { if (depth) depth--; }
            else if (c == ',' && depth == 0) break;
            i++;
        }
        u32 en = i; if (i < se) i++;
        u32 spec; u8 pe;
        u32 cf = css_comp_count;
        int nc = css_parse_selector(b + st, en - st, &spec, &pe);
        if (nc > 0 && css_rule_count < RD_CSS_RULE_MAX) {
            css_rule_t *r = &RD_CSS_RULES[css_rule_count];
            r->comp_first = cf; r->ncomp = (u8)nc; r->spec = spec | ((u32)origin << 28);
            r->order = css_order++; r->ndecl = 0; r->decl_first = 0; r->next = CSS_NIL;
            r->pe = pe; r->pad[0] = r->pad[1] = r->pad[2] = 0;
            if (pe) css_has_pe = 1;
            else if (css_is_rootish(cf, nc)) rootish = 1;
            css_rule_count++;
        } else if (nc > 0) { css_comp_count = cf; css_overflowed = 1; }   /* rule table full: this rule is dropped */
    }
    if (css_rule_count == r0) return;
    u32 d0 = css_decl_count, bo = (u32)(b - RD_POOL);       /* value offsets are stored pool-absolute */
    u32 p = ds, ns, nn, vs, vn; int imp;
    while (css_next_decl(b, &p, de, &ns, &nn, &vs, &vn, &imp)) {
        if (nn > 2 && b[ns] == '-' && b[ns + 1] == '-') {                 /* custom property */
            if (rootish) css_var_set(css_hash(b + ns, nn), bo + vs, vn);
            else if (nn < 256 && vs - ns < 256 && vn < 0x1000000u && css_decl_count < RD_CSS_DECL_MAX) {
                /* per-element custom property: the name and the value are both still in the pool, so the declaration only
                 * has to say where. voff = name, pad = name length, vlen = (value length << 8) | (distance name -> value). */
                css_decl_t *d = &RD_CSS_DECLS[css_decl_count++];
                d->prop = CSS_CUSTOM_PROP; d->imp = (u8)imp; d->pad = (u8)nn; d->voff = bo + ns; d->vlen = (vn << 8) | (vs - ns);
            }
            continue;
        }
        u32 pid = css_prop_id(b + ns, nn);
        if (pid == CP_NONE || css_decl_count >= RD_CSS_DECL_MAX) continue;
        if (pid == CP_COUNTER_RESET || pid == CP_COUNTER_INCREMENT) css_has_counters = 1;
        css_decl_t *d = &RD_CSS_DECLS[css_decl_count++];
        d->prop = (u16)pid; d->imp = (u8)imp; d->pad = 0; d->voff = bo + vs; d->vlen = vn;
    }
    u32 nd = css_decl_count - d0;
    for (u32 r = r0; r < css_rule_count; r++) {
        RD_CSS_RULES[r].decl_first = d0; RD_CSS_RULES[r].ndecl = (u16)(nd > 65535 ? 65535 : nd);
        if (nd) css_bucket_add(r);                                   /* a rule that only has nested rules inside has nothing to match for */
    }
    css_add_nested(b, ss, se, ds, de, origin);
}

/* ---- @media ---- */
static inline int css_media_feature(const u8 *s, u32 n) {         /* s = inside the parentheses */
    while (n && css_is_ws(s[0])) { s++; n--; }
    while (n && css_is_ws(s[n - 1])) n--;
    int vw = css_viewport_w * CSS_ZOOM_DEN / CSS_ZOOM_NUM, vh = css_viewport_h * CSS_ZOOM_DEN / CSS_ZOOM_NUM;
    /* range syntax: (width >= 600px) */
    for (u32 i = 0; i + 1 < n; i++) {
        if (s[i] == '<' || s[i] == '>') {
            int lt = s[i] == '<', eq = (s[i + 1] == '='); u32 vs = i + 1 + (eq ? 1 : 0);
            u32 e = i; while (e && css_is_ws(s[e - 1])) e--;
            int dim = (css_kw(s, e, "width") || css_kw(s, e, "inline-size")) ? vw : (css_kw(s, e, "height") || css_kw(s, e, "block-size")) ? vh : -1;
            if (dim < 0) return 0;
            while (vs < n && css_is_ws(s[vs])) vs++;
            int v; u32 c = css_num(s + vs, n - vs, &v); if (!c) return 0;
            int px = v / 100; if (css_kw(s + vs + c, n - vs - c, "em") || css_kw(s + vs + c, n - vs - c, "rem")) px = v * 16 / 100;
            return lt ? (eq ? dim <= px : dim < px) : (eq ? dim >= px : dim > px);
        }
    }
    u32 colon = n; for (u32 i = 0; i < n; i++) if (s[i] == ':') { colon = i; break; }
    u32 fe = colon; while (fe > 0 && css_is_ws(s[fe - 1])) fe--;
    const u8 *val = s + (colon < n ? colon + 1 : n); u32 vl = colon < n ? n - colon - 1 : 0;
    while (vl && css_is_ws(val[0])) { val++; vl--; }
    if (css_kw(s, fe, "color") && colon == n) return 1;
    if (colon == n) return 0;
    int v = 0; u32 c = css_num(val, vl, &v);
    int px = 0;
    if (c) { px = v / 100; if (css_kw(val + c, vl - c, "em") || css_kw(val + c, vl - c, "rem")) px = v * 16 / 100; }
    if (css_kw(s, fe, "min-width") || css_kw(s, fe, "min-inline-size")) return c && vw >= px;
    if (css_kw(s, fe, "max-width") || css_kw(s, fe, "max-inline-size")) return c && vw <= px;
    if (css_kw(s, fe, "width") || css_kw(s, fe, "inline-size")) return c && vw == px;
    if (css_kw(s, fe, "min-height") || css_kw(s, fe, "min-block-size")) return c && vh >= px;
    if (css_kw(s, fe, "max-height") || css_kw(s, fe, "max-block-size")) return c && vh <= px;
    if (css_kw(s, fe, "orientation")) return css_kw(val, vl, "landscape") ? vw >= vh : vw < vh;
    if (css_kw(s, fe, "prefers-color-scheme")) return css_kw(val, vl, "light");
    if (css_kw(s, fe, "prefers-reduced-motion")) return css_kw(val, vl, "no-preference");
    if (css_kw(s, fe, "hover") || css_kw(s, fe, "any-hover")) return css_kw(val, vl, "hover");
    if (css_kw(s, fe, "pointer") || css_kw(s, fe, "any-pointer")) return css_kw(val, vl, "fine");
    if (css_kw(s, fe, "forced-colors")) return css_kw(val, vl, "none");
    return 0;
}
static inline int css_media_eval(const u8 *s, u32 n) {
    u32 i = 0;
    if (n == 0) return 1;
    while (i <= n) {
        u32 st = i; int depth = 0;
        while (i < n && !(s[i] == ',' && depth == 0)) { if (s[i] == '(') depth++; else if (s[i] == ')') depth--; i++; }
        u32 en = i; i++;
        /* one query */
        u32 k = st; int neg = 0, ok = 1, saw_type = 0;
        while (k < en) {
            while (k < en && css_is_ws(s[k])) k++;
            if (k >= en) break;
            if (s[k] == '(') {
                u32 d = 1, e = k + 1; while (e < en && d) { if (s[e] == '(') d++; else if (s[e] == ')') d--; e++; }
                if (!css_media_feature(s + k + 1, e - k - 2)) ok = 0;
                k = e; continue;
            }
            u32 e = k; while (e < en && !css_is_ws(s[e]) && s[e] != '(') e++;
            if (css_kw(s + k, e - k, "not")) neg = 1;
            else if (css_kw(s + k, e - k, "only") || css_kw(s + k, e - k, "and")) { }
            else if (css_kw(s + k, e - k, "all") || css_kw(s + k, e - k, "screen")) saw_type = 1;
            else { ok = 0; saw_type = 1; }                                        /* print, speech, tv... */
            k = e;
        }
        (void)saw_type;
        if (neg) ok = !ok;
        if (ok) return 1;
        if (i > n) break;
    }
    return 0;
}

static inline void css_parse_range(u8 *b, u32 i, u32 end, int origin, int depth) {
    while (i < end) {
        while (i < end && (css_is_ws(b[i]) || b[i] == '}' || b[i] == ';')) i++;
        if (i >= end) break;
        if (b[i] == '@') {
            u32 ne = css_ident_end(b, i + 1, end);
            u32 j = ne;
            while (j < end && b[j] != '{' && b[j] != ';') j++;
            if (j >= end) break;
            if (b[j] == ';') { i = j + 1; continue; }
            u32 k = css_match_brace(b, j, end);
            u32 nl = ne - i - 1;
            if (depth < 4) {
                if (css_kw(b + i + 1, nl, "media")) { if (css_media_eval(b + ne, j - ne)) css_parse_range(b, j + 1, k, origin, depth + 1); }
                else if (css_kw(b + i + 1, nl, "container")) {          /* @container [name] (min-width: 400px): judged against the viewport -- there are no container boxes at parse time */
                    u32 qq = ne; while (qq < j && b[qq] != '(') qq++;
                    if (css_media_eval(b + qq, j - qq)) css_parse_range(b, j + 1, k, origin, depth + 1);
                }
                else if (css_kw(b + i + 1, nl, "supports") || css_kw(b + i + 1, nl, "layer") || css_kw(b + i + 1, nl, "document") || css_kw(b + i + 1, nl, "scope"))
                    css_parse_range(b, j + 1, k, origin, depth + 1);
            }
            i = k + 1; continue;
        }
        u32 j = i; u8 q = 0;
        while (j < end) { u8 c = b[j]; if (q) { if (c == q) q = 0; } else if (c == '"' || c == '\'') q = c; else if (c == '{') break; j++; }
        if (j >= end) break;
        u32 k = css_match_brace(b, j, end);
        css_add_rule(b, i, j, j + 1, k, origin);
        i = k + 1;
    }
}

/* Parses the stylesheet text at RD_POOL[off .. off+len). Comments (and the old <!-- --> hiding trick) are blanked in place. */
static inline void css_parse_sheet(u32 off, u32 len, int origin) {
    u8 *b = RD_POOL + off;
    for (u32 i = 0; i + 1 < len; i++) {
        if (b[i] == '/' && b[i + 1] == '*') {
            u32 j = i + 2; while (j + 1 < len && !(b[j] == '*' && b[j + 1] == '/')) j++;
            u32 e = j + 1 < len ? j + 2 : len;
            for (u32 k = i; k < e; k++) b[k] = ' ';
            i = e - 1;
        } else if (b[i] == '<' && i + 3 < len && b[i + 1] == '!' && b[i + 2] == '-' && b[i + 3] == '-') { b[i] = b[i + 1] = b[i + 2] = b[i + 3] = ' '; }
        else if (b[i] == '-' && i + 2 < len && b[i + 1] == '-' && b[i + 2] == '>') { b[i] = b[i + 1] = b[i + 2] = ' '; }
    }
    css_parse_range(b, 0, len, origin, 0);
}

/* The user-agent sheet: what a browser looks like before a website has an opinion. */
static const char css_ua_sheet[] =
"html,body,address,blockquote,center,dialog,div,figure,figcaption,footer,form,header,hgroup,main,menu,nav,section,article,aside,"
"details,summary,dir,dl,dt,dd,ol,ul,fieldset,legend,optgroup,p,pre,listing,xmp,plaintext,hr,h1,h2,h3,h4,h5,h6,search,marquee{display:block}"
"head,link,meta,script,style,title,base,template,area,datalist,param,source,track,noembed,noframes,rt,rp,option,[hidden],dialog,frameset,frame{display:none}"
"li{display:list-item}"
"table{display:table;border-collapse:separate;border-spacing:2px}caption{display:table-caption;text-align:center}"
"tr{display:table-row}td,th{display:table-cell;padding:1px}thead,tbody,tfoot{display:table-row-group}col,colgroup{display:none}"
"th{font-weight:bold;text-align:center}"
"img,input,select,textarea,button,iframe,canvas,video,audio,object,embed,svg,math,progress,meter{display:inline-block}"
"body{margin:8px}"
"p,dl,pre,listing,xmp,plaintext{margin:1em 0}blockquote,figure{margin:1em 40px}dd{margin-left:40px}"
"ul,ol,dir,menu{margin:1em 0;padding-left:40px}ul ul,ul ol,ol ul,ol ol,ul dir,dir ul{margin:0}"
"ul{list-style-type:disc}ul ul{list-style-type:circle}ul ul ul{list-style-type:square}ol{list-style-type:decimal}"
"h1{font-size:2em;margin:.67em 0}h2{font-size:1.5em;margin:.83em 0}h3{font-size:1.17em;margin:1em 0}"
"h4{margin:1.33em 0}h5{font-size:.83em;margin:1.67em 0}h6{font-size:.67em;margin:2.33em 0}"
"h1,h2,h3,h4,h5,h6,b,strong,th{font-weight:bold}"
"i,em,cite,dfn,var,address{font-style:italic}u,ins{text-decoration:underline}s,strike,del{text-decoration:line-through}"
"a[href]{color:#0000EE;text-decoration:underline}"
"small{font-size:.83em}big{font-size:1.17em}sub{vertical-align:sub;font-size:.83em}sup{vertical-align:super;font-size:.83em}"
"mark{background-color:#FFFF00;color:#000000}center{text-align:center}nobr{white-space:nowrap}"
"pre,listing,xmp,plaintext{white-space:pre}"
"hr{margin:8px auto;border:1px inset #808080;height:0}"
"fieldset{border:2px groove #C0C0C0;padding:.35em .75em .625em;margin:0 2px}legend{padding:0 2px}"
"button,input[type=submit],input[type=button],input[type=reset]{border:2px outset #E0E0E0;background-color:#D4D0C8;padding:1px 6px;text-align:center}"
"input,textarea,select{border:2px inset #808080;background-color:#FFFFFF;padding:1px 2px}"
"input[type=checkbox],input[type=radio],input[type=hidden],input[type=image]{border:0;background-color:transparent;padding:0}"
"input[type=hidden]{display:none}"
"img{border:0}"
"summary{display:block;padding-left:1.6em}details>summary{font-weight:bold}"
"progress,meter{border:1px solid #808080;background-color:#E0E0E0}"
"dialog[open]{display:block;margin:1em auto;padding:1em;border:2px solid #000000;background-color:#FFFFFF;width:70%}"
"kbd{border:1px solid #808080;background-color:#EEEEEE;padding:0 3px;border-radius:3px}"
"abbr[title],acronym[title]{text-decoration:underline}";

/* ============================================================
 * matching
 * ============================================================ */
static inline u32 css_parent_elem(u32 node) {
    u32 p = RD_NODES[node].parent;
    return (p && RD_NODES[p].kind == RDK_ELEM) ? p : 0;
}
static inline u32 css_prev_elem(u32 node) {
    u32 p = RD_NODES[node].parent, prev = 0;
    if (!p) return 0;
    for (u32 c = RD_NODES[p].first; c && c != node; c = RD_NODES[c].next) if (RD_NODES[c].kind == RDK_ELEM) prev = c;
    return prev;
}
static inline int css_has_class(u32 node, u32 h) {
    u32 len; const u8 *v = dom_attr(node, AT_CLASS, &len);
    if (!v) return 0;
    for (u32 i = 0; i < len; ) {
        while (i < len && css_is_ws(v[i])) i++;
        u32 st = i; while (i < len && !css_is_ws(v[i])) i++;
        if (i > st && css_hash(v + st, i - st) == h) return 1;
    }
    return 0;
}
static inline int css_attr_match(u32 node, const css_comp_t *c) {
    u32 len; const u8 *v = dom_attr(node, c->attr_id, &len);
    if (!v) return 0;
    if (c->attr_op == AOP_EXISTS) return 1;
    const u8 *w = RD_POOL + c->attr_voff; u32 wl = c->attr_vlen;
    int ic = c->attr_icase;
    #define CMP(a, b) (ic ? css_lc(a) == css_lc(b) : (a) == (b))
    switch (c->attr_op) {
        case AOP_EQ: if (len != wl) return 0; for (u32 i = 0; i < wl; i++) if (!CMP(v[i], w[i])) return 0; return 1;
        case AOP_PREFIX: if (wl == 0 || len < wl) return 0; for (u32 i = 0; i < wl; i++) if (!CMP(v[i], w[i])) return 0; return 1;
        case AOP_SUFFIX: if (wl == 0 || len < wl) return 0; for (u32 i = 0; i < wl; i++) if (!CMP(v[len - wl + i], w[i])) return 0; return 1;
        case AOP_SUBSTR: if (wl == 0) return 0;
            for (u32 s = 0; s + wl <= len; s++) { u32 k = 0; while (k < wl && CMP(v[s + k], w[k])) k++; if (k == wl) return 1; }
            return 0;
        case AOP_DASH: if (len < wl) return 0; for (u32 i = 0; i < wl; i++) if (!CMP(v[i], w[i])) return 0; return len == wl || v[wl] == '-';
        case AOP_WORD:
            for (u32 i = 0; i < len; ) {
                while (i < len && css_is_ws(v[i])) i++;
                u32 st = i; while (i < len && !css_is_ws(v[i])) i++;
                if (i - st == wl && wl) { u32 k = 0; while (k < wl && CMP(v[st + k], w[k])) k++; if (k == wl) return 1; }
            }
            return 0;
    }
    #undef CMP
    return 0;
}
static int css_comp_ok(u32 node, const css_comp_t *c);

/* :has(): does `node` have a descendant / child / following sibling that matches member `m`? Bounded: a page that asks for
 * body:has(...) must not turn every style computation into a walk of the whole document. */
static int css_has_ok(u32 node, const css_comp_t *m) {
    rd_node_t *n = &RD_NODES[node];
    int budget = 3000;
    if (m->comb == '>') {
        for (u32 ch = n->first; ch; ch = RD_NODES[ch].next) if (RD_NODES[ch].kind == RDK_ELEM && css_comp_ok(ch, m)) return 1;
        return 0;
    }
    if (m->comb == '+' || m->comb == '~') {
        for (u32 ch = n->next; ch; ch = RD_NODES[ch].next) {
            if (RD_NODES[ch].kind != RDK_ELEM) continue;
            if (css_comp_ok(ch, m)) return 1;
            if (m->comb == '+') return 0;
        }
        return 0;
    }
    u32 c = n->first;
    while (c && budget-- > 0) {
        if (RD_NODES[c].kind == RDK_ELEM && css_comp_ok(c, m)) return 1;
        if (RD_NODES[c].first) { c = RD_NODES[c].first; continue; }
        while (c && c != node && !RD_NODES[c].next) c = RD_NODES[c].parent;
        if (!c || c == node) return 0;
        c = RD_NODES[c].next;
    }
    return 0;
}

/* every :is()/:where()/:not()/:has() group hanging off a compound must hold */
static inline int css_groups_ok(u32 node, u32 g) {
    for (; g != 0xFFFFFFFFu; g = RD_CSS_COMPS[g].attr_voff) {
        const css_comp_t *d = &RD_CSS_COMPS[g];
        int any = 0;
        for (u32 k = 0; k < d->alt_n && !any; k++) {
            const css_comp_t *m = &RD_CSS_COMPS[d->grp + k];
            any = (d->alt_kind == GK_HAS) ? css_has_ok(node, m) : (RD_NODES[node].kind == RDK_ELEM && css_comp_ok(node, m));
        }
        if (d->alt_kind == GK_NOT) { if (any) return 0; }
        else if (!any) return 0;
    }
    return 1;
}

static inline int css_is_form_control(u32 tag) { return tag == TG_INPUT || tag == TG_BUTTON || tag == TG_SELECT || tag == TG_TEXTAREA || tag == TG_OPTION || tag == TG_OPTGROUP || tag == TG_FIELDSET; }

static int css_comp_ok(u32 node, const css_comp_t *c) {
    rd_node_t *n = &RD_NODES[node];
    if (n->kind != RDK_ELEM) return 0;
    if (c->never) return 0;
    if (c->tag && n->tag != c->tag) return 0;
    if (c->id_hash) {
        u32 len; const u8 *v = dom_attr(node, AT_ID, &len);
        if (!v || css_hash(v, len) != c->id_hash) return 0;
    }
    for (u32 i = 0; i < c->ncls; i++) if (!css_has_class(node, c->cls[i])) return 0;
    if (c->attr_id && !css_attr_match(node, c)) return 0;
    if (c->pseudo) {
        u32 ps = c->pseudo;
        if (ps & PS_ROOT) { if (node != dom_html) return 0; }
        if (ps & PS_EMPTY) { if (n->first) return 0; }
        if (ps & PS_LINK) { if (n->tag != TG_A && n->tag != TG_AREA) return 0; if (!dom_has_attr(node, AT_HREF)) return 0; }
        if (ps & PS_CHECKED) { if (!dom_has_attr(node, AT_CHECKED) && !dom_has_attr(node, AT_SELECTED)) return 0; }
        if (ps & PS_DISABLED) { if (!dom_has_attr(node, AT_DISABLED)) return 0; }
        if (ps & PS_ENABLED) { if (!css_is_form_control(n->tag) || dom_has_attr(node, AT_DISABLED)) return 0; }
        if (ps & PS_READONLY) {                                          /* everything but an editable field is read-only */
            if (n->tag == TG_INPUT || n->tag == TG_TEXTAREA) { if (!dom_has_attr(node, AT_READONLY) && !dom_has_attr(node, AT_DISABLED)) return 0; }
        }
        if (ps & PS_PHSHOWN) {
            if (n->tag != TG_INPUT && n->tag != TG_TEXTAREA) return 0;
            if (!dom_has_attr(node, AT_PLACEHOLDER)) return 0;
            u32 vl; const u8 *vv = dom_attr(node, AT_VALUE, &vl); (void)vv; if (vl) return 0;
        }
        if (ps & (PS_FIRST | PS_LAST | PS_ONLY | PS_NTH | PS_TFIRST | PS_TLAST | PS_TONLY)) {
            u32 p = n->parent; int pos = 0, total = 0, tpos = 0, ttotal = 0, found = 0;
            for (u32 ch = RD_NODES[p].first; ch; ch = RD_NODES[ch].next) {
                if (RD_NODES[ch].kind != RDK_ELEM) continue;
                total++;
                if (RD_NODES[ch].tag == n->tag) ttotal++;
                if (ch == node) { pos = total; tpos = ttotal; found = 1; }
            }
            if (!found) return 0;
            if ((ps & PS_FIRST) && pos != 1) return 0;
            if ((ps & PS_LAST) && pos != total) return 0;
            if ((ps & PS_ONLY) && total != 1) return 0;
            if ((ps & PS_TFIRST) && tpos != 1) return 0;
            if ((ps & PS_TLAST) && tpos != ttotal) return 0;
            if ((ps & PS_TONLY) && ttotal != 1) return 0;
            if (ps & PS_NTH) {
                int idx = (ps & PS_NTHT) ? tpos : pos;
                if (ps & PS_NTHL) idx = ((ps & PS_NTHT) ? ttotal : total) - idx + 1;
                int a = c->nth_a, b = c->nth_b;
                if (a == 0) { if (idx != b) return 0; }
                else { int d = idx - b; if (d % a != 0 || d / a < 0) return 0; }
            }
        }
        if ((ps & PS_GROUP) && !css_groups_ok(node, c->grp)) return 0;
    }
    return 1;
}
static inline int css_match_from(u32 node, const css_comp_t *comps, int idx) {
    if (!css_comp_ok(node, &comps[idx])) return 0;
    if (idx == 0) return 1;
    switch (comps[idx].comb) {
        case '>': { u32 p = css_parent_elem(node); return p && css_match_from(p, comps, idx - 1); }
        case ' ': for (u32 p = css_parent_elem(node); p; p = css_parent_elem(p)) if (css_match_from(p, comps, idx - 1)) return 1; return 0;
        case '+': { u32 p = css_prev_elem(node); return p && css_match_from(p, comps, idx - 1); }
        case '~': { u32 p = css_prev_elem(node); while (p) { if (css_match_from(p, comps, idx - 1)) return 1; p = css_prev_elem(p); } return 0; }
    }
    return 0;
}


/* ============================================================
 * computed style: initial values, inheritance, applying declarations
 * ============================================================ */
static inline void css_zero(void *p, u32 n) { u8 *b = (u8 *)p; for (u32 i = 0; i < n; i++) b[i] = 0; }

static inline void css_style_init(css_style_t *s) {
    css_zero(s, sizeof(*s));
    s->display = DISP_INLINE; s->color = 0x000000; s->bg = CSS_NOCOLOR;
    s->fpx = CSS_BASE_FONT_PX; s->cell = CSS_MIN_CELL;
    for (int i = 0; i < 4; i++) s->bcolor[i] = CSS_CURRENT;
    s->width = s->height = s->minw = s->minh = s->maxw = s->maxh = s->flex_basis = css_len_auto();
    s->flex_shrink = 10; s->align_items = AI_STRETCH; s->align_self = AI_AUTO;
    s->list_style = LS_DISC;
    s->bg2 = CSS_NOCOLOR; s->sh_color = CSS_NOCOLOR; s->backdrop = 0xFFFFFFu; s->alpha = 255;
    for (int i = 0; i < 4; i++) s->off[i] = css_len_auto();
}
static inline void css_style_inherit(css_style_t *s, const css_style_t *par) {
    css_style_init(s);
    if (!par) return;
    s->color = par->color; s->fpx = par->fpx; s->cell = par->cell; s->flags = par->flags;
    s->text_align = par->text_align; s->white_space = par->white_space; s->visibility = par->visibility;
    s->text_transform = par->text_transform; s->list_style = par->list_style; s->lh = par->lh; s->lh_mode = par->lh_mode;
    s->border_collapse = par->border_collapse; s->spacing_x = par->spacing_x; s->spacing_y = par->spacing_y;
    s->text_indent = par->text_indent; s->hidden_text = par->hidden_text;
    s->backdrop = par->backdrop; s->vscope = par->vscope;
}

static inline int css_cell_for(int fpx) {
    int c = (fpx * CSS_ZOOM_NUM + CSS_ZOOM_DEN / 2) / CSS_ZOOM_DEN;
    return c < CSS_MIN_CELL ? CSS_MIN_CELL : c > CSS_MAX_CELL ? CSS_MAX_CELL : c;
}
static inline void css_set_fpx(css_style_t *s, int fpx) {
    if (fpx < 1) fpx = 1;
    if (fpx > 400) fpx = 400;
    s->fpx = (short)fpx; s->cell = (u8)css_cell_for(fpx);
}
/* font-size value -> CSS px, or -1 */
static inline int css_parse_fontsize(const u8 *v, u32 n, int parfpx) {
    if (css_kw(v, n, "xx-small")) return 9;
    if (css_kw(v, n, "x-small")) return 10;
    if (css_kw(v, n, "small")) return 13;
    if (css_kw(v, n, "medium")) return 16;
    if (css_kw(v, n, "large")) return 18;
    if (css_kw(v, n, "x-large")) return 24;
    if (css_kw(v, n, "xx-large")) return 32;
    if (css_kw(v, n, "xxx-large")) return 48;
    if (css_kw(v, n, "smaller")) return parfpx * 5 / 6;
    if (css_kw(v, n, "larger")) return parfpx * 6 / 5;
    int x; u32 c = css_num(v, n, &x);
    if (!c) return -1;
    const u8 *u = v + c; u32 un = n - c;
    if (css_kw(u, un, "px") || (un == 0 && x == 0)) return x / 100;
    if (css_kw(u, un, "em")) return parfpx * x / 100;
    if (css_kw(u, un, "rem")) return CSS_BASE_FONT_PX * x / 100;
    if (css_kw(u, un, "%")) return parfpx * x / 10000;
    if (css_kw(u, un, "pt")) return x * 4 / 300;
    if (css_kw(u, un, "ex")) return parfpx * x / 200;
    return -1;
}

/* one to four lengths -> top,right,bottom,left */
static inline int css_parse_sides(const u8 *v, u32 n, css_len_t out[4], int fpx, int allow_auto) {
    css_len_t t[4]; int k = 0;
    const u8 *p = v, *e = v + n, *ts; u32 tn;
    while (css_tok(&p, e, &ts, &tn)) {
        if (ts[0] == ',' || ts[0] == '/') return 0;
        if (k >= 4 || !css_parse_len(ts, tn, &t[k], fpx, 0)) return 0;
        if (!allow_auto && css_len_is_auto(t[k])) t[k] = css_len_px(0);
        k++;
    }
    if (k == 0) return 0;
    if (k == 1) { out[0] = out[1] = out[2] = out[3] = t[0]; }
    else if (k == 2) { out[0] = out[2] = t[0]; out[1] = out[3] = t[1]; }
    else if (k == 3) { out[0] = t[0]; out[1] = out[3] = t[1]; out[2] = t[2]; }
    else { out[0] = t[0]; out[1] = t[1]; out[2] = t[2]; out[3] = t[3]; }
    return 1;
}

static inline int css_border_style_kw(const u8 *v, u32 n) {
    if (css_kw(v, n, "none") || css_kw(v, n, "hidden")) return BS_NONE;
    if (css_kw(v, n, "solid")) return BS_SOLID;
    if (css_kw(v, n, "dashed")) return BS_DASHED;
    if (css_kw(v, n, "dotted")) return BS_DOTTED;
    if (css_kw(v, n, "double")) return BS_DOUBLE;
    if (css_kw(v, n, "inset")) return BS_INSET;
    if (css_kw(v, n, "outset")) return BS_OUTSET;
    if (css_kw(v, n, "groove")) return BS_GROOVE;
    if (css_kw(v, n, "ridge")) return BS_RIDGE;
    return -1;
}
static inline int css_border_width_tok(const u8 *v, u32 n, int fpx) {
    if (css_kw(v, n, "thin")) return 1;
    if (css_kw(v, n, "medium")) return 2;
    if (css_kw(v, n, "thick")) return 3;
    css_len_t l;
    if (!css_parse_len(v, n, &l, fpx, 0) || l.pct) return -1;
    return l.px < 0 ? 0 : l.px > 40 ? 40 : l.px;
}
static inline void css_set_color(u32 *dst, u32 c, u32 cur) { *dst = (c == CSS_CURRENT) ? cur : c; }

static inline void css_apply_border(css_style_t *s, int mask, const u8 *v, u32 n) {
    int w = -1, st = -1; u32 col = CSS_CURRENT; int got_col = 0;
    const u8 *p = v, *e = v + n, *ts; u32 tn;
    while (css_tok(&p, e, &ts, &tn)) {
        int x;
        if ((x = css_border_style_kw(ts, tn)) >= 0) st = x;
        else if ((x = css_border_width_tok(ts, tn, s->fpx)) >= 0) w = x;
        else { u32 c; if (css_parse_color(ts, tn, &c)) { col = c; got_col = 1; } }
    }
    if (st < 0) st = BS_NONE;
    if (w < 0) w = 2;
    for (int i = 0; i < 4; i++) if (mask & (1 << i)) {
        s->bstyle[i] = (u8)st; s->bw[i] = (u8)(st == BS_NONE ? 0 : w);
        s->bcolor[i] = got_col ? col : CSS_CURRENT;
    }
}

static inline int css_list_style_kw(const u8 *v, u32 n) {
    if (css_kw(v, n, "none")) return LS_NONE;
    if (css_kw(v, n, "disc")) return LS_DISC;
    if (css_kw(v, n, "circle")) return LS_CIRCLE;
    if (css_kw(v, n, "square")) return LS_SQUARE;
    if (css_kw(v, n, "decimal") || css_kw(v, n, "decimal-leading-zero")) return LS_DECIMAL;
    if (css_kw(v, n, "lower-alpha") || css_kw(v, n, "lower-latin")) return LS_LOWER_ALPHA;
    if (css_kw(v, n, "upper-alpha") || css_kw(v, n, "upper-latin")) return LS_UPPER_ALPHA;
    if (css_kw(v, n, "lower-roman")) return LS_LOWER_ROMAN;
    if (css_kw(v, n, "upper-roman")) return LS_UPPER_ROMAN;
    if (css_kw(v, n, "korean-hangul-formal") || css_kw(v, n, "hangul") || css_kw(v, n, "cjk-decimal")) return LS_DECIMAL;
    return -1;
}

static inline int css_flex_align_kw(const u8 *v, u32 n, int allow_auto) {
    if (css_kw(v, n, "stretch") || css_kw(v, n, "normal")) return AI_STRETCH;
    if (css_kw(v, n, "flex-start") || css_kw(v, n, "start") || css_kw(v, n, "baseline") || css_kw(v, n, "self-start")) return AI_START;
    if (css_kw(v, n, "flex-end") || css_kw(v, n, "end") || css_kw(v, n, "self-end")) return AI_END;
    if (css_kw(v, n, "center")) return AI_CENTER;
    if (allow_auto && css_kw(v, n, "auto")) return AI_AUTO;
    return -1;
}

/* a color hiding inside linear-gradient(...) etc: the first color stop stands in for the whole gradient */
static inline int css_gradient_color(const u8 *v, u32 n, u32 *out) {
    u32 i = 0; while (i < n && v[i] != '(') i++;
    i++;
    while (i < n) {
        u32 st = i; int d = 0;
        while (i < n && !(v[i] == ',' && d == 0) && !(v[i] == ')' && d == 0)) { if (v[i] == '(') d++; else if (v[i] == ')') d--; i++; }
        u32 en = i; while (en > st && css_is_ws(v[en - 1])) en--;
        u32 k = st; while (k < en && css_is_ws(v[k])) k++;
        u32 sp = k, dd = 0; while (sp < en && !(css_is_ws(v[sp]) && dd == 0)) { if (v[sp] == '(') dd++; else if (v[sp] == ')') dd--; sp++; }
        if (css_parse_color(v + k, sp - k, out)) return 1;
        if (i < n && v[i] == ')') break;
        i++;
    }
    return 0;
}

static inline u32 css_mix(u32 c, u32 back, int a255) {          /* colour c at opacity a255 over colour `back` */
    if (a255 >= 255) return c;
    int r = (int)((c >> 16) & 255), g = (int)((c >> 8) & 255), b = (int)(c & 255);
    int br = (int)((back >> 16) & 255), bg = (int)((back >> 8) & 255), bb = (int)(back & 255);
    r = (r * a255 + br * (255 - a255)) / 255; g = (g * a255 + bg * (255 - a255)) / 255; b = (b * a255 + bb * (255 - a255)) / 255;
    return (u32)((r << 16) | (g << 8) | b);
}
static inline int css_has_word(const u8 *v, u32 n, const char *w) {
    u32 wl = 0; while (w[wl]) wl++;
    for (u32 i = 0; i + wl <= n; i++) { u32 k = 0; while (k < wl && css_lc(v[i + k]) == (u8)w[k]) k++; if (k == wl) return 1; }
    return 0;
}
/* linear-gradient(<direction>?, c1, c2, ...) -> the first two colour stops and a direction:
 * *dir = 1 vertical (c1 on top), 2 horizontal (c1 on the left). Returns 0 when there are not two colours. */
static inline int css_gradient2(const u8 *v, u32 n, u32 *c1, u32 *c2, u8 *dir) {
    u32 i = 0; while (i < n && v[i] != '(') i++;
    i++;
    u32 cols[2]; int nc = 0, first = 1, d = 1;                 /* d: 1 to bottom, 2 to right, 3 to top, 4 to left */
    while (i < n && nc < 2) {
        u32 st = i; int dp = 0;
        while (i < n && !(v[i] == ',' && dp == 0) && !(v[i] == ')' && dp == 0)) { if (v[i] == '(') dp++; else if (v[i] == ')') dp--; i++; }
        u32 en = i; while (en > st && css_is_ws(v[en - 1])) en--;
        u32 k = st; while (k < en && css_is_ws(v[k])) k++;
        u32 sp = k, dd = 0; while (sp < en && !(css_is_ws(v[sp]) && dd == 0)) { if (v[sp] == '(') dd++; else if (v[sp] == ')') dd--; sp++; }
        u32 c;
        if (sp > k && css_parse_color(v + k, sp - k, &c)) cols[nc++] = c;
        else if (first && en > k) {
            if (css_has_word(v + k, en - k, "to ")) {
                if (css_has_word(v + k, en - k, "right")) d = 2;
                else if (css_has_word(v + k, en - k, "left")) d = 4;
                else if (css_has_word(v + k, en - k, "top")) d = 3;
                else d = 1;
            } else {
                int num; u32 cn = css_num(v + k, en - k, &num);
                if (cn && css_has_word(v + k + cn, en - k - cn, "deg")) {
                    int deg = (num / 100) % 360; if (deg < 0) deg += 360;
                    d = (deg < 45 || deg >= 315) ? 3 : deg < 135 ? 2 : deg < 225 ? 1 : 4;
                }
            }
        }
        first = 0;
        if (i < n && v[i] == ')') break;
        i++;
    }
    if (nc < 2) return 0;
    if (d == 3 || d == 4) { *c1 = cols[1]; *c2 = cols[0]; } else { *c1 = cols[0]; *c2 = cols[1]; }
    *dir = (d == 2 || d == 4) ? 2 : 1;
    return 1;
}

/* A value that must outlive css_apply() has to live in the pool; the ones that arrived through var() were expanded onto the stack. */
static inline int css_intern(const u8 *v, u32 n, u32 *off) {
    if (v >= RD_POOL && v + n <= RD_POOL + RD_POOL_SIZE) { *off = (u32)(v - RD_POOL); return 1; }
    u32 h = css_hash(v, n) ^ n;
    for (u32 i = 0; i < css_intern_n; i++) if (css_intern_hash[i] == h) { *off = css_intern_off[i]; return 1; }
    if (css_intern_n >= CSS_INTERN_MAX || dom_pool_len + n + 16 >= RD_POOL_SIZE) return 0;
    *off = dom_pool_len;
    for (u32 i = 0; i < n; i++) RD_POOL[dom_pool_len++] = v[i];
    RD_POOL[dom_pool_len++] = ' ';
    css_intern_hash[css_intern_n] = h; css_intern_off[css_intern_n] = *off; css_intern_n++;
    return 1;
}

static inline void css_apply(css_style_t *s, u32 prop, const u8 *v, u32 n, const css_style_t *par) {
    u8 buf[384];
    css_blend_base = s->backdrop;                                   /* translucent colours are composited over what is behind this box */
    for (u32 i = 0; i + 3 < n; i++) {
        if (css_lc(v[i]) == 'v' && css_lc(v[i + 1]) == 'a' && css_lc(v[i + 2]) == 'r' && v[i + 3] == '(') { n = css_expand(v, n, buf, sizeof(buf), 0); v = buf; break; }
    }
    if (n == 0) return;
    #define KW(x) css_kw(v, n, x)
    if (KW("inherit") && par) {
        switch (prop) {
            case CP_COLOR: s->color = par->color; break;
            case CP_FONT_SIZE: s->fpx = par->fpx; s->cell = par->cell; break;
            case CP_FONT_WEIGHT: s->flags = (u8)((s->flags & ~SF_BOLD) | (par->flags & SF_BOLD)); break;
            case CP_FONT_STYLE: s->flags = (u8)((s->flags & ~SF_ITALIC) | (par->flags & SF_ITALIC)); break;
            case CP_TEXT_ALIGN: s->text_align = par->text_align; break;
            case CP_LINE_HEIGHT: s->lh = par->lh; s->lh_mode = par->lh_mode; break;
            case CP_VISIBILITY: s->visibility = par->visibility; break;
            case CP_WHITE_SPACE: s->white_space = par->white_space; break;
            case CP_DISPLAY: s->display = par->display == DISP_NONE ? DISP_INLINE : par->display; break;
            case CP_BACKGROUND: case CP_BACKGROUND_COLOR: s->bg = par->bg; break;
            default: break;
        }
        return;
    }
    const u8 *ts; u32 tn;
    switch (prop) {
        case CP_DISPLAY: {
            const u8 *p = v, *e = v + n;
            if (!css_tok(&p, e, &ts, &tn)) return;
            int d = -1;
            if (css_kw(ts, tn, "none")) d = DISP_NONE;
            else if (css_kw(ts, tn, "block") || css_kw(ts, tn, "flow-root") || css_kw(ts, tn, "-webkit-box")) d = DISP_BLOCK;
            else if (css_kw(ts, tn, "inline") || css_kw(ts, tn, "contents") || css_kw(ts, tn, "ruby")) d = DISP_INLINE;
            else if (css_kw(ts, tn, "inline-block") || css_kw(ts, tn, "inline-grid")) d = DISP_INLINE_BLOCK;
            else if (css_kw(ts, tn, "list-item")) d = DISP_LIST_ITEM;
            else if (css_kw(ts, tn, "table")) d = DISP_TABLE;
            else if (css_kw(ts, tn, "inline-table")) d = DISP_INLINE_TABLE;
            else if (css_kw(ts, tn, "table-row")) d = DISP_TABLE_ROW;
            else if (css_kw(ts, tn, "table-cell")) d = DISP_TABLE_CELL;
            else if (css_kw(ts, tn, "table-row-group") || css_kw(ts, tn, "table-header-group") || css_kw(ts, tn, "table-footer-group")) d = DISP_TABLE_GROUP;
            else if (css_kw(ts, tn, "table-caption")) d = DISP_TABLE_CAPTION;
            else if (css_kw(ts, tn, "table-column") || css_kw(ts, tn, "table-column-group")) d = DISP_NONE;
            else if (css_kw(ts, tn, "flex")) d = DISP_FLEX;
            else if (css_kw(ts, tn, "inline-flex")) d = DISP_INLINE_FLEX;
            else if (css_kw(ts, tn, "grid")) d = DISP_GRID;
            if (d >= 0) s->display = (u8)d;
        } break;
        case CP_COLOR: { u32 c; if (css_parse_color(v, n, &c)) css_set_color(&s->color, c, par ? par->color : 0); } break;
        case CP_BACKGROUND_COLOR: { u32 c; if (css_parse_color(v, n, &c)) { s->bg = c; s->grad = 0; } } break;   /* currentcolor stays CSS_CURRENT until the whole rule set is applied: `color` may come later */
        case CP_BACKGROUND: {
            u32 col = CSS_NOCOLOR; const u8 *p = v, *e = v + n; u32 g1, g2; u8 gd; int got_grad = 0;
            while (css_tok(&p, e, &ts, &tn)) {
                u32 c;
                if (tn > 16 && css_kw(ts, 16, "linear-gradient(") && css_gradient2(ts, tn, &g1, &g2, &gd)) { got_grad = 1; col = g1; }
                else if (css_parse_color(ts, tn, &c)) col = c;
                else if (tn > 8 && (css_kw(ts, 7, "linear-") || css_kw(ts, 7, "radial-") || css_kw(ts, 9, "repeating")) && css_gradient_color(ts, tn, &c)) col = c;
            }
            s->bg = col;                                           /* (currentcolor is resolved at the end, see "finishing touches") */
            if (got_grad) { s->grad = gd; s->bg2 = g2; } else s->grad = 0;
        } break;
        case CP_FONT_SIZE: { int px = css_parse_fontsize(v, n, par ? par->fpx : CSS_BASE_FONT_PX); if (px >= 0) css_set_fpx(s, px); } break;
        case CP_FONT_WEIGHT: {
            int x;
            if (KW("bold") || KW("bolder")) s->flags |= SF_BOLD;
            else if (KW("normal") || KW("lighter")) s->flags &= (u8)~SF_BOLD;
            else if (css_num(v, n, &x)) { if (x >= 60000) s->flags |= SF_BOLD; else s->flags &= (u8)~SF_BOLD; }
        } break;
        case CP_FONT_STYLE:
            if (KW("italic") || KW("oblique")) s->flags |= SF_ITALIC; else if (KW("normal")) s->flags &= (u8)~SF_ITALIC;
            break;
        case CP_FONT: {
            const u8 *p = v, *e = v + n; int seen_size = 0;
            if (KW("inherit")) return;
            s->flags &= (u8)~(SF_BOLD | SF_ITALIC);
            while (!seen_size && css_tok(&p, e, &ts, &tn)) {
                int x;
                if (css_kw(ts, tn, "italic") || css_kw(ts, tn, "oblique")) s->flags |= SF_ITALIC;
                else if (css_kw(ts, tn, "bold") || css_kw(ts, tn, "bolder")) s->flags |= SF_BOLD;
                else if (css_kw(ts, tn, "normal") || css_kw(ts, tn, "small-caps")) { }
                else if (css_num(ts, tn, &x) == tn && x >= 10000) { if (x >= 60000) s->flags |= SF_BOLD; }
                else {
                    int px = css_parse_fontsize(ts, tn, par ? par->fpx : CSS_BASE_FONT_PX);
                    if (px >= 0) {
                        css_set_fpx(s, px); seen_size = 1;
                        const u8 *q = p;
                        if (css_tok(&q, e, &ts, &tn) && ts[0] == '/') {
                            p = q;
                            if (css_tok(&p, e, &ts, &tn)) css_apply(s, CP_LINE_HEIGHT, ts, tn, par);
                        }
                    }
                }
            }
        } break;
        case CP_LINE_HEIGHT: {
            int x; u32 c = css_num(v, n, &x);
            if (KW("normal")) { s->lh_mode = 0; s->lh = 0; }
            else if (c == n) { s->lh_mode = 2; s->lh = (short)(x > 3000 ? 3000 : x < 0 ? 0 : x); }
            else {
                css_len_t l;
                if (css_parse_len(v, n, &l, s->fpx, 0) && !css_len_is_auto(l)) {
                    if (l.pct) { s->lh_mode = 2; s->lh = (short)(l.pct / 10); } else { s->lh_mode = 1; s->lh = l.px; }
                }
            }
        } break;
        case CP_TEXT_ALIGN:
            if (KW("left") || KW("start") || KW("-webkit-left")) s->text_align = TA_LEFT;
            else if (KW("center") || KW("-webkit-center") || KW("-moz-center")) s->text_align = TA_CENTER;
            else if (KW("right") || KW("end") || KW("-webkit-right")) s->text_align = TA_RIGHT;
            else if (KW("justify")) s->text_align = TA_JUSTIFY;
            break;
        case CP_TEXT_DECORATION: case CP_TEXT_DECORATION_LINE: {
            const u8 *p = v, *e = v + n; u8 f = 0; int none = 0;
            while (css_tok(&p, e, &ts, &tn)) {
                if (css_kw(ts, tn, "underline")) f |= SF_UNDER;
                else if (css_kw(ts, tn, "line-through")) f |= SF_STRIKE;
                else if (css_kw(ts, tn, "none")) none = 1;
            }
            if (none) s->flags &= (u8)~(SF_UNDER | SF_STRIKE);
            s->flags |= f;
        } break;
        case CP_TEXT_TRANSFORM:
            s->text_transform = KW("uppercase") ? TT_UPPER : KW("lowercase") ? TT_LOWER : KW("capitalize") ? TT_CAP : TT_NONE;
            break;
        case CP_TEXT_INDENT: { css_len_t l; if (css_parse_len(v, n, &l, s->fpx, 0) && !l.pct) { if (l.px <= -300) s->hidden_text = 1; else s->text_indent = l.px; } } break;
        case CP_WHITE_SPACE:
            s->white_space = KW("nowrap") ? WS_NOWRAP : KW("pre") ? WS_PRE : (KW("pre-wrap") || KW("break-spaces")) ? WS_PRE_WRAP :
                             KW("pre-line") ? WS_PRE_LINE : WS_NORMAL;
            break;
        case CP_VISIBILITY: s->visibility = (KW("hidden") || KW("collapse")) ? 1 : 0; break;
        case CP_OPACITY: {
            int x; u32 c = css_num(v, n, &x);
            if (c) {
                if (c < n && v[c] == '%') x /= 100;
                if (x < 0) x = 0;
                if (x > 100) x = 100;
                if (x <= 5) s->visibility = 1; else s->alpha = (u8)(x * 255 / 100);
            }
        } break;
        case CP_BORDER_RADIUS: {
            const u8 *p = v, *e = v + n; css_len_t l;
            if (css_tok(&p, e, &ts, &tn) && css_parse_len(ts, tn, &l, s->fpx, 0) && !css_len_is_auto(l)) {
                if (l.pct > 0) s->radius = l.pct >= 400 ? 255 : 8;
                else s->radius = (u8)(l.px < 0 ? 0 : l.px > 254 ? 254 : l.px);
            }
        } break;
        case CP_BOX_SHADOW: {
            const u8 *p = v, *e = v + n; int nl = 0, xs[3] = {0, 0, 0}, inset = 0; u32 col = CSS_CURRENT;
            if (KW("none")) { s->sh_color = CSS_NOCOLOR; break; }
            while (css_tok(&p, e, &ts, &tn)) {
                u32 c; css_len_t l;
                if (tn == 1 && ts[0] == ',') break;                       /* only the first shadow is drawn */
                if (css_kw(ts, tn, "inset")) inset = 1;
                else if (css_parse_color(ts, tn, &c)) col = c;
                else if (css_parse_len(ts, tn, &l, s->fpx, 0) && !css_len_is_auto(l) && !l.pct) { if (nl < 3) xs[nl] = l.px; nl++; }
            }
            if (inset || nl < 2) break;
            s->sh_dx = (short)xs[0]; s->sh_dy = (short)xs[1]; s->sh_blur = (u8)(xs[2] < 0 ? 0 : xs[2] > 40 ? 40 : xs[2]);
            s->sh_color = col;                                     /* resolved at the end, like the background */
        } break;
        case CP_INSET: {
            const u8 *p = v, *e = v + n; css_len_t l[4]; int k = 0;
            while (k < 4 && css_tok(&p, e, &ts, &tn)) { if (!css_parse_len(ts, tn, &l[k], s->fpx, 0)) return; k++; }
            if (k == 0) return;
            if (k == 1) l[1] = l[2] = l[3] = l[0]; else if (k == 2) { l[2] = l[0]; l[3] = l[1]; } else if (k == 3) l[3] = l[1];
            for (int i = 0; i < 4; i++) s->off[i] = l[i];
        } break;
        case CP_VERTICAL_ALIGN:
            s->valign = (KW("top") || KW("text-top")) ? VA_TOP : KW("middle") ? VA_MIDDLE : (KW("bottom") || KW("text-bottom")) ? VA_BOTTOM :
                        KW("sub") ? VA_SUB : KW("super") ? VA_SUPER : VA_BASELINE;
            break;
        case CP_MARGIN: css_parse_sides(v, n, s->margin, s->fpx, 1); break;
        case CP_PADDING: css_parse_sides(v, n, s->padding, s->fpx, 0); break;
        case CP_MARGIN_TOP: case CP_MARGIN_RIGHT: case CP_MARGIN_BOTTOM: case CP_MARGIN_LEFT: {
            css_len_t l; if (css_parse_len(v, n, &l, s->fpx, 0)) s->margin[prop - CP_MARGIN_TOP] = l;
        } break;
        case CP_PADDING_TOP: case CP_PADDING_RIGHT: case CP_PADDING_BOTTOM: case CP_PADDING_LEFT: {
            css_len_t l; if (css_parse_len(v, n, &l, s->fpx, 0)) { if (css_len_is_auto(l)) l = css_len_px(0); s->padding[prop - CP_PADDING_TOP] = l; }
        } break;
        case CP_BORDER: css_apply_border(s, 15, v, n); break;
        case CP_BORDER_TOP: css_apply_border(s, 1, v, n); break;
        case CP_BORDER_RIGHT: css_apply_border(s, 2, v, n); break;
        case CP_BORDER_BOTTOM: css_apply_border(s, 4, v, n); break;
        case CP_BORDER_LEFT: css_apply_border(s, 8, v, n); break;
        case CP_BORDER_WIDTH: {
            css_len_t t[4]; const u8 *p = v, *e = v + n; int k = 0, w[4];
            (void)t;
            while (css_tok(&p, e, &ts, &tn) && k < 4) { w[k] = css_border_width_tok(ts, tn, s->fpx); if (w[k] < 0) return; k++; }
            if (!k) return;
            int o[4] = { w[0], k > 1 ? w[1] : w[0], k > 2 ? w[2] : w[0], k > 3 ? w[3] : (k > 1 ? w[1] : w[0]) };
            for (int i = 0; i < 4; i++) s->bw[i] = (u8)o[i];
        } break;
        case CP_BORDER_STYLE: {
            const u8 *p = v, *e = v + n; int k = 0, w[4];
            while (css_tok(&p, e, &ts, &tn) && k < 4) { w[k] = css_border_style_kw(ts, tn); if (w[k] < 0) return; k++; }
            if (!k) return;
            int o[4] = { w[0], k > 1 ? w[1] : w[0], k > 2 ? w[2] : w[0], k > 3 ? w[3] : (k > 1 ? w[1] : w[0]) };
            for (int i = 0; i < 4; i++) s->bstyle[i] = (u8)o[i];
        } break;
        case CP_BORDER_COLOR: {
            const u8 *p = v, *e = v + n; int k = 0; u32 w[4];
            while (css_tok(&p, e, &ts, &tn) && k < 4) { if (!css_parse_color(ts, tn, &w[k])) return; k++; }
            if (!k) return;
            u32 o[4] = { w[0], k > 1 ? w[1] : w[0], k > 2 ? w[2] : w[0], k > 3 ? w[3] : (k > 1 ? w[1] : w[0]) };
            for (int i = 0; i < 4; i++) css_set_color(&s->bcolor[i], o[i], s->color);
        } break;
        case CP_BORDER_TOP_WIDTH: case CP_BORDER_RIGHT_WIDTH: case CP_BORDER_BOTTOM_WIDTH: case CP_BORDER_LEFT_WIDTH: {
            int w = css_border_width_tok(v, n, s->fpx); if (w >= 0) s->bw[prop - CP_BORDER_TOP_WIDTH] = (u8)w;
        } break;
        case CP_BORDER_TOP_STYLE: case CP_BORDER_RIGHT_STYLE: case CP_BORDER_BOTTOM_STYLE: case CP_BORDER_LEFT_STYLE: {
            int w = css_border_style_kw(v, n); if (w >= 0) s->bstyle[prop - CP_BORDER_TOP_STYLE] = (u8)w;
        } break;
        case CP_BORDER_TOP_COLOR: case CP_BORDER_RIGHT_COLOR: case CP_BORDER_BOTTOM_COLOR: case CP_BORDER_LEFT_COLOR: {
            u32 c; if (css_parse_color(v, n, &c)) css_set_color(&s->bcolor[prop - CP_BORDER_TOP_COLOR], c, s->color);
        } break;
        case CP_WIDTH: case CP_HEIGHT: case CP_MIN_WIDTH: case CP_MAX_WIDTH: case CP_MIN_HEIGHT: case CP_MAX_HEIGHT: {
            css_len_t l;
            if (!css_parse_len(v, n, &l, s->fpx, 0)) return;
            if ((prop == CP_MIN_WIDTH || prop == CP_MIN_HEIGHT) && css_len_is_auto(l)) l = css_len_px(0);
            switch (prop) {
                case CP_WIDTH: s->width = l; break;          case CP_HEIGHT: s->height = l; break;
                case CP_MIN_WIDTH: s->minw = l; break;       case CP_MAX_WIDTH: s->maxw = l; break;
                case CP_MIN_HEIGHT: s->minh = l; break;      default: s->maxh = l; break;
            }
        } break;
        case CP_FLOAT: s->floating = KW("left") ? 1 : KW("right") ? 2 : 0; break;
        case CP_CLEAR: s->clear = KW("left") ? 1 : KW("right") ? 2 : KW("both") ? 3 : 0; break;
        case CP_POSITION: s->position = KW("absolute") ? POS_ABSOLUTE : KW("fixed") ? POS_FIXED : (KW("relative") || KW("sticky")) ? POS_RELATIVE : POS_STATIC; break;
        case CP_OVERFLOW: case CP_OVERFLOW_X: case CP_OVERFLOW_Y: s->overflow_hidden = (KW("hidden") || KW("clip")) ? 1 : 0; break;
        case CP_BOX_SIZING: s->box_sizing_border = KW("border-box") ? 1 : 0; break;
        case CP_LIST_STYLE: case CP_LIST_STYLE_TYPE: {
            const u8 *p = v, *e = v + n;
            while (css_tok(&p, e, &ts, &tn)) { int x = css_list_style_kw(ts, tn); if (x >= 0) { s->list_style = (u8)x; break; } }
        } break;
        case CP_BORDER_COLLAPSE: s->border_collapse = KW("collapse") ? 1 : 0; break;
        case CP_BORDER_SPACING: {
            css_len_t a, b; const u8 *p = v, *e = v + n;
            if (css_tok(&p, e, &ts, &tn) && css_parse_len(ts, tn, &a, s->fpx, 0)) {
                b = a;
                if (css_tok(&p, e, &ts, &tn)) css_parse_len(ts, tn, &b, s->fpx, 0);
                s->spacing_x = a.px; s->spacing_y = b.px;
            }
        } break;
        case CP_FLEX: {
            const u8 *p = v, *e = v + n; int nums = 0;
            if (KW("none")) { s->flex_grow = 0; s->flex_shrink = 0; s->flex_basis = css_len_auto(); return; }
            if (KW("auto")) { s->flex_grow = 10; s->flex_shrink = 10; s->flex_basis = css_len_auto(); return; }
            s->flex_grow = 10; s->flex_shrink = 10; s->flex_basis = css_len_px(0);
            while (css_tok(&p, e, &ts, &tn)) {
                int x; u32 c = css_num(ts, tn, &x);
                if (c == tn) { if (nums == 0) s->flex_grow = (short)(x / 10); else if (nums == 1) s->flex_shrink = (short)(x / 10); else s->flex_basis = css_len_px(0); nums++; }
                else { css_len_t l; if (css_parse_len(ts, tn, &l, s->fpx, 0)) s->flex_basis = l; }
            }
        } break;
        case CP_FLEX_GROW: { int x; if (css_num(v, n, &x)) s->flex_grow = (short)(x / 10); } break;
        case CP_FLEX_SHRINK: { int x; if (css_num(v, n, &x)) s->flex_shrink = (short)(x / 10); } break;
        case CP_FLEX_BASIS: { css_len_t l; if (css_parse_len(v, n, &l, s->fpx, 0)) s->flex_basis = l; } break;
        case CP_FLEX_DIRECTION: s->flex_dir = (KW("column") || KW("column-reverse")) ? FD_COLUMN : FD_ROW; break;
        case CP_FLEX_WRAP: s->flex_wrap = (KW("wrap") || KW("wrap-reverse")) ? 1 : 0; break;
        case CP_FLEX_FLOW: {
            const u8 *p = v, *e = v + n;
            while (css_tok(&p, e, &ts, &tn)) {
                if (css_kw(ts, tn, "column") || css_kw(ts, tn, "column-reverse")) s->flex_dir = FD_COLUMN;
                else if (css_kw(ts, tn, "row") || css_kw(ts, tn, "row-reverse")) s->flex_dir = FD_ROW;
                else if (css_kw(ts, tn, "wrap") || css_kw(ts, tn, "wrap-reverse")) s->flex_wrap = 1;
                else if (css_kw(ts, tn, "nowrap")) s->flex_wrap = 0;
            }
        } break;
        case CP_JUSTIFY_CONTENT:
            s->justify = (KW("flex-end") || KW("end") || KW("right")) ? JC_END : KW("center") ? JC_CENTER : KW("space-between") ? JC_BETWEEN :
                         KW("space-around") ? JC_AROUND : KW("space-evenly") ? JC_EVENLY : JC_START;
            break;
        case CP_ALIGN_ITEMS: { int a = css_flex_align_kw(v, n, 0); if (a >= 0) s->align_items = (u8)a; } break;
        case CP_ALIGN_SELF: { int a = css_flex_align_kw(v, n, 1); if (a >= 0) s->align_self = (u8)a; } break;
        case CP_GAP: case CP_GRID_GAP: {
            css_len_t a, b; const u8 *p = v, *e = v + n;
            if (css_tok(&p, e, &ts, &tn) && css_parse_len(ts, tn, &a, s->fpx, 0)) {
                b = a;
                if (css_tok(&p, e, &ts, &tn)) css_parse_len(ts, tn, &b, s->fpx, 0);
                s->gap_row = css_len_is_auto(a) ? 0 : a.px; s->gap_col = css_len_is_auto(b) ? 0 : b.px;
            }
        } break;
        case CP_ROW_GAP: { css_len_t l; if (css_parse_len(v, n, &l, s->fpx, 0)) s->gap_row = css_len_is_auto(l) ? 0 : l.px; } break;
        case CP_COLUMN_GAP: { css_len_t l; if (css_parse_len(v, n, &l, s->fpx, 0)) s->gap_col = css_len_is_auto(l) ? 0 : l.px; } break;
        case CP_GRID_TEMPLATE_COLUMNS:
            if (v >= RD_POOL && v < RD_POOL + RD_POOL_SIZE) { s->gtc_off = (u32)(v - RD_POOL); s->gtc_len = (u16)(n > 65535 ? 65535 : n); }
            else if (KW("none")) s->gtc_len = 0;
            break;
        case CP_TOP: case CP_LEFT: case CP_RIGHT: case CP_BOTTOM: {
            css_len_t l;
            if (css_parse_len(v, n, &l, s->fpx, 0)) {
                if (!l.pct && l.px <= -300) s->pad8 |= 1;                     /* parked off-screen */
                s->off[prop == CP_TOP ? 0 : prop == CP_RIGHT ? 1 : prop == CP_BOTTOM ? 2 : 3] = l;
            }
        } break;
        case CP_CONTENT:
            if (KW("none") || KW("normal")) { s->has_content = 0; break; }
            { u32 off; if (css_intern(v, n, &off)) { s->content_off = off; s->content_len = (u16)(n > 60000 ? 60000 : n); s->has_content = 1; } }
            break;
        case CP_COUNTER_RESET: case CP_COUNTER_INCREMENT: {
            u32 off = 0; u16 ln = 0;
            if (!KW("none") && css_intern(v, n, &off)) ln = (u16)(n > 60000 ? 60000 : n);
            if (prop == CP_COUNTER_RESET) { s->reset_off = off; s->reset_len = ln; } else { s->incr_off = off; s->incr_len = ln; }
        } break;
        case CP_Z_INDEX: {
            int x; u32 c = css_num(v, n, &x);
            if (KW("auto")) s->has_z = 0;
            else if (c) { x /= 100; s->zindex = (short)(x > 30000 ? 30000 : x < -30000 ? -30000 : x); s->has_z = 1; }
        } break;
        case CP_TEXT_OVERFLOW: s->text_overflow = (n >= 8 && css_kw(v, 8, "ellipsis")) ? 1 : 0; break;
        case CP_ASPECT_RATIO: {
            const u8 *p = v, *e = v + n; int a = 0, b = 100, got = 0;
            while (css_tok(&p, e, &ts, &tn)) {
                int x;
                if (tn == 1 && ts[0] == '/') continue;
                if (css_num(ts, tn, &x) == tn && x > 0) { if (!got) { a = x; got = 1; } else b = x; }
            }
            s->aspect = (got && b > 0) ? (u16)((long)a * 256 / b > 60000 ? 60000 : (long)a * 256 / b) : 0;
        } break;
        case CP_TRANSFORM: {
            s->has_tr = 0; s->tx = css_len_px(0); s->ty = css_len_px(0);
            for (u32 i = 0; i + 9 < n; i++) {
                if (i && v[i - 1] != ' ' && v[i - 1] != ')') continue;
                if (!css_kw(v + i, 9, "translate")) continue;
                u32 j = i + 9; u8 axis = 0;
                if (j < n && (css_lc(v[j]) == 'x' || css_lc(v[j]) == 'y')) { axis = css_lc(v[j]); j++; }
                else if (j + 1 < n && v[j] == '3' && css_lc(v[j + 1]) == 'd') j += 2;
                if (j >= n || v[j] != '(') continue;
                u32 e = j + 1; int d = 1; while (e < n && d) { if (v[e] == '(') d++; else if (v[e] == ')') d--; e++; }
                const u8 *p = v + j + 1, *pe2 = v + e - 1; const u8 *a1; u32 n1; css_len_t l1, l2; int got2 = 0;
                if (!css_tok(&p, pe2, &a1, &n1) || !css_parse_len(a1, n1, &l1, s->fpx, 0)) continue;
                if (css_len_is_auto(l1)) l1 = css_len_px(0);
                l2 = css_len_px(0);
                if (css_tok(&p, pe2, &a1, &n1) && a1[0] == ',') { if (css_tok(&p, pe2, &a1, &n1) && css_parse_len(a1, n1, &l2, s->fpx, 0)) got2 = 1; }
                (void)got2;
                if (css_len_is_auto(l2)) l2 = css_len_px(0);
                if (axis == 'x') s->tx = l1; else if (axis == 'y') s->ty = l1; else { s->tx = l1; s->ty = l2; }
                s->has_tr = 1; i = e;
            }
        } break;
        case CP_MARGIN_INLINE: case CP_MARGIN_BLOCK: case CP_PADDING_INLINE: case CP_PADDING_BLOCK: case CP_INSET_INLINE: case CP_INSET_BLOCK: {
            const u8 *p = v, *e = v + n, *t1, *t2; u32 n1, n2;
            if (!css_tok(&p, e, &t1, &n1)) break;
            if (!css_tok(&p, e, &t2, &n2)) { t2 = t1; n2 = n1; }
            u32 pa, pb;
            switch (prop) {
                case CP_MARGIN_INLINE: pa = CP_MARGIN_LEFT; pb = CP_MARGIN_RIGHT; break;
                case CP_MARGIN_BLOCK: pa = CP_MARGIN_TOP; pb = CP_MARGIN_BOTTOM; break;
                case CP_PADDING_INLINE: pa = CP_PADDING_LEFT; pb = CP_PADDING_RIGHT; break;
                case CP_PADDING_BLOCK: pa = CP_PADDING_TOP; pb = CP_PADDING_BOTTOM; break;
                case CP_INSET_INLINE: pa = CP_LEFT; pb = CP_RIGHT; break;
                default: pa = CP_TOP; pb = CP_BOTTOM; break;
            }
            css_apply(s, pa, t1, n1, par); css_apply(s, pb, t2, n2, par);
        } break;
        case CP_MARGIN_INLINE_START: case CP_MARGIN_INLINE_END: case CP_MARGIN_BLOCK_START: case CP_MARGIN_BLOCK_END:
        case CP_PADDING_INLINE_START: case CP_PADDING_INLINE_END: case CP_PADDING_BLOCK_START: case CP_PADDING_BLOCK_END:
        case CP_INSET_INLINE_START: case CP_INSET_INLINE_END: case CP_INSET_BLOCK_START: case CP_INSET_BLOCK_END:
        case CP_INLINE_SIZE: case CP_BLOCK_SIZE: case CP_MIN_INLINE_SIZE: case CP_MAX_INLINE_SIZE: case CP_MIN_BLOCK_SIZE: case CP_MAX_BLOCK_SIZE: {
            /* logical properties on a left-to-right, horizontal-tb page are the physical ones under another name */
            static const struct { u16 from, to; } alias[] = {
                { CP_MARGIN_INLINE_START, CP_MARGIN_LEFT }, { CP_MARGIN_INLINE_END, CP_MARGIN_RIGHT }, { CP_MARGIN_BLOCK_START, CP_MARGIN_TOP }, { CP_MARGIN_BLOCK_END, CP_MARGIN_BOTTOM },
                { CP_PADDING_INLINE_START, CP_PADDING_LEFT }, { CP_PADDING_INLINE_END, CP_PADDING_RIGHT }, { CP_PADDING_BLOCK_START, CP_PADDING_TOP }, { CP_PADDING_BLOCK_END, CP_PADDING_BOTTOM },
                { CP_INSET_INLINE_START, CP_LEFT }, { CP_INSET_INLINE_END, CP_RIGHT }, { CP_INSET_BLOCK_START, CP_TOP }, { CP_INSET_BLOCK_END, CP_BOTTOM },
                { CP_INLINE_SIZE, CP_WIDTH }, { CP_BLOCK_SIZE, CP_HEIGHT }, { CP_MIN_INLINE_SIZE, CP_MIN_WIDTH }, { CP_MAX_INLINE_SIZE, CP_MAX_WIDTH },
                { CP_MIN_BLOCK_SIZE, CP_MIN_HEIGHT }, { CP_MAX_BLOCK_SIZE, CP_MAX_HEIGHT } };
            for (u32 i = 0; i < sizeof(alias) / sizeof(alias[0]); i++) if (alias[i].from == prop) { css_apply(s, alias[i].to, v, n, par); break; }
        } break;
        case CP_BORDER_TOP_LEFT_RADIUS: case CP_BORDER_TOP_RIGHT_RADIUS: case CP_BORDER_BOTTOM_LEFT_RADIUS: case CP_BORDER_BOTTOM_RIGHT_RADIUS: {
            /* one radius per box is all the painter has: the biggest corner wins (a "tab" with two round corners gets four) */
            u8 old = s->radius; css_apply(s, CP_BORDER_RADIUS, v, n, par);
            if (old != 255 && s->radius < old) s->radius = old;
        } break;
        case CP_CLIP: if (KW("rect(0,0,0,0)") || KW("rect(0 0 0 0)") || KW("rect(1px,1px,1px,1px)") || KW("rect(1px 1px 1px 1px)")) s->pad8 |= 2; break;
        case CP_CLIP_PATH: if (n > 5 && css_kw(v, 5, "inset")) s->pad8 |= 2; break;
        default: break;
    }
    #undef KW
}

/* ============================================================
 * presentational attributes (HTML4 leftovers that millions of pages still use)
 * ============================================================ */
/* "120", "120px", "50%" -> css_len_t */
static inline int css_attr_len(const u8 *v, u32 n, css_len_t *out) {
    int x; u32 c = css_num(v, n, &x);
    if (!c || x < 0) return 0;
    if (c < n && v[c] == '%') { out->px = 0; out->pct = (short)(x / 10 > 30000 ? 30000 : x / 10); return 1; }
    int px = (x * CSS_ZOOM_NUM + (CSS_ZOOM_DEN * 100) / 2) / (CSS_ZOOM_DEN * 100);
    if (x > 0 && px < 1) px = 1;
    *out = css_len_px(px); return 1;
}
static inline int css_zoom_px(int css_px) { int p = (css_px * CSS_ZOOM_NUM + CSS_ZOOM_DEN / 2) / CSS_ZOOM_DEN; return (css_px > 0 && p < 1) ? 1 : p; }

static inline void css_hints(u32 node, css_style_t *s, const css_style_t *par) {
    css_blend_base = s->backdrop;
    rd_node_t *n = &RD_NODES[node];
    u32 tag = n->tag;
    (void)par;
    for (u32 i = 0; i < n->b; i++) {
        rd_attr_t *a = &RD_ATTRS[n->a + i];
        const u8 *v = RD_POOL + a->voff; u32 vl = a->vlen;
        u32 c;
        switch (a->id) {
            case AT_ALIGN:
                if (tag == TG_IMG || tag == TG_INPUT || tag == TG_OBJECT || tag == TG_EMBED) break;
                if (css_kw(v, vl, "center") || css_kw(v, vl, "middle")) {
                    s->text_align = TA_CENTER;
                    if (tag == TG_TABLE || tag == TG_HR) { s->margin[1] = s->margin[3] = css_len_auto(); }
                } else if (css_kw(v, vl, "right")) {
                    s->text_align = TA_RIGHT;
                    if (tag == TG_TABLE || tag == TG_HR) s->margin[3] = css_len_auto();
                } else if (css_kw(v, vl, "left")) s->text_align = TA_LEFT;
                else if (css_kw(v, vl, "justify")) s->text_align = TA_JUSTIFY;
                break;
            case AT_VALIGN:
                if (tag == TG_TD || tag == TG_TH || tag == TG_TR || tag == TG_TBODY || tag == TG_THEAD || tag == TG_TFOOT)
                    s->valign = css_kw(v, vl, "top") ? VA_TOP : css_kw(v, vl, "bottom") ? VA_BOTTOM : css_kw(v, vl, "baseline") ? VA_BASELINE : VA_MIDDLE;
                break;
            case AT_BGCOLOR:
                if (tag == TG_BODY || tag == TG_TABLE || tag == TG_TR || tag == TG_TD || tag == TG_TH || tag == TG_THEAD || tag == TG_TBODY) {
                    if (css_parse_color(v, vl, &c)) s->bg = c;
                    else if (vl == 6 && css_hexv(v[0]) >= 0) {                      /* "FFCC00" without the '#': browsers accept it */
                        u8 t[8]; t[0] = '#'; for (u32 k = 0; k < 6; k++) t[1 + k] = v[k];
                        if (css_parse_color(t, 7, &c)) s->bg = c;
                    }
                }
                break;
            case AT_TEXT: if (tag == TG_BODY && css_parse_color(v, vl, &c)) s->color = c; break;
            case AT_COLOR:
                if (tag == TG_FONT || tag == TG_HR) {
                    if (css_parse_color(v, vl, &c)) s->color = c;
                    else if (vl == 6 && css_hexv(v[0]) >= 0) { u8 t[8]; t[0] = '#'; for (u32 k = 0; k < 6; k++) t[1 + k] = v[k]; if (css_parse_color(t, 7, &c)) s->color = c; }
                }
                break;
            case AT_SIZE:
                if (tag == TG_FONT && vl) {
                    static const short map[8] = { 16, 10, 13, 16, 18, 24, 32, 48 };
                    int x, lvl;
                    if (v[0] == '+' || v[0] == '-') { if (!css_num(v + 1, vl - 1, &x)) break; lvl = 3 + (v[0] == '-' ? -(x / 100) : (x / 100)); }
                    else { if (!css_num(v, vl, &x)) break; lvl = x / 100; }
                    if (lvl < 1) lvl = 1;
                    if (lvl > 7) lvl = 7;
                    css_set_fpx(s, map[lvl]);
                }
                break;
            case AT_WIDTH: case AT_HEIGHT:
                if (tag == TG_TABLE || tag == TG_TD || tag == TG_TH || tag == TG_IMG || tag == TG_HR || tag == TG_IFRAME || tag == TG_CANVAS ||
                    tag == TG_VIDEO || tag == TG_SVG || tag == TG_OBJECT || tag == TG_EMBED || tag == TG_AUDIO || tag == TG_COL || tag == TG_TR) {
                    css_len_t l;
                    if (css_attr_len(v, vl, &l)) { if (a->id == AT_WIDTH) s->width = l; else s->height = l; }
                }
                break;
            case AT_NOWRAP: if (tag == TG_TD || tag == TG_TH) s->white_space = WS_NOWRAP; break;
            case AT_HSPACE: if (tag == TG_IMG) { css_len_t l; if (css_attr_len(v, vl, &l)) { s->margin[1] = s->margin[3] = l; } } break;
            case AT_VSPACE: if (tag == TG_IMG) { css_len_t l; if (css_attr_len(v, vl, &l)) { s->margin[0] = s->margin[2] = l; } } break;
            case AT_BORDER:
                if (tag == TG_TABLE || tag == TG_IMG) {
                    int x = 1; if (vl) { if (!css_num(v, vl, &x)) x = 100; }
                    int w = x > 0 ? css_zoom_px(x / 100) : 0;
                    for (int k = 0; k < 4; k++) {
                        s->bw[k] = (u8)w; s->bstyle[k] = (u8)(w ? (tag == TG_TABLE ? BS_OUTSET : BS_SOLID) : BS_NONE);
                        s->bcolor[k] = tag == TG_TABLE ? 0x808080u : CSS_CURRENT;
                    }
                }
                break;
            case AT_TYPE:                                  /* <ol type="a">, <ul type=square>, <li type=i> */
                if (tag == TG_OL || tag == TG_UL || tag == TG_LI) {
                    if (vl == 1 && v[0] == 'a') s->list_style = LS_LOWER_ALPHA; else if (vl == 1 && v[0] == 'A') s->list_style = LS_UPPER_ALPHA;
                    else if (vl == 1 && v[0] == 'i') s->list_style = LS_LOWER_ROMAN; else if (vl == 1 && v[0] == 'I') s->list_style = LS_UPPER_ROMAN;
                    else if (vl == 1 && v[0] == '1') s->list_style = LS_DECIMAL;
                    else { int x = css_list_style_kw(v, vl); if (x >= 0) s->list_style = (u8)x; }
                }
                break;
            case AT_CELLSPACING: if (tag == TG_TABLE) { int x; if (css_num(v, vl, &x)) s->spacing_x = s->spacing_y = (short)css_zoom_px(x / 100); } break;
            default: break;
        }
    }
    if (tag == TG_TD || tag == TG_TH) {                         /* the table's border=/cellpadding= reach down into its cells */
        u32 t = n->parent;
        for (int up = 0; t && up < 6 && RD_NODES[t].tag != TG_TABLE; up++) t = RD_NODES[t].parent;
        if (t && RD_NODES[t].tag == TG_TABLE) {
            u32 vl; const u8 *v = dom_attr(t, AT_BORDER, &vl);
            int x = 0;
            if (v) { x = 100; if (vl) { if (!css_num(v, vl, &x)) x = 100; } }
            if (x > 0) for (int k = 0; k < 4; k++) { s->bw[k] = 1; s->bstyle[k] = BS_INSET; s->bcolor[k] = 0x808080u; }
            v = dom_attr(t, AT_CELLPADDING, &vl);
            if (v && css_num(v, vl, &x)) { int p = css_zoom_px(x / 100); for (int k = 0; k < 4; k++) s->padding[k] = css_len_px(p); }
        }
    }
}

/* ============================================================
 * the cascade
 * ============================================================ */
static inline void css_apply_inline(u32 node, css_style_t *s, const css_style_t *par, int want_important) {
    u32 len; const u8 *v = dom_attr(node, AT_STYLE, &len);
    if (!v) return;
    u32 p = (u32)(v - RD_POOL), end = p + len, ns, nn, vs, vn; int imp;
    while (css_next_decl(RD_POOL, &p, end, &ns, &nn, &vs, &vn, &imp)) {
        if (imp != want_important) continue;
        u32 pid = css_prop_id(RD_POOL + ns, nn);
        if (pid != CP_NONE) css_apply(s, pid, RD_POOL + vs, vn, par);
    }
}

#define CSS_MAX_MATCH 192
static inline void css_collect(u16 head, u32 node, u16 *m, int *nm, u8 pe) {
    for (u16 r = head; r != CSS_NIL; r = RD_CSS_RULES[r].next) {
        css_rule_t *rule = &RD_CSS_RULES[r];
        if (rule->pe != pe) continue;                                /* ::before / ::after rules style the generated boxes, not the element */
        if (*nm >= CSS_MAX_MATCH) { css_overflowed = 1; return; }
        if (css_match_from(node, &RD_CSS_COMPS[rule->comp_first], (int)rule->ncomp - 1)) m[(*nm)++] = r;
    }
}

/* Nodes the layout engine makes up for ::before / ::after carry one of these in rd_node_t.flags. They are real element nodes
 * (so every layout path can treat them like any child) that are NOT linked into their owner's child chain: selectors, the
 * JavaScript DOM and :empty never see them. Their parent link points at the owner. */
#define DOMF_PE_BEFORE 0x0002
#define DOMF_PE_AFTER  0x0004
#define DOMF_PE_MASK   0x0006

/* Computes the style of element `node` given its parent's computed style (NULL for the root).
 * For a made-up ::before / ::after node, `par` is the OWNER's style and the rules are the ones whose selector ends in that pseudo-element. */
static inline void css_compute(u32 node, const css_style_t *par, css_style_t *s) {
    rd_node_t *n = &RD_NODES[node];
    u8 pe = (u8)((n->flags & DOMF_PE_MASK) >> 1);
    u32 owner = pe ? n->parent : node;
    if (pe) n = &RD_NODES[owner];
    css_style_inherit(s, par);
    if (!pe && css_node_scope[node] != CSS_SCOPE_UNSET) s->vscope = css_node_scope[node];

    u16 m[CSS_MAX_MATCH]; int nm = 0;
    css_collect(css_universal, owner, m, &nm, pe);
    css_collect(css_by_tag[n->tag], owner, m, &nm, pe);
    u32 len; const u8 *v = dom_attr(owner, AT_CLASS, &len);
    if (v) {
        for (u32 i = 0; i < len; ) {
            while (i < len && css_is_ws(v[i])) i++;
            u32 st = i; while (i < len && !css_is_ws(v[i])) i++;
            if (i > st) css_collect(css_by_cls[css_hash(v + st, i - st) & (CSS_BUCKET_N - 1)], owner, m, &nm, pe);
        }
    }
    v = dom_attr(owner, AT_ID, &len);
    if (v) css_collect(css_by_id[css_hash(v, len) & (CSS_BUCKET_N - 1)], owner, m, &nm, pe);

    for (int i = 1; i < nm; i++) {                                  /* insertion sort: lowest priority first */
        u16 x = m[i]; int j = i - 1;
        while (j >= 0 && (RD_CSS_RULES[m[j]].spec > RD_CSS_RULES[x].spec ||
              (RD_CSS_RULES[m[j]].spec == RD_CSS_RULES[x].spec && RD_CSS_RULES[m[j]].order > RD_CSS_RULES[x].order))) { m[j + 1] = m[j]; j--; }
        m[j + 1] = x;
    }
    /* per-element custom properties come first: every other declaration of this element may use them. Done once per element
     * (the result is remembered in css_node_scope); later declarations of equal or higher priority sit later in the chain and win. */
    if (!pe && css_node_scope[node] == CSS_SCOPE_UNSET) {
        u16 sc = s->vscope;
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < nm; i++) {
                css_rule_t *r = &RD_CSS_RULES[m[i]];
                for (u32 d = 0; d < r->ndecl; d++) {
                    css_decl_t *dc = &RD_CSS_DECLS[r->decl_first + d];
                    if (dc->prop != CSS_CUSTOM_PROP || dc->imp != pass) continue;
                    u32 nameoff = dc->voff, dist = dc->vlen & 0xFFu;
                    sc = css_scope_push(sc, css_hash(RD_POOL + nameoff, dc->pad), nameoff + dist, dc->vlen >> 8);
                }
            }
            u32 il; const u8 *iv = dom_attr(node, AT_STYLE, &il);        /* style="--x: 1" */
            if (iv) {
                u32 p = (u32)(iv - RD_POOL), end = p + il, ns, nn, vs, vn; int imp;
                while (css_next_decl(RD_POOL, &p, end, &ns, &nn, &vs, &vn, &imp))
                    if (imp == pass && nn > 2 && RD_POOL[ns] == '-' && RD_POOL[ns + 1] == '-') sc = css_scope_push(sc, css_hash(RD_POOL + ns, nn), vs, vn);
            }
        }
        css_node_scope[node] = sc; s->vscope = sc;
    }
    css_cur_scope = s->vscope;

    int hints_done = 0;                                             /* presentational attributes slot in between: UA < hints < author */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < nm; i++) {
            css_rule_t *r = &RD_CSS_RULES[m[i]];
            if (pass == 0 && !hints_done && (r->spec >> 28) >= 1) { if (!pe) css_hints(node, s, par); hints_done = 1; }
            for (u32 d = 0; d < r->ndecl; d++) {
                css_decl_t *dc = &RD_CSS_DECLS[r->decl_first + d];
                if (dc->imp == pass && dc->prop != CSS_CUSTOM_PROP) css_apply(s, dc->prop, RD_POOL + dc->voff, dc->vlen, par);
            }
        }
        if (pass == 0 && !hints_done) { if (!pe) css_hints(node, s, par); hints_done = 1; }
        if (!pe) css_apply_inline(node, s, par, pass);
    }

    css_blend_base = 0xFFFFFFu; css_cur_scope = 0;
    /* finishing touches */
    for (int i = 0; i < 4; i++) {
        if (s->bstyle[i] == BS_NONE) s->bw[i] = 0;
        if (s->bcolor[i] == CSS_CURRENT) s->bcolor[i] = s->color;
    }
    if (s->bg == CSS_CURRENT) s->bg = s->color;
    if (s->sh_color == CSS_CURRENT) s->sh_color = s->color;
    if (s->alpha < 255) {                                           /* opacity: fade this box's own colours towards what is behind it */
        u32 bd = s->backdrop;
        if (s->bg != CSS_NOCOLOR) s->bg = css_mix(s->bg, bd, s->alpha);
        if (s->grad) s->bg2 = css_mix(s->bg2, bd, s->alpha);
        s->color = css_mix(s->color, s->bg != CSS_NOCOLOR ? s->bg : bd, s->alpha);
        for (int i = 0; i < 4; i++) if (s->bcolor[i] != CSS_NOCOLOR) s->bcolor[i] = css_mix(s->bcolor[i], bd, s->alpha);
        if (s->sh_color != CSS_NOCOLOR) s->sh_color = css_mix(s->sh_color, bd, s->alpha);
    }
    if (s->bg != CSS_NOCOLOR) s->backdrop = s->bg;
    if (s->floating && (s->display == DISP_INLINE || s->display == DISP_BLOCK || s->display == DISP_LIST_ITEM)) s->display = DISP_INLINE_BLOCK;
    if ((s->pad8 & 3) && s->position != POS_STATIC) s->display = DISP_NONE;      /* sr-only / off-screen tricks */
    if (s->position == POS_ABSOLUTE || s->position == POS_FIXED) { if (s->display == DISP_INLINE) s->display = DISP_BLOCK; }
}

/* Sets up the engine's stylesheets for the document just parsed: UA sheet, then every <style> in order. */
static inline void css_load_document(void) {
    css_reset();
    u32 off = dom_pool_len;
    for (u32 i = 0; css_ua_sheet[i]; i++) dom_pool_putc((u8)css_ua_sheet[i]);
    css_parse_sheet(off, dom_pool_len - off, 0);
    for (u32 nd = 1; nd < dom_node_count; nd++) {
        rd_node_t *e = &RD_NODES[nd];
        if (e->kind != RDK_ELEM || e->tag != TG_STYLE) continue;
        u32 ml; const u8 *mv = dom_attr(nd, AT_MEDIA, &ml);
        if (mv && !css_media_eval(mv, ml)) continue;
        u32 tl; const u8 *tv = dom_attr(nd, AT_TYPE, &tl);
        if (tv && tl && !css_kw(tv, tl, "text/css")) continue;
        for (u32 c = e->first; c; c = RD_NODES[c].next)
            if (RD_NODES[c].kind == RDK_TEXT) css_parse_sheet(RD_NODES[c].a, RD_NODES[c].b, 1);
    }
}

#endif
