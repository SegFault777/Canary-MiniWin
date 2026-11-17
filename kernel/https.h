/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef HTTPS_H
#define HTTPS_H
#include "io.h"
#include "net.h"
#include "tls.h"
#include "serial.h"
#include "httpresp.h"
#include "h3.h"

/* ============================================================
 * https.h -- http.h's counterpart, speaking exactly the same HTTP/1.1
 * GET dialect but through kernel/tls.h instead of talking to
 * kernel/tcp.h directly. Structurally this is http.h with every
 * tcp_* call swapped for its tls_* equivalent (tls_connect() instead of
 * tcp_connect(), tls_send_app_data() instead of tcp_send_data(), and so
 * on) -- the HTTP-level logic (build a GET request, wait for the
 * response, notice when it's done) doesn't change at all between plain
 * and encrypted transport, which is exactly the point of having built
 * tls.h with an interface that mirrors tcp.h's own.
 *
 * Same scope limits as http.h: no redirects, no chunked
 * transfer-encoding, no keep-alive, no cookies. Plus tls.h's own limits
 * on top: one cipher suite, no session resumption, no TLS 1.3. A page
 * this can't fetch is a reason to note the limitation, not a reason to
 * silently pretend it isn't there.
 * ============================================================ */

#define HTTPS_PATH_MAX 1024

typedef enum {
    HTTPS_IDLE = 0,
    HTTPS_CONNECTING,       /* TLS handshake in progress */
    HTTPS_SENDING_REQUEST,
    HTTPS_AWAITING_RESPONSE,
    HTTPS_DONE,
    HTTPS_FAILED,
} https_state_t;

/* Like http.h, the response lives in httpresp.h's shared buffer, not in here. */
typedef struct {
    https_state_t state;
    char host_header[80];
    char path[HTTPS_PATH_MAX];
    int  request_sent;
    int  use_h3;              /* this load runs over QUIC (h3.h) instead of TLS over TCP */
    char bare_host[80];       /* the name without :port: SNI, certificate check, Alt-Svc key */
    u32  ip; u16 port; u8 seed[16];
} https_client_t;

static https_client_t https_client;

static inline void https_get(u32 server_ip, u16 port, const char *host, const char *path, const u8 entropy_seed[16]) {
    https_client.state = HTTPS_CONNECTING;
    hr_reset();
    https_client.request_sent = 0;

    u32 i = 0;
    for (; host[i] && i < sizeof(https_client.host_header) - 8; i++) https_client.host_header[i] = host[i];
    if (port != 443) {
        https_client.host_header[i++] = ':';
        char digits[6]; int nd = 0; u32 pv = port;
        do { digits[nd++] = (char)('0' + pv % 10); pv /= 10; } while (pv && nd < 5);
        while (nd) https_client.host_header[i++] = digits[--nd];
    }
    https_client.host_header[i] = 0;
    for (i = 0; path[i] && i < sizeof(https_client.path) - 1; i++) https_client.path[i] = path[i];
    https_client.path[i] = 0;

    serial_puts("[HTTPS] GET ");
    serial_puts(path);
    serial_puts(" from ");
    net_log_ip(server_ip);
    serial_putc('\n');

    {   /* remember what a fallback from QUIC to TCP needs */
        u32 k = 0; for (; host[k] && k < sizeof(https_client.bare_host) - 1; k++) https_client.bare_host[k] = host[k];
        https_client.bare_host[k] = 0;
        https_client.ip = server_ip; https_client.port = port;
        for (k = 0; k < 16; k++) https_client.seed[k] = entropy_seed[k];
    }
    u16 udp_port = 0;
    https_client.use_h3 = h3_wanted(host, port, &udp_port);
    int forced = h3_force_next; h3_force_next = 0;
    h3_last_used = 0;
    if (https_client.use_h3) {
        h3_get(server_ip, udp_port, host, https_client.host_header, path, entropy_seed, forced);
        h3.forced = (u8)forced;
        return;
    }
    tls_connect(server_ip, port, host, entropy_seed);   /* SNI / name check use the bare host, not host:port */
}

/* HTTP/3 flavour of https_poll(): QUIC handshake, request, answer. A failure before any response byte (and not forced by h3://) marks the
 * host bad for the session and silently restarts the same load over TLS/TCP. */
static inline https_state_t https_poll_h3(u64 now_packed) {
    h3_state_t hs = h3_poll(now_packed);
    if (hs == H3_FAILED) {
        if (!h3.resp_started && !h3.forced) {
            serial_puts("[HTTPS] HTTP/3 failed before any response: falling back to TLS over TCP\n");
            h3_alt_set(https_client.bare_host, 0, 2);
            hr_reset();
            https_client.use_h3 = 0;
            https_client.state = HTTPS_CONNECTING;
            tls_connect(https_client.ip, https_client.port, https_client.bare_host, https_client.seed);
        } else {
            serial_puts("[HTTPS] HTTP/3 load failed, reason="); serial_put_dec((u32)qc.fail); serial_puts(" h3=0x"); serial_put_hex32(h3.err); serial_putc('\n');
            https_client.state = HTTPS_FAILED;
        }
        return https_client.state;
    }
    if (hs == H3_AWAITING || hs == H3_DONE) {
        if (hr_size_fatal()) { serial_puts("[HTTPS] response too large for the buffer, failing\n"); h3_close(); https_client.state = HTTPS_FAILED; return https_client.state; }
        if (hs == H3_DONE || hr.complete) {
            hr_finish();
            serial_puts("[HTTPS] response complete over HTTP/3: status="); serial_put_dec((u32)hr.status); serial_puts(" bytes="); serial_put_dec(hr.len); serial_putc('\n');
            h3_close();
            h3_last_used = 1;
            if (https_client.bare_host[0]) h3_alt_set(https_client.bare_host, h3.port, 1);   /* it works: keep using it for this host */
            https_client.state = HTTPS_DONE;
        } else https_client.state = HTTPS_AWAITING_RESPONSE;
    }
    return https_client.state;
}

static inline https_state_t https_poll(u64 now_packed) {
    if (https_client.use_h3 && https_client.state != HTTPS_IDLE && https_client.state != HTTPS_DONE && https_client.state != HTTPS_FAILED)
        return https_poll_h3(now_packed);
    switch (https_client.state) {
        case HTTPS_CONNECTING: {
            tls_state_t ts = tls_poll(now_packed);
            if (ts == TLS_ESTABLISHED) {
                https_client.state = HTTPS_SENDING_REQUEST;
            } else if (ts == TLS_FAILED) {
                serial_puts("[HTTPS] TLS handshake failed, reason=");
                serial_put_dec((u32)tls_conn.fail_reason);
                serial_putc('\n');
                https_client.state = HTTPS_FAILED;
            }
            break;
        }

        case HTTPS_SENDING_REQUEST:
            tls_poll(now_packed);
            if (!https_client.request_sent) {
                char req[2048];
                u32 n = hr_build_request(req, https_client.host_header, https_client.path);
                if (tls_send_app_data((const u8 *)req, n)) {
                    https_client.request_sent = 1;
                    https_client.state = HTTPS_AWAITING_RESPONSE;
                }
            }
            break;

        case HTTPS_AWAITING_RESPONSE: {
            tls_state_t ts = tls_poll(now_packed);
            if (ts == TLS_FAILED) {
                serial_puts("[HTTPS] connection failed mid-response, reason=");
                serial_put_dec((u32)tls_conn.fail_reason);
                serial_putc('\n');
                https_client.state = HTTPS_FAILED;
                break;
            }
            u8 tmp[4096];
            for (int rounds = 0; rounds < 8 && !hr.complete; rounds++) {
                u16 n = tls_poll_recv_app_data(tmp, sizeof(tmp));
                if (n == 0) break;
                hr_feed(tmp, n);
            }
            if (hr_size_fatal()) {                 /* bigger than the response buffer: fail, don't show a cut-off page */
                serial_puts("[HTTPS] response too large for the buffer, failing\n");
                tls_close();
                https_client.state = HTTPS_FAILED;
                break;
            }
            int peer_done = (tcp_conn.peer_fin_seen || tls_conn.peer_close_notify || tcp_conn.state == TCP_CLOSED) &&
                            tcp_conn.recv_len == 0 && tls_conn.rx_raw_len == 0 && tls_conn.app_recv_len == 0;
            if (hr.complete || peer_done) {
                hr_finish();
                if (hr.alt_h3_port && https_client.bare_host[0]) h3_alt_set(https_client.bare_host, hr.alt_h3_port, 1);   /* Alt-Svc: next time over QUIC */
                serial_puts("[HTTPS] response complete: status=");
                serial_put_dec((u32)hr.status);
                serial_puts(" bytes=");
                serial_put_dec(hr.len);
                serial_putc('\n');
                tls_close();
                https_client.state = HTTPS_DONE;
            }
            break;
        }

        default:
            break;
    }
    return https_client.state;
}

#endif
