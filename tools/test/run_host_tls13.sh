#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# End-to-end TLS test on the build host: kernel/tls.h <-> `openssl s_server` over loopback, no QEMU. Covers TLS 1.3 (RSA-PSS, ECDSA P-256 and
# P-384 certificates, AES-128 and AES-256 suites), HelloRetryRequest (servers that only take P-256 / P-384), KeyUpdate in both modes, TLS 1.2
# (regression), a client-certificate request, a wrong-CA negative, and crafted-input unit checks (downgrade sentinel, HRR validation, alerts,
# record_overflow, duplicate ServerHello).
# Needs: gcc, openssl, python3 + `cryptography`.   usage: tools/test/run_host_tls13.sh
set -uo pipefail
cd "$(dirname "$0")"
D=/tmp/tls13_test; rm -rf $D; mkdir -p $D /tmp/pki
fails=0
python3 gen_crypto_vectors.py /tmp/crypto_vectors.txt >/dev/null 2>&1 || true
build() {  # $1 = test_roots.h
  gcc -O1 -g -Wall -Wno-unused-function -Wno-unused-variable -DMW_TLS_TEST_ROOTS="\"$1\"" -o $D/t host_tls13_test.c 2>$D/cc.log || { cat $D/cc.log; exit 1; }
}
run() {  # name leaf ca server-args expect [wrong]
  local name=$1 leaf=$2 ca=$3 sargs=$4 expect=$5 wrong=${6:-}
  ( WRONG_ROOT=$wrong SAN=localhost ./mkcerts.sh $leaf $ca >/dev/null 2>&1 ) || { echo "FAIL: mkcerts $leaf $ca"; fails=$((fails+1)); return; }
  build /tmp/pki/test_roots.h
  local port=$((24000 + RANDOM % 8000))
  openssl s_server -www -accept $port -cert /tmp/pki/leaf.pem -key /tmp/pki/leaf.key $sargs >$D/srv.log 2>&1 &
  local pid=$!; sleep 0.6
  out=$(timeout 30 $D/t $port localhost $expect 2>&1); rc=$?
  kill $pid 2>/dev/null; wait $pid 2>/dev/null
  if [ $rc -eq 0 ]; then echo "  pass  $name: $out"; else echo "  FAIL  $name: $out"; tail -3 $D/srv.log; fails=$((fails+1)); fi
}
echo "TLS 1.3 / 1.2 against openssl s_server:"
run "1.3 RSA (rsa_pss) AES-128"     rsa   rsa   "-tls1_3 -ciphersuites TLS_AES_128_GCM_SHA256"  13
run "1.3 RSA (rsa_pss) AES-256"     rsa   rsa   "-tls1_3 -ciphersuites TLS_AES_256_GCM_SHA384"  13
run "1.3 ECDSA P-256 AES-128"       ec256 rsa   "-tls1_3 -ciphersuites TLS_AES_128_GCM_SHA256"  13
run "1.3 ECDSA P-384 AES-256"       ec384 ec384 "-tls1_3 -ciphersuites TLS_AES_256_GCM_SHA384"  13
run "1.3 client cert requested (empty Certificate answered)" rsa rsa "-tls1_3 -Verify 1 -CAfile /tmp/pki/ca.pem" hs13
run "1.3 no session ticket"        rsa   rsa   "-tls1_3 -ciphersuites TLS_AES_128_GCM_SHA256 -no_ticket" 13
run "1.2 RSA (regression)"          rsa   rsa   "-tls1_2"                                         12
run "1.2 ECDSA P-256 (regression)"  ec256 rsa   "-tls1_2"                                         12
run "1.3 untrusted chain refused"   rsa   rsa   "-tls1_3"                                         fail  1
run "1.3 HRR: server takes only P-256, AES-128"  rsa rsa "-tls1_3 -groups P-256 -ciphersuites TLS_AES_128_GCM_SHA256"  13
run "1.3 HRR: server takes only P-256, AES-256"  rsa rsa "-tls1_3 -groups P-256 -ciphersuites TLS_AES_256_GCM_SHA384"  13
run "1.3 HRR: server takes only P-384, AES-128"  ec256 rsa "-tls1_3 -groups P-384 -ciphersuites TLS_AES_128_GCM_SHA256" 13
run "1.3 HRR: server takes only P-384, AES-256"  ec384 ec384 "-tls1_3 -groups P-384 -ciphersuites TLS_AES_256_GCM_SHA384" 13
run "1.3 server prefers P-256 over x25519 (HRR)" rsa rsa "-tls1_3 -groups P-256:X25519"                                   13
run "1.3 HRR then untrusted chain still refused" rsa rsa "-tls1_3 -groups P-256"                                          fail 1
ku() {  # name key(K = server asks us to rekey too, k = it does not) suite
  local name=$1 key=$2 suite=$3
  ( WRONG_ROOT= SAN=localhost ./mkcerts.sh rsa rsa >/dev/null 2>&1 ) || { echo "FAIL: mkcerts"; fails=$((fails+1)); return; }
  build /tmp/pki/test_roots.h
  local port=$((24000 + RANDOM % 8000))
  ( sleep 1.0; echo first; sleep 0.6; printf "$key\n"; sleep 0.6; echo second; sleep 4 ) | openssl s_server -accept $port -cert /tmp/pki/leaf.pem -key /tmp/pki/leaf.key -tls1_3 -ciphersuites $suite >$D/ku.out 2>&1 &
  local pid=$!; sleep 0.8
  out=$(timeout 30 $D/t $port localhost ku 2>&1); rc=$?; sleep 0.4
  kill $pid 2>/dev/null; wait $pid 2>/dev/null
  if [ $rc -eq 0 ] && grep -q "client-two-after-keyupdate" $D/ku.out; then echo "  pass  $name: $out"; else echo "  FAIL  $name: $out"; fails=$((fails+1)); fi
}
echo "KeyUpdate:"
ku "1.3 KeyUpdate(update_requested) AES-128"     K TLS_AES_128_GCM_SHA256
ku "1.3 KeyUpdate(update_not_requested) AES-128" k TLS_AES_128_GCM_SHA256
ku "1.3 KeyUpdate(update_requested) AES-256"     K TLS_AES_256_GCM_SHA384
echo "Unit checks (crafted input):"
build /tmp/pki/test_roots.h
out=$($D/t unit 2>&1); rc=$?
if [ $rc -eq 0 ]; then echo "  pass  $out"; else echo "$out" | sed 's/^/  /'; fails=$((fails+1)); fi
echo "Fuzz (a hostile server's bytes into every TLS parser; ASan + UBSan):"
gcc -O1 -g -Wall -Wno-unused-function -Wno-unused-variable -fsanitize=address,undefined -DMW_TLS_TEST_ROOTS="\"/tmp/pki/test_roots.h\"" -o $D/tfz host_tls13_test.c 2>$D/cc.log || { cat $D/cc.log; exit 1; }
out=$(FUZZ_ITERS=60000 timeout 250 $D/tfz fuzz 2>&1); rc=$?
if [ $rc -eq 0 ]; then echo "  pass  $out"; else echo "  FAIL  fuzz: $(echo "$out" | tail -5)"; fails=$((fails+1)); fi
echo "$fails failure(s)"
exit $fails
