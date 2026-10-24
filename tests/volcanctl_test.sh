#!/bin/sh
# Test end-to-end: volcanctl <-> prawdziwy kod IPC z shim.hcs (skompilowany osobno).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
tmp=$(mktemp -d)
cleanup() { [ -n "$srv" ] && kill "$srv" 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
srv=""
python3 "$root/scripts/extract_block.py" STANDALONE "$tmp/standalone.inc" >/dev/null
${CXX:-g++} -std=c++20 -O1 -pthread -Wno-unused-function \
    -DSTANDALONE_INC="\"$tmp/standalone.inc\"" -I"$here/standalone" \
    "$here/standalone/ipc_server.cpp" -o "$tmp/server"
ctl="$root/bin/volcanctl"
export VOLCAN_SOCK="$tmp/v.sock"
fail() { echo "BLAD: $1" >&2; exit 1; }

"$tmp/server" "$VOLCAN_SOCK" > "$tmp/server.out" &
srv=$!
i=0; while [ ! -S "$VOLCAN_SOCK" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i+1)); done
[ -S "$VOLCAN_SOCK" ] || fail "serwer nie wystartowal"

[ "$("$ctl" ping)" = "pong" ] || fail "ping"
"$ctl" nieznane >/dev/null 2>&1 && fail "kod wyjscia dla error powinien byc 1" || true
[ "$("$ctl" nieznane 2>&1)" = "error: nieznane polecenie" ] || fail "tekst bledu"

# subskrypcja z filtrem: dwa zdarzenia, jedno przefiltrowane
"$ctl" subscribe workspace,focus > "$tmp/sub.out" &
sub=$!
sleep 0.4
"$ctl" publish workspace 2 >/dev/null
"$ctl" publish "window_open 5 foot	zsh" >/dev/null
"$ctl" publish focus 5 >/dev/null
sleep 0.4
kill $sub 2>/dev/null || true
wait $sub 2>/dev/null || true
[ "$(cat "$tmp/sub.out")" = "workspace 2
focus 5" ] || { cat "$tmp/sub.out"; fail "filtr subskrypcji"; }

# subskrypcja bez filtra widzi tez window_open (z tabulatorem w tytule)
"$ctl" subscribe > "$tmp/all.out" &
sub=$!
sleep 0.4
"$ctl" publish "window_open 7 foot	zsh" >/dev/null
sleep 0.3
kill $sub 2>/dev/null || true
wait $sub 2>/dev/null || true
grep -q "^window_open 7 foot	zsh$" "$tmp/all.out" || fail "subskrypcja bez filtra"

# brak gniazda -> kod 2
VOLCAN_SOCK="$tmp/nie-ma.sock" "$ctl" ping >/dev/null 2>&1 && fail "brak gniazda powinien dac blad" || [ $? -eq 2 ] || fail "kod 2 dla braku gniazda"

"$ctl" quit >/dev/null
wait $srv 2>/dev/null || true
srv=""

# token
"$tmp/server" "$VOLCAN_SOCK" sekret > "$tmp/server2.out" &
srv=$!
i=0; while [ ! -S "$VOLCAN_SOCK" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i+1)); done
[ "$(VOLCAN_TOKEN=zly "$ctl" ping 2>&1 || true)" = "error: brak autoryzacji" ] || fail "zly token"
[ "$(VOLCAN_TOKEN=sekret "$ctl" ping)" = "pong" ] || fail "dobry token"
echo sekret > "$tmp/tok"
[ "$(VOLCAN_TOKEN_FILE="$tmp/tok" "$ctl" ping)" = "pong" ] || fail "token z pliku"
VOLCAN_TOKEN=sekret "$ctl" quit >/dev/null
wait $srv 2>/dev/null || true
srv=""
echo "volcanctl: OK"
