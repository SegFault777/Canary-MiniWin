/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QUIC_H
#define QUIC_H
#include "io.h"
#include "net.h"
#include "udp.h"
#include "tls.h"
#include "serial.h"
#include "netclock.h"
#ifndef QUIC_NOW_PACKED
#include "rtc.h"
/* The certificate date check needs the wall clock at the moment a datagram is handled -- which can be before the first quic_poll() of a
 * connection -- so it is read here, not remembered from the last poll. (The host test defines its own.) */
static inline u64 quic_now_packed(void) {
    rtc_time_t t; rtc_read(&t);
    return tls_pack_datetime((u32)t.year, (u32)t.month, (u32)t.day, (u32)t.hour, (u32)t.minute, (u32)t.second);
}
#define QUIC_NOW_PACKED() quic_now_packed()
#endif

/* ============================================================
 * quic.h -- a QUIC version 1 CLIENT (RFC 9000 transport, RFC 9001 TLS, RFC 9002 loss recovery, subset).
 *
 * QUIC is TLS 1.3 + a reliable, multiplexed, encrypted transport on top of UDP. The security comes from kernel/tls.h, run in
 * "QUIC mode": its handshake messages travel in CRYPTO frames instead of TLS records, its traffic secrets become the packet
 * protection keys, and the server's certificate chain, CertificateVerify and Finished are verified exactly as for HTTPS (the same
 * trusted-root store, the same hostname check). Nothing is sent in the clear except the Initial packets, which are protected with
 * keys anyone can derive (that is by design -- they only carry the ClientHello / ServerHello).
 *
 * What is here: Initial / Handshake / 1-RTT packet protection (AES-128-GCM, AES-128 header protection), packet number spaces,
 * ACK generation and processing, PTO retransmission, CRYPTO and STREAM reassembly, the transport-parameter checks of RFC 9000 7.3
 * (original_destination_connection_id, initial_source_connection_id), stream and connection flow-control limits, PATH_CHALLENGE
 * answers, peer-initiated key updates, CONNECTION_CLOSE in both directions.
 *
 * What is NOT here (a request/response client that opens ONE connection per page does not need it, and each is a clean failure,
 * never a silent misbehaviour): Retry and Version Negotiation (the attempt fails with QF_RETRY / QF_VERSION and https.h falls back
 * to TLS over TCP), 0-RTT and session resumption, connection migration, congestion control and pacing (the client sends a few
 * hundred bytes; the SERVER's sender is the one that needs a congestion controller), ECN, DPLPMTUD (datagrams are 1252 bytes at
 * most), more than one request stream. Only TLS_AES_128_GCM_SHA256 is offered (RFC 9001 requires it, every QUIC server has it).
 *
 * Time: the only clock this kernel has with seconds is the CMOS RTC, so timers here are coarse (1 s). The first PTO is 1 s and
 * doubles; with no authenticated packet from the server at all after QUIC_FIRST_RX_MS the attempt is abandoned so the browser can
 * fall back to TCP quickly on a network that blocks UDP.
 * ============================================================ */

#ifndef QUIC_RAW_MS
#define QUIC_RAW_MS()   (net_wall_seconds() * 1000u)
#define QUIC_RAW_WRAP   86400000u
#endif
#ifndef QUIC_RAW_WRAP
#define QUIC_RAW_WRAP   0xFFFFFFFFu
#endif
#define QUIC_FIRST_RX_MS     4000u     /* no packet from the server at all by then: UDP is probably blocked */
#define QUIC_HANDSHAKE_MS   15000u
#define QUIC_IDLE_MS        30000u
#define QUIC_PTO_MAX_COUNT  6

#define QUIC_VERSION_1      1u
#define QUIC_MAX_DGRAM      1252u
#define QUIC_MIN_INITIAL    1200u
#define QUIC_CID_LEN        8u
#define QUIC_NREC           24
#define QUIC_NACKR          8
#define QUIC_RASM_R         8
#define QUIC_UNI_SLOTS      8
#define QUIC_UNI_CAP        0x1000u     /* flow-control credit we give each server-initiated unidirectional stream */
#define QUIC_RX_MAX_DATA    0x400000u   /* connection-level credit we advertise (4 MB; we count frame bytes, so retransmits over-count) */
#define QUIC_S0_CAP         0x50000u    /* the request stream's receive buffer: 320 KB, also its advertised credit */
#define QUIC_LOCAL_PORT     41443

#define QUIC_LVL_INITIAL 0
#define QUIC_LVL_HS      1
#define QUIC_LVL_APP     2

/* transport error codes (RFC 9000 20.1) */
#define QERR_NO_ERROR            0x00u
#define QERR_INTERNAL            0x01u
#define QERR_FLOW_CONTROL        0x03u
#define QERR_STREAM_LIMIT        0x04u
#define QERR_STREAM_STATE        0x05u
#define QERR_FINAL_SIZE          0x06u
#define QERR_FRAME_ENCODING      0x07u
#define QERR_TRANSPORT_PARAMETER 0x08u
#define QERR_PROTOCOL_VIOLATION  0x0Au
#define QERR_CRYPTO_BASE         0x100u   /* + the TLS alert description */

/* The memory behind the big buffers (the net arena in the kernel, plain arrays in the host test). Offsets are laid end to end. */
#ifndef QUIC_ARENA
#define QUIC_ARENA ((u8 *)MW_QUIC_ADDR)
#endif
#define QUIC_OFF_S0      0x00000u   /* request stream receive buffer       QUIC_S0_CAP */
#define QUIC_OFF_CRX_I   0x50000u   /* CRYPTO receive, Initial             4 KB  */
#define QUIC_OFF_CRX_H   0x51000u   /* CRYPTO receive, Handshake           32 KB (a certificate chain) */
#define QUIC_OFF_CRX_A   0x59000u   /* CRYPTO receive, 1-RTT               8 KB (NewSessionTicket) */
#define QUIC_OFF_UNI     0x5B000u   /* 8 x QUIC_UNI_CAP: server unidirectional streams */
#define QUIC_OFF_CTX_I   0x63000u   /* CRYPTO send, Initial: ClientHello (twice after a HelloRetryRequest) 4 KB */
#define QUIC_OFF_CTX_H   0x64000u   /* CRYPTO send, Handshake: client Finished  4 KB */
#define QUIC_OFF_CTX_A   0x65000u   /* CRYPTO send, 1-RTT                  1 KB */
#define QUIC_OFF_TXS_C   0x65400u   /* control stream send buffer          1 KB */
#define QUIC_OFF_TXS_R   0x65800u   /* request stream send buffer          6 KB */
#define QUIC_ARENA_USED  0x67000u
#define QUIC_TXS_R_CAP   0x1800u

typedef enum { QUIC_IDLE = 0, QUIC_HANDSHAKE, QUIC_CONNECTED, QUIC_CLOSED, QUIC_FAILED } quic_state_t;
typedef enum {
    QF_NONE = 0, QF_NO_RESPONSE, QF_TIMEOUT, QF_TLS, QF_PEER_CLOSE, QF_PROTOCOL, QF_VERSION, QF_RETRY, QF_PARAMS,
    QF_INTERNAL, QF_STREAM_RESET, QF_OVERFLOW
} quic_fail_t;

/* ------------------------------------------------------------------ variable-length integers (RFC 9000 16) */
static inline u32 quic_vi_len(u64 v) { return v < 64 ? 1u : v < 16384 ? 2u : v < 0x40000000ull ? 4u : 8u; }
static inline u32 quic_put_vi(u8 *p, u64 v) {
    if (v < 64) { p[0] = (u8)v; return 1; }
    if (v < 16384) { p[0] = (u8)(0x40 | (v >> 8)); p[1] = (u8)v; return 2; }
    if (v < 0x40000000ull) { p[0] = (u8)(0x80 | (v >> 24)); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; return 4; }
    p[0] = (u8)(0xC0 | (v >> 56)); for (int i = 1; i < 8; i++) p[i] = (u8)(v >> (56 - 8 * i));
    return 8;
}
/* Reads one; returns the bytes used, or 0 if [p, end) is too short. */
static inline u32 quic_get_vi(const u8 *p, const u8 *end, u64 *v) {
    if (p >= end) return 0;
    u32 l = 1u << (p[0] >> 6);
    if ((u32)(end - p) < l) return 0;
    u64 x = p[0] & 0x3F;
    for (u32 i = 1; i < l; i++) x = (x << 8) | p[i];
    *v = x;
    return l;
}

/* ------------------------------------------------------------------ stream / CRYPTO reassembly */
typedef struct {
    u8 *buf; u32 cap;
    u32 contig;                      /* bytes 0..contig-1 have all arrived */
    u32 n; u32 rs[QUIC_RASM_R], re[QUIC_RASM_R];   /* received ranges beyond contig, [rs, re) */
    u8 fin; u32 fin_off;             /* the final size, once a FIN frame told us */
} quic_rasm_t;

static inline void quic_rasm_init(quic_rasm_t *r, u8 *buf, u32 cap) {
    r->buf = buf; r->cap = cap; r->contig = 0; r->n = 0; r->fin = 0; r->fin_off = 0;
}
/* 1 = taken (or a duplicate), 0 = beyond the buffer (a flow-control violation), -1 = too many holes: the caller must NOT acknowledge the
 * packet, so the peer sends the data again. */
static inline int quic_rasm_add(quic_rasm_t *r, u32 off, const u8 *d, u32 n) {
    if (off > r->cap || n > r->cap - off) return 0;
    if (n == 0) return 1;
    u32 end = off + n;
    if (end <= r->contig) return 1;
    u32 s = off < r->contig ? r->contig : off;
    /* a gap we cannot track: refuse BEFORE writing anything */
    u32 rs0 = s, re0 = end, keep = 0;
    for (u32 i = 0; i < r->n; i++) if (!(r->rs[i] <= re0 && r->re[i] >= rs0)) keep++;
    if (rs0 > r->contig && keep + 1 > QUIC_RASM_R) return -1;
    for (u32 i = 0; i < end - s; i++) r->buf[s + i] = d[(s - off) + i];
    for (u32 i = 0; i < r->n; ) {
        if (r->rs[i] <= re0 && r->re[i] >= rs0) {
            if (r->rs[i] < rs0) rs0 = r->rs[i];
            if (r->re[i] > re0) re0 = r->re[i];
            r->n--; r->rs[i] = r->rs[r->n]; r->re[i] = r->re[r->n];
            i = 0;
        } else i++;
    }
    if (rs0 <= r->contig) {
        if (re0 > r->contig) r->contig = re0;
        for (u32 i = 0; i < r->n; ) {
            if (r->rs[i] <= r->contig) {
                if (r->re[i] > r->contig) r->contig = r->re[i];
                r->n--; r->rs[i] = r->rs[r->n]; r->re[i] = r->re[r->n];
                i = 0;
            } else i++;
        }
    } else { r->rs[r->n] = rs0; r->re[r->n] = re0; r->n++; }
    return 1;
}

/* ------------------------------------------------------------------ packet protection keys */
typedef struct {
    aes_ctx_t aead, hp;
    u8 iv[12];
    u8 secret[32];
    u8 valid;
} quic_dir_t;

/* HKDF-Expand-Label (RFC 8446 7.1) with SHA-256 and an empty context, for outputs of at most one hash block. */
static inline void quic_expand_label(u8 *out, u32 olen, const u8 secret[32], const char *label) {
    u8 info[2 + 1 + 6 + 12 + 1 + 1];
    u32 p = 0, ll = 0;
    while (label[ll]) ll++;
    info[p++] = (u8)(olen >> 8); info[p++] = (u8)olen;
    info[p++] = (u8)(6 + ll);
    info[p++] = 't'; info[p++] = 'l'; info[p++] = 's'; info[p++] = '1'; info[p++] = '3'; info[p++] = ' ';
    for (u32 i = 0; i < ll; i++) info[p++] = (u8)label[i];
    info[p++] = 0;       /* empty context */
    info[p++] = 0x01;
    u8 full[32];
    hmac_sha256(secret, 32, info, p, full);
    for (u32 i = 0; i < olen; i++) out[i] = full[i];
}
/* secret -> AEAD key + IV + header-protection key (RFC 9001 5.1) */
static inline void quic_dir_set(quic_dir_t *d, const u8 secret[32]) {
    u8 key[16], hp[16];
    quic_expand_label(key, 16, secret, "quic key");
    quic_expand_label(d->iv, 12, secret, "quic iv");
    quic_expand_label(hp, 16, secret, "quic hp");
    aes_set_key(&d->aead, key, 16);
    aes_set_key(&d->hp, hp, 16);
    for (int i = 0; i < 32; i++) d->secret[i] = secret[i];
    d->valid = 1;
}
/* key update: new AEAD key and IV from the next secret, the header-protection key stays (RFC 9001 6.1) */
static inline void quic_dir_next(quic_dir_t *dst, const quic_dir_t *cur) {
    u8 ns[32], key[16];
    quic_expand_label(ns, 32, cur->secret, "quic ku");
    quic_expand_label(key, 16, ns, "quic key");
    quic_expand_label(dst->iv, 12, ns, "quic iv");
    aes_set_key(&dst->aead, key, 16);
    dst->hp = cur->hp;
    for (int i = 0; i < 32; i++) dst->secret[i] = ns[i];
    dst->valid = 1;
}
static const u8 quic_initial_salt_v1[20] = { 0x38,0x76,0x2c,0xf7,0xf5,0x59,0x34,0xb3,0x4d,0x17,0x9a,0xe6,0xa4,0xc8,0x0c,0xad,0xcc,0xbb,0x7f,0x0a };
/* Initial secrets come from the client's first destination connection ID (RFC 9001 5.2). tx = client, rx = server. */
static inline void quic_initial_keys(quic_dir_t *tx, quic_dir_t *rx, const u8 *dcid, u32 dcid_len) {
    u8 init[32], cs[32], ss[32];
    hmac_sha256(quic_initial_salt_v1, 20, dcid, dcid_len, init);
    quic_expand_label(cs, 32, init, "client in");
    quic_expand_label(ss, 32, init, "server in");
    quic_dir_set(tx, cs);
    quic_dir_set(rx, ss);
}

/* ------------------------------------------------------------------ connection state */
typedef struct { u8 kind; u8 fin; u8 sidx; u32 off; u32 len; } quic_fr_t;       /* kind 1 = CRYPTO, 2 = STREAM (sidx -> txs[]) */
typedef struct { u8 used, elicit, lvl, nfr; u32 t; u64 pn; quic_fr_t fr[3]; } quic_rec_t;

typedef struct {
    quic_dir_t tx, rx;
    u64 next_pn;
    u64 rx_largest; u8 rx_any;
    u64 alo[QUIC_NACKR], ahi[QUIC_NACKR]; u8 na;            /* received packet numbers, descending ranges */
    u8 ack_needed;
    u8 *ctx; u32 ctx_cap, ctx_len, ctx_sent;                /* CRYPTO bytes we send in this space */
    u8 nrtx; quic_fr_t rtx[6];                              /* frames to send again */
    u8 ping;
    quic_rasm_t crx; u32 crx_done;                          /* CRYPTO bytes we receive, and how many already went to TLS */
} quic_space_t;

typedef struct { u8 used; u32 id; u8 *buf; u32 cap, len, sent; u8 fin, fin_sent; } quic_txs_t;
typedef struct { u8 used; u32 id; quic_rasm_t r; } quic_uni_t;

typedef struct {
    quic_state_t state; quic_fail_t fail; u32 fail_code;
    u32 ip; u16 port;
    u8 scid[QUIC_CID_LEN], odcid[QUIC_CID_LEN];
    u8 dcid[20]; u8 dcid_len;                                /* the server's connection ID we address packets to */
    u8 got_srv_cid; u8 srv_scid[20]; u8 srv_scid_len;
    quic_space_t sp[3];
    quic_rec_t rec[QUIC_NREC];
    u32 t_start, t_last_rx; u8 got_rx;
    u32 pto_ms; u8 pto_count;
    u64 now_packed;
    u8 app_keys, init_dropped, hs_dropped, hs_confirmed;
    u8 kp_tx, kp_rx;                                         /* key phase bits */
    /* the peer's transport parameters */
    u32 peer_max_data, peer_sd_bidi_remote, peer_sd_uni, peer_max_streams_bidi, peer_max_streams_uni; u8 params_ok;
    u32 tx_data_total, rx_total;
    u8 pr_pending; u8 pr[8];
    u8 close_pending, close_app; u32 close_code;
    quic_rasm_t s0; u8 s0_reset;
    quic_uni_t uni[QUIC_UNI_SLOTS];
    quic_txs_t txs[2];                                       /* 0: our control stream (id 2), 1: the request stream (id 0) */
    u8 nack_pkt;                                             /* set while parsing a packet whose data we could not keep: do not acknowledge it */
    u8 dg_pad;                                               /* the datagram being built holds an ack-eliciting Initial: pad it to 1200 */
} quic_t;

static quic_t qc;
static u8 quic_rxbuf[1600];
static u8 quic_scratch[1400];
static u16 quic_listen_port;

static inline u32 quic_elapsed(u32 since) { u32 n = QUIC_RAW_MS(); return n >= since ? n - since : n + (QUIC_RAW_WRAP - since) + 1u; }

static inline void quic_fail(quic_fail_t why, u32 code) {
    if (qc.state == QUIC_FAILED || qc.state == QUIC_CLOSED) return;
    qc.fail = why; qc.fail_code = code; qc.state = QUIC_FAILED;
    qc.close_pending = 1; qc.close_app = 0; qc.close_code = code;       /* best effort: tell the peer, if any key can protect it */
    serial_puts("[QUIC] failed, reason="); serial_put_dec((u32)why); serial_puts(" code=0x"); serial_put_hex32(code); serial_putc('\n');
}

/* ------------------------------------------------------------------ the TLS side: hooks installed into tls.h */
static void quic_flush(void);
static inline u32 quic_tls_alert_for(tls_fail_reason_t r) {
    switch (r) {
        case TLS_FAIL_CERT_PARSE: case TLS_FAIL_CERT_CHAIN: case TLS_FAIL_CERT_EXTENSION: case TLS_FAIL_HOSTNAME_MISMATCH: return 42;   /* bad_certificate */
        case TLS_FAIL_CERT_UNTRUSTED: return 48;                                                                                        /* unknown_ca */
        case TLS_FAIL_SKE_SIGNATURE: case TLS_FAIL_DECRYPT: case TLS_FAIL_SERVER_FINISHED: return 51;                                  /* decrypt_error */
        case TLS_FAIL_UNEXPECTED_MESSAGE: return 10;
        case TLS_FAIL_PROTOCOL_VERSION: return 70;
        case TLS_FAIL_UNSUPPORTED_CIPHER_SUITE: case TLS_FAIL_UNSUPPORTED_CURVE: return 40;
        case TLS_FAIL_NO_ALPN: return 120;                                                                                              /* no_application_protocol */
        default: return 80;
    }
}
static void quic_hook_send(int hs_level, const u8 *msg, u32 len) {
    quic_space_t *s = &qc.sp[hs_level ? QUIC_LVL_HS : QUIC_LVL_INITIAL];
    if (s->ctx_len + len > s->ctx_cap) { quic_fail(QF_OVERFLOW, QERR_INTERNAL); return; }
    for (u32 i = 0; i < len; i++) s->ctx[s->ctx_len + i] = msg[i];
    s->ctx_len += len;
}
static void quic_hook_keys(int which) {
    if (which == 1) {                              /* ServerHello processed: handshake secrets are in tls_conn */
        quic_dir_set(&qc.sp[QUIC_LVL_HS].tx, tls_conn.c_secret);
        quic_dir_set(&qc.sp[QUIC_LVL_HS].rx, tls_conn.s_secret);
        serial_puts("[QUIC] handshake keys installed\n");
    } else {                                       /* server Finished verified, ours sent: 1-RTT secrets */
        quic_dir_set(&qc.sp[QUIC_LVL_APP].tx, tls_conn.c_secret);
        quic_dir_set(&qc.sp[QUIC_LVL_APP].rx, tls_conn.s_secret);
        qc.app_keys = 1;
        qc.kp_tx = 0; qc.kp_rx = 0;
        if (qc.state == QUIC_HANDSHAKE) qc.state = QUIC_CONNECTED;
        serial_puts("[QUIC] handshake complete, 1-RTT keys installed\n");
    }
}
/* The server's transport parameters (RFC 9000 18.2), checked the way 7.3 asks: the two connection ID parameters must match what we saw
 * on the wire, retry_source_connection_id must be absent (we never saw a Retry), and the stream limits must leave room for HTTP/3. */
static int quic_hook_params(const u8 *p, u32 n) {
    const u8 *end = p + n;
    int got_odcid = 0, got_iscid = 0;
    qc.peer_max_data = 0; qc.peer_sd_bidi_remote = 0; qc.peer_sd_uni = 0; qc.peer_max_streams_bidi = 0; qc.peer_max_streams_uni = 0;
    while (p < end) {
        u64 id, len; u32 a = quic_get_vi(p, end, &id); if (!a) return 0; p += a;
        a = quic_get_vi(p, end, &len); if (!a) return 0; p += a;
        if (len > (u64)(end - p)) return 0;
        const u8 *v = p; u32 vl = (u32)len; p += vl;
        u64 num = 0; int is_num = (vl > 0 && quic_get_vi(v, v + vl, &num) == vl);
        switch ((u32)id) {
            case 0x00: if (vl != QUIC_CID_LEN) return 0; for (u32 i = 0; i < vl; i++) if (v[i] != qc.odcid[i]) return 0; got_odcid = 1; break;
            case 0x0f: if (vl != qc.srv_scid_len) return 0; for (u32 i = 0; i < vl; i++) if (v[i] != qc.srv_scid[i]) return 0; got_iscid = 1; break;
            case 0x10: return 0;                                        /* retry_source_connection_id without a Retry */
            case 0x04: if (!is_num) return 0; qc.peer_max_data = num > 0x7fffffffull ? 0x7fffffffu : (u32)num; break;
            case 0x06: if (!is_num) return 0; qc.peer_sd_bidi_remote = num > 0x7fffffffull ? 0x7fffffffu : (u32)num; break;
            case 0x07: if (!is_num) return 0; qc.peer_sd_uni = num > 0x7fffffffull ? 0x7fffffffu : (u32)num; break;
            case 0x08: if (!is_num) return 0; qc.peer_max_streams_bidi = num > 0x7fffffffull ? 0x7fffffffu : (u32)num; break;
            case 0x09: if (!is_num) return 0; qc.peer_max_streams_uni = num > 0x7fffffffull ? 0x7fffffffu : (u32)num; break;
            case 0x03: if (!is_num || num < 1200) return 0; break;        /* max_udp_payload_size below 1200 is illegal */
            case 0x0a: if (!is_num || num > 20) return 0; break;          /* ack_delay_exponent */
            case 0x0b: if (!is_num || num >= 16384) return 0; break;      /* max_ack_delay */
            case 0x0e: if (!is_num || num < 2) return 0; break;           /* active_connection_id_limit */
            default: break;                                               /* idle timeout, reset token, preferred address, grease ...: not needed */
        }
    }
    if (!got_odcid || !got_iscid) return 0;
    if (qc.peer_max_streams_bidi < 1 || qc.peer_max_streams_uni < 1) return 0;     /* HTTP/3 needs a request stream and a control stream */
    qc.params_ok = 1;
    return 1;
}

/* ------------------------------------------------------------------ sent-packet records, ACK ranges */
static inline quic_rec_t *quic_rec_alloc(void) {
    for (int i = 0; i < QUIC_NREC; i++) if (!qc.rec[i].used) return &qc.rec[i];
    return 0;
}
static inline int quic_have_free_rec(void) { return quic_rec_alloc() != 0; }

static inline int quic_ack_seen(const quic_space_t *s, u64 pn) {
    for (u32 i = 0; i < s->na; i++) if (pn >= s->alo[i] && pn <= s->ahi[i]) return 1;
    return 0;
}
static inline void quic_ack_note(quic_space_t *s, u64 pn) {
    if (quic_ack_seen(s, pn)) return;
    u32 pos = s->na;
    for (u32 i = 0; i < s->na; i++) if (pn > s->ahi[i]) { pos = i; break; }       /* first range entirely below pn */
    int merged = 0;
    if (pos > 0 && pn + 1 == s->alo[pos - 1]) {                  /* extends the range above it downwards */
        s->alo[pos - 1] = pn; merged = 1;
        if (pos < s->na && s->ahi[pos] + 1 == pn) {              /* and now touches the range below: join them */
            s->alo[pos - 1] = s->alo[pos];
            for (u32 j = pos; j + 1 < s->na; j++) { s->alo[j] = s->alo[j + 1]; s->ahi[j] = s->ahi[j + 1]; }
            s->na--;
        }
    } else if (pos < s->na && s->ahi[pos] + 1 == pn) {          /* extends the range below it upwards */
        s->ahi[pos] = pn; merged = 1;
    }
    if (!merged) {
        if (s->na == QUIC_NACKR) { if (pos == s->na) return; s->na--; }      /* full: forget the oldest (lowest) range */
        for (u32 j = s->na; j > pos; j--) { s->alo[j] = s->alo[j - 1]; s->ahi[j] = s->ahi[j - 1]; }
        s->alo[pos] = pn; s->ahi[pos] = pn; s->na++;
    }
}

/* ------------------------------------------------------------------ building packets */
static inline int quic_level_keys(int l) { return qc.sp[l].tx.valid; }
static inline int quic_txs_unsent(const quic_txs_t *t) { return t->used && (t->sent < t->len || (t->fin && !t->fin_sent)); }

static inline int quic_level_has_work(int l) {
    quic_space_t *s = &qc.sp[l];
    if (!s->tx.valid) return 0;
    if (qc.close_pending) return l == (qc.app_keys ? QUIC_LVL_APP : (qc.sp[QUIC_LVL_HS].tx.valid ? QUIC_LVL_HS : QUIC_LVL_INITIAL));
    if (s->ack_needed) return 1;
    if (!quic_have_free_rec()) return 0;
    if (s->ctx_sent < s->ctx_len || s->nrtx || s->ping) return 1;
    if (l == QUIC_LVL_APP && (qc.pr_pending || quic_txs_unsent(&qc.txs[0]) || quic_txs_unsent(&qc.txs[1]))) return 1;
    return 0;
}

/* Appends an ACK frame for space s. */
static inline u32 quic_put_ack(u8 *o, const quic_space_t *s) {
    u32 p = 0;
    o[p++] = 0x02;
    p += quic_put_vi(o + p, s->ahi[0]);
    p += quic_put_vi(o + p, 0);                                  /* ack delay: we answer at once */
    p += quic_put_vi(o + p, (u64)(s->na - 1));
    p += quic_put_vi(o + p, s->ahi[0] - s->alo[0]);
    for (u32 i = 1; i < s->na; i++) {
        p += quic_put_vi(o + p, s->alo[i - 1] - s->ahi[i] - 2);  /* gap */
        p += quic_put_vi(o + p, s->ahi[i] - s->alo[i]);
    }
    return p;
}
/* One CRYPTO or STREAM frame for `fr` into o (capacity room); returns bytes written and may shorten fr->len to fit; 0 = does not fit. */
static inline u32 quic_put_data_frame(u8 *o, u32 room, quic_space_t *s, quic_fr_t *fr) {
    u32 p = 0;
    if (fr->kind == 1) {
        u32 hdr = 1 + quic_vi_len(fr->off) + 4;                  /* length field is written as a 4-byte varint below, so its size is fixed */
        if (room <= hdr) return 0;
        u32 n = fr->len; if (n > room - hdr) n = room - hdr;
        fr->len = n;
        o[p++] = 0x06;
        p += quic_put_vi(o + p, fr->off);
        { o[p++] = (u8)(0x80 | (n >> 24)); o[p++] = (u8)(n >> 16); o[p++] = (u8)(n >> 8); o[p++] = (u8)n; }
        for (u32 i = 0; i < n; i++) o[p + i] = s->ctx[fr->off + i];
        return p + n;
    } else {
        quic_txs_t *t = &qc.txs[fr->sidx];
        u32 hdr = 1 + quic_vi_len(t->id) + (fr->off ? quic_vi_len(fr->off) : 0) + 4;
        if (room < hdr) return 0;
        u32 n = fr->len; if (n > room - hdr) n = room - hdr;
        int fin = fr->fin && n == fr->len;
        if (n == 0 && !fin) return 0;
        fr->len = n; fr->fin = (u8)fin;
        o[p++] = (u8)(0x08 | (fr->off ? 0x04 : 0) | 0x02 | (fin ? 0x01 : 0));
        p += quic_put_vi(o + p, t->id);
        if (fr->off) p += quic_put_vi(o + p, fr->off);
        o[p++] = (u8)(0x80 | (n >> 24)); o[p++] = (u8)(n >> 16); o[p++] = (u8)(n >> 8); o[p++] = (u8)n;
        for (u32 i = 0; i < n; i++) o[p + i] = t->buf[fr->off + i];
        return p + n;
    }
}

/* Builds, protects and appends ONE packet of level l at out[0..]; returns its length (0 = nothing written). dg_off is how many bytes of the
 * datagram precede it, is_last says no other packet follows it (padding goes into the last packet). */
static u32 quic_build_packet(int l, u8 *out, u32 cap, u32 dg_off, int is_last) {
    quic_space_t *s = &qc.sp[l];
    u8 *pt = quic_scratch;
    u32 pp = 0;
    int elicit = 0;
    u32 hdr_len = (l == QUIC_LVL_APP) ? 1u + qc.dcid_len + 4u
                                      : 1u + 4u + 1u + qc.dcid_len + 1u + QUIC_CID_LEN + (l == QUIC_LVL_INITIAL ? 1u : 0u) + 2u + 4u;
    if (cap < hdr_len + 16 + 1) return 0;
    u32 room = cap - hdr_len - 16;
    if (room > sizeof(quic_scratch)) room = sizeof(quic_scratch);
    quic_rec_t *rec = 0;
    quic_rec_t tmp; tmp.nfr = 0;

    if (qc.close_pending) {
        pt[pp++] = (u8)(qc.close_app ? 0x1d : 0x1c);
        pp += quic_put_vi(pt + pp, qc.close_code);
        if (!qc.close_app) pp += quic_put_vi(pt + pp, 0);        /* frame type that caused it: unknown */
        pp += quic_put_vi(pt + pp, 0);                           /* no reason phrase */
        qc.close_pending = 0;
    } else {
        if (s->ack_needed && s->na) {
            if (room >= 40) { pp += quic_put_ack(pt + pp, s); s->ack_needed = 0; }
        }
        if (quic_have_free_rec()) {
            if (l == QUIC_LVL_APP && qc.pr_pending && room - pp >= 9) {
                pt[pp++] = 0x1b; for (int i = 0; i < 8; i++) pt[pp++] = qc.pr[i];
                qc.pr_pending = 0; elicit = 1;
            }
            /* frames to send again first */
            while (s->nrtx && tmp.nfr < 3) {
                quic_fr_t fr = s->rtx[0];
                u32 n = quic_put_data_frame(pt + pp, room - pp, s, &fr);
                if (!n) break;
                pp += n; elicit = 1; tmp.fr[tmp.nfr++] = fr;
                if (fr.len < s->rtx[0].len) {                    /* only part fit: keep the rest queued */
                    s->rtx[0].off += fr.len; s->rtx[0].len -= fr.len;
                    break;
                }
                for (u32 j = 0; j + 1 < s->nrtx; j++) s->rtx[j] = s->rtx[j + 1];
                s->nrtx--;
            }
            if (s->ctx_sent < s->ctx_len && tmp.nfr < 3) {
                quic_fr_t fr; fr.kind = 1; fr.fin = 0; fr.sidx = 0; fr.off = s->ctx_sent; fr.len = s->ctx_len - s->ctx_sent;
                u32 n = quic_put_data_frame(pt + pp, room - pp, s, &fr);
                if (n) { pp += n; elicit = 1; s->ctx_sent += fr.len; tmp.fr[tmp.nfr++] = fr; }
            }
            if (l == QUIC_LVL_APP) {
                for (int k = 0; k < 2 && tmp.nfr < 3; k++) {
                    quic_txs_t *t = &qc.txs[k];
                    if (!quic_txs_unsent(t)) continue;
                    quic_fr_t fr; fr.kind = 2; fr.sidx = (u8)k; fr.off = t->sent; fr.len = t->len - t->sent; fr.fin = (u8)(t->fin && !t->fin_sent);
                    u32 n = quic_put_data_frame(pt + pp, room - pp, s, &fr);
                    if (n) { pp += n; elicit = 1; t->sent += fr.len; if (fr.fin) t->fin_sent = 1; tmp.fr[tmp.nfr++] = fr; }
                }
            }
            if (s->ping && !elicit && room - pp >= 1) { pt[pp++] = 0x01; elicit = 1; }
            s->ping = 0;
        }
    }
    if (pp == 0) return 0;
    if (l == QUIC_LVL_INITIAL && elicit) qc.dg_pad = 1;
    /* The client's datagram that holds an ack-eliciting Initial must be at least 1200 bytes (RFC 9000 14.1): pad the last packet. */
    if (is_last && qc.dg_pad) {
        u32 total = dg_off + hdr_len + pp + 16;
        if (total < QUIC_MIN_INITIAL) { u32 pad = QUIC_MIN_INITIAL - total; if (pp + pad > room) pad = room - pp; for (u32 i = 0; i < pad; i++) pt[pp + i] = 0; pp += pad; }
    }
    /* a packet needs at least 4 bytes of (payload + tag) after the packet number for the header-protection sample: the 16-byte tag covers it */

    u64 pn = s->next_pn++;
    if (elicit) {
        rec = quic_rec_alloc();
        if (rec) {
            rec->used = 1; rec->elicit = 1; rec->lvl = (u8)l; rec->pn = pn; rec->t = QUIC_RAW_MS(); rec->nfr = tmp.nfr;
            for (u32 i = 0; i < tmp.nfr; i++) rec->fr[i] = tmp.fr[i];
        }
    }
    /* header */
    u32 h = 0, pn_off;
    u8 *o = out;
    if (l == QUIC_LVL_APP) {
        o[h++] = (u8)(0x40 | (qc.kp_tx << 2) | 0x03);
        for (u32 i = 0; i < qc.dcid_len; i++) o[h++] = qc.dcid[i];
    } else {
        o[h++] = (u8)(0xC0 | ((l == QUIC_LVL_HS ? 2 : 0) << 4) | 0x03);
        o[h++] = 0; o[h++] = 0; o[h++] = 0; o[h++] = 1;
        o[h++] = qc.dcid_len; for (u32 i = 0; i < qc.dcid_len; i++) o[h++] = qc.dcid[i];
        o[h++] = QUIC_CID_LEN; for (u32 i = 0; i < QUIC_CID_LEN; i++) o[h++] = qc.scid[i];
        if (l == QUIC_LVL_INITIAL) o[h++] = 0;                    /* token length */
        u32 len = 4 + pp + 16;
        o[h++] = (u8)(0x40 | (len >> 8)); o[h++] = (u8)len;
    }
    pn_off = h;
    o[h++] = (u8)(pn >> 24); o[h++] = (u8)(pn >> 16); o[h++] = (u8)(pn >> 8); o[h++] = (u8)pn;
    for (u32 i = 0; i < pp; i++) o[h + i] = pt[i];
    /* AEAD: nonce = IV xor packet number (right aligned), AAD = the header with the unprotected packet number */
    u8 nonce[12]; for (int i = 0; i < 12; i++) nonce[i] = s->tx.iv[i];
    for (int i = 0; i < 8; i++) nonce[4 + i] ^= (u8)(pn >> (56 - 8 * i));
    u8 tag[16];
    gcm_encrypt(&s->tx.aead, nonce, o, h, o + h, pp, tag);
    for (int i = 0; i < 16; i++) o[h + pp + i] = tag[i];
    /* header protection */
    u8 mask[16]; for (int i = 0; i < 16; i++) mask[i] = o[pn_off + 4 + i];
    aes128_encrypt_block(&s->tx.hp, mask);
    o[0] ^= (u8)(mask[0] & (l == QUIC_LVL_APP ? 0x1F : 0x0F));
    for (int i = 0; i < 4; i++) o[pn_off + i] ^= mask[1 + i];
    return h + pp + 16;
}

static inline void quic_send_dgram(const u8 *d, u32 n) {
    udp_send(net_cfg.my_ip, quic_listen_port, qc.ip, qc.port, d, (u16)n);
}

static void quic_drop_level(int l) {
    quic_space_t *s = &qc.sp[l];
    s->tx.valid = 0; s->rx.valid = 0; s->ack_needed = 0; s->nrtx = 0; s->ping = 0; s->ctx_sent = s->ctx_len;
    for (int i = 0; i < QUIC_NREC; i++) if (qc.rec[i].used && qc.rec[i].lvl == l) qc.rec[i].used = 0;
}

static void quic_flush(void) {
    if ((qc.state == QUIC_FAILED || qc.state == QUIC_CLOSED) && !qc.close_pending) return;
    if (qc.state == QUIC_IDLE) return;
    for (int round = 0; round < 12; round++) {
        int has[3], last = -1, any = 0;
        for (int l = 0; l < 3; l++) { has[l] = quic_level_has_work(l); if (has[l]) { last = l; any = 1; } }
        if (!any) break;
        u8 dg[QUIC_MAX_DGRAM];
        u32 off = 0;
        qc.dg_pad = 0;
        for (int l = 0; l < 3; l++) {
            if (!has[l]) continue;
            u32 n = quic_build_packet(l, dg + off, QUIC_MAX_DGRAM - off, off, l == last);
            off += n;
            if (l == QUIC_LVL_HS && n && !qc.init_dropped && qc.sp[QUIC_LVL_INITIAL].tx.valid) {
                /* RFC 9001 4.9.1: a client discards its Initial keys when it first sends a Handshake packet. The Initial packet already in this
                 * datagram was built above, so nothing is lost. */
                qc.init_dropped = 1;
            }
        }
        if (!off) break;
        quic_send_dgram(dg, off);
        if (qc.init_dropped && qc.sp[QUIC_LVL_INITIAL].tx.valid) quic_drop_level(QUIC_LVL_INITIAL);
    }
}

/* ------------------------------------------------------------------ receiving */
/* RFC 9000 A.3 */
static inline u64 quic_decode_pn(u64 largest, int any, u64 trunc, u32 pnlen) {
    u64 expected = any ? largest + 1 : 0;
    u64 win = 1ull << (8 * pnlen), half = win >> 1, mask = win - 1;
    u64 cand = (expected & ~mask) | trunc;
    if (expected > half && cand <= expected - half && cand < (1ull << 62) - win) return cand + win;
    if (cand > expected + half && cand >= win) return cand - win;
    return cand;
}

static inline void quic_deliver_crypto(int l) {
    quic_space_t *s = &qc.sp[l];
    if (s->crx.contig <= s->crx_done) return;
    u32 n = s->crx.contig - s->crx_done;
    if (tls_conn.hs_buf_len + n > TLS_HS_BUF_SIZE) { quic_fail(QF_OVERFLOW, QERR_CRYPTO_BASE + 80); return; }
    for (u32 i = 0; i < n; i++) TLS_HS_BUF[tls_conn.hs_buf_len + i] = s->crx.buf[s->crx_done + i];
    tls_conn.hs_buf_len += n;
    s->crx_done = s->crx.contig;
    tls_process_handshake_buffer(QUIC_NOW_PACKED());
    if (tls_conn.state == TLS_FAILED) {
        quic_fail(QF_TLS, QERR_CRYPTO_BASE + quic_tls_alert_for(tls_conn.fail_reason));
    }
}

static inline int quic_stream_ok_to_receive(u64 id) {
    u32 t = (u32)(id & 3);
    if (t == 0) return id == 0;                       /* client-initiated bidirectional: only our request stream */
    if (t == 3) return 1;                             /* server-initiated unidirectional */
    return 0;                                         /* server-initiated bidirectional (we allow none), client-initiated unidirectional (ours) */
}
static inline quic_uni_t *quic_uni_slot(u32 id) {
    for (int i = 0; i < QUIC_UNI_SLOTS; i++) if (qc.uni[i].used && qc.uni[i].id == id) return &qc.uni[i];
    for (int i = 0; i < QUIC_UNI_SLOTS; i++) if (!qc.uni[i].used) {
        qc.uni[i].used = 1; qc.uni[i].id = id;
        quic_rasm_init(&qc.uni[i].r, QUIC_ARENA + QUIC_OFF_UNI + (u32)i * QUIC_UNI_CAP, QUIC_UNI_CAP);
        return &qc.uni[i];
    }
    return 0;
}

static void quic_ack_frame(int l, const u8 *p, const u8 *end, int ecn, const u8 **next, int *ok) {
    u64 largest, delay, count, first;
    u32 a;
    *ok = 0;
    if (!(a = quic_get_vi(p, end, &largest))) { return; } p += a;
    if (!(a = quic_get_vi(p, end, &delay))) { return; } p += a;
    if (!(a = quic_get_vi(p, end, &count))) { return; } p += a;
    if (!(a = quic_get_vi(p, end, &first))) { return; } p += a;
    quic_space_t *s = &qc.sp[l];
    if (largest >= s->next_pn || first > largest || count > 255) { quic_fail(QF_PROTOCOL, QERR_PROTOCOL_VIOLATION); return; }
    u64 hi = largest, lo = largest - first;
    int progress = 0;
    for (u64 k = 0;; k++) {
        for (int i = 0; i < QUIC_NREC; i++) {
            quic_rec_t *r = &qc.rec[i];
            if (r->used && r->lvl == l && r->pn >= lo && r->pn <= hi) { r->used = 0; progress = 1; }
        }
        if (k == count) break;
        u64 gap, len;
        if (!(a = quic_get_vi(p, end, &gap))) { return; } p += a;
        if (!(a = quic_get_vi(p, end, &len))) { return; } p += a;
        if (gap + 2 > lo) { quic_fail(QF_PROTOCOL, QERR_FRAME_ENCODING); return; }
        hi = lo - gap - 2;
        if (len > hi) { quic_fail(QF_PROTOCOL, QERR_FRAME_ENCODING); return; }
        lo = hi - len;
    }
    if (ecn) { for (int i = 0; i < 3; i++) { u64 x; if (!(a = quic_get_vi(p, end, &x))) { return; } p += a; } }
    if (progress) { qc.pto_count = 0; qc.pto_ms = 1000; }
    *next = p; *ok = 1;
}

/* Parses the frames of one decrypted packet of level l. Returns 1 if the packet is ack-eliciting, -1 on a protocol violation. */
static int quic_parse_frames(int l, const u8 *p, u32 len) {
    const u8 *end = p + len;
    int elicit = 0;
    while (p < end) {
        u64 ft; u32 a = quic_get_vi(p, end, &ft);
        if (!a) { quic_fail(QF_PROTOCOL, QERR_FRAME_ENCODING); return -1; }
        p += a;
        if (ft == 0x00) continue;                                   /* PADDING */
        if (l != QUIC_LVL_APP && !(ft == 0x01 || ft == 0x02 || ft == 0x03 || ft == 0x06 || ft == 0x1c)) {
            quic_fail(QF_PROTOCOL, QERR_PROTOCOL_VIOLATION); return -1;     /* RFC 9000 12.4: nothing else in Initial / Handshake packets */
        }
        if (ft != 0x02 && ft != 0x03 && ft != 0x1c && ft != 0x1d) elicit = 1;
        if (ft == 0x01) continue;                                   /* PING */
        if (ft == 0x02 || ft == 0x03) {
            const u8 *nx; int ok;
            quic_ack_frame(l, p, end, ft == 0x03, &nx, &ok);
            if (!ok) { if (qc.state != QUIC_FAILED) quic_fail(QF_PROTOCOL, QERR_FRAME_ENCODING); return -1; }
            p = nx; continue;
        }
        if (ft == 0x06) {                                           /* CRYPTO */
            u64 off, n;
            if (!(a = quic_get_vi(p, end, &off))) { goto bad; } p += a;
            if (!(a = quic_get_vi(p, end, &n))) { goto bad; } p += a;
            if (n > (u64)(end - p) || off > 0x7fffffffull) goto bad;
            int r = quic_rasm_add(&qc.sp[l].crx, (u32)off, p, (u32)n);
            if (r == 0) { quic_fail(QF_OVERFLOW, QERR_CRYPTO_BASE + 80); return -1; }
            if (r < 0) qc.nack_pkt = 1;
            p += n;
            if (r > 0) { quic_deliver_crypto(l); if (qc.state == QUIC_FAILED) return -1; }
            continue;
        }
        if (ft >= 0x08 && ft <= 0x0f) {                             /* STREAM */
            u64 id, off = 0, n;
            if (!(a = quic_get_vi(p, end, &id))) { goto bad; } p += a;
            if (ft & 0x04) { if (!(a = quic_get_vi(p, end, &off))) { goto bad; } p += a; }
            if (ft & 0x02) { if (!(a = quic_get_vi(p, end, &n))) { goto bad; } p += a; } else n = (u64)(end - p);
            if (n > (u64)(end - p)) goto bad;
            if (!quic_stream_ok_to_receive(id)) { quic_fail(QF_PROTOCOL, QERR_STREAM_STATE); return -1; }
            if (off > 0x7fffffffull || n > 0x7fffffffull) { quic_fail(QF_PROTOCOL, QERR_FLOW_CONTROL); return -1; }
            qc.rx_total += (u32)n;
            if (qc.rx_total > QUIC_RX_MAX_DATA) { quic_fail(QF_PROTOCOL, QERR_FLOW_CONTROL); return -1; }
            quic_rasm_t *r = 0;
            if (id == 0) r = &qc.s0;
            else { quic_uni_t *u = quic_uni_slot((u32)id); if (!u) { quic_fail(QF_PROTOCOL, QERR_STREAM_LIMIT); return -1; } r = &u->r; }
            if (r->fin && off + n > r->fin_off) { quic_fail(QF_PROTOCOL, QERR_FINAL_SIZE); return -1; }
            int res = quic_rasm_add(r, (u32)off, p, (u32)n);
            if (res == 0) { quic_fail(QF_PROTOCOL, QERR_FLOW_CONTROL); return -1; }
            if (res < 0) qc.nack_pkt = 1;
            if (res > 0 && (ft & 0x01)) {
                u32 fo = (u32)(off + n);
                if (r->fin && r->fin_off != fo) { quic_fail(QF_PROTOCOL, QERR_FINAL_SIZE); return -1; }
                r->fin = 1; r->fin_off = fo;
            }
            p += n;
            continue;
        }
        switch ((u32)ft) {
            case 0x04: { u64 id, e, fs; if (!(a = quic_get_vi(p, end, &id))) { goto bad; } p += a; if (!(a = quic_get_vi(p, end, &e))) { goto bad; } p += a;
                         if (!(a = quic_get_vi(p, end, &fs))) { goto bad; } p += a;
                         if (id == 0) { qc.s0_reset = 1; } break; }                           /* RESET_STREAM of the request stream */
            case 0x05: { u64 id, e; if (!(a = quic_get_vi(p, end, &id))) { goto bad; } p += a; if (!(a = quic_get_vi(p, end, &e))) { goto bad; } p += a; break; }   /* STOP_SENDING */
            case 0x07: { u64 n; if (!(a = quic_get_vi(p, end, &n))) { goto bad; } p += a; if (n > (u64)(end - p)) goto bad; p += n; break; }    /* NEW_TOKEN */
            case 0x10: { u64 v; if (!(a = quic_get_vi(p, end, &v))) { goto bad; } p += a; if (v > qc.peer_max_data && v < 0x7fffffffull) qc.peer_max_data = (u32)v; break; }
            case 0x11: { u64 id, v; if (!(a = quic_get_vi(p, end, &id))) { goto bad; } p += a; if (!(a = quic_get_vi(p, end, &v))) { goto bad; } p += a;
                         if (id == 0 && v > qc.peer_sd_bidi_remote && v < 0x7fffffffull) { qc.peer_sd_bidi_remote = (u32)v; }
                         if (id == 2 && v > qc.peer_sd_uni && v < 0x7fffffffull) { qc.peer_sd_uni = (u32)v; }
                         break; }
            case 0x12: case 0x13: { u64 v; if (!(a = quic_get_vi(p, end, &v))) { goto bad; } p += a; break; }                                   /* MAX_STREAMS */
            case 0x14: case 0x16: case 0x17: { u64 v; if (!(a = quic_get_vi(p, end, &v))) { goto bad; } p += a; break; }                         /* *_BLOCKED */
            case 0x15: { u64 id, v; if (!(a = quic_get_vi(p, end, &id))) { goto bad; } p += a; if (!(a = quic_get_vi(p, end, &v))) { goto bad; } p += a; break; }
            case 0x18: {                                                                                                                    /* NEW_CONNECTION_ID */
                u64 seq, ret; if (!(a = quic_get_vi(p, end, &seq))) { goto bad; } p += a; if (!(a = quic_get_vi(p, end, &ret))) { goto bad; } p += a;
                if (p >= end) { goto bad; }
                u32 cl = *p++;
                if (cl < 1 || cl > 20 || (u32)(end - p) < cl + 16 || ret > seq) { goto bad; }
                p += cl + 16; break; }
            case 0x19: { u64 v; if (!(a = quic_get_vi(p, end, &v))) { goto bad; } p += a; break; }                                              /* RETIRE_CONNECTION_ID */
            case 0x1a: { if ((u32)(end - p) < 8) goto bad; for (int i = 0; i < 8; i++) qc.pr[i] = p[i]; p += 8; qc.pr_pending = 1; break; }   /* PATH_CHALLENGE */
            case 0x1b: { if ((u32)(end - p) < 8) goto bad; p += 8; break; }                                                                  /* PATH_RESPONSE */
            case 0x1c: case 0x1d: {                                                                                                          /* CONNECTION_CLOSE */
                u64 code, fty, rl; if (!(a = quic_get_vi(p, end, &code))) { goto bad; } p += a;
                if (ft == 0x1c) { if (!(a = quic_get_vi(p, end, &fty))) { goto bad; } p += a; }
                if (!(a = quic_get_vi(p, end, &rl))) { goto bad; } p += a; if (rl > (u64)(end - p)) goto bad; p += rl;
                if (qc.state != QUIC_CLOSED && qc.state != QUIC_FAILED) {
                    qc.fail = QF_PEER_CLOSE; qc.fail_code = (u32)code; qc.state = QUIC_FAILED;
                    serial_puts("[QUIC] peer closed the connection, code=0x"); serial_put_hex32((u32)code); serial_putc('\n');
                }
                qc.close_pending = 0; return elicit; }
            case 0x1e:                                                                                                                       /* HANDSHAKE_DONE */
                if (!qc.hs_confirmed) { qc.hs_confirmed = 1; if (qc.sp[QUIC_LVL_HS].tx.valid) quic_drop_level(QUIC_LVL_HS); } break;
            default: quic_fail(QF_PROTOCOL, QERR_FRAME_ENCODING); return -1;
        }
        if (qc.state == QUIC_FAILED) return -1;
    }
    return elicit;
bad:
    quic_fail(QF_PROTOCOL, QERR_FRAME_ENCODING);
    return -1;
}

/* One packet inside the (mutable) datagram copy d[0..n). Returns the number of bytes it occupied, 0 to stop. */
static u32 quic_rx_packet(u8 *d, u32 n) {
    if (n < 1) return 0;
    u8 b0 = d[0];
    int lvl;
    u32 pn_off, pkt_len;
    if (b0 & 0x80) {                                                            /* long header */
        if (n < 7) return 0;
        u32 ver = ((u32)d[1] << 24) | ((u32)d[2] << 16) | ((u32)d[3] << 8) | d[4];
        u32 dl = d[5]; u32 p = 6;
        if (dl > 20 || p + dl + 1 > n) return 0;
        const u8 *dc = d + p; p += dl;
        u32 sl = d[p++];
        if (sl > 20 || p + sl > n) return 0;
        const u8 *sc = d + p; p += sl;
        if (ver == 0) { if (!qc.got_rx) quic_fail(QF_VERSION, QERR_PROTOCOL_VIOLATION); return 0; }          /* Version Negotiation: not supported */
        if (ver != QUIC_VERSION_1) return n;
        if (dl != QUIC_CID_LEN) return n;
        for (u32 i = 0; i < dl; i++) if (dc[i] != qc.scid[i]) return n;      /* not for this connection */
        u32 type = (b0 >> 4) & 3;
        if (type == 3) { if (!qc.got_rx) quic_fail(QF_RETRY, QERR_PROTOCOL_VIOLATION); return 0; }            /* Retry: not supported */
        if (type == 1) return n;                                              /* 0-RTT: never sent to a client */
        lvl = type == 0 ? QUIC_LVL_INITIAL : QUIC_LVL_HS;
        if (qc.got_srv_cid) { if (sl != qc.srv_scid_len) return n; for (u32 i = 0; i < sl; i++) if (sc[i] != qc.srv_scid[i]) return n; }
        if (lvl == QUIC_LVL_INITIAL) {
            u64 tl; u32 a = quic_get_vi(d + p, d + n, &tl); if (!a || tl != 0) return n; p += a;     /* a server sends no token */
        }
        u64 plen; u32 a = quic_get_vi(d + p, d + n, &plen); if (!a) return 0; p += a;
        if (plen > n - p || plen < 20) return n;
        pn_off = p; pkt_len = p + (u32)plen;
        if (!qc.sp[lvl].rx.valid) return pkt_len;                             /* keys not available (yet, or already dropped) */
        /* remember the server's connection ID from its first packet, before it is authenticated -- but only USE it once a packet decrypted */
    } else {                                                                    /* short header */
        if (n < 1 + QUIC_CID_LEN + 4 + 16) return 0;
        for (u32 i = 0; i < QUIC_CID_LEN; i++) if (d[1 + i] != qc.scid[i]) return 0;
        lvl = QUIC_LVL_APP; pn_off = 1 + QUIC_CID_LEN; pkt_len = n;
        if (!qc.sp[lvl].rx.valid) return 0;
    }
    quic_space_t *s = &qc.sp[lvl];
    if (pn_off + 4 + 16 > pkt_len) return pkt_len;
    /* header protection */
    u8 mask[16]; for (int i = 0; i < 16; i++) mask[i] = d[pn_off + 4 + i];
    aes128_encrypt_block(&s->rx.hp, mask);
    d[0] ^= (u8)(mask[0] & (lvl == QUIC_LVL_APP ? 0x1F : 0x0F));
    u32 pnlen = (u32)(d[0] & 3) + 1;
    u64 trunc = 0;
    for (u32 i = 0; i < pnlen; i++) { d[pn_off + i] ^= mask[1 + i]; trunc = (trunc << 8) | d[pn_off + i]; }
    if (lvl != QUIC_LVL_APP ? (d[0] & 0x0C) : (d[0] & 0x18)) return pkt_len;            /* reserved bits must be zero (checked after decryption in the spec; a failure here only drops it) */
    u64 pn = quic_decode_pn(s->rx_largest, s->rx_any, trunc, pnlen);
    u32 hdr = pn_off + pnlen;
    u32 ct_len = pkt_len - hdr - 16;
    if (pkt_len < hdr + 16) return pkt_len;
    u8 nonce[12];
    quic_dir_t *key = &s->rx;
    quic_dir_t nxt;
    int phase_flip = 0;
    if (lvl == QUIC_LVL_APP && (u8)((d[0] >> 2) & 1) != qc.kp_rx) {
        quic_dir_next(&nxt, &s->rx); key = &nxt; phase_flip = 1;                        /* the peer updated its keys */
    }
    for (int i = 0; i < 12; i++) nonce[i] = key->iv[i];
    for (int i = 0; i < 8; i++) nonce[4 + i] ^= (u8)(pn >> (56 - 8 * i));
    if (!gcm_decrypt(&key->aead, nonce, d, hdr, d + hdr, ct_len, d + hdr + ct_len)) return pkt_len;   /* forged, corrupted or old-key: dropped */
    /* authenticated from here on */
    if (phase_flip) {
        s->rx = nxt; qc.kp_rx ^= 1;
        quic_dir_t ntx; quic_dir_next(&ntx, &s->tx); s->tx = ntx; qc.kp_tx ^= 1;       /* answer with updated keys too */
    }
    if (lvl != QUIC_LVL_APP && !qc.got_srv_cid) {
        qc.got_srv_cid = 1; qc.srv_scid_len = d[6 + d[5]];
        const u8 *sc = d + 7 + d[5];
        for (u32 i = 0; i < qc.srv_scid_len; i++) qc.srv_scid[i] = sc[i];
        for (u32 i = 0; i < qc.srv_scid_len; i++) qc.dcid[i] = sc[i];                    /* RFC 9000 7.2: from now on address the server's own ID */
        qc.dcid_len = qc.srv_scid_len;
    }
    qc.got_rx = 1; qc.t_last_rx = QUIC_RAW_MS();
    if (s->rx_any && quic_ack_seen(s, pn)) { s->ack_needed = 1; return pkt_len; }       /* a duplicate: acknowledge again, process nothing */
    qc.nack_pkt = 0;
    int el = quic_parse_frames(lvl, d + hdr, ct_len);
    if (el < 0) return pkt_len;
    if (qc.state == QUIC_FAILED || qc.state == QUIC_CLOSED) return pkt_len;
    if (!qc.nack_pkt) {
        quic_ack_note(s, pn);
        if (!s->rx_any || pn > s->rx_largest) { s->rx_largest = pn; s->rx_any = 1; }
        if (el) s->ack_needed = 1;
    }
    return pkt_len;
}

static void quic_rx_datagram(const u8 *data, u32 n) {
    if (n < 21 || n > sizeof(quic_rxbuf)) return;
    for (u32 i = 0; i < n; i++) quic_rxbuf[i] = data[i];
    u32 off = 0;
    while (off < n) {
        u32 used = quic_rx_packet(quic_rxbuf + off, n - off);
        if (!used) break;
        off += used;
        if (qc.state == QUIC_FAILED || qc.state == QUIC_CLOSED) break;
    }
}

static void quic_udp_rx(u32 src_ip, u16 src_port, const u8 *data, u16 len) {
    if (qc.state == QUIC_IDLE || qc.state == QUIC_CLOSED) return;
    if (src_ip != qc.ip || src_port != qc.port) return;
    quic_rx_datagram(data, len);
    quic_flush();
}

/* ------------------------------------------------------------------ the client API */
static inline u32 quic_build_tp(u8 *o) {
    u32 p = 0;
#define QUIC_TP(id, val) do { p += quic_put_vi(o + p, (id)); p += quic_put_vi(o + p, quic_vi_len(val)); p += quic_put_vi(o + p, (val)); } while (0)
    QUIC_TP(0x01, 30000);                   /* max_idle_timeout (ms) */
    QUIC_TP(0x03, 1472);                    /* max_udp_payload_size */
    QUIC_TP(0x04, QUIC_RX_MAX_DATA);        /* initial_max_data */
    QUIC_TP(0x05, QUIC_S0_CAP);             /* initial_max_stream_data_bidi_local: the response arrives on a stream WE opened */
    QUIC_TP(0x07, QUIC_UNI_CAP);            /* initial_max_stream_data_uni */
    QUIC_TP(0x09, QUIC_UNI_SLOTS);          /* initial_max_streams_uni: control + two QPACK streams + room for grease */
#undef QUIC_TP
    p += quic_put_vi(o + p, 0x0f); p += quic_put_vi(o + p, QUIC_CID_LEN);       /* initial_source_connection_id */
    for (u32 i = 0; i < QUIC_CID_LEN; i++) o[p++] = qc.scid[i];
    return p;
}

/* Starts a connection to ip:port; `host` is the SNI and the name the certificate must match. Returns immediately; call quic_poll(). */
static void quic_connect(u32 ip, u16 port, const char *host, const u8 seed[16]) {
    u8 *z = (u8 *)&qc; for (u32 i = 0; i < sizeof(qc); i++) z[i] = 0;
    qc.ip = ip; qc.port = port; qc.state = QUIC_HANDSHAKE;
    qc.t_start = QUIC_RAW_MS(); qc.t_last_rx = qc.t_start; qc.pto_ms = 1000;
    tls_conn_reset(host, seed);                                   /* hostname, RNG, transcripts, all the per-connection TLS state */
    tls_rng_bytes(qc.scid, QUIC_CID_LEN);
    tls_rng_bytes(qc.odcid, QUIC_CID_LEN);
    for (u32 i = 0; i < QUIC_CID_LEN; i++) qc.dcid[i] = qc.odcid[i];
    qc.dcid_len = QUIC_CID_LEN;
    u8 *A = QUIC_ARENA;
    qc.sp[QUIC_LVL_INITIAL].ctx = A + QUIC_OFF_CTX_I; qc.sp[QUIC_LVL_INITIAL].ctx_cap = 0x1000;
    qc.sp[QUIC_LVL_HS].ctx      = A + QUIC_OFF_CTX_H; qc.sp[QUIC_LVL_HS].ctx_cap      = 0x1000;
    qc.sp[QUIC_LVL_APP].ctx     = A + QUIC_OFF_CTX_A; qc.sp[QUIC_LVL_APP].ctx_cap     = 0x400;
    quic_rasm_init(&qc.sp[QUIC_LVL_INITIAL].crx, A + QUIC_OFF_CRX_I, 0x1000);
    quic_rasm_init(&qc.sp[QUIC_LVL_HS].crx,      A + QUIC_OFF_CRX_H, 0x8000);
    quic_rasm_init(&qc.sp[QUIC_LVL_APP].crx,     A + QUIC_OFF_CRX_A, 0x2000);
    quic_rasm_init(&qc.s0, A + QUIC_OFF_S0, QUIC_S0_CAP);
    qc.txs[0].used = 1; qc.txs[0].id = 2; qc.txs[0].buf = A + QUIC_OFF_TXS_C; qc.txs[0].cap = 0x400;
    qc.txs[1].used = 1; qc.txs[1].id = 0; qc.txs[1].buf = A + QUIC_OFF_TXS_R; qc.txs[1].cap = QUIC_TXS_R_CAP;
    quic_initial_keys(&qc.sp[QUIC_LVL_INITIAL].tx, &qc.sp[QUIC_LVL_INITIAL].rx, qc.odcid, QUIC_CID_LEN);
    tls_quic_hooks.send = quic_hook_send; tls_quic_hooks.keys = quic_hook_keys; tls_quic_hooks.params = quic_hook_params;
    if (!quic_listen_port) { quic_listen_port = QUIC_LOCAL_PORT; udp_listen(quic_listen_port, quic_udp_rx); }
    serial_puts("[QUIC] connecting to "); net_log_ip(ip); serial_putc(':'); serial_put_dec(port); serial_putc('\n');
    tls_conn.quic_tp_len = (u8)quic_build_tp(tls_conn.quic_tp);
    tls_quic_begin();                                             /* builds the ClientHello; it comes back through quic_hook_send() */
    quic_flush();
}

/* Writes to one of our send streams (0 = control, 1 = request); fin ends the stream. 0 = does not fit, or the peer's limits forbid it. */
static int quic_stream_write(int idx, const u8 *d, u32 n, int fin) {
    quic_txs_t *t = &qc.txs[idx];
    if (!qc.app_keys || !qc.params_ok || t->fin) return 0;
    if (t->len + n > t->cap) return 0;
    if (t->len + n > (idx == 1 ? qc.peer_sd_bidi_remote : qc.peer_sd_uni)) return 0;
    if (qc.tx_data_total + n > qc.peer_max_data) return 0;
    for (u32 i = 0; i < n; i++) t->buf[t->len + i] = d[i];
    t->len += n; qc.tx_data_total += n;
    if (fin) t->fin = 1;
    quic_flush();
    return 1;
}

/* Moves the connection forward: timers, retransmission, and anything the receive path queued. */
static quic_state_t quic_poll(u64 now_packed) {
    qc.now_packed = now_packed;
    if (qc.state == QUIC_IDLE || qc.state == QUIC_CLOSED) return qc.state;
    if (qc.state == QUIC_FAILED) { quic_flush(); return qc.state; }
    if (tls_conn.state == TLS_FAILED && qc.state != QUIC_FAILED) { quic_fail(QF_TLS, QERR_CRYPTO_BASE + quic_tls_alert_for(tls_conn.fail_reason)); quic_flush(); return qc.state; }
    if (qc.state == QUIC_HANDSHAKE) {
        if (!qc.got_rx && quic_elapsed(qc.t_start) >= QUIC_FIRST_RX_MS) { quic_fail(QF_NO_RESPONSE, QERR_NO_ERROR); return qc.state; }
        if (quic_elapsed(qc.t_start) >= QUIC_HANDSHAKE_MS) { quic_fail(QF_TIMEOUT, QERR_NO_ERROR); quic_flush(); return qc.state; }
    } else if (quic_elapsed(qc.t_last_rx) >= QUIC_IDLE_MS) { quic_fail(QF_TIMEOUT, QERR_NO_ERROR); return qc.state; }
    /* PTO: anything ack-eliciting that stayed unacknowledged too long is sent again (once per timeout, the timeout doubles) */
    int fired = 0;
    for (int i = 0; i < QUIC_NREC; i++) {
        quic_rec_t *r = &qc.rec[i];
        if (!r->used || !r->elicit || quic_elapsed(r->t) < qc.pto_ms) continue;
        quic_space_t *s = &qc.sp[r->lvl];
        for (u32 k = 0; k < r->nfr && s->nrtx < 6; k++) s->rtx[s->nrtx++] = r->fr[k];
        if (!r->nfr) s->ping = 1;
        r->used = 0; fired = 1;
    }
    if (fired) {
        qc.pto_count++;
        if (qc.pto_count > QUIC_PTO_MAX_COUNT) { quic_fail(QF_TIMEOUT, QERR_NO_ERROR); return qc.state; }
        if (qc.pto_ms < 8000) qc.pto_ms *= 2;
        serial_puts("[QUIC] PTO: retransmitting\n");
    }
    quic_flush();
    return qc.state;
}

/* A clean shutdown: tell the server (HTTP/3 code 0x100 = H3_NO_ERROR) and stop. */
static void quic_close(u32 app_code) {
    if (qc.state == QUIC_CONNECTED) {
        qc.close_pending = 1; qc.close_app = 1; qc.close_code = app_code;
        quic_flush();
    }
    if (qc.state != QUIC_FAILED) qc.state = QUIC_CLOSED;
}

#endif
