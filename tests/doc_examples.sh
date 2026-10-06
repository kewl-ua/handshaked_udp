#!/bin/sh
# Compiles every complete C program in README.md and docs/*.md (a ```c block with a main function)
# against the library with warnings as errors, so the examples in the documentation can't rot.
# Uses $CC and $CFLAGS from the environment, as passed by `make test`.
set -eu

: "${CC:=gcc}"
: "${CFLAGS:=-Wall -Wextra -Iinclude}"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for md in README.md docs/*.md; do
    awk -v dir="$work" -v doc="$(basename "$md" .md)" '
        /^```c$/            { inside = 1; n++; file = sprintf("%s/%s-%02d.c", dir, doc, n); next }
        /^```$/ && inside   { inside = 0; close(file); next }
        inside              { print > file }
    ' "$md"
done

compiled=0

for src in "$work"/*.c; do
    [ -e "$src" ] || continue
    grep -q "int main" "$src" || continue # A fragment, not a program

    name=$(head -n 1 "$src" | sed -n 's#^// *\([A-Za-z0-9_]*\.c\).*#\1#p')

    if ! $CC $CFLAGS -Werror "$src" lib/libhudp.a -o "${src%.c}" 2> "$work/errors.txt"; then
        echo "FAIL: documentation example ${name:-$(basename "$src")} does not compile"
        cat "$work/errors.txt"
        exit 1
    fi

    compiled=$((compiled + 1))
done

[ "$compiled" -gt 0 ] || { echo "FAIL: no C programs found in the documentation"; exit 1; }

echo "OK: $compiled documentation examples compile"
