#!/bin/sh
# Test integracyjny: kompozytor volcan zagnieżdżony w bezglowym weston (Louvre wybiera backend Wayland, gdy
# ustawione jest WAYLAND_DISPLAY), z prawdziwym klientem Wayland i sterowaniem przez volcanctl.
# WYMAGA: zbudowanego przykladu (examples/minimal), weston, foot lub weston-terminal. NIE JEST uruchamiany w CI
# bez obrazu z Louvre - patrz docs/ARCHITECTURE.md (sekcja Testy).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
bin=${VOLCAN_TEST_BIN:-"$root/examples/minimal/target/release/volcan-minimal"}
[ -x "$bin" ] || { echo "brak $bin - zbuduj przyklad (bit build) albo ustaw VOLCAN_TEST_BIN"; exit 77; }
command -v weston >/dev/null || { echo "brak weston"; exit 77; }
tmp=$(mktemp -d)
cleanup() { kill $weston_pid $comp_pid 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
export XDG_RUNTIME_DIR="$tmp"
chmod 700 "$tmp"
weston --backend=headless --socket=host-0 >"$tmp/weston.log" 2>&1 &
weston_pid=$!
i=0; while [ ! -S "$tmp/host-0" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
WAYLAND_DISPLAY=host-0 VOLCAN_SOCK="$tmp/minimal.sock" "$bin" >"$tmp/comp.log" 2>&1 &
comp_pid=$!
i=0; while [ ! -S "$tmp/minimal.sock" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
[ -S "$tmp/minimal.sock" ] || { cat "$tmp/comp.log"; echo "kompozytor nie wystartowal"; exit 1; }
export VOLCAN_SOCK="$tmp/minimal.sock"
ctl="$root/bin/volcanctl"
[ "$("$ctl" caps | grep -c ipc-subscribe)" -ge 1 ] || { echo "caps"; exit 1; }
"$ctl" status | grep -q '^workspace=1' || { echo "status"; exit 1; }
"$ctl" layout dwindle | grep -q '^ok' || { echo "layout"; exit 1; }
"$ctl" status | grep -q 'layout_name=dwindle' || { echo "layout_name"; exit 1; }
"$ctl" workspace next | grep -q '^ok' || { echo "workspace next"; exit 1; }
echo "integracja: OK"
