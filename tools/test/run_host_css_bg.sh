#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# CSS background colours checked on painted pixels (kernel/render.h on the host).
set -euo pipefail
cd "$(dirname "$0")"
gcc -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -fsanitize=undefined,address -o /tmp/host_css_bg_test host_css_bg_test.c
/tmp/host_css_bg_test
