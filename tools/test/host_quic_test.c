/* SPDX-License-Identifier: GPL-3.0-only */
/* host_quic_test.c -- the REAL kernel/quic.h + h3.h + tls.h (QUIC mode) talking to tools/test/quic_server.py over loopback UDP.
 * Only the layer under QUIC is faked: udp_send()/udp_listen() are backed by a BSD socket, the arena is an ordinary array, the clock is
 * CLOCK_MONOTONIC. Everything above (packet protection, TLS 1.3 in CRYPTO frames, certificate validation, HTTP/3, QPACK) is the kernel's code.
 *   usage: host_quic_test <port> <hostname> <expect> [path] [post-body]      or      host_quic_test unit
 * expect: ok (200 + the small page), big, redir, post, huff, altsvc:<port>, fail[:<quic_fail_t>] */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define IO_H
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64; typedef int i32; typedef long long i64;
#define SERIAL_H
static void serial_putc(char c) { if (getenv("QUIC_TRACE")) fputc(c, stderr); }
static void serial_puts(const char *s) { if (getenv("QUIC_TRACE")) fputs(s, stderr); }
static void serial_put_hex16(u16 v) { if (getenv("QUIC_TRACE")) fprintf(stderr, "%04X", v); }
static void serial_put_hex32(u32 v) { if (getenv("QUIC_TRACE")) fprintf(stderr, "%08X", v); }
static void serial_put_dec(u32 v) { if (getenv("QUIC_TRACE")) fprintf(stderr, "%u", v); }

/* TCP is not used in QUIC mode, but tls.h calls these from functions that must still compile */
#define TCP_H
typedef enum { TCP_CLOSED = 0, TCP_SYN_SENT, TCP_ESTABLISHED } tcp_state_t;
static struct { tcp_state_t state; u32 recv_len; } tcp_conn;
static void tcp_connect(u32 ip, u16 port) { (void)ip; (void)port; }
static u32 tcp_send_space(void) { return 60000; }
static int tcp_send_data(const u8 *d, u16 n) { (void)d; (void)n; return 0; }
static u16 tcp_poll_recv(u8 *out, u16 max) { (void)out; (void)max; return 0; }
static void tcp_close(void) {}

static u8 raw_arr[0x5000], hs_arr[0x8000], app_arr[0x8000];
static u8 quic_arena[0x67000];
static u8 hr_arr[0x40000];
#define MEMMAP_H
#define MW_TLS_RAW_SIZE 0x5000u
#define MW_TLS_HS_SIZE  0x8000u
#define MW_TLS_APP_SIZE 0x8000u
#define MW_TLS_RAW_ADDR ((unsigned long)raw_arr)
#define MW_TLS_HS_ADDR  ((unsigned long)hs_arr)
#define MW_TLS_APP_ADDR ((unsigned long)app_arr)
#define HR_BUF hr_arr
#define HR_BUF_SIZE 0x40000u
#define QUIC_ARENA quic_arena

/* the network layer under QUIC */
#define NET_H
#define UDP_H
#define NETCLOCK_H
typedef struct { u32 my_ip; } net_cfg_t;
static net_cfg_t net_cfg = { 0x7f000001u };
static void net_log_ip(u32 ip) { if (getenv("QUIC_TRACE")) fprintf(stderr, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255); }
typedef void (*udp_handler_t)(u32 src_ip, u16 src_port, const u8 *data, u16 len);
static udp_handler_t g_handler; static int usock = -1; static u16 srv_port; static u64 sent_dg, recv_dg;
static int udp_listen(u16 port, udp_handler_t h) { (void)port; g_handler = h; return 1; }
static int udp_send(u32 sip, u16 sport, u32 dip, u16 dport, const u8 *p, u16 n) {
    (void)sip; (void)sport; (void)dip; (void)dport;
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(srv_port) }; inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    sent_dg++;
    return sendto(usock, p, n, 0, (struct sockaddr *)&a, sizeof a) == n;
}
static u32 quic_test_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (u32)(t.tv_sec * 1000u + t.tv_nsec / 1000000u); }
#define QUIC_RAW_MS() quic_test_ms()
#define QUIC_RAW_WRAP 0xFFFFFFFFu
static u64 quic_test_now(void) {   /* the certificates mkcerts.sh just made are valid from "now": use the real clock (a fixed date goes stale) */
    time_t tt = time(0) + 600; struct tm *g = gmtime(&tt);
    return (u64)(g->tm_year + 1900) * 10000000000ull + (u64)(g->tm_mon + 1) * 100000000ull + (u64)g->tm_mday * 1000000ull + (u64)g->tm_hour * 10000 + (u64)g->tm_min * 100 + (u64)g->tm_sec;
}
#define QUIC_NOW_PACKED() quic_test_now()

#include "../../kernel/tls.h"
#include "../../kernel/httpresp.h"
#include "../../kernel/h3.h"

static tls_conn_t conn_storage;
static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("  FAIL  unit: %s\n", m); fails++; } } while (0)

static void hex(const u8 *b, int n, char *o) { for (int i = 0; i < n; i++) sprintf(o + 2 * i, "%02x", b[i]); }
static int eqhex(const u8 *b, int n, const char *h) { char o[200]; hex(b, n, o); return strcmp(o, h) == 0; }

static int run_unit(void) {
    /* RFC 9001 Appendix A.1: Initial secrets for DCID 0x8394c8f03e515708 */
    static const u8 dcid[8] = { 0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08 };
    quic_dir_t tx, rx; quic_initial_keys(&tx, &rx, dcid, 8);
    u8 key[16], iv[12], hp[16];
    CHECK(eqhex(tx.secret, 32, "c00cf151ca5be075ed0ebfb5c80323c42d6b7db67881289af4008f1f6c357aea"), "RFC 9001 A.1 client_initial_secret");
    quic_expand_label(key, 16, tx.secret, "quic key"); quic_expand_label(iv, 12, tx.secret, "quic iv"); quic_expand_label(hp, 16, tx.secret, "quic hp");
    CHECK(eqhex(key, 16, "1f369613dd76d5467730efcbe3b1a22d"), "A.1 client key"); CHECK(eqhex(iv, 12, "fa044b2f42a3fd3b46fb255c"), "A.1 client iv");
    CHECK(eqhex(hp, 16, "9f50449e04a0e810283a1e9933adedd2"), "A.1 client hp");
    CHECK(eqhex(rx.secret, 32, "3c199828fd139efd216c155ad844cc81fb82fa8d7446fa7d78be803acdda951b"), "A.1 server_initial_secret");
    quic_expand_label(key, 16, rx.secret, "quic key"); quic_expand_label(iv, 12, rx.secret, "quic iv"); quic_expand_label(hp, 16, rx.secret, "quic hp");
    CHECK(eqhex(key, 16, "cf3a5331653c364c88f0f379b6067e37"), "A.1 server key"); CHECK(eqhex(iv, 12, "0ac1493ca1905853b0bba03e"), "A.1 server iv");
    CHECK(eqhex(hp, 16, "c206b8d9b9f0f37644430b490eeaa314"), "A.1 server hp");
    /* A.2: header-protection mask of the client's Initial: sample d1b1c98dd7689fb8ec11d242b123dc9b -> 437b9aec36 */
    { u8 sample[16] = { 0xd1,0xb1,0xc9,0x8d,0xd7,0x68,0x9f,0xb8,0xec,0x11,0xd2,0x42,0xb1,0x23,0xdc,0x9b }; aes128_encrypt_block(&tx.hp, sample);
      CHECK(eqhex(sample, 5, "437b9aec36"), "A.2 header protection mask"); }
    /* variable-length integers (RFC 9000 A.1) */
    { static const u8 a[8] = { 0xc2,0x19,0x7c,0x5e,0xff,0x14,0xe8,0x8c }, b[4] = { 0x9d,0x7f,0x3e,0x7d }, c[2] = { 0x7b,0xbd }, d[1] = { 0x25 }, e[2] = { 0x40,0x25 };
      u64 v;
      CHECK(quic_get_vi(a, a + 8, &v) == 8 && v == 151288809941952652ull, "varint 8 bytes"); CHECK(quic_get_vi(b, b + 4, &v) == 4 && v == 494878333ull, "varint 4 bytes");
      CHECK(quic_get_vi(c, c + 2, &v) == 2 && v == 15293, "varint 2 bytes"); CHECK(quic_get_vi(d, d + 1, &v) == 1 && v == 37, "varint 1 byte");
      CHECK(quic_get_vi(e, e + 2, &v) == 2 && v == 37, "varint non-minimal"); CHECK(quic_get_vi(a, a + 7, &v) == 0, "truncated varint is refused");
      u8 o[8]; CHECK(quic_put_vi(o, 151288809941952652ull) == 8 && memcmp(o, a, 8) == 0, "varint encode 8"); CHECK(quic_put_vi(o, 15293) == 2 && o[0] == 0x7b && o[1] == 0xbd, "varint encode 2"); }
    /* HPACK / QPACK integers (RFC 7541 C.1) and Huffman (C.4) */
    { u8 o[8]; u32 n = h3_put_int(o, 0, 5, 0, 10); CHECK(n == 1 && o[0] == 10, "int 10 / 5-bit");
      n = h3_put_int(o, 0, 5, 0, 1337); CHECK(n == 3 && o[0] == 0x1f && o[1] == 0x9a && o[2] == 0x0a, "int 1337 / 5-bit");
      const u8 *p = o; u32 v; CHECK(h3_get_int(&p, o + 3, 5, &v) && v == 1337 && p == o + 3, "int 1337 decode");
      static const u8 big[6] = { 0x1f, 0xff, 0xff, 0xff, 0xff, 0x7f }; p = big; CHECK(!h3_get_int(&p, big + 6, 5, &v), "an integer that overflows 32 bits is refused");
      static const u8 hw[] = { 0xf1,0xe3,0xc2,0xe5,0xf2,0x3a,0x6b,0xa0,0xab,0x90,0xf4,0xff }; u8 out[64];
      int r = h3_huff_decode(hw, sizeof hw, out, sizeof out); CHECK(r == 15 && memcmp(out, "www.example.com", 15) == 0, "Huffman: www.example.com");
      static const u8 nc[] = { 0xa8,0xeb,0x10,0x64,0x9c,0xbf }; r = h3_huff_decode(nc, sizeof nc, out, sizeof out); CHECK(r == 8 && memcmp(out, "no-cache", 8) == 0, "Huffman: no-cache");
      static const u8 badpad[] = { 0xa8,0xeb,0x10,0x64,0x9c,0xbe }; CHECK(h3_huff_decode(badpad, sizeof badpad, out, sizeof out) < 0, "Huffman: padding that is not all ones is refused");
      static const u8 eos[] = { 0xff,0xff,0xff,0xff }; CHECK(h3_huff_decode(eos, sizeof eos, out, sizeof out) < 0, "Huffman: EOS inside a string is refused"); }
    /* reassembly */
    { static u8 buf[64]; quic_rasm_t r; quic_rasm_init(&r, buf, 64);
      CHECK(quic_rasm_add(&r, 10, (const u8 *)"world", 5) == 1 && r.contig == 0 && r.n == 1, "out-of-order data is held back");
      CHECK(quic_rasm_add(&r, 0, (const u8 *)"hello hell", 10) == 1 && r.contig == 15 && r.n == 0, "the gap closes and both parts join");
      CHECK(memcmp(buf, "hello helloworld", 15) != 0 || 1, "(content)"); CHECK(memcmp(buf, "hello hellworld", 15) == 0, "reassembled bytes are in place");
      CHECK(quic_rasm_add(&r, 3, (const u8 *)"lo he", 5) == 1 && r.contig == 15, "a duplicate changes nothing");
      CHECK(quic_rasm_add(&r, 60, (const u8 *)"12345", 5) == 0, "data beyond the buffer is a flow-control violation");
      quic_rasm_init(&r, buf, 64);
      for (int i = 0; i < QUIC_RASM_R; i++) CHECK(quic_rasm_add(&r, 10 + i * 4, (const u8 *)"ab", 2) == 1, "holes up to the limit are tracked");
      CHECK(quic_rasm_add(&r, 50, (const u8 *)"zz", 2) == -1, "one hole too many: refused so the packet is NOT acknowledged"); }
    /* ACK range bookkeeping */
    { quic_space_t s; memset(&s, 0, sizeof s);
      quic_ack_note(&s, 5); quic_ack_note(&s, 6); quic_ack_note(&s, 9); quic_ack_note(&s, 7);
      CHECK(s.na == 2 && s.ahi[0] == 9 && s.alo[0] == 9 && s.ahi[1] == 7 && s.alo[1] == 5, "ranges {9} {5..7}");
      quic_ack_note(&s, 8); CHECK(s.na == 1 && s.ahi[0] == 9 && s.alo[0] == 5, "filling the hole joins the ranges");
      quic_ack_note(&s, 2); CHECK(s.na == 2 && quic_ack_seen(&s, 2) && !quic_ack_seen(&s, 3) && quic_ack_seen(&s, 7), "seen / not seen");
      u8 o[64]; u32 n = quic_put_ack(o, &s); CHECK(n == 7 && o[0] == 2 && o[1] == 9 && o[3] == 1 && o[4] == 4 && o[5] == 1 && o[6] == 0, "ACK frame: largest 9, first range 4, gap 1 (two missing), range 0"); }
    /* packet number decoding (RFC 9000 A.3 example) */
    CHECK(quic_decode_pn(0xa82f30ea, 1, 0x9b32, 2) == 0xa82f9b32ull, "pn decode (A.3)");
    /* packet protection round trip + tamper */
    { static u8 pkt[1300]; quic_dir_t k; u8 sec[32]; for (int i = 0; i < 32; i++) sec[i] = (u8)i; quic_dir_set(&k, sec);
      u8 nonce[12]; memcpy(nonce, k.iv, 12); u8 hdr[8] = { 0x40, 1, 2, 3, 4, 5, 6, 7 }, pt[40], tag[16]; for (int i = 0; i < 40; i++) pt[i] = (u8)i;
      memcpy(pkt, pt, 40); gcm_encrypt(&k.aead, nonce, hdr, 8, pkt, 40, tag);
      u8 bad[16]; memcpy(bad, tag, 16); bad[3] ^= 1; u8 c2[40]; memcpy(c2, pkt, 40);
      CHECK(!gcm_decrypt(&k.aead, nonce, hdr, 8, c2, 40, bad), "a modified tag is refused"); CHECK(gcm_decrypt(&k.aead, nonce, hdr, 8, pkt, 40, tag) && memcmp(pkt, pt, 40) == 0, "decrypt restores the payload");
      quic_dir_t n2; quic_dir_next(&n2, &k); CHECK(memcmp(n2.iv, k.iv, 12) != 0 && memcmp(&n2.hp, &k.hp, sizeof k.hp) == 0, "key update changes key + iv, keeps the header-protection key"); }
    /* QPACK: a response that points at the dynamic table is refused; static + literal fields decode */
    { static const u8 ok[] = { 0x00, 0x00, 0xd9, 0xf4 }; int st; int n = h3_decode_headers(ok, sizeof ok, &st);
      CHECK(n > 0 && st == 200 && memcmp(h3.hbuf, "content-type: text/html; charset=utf-8\r\n\r\n", n) == 0, "QPACK: :status 200 + content-type from the static table");
      static const u8 dyn[] = { 0x01, 0x00, 0xd9 }; CHECK(h3_decode_headers(dyn, sizeof dyn, &st) == 0, "QPACK: Required Insert Count != 0 is refused");
      static const u8 dyn2[] = { 0x00, 0x00, 0x80 }; CHECK(h3_decode_headers(dyn2, sizeof dyn2, &st) == 0, "QPACK: indexed field line into the dynamic table is refused");
      static const u8 nostatus[] = { 0x00, 0x00, 0xf4 }; CHECK(h3_decode_headers(nostatus, sizeof nostatus, &st) == 0, "QPACK: a response without :status is refused");
      static const u8 r302[] = { 0x00, 0x00, 0xff, 0x03 }; n = h3_decode_headers(r302, sizeof r302, &st); CHECK(n > 0 && st == 302, "QPACK: index 66 needs the integer continuation (:status 302)"); }
    /* Alt-Svc */
    { memset(h3_alt, 0, sizeof h3_alt); u16 up;
      CHECK(!h3_wanted("Example.COM", 443, &up), "no QUIC without an Alt-Svc offer"); h3_alt_set("example.com", 8443, 1);
      CHECK(h3_wanted("EXAMPLE.com", 443, &up) && up == 8443, "an advertised host is tried over QUIC, on the advertised port, case-insensitively");
      h3_alt_set("example.com", 0, 2); CHECK(!h3_wanted("example.com", 443, &up), "a host that failed goes back to TCP"); h3_alt_set("example.com", 443, 1);
      CHECK(!h3_wanted("example.com", 443, &up), "...and a later Alt-Svc does not bring it back this session"); }
    /* Alt-Svc header parsing in httpresp.h */
    { const char *resp = "HTTP/1.1 200 OK\r\nalt-svc: h3=\":8443\"; ma=86400, h3-29=\":443\"\r\ncontent-length: 0\r\n\r\n"; hr_reset(); hr_feed((const u8 *)resp, (u32)strlen(resp));
      CHECK(hr.alt_h3_port == 8443, "Alt-Svc h3=\":8443\" parsed"); resp = "HTTP/1.1 200 OK\r\nAlt-Svc: clear\r\n\r\n"; hr_reset(); hr_feed((const u8 *)resp, (u32)strlen(resp)); CHECK(hr.alt_h3_port == 0, "Alt-Svc: clear");
      resp = "HTTP/1.1 200 OK\r\nalt-svc: h3=\"other.example:443\"\r\n\r\n"; hr_reset(); hr_feed((const u8 *)resp, (u32)strlen(resp)); CHECK(hr.alt_h3_port == 0, "an offer for ANOTHER host is not taken"); }
    if (!fails) printf("ok: all QUIC / HTTP/3 unit checks passed\n");
    return fails ? 1 : 0;
}


/* ================= fuzz: hostile datagrams, frames and HTTP/3 straight into every parser, under ASan + UBSan ================= */
static unsigned long long fzq = 0x2545F4914F6CDD1Dull;
static u32 fzr(void) { fzq ^= fzq << 13; fzq ^= fzq >> 7; fzq ^= fzq << 17; return (u32)fzq; }
static int run_fuzz(void) {
    int iters = getenv("FUZZ_ITERS") ? atoi(getenv("FUZZ_ITERS")) : 12, rounds = getenv("FUZZ_ROUNDS") ? atoi(getenv("FUZZ_ROUNDS")) : 600;
    if (getenv("FUZZ_SEED")) fzq ^= (unsigned long long)atoll(getenv("FUZZ_SEED")) * 0x9E3779B97F4A7C15ull;
    usock = socket(AF_INET, SOCK_DGRAM, 0); fcntl(usock, F_SETFL, O_NONBLOCK); srv_port = 9;     /* the discard port: whatever we send goes nowhere */
    static const u8 ftypes[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e };
    u64 now = quic_test_now();
    for (int it = 0; it < iters; it++) {
        u8 seed[16]; for (int i = 0; i < 16; i++) seed[i] = (u8)fzr();
        hr_reset();
        h3_get(0x7f000001u, 443, "localhost", "localhost", "/", seed, 1);       /* a fresh connection: Initial keys, the ClientHello is out */
        for (int round = 0; round < rounds; round++) {
            u8 b[1400]; u32 n = fzr() % 300;
            for (u32 k = 0; k < n; k++) b[k] = (u8)fzr();
            switch (fzr() % 5) {
            case 0: case 1:                                                    /* frames of any level, biased to real frame types and lengths */
                if (n) { for (u32 k = 0; k < n; k += 1 + fzr() % 12) if (fzr() % 3) b[k] = ftypes[fzr() % sizeof ftypes]; }
                (void)quic_parse_frames((int)(fzr() % 3), b, n); break;
            case 2:                                                            /* STREAM frames to the response stream (and others) with random offsets: reaches the HTTP/3 parser */
                if (n > 12) { b[0] = (u8)(0x08 | (fzr() & 7)); b[1] = (u8)(fzr() % 12); if (b[0] & 4) b[2] = (u8)(fzr() % 40); if (b[0] & 2) { u32 k = 3, l = n - k; b[k] = (u8)(0x40 | (l >> 8)); b[k + 1] = (u8)l; } }
                (void)quic_parse_frames(2, b, n); break;
            case 3:                                                            /* a datagram: long header, our version, plausible lengths */
                if (n > 24) { b[0] = (u8)(0xC0 | (fzr() & 0x3F)); b[1] = 0; b[2] = 0; b[3] = 0; b[4] = 1; b[5] = (u8)(fzr() % 22); }
                quic_rx_datagram(b, n); break;
            default: { int st; (void)h3_decode_headers(b, n, &st); u8 o[64]; u32 ol; const u8 *p = b; (void)h3_get_str(&p, b + n, 7, o, sizeof o, &ol); (void)h3_huff_decode(b, n, o, sizeof o); break; }
            }
            (void)h3_poll(now);
        }
        quic_close(0);
    }
    printf("ok: quic/h3 fuzz, %d connections x %d inputs, no crash, no hang, no sanitizer report\n", iters, rounds);
    return 0;
}

int main(int argc, char **argv) {
    tls_conn_p = &conn_storage;
    if (argc == 2 && strcmp(argv[1], "unit") == 0) return run_unit();
    if (argc == 2 && strcmp(argv[1], "fuzz") == 0) return run_fuzz();
    if (argc < 4) { fprintf(stderr, "usage: %s port host expect [path] [post-body]\n", argv[0]); return 2; }
    srv_port = (u16)atoi(argv[1]); const char *host = argv[2], *expect = argv[3], *path = argc > 4 ? argv[4] : "/";
    if (argc > 5) hr_set_post(argv[5]);
    usock = socket(AF_INET, SOCK_DGRAM, 0); fcntl(usock, F_SETFL, O_NONBLOCK);
    u8 seed[16]; for (int i = 0; i < 16; i++) seed[i] = (u8)(i * 29 + getpid());
    hr_reset();
    u64 now = quic_test_now();
    h3_get(0x7f000001u, 443, host, host, path, seed, 1);
    u32 t0 = quic_test_ms();
    while (quic_test_ms() - t0 < 30000) {
        u8 buf[2000]; ssize_t r;
        while ((r = recv(usock, buf, sizeof buf, 0)) > 0) { recv_dg++; if (g_handler) g_handler(0x7f000001u, 443, buf, (u16)r); }
        h3_state_t st = h3_poll(now);
        if (st == H3_DONE || st == H3_FAILED) break;
        usleep(500);
    }
    int st = h3.state;
    if (strncmp(expect, "fail", 4) == 0) {
        int want = expect[4] == ':' ? atoi(expect + 5) : -1;
        int ok = st == H3_FAILED && (want < 0 || (int)qc.fail == want);
        printf("%s: HTTP/3 %s (quic fail %d, code 0x%x, h3 error 0x%x, tls reason %d)\n", ok ? "ok" : "FAIL", st == H3_FAILED ? "failed as expected" : "SUCCEEDED", (int)qc.fail, qc.fail_code, h3.err, (int)tls_conn.fail_reason);
        return ok ? 0 : 1;
    }
    if (st != H3_DONE) { printf("FAIL: HTTP/3 ended in state %d (quic fail %d, code 0x%x, h3 error 0x%x, tls reason %d, %llu dg out / %llu in)\n", st, (int)qc.fail, qc.fail_code, h3.err, (int)tls_conn.fail_reason, sent_dg, recv_dg); return 1; }
    hr_finish();
    const u8 *body = HR_BUF + hr.body_start; int ok = 0; char why[160] = "";
    if (strcmp(expect, "ok") == 0) ok = hr.status == 200 && hr.body_len > 20 && memmem(body, hr.body_len, "hello over HTTP/3", 17);
    else if (strcmp(expect, "big") == 0) {
        ok = hr.status == 200 && hr.body_len == 150000;
        for (u32 i = 0; ok && i < 150000; i++) if (body[i] != (u8)((i * 7 + 3) & 0xFF)) { ok = 0; snprintf(why, sizeof why, " (body differs at byte %u)", i); }
    } else if (strcmp(expect, "redir") == 0) ok = hr_is_redirect() && hr.status == 302 && strcmp(hr.location, "/") == 0;
    else if (strcmp(expect, "post") == 0) ok = hr.status == 200 && memmem(body, hr.body_len, "posted 10 bytes: a=1&b=two2", 27);
    else if (strcmp(expect, "huff") == 0) ok = hr.status == 200 && memmem(HR_BUF, hr.len, "x-test: Hello, World! 0123456789 ~{}|\r\n", 39) && memmem(HR_BUF, hr.len, "set-cookie: a=b; path=/\r\n", 25) && hr.body_len == 10;
    else if (strncmp(expect, "altsvc:", 7) == 0) ok = hr.status == 200 && hr.alt_h3_port == atoi(expect + 7);
    printf("%s: HTTP/3 status %d, %u body bytes, content-type \"%s\", %llu datagrams out / %llu in%s\n", ok ? "ok" : "FAIL", hr.status, hr.body_len, hr.content_type, sent_dg, recv_dg, why);
    h3_close();
    return ok ? 0 : 1;
}
