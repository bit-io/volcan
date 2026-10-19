#!/usr/bin/env bash
# Kompiluje blok C++ z lib/shim.hcs do pliku obiektowego na PRAWDZIWYCH naglowkach Louvre 3.x
# (bez linkowania - nie wymaga zbudowanej Louvre, Skii, SRM ani Ream).
#
# Wymaga: git, g++ (C++20), python3 oraz naglowkow systemowych:
#   apt install libwayland-dev libxkbcommon-dev libdrm-dev libinput-dev libudev-dev \
#               libseat-dev libegl-dev libgles-dev libgl-dev libboost-container-dev libfontconfig-dev
# Zaleznosci Louvre (Core, Ream, SRM, naglowki Skii) sa klonowane plytko do $VOLCAN_DEPS
# (domyslnie build/deps). Uwaga: sprawdza HEAD galezi main - Louvre 3.x jeszcze sie zmienia.
set -euo pipefail
cd "$(dirname "$0")/.."
DEPS="${VOLCAN_DEPS:-build/deps}"
GEN=build/gen
mkdir -p "$DEPS" "$GEN"
export GIT_TERMINAL_PROMPT=0

clone() { [ -d "$DEPS/$2" ] || git clone --quiet --depth 1 "https://github.com/CuarzoSoftware/$1.git" "$DEPS/$2"; }
clone Louvre louvre
clone Core core
clone Ream ream
clone SRM srm
if [ ! -d "$DEPS/skia" ]; then
    git clone --quiet --depth 1 --filter=blob:none --sparse https://github.com/CuarzoSoftware/Skia.git "$DEPS/skia"
    git -C "$DEPS/skia" sparse-checkout set headers
fi

# Naglowki wersji generowane z szablonow .h.in (numery nie maja znaczenia dla kompilacji).
for f in "$DEPS/core/src/CZ/Core/CZCoreVersion.h.in" "$DEPS/ream/src/CZ/Ream/CZReamVersion.h.in" \
         "$DEPS/srm/src/CZ/SRM/CZSRMVersion.h.in" "$DEPS/louvre/src/CZ/Louvre/CZLouvreVersion.h.in"; do
    sed -e 's/@VERSION_MAJOR@/3/g;s/@VERSION_MINOR@/0/g;s/@VERSION_PATCH@/0/g' "$f" > "$GEN/$(basename "$f" .in)"
done

python3 tools/extract_shim.py lib/shim.hcs build/volcan_shim.cpp
INC=(-I"$GEN" -I"$DEPS/core/src" -I"$DEPS/ream/src" -I"$DEPS/srm/src" -I"$DEPS/louvre/src"
     -I"$DEPS/skia/headers" -I/usr/include/libdrm)
g++ -std=c++20 -Wall -Werror=unused-function -Werror=unused-variable -Werror=return-type -fPIC -c "${INC[@]}" build/volcan_shim.cpp -o build/volcan_shim.o
echo "OK: shim kompiluje sie na naglowkach Louvre ($(git -C "$DEPS/louvre" rev-parse --short HEAD))"
nm -C --defined-only build/volcan_shim.o | grep -c " T vcn_" | sed 's/^/wyeksportowanych funkcji vcn_*: /'
