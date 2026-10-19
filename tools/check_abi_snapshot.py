#!/usr/bin/env python3
"""Pilnuje zgodnosci ABI wersji 1: porownuje prototypy vcn_* z shim.hcs ze zrzutem abi/abi-1.txt.

Dozwolone bez podnoszenia wersji ABI: DODAWANIE funkcji (zrzut aktualizuje `--update`).
Niedozwolone (kod wyjscia 1): usuniecie funkcji, zmiana typu zwracanego lub typow parametrow,
zmiana liczby parametrow. Nazwy parametrow moga sie zmieniac. Taka zmiana wymaga nowej wersji ABI
(vcn_abi_version), nowego zrzutu abi/abi-N.txt i zmiany `vcn_abi_min`.

  check_abi_snapshot.py             sprawdz (bledy lamiace ABI => 1; nowe funkcje => informacja)
  check_abi_snapshot.py --strict    nowe funkcje bez wpisu w zrzucie tez sa bledem (CI)
  check_abi_snapshot.py --update    dopisz nowe funkcje do zrzutu (odmowa, gdy sa zmiany lamiace)
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shim_protos import protos

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SNAP = os.path.join(ROOT, "abi", "abi-1.txt")

def norm_param(p):
    m = re.match(r"^(.*?)(\w+)$", p.strip())
    t = (m.group(1) if m else p).strip()
    return re.sub(r"\s*\*\s*", " *", t).strip() if "*" in t else t

def signature(ret, name, params):
    ps = ", ".join(norm_param(p) for p in params) if params else "void"
    return f"{ret} {name}({ps})"

def current(shim_protos):
    return {name: signature(ret, name, params) for ret, name, params in shim_protos}

def compare(snapshot, now):
    """snapshot, now: {nazwa: sygnatura}. Zwraca (bledy_lamiace, nowe_funkcje)."""
    breaking = []
    for name, sig in snapshot.items():
        if name not in now:
            breaking.append(f"usunieta funkcja: {sig}")
        elif now[name] != sig:
            breaking.append(f"zmieniona sygnatura: {sig}  ->  {now[name]}")
    return breaking, sorted(n for n in now if n not in snapshot)

def read_snapshot(path=SNAP):
    snap = {}
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if line and not line.startswith("#"):
            snap[re.search(r"(vcn_\w+)\(", line).group(1)] = line
    return snap

def write_snapshot(now, path=SNAP):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write("# Zrzut ABI wersji 1 biblioteki volcan (patrz tools/check_abi_snapshot.py).\n")
        f.write("# Wiersz: typ_zwracany nazwa(typy parametrow). Nie edytuj recznie - uzyj --update.\n")
        for name in sorted(now):
            f.write(now[name] + "\n")

if __name__ == "__main__":
    now = current(protos(os.path.join(ROOT, "lib", "shim.hcs")))
    if "--update" in sys.argv:
        snap = read_snapshot() if os.path.exists(SNAP) else {}
        breaking, new = compare(snap, now)
        if breaking:
            print("ODMOWA: zmiany lamiace ABI:\n  " + "\n  ".join(breaking)); sys.exit(1)
        write_snapshot(now)
        print(f"zrzut zaktualizowany: {len(now)} funkcji ({len(new)} nowych)"); sys.exit(0)
    snap = read_snapshot()
    breaking, new = compare(snap, now)
    if breaking:
        print("BLAD: zmiana lamiaca ABI 1 (nowa wersja ABI zamiast zmiany prototypu):\n  " + "\n  ".join(breaking)); sys.exit(1)
    if new and "--strict" in sys.argv:
        print(f"BLAD: {len(new)} nowych funkcji bez wpisu w abi/abi-1.txt (uruchom --update): " + ", ".join(new)); sys.exit(1)
    print(f"OK: ABI 1 zgodne ze zrzutem ({len(snap)} funkcji" + (f", {len(new)} nowych - uruchom --update" if new else "") + ")")
