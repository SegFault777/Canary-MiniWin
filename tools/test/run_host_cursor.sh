#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# The pointer trails of kernel/ui/cursor.h with a fake clock and a canvas (ASan/UBSan).
#   tools/test/run_host_cursor.sh
set -euo pipefail
cd "$(dirname "$0")"
gcc -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-variable -Wno-comment -fsanitize=address,undefined -fno-sanitize-recover=undefined -o /tmp/host_cursor_test host_cursor_test.c
/tmp/host_cursor_test
