#!/usr/bin/env bash
# Builds and runs the host-side logic tests with a plain C compiler --
# no ESP-IDF toolchain required. Each test_*.c #includes its corresponding
# real firmware source file directly (not a copy), so these test the
# actual shipped logic.
set -euo pipefail

CC="${CC:-gcc}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/.build"
mkdir -p "$OUT"

CFLAGS=(-std=c17 -Wall -Wextra -Werror -I"$HERE" -I"$HERE/stubs" -I"$HERE/../main")

status=0
for test_src in "$HERE"/test_*.c; do
    name="$(basename "$test_src" .c)"
    bin="$OUT/$name"
    echo "=== building $name ==="
    "$CC" "${CFLAGS[@]}" "$test_src" -o "$bin"
    echo "=== running $name ==="
    if ! "$bin"; then
        status=1
    fi
    echo
done

exit $status
