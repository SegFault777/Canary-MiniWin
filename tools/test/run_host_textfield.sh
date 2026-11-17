#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# kernel/ui/textfield.h (the shared one-line text field) on the real kernel code, font and screen stubbed.
set -euo pipefail
cd "$(dirname "$0")"
gcc -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -fsanitize=undefined,address -o /tmp/host_textfield_test host_textfield_test.c
/tmp/host_textfield_test
