#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A small QUIC v1 + TLS 1.3 + HTTP/3 server for testing kernel/quic.h and kernel/h3.h on the build host (no QEMU, no network).

It is written from the RFCs (9000, 9001, 9002 loss recovery in spirit, 9114, 9204) on top of the `cryptography` package only, and shares no
code with the kernel's C implementation, so a misreading of the specification would have to be made twice to go unnoticed.
It deliberately misbehaves on request, for the negative tests:

  --hrr            only accept P-256: answer the ClientHello with a HelloRetryRequest
  --drop-every N   silently drop every Nth datagram it sends (the client must reassemble, the server retransmits on its own timer)
  --drop-client N  ignore the first N datagrams from the client (the client's PTO must resend)
  --keyupdate      switch to updated 1-RTT keys in the middle of the response (RFC 9001 6)
  --vn / --retry   answer the first Initial with Version Negotiation / a Retry packet (the client must give up cleanly)
  --bad-alpn       select "h2" instead of "h3"          --bad-odcid  a wrong original_destination_connection_id parameter
  --no-tp          no transport parameters at all       --bad-iscid  a wrong initial_source_connection_id parameter
  --alt-svc PORT   add an Alt-Svc header to every response
  --sig rsa|ec     certificate type of /tmp/pki/leaf.pem (CertificateVerify scheme follows)
Routes: /  small page; /big  150000 bytes; /redir  302 to /; /post  echoes the length of the request body; /huff  Huffman-coded header values.
"""
import argparse, hashlib, hmac, os, select, socket, struct, sys, time, re
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.asymmetric import x25519, ec, padding
from cryptography.hazmat.primitives import hashes, serialization
from cryptography import x509

H = hashlib.sha256
def hm(k, m): return hmac.new(k, m, H).digest()
def label(secret, lab, ctx, n):
    info = struct.pack(">HB", n, 6 + len(lab)) + b"tls13 " + lab + bytes([len(ctx)]) + ctx
    return hm(secret, info + b"\x01")[:n]

def vi(v):
    if v < 64: return bytes([v])
    if v < 16384: return struct.pack(">H", 0x4000 | v)
    if v < 1 << 30: return struct.pack(">I", 0x80000000 | v)
    return struct.pack(">Q", 0xC000000000000000 | v)
def rvi(b, i):
    l = 1 << (b[i] >> 6); v = b[i] & 0x3F
    for k in range(1, l): v = (v << 8) | b[i + k]
    return v, i + l

class K:
    def __init__(s, secret, hp=None):
        s.secret = secret; s.key = label(secret, b"quic key", b"", 16); s.iv = label(secret, b"quic iv", b"", 12)
        s.hp = hp or label(secret, b"quic hp", b"", 16); s.aead = AESGCM(s.key)
    def mask(s, sample): return Cipher(algorithms.AES(s.hp), modes.ECB()).encryptor().update(sample)
    def nxt(s): return K(label(s.secret, b"quic ku", b"", 32), hp=s.hp)

SALT = bytes.fromhex("38762cf7f55934b34d179ae6a4c80cadccbb7f0a")
def initial_keys(dcid):
    i = hm(SALT, dcid)
    return K(label(i, b"client in", b"", 32)), K(label(i, b"server in", b"", 32))   # (client's, server's)

def protect(keys, kind, dcid, scid, pn, payload, kp=0):
    if kind == "short":
        hdr = bytes([0x40 | (kp << 2) | 3]) + dcid
    else:
        t = 0 if kind == "initial" else 2
        hdr = bytes([0xC0 | (t << 4) | 3]) + struct.pack(">I", 1) + bytes([len(dcid)]) + dcid + bytes([len(scid)]) + scid
        if kind == "initial": hdr += b"\x00"
        hdr += struct.pack(">H", 0x4000 | (len(payload) + 4 + 16))
    full = hdr + pn.to_bytes(4, "big")
    nonce = bytes(a ^ b for a, b in zip(keys.iv, (pn).to_bytes(12, "big")))
    pkt = bytearray(full + keys.aead.encrypt(nonce, bytes(payload), full))
    po = len(hdr); m = keys.mask(bytes(pkt[po + 4:po + 20]))
    pkt[0] ^= m[0] & (0x1F if kind == "short" else 0x0F)
    for i in range(4): pkt[po + i] ^= m[1 + i]
    return bytes(pkt)

def unprotect(data, off, end, po, keys, largest, short):
    pkt = bytearray(data[off:end]); po -= off
    m = keys.mask(bytes(pkt[po + 4:po + 20]))
    pkt[0] ^= m[0] & (0x1F if short else 0x0F)
    pnl = (pkt[0] & 3) + 1; t = 0
    for i in range(pnl): pkt[po + i] ^= m[1 + i]; t = (t << 8) | pkt[po + i]
    win = 1 << (8 * pnl); exp = largest + 1; cand = (exp & ~(win - 1)) | t
    if cand + win // 2 <= exp: cand += win
    elif cand > exp + win // 2 and cand >= win: cand -= win
    nonce = bytes(a ^ b for a, b in zip(keys.iv, cand.to_bytes(12, "big")))
    pt = keys.aead.decrypt(nonce, bytes(pkt[po + pnl:]), bytes(pkt[:po + pnl]))
    return cand, pt, bool((pkt[0] >> 2) & 1) if short else False

# ---- Huffman (the table comes from the kernel's own h3.h, which tools/gen_huffman.py verified against libnghttp2) ----
def load_huff():
    src = open(os.path.join(os.path.dirname(__file__), "../../kernel/h3.h")).read()
    code = [int(x.rstrip("u"), 16) for x in re.search(r"h3_huff_code\[257\] = \{(.*?)\};", src, re.S).group(1).replace("\n", " ").split(",") if x.strip()]
    ln = [int(x) for x in re.search(r"h3_huff_len\[257\] = \{(.*?)\};", src, re.S).group(1).replace("\n", " ").split(",") if x.strip()]
    return code, ln
HC, HL = load_huff()
def henc(b):
    bits = "".join(f"{HC[c]:0{HL[c]}b}" for c in b); bits += "1" * ((8 - len(bits) % 8) % 8)
    return bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))
def qint(prefix, flags, v):
    mx = (1 << prefix) - 1
    if v < mx: return bytes([flags | v])
    out = bytearray([flags | mx]); v -= mx
    while v >= 128: out.append(0x80 | (v & 127)); v >>= 7
    out.append(v); return bytes(out)
def qstr(s, huff=False):
    s = s.encode() if isinstance(s, str) else s
    if huff:
        e = henc(s); return qint(7, 0x80, len(e)) + e
    return qint(7, 0, len(s)) + s

def u16(n): return struct.pack(">H", n)
def u24(n): return struct.pack(">I", n)[1:]
def hs_msg(t, body): return bytes([t]) + u24(len(body)) + body

class Conn:
    pass

class Server:
    def __init__(s, a):
        s.a = a
        s.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.sock.bind(("127.0.0.1", a.port)); s.sock.setblocking(False)
        s.leaf_der = x509.load_pem_x509_certificate(open("/tmp/pki/leaf.pem", "rb").read()).public_bytes(serialization.Encoding.DER)
        s.leaf_key = serialization.load_pem_private_key(open("/tmp/pki/leaf.key", "rb").read(), None)
        s.c = None; s.sent_count = 0; s.dropc = 0; s.log = a.verbose
    def p(s, *x):
        if s.log: print("[srv]", *x, file=sys.stderr, flush=True)

    # --- sending ---
    def raw_send(s, d, addr):
        s.sent_count += 1
        if s.a.drop_every and s.sent_count % s.a.drop_every == 0: s.p("DROP outgoing datagram", s.sent_count); return
        s.sock.sendto(d, addr)
    def pkt(s, lvl, frames, track=True):
        c = s.c; sp = c.sp[lvl]
        pn = sp["next"]; sp["next"] += 1
        payload = frames
        if lvl == "initial": kind, keys = "initial", c.skeys["initial"]
        elif lvl == "handshake": kind, keys = "handshake", c.skeys["handshake"]
        else: kind, keys = "short", c.skeys["app"]
        d = protect(keys, kind, c.cscid, c.scid, pn, payload, c.kp_s if lvl == "app" else 0)
        return pn, d
    def ack_frame(s, lvl):
        r = sorted(s.c.sp[lvl]["rx"], reverse=True)
        if not r: return b""
        ranges = []; hi = lo = r[0]
        for x in r[1:]:
            if x == lo - 1: lo = x
            else: ranges.append((hi, lo)); hi = lo = x
        ranges.append((hi, lo))
        out = b"\x02" + vi(ranges[0][0]) + vi(0) + vi(len(ranges) - 1) + vi(ranges[0][0] - ranges[0][1])
        for (h1, l1), (h0, l0) in zip(ranges[1:], ranges):
            out += vi(l0 - h1 - 2) + vi(h1 - l1)
        return out
    def send(s, lvl, frames, retransmit=True, coalesce=None):
        c = s.c
        ack = s.ack_frame(lvl) if c.sp[lvl]["ackneeded"] else b""
        c.sp[lvl]["ackneeded"] = False
        pn, d = s.pkt(lvl, ack + frames)
        if retransmit and frames: c.out[(lvl, pn)] = (frames, time.time())
        return d
    def flush_dg(s, parts):
        s.raw_send(b"".join(parts), s.c.addr)

    def crypto_frames(s, lvl, data):
        c = s.c; sp = c.sp[lvl]; out = []
        i = 0
        while i < len(data):
            chunk = data[i:i + 1000]
            out.append(b"\x06" + vi(sp["coff"]) + vi(len(chunk)) + chunk); sp["coff"] += len(chunk); i += len(chunk)
        return out

    # --- TLS ---
    def th(s): return H(s.c.transcript).digest()
    def on_client_hello(s, msg, first):
        c = s.c
        body = msg[4:]; i = 2 + 32; sid = body[i]; i += 1 + sid
        cs = body[i] << 8 | body[i + 1]; i += 2; suites = [body[i + k] << 8 | body[i + k + 1] for k in range(0, cs, 2)]; i += cs
        i += 1 + body[i]
        el = body[i] << 8 | body[i + 1]; i += 2; ext = {}
        e = body[i:i + el]; j = 0
        while j < len(e):
            t = e[j] << 8 | e[j + 1]; l = e[j + 2] << 8 | e[j + 3]; ext[t] = e[j + 4:j + 4 + l]; j += 4 + l
        assert sid == 0 and suites == [0x1301], (sid, suites)
        assert ext[43] == b"\x02\x03\x04", ext[43]
        assert 16 in ext and ext[16] == b"\x00\x03\x02h3", ext.get(16)
        tp = ext[0x39]; s.check_client_tp(tp)
        ks = ext[51]; l = ks[0] << 8 | ks[1]; shares = {}; j = 2
        while j < 2 + l:
            g = ks[j] << 8 | ks[j + 1]; n = ks[j + 2] << 8 | ks[j + 3]; shares[g] = ks[j + 4:j + 4 + n]; j += 4 + n
        if s.a.hrr and first and 0x17 not in shares:
            # HelloRetryRequest for P-256 (RFC 8446 4.1.4): transcript becomes message_hash(CH1) || HRR
            ch1 = H(msg).digest()
            hrr_random = bytes.fromhex("CF21AD74E59A6111BE1D8C021E65B891C2A211167ABB8C5E079E09E2C8A8339C")
            exts = b"\x00\x2b\x00\x02\x03\x04" + b"\x00\x33\x00\x02\x00\x17"
            hrr = hs_msg(2, b"\x03\x03" + hrr_random + b"\x00" + b"\x13\x01" + b"\x00" + u16(len(exts)) + exts)
            c.transcript = bytes([254, 0, 0, 32]) + ch1 + hrr
            c.hrr_sent = True
            return ("hrr", hrr)
        c.transcript += msg
        if 0x17 in shares and (s.a.hrr or 0x1d not in shares):
            priv = ec.generate_private_key(ec.SECP256R1()); grp = 0x17
            pub = priv.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
            peer = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), shares[0x17]); ecdhe = priv.exchange(ec.ECDH(), peer)
        else:
            priv = x25519.X25519PrivateKey.generate(); grp = 0x1d
            pub = priv.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
            ecdhe = priv.exchange(x25519.X25519PublicKey.from_public_bytes(shares[0x1d]))
        exts = b"\x00\x2b\x00\x02\x03\x04" + b"\x00\x33" + u16(4 + len(pub)) + u16(grp) + u16(len(pub)) + pub
        sh = hs_msg(2, b"\x03\x03" + os.urandom(32) + b"\x00" + b"\x13\x01" + b"\x00" + u16(len(exts)) + exts)
        c.transcript += sh
        z = bytes(32); early = hm(b"", z); emp = H(b"").digest()
        hs = hm(label(early, b"derived", emp, 32), ecdhe)
        c.c_hs = label(hs, b"c hs traffic", s.th(), 32); c.s_hs = label(hs, b"s hs traffic", s.th(), 32)
        c.master = hm(label(hs, b"derived", emp, 32), z)
        c.ckeys["handshake"] = K(c.c_hs); c.skeys["handshake"] = K(c.s_hs)
        # server flight
        tp = s.server_tp()
        eexts = (b"\x00\x10\x00\x05\x00\x03\x02" + (b"h2" if s.a.bad_alpn else b"h3")) + ((b"\x00\x39" + u16(len(tp)) + tp) if not s.a.no_tp else b"")
        ee = hs_msg(8, u16(len(eexts)) + eexts)
        cert = hs_msg(11, b"\x00" + u24(3 + len(s.leaf_der) + 2) + u24(len(s.leaf_der)) + s.leaf_der + b"\x00\x00")
        c.transcript += ee + cert
        sd = b" " * 64 + b"TLS 1.3, server CertificateVerify\x00" + s.th()
        if isinstance(s.leaf_key, ec.EllipticCurvePrivateKey): alg, sig = 0x0403, s.leaf_key.sign(sd, ec.ECDSA(hashes.SHA256()))
        else: alg, sig = 0x0804, s.leaf_key.sign(sd, padding.PSS(padding.MGF1(hashes.SHA256()), 32), hashes.SHA256())
        cv = hs_msg(15, u16(alg) + u16(len(sig)) + sig)
        c.transcript += cv
        fin = hs_msg(20, hm(label(c.s_hs, b"finished", b"", 32), s.th()))
        c.transcript += fin
        c.c_ap = label(c.master, b"c ap traffic", s.th(), 32); c.s_ap = label(c.master, b"s ap traffic", s.th(), 32)
        c.th_srv_fin = s.th()
        c.skeys["app"] = K(c.s_ap)
        return ("hello", sh, ee + cert + cv + fin)
    def check_client_tp(s, tp):
        i = 0; got = {}
        while i < len(tp):
            t, i = rvi(tp, i); l, i = rvi(tp, i); got[t] = tp[i:i + l]; i += l
        assert got[0x0f] == s.c.cscid, "client initial_source_connection_id"
        assert 0 not in got and 0x10 not in got and 0x02 not in got, "client sent a server-only parameter"
        s.c.client_tp = got
        s.p("client TP ids", sorted(got))
    def server_tp(s):
        c = s.c
        odcid = os.urandom(8) if s.a.bad_odcid else c.odcid
        iscid = os.urandom(8) if s.a.bad_iscid else c.scid
        out = b"\x00" + vi(len(odcid)) + odcid + b"\x0f" + vi(len(iscid)) + iscid
        for t, v in ((1, 30000), (3, 1472), (4, 1 << 20), (6, 1 << 18), (7, 100000), (8, 100), (9, 100), (0x0e, 4)):
            out += vi(t) + vi(len(vi(v))) + vi(v)
        return out

    # --- datagram handling ---
    def parse_dg(s, d, addr):
        off = 0
        while off < len(d):
            b0 = d[off]
            if b0 & 0x80:
                ver = struct.unpack(">I", d[off + 1:off + 5])[0]; dl = d[off + 5]; dcid = d[off + 6:off + 6 + dl]; p = off + 6 + dl
                sl = d[p]; scid = d[p + 1:p + 1 + sl]; p += 1 + sl; typ = (b0 >> 4) & 3
                if typ == 0:
                    tl, p = rvi(d, p); p += tl
                ln, p = rvi(d, p); end = p + ln
                if typ == 0: s.on_initial(d, off, end, p, dcid, scid, addr)
                elif typ == 2 and s.c: s.on_handshake(d, off, end, p)
                off = end
            else:
                s.on_short(d, off, len(d)); break
    def on_initial(s, d, off, end, po, dcid, scid, addr):
        if s.c is None:
            if s.a.vn:
                s.sock.sendto(bytes([0x80]) + b"\x00\x00\x00\x00" + bytes([len(scid)]) + scid + bytes([len(dcid)]) + dcid + struct.pack(">I", 1), addr); return
            if s.a.retry:
                s.sock.sendto(bytes([0xF0]) + struct.pack(">I", 1) + bytes([len(scid)]) + scid + bytes([8]) + os.urandom(8) + os.urandom(20) + os.urandom(16), addr); return
            c = s.c = Conn(); c.addr = addr; c.odcid = dcid; c.cscid = scid; c.scid = os.urandom(8)
            c.ckeys = {}; c.skeys = {}; c.sp = {l: {"next": 0, "rx": set(), "largest": -1, "ackneeded": False, "coff": 0} for l in ("initial", "handshake", "app")}
            c.ckeys["initial"], c.skeys["initial"] = initial_keys(dcid)
            c.transcript = b""; c.crypto_in = {"initial": {}, "handshake": {}}; c.out = {}; c.kp_s = 0; c.kp_c = 0; c.hrr_sent = False
            c.streams = {}; c.responded = False; c.state = "initial"; c.ch_seen = 0; c.hs_buf = b""; c.in_buf = b""; c.cin_off = 0; c.hin_off = 0
            c.app_rx = None
        c = s.c
        try: pn, pt, _ = unprotect(d, off, end, po, c.ckeys["initial"], c.sp["initial"]["largest"], False)
        except Exception as e: s.p("initial decrypt failed", e); return
        s.frames("initial", pn, pt)
    def on_handshake(s, d, off, end, po):
        c = s.c
        if "handshake" not in c.ckeys: return
        try: pn, pt, _ = unprotect(d, off, end, po, c.ckeys["handshake"], c.sp["handshake"]["largest"], False)
        except Exception as e: s.p("handshake decrypt failed", e); return
        s.frames("handshake", pn, pt)
    def on_short(s, d, off, end):
        c = s.c
        if c is None or c.app_rx is None: return
        keys = c.app_rx
        first = d[off]
        # key phase of the packet (header protection must be removed to read it: unprotect() does that and reports it)
        try:
            pn, pt, kp = unprotect(d, off, end, off + 1 + 8, keys, c.sp["app"]["largest"], True)
        except Exception:
            try:
                nk = keys.nxt(); pn, pt, kp = unprotect(d, off, end, off + 1 + 8, nk, c.sp["app"]["largest"], True)
                c.app_rx = nk; c.kp_c ^= 1; s.p("client key update accepted")
            except Exception as e: s.p("1-RTT decrypt failed", e); return
        s.frames("app", pn, pt)

    def frames(s, lvl, pn, pt):
        c = s.c; sp = c.sp[lvl]
        sp["rx"].add(pn); sp["largest"] = max(sp["largest"], pn)
        i = 0; elicit = False
        while i < len(pt):
            t, i = rvi(pt, i)
            if t == 0: continue
            if t == 1: elicit = True; continue
            if t in (2, 3):
                lg, i = rvi(pt, i); _, i = rvi(pt, i); cnt, i = rvi(pt, i); first, i = rvi(pt, i)
                hi, lo = lg, lg - first; s.acked(lvl, lo, hi)
                for _ in range(cnt):
                    g, i = rvi(pt, i); ln, i = rvi(pt, i); hi = lo - g - 2; lo = hi - ln; s.acked(lvl, lo, hi)
                if t == 3:
                    for _ in range(3): _, i = rvi(pt, i)
                continue
            elicit = True
            if t == 6:
                o, i = rvi(pt, i); n, i = rvi(pt, i); data = pt[i:i + n]; i += n
                s.crypto_in(lvl, o, data)
            elif 8 <= t <= 15:
                sid, i = rvi(pt, i); o = 0
                if t & 4: o, i = rvi(pt, i)
                if t & 2: n, i = rvi(pt, i)
                else: n = len(pt) - i
                data = pt[i:i + n]; i += n
                s.stream_in(sid, o, data, bool(t & 1))
            elif t == 0x1c or t == 0x1d:
                code, i = rvi(pt, i)
                if t == 0x1c: _, i = rvi(pt, i)
                rl, i = rvi(pt, i); i += rl
                print(f"CLIENT_CLOSE type=0x{t:x} code=0x{code:x}", flush=True); c.closed = code
            elif t == 0x1e: pass
            else: s.p("unknown frame", hex(t)); return
        if elicit: sp["ackneeded"] = True; s.send_ack_only(lvl)
    def send_ack_only(s, lvl):
        c = s.c
        if lvl == "app" and c.app_rx is None: return
        if lvl == "app" and "app" not in c.skeys: return
        if lvl == "handshake" and "handshake" not in c.skeys: return
        if lvl == "initial" and c.state != "initial": pass
        if getattr(s, "_defer", False): return
        a = s.ack_frame(lvl)
        if not a: return
        c.sp[lvl]["ackneeded"] = False
        pn, d = s.pkt(lvl, a)
        s.raw_send(d, c.addr)
    def acked(s, lvl, lo, hi):
        for k in [k for k in s.c.out if k[0] == lvl and lo <= k[1] <= hi]: del s.c.out[k]

    def crypto_in(s, lvl, o, data):
        c = s.c
        buf = c.crypto_in[lvl]; buf[o] = data
        # reassemble in order
        cur = c.cin_off if lvl == "initial" else c.hin_off
        acc = c.in_buf if lvl == "initial" else c.hs_buf
        while True:
            hit = [k for k in buf if k <= cur < k + len(buf[k])]
            if not hit: break
            k = hit[0]; piece = buf.pop(k)[cur - k:]; acc += piece; cur += len(piece)
        if lvl == "initial": c.cin_off = cur; c.in_buf = acc
        else: c.hin_off = cur; c.hs_buf = acc
        if lvl == "initial":
            while len(c.in_buf) >= 4 and len(c.in_buf) >= 4 + int.from_bytes(c.in_buf[1:4], "big"):
                n = 4 + int.from_bytes(c.in_buf[1:4], "big"); msg = c.in_buf[:n]; c.in_buf = c.in_buf[n:]
                s.handle_ch(msg)
        else:
            while len(c.hs_buf) >= 4 and len(c.hs_buf) >= 4 + int.from_bytes(c.hs_buf[1:4], "big"):
                n = 4 + int.from_bytes(c.hs_buf[1:4], "big"); msg = c.hs_buf[:n]; c.hs_buf = c.hs_buf[n:]
                s.handle_client_finished(msg)
    def handle_ch(s, msg):
        c = s.c
        if c.state not in ("initial", "hrr"): return
        r = s.on_client_hello(msg, first=not c.hrr_sent)
        if r[0] == "hrr":
            c.state = "hrr"
            parts = [s.send("initial", f) for f in s.crypto_frames("initial", r[1])]
            s.raw_send(b"".join(parts), c.addr); return
        c.state = "hs"
        _, sh, flight = r
        init_pkt = s.send("initial", b"".join(s.crypto_frames("initial", sh)))
        hfr = s.crypto_frames("handshake", flight)
        # coalesce the Initial with the first Handshake packet, like a real server
        first = s.send("handshake", hfr[0]); s.raw_send(init_pkt + first, c.addr)
        for f in hfr[1:]: s.raw_send(s.send("handshake", f), c.addr)
    def handle_client_finished(s, msg):
        c = s.c
        if msg[0] != 20 or c.state != "hs": return
        exp = hm(label(c.c_hs, b"finished", b"", 32), H(c.transcript).digest())
        if msg[4:] != exp: print("CLIENT_FINISHED_BAD", flush=True); return
        print("HANDSHAKE_OK", flush=True)
        c.transcript += msg
        c.app_rx = K(c.c_ap); c.state = "app"
        # HANDSHAKE_DONE, the server's three unidirectional streams (control with SETTINGS, QPACK encoder / decoder: empty)
        settings = vi(0x01) + vi(0) + vi(0x07) + vi(0) + vi(0x2a) + vi(7)      # + a grease-ish setting
        ctl = vi(0) + b"\x04" + vi(len(settings)) + settings
        fr = b"\x1e" + b"\x0a" + vi(3) + vi(len(ctl)) + ctl + b"\x0a" + vi(7) + vi(1) + b"\x02" + b"\x0a" + vi(11) + vi(1) + b"\x03"
        c.sp["app"]["soff"] = {3: len(ctl), 7: 1, 11: 1, 0: 0}
        s.raw_send(s.send("app", fr), c.addr)
    def stream_in(s, sid, o, data, fin):
        c = s.c; st = c.streams.setdefault(sid, {"parts": {}, "fin": None, "buf": b"", "off": 0})
        st["parts"][o] = data
        if fin: st["fin"] = o + len(data)
        while True:
            hit = [k for k in st["parts"] if k <= st["off"] < k + len(st["parts"][k])]
            if not hit: break
            k = hit[0]; piece = st["parts"].pop(k)[st["off"] - k:]; st["buf"] += piece; st["off"] += len(piece)
        if sid == 0 and st["fin"] is not None and st["off"] == st["fin"] and not c.responded:
            c.responded = True; s.respond(st["buf"])
    def respond(s, req):
        c = s.c; i = 0; blocks = b""; body = b""
        while i < len(req):
            t, i = rvi(req, i); l, i = rvi(req, i)
            if t == 1: blocks = req[i:i + l]
            elif t == 0: body += req[i:i + l]
            i += l
        path = s.decode_path(blocks)
        s.p("request", path, "body", len(body))
        extra = b""
        if s.a.alt_svc: extra = qint(4, 0x50, 83) + qstr('h3=":%d"; ma=3600' % s.a.alt_svc)
        if path == "/redir":
            head = b"\x00\x00" + qint(6, 0xC0, 66) + qint(4, 0x50, 12) + qstr("/") + extra; data = b""
        elif path == "/big":
            data = bytes((i * 7 + 3) & 0xFF for i in range(150000))
            head = b"\x00\x00" + qint(6, 0xC0, 25) + qint(6, 0xC0, 52) + qint(4, 0x50, 4) + qstr(str(len(data))) + extra
        elif path == "/post":
            data = ("posted %d bytes: %s" % (len(body), body.decode(errors="replace"))).encode()
            head = b"\x00\x00" + qint(6, 0xC0, 25) + qint(6, 0xC0, 52) + extra
        elif path == "/huff":
            data = b"huffman ok"
            nm = henc(b"x-test")
            head = (b"\x00\x00" + qint(6, 0xC0, 25) + qint(6, 0xC0, 53)
                    + qint(3, 0x20 | 0x08, len(nm)) + nm + qstr("Hello, World! 0123456789 ~{}|", True)
                    + qint(4, 0x50, 14) + qstr("a=b; path=/", True) + extra)
        else:
            data = b"<html><body><h1>hello over HTTP/3</h1></body></html>"
            head = b"\x00\x00" + qint(6, 0xC0, 25) + qint(6, 0xC0, 52) + extra
        # a 103 Early Hints first, then the final response (the client must skip the 1xx)
        early = vi(1) + vi(len(b"\x00\x00" + qint(6, 0xC0, 24))) + b"\x00\x00" + qint(6, 0xC0, 24)
        msg = early + vi(1) + vi(len(head)) + head
        for k in range(0, len(data), 4000):
            ch = data[k:k + 4000]; msg += vi(0) + vi(len(ch)) + ch
        st = c.sp["app"]["soff"]; o = st[0]; c.resp_off = o
        i = 0; n = 0
        while i < len(msg) or (i == 0 and not msg):
            chunk = msg[i:i + 1100]; fin = i + len(chunk) >= len(msg)
            if s.a.keyupdate and n == 20:
                c.skeys["app"] = c.skeys["app"].nxt(); c.kp_s ^= 1; s.p("server key update")
            frame = bytes([0x08 | 0x04 | 0x02 | (1 if fin else 0)]) + vi(0) + vi(o + i) + vi(len(chunk)) + chunk
            s.raw_send(s.send("app", frame), c.addr); i += len(chunk); n += 1
            if fin: break
    def decode_path(s, blk):
        # the request block: prefix(2) then field lines; we only need :path (static name ref 1 or literal)
        i = 2; path = "?"
        while i < len(blk):
            b = blk[i]
            if b & 0x80: i += 1
            elif b & 0x40:
                idx = b & 0x0F; i += 1
                if idx == 0x0F: v = blk[i]; idx += v; i += 1
                n = blk[i] & 0x7F; i += 1
                if n == 0x7F: raise Exception("long value")
                val = blk[i:i + n]; i += n
                if idx == 1: path = val.decode()
            elif b & 0x20:
                nl = b & 7; i += 1
                if nl == 7: nl += blk[i]; i += 1
                i += nl
                vl, i2 = blk[i] & 0x7F, i + 1
                if vl == 0x7F: vl = 127 + blk[i2]; i2 += 1
                i = i2 + vl
            else: raise Exception("bad block")
        return path
    def retransmit(s):
        c = s.c
        if c is None: return
        now = time.time()
        for (lvl, pn), (frames, t) in list(c.out.items()):
            if now - t > 0.45:
                del c.out[(lvl, pn)]
                if lvl == "app" and "app" not in c.skeys: continue
                s.p("retransmit", lvl, pn)
                s.raw_send(s.send(lvl, frames), c.addr)
    def run(s):
        print("READY", flush=True)
        t_end = time.time() + s.a.lifetime
        while time.time() < t_end:
            r, _, _ = select.select([s.sock], [], [], 0.05)
            if r:
                d, addr = s.sock.recvfrom(4000)
                if s.dropc < s.a.drop_client: s.dropc += 1; s.p("DROP client datagram"); continue
                try: s.parse_dg(d, addr)
                except Exception as e:
                    import traceback; s.p("error", repr(e)); traceback.print_exc()
            s.retransmit()
            if s.c is not None and getattr(s.c, "closed", None) is not None and not s.c.out: break

ap = argparse.ArgumentParser()
ap.add_argument("--port", type=int, required=True)
for f in ("hrr", "keyupdate", "vn", "retry", "bad-alpn", "bad-odcid", "no-tp", "bad-iscid", "verbose"): ap.add_argument("--" + f, action="store_true")
ap.add_argument("--drop-every", type=int, default=0); ap.add_argument("--drop-client", type=int, default=0)
ap.add_argument("--alt-svc", type=int, default=0); ap.add_argument("--lifetime", type=float, default=25)
a = ap.parse_args()
for k in ("bad_alpn", "bad_odcid", "no_tp", "bad_iscid"): pass
Server(a).run()
