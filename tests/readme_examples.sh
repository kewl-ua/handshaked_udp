#!/bin/sh
# Compiles every complete C program in README.md (a ```c block with a main function) against
# the library with warnings as errors, so the examples in the documentation can't rot.
# Uses $CC and $CFLAGS from the environment, as passed by `make test`.
set -eu

: "${CC:=gcc}"
: "${CFLAGS:=-Wall -Wextra -Iinclude}"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

awk -v dir="$work" '
    /^```c$/            { inside = 1; n++; file = sprintf("%s/example%02d.c", dir, n); next }
    /^```$/ && inside   { inside = 0; close(file); next }
    inside              { print > file }
' README.md

compiled=0

for src in "$work"/example*.c; do
    [ -e "$src" ] || continue
    grep -q "int main" "$src" || continue # A fragment, not a program

    name=$(head -n 1 "$src" | sed -n 's#^// *\([A-Za-z0-9_]*\.c\).*#\1#p')

    if ! $CC $CFLAGS -Werror "$src" lib/libhudp.a -o "${src%.c}" 2> "$work/errors.txt"; then
        echo "FAIL: README example ${name:-$(basename "$src")} does not compile"
        cat "$work/errors.txt"
        exit 1
    fi

    compiled=$((compiled + 1))
done

[ "$compiled" -gt 0 ] || { echo "FAIL: no C programs found in README.md"; exit 1; }

echo "OK: $compiled README examples compile"
