#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# kernel/img.h -- the PNG / JPEG / GIF decoders -- against Pillow's decodes of tools/test/img_corpus/ (made with Pillow, plus hand-built
# Adam7 PNGs), with truncated and corrupted copies of every small file thrown in, under ASan + UBSan.
set -euo pipefail
cd "$(dirname "$0")"
gcc -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -fsanitize=undefined,address -o /tmp/host_img_test host_img_test.c
/tmp/host_img_test img_corpus
