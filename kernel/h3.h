/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef H3_H
#define H3_H
#include "io.h"
#include "quic.h"
#include "httpresp.h"
#include "serial.h"

/* ============================================================
 * h3.h -- HTTP/3 (RFC 9114) with QPACK (RFC 9204) on top of quic.h: one GET or POST per connection, the answer handed to httpresp.h as
 * an ordinary HTTP/1.1-shaped response (status line, headers, body), so redirects, Content-Type, the size limits and everything above
 * them behave the same whichever transport carried the page.
 *
 * QPACK without a dynamic table: our SETTINGS announce QPACK_MAX_TABLE_CAPACITY = 0 and QPACK_BLOCKED_STREAMS = 0, so a conforming
 * server can only use the static table and literals (Huffman-coded or not). That removes the encoder and decoder streams entirely --
 * RFC 9204 4.2 lets an endpoint skip creating them in exactly this case -- and a response that nevertheless refers to the dynamic table
 * is refused as H3_QPACK_DECOMPRESSION_FAILED.
 *
 * Alt-Svc: HTTP/3 is discovered the way browsers do it. A page fetched over HTTPS that carries `alt-svc: h3=":443"` makes its host
 * eligible; later loads of that host go over QUIC first. If QUIC fails before any response byte arrives (UDP blocked, no QUIC server,
 * Retry/Version Negotiation which quic.h does not do), the host is marked bad for this session and the load falls back to TLS over TCP.
 * The address bar's `h3://host/path` forces QUIC and shows the failure instead of falling back.
 * ============================================================ */

#define H3_FRAME_DATA      0x00u
#define H3_FRAME_HEADERS   0x01u
#define H3_FRAME_SETTINGS  0x04u
#define H3_HDR_MAX         6144u
#define H3_REQ_MAX         2400u

/* HTTP/3 error codes (RFC 9114 8.1) */
#define H3_NO_ERROR            0x100u
#define H3_GENERAL_PROTOCOL    0x101u
#define H3_INTERNAL_ERROR      0x102u
#define H3_STREAM_CREATION     0x103u
#define H3_CLOSED_CRITICAL     0x104u
#define H3_FRAME_UNEXPECTED    0x105u
#define H3_FRAME_ERROR         0x106u
#define H3_ID_ERROR            0x108u
#define H3_SETTINGS_ERROR      0x109u
#define H3_MISSING_SETTINGS    0x10au
#define H3_MESSAGE_ERROR       0x10eu
#define H3_QPACK_DECOMPRESSION 0x200u

/* ------------------------------------------------------------------ Huffman table (tools/gen_huffman.py) */
static const u32 h3_huff_code[257] = {
0x1ff8u, 0x7fffd8u, 0xfffffe2u, 0xfffffe3u, 0xfffffe4u, 0xfffffe5u, 0xfffffe6u, 0xfffffe7u,
0xfffffe8u, 0xffffeau, 0x3ffffffcu, 0xfffffe9u, 0xfffffeau, 0x3ffffffdu, 0xfffffebu, 0xfffffecu,
0xfffffedu, 0xfffffeeu, 0xfffffefu, 0xffffff0u, 0xffffff1u, 0xffffff2u, 0x3ffffffeu, 0xffffff3u,
0xffffff4u, 0xffffff5u, 0xffffff6u, 0xffffff7u, 0xffffff8u, 0xffffff9u, 0xffffffau, 0xffffffbu,
0x14u, 0x3f8u, 0x3f9u, 0xffau, 0x1ff9u, 0x15u, 0xf8u, 0x7fau,
0x3fau, 0x3fbu, 0xf9u, 0x7fbu, 0xfau, 0x16u, 0x17u, 0x18u,
0x0u, 0x1u, 0x2u, 0x19u, 0x1au, 0x1bu, 0x1cu, 0x1du,
0x1eu, 0x1fu, 0x5cu, 0xfbu, 0x7ffcu, 0x20u, 0xffbu, 0x3fcu,
0x1ffau, 0x21u, 0x5du, 0x5eu, 0x5fu, 0x60u, 0x61u, 0x62u,
0x63u, 0x64u, 0x65u, 0x66u, 0x67u, 0x68u, 0x69u, 0x6au,
0x6bu, 0x6cu, 0x6du, 0x6eu, 0x6fu, 0x70u, 0x71u, 0x72u,
0xfcu, 0x73u, 0xfdu, 0x1ffbu, 0x7fff0u, 0x1ffcu, 0x3ffcu, 0x22u,
0x7ffdu, 0x3u, 0x23u, 0x4u, 0x24u, 0x5u, 0x25u, 0x26u,
0x27u, 0x6u, 0x74u, 0x75u, 0x28u, 0x29u, 0x2au, 0x7u,
0x2bu, 0x76u, 0x2cu, 0x8u, 0x9u, 0x2du, 0x77u, 0x78u,
0x79u, 0x7au, 0x7bu, 0x7ffeu, 0x7fcu, 0x3ffdu, 0x1ffdu, 0xffffffcu,
0xfffe6u, 0x3fffd2u, 0xfffe7u, 0xfffe8u, 0x3fffd3u, 0x3fffd4u, 0x3fffd5u, 0x7fffd9u,
0x3fffd6u, 0x7fffdau, 0x7fffdbu, 0x7fffdcu, 0x7fffddu, 0x7fffdeu, 0xffffebu, 0x7fffdfu,
0xffffecu, 0xffffedu, 0x3fffd7u, 0x7fffe0u, 0xffffeeu, 0x7fffe1u, 0x7fffe2u, 0x7fffe3u,
0x7fffe4u, 0x1fffdcu, 0x3fffd8u, 0x7fffe5u, 0x3fffd9u, 0x7fffe6u, 0x7fffe7u, 0xffffefu,
0x3fffdau, 0x1fffddu, 0xfffe9u, 0x3fffdbu, 0x3fffdcu, 0x7fffe8u, 0x7fffe9u, 0x1fffdeu,
0x7fffeau, 0x3fffddu, 0x3fffdeu, 0xfffff0u, 0x1fffdfu, 0x3fffdfu, 0x7fffebu, 0x7fffecu,
0x1fffe0u, 0x1fffe1u, 0x3fffe0u, 0x1fffe2u, 0x7fffedu, 0x3fffe1u, 0x7fffeeu, 0x7fffefu,
0xfffeau, 0x3fffe2u, 0x3fffe3u, 0x3fffe4u, 0x7ffff0u, 0x3fffe5u, 0x3fffe6u, 0x7ffff1u,
0x3ffffe0u, 0x3ffffe1u, 0xfffebu, 0x7fff1u, 0x3fffe7u, 0x7ffff2u, 0x3fffe8u, 0x1ffffecu,
0x3ffffe2u, 0x3ffffe3u, 0x3ffffe4u, 0x7ffffdeu, 0x7ffffdfu, 0x3ffffe5u, 0xfffff1u, 0x1ffffedu,
0x7fff2u, 0x1fffe3u, 0x3ffffe6u, 0x7ffffe0u, 0x7ffffe1u, 0x3ffffe7u, 0x7ffffe2u, 0xfffff2u,
0x1fffe4u, 0x1fffe5u, 0x3ffffe8u, 0x3ffffe9u, 0xffffffdu, 0x7ffffe3u, 0x7ffffe4u, 0x7ffffe5u,
0xfffecu, 0xfffff3u, 0xfffedu, 0x1fffe6u, 0x3fffe9u, 0x1fffe7u, 0x1fffe8u, 0x7ffff3u,
0x3fffeau, 0x3fffebu, 0x1ffffeeu, 0x1ffffefu, 0xfffff4u, 0xfffff5u, 0x3ffffeau, 0x7ffff4u,
0x3ffffebu, 0x7ffffe6u, 0x3ffffecu, 0x3ffffedu, 0x7ffffe7u, 0x7ffffe8u, 0x7ffffe9u, 0x7ffffeau,
0x7ffffebu, 0xffffffeu, 0x7ffffecu, 0x7ffffedu, 0x7ffffeeu, 0x7ffffefu, 0x7fffff0u, 0x3ffffeeu,
0x3fffffffu, };
static const u8 h3_huff_len[257] = {
13, 23, 28, 28, 28, 28, 28, 28, 28, 24, 30, 28, 28, 30, 28, 28,
28, 28, 28, 28, 28, 28, 30, 28, 28, 28, 28, 28, 28, 28, 28, 28,
6, 10, 10, 12, 13, 6, 8, 11, 10, 10, 8, 11, 8, 6, 6, 6,
5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 7, 8, 15, 6, 12, 10,
13, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
7, 7, 7, 7, 7, 7, 7, 7, 8, 7, 8, 13, 19, 13, 14, 6,
15, 5, 6, 5, 6, 5, 6, 6, 6, 5, 7, 7, 6, 6, 6, 5,
6, 7, 6, 5, 5, 6, 7, 7, 7, 7, 7, 15, 11, 14, 13, 28,
20, 22, 20, 20, 22, 22, 22, 23, 22, 23, 23, 23, 23, 23, 24, 23,
24, 24, 22, 23, 24, 23, 23, 23, 23, 21, 22, 23, 22, 23, 23, 24,
22, 21, 20, 22, 22, 23, 23, 21, 23, 22, 22, 24, 21, 22, 23, 23,
21, 21, 22, 21, 23, 22, 23, 23, 20, 22, 22, 22, 23, 22, 22, 23,
26, 26, 20, 19, 22, 23, 22, 25, 26, 26, 26, 27, 27, 26, 24, 25,
19, 21, 26, 27, 27, 26, 27, 24, 21, 21, 26, 26, 28, 27, 27, 27,
20, 24, 20, 21, 22, 21, 21, 23, 22, 22, 25, 25, 24, 24, 26, 23,
26, 27, 26, 26, 27, 27, 27, 27, 27, 28, 27, 27, 27, 27, 27, 26,
30, };

/* ------------------------------------------------------------------ QPACK static table (RFC 9204 Appendix A) */
typedef struct { const char *n, *v; } h3_sf_t;
static const h3_sf_t h3_static[99] = {
    {":authority",""}, {":path","/"}, {"age","0"}, {"content-disposition",""}, {"content-length","0"}, {"cookie",""}, {"date",""}, {"etag",""},
    {"if-modified-since",""}, {"if-none-match",""}, {"last-modified",""}, {"link",""}, {"location",""}, {"referer",""}, {"set-cookie",""},
    {":method","CONNECT"}, {":method","DELETE"}, {":method","GET"}, {":method","HEAD"}, {":method","OPTIONS"}, {":method","POST"}, {":method","PUT"},
    {":scheme","http"}, {":scheme","https"}, {":status","103"}, {":status","200"}, {":status","304"}, {":status","404"}, {":status","503"},
    {"accept","*/*"}, {"accept","application/dns-message"}, {"accept-encoding","gzip, deflate, br"}, {"accept-ranges","bytes"},
    {"access-control-allow-headers","cache-control"}, {"access-control-allow-headers","content-type"}, {"access-control-allow-origin","*"},
    {"cache-control","max-age=0"}, {"cache-control","max-age=2592000"}, {"cache-control","max-age=604800"}, {"cache-control","no-cache"},
    {"cache-control","no-store"}, {"cache-control","public, max-age=31536000"}, {"content-encoding","br"}, {"content-encoding","gzip"},
    {"content-type","application/dns-message"}, {"content-type","application/javascript"}, {"content-type","application/json"},
    {"content-type","application/x-www-form-urlencoded"}, {"content-type","image/gif"}, {"content-type","image/jpeg"}, {"content-type","image/png"},
    {"content-type","text/css"}, {"content-type","text/html; charset=utf-8"}, {"content-type","text/plain"}, {"content-type","text/plain;charset=utf-8"},
    {"range","bytes=0-"}, {"strict-transport-security","max-age=31536000"}, {"strict-transport-security","max-age=31536000; includesubdomains"},
    {"strict-transport-security","max-age=31536000; includesubdomains; preload"}, {"vary","accept-encoding"}, {"vary","origin"},
    {"x-content-type-options","nosniff"}, {"x-xss-protection","1; mode=block"}, {":status","100"}, {":status","204"}, {":status","206"},
    {":status","302"}, {":status","400"}, {":status","403"}, {":status","421"}, {":status","425"}, {":status","500"}, {"accept-language",""},
    {"access-control-allow-credentials","FALSE"}, {"access-control-allow-credentials","TRUE"}, {"access-control-allow-headers","*"},
    {"access-control-allow-methods","get"}, {"access-control-allow-methods","get, post, options"}, {"access-control-allow-methods","options"},
    {"access-control-expose-headers","content-length"}, {"access-control-request-headers","content-type"}, {"access-control-request-method","get"},
    {"access-control-request-method","post"}, {"alt-svc","clear"}, {"authorization",""},
    {"content-security-policy","script-src 'none'; object-src 'none'; base-uri 'none'"}, {"early-data","1"}, {"expect-ct",""}, {"forwarded",""},
    {"if-range",""}, {"origin",""}, {"purpose","prefetch"}, {"server",""}, {"timing-allow-origin","*"}, {"upgrade-insecure-requests","1"},
    {"user-agent",""}, {"x-forwarded-for",""}, {"x-frame-options","deny"}, {"x-frame-options","sameorigin"},
};

/* ------------------------------------------------------------------ QPACK integers and strings (RFC 7541 5.1, RFC 9204 4.1) */
static inline u32 h3_put_int(u8 *o, u32 pos, int prefix, u8 flags, u32 v) {
    u32 mx = (1u << prefix) - 1;
    if (v < mx) { o[pos++] = (u8)(flags | v); return pos; }
    o[pos++] = (u8)(flags | mx); v -= mx;
    while (v >= 128) { o[pos++] = (u8)(0x80 | (v & 0x7F)); v >>= 7; }
    o[pos++] = (u8)v;
    return pos;
}
/* 0 on malformed or truncated input */
static inline int h3_get_int(const u8 **pp, const u8 *end, int prefix, u32 *out) {
    const u8 *p = *pp;
    if (p >= end) return 0;
    u32 mx = (1u << prefix) - 1, v = *p++ & mx;
    if (v == mx) {
        u32 shift = 0;
        for (;;) {
            if (p >= end || shift > 28) return 0;
            u8 b = *p++;
            if (shift == 28 && (b & 0x70)) return 0;                 /* would not fit 32 bits */
            v += (u32)(b & 0x7F) << shift; shift += 7;
            if (!(b & 0x80)) break;
        }
    }
    *pp = p; *out = v;
    return 1;
}
static inline u32 h3_put_str(u8 *o, u32 pos, const char *s) {        /* a raw (non-Huffman) string */
    u32 n = 0; while (s[n]) n++;
    pos = h3_put_int(o, pos, 7, 0x00, n);
    for (u32 i = 0; i < n; i++) o[pos++] = (u8)s[i];
    return pos;
}

/* Huffman decoding (RFC 7541 Appendix B). Walks the code bit by bit against the table; headers are small, so the plain search is fine.
 * Returns the decoded length or -1 (bad padding: more than 7 bits or not all ones; EOS inside the string; output full). */
static inline int h3_huff_decode(const u8 *in, u32 n, u8 *out, u32 cap) {
    u32 code = 0, len = 0, o = 0, ones = 1;
    for (u32 i = 0; i < n * 8; i++) {
        u32 bit = (in[i >> 3] >> (7 - (i & 7))) & 1;
        code = (code << 1) | bit; len++; ones = ones && bit;
        if (len > 30) return -1;
        if (len < 5) continue;
        for (u32 s = 0; s < 257; s++) {
            if (h3_huff_len[s] == len && h3_huff_code[s] == code) {
                if (s == 256) return -1;
                if (o >= cap) return -1;
                out[o++] = (u8)s; code = 0; len = 0; ones = 1;
                break;
            }
        }
    }
    if (len > 7 || !ones) return -1;
    return (int)o;
}
/* a string literal with its H flag, into out */
static inline int h3_get_str(const u8 **pp, const u8 *end, int prefix, u8 *out, u32 cap, u32 *olen) {
    if (*pp >= end) return 0;
    int huff = (**pp >> prefix) & 1;
    u32 l;
    if (!h3_get_int(pp, end, prefix, &l)) return 0;
    if (l > (u32)(end - *pp)) return 0;
    if (huff) { int r = h3_huff_decode(*pp, l, out, cap); if (r < 0) return 0; *olen = (u32)r; }
    else { if (l > cap) return 0; for (u32 i = 0; i < l; i++) out[i] = (*pp)[i]; *olen = l; }
    *pp += l;
    return 1;
}

/* ------------------------------------------------------------------ the HTTP/3 client */
typedef enum { H3_IDLE = 0, H3_CONNECTING, H3_AWAITING, H3_DONE, H3_FAILED } h3_state_t;
typedef struct { u8 type, got_type, got_settings; u32 pos; } h3_unistate_t;

typedef struct {
    h3_state_t state;
    char host[80], host_header[80], path[1024];
    u32 ip; u16 port; u8 seed[16];
    u8 request_sent;
    int post; const char *post_body; u32 post_len;
    u32 parsed;                  /* bytes of the request stream already consumed */
    u8 in_frame; u32 ftype, flen, frem;
    u8 got_headers, got_final, resp_started, resp_done, forced;
    u32 err;                     /* the HTTP/3 error code that ended the connection (0 = none) */
    h3_unistate_t uni[QUIC_UNI_SLOTS];
    u8 hbuf[H3_HDR_MAX];
    u8 req[H3_REQ_MAX];
} h3_t;
static h3_t h3;

static inline void h3_fail(u32 code) {
    if (h3.state == H3_FAILED) return;
    serial_puts("[H3] error 0x"); serial_put_hex32(code); serial_putc('\n');
    h3.err = code;
    h3.state = H3_FAILED;
    if (qc.state == QUIC_CONNECTED) quic_close(code);
}

/* ---- Alt-Svc memory (host -> UDP port), for this session ---- */
#define H3_ALT_MAX 16
static struct { char host[64]; u16 port; u8 state; } h3_alt[H3_ALT_MAX];   /* state 1: advertised, 2: failed once, do not try again */
static u8 h3_force_next;                                                    /* the next https_get() must use HTTP/3 (address bar h3://) */
static u8 h3_last_used;                                                     /* the last finished load went over HTTP/3 (for the status line) */

static inline int h3_alt_eq(const char *a, const char *b) {
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        if (!x) return 1;
    }
}
static inline int h3_alt_find(const char *host) {
    for (int i = 0; i < H3_ALT_MAX; i++) if (h3_alt[i].state && h3_alt_eq(h3_alt[i].host, host)) return i;
    return -1;
}
static inline void h3_alt_set(const char *host, u16 port, u8 state) {
    int i = h3_alt_find(host);
    if (i < 0) {
        for (i = 0; i < H3_ALT_MAX && h3_alt[i].state; i++) {}
        if (i == H3_ALT_MAX) i = 0;                                          /* full: replace the first */
        u32 k = 0; while (host[k] && k < sizeof(h3_alt[i].host) - 1) { h3_alt[i].host[k] = host[k]; k++; }
        h3_alt[i].host[k] = 0;
    }
    if (state == 1 && h3_alt[i].state == 2) return;                          /* a host that failed stays on TCP, whatever it advertises */
    h3_alt[i].port = port; h3_alt[i].state = state;
}
/* Should this host be tried over QUIC, and on which UDP port? */
static inline int h3_wanted(const char *host, u16 tcp_port, u16 *udp_port) {
    if (h3_force_next) { *udp_port = tcp_port; int i = h3_alt_find(host); if (i >= 0 && h3_alt[i].state == 1) *udp_port = h3_alt[i].port; return 1; }
    int i = h3_alt_find(host);
    if (i >= 0 && h3_alt[i].state == 1) { *udp_port = h3_alt[i].port; return 1; }
    return 0;
}

/* ---- the request ---- */
static inline u32 h3_build_request(u8 *o) {
    u8 *blk = o + 16; u32 p = 0;
    blk[p++] = 0x00; blk[p++] = 0x00;                                          /* Required Insert Count 0, Delta Base 0: no dynamic table */
    blk[p++] = (u8)(0xC0 | (h3.post ? 20 : 17));                               /* :method POST / GET   (indexed, static) */
    blk[p++] = (u8)(0xC0 | 23);                                                /* :scheme https */
    p = h3_put_int(blk, p, 4, 0x50, 0); p = h3_put_str(blk, p, h3.host_header);   /* :authority  (literal, static name reference) */
    p = h3_put_int(blk, p, 4, 0x50, 1); p = h3_put_str(blk, p, h3.path);          /* :path */
#define H3_LIT(name, value) do { const char *nm_ = (name); u32 nl_ = 0; while (nm_[nl_]) { nl_++; } p = h3_put_int(blk, p, 3, 0x20, nl_); \
        for (u32 i_ = 0; i_ < nl_; i_++) { blk[p++] = (u8)nm_[i_]; } p = h3_put_str(blk, p, (value)); } while (0)
    H3_LIT("user-agent", "Mozilla/5.0 (compatible; MiniWin/1.0; +https://github.com/SegFault777/MiniWin)");
    H3_LIT("accept", "text/html,text/plain;q=0.9,*/*;q=0.5");
    H3_LIT("accept-language", "en-US,en;q=0.9,ko;q=0.8");
    H3_LIT("accept-encoding", "identity");                                      /* we cannot decompress */
    if (h3.post) {
        char d[12]; int nd = 0; u32 v = h3.post_len; char num[12];
        do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v && nd < 10);
        for (int i = 0; i < nd; i++) num[i] = d[nd - 1 - i];
        num[nd] = 0;
        H3_LIT("content-type", "application/x-www-form-urlencoded");
        H3_LIT("content-length", num);
    }
#undef H3_LIT
    u32 q = 0;
    o[q++] = (u8)H3_FRAME_HEADERS;
    q += quic_put_vi(o + q, p);
    for (u32 i = 0; i < p; i++) o[q + i] = blk[i];                            /* (blk starts 16 bytes in, the frame header is at most 3) */
    q += p;
    if (h3.post) {
        o[q++] = (u8)H3_FRAME_DATA;
        q += quic_put_vi(o + q, h3.post_len);
        for (u32 i = 0; i < h3.post_len; i++) o[q + i] = (u8)h3.post_body[i];
        q += h3.post_len;
    }
    return q;
}

/* ---- the response head: QPACK field section -> "HTTP/1.1 NNN\r\nname: value\r\n...\r\n" for httpresp.h ---- */
static inline int h3_decode_headers(const u8 *p, u32 n, int *status) {
    const u8 *end = p + n;
    u32 ric, db, o = 0;
    *status = 0;
    if (!h3_get_int(&p, end, 8, &ric) || ric != 0) return 0;                    /* a dynamic-table reference: we never allowed one */
    if (p >= end) return 0;
    if (!h3_get_int(&p, end, 7, &db)) return 0;
    u8 name[96], val[2048];
    while (p < end) {
        u8 b = *p; u32 nl = 0, vl = 0; const char *sn = 0; int have_name = 0;
        if (b & 0x80) {                                                          /* indexed field line */
            if (!(b & 0x40)) return 0;                                           /* T = 0: dynamic table */
            u32 idx; if (!h3_get_int(&p, end, 6, &idx) || idx >= 99) return 0;
            const char *a = h3_static[idx].n, *v = h3_static[idx].v;
            while (a[nl]) { name[nl] = (u8)a[nl]; nl++; }
            while (v[vl]) { val[vl] = (u8)v[vl]; vl++; }
        } else if (b & 0x40) {                                                   /* literal with name reference */
            if (!(b & 0x10)) return 0;                                           /* T = 0: dynamic table */
            u32 idx; if (!h3_get_int(&p, end, 4, &idx) || idx >= 99) return 0;
            sn = h3_static[idx].n; have_name = 1;
            while (sn[nl]) { name[nl] = (u8)sn[nl]; nl++; }
            if (!h3_get_str(&p, end, 7, val, sizeof(val), &vl)) return 0;
        } else if (b & 0x20) {                                                   /* literal with literal name */
            if (!h3_get_str(&p, end, 3, name, sizeof(name), &nl)) return 0;
            if (!h3_get_str(&p, end, 7, val, sizeof(val), &vl)) return 0;
        } else return 0;                                                         /* post-base forms need a dynamic table */
        (void)have_name;
        if (nl == 0) return 0;
        if (name[0] == ':') {
            if (nl != 7 || name[1] != 's' || name[2] != 't' || name[3] != 'a' || name[4] != 't' || name[5] != 'u' || name[6] != 's' || vl != 3) return 0;
            int c = 0; for (u32 i = 0; i < 3; i++) { if (val[i] < '0' || val[i] > '9') return 0; c = c * 10 + (val[i] - '0'); }
            *status = c;
            continue;
        }
        if (o + nl + vl + 4 > H3_HDR_MAX) continue;                              /* an oversized header is dropped, not fatal */
        for (u32 i = 0; i < nl; i++) h3.hbuf[o++] = (name[i] >= 'A' && name[i] <= 'Z') ? (u8)(name[i] + 32) : name[i];
        h3.hbuf[o++] = ':'; h3.hbuf[o++] = ' ';
        for (u32 i = 0; i < vl; i++) h3.hbuf[o++] = (val[i] == '\r' || val[i] == '\n') ? ' ' : val[i];
        h3.hbuf[o++] = '\r'; h3.hbuf[o++] = '\n';
    }
    if (*status < 100 || *status > 599) return 0;
    h3.hbuf[o++] = '\r'; h3.hbuf[o++] = '\n';
    return (int)o;
}

/* ---- the server's unidirectional streams: control (0), push (1, refused), QPACK encoder (2) / decoder (3) (ignored: no dynamic table) ---- */
static inline void h3_pump_uni(void) {
    for (int i = 0; i < QUIC_UNI_SLOTS; i++) {
        quic_uni_t *u = &qc.uni[i];
        if (!u->used) continue;
        h3_unistate_t *st = &h3.uni[i];
        const u8 *b = u->r.buf; u32 avail = u->r.contig;
        if (!st->got_type) {
            u64 t; u32 a = quic_get_vi(b, b + avail, &t);
            if (!a) continue;
            st->got_type = 1; st->type = t > 0xff ? 0xff : (u8)t; st->pos = a;
            if (t == 1) { h3_fail(H3_ID_ERROR); return; }                       /* a server push: we never sent MAX_PUSH_ID */
        }
        if (st->type != 0) continue;                                              /* encoder/decoder/grease streams: nothing to do */
        while (st->pos < avail) {                                                 /* control stream: frames; the first MUST be SETTINGS */
            u64 ft, fl; const u8 *p = b + st->pos; u32 a1 = quic_get_vi(p, b + avail, &ft); if (!a1) break;
            u32 a2 = quic_get_vi(p + a1, b + avail, &fl); if (!a2) break;
            if (fl > QUIC_UNI_CAP) { h3_fail(H3_FRAME_ERROR); return; }
            if (a1 + a2 + fl > avail - st->pos) break;                            /* not complete yet */
            if (!st->got_settings && ft != H3_FRAME_SETTINGS) { h3_fail(H3_MISSING_SETTINGS); return; }
            if (st->got_settings && ft == H3_FRAME_SETTINGS) { h3_fail(H3_FRAME_UNEXPECTED); return; }
            if (ft == H3_FRAME_DATA || ft == H3_FRAME_HEADERS) { h3_fail(H3_FRAME_UNEXPECTED); return; }
            if (ft == H3_FRAME_SETTINGS) {
                const u8 *q = p + a1 + a2, *qe = q + fl;
                while (q < qe) {
                    u64 id, v; u32 x = quic_get_vi(q, qe, &id); if (!x) { h3_fail(H3_FRAME_ERROR); return; } q += x;
                    x = quic_get_vi(q, qe, &v); if (!x) { h3_fail(H3_FRAME_ERROR); return; } q += x;
                    if (id == 0x00 || id == 0x02 || id == 0x03 || id == 0x04 || id == 0x05) { h3_fail(H3_SETTINGS_ERROR); return; }   /* HTTP/2 leftovers */
                }
                st->got_settings = 1;
            }
            st->pos += a1 + a2 + (u32)fl;
        }
    }
}

/* ---- the request stream: HEADERS (informational ones skipped), DATA, trailers ---- */
static inline void h3_pump_response(void) {
    const u8 *b = qc.s0.buf;
    for (;;) {
        u32 avail = qc.s0.contig - h3.parsed;
        if (!h3.in_frame) {
            if (avail == 0) break;
            u64 ft, fl; const u8 *p = b + h3.parsed; u32 a1 = quic_get_vi(p, p + avail, &ft); if (!a1) break;
            u32 a2 = quic_get_vi(p + a1, p + avail, &fl); if (!a2) break;
            if (fl > 0x7fffffffull) { h3_fail(H3_FRAME_ERROR); return; }
            h3.ftype = (u32)ft; h3.flen = (u32)fl; h3.frem = (u32)fl; h3.parsed += a1 + a2; h3.in_frame = 1;
            if (ft == 0x02 || ft == 0x06 || ft == 0x08 || ft == 0x09 || ft == H3_FRAME_SETTINGS || ft == 0x03 || ft == 0x07 || ft == 0x0d) { h3_fail(H3_FRAME_UNEXPECTED); return; }   /* PRIORITY/PING/WINDOW (h2), SETTINGS, CANCEL_PUSH, GOAWAY, MAX_PUSH_ID on a request stream */
            if (ft == 0x05) { h3_fail(H3_GENERAL_PROTOCOL); return; }                                                                     /* PUSH_PROMISE: no push allowed */
            if (ft == H3_FRAME_DATA && !h3.got_final) { h3_fail(H3_FRAME_UNEXPECTED); return; }                                         /* DATA before the final HEADERS */
            if (ft == H3_FRAME_HEADERS && h3.flen > 60000u) { h3_fail(H3_FRAME_ERROR); return; }
            continue;
        }
        avail = qc.s0.contig - h3.parsed;
        if (h3.ftype == H3_FRAME_HEADERS) {
            if (avail < h3.flen) break;                                          /* the whole field section first */
            if (!h3.got_final) {
                int status;
                int hl = h3_decode_headers(b + h3.parsed, h3.flen, &status);
                if (hl < 0 || (hl == 0)) { h3_fail(H3_QPACK_DECOMPRESSION); return; }
                if (status >= 200) {
                    char sl[20]; u32 k = 0; const char *pre = "HTTP/1.1 ";
                    while (pre[k]) { sl[k] = pre[k]; k++; }
                    sl[k++] = (char)('0' + status / 100); sl[k++] = (char)('0' + (status / 10) % 10); sl[k++] = (char)('0' + status % 10);
                    sl[k++] = ' '; sl[k++] = 'H'; sl[k++] = '3'; sl[k++] = '\r'; sl[k++] = '\n';
                    hr_feed((const u8 *)sl, k);
                    hr_feed(h3.hbuf, (u32)hl);
                    h3.got_final = 1; h3.resp_started = 1;
                }                                                                  /* 1xx: informational, wait for the real answer */
                h3.got_headers = 1;
            }                                                                      /* else trailers: parsed over, not used */
            h3.parsed += h3.flen; h3.in_frame = 0;
        } else {
            u32 take = avail < h3.frem ? avail : h3.frem;
            if (h3.ftype == H3_FRAME_DATA && take) hr_feed(b + h3.parsed, take);
            h3.parsed += take; h3.frem -= take;
            if (h3.frem == 0) h3.in_frame = 0;
            else break;
        }
    }
    if (qc.s0.fin && h3.parsed == qc.s0.fin_off && !h3.in_frame) {
        if (!h3.got_final) h3_fail(H3_MESSAGE_ERROR);                              /* the stream ended without a final response */
        else h3.resp_done = 1;
    } else if (qc.s0.fin && qc.s0.contig == qc.s0.fin_off && h3.parsed != qc.s0.fin_off) {
        h3_fail(H3_FRAME_ERROR);                                                   /* ended in the middle of a frame */
    }
}

/* ---- the entry points (same shape as tls_connect / tls_poll / tls_close) ---- */
static inline void h3_get(u32 ip, u16 udp_port, const char *host, const char *host_header, const char *path,
                          const u8 seed[16], int forced) {
    u8 *z = (u8 *)&h3; for (u32 i = 0; i < sizeof(h3); i++) z[i] = 0;
    u32 k;
    for (k = 0; host[k] && k < sizeof(h3.host) - 1; k++) h3.host[k] = host[k];
    for (k = 0; host_header[k] && k < sizeof(h3.host_header) - 1; k++) h3.host_header[k] = host_header[k];
    for (k = 0; path[k] && k < sizeof(h3.path) - 1; k++) h3.path[k] = path[k];
    for (k = 0; k < 16; k++) h3.seed[k] = seed[k];
    h3.ip = ip; h3.port = udp_port; h3.forced = (u8)forced;
    if (hr_post_active) { h3.post = 1; h3.post_body = hr_post_body; h3.post_len = hr_post_len; }
    h3.state = H3_CONNECTING;
    serial_puts("[H3] GET "); serial_puts(path); serial_puts(" from "); net_log_ip(ip); serial_puts(" over QUIC\n");
    quic_connect(ip, udp_port, host, seed);
}

static inline h3_state_t h3_poll(u64 now_packed) {
    if (h3.state == H3_IDLE || h3.state == H3_DONE || h3.state == H3_FAILED) return h3.state;
    quic_state_t qs = quic_poll(now_packed);
    if (qs == QUIC_FAILED || qs == QUIC_CLOSED) {
        /* the server may close the connection right after a complete answer: finish what already arrived first */
        if (h3.request_sent) { h3_pump_response(); if (h3.resp_done) { h3.state = H3_DONE; return h3.state; } }
        if (h3.state != H3_FAILED) { h3.state = H3_FAILED; if (!h3.err) h3.err = 0xFFFFFFFFu; }
        return h3.state;
    }
    if (qs == QUIC_CONNECTED && !h3.request_sent) {
        static const u8 ctl[] = { 0x00, H3_FRAME_SETTINGS, 0x04, 0x01, 0x00, 0x07, 0x00 };   /* control stream type, SETTINGS { QPACK_MAX_TABLE_CAPACITY=0, QPACK_BLOCKED_STREAMS=0 } */
        u32 n = h3_build_request(h3.req);
        if (!quic_stream_write(0, ctl, sizeof(ctl), 0) || !quic_stream_write(1, h3.req, n, 1)) { h3_fail(H3_INTERNAL_ERROR); return h3.state; }
        h3.request_sent = 1; h3.state = H3_AWAITING;
    }
    if (h3.request_sent) {
        h3_pump_uni();
        if (h3.state == H3_FAILED) return h3.state;
        if (qc.s0_reset) { h3_fail(H3_GENERAL_PROTOCOL); return h3.state; }
        h3_pump_response();
        if (h3.state != H3_FAILED && h3.resp_done) h3.state = H3_DONE;
    }
    return h3.state;
}

static inline void h3_close(void) { if (qc.state == QUIC_CONNECTED) quic_close(H3_NO_ERROR); }

#endif
