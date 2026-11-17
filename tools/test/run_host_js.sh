#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Host-side tests for the JavaScript engine (kernel/js.h) and its DOM glue (kernel/js_dom.h).
set -euo pipefail
cd "$(dirname "$0")"
gcc -O1 -g -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined -o /tmp/host_js_test host_js_test.c
/tmp/host_js_test
gcc -O1 -g -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined -o /tmp/host_js_dom_test host_js_dom_test.c
/tmp/host_js_dom_test
