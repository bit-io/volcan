#!/bin/sh
# Buduje i uruchamia testy blokow shima bez Louvre: STANDALONE (konfiguracja, IPC, procesy, watch) i LIBINPUT
# (parser regul; wymaga naglowkow libinput - pakiet libinput-dev).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
python3 "$root/scripts/extract_block.py" STANDALONE "$tmp/standalone.inc"
${CXX:-g++} -std=c++20 -Wall -Wextra -Wno-unused-function -O1 -pthread \
    -DSTANDALONE_INC="\"$tmp/standalone.inc\"" -I"$here" \
    "$here/standalone_test.cpp" -o "$tmp/standalone_test"
"$tmp/standalone_test"
if [ -f /usr/include/libinput.h ] || pkg-config --exists libinput 2>/dev/null; then
    python3 "$root/scripts/extract_block.py" LIBINPUT "$tmp/libinput.inc"
    ${CXX:-g++} -std=c++20 -Wall -Wextra -Wno-unused-function -O1 \
        -DLIBINPUT_INC="\"$tmp/libinput.inc\"" -I"$here" $(pkg-config --cflags libinput 2>/dev/null) \
        "$here/libinput_test.cpp" -o "$tmp/libinput_test"
    "$tmp/libinput_test"
else
    echo "libinput: POMINIETO (brak libinput-dev)"
fi
python3 "$root/scripts/extract_block.py" I3IPC "$tmp/i3.inc" >/dev/null
${CXX:-g++} -std=c++20 -Wall -Wextra -Wno-unused-function -O1 -pthread \
    -DI3IPC_INC="\"$tmp/i3.inc\"" -I"$here" "$here/i3_test.cpp" -o "$tmp/i3_test"
"$tmp/i3_test"
python3 "$root/scripts/extract_block.py" STANDALONE "$tmp/standalone2.inc" >/dev/null
${CXX:-g++} -std=c++20 -Wall -Wextra -Wno-unused-function -O1 -pthread \
    -DSTANDALONE_INC="\"$tmp/standalone2.inc\"" -I"$here" "$here/fuzz_test.cpp" -o "$tmp/fuzz_test"
"$tmp/fuzz_test"
if pkg-config --exists wayland-server wayland-client 2>/dev/null && command -v wayland-scanner >/dev/null 2>&1; then
    sh "$here/screencopy_run.sh"
else
    echo "screencopy: POMINIETO (brak libwayland-dev / wayland-scanner)"
fi
