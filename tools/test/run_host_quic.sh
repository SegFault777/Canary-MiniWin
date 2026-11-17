#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# End-to-end QUIC / HTTP/3 test on the build host: kernel/quic.h + h3.h + tls.h (QUIC mode) <-> tools/test/quic_server.py over loopback UDP, no QEMU.
# Covers: a plain GET (RSA and ECDSA certificates), a 150 KB body, redirects, POST, Huffman-coded headers, Alt-Svc, loss in both directions,
# HelloRetryRequest, a server key update, and the refusals: untrusted certificate, wrong ALPN, wrong transport parameters, Retry, Version
# Negotiation, no answer at all. Plus unit checks (RFC 9001 Appendix A key vectors, varints, QPACK/Huffman, reassembly, ACK ranges).
# Needs: gcc, openssl, python3 + `cryptography`.   usage: tools/test/run_host_quic.sh
set -uo pipefail
cd "$(dirname "$0")"
D=/tmp/quic_test; rm -rf $D; mkdir -p $D /tmp/pki
fails=0
build() { gcc -O1 -g -Wall -Wno-unused-function -Wno-unused-variable -DMW_TLS_TEST_ROOTS="\"$1\"" -o $D/t host_quic_test.c 2>$D/cc.log || { cat $D/cc.log; exit 1; }; }
run() {  # name leaf wrongroot server-args expect path [post]
  local name=$1 leaf=$2 wrong=$3 sargs=$4 expect=$5 path=${6:-/} post=${7:-}
  ( WRONG_ROOT=$wrong SAN=localhost ./mkcerts.sh $leaf rsa >/dev/null 2>&1 ) || { echo "FAIL: mkcerts"; fails=$((fails+1)); return; }
  build /tmp/pki/test_roots.h
  local port=$((26000 + RANDOM % 8000))
  python3 quic_server.py --port $port --lifetime 40 $sargs >$D/srv.out 2>$D/srv.err &
  local pid=$!; sleep 0.8
  out=$(QUIC_TRACE=1 timeout 45 $D/t $port localhost $expect $path $post 2>$D/cli.err); rc=$?
  sleep 0.3; kill $pid 2>/dev/null; wait $pid 2>/dev/null
  if [ $rc -eq 0 ]; then echo "  pass  $name: $out"; else echo "  FAIL  $name: $out"; tail -4 $D/srv.err | sed 's/^/        srv: /'; tail -6 $D/cli.err | sed 's/^/        cli: /'; fails=$((fails+1)); fi
}
echo "Unit checks:"
build /tmp/pki/test_roots.h
out=$($D/t unit 2>&1); rc=$?
if [ $rc -eq 0 ]; then echo "  pass  $out"; else echo "$out" | sed 's/^/  /'; fails=$((fails+1)); fi
echo "HTTP/3 against quic_server.py:"
run "GET /, ECDSA P-256 certificate"          ec256 ""  ""                          ok
run "GET /, RSA (rsa_pss) certificate"        rsa   ""  ""                          ok
run "150 KB body over many packets"           ec256 ""  ""                          big   /big
run "302 redirect (QPACK index 66 continuation)" ec256 "" ""                        redir /redir
run "POST body"                               ec256 ""  ""                          post  /post "a=1&b=two2"
run "Huffman-coded header values"             ec256 ""  ""                          huff  /huff
run "Alt-Svc header parsed"                   ec256 ""  "--alt-svc 8443"            altsvc:8443
run "server datagrams lost (every 4th), 150 KB" ec256 "" "--drop-every 4"           big   /big
run "first 2 client datagrams lost (client PTO)" ec256 "" "--drop-client 2"         ok
run "HelloRetryRequest (server wants P-256)"  ec256 ""  "--hrr"                     ok
run "server-initiated key update mid-response" ec256 "" "--keyupdate"               big   /big
echo "Refusals:"
run "untrusted certificate authority"         ec256 1   ""                          fail:3
run "server selects ALPN h2"                  ec256 ""  "--bad-alpn"                fail:3
run "wrong original_destination_connection_id" ec256 "" "--bad-odcid"               fail:3
run "wrong initial_source_connection_id"      ec256 ""  "--bad-iscid"               fail:3
run "no transport parameters"                 ec256 ""  "--no-tp"                   fail:3
run "Retry (unsupported -> clean failure)"    ec256 ""  "--retry"                   fail:7
run "Version Negotiation (clean failure)"     ec256 ""  "--vn"                      fail:6
echo "Fuzz (hostile datagrams, frames and HTTP/3 into every parser; ASan + UBSan):"
gcc -O1 -g -Wall -Wno-unused-function -Wno-unused-variable -fsanitize=address,undefined -DMW_TLS_TEST_ROOTS="\"/tmp/pki/test_roots.h\"" -o $D/tf host_quic_test.c 2>$D/cc.log || { cat $D/cc.log; exit 1; }
out=$(FUZZ_ITERS=4 FUZZ_ROUNDS=400 timeout 250 $D/tf fuzz 2>&1); rc=$?
if [ $rc -eq 0 ]; then echo "  pass  $out"; else echo "  FAIL  fuzz: $(echo "$out" | tail -5)"; fails=$((fails+1)); fi
echo "No server at all:"
build /tmp/pki/test_roots.h
out=$(timeout 30 $D/t 9 localhost fail:1 2>&1); rc=$?
if [ $rc -eq 0 ]; then echo "  pass  nothing answers (UDP blocked): $out"; else echo "  FAIL  nothing answers: $out"; fails=$((fails+1)); fi
echo "$fails failure(s)"
exit $fails
