#!/bin/sh
# Buduje i uruchamia test silnika screencopy na prawdziwym libwayland (wymaga: libwayland-dev, wayland-protocols, gcc, g++).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
P=$(pkg-config --variable=pkgdatadir wayland-protocols 2>/dev/null || echo /usr/share/wayland-protocols)
for x in ext-image-capture-source/ext-image-capture-source-v1 ext-image-copy-capture/ext-image-copy-capture-v1; do
    b=$(basename "$x")
    wayland-scanner client-header "$P/staging/$x.xml" "$tmp/$b-client.h"
    wayland-scanner server-header "$P/staging/$x.xml" "$tmp/$b-server.h"
    wayland-scanner private-code "$P/staging/$x.xml" "$tmp/$b.c"
done
python3 "$root/scripts/extract_block.py" PROTOGEN "$tmp/protogen.inc" >/dev/null
python3 "$root/scripts/extract_block.py" SCREENCOPY "$tmp/screencopy.inc" >/dev/null
${CC:-gcc} -c -O1 -w "$tmp/ext-image-capture-source-v1.c" -o "$tmp/a.o"
${CC:-gcc} -c -O1 -w "$tmp/ext-image-copy-capture-v1.c" -o "$tmp/b.o"
wayland-scanner client-header "$root/data/protocols/wlr-screencopy-unstable-v1.xml" "$tmp/wlr-screencopy-unstable-v1-client.h"
wayland-scanner private-code "$root/data/protocols/wlr-screencopy-unstable-v1.xml" "$tmp/wlr.c"
${CC:-gcc} -c -O1 -w "$tmp/wlr.c" -o "$tmp/c.o"
${CXX:-g++} -std=c++20 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -O1 -pthread \
    -DPROTOGEN_INC="\"$tmp/protogen.inc\"" -DSCREENCOPY_INC="\"$tmp/screencopy.inc\"" -I"$tmp" \
    "$here/screencopy_test.cpp" "$tmp/a.o" "$tmp/b.o" "$tmp/c.o" -o "$tmp/screencopy_test" -lwayland-server -lwayland-client
"$tmp/screencopy_test"
