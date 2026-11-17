/* SPDX-License-Identifier: GPL-3.0-only */
/* host_tls13_test.c -- the REAL kernel/tls.h (TLS 1.2 and 1.3) talking to a REAL `openssl s_server` over a loopback socket.
 * Only the layer under TLS is faked: the tcp_* calls tls.h makes are backed by a non-blocking BSD socket instead of the
 * kernel's TCP stack, port I/O is not needed, and the three TLS buffers are ordinary arrays.
 *   usage: host_tls13_test <port> <hostname> <test_roots.h already compiled in> <expect: 13|12|fail> [ciphersuites-note]
 * run_host_tls13.sh drives it against several server configurations (RSA / P-256 / P-384 leaf; AES-128 / AES-256; 1.2 and 1.3). */
#define _GNU_SOURCE
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define IO_H
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64; typedef int i32; typedef long long i64;
#define SERIAL_H
/* the certificates mkcerts.sh makes are valid from the moment it ran, so "now" must follow the real clock (a fixed date goes stale) */
static u64 test_now(void) { time_t tt = time(0) + 600; struct tm *g = gmtime(&tt);
    return (u64)(g->tm_year + 1900) * 10000000000ull + (u64)(g->tm_mon + 1) * 100000000ull + (u64)g->tm_mday * 1000000ull + (u64)g->tm_hour * 10000 + (u64)g->tm_min * 100 + (u64)g->tm_sec; }
static void serial_putc(char c) { if (getenv("TLS_TRACE")) fputc(c, stderr); }
static void serial_puts(const char *s) { if (getenv("TLS_TRACE")) fputs(s, stderr); }
static void serial_put_hex16(u16 v) { if (getenv("TLS_TRACE")) fprintf(stderr, "%04X", v); }
static void serial_put_dec(u32 v) { if (getenv("TLS_TRACE")) fprintf(stderr, "%u", v); }

/* ---- the layer under TLS: tcp_* over a socket ---- */
#define TCP_H
typedef enum { TCP_CLOSED = 0, TCP_SYN_SENT, TCP_ESTABLISHED } tcp_state_t;
static struct { tcp_state_t state; u32 recv_len; } tcp_conn;
static int sock = -1;
static u16 test_port;
static void tcp_connect(u32 ip, u16 port) {
    (void)ip; sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(test_port ? test_port : port) }; inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (connect(sock, (struct sockaddr *)&a, sizeof a) == 0) { fcntl(sock, F_SETFL, O_NONBLOCK); tcp_conn.state = TCP_ESTABLISHED; } else tcp_conn.state = TCP_CLOSED;
}
static u32 tcp_send_space(void) { return 60000; }
static int tcp_send_data(const u8 *d, u16 n) { u16 off = 0; while (off < n) { ssize_t w = send(sock, d + off, n - off, 0); if (w <= 0) { if (errno == EAGAIN) continue; return 0; } off += (u16)w; } return 1; }
static u16 tcp_poll_recv(u8 *out, u16 max) { ssize_t r = recv(sock, out, max, 0); return r > 0 ? (u16)r : 0; }
static void tcp_close(void) { if (sock >= 0) close(sock); sock = -1; tcp_conn.state = TCP_CLOSED; }

static u8 raw_arr[0x5000], hs_arr[0x8000], app_arr[0x8000];
#define MEMMAP_H
#define MW_TLS_RAW_SIZE 0x5000u
#define MW_TLS_HS_SIZE  0x8000u
#define MW_TLS_APP_SIZE 0x8000u
#define MW_TLS_RAW_ADDR ((unsigned long)raw_arr)
#define MW_TLS_HS_ADDR  ((unsigned long)hs_arr)
#define MW_TLS_APP_ADDR ((unsigned long)app_arr)

#include "../../kernel/tls.h"

static u8 test_ca_der[4096]; static u32 test_ca_len;
static tls_conn_t conn_storage;

/* ================= unit tests: crafted input straight into tls.h's parsers (no server) ================= */
static int u_fail;
#define UCHECK(cond, msg) do { if (!(cond)) { printf("  FAIL  unit: %s\n", msg); u_fail++; } } while (0)
static void u_reset(void) {
    memset(&conn_storage, 0, sizeof conn_storage); tls_conn_p = &conn_storage;
    tls_conn.state = TLS_CLIENT_HELLO_SENT; tls_conn.sent_group = TLS_GROUP_X25519;
}
/* ServerHello / HelloRetryRequest body: legacy_version, random, empty session id, suite, null compression, extensions */
static u32 u_sh(u8 *o, const u8 *random, u16 suite, int ver13, int ks_form, u16 group, const u8 *cookie, int cklen) {
    u32 p = 0; o[p++] = 3; o[p++] = 3; memcpy(o + p, random, 32); p += 32; o[p++] = 0; o[p++] = (u8)(suite >> 8); o[p++] = (u8)suite; o[p++] = 0;
    u32 el = p; p += 2;
    if (ver13) { o[p++] = 0; o[p++] = 43; o[p++] = 0; o[p++] = 2; o[p++] = 3; o[p++] = 4; }
    if (ks_form == 2) { o[p++] = 0; o[p++] = 51; o[p++] = 0; o[p++] = 2; o[p++] = (u8)(group >> 8); o[p++] = (u8)group; }          /* HRR form */
    if (cklen) { o[p++] = 0; o[p++] = 44; o[p++] = (u8)((2 + cklen) >> 8); o[p++] = (u8)(2 + cklen); o[p++] = (u8)(cklen >> 8); o[p++] = (u8)cklen; memcpy(o + p, cookie, cklen); p += cklen; }
    o[el] = (u8)((p - el - 2) >> 8); o[el + 1] = (u8)(p - el - 2);
    return p;
}
static const u8 u_hrr_random[32] = { 0xCF,0x21,0xAD,0x74,0xE5,0x9A,0x61,0x11,0xBE,0x1D,0x8C,0x02,0x1E,0x65,0xB8,0x91,0xC2,0xA2,0x11,0x16,0x7A,0xBB,0x8C,0x5E,0x07,0x9E,0x09,0xE2,0xC8,0xA8,0x33,0x9C };

static int run_unit(void) {
    u8 b[512], rnd[32]; u32 n;
    for (int i = 0; i < 32; i++) rnd[i] = (u8)(i + 1);

    /* downgrade sentinel: a 1.2 ServerHello to a client that offered 1.3 */
    u_reset(); n = u_sh(b, rnd, 0xC02F, 0, 0, 0, 0, 0);
    UCHECK(tls_parse_server_hello(b, n) == 1, "plain TLS 1.2 ServerHello is accepted");
    for (int v = 0; v < 2; v++) {
        u8 r2[32]; memcpy(r2, rnd, 32); memcpy(r2 + 24, "DOWNGRD", 7); r2[31] = (u8)v;
        u_reset(); n = u_sh(b, r2, 0xC02F, 0, 0, 0, 0, 0);
        UCHECK(tls_parse_server_hello(b, n) == 0 && tls_conn.fail_reason == TLS_FAIL_PROTOCOL_VERSION, "DOWNGRD sentinel (0x00 / 0x01) in a 1.2 ServerHello is refused");
    }

    /* a second ServerHello after the first one chose 1.3 (used to fall through to the 1.2 path) / 1.2 */
    u_reset(); tls_conn.is13 = 1; tls_conn.sh_seen = 0; /* (sh_seen off on purpose: this isolates the dispatcher's is13 routing from the sh_seen guard) */ tls_conn.hs13_stage = 1; tls_conn.cipher_suite = TLS13_SUITE_AES128_GCM_SHA256; n = u_sh(b, rnd, 0x1301, 1, 0, 0, 0, 0);
    tls_dispatch_handshake_message(TLS_HS_SERVER_HELLO, b, n, 0);
    UCHECK(tls_conn.state == TLS_FAILED && tls_conn.fail_reason == TLS_FAIL_UNEXPECTED_MESSAGE, "second ServerHello during a 1.3 handshake fails");
    u_reset(); tls_conn.sh_seen = 1; n = u_sh(b, rnd, 0xC02F, 0, 0, 0, 0, 0);
    tls_dispatch_handshake_message(TLS_HS_SERVER_HELLO, b, n, 0);
    UCHECK(tls_conn.state == TLS_FAILED && tls_conn.fail_reason == TLS_FAIL_UNEXPECTED_MESSAGE, "second ServerHello during a 1.2 handshake fails");

    /* HelloRetryRequest validation */
    u_reset(); n = u_sh(b, u_hrr_random, 0x1301, 1, 2, TLS_GROUP_SECP256R1, 0, 0);
    UCHECK(tls_parse_server_hello(b, n) == 1 && tls_conn.hrr_pending && tls_conn.hrr_group == TLS_GROUP_SECP256R1 && !tls_conn.is13, "HRR for P-256 accepted, still not 'is13'");
    u_reset(); n = u_sh(b, u_hrr_random, 0x1301, 1, 2, TLS_GROUP_X25519, 0, 0);
    UCHECK(tls_parse_server_hello(b, n) == 0, "HRR asking for the group we already sent is refused");
    u_reset(); n = u_sh(b, u_hrr_random, 0x1301, 1, 2, 0x0101, 0, 0);
    UCHECK(tls_parse_server_hello(b, n) == 0, "HRR for a group we never offered (ffdhe2048) is refused");
    u_reset(); n = u_sh(b, u_hrr_random, 0x1301, 1, 0, 0, 0, 0);
    UCHECK(tls_parse_server_hello(b, n) == 0, "HRR that changes nothing is refused");
    u_reset(); tls_conn.hrr_done = 1; n = u_sh(b, u_hrr_random, 0x1301, 1, 2, TLS_GROUP_SECP256R1, 0, 0);
    UCHECK(tls_parse_server_hello(b, n) == 0, "a second HRR is refused");
    u_reset(); { u8 ck[40]; memset(ck, 0xAB, 40); n = u_sh(b, u_hrr_random, 0x1302, 1, 0, 0, ck, 40);
      UCHECK(tls_parse_server_hello(b, n) == 1 && tls_conn.cookie_len == 40 && tls_conn.hrr_group == TLS_GROUP_X25519 && tls_conn.cookie[39] == 0xAB, "cookie-only HRR keeps the group and stores the cookie"); }
    u_reset(); { u8 ck[TLS_COOKIE_MAX + 1]; memset(ck, 1, sizeof ck); static u8 big[600]; n = u_sh(big, u_hrr_random, 0x1301, 1, 2, TLS_GROUP_SECP256R1, ck, TLS_COOKIE_MAX + 1);
      UCHECK(tls_parse_server_hello(big, n) == 0 && tls_conn.fail_reason == TLS_FAIL_BUFFER_OVERFLOW, "an oversized cookie is refused, not truncated"); }

    /* KeyUpdate */
    u_reset(); tls_conn.state = TLS_ESTABLISHED; tls_conn.is13 = 1; tls_conn.cipher_suite = TLS13_SUITE_AES128_GCM_SHA256;
    { u8 bad = 2; tls13_dispatch(24, &bad, 1, 0); UCHECK(tls_conn.state == TLS_FAILED, "KeyUpdate with request_update=2 fails"); }
    u_reset(); tls_conn.state = TLS_ESTABLISHED; tls_conn.is13 = 1; tls_conn.cipher_suite = TLS13_SUITE_AES128_GCM_SHA256;
    { u8 two[2] = {0, 0}; tls13_dispatch(24, two, 2, 0); UCHECK(tls_conn.state == TLS_FAILED, "KeyUpdate with a 2-byte body fails"); }
    u_reset(); tls_conn.state = TLS_ESTABLISHED; tls_conn.is13 = 1; tls_conn.cipher_suite = TLS13_SUITE_AES128_GCM_SHA256; tls_conn.write_cipher_active = 1;
    memset(tls_conn.c_secret, 0x11, 32); memset(tls_conn.s_secret, 0x22, 32); u8 c0[32], s0[32]; memcpy(c0, tls_conn.c_secret, 32); memcpy(s0, tls_conn.s_secret, 32);
    { u8 req = 1; sock = -1; tls13_dispatch(24, &req, 1, 0);                       /* the answer cannot be sent (no socket) */
      UCHECK(tls_conn.state == TLS_ESTABLISHED, "KeyUpdate(update_requested) keeps the connection");
      UCHECK(memcmp(tls_conn.s_secret, s0, 32) != 0, "the server direction is rekeyed");
      UCHECK(memcmp(tls_conn.c_secret, c0, 32) == 0, "our direction is NOT rekeyed when the KeyUpdate answer could not be sent"); }

    /* alerts: TLS 1.3 ignores the level byte */
    struct { int is13; u8 level, desc; int fatal; const char *what; } al[] = {
        {1, 1, 40, 1, "1.3 warning-level handshake_failure is fatal"}, {1, 1, 0, 0, "1.3 close_notify is not fatal"}, {1, 1, 90, 0, "1.3 user_canceled is not fatal"},
        {1, 2, 0, 1, "1.3 fatal-level close_notify is fatal (level byte 2)"}, {0, 1, 40, 0, "1.2 warning-level alert stays non-fatal"}, {0, 2, 40, 1, "1.2 fatal alert is fatal"} };
    for (unsigned k = 0; k < sizeof al / sizeof al[0]; k++) {
        u_reset(); tls_conn.state = TLS_ESTABLISHED; tls_conn.is13 = (u8)al[k].is13;
        u8 rec[7] = { 21, 3, 3, 0, 2, al[k].level, al[k].desc }; memcpy(TLS_RX_RAW, rec, 7); tls_conn.rx_raw_len = 7; tls_process_raw_buffer(0);
        UCHECK((tls_conn.state == TLS_FAILED) == (al[k].fatal != 0), al[k].what);
    }

    /* TLS 1.3 record carrying more than 2^14 bytes of content (record_overflow) vs exactly 2^14 */
    for (int over = 0; over < 2; over++) {
        u_reset(); tls_conn.state = TLS_ESTABLISHED; tls_conn.is13 = 1; tls_conn.read_cipher_active = 1; tls_conn.cipher_suite = TLS13_SUITE_AES128_GCM_SHA256;
        u8 key[16]; memset(key, 7, 16); aes_set_key(&tls_conn.server_write_aes, key, 16); memset(tls_conn.server_iv12, 3, 12); tls_conn.server_seq = 0;
        u32 content = 16384 + (u32)over, inner = content + 1, plen = inner + 16;
        static u8 rec[5 + 16384 + 64]; u8 *ct = rec + 5; memset(ct, 'A', content); ct[content] = 23;
        u8 aad[5] = { 23, 3, 3, (u8)(plen >> 8), (u8)plen }, nonce[12], tag[16]; memcpy(nonce, tls_conn.server_iv12, 12);
        gcm_encrypt(&tls_conn.server_write_aes, nonce, aad, 5, ct, inner, tag); memcpy(ct + inner, tag, 16); memcpy(rec, aad, 5);
        memcpy(TLS_RX_RAW, rec, 5 + plen); tls_conn.rx_raw_len = 5 + plen; tls_process_raw_buffer(0);
        if (over) UCHECK(tls_conn.state == TLS_FAILED && tls_conn.fail_reason == TLS_FAIL_BAD_RECORD, "TLS 1.3 record with 16385 bytes of content is refused (record_overflow)");
        else UCHECK(tls_conn.state == TLS_ESTABLISHED && tls_conn.app_recv_len == 16384, "TLS 1.3 record with exactly 16384 bytes of content is accepted");
    }
    printf(u_fail ? "FAIL: %d unit check(s) failed\n" : "ok: all unit checks passed\n", u_fail);
    return u_fail ? 1 : 0;
}

/* KeyUpdate against a live server: s_server's stdin is fed "first", then 'K' (it rekeys and asks us to) or 'k' (it rekeys, no request), then "second".
 * Receiving "second" proves our read keys followed the server's; the data we send afterwards proves our write keys followed ours (the script greps the
 * server's output for it). */
static int run_ku(void) {
    u32 gl = 0; static char got[4096]; u64 now = test_now(); int sent1 = 0, sent2 = 0;
    for (int i = 0; i < 60000; i++) {
        tls_poll(now);
        if (tls_conn.state == TLS_FAILED) { printf("FAIL: connection failed (reason %d)\n", (int)tls_conn.fail_reason); return 1; }
        if (tls_conn.state != TLS_ESTABLISHED) { usleep(500); continue; }
        u16 n = tls_poll_recv_app_data((u8 *)got + gl, (u16)(sizeof got - 1 - gl)); gl += n; got[gl] = 0;
        if (!sent1 && strstr(got, "first")) { tls_send_app_data((const u8 *)"client-one\n", 11); sent1 = 1; }
        if (sent1 && !sent2 && strstr(got, "second")) { tls_send_app_data((const u8 *)"client-two-after-keyupdate\n", 27); sent2 = 1; usleep(300000); break; }
        if (!n) usleep(500);
    }
    printf(sent2 ? "ok: data flowed both ways across a KeyUpdate\n" : "FAIL: never received \"second\" after the KeyUpdate\n");
    tls_close();
    return sent2 ? 0 : 1;
}



/* ================= fuzz: a hostile server's bytes straight into every parser, under ASan + UBSan ================= */
static unsigned long long fz_s = 88172645463325252ull;
static u32 fz_rnd(void) { fz_s ^= fz_s << 13; fz_s ^= fz_s >> 7; fz_s ^= fz_s << 17; return (u32)fz_s; }
static void fz_mutate(u8 *b, u32 *n, u32 cap) {                     /* flip bytes, then maybe cut it short or pad it with noise */
    u32 flips = fz_rnd() % 6;
    for (u32 i = 0; i < flips && *n; i++) b[fz_rnd() % *n] ^= (u8)(1u << (fz_rnd() & 7));
    if (*n && !(fz_rnd() % 4)) b[fz_rnd() % *n] = (u8)fz_rnd();
    if (!(fz_rnd() % 4) && *n) *n = fz_rnd() % (*n + 1);
    if (!(fz_rnd() % 8)) { u32 add = fz_rnd() % 64; for (u32 i = 0; i < add && *n < cap; i++) b[(*n)++] = (u8)fz_rnd(); }
}
static int run_fuzz(void) {
    static u8 b[4096], rnd32[32];
    static const u16 suites[] = { 0x1301, 0x1302, 0xC02F, 0xC030, 0xC02B, 0xC02C, 0x009C, 0x1303, 0x0000 };
    static const u16 groups[] = { TLS_GROUP_X25519, TLS_GROUP_SECP256R1, TLS_GROUP_SECP384R1, 0x0101 };
    int iters = getenv("FUZZ_ITERS") ? atoi(getenv("FUZZ_ITERS")) : 40000;
    if (getenv("FUZZ_SEED")) fz_s ^= (unsigned long long)atoll(getenv("FUZZ_SEED")) * 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 32; i++) rnd32[i] = (u8)(i * 7 + 1);
    for (int it = 0; it < iters; it++) {
        u_reset();
        tls_conn.is13 = (u8)(fz_rnd() & 1); tls_conn.hrr_done = (u8)(fz_rnd() & 1); tls_conn.sent_group = groups[fz_rnd() % 4];
        tls_conn.cipher_suite = suites[fz_rnd() % 9]; tls_conn.hs13_stage = (u8)(fz_rnd() % 6); tls_conn.sh_seen = (u8)(fz_rnd() & 1);
        tls_conn.kx_curve = groups[fz_rnd() % 4]; tls_conn.state = (it & 8) ? TLS_CLIENT_HELLO_SENT : TLS_ESTABLISHED;
        u32 n = 0;
        switch (fz_rnd() % 8) {
        case 0: case 1: {                                            /* a ServerHello / HRR built from the real shape, then damaged */
            int hrr = (int)(fz_rnd() & 1); u8 ck[300]; for (int k = 0; k < 300; k++) ck[k] = (u8)fz_rnd();
            n = u_sh(b, hrr ? u_hrr_random : rnd32, suites[fz_rnd() % 9], (int)(fz_rnd() & 1), hrr ? 2 : (int)(fz_rnd() % 3), groups[fz_rnd() % 4], ck, (int)(fz_rnd() % 3 ? 0 : fz_rnd() % 290));
            fz_mutate(b, &n, sizeof b); (void)tls_parse_server_hello(b, n); break; }
        case 2: n = fz_rnd() % 400; for (u32 k = 0; k < n; k++) b[k] = (u8)fz_rnd(); (void)tls_parse_certificate_message(b, n); break;
        case 3: {                                                    /* a 1.3 Certificate: sane lengths around random contents */
            n = fz_rnd() % 400; for (u32 k = 0; k < n; k++) b[k] = (u8)fz_rnd();
            if (n > 8) { b[0] = (u8)(fz_rnd() % 4); u32 p = 1 + b[0]; if (p + 3 <= n) { u32 t = n - p - 3; b[p] = (u8)(t >> 16); b[p + 1] = (u8)(t >> 8); b[p + 2] = (u8)t; if (p + 6 <= n) { u32 cl = fz_rnd() % (t + 1); b[p + 3] = (u8)(cl >> 16); b[p + 4] = (u8)(cl >> 8); b[p + 5] = (u8)cl; } } }
            (void)tls13_parse_certificate(b, n); break; }
        case 4: n = fz_rnd() % 400; for (u32 k = 0; k < n; k++) b[k] = (u8)fz_rnd(); if (n > 4 && (fz_rnd() & 1)) { b[2] = (u8)((n - 4) >> 8); b[3] = (u8)(n - 4); b[0] = 8; b[1] = (u8)(fz_rnd() & 7); }
                (void)tls13_verify_certificate_verify(b, n); break;
        case 5: n = fz_rnd() % 300; for (u32 k = 0; k < n; k++) b[k] = (u8)fz_rnd(); (void)tls_parse_server_key_exchange(b, n); break;
        case 6: { n = fz_rnd() % 300; for (u32 k = 0; k < n; k++) b[k] = (u8)fz_rnd(); u8 t = (u8)(fz_rnd() % 26); sock = -1;
                  tls_dispatch_handshake_message(t, b, n, 20261107120000ull); break; }
        default: {                                                   /* raw records on the wire: random type / version / length, often consistent */
            u32 off = 0; int recs = 1 + (int)(fz_rnd() % 3);
            for (int r = 0; r < recs && off + 5 < 0x4000; r++) {
                u32 plen = fz_rnd() % 200, ty = 20 + fz_rnd() % 4; if (!(fz_rnd() % 6)) ty = fz_rnd() & 0xFF;
                TLS_RX_RAW[off++] = (u8)ty; TLS_RX_RAW[off++] = 3; TLS_RX_RAW[off++] = (u8)(fz_rnd() % 5); TLS_RX_RAW[off++] = (u8)(plen >> 8); TLS_RX_RAW[off++] = (u8)plen;
                for (u32 k = 0; k < plen; k++) TLS_RX_RAW[off + k] = (u8)fz_rnd();
                off += plen;
            }
            tls_conn.rx_raw_len = off > 0x5000 ? 0x5000 : off; sock = -1; tls_conn.read_cipher_active = (u8)(fz_rnd() & 1);
            tls_process_raw_buffer(20261107120000ull); break; }
        }
    }
    printf("ok: fuzz, %d inputs, no crash, no hang, no sanitizer report\n", iters);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "unit") == 0) return run_unit();
    if (argc == 2 && strcmp(argv[1], "fuzz") == 0) return run_fuzz();
    if (argc < 4) { fprintf(stderr, "usage: %s port host expect(13|12|fail) [wrongroot]\n", argv[0]); return 2; }
    test_port = (u16)atoi(argv[1]); const char *host = argv[2]; const char *expect = argv[3];
    static tls_conn_t conn; tls_conn_p = &conn;
    u8 seed[16]; for (int i = 0; i < 16; i++) seed[i] = (u8)(i * 31 + getpid());
    tls_connect(0, 443, host, seed);
    u64 now = test_now();
    int spins = 0;
    while (spins++ < 20000) {
        tls_state_t st = tls_poll(now);
        if (st == TLS_ESTABLISHED || st == TLS_FAILED) break;
        usleep(500);
    }
    int is13 = tls_conn.is13;
    if (strcmp(expect, "ku") == 0) { if (tls_conn.state != TLS_ESTABLISHED || !is13) { printf("FAIL: no TLS 1.3 session (state %d, reason %d)\n", (int)tls_conn.state, (int)tls_conn.fail_reason); return 1; } return run_ku(); }
    if (strcmp(expect, "fail") == 0) {
        int ok = tls_conn.state == TLS_FAILED;
        printf("%s: handshake %s as expected (reason %d)\n", ok ? "ok" : "FAIL", ok ? "failed" : "SUCCEEDED", (int)tls_conn.fail_reason);
        return ok ? 0 : 1;
    }
    if (tls_conn.state != TLS_ESTABLISHED) { printf("FAIL: handshake ended in state %d, reason %d (alert %d/%d)\n", (int)tls_conn.state, (int)tls_conn.fail_reason, tls_conn.alert_level, tls_conn.alert_desc); return 1; }
    int want13 = strcmp(expect, "13") == 0 || strcmp(expect, "hs13") == 0;
    if (is13 != want13) { printf("FAIL: negotiated TLS %s, expected %s (suite 0x%04X)\n", is13 ? "1.3" : "1.2", expect, tls_conn.cipher_suite); return 1; }
    if (strcmp(expect, "hs13") == 0) { printf("%s: TLS 1.3 handshake completed (the server then refuses the client: expected)\n", is13 ? "ok" : "FAIL"); tls_close(); return is13 ? 0 : 1; }
    /* application data both ways: s_server -www answers a GET with its status page */
    const char *req = "GET / HTTP/1.0\r\n\r\n";
    if (!tls_send_app_data((const u8 *)req, (u32)strlen(req))) { printf("FAIL: send\n"); return 1; }
    static char got[70000]; u32 gl = 0; int idle = 0;
    for (int i = 0; i < 40000 && idle < 4000; i++) {
        tls_poll(now);
        u16 n = tls_poll_recv_app_data((u8 *)got + gl, (u16)(sizeof got - 1 - gl)); gl += n;
        if (tls_conn.state == TLS_FAILED) break;
        if (n == 0) { idle++; usleep(300); } else idle = 0;
        if (tls_conn.peer_close_notify) break;
    }
    got[gl] = 0;
    int ok = strstr(got, "HTTP/1.0 200") != 0 && strstr(got, "s_server") != 0;
    printf("%s: TLS %s suite 0x%04X, %u bytes of application data back%s\n", ok ? "ok" : "FAIL", is13 ? "1.3" : "1.2", tls_conn.cipher_suite, gl, ok ? "" : " (no 200 page)");
    tls_close();
    return ok ? 0 : 1;
}
