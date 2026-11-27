#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# sdk/include/mw_libc.h (the program-side C library: heap, rand, strtol, qsort ...), built 32-bit like a program is, with the sanitizers on.
set -euo pipefail
cd "$(dirname "$0")"
gcc -m32 -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -fsanitize=undefined,address -o /tmp/host_sdk_libc_test host_sdk_libc_test.c
/tmp/host_sdk_libc_test
