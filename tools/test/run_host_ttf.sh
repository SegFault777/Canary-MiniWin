#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# The TrueType rasterizer (kernel/ttf.h): Galmuri's TTFs must reproduce the font pack's bitmap faces exactly, other fonts must render,
# hostile fonts must not crash it (ASan/UBSan).  The Galmuri TTFs (SIL OFL 1.1, ~20 MB) are downloaded to /tmp/galmuri if they are not there;
# without them (no network) that part is skipped.  Other test fonts: whatever .ttf/.otf the system has, plus the CFF one that must be refused.
#   tools/test/run_host_ttf.sh
set -euo pipefail
cd "$(dirname "$0")"
GAL=${GALMURI_DIR:-/tmp/galmuri}
mkdir -p "$GAL"
for f in Galmuri11 Galmuri7 Galmuri9 Galmuri14 Galmuri11-Bold; do
  [ -s "$GAL/$f.ttf" ] || curl -sfL -m 60 -o "$GAL/$f.ttf" "https://raw.githubusercontent.com/quiple/galmuri/main/dist/$f.ttf" || rm -f "$GAL/$f.ttf"
done
python3 ttf_ref.py /tmp > /dev/null
EXTRA=()
for f in /usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf /usr/share/fonts/truetype/liberation/LiberationSerif-Italic.ttf \
         /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf /usr/share/fonts/opentype/inter/InterDisplay-MediumItalic.otf; do
  [ -f "$f" ] && EXTRA+=("$f")
done
gcc -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-variable -fsanitize=address,undefined -fno-sanitize-recover=undefined -o /tmp/host_ttf_test host_ttf_test.c
/tmp/host_ttf_test "$GAL" /tmp "${EXTRA[@]}"
