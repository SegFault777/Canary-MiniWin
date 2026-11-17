#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# kernel/tz.h (time zones, minute offsets, daylight-saving rules) on the real kernel code.
set -euo pipefail
cd "$(dirname "$0")"
gcc -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -fsanitize=undefined,address -o /tmp/host_tz_test host_tz_test.c
/tmp/host_tz_test
