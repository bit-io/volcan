#!/usr/bin/env python3
"""Kontrakt jednowatkowy: wstawia i sprawdza `if (!thread_ok()) return ...;` w funkcjach vcn_*.

  guards.py --apply   wstawia brakujace sprawdzenia (idempotentnie)
  guards.py           tylko sprawdza (kod wyjscia 1, gdy czegos brakuje) - uzywane przez `make check`

Wyjatki: funkcje bez dostepu do Louvre, bezpieczne z kazdego watku (lista EXEMPT) oraz caly blok
BEGIN-STANDALONE..END-STANDALONE (ma wlasne muteksy).
"""
import os, re, sys

EXEMPT = {"vcn_abi_version", "vcn_abi_min", "vcn_abi_compatible", "vcn_features", "vcn_dropped_events",
          "vcn_last_code", "vcn_clear_error", "vcn_thread_violations", "vcn_thread_adopt",
          "vcn_last_error_c", "vcn_last_error_buf"}
FUNC = re.compile(r"^(\s{8})(int64_t|double|void|const char \*)\s*(vcn_\w+)\(([^)]*)\)\s*(\{.*)?$")
DEFAULT = {"int64_t": "0", "double": "0.0", "void": "", "const char *": '""'}
path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib", "shim.hcs")

def guard_text(ret):
    return "if (!thread_ok()) return%s;" % ("" if ret == "void" else " " + DEFAULT[ret])

def process(apply):
    lines = open(path, encoding="utf-8").read().split("\n")
    out, missing, i = [], [], 0
    in_sa = False
    while i < len(lines):
        l = lines[i]
        if "// BEGIN-STANDALONE" in l: in_sa = True
        if "// END-STANDALONE" in l: in_sa = False
        m = FUNC.match(l)
        if m and not in_sa and not l.lstrip().startswith("static"):
            indent, ret, name = m.group(1), m.group(2), m.group(3)
            if name in EXEMPT:
                out.append(l); i += 1; continue
            if m.group(5):                              # { na tej samej linii
                brace = l.index("{")
                if "thread_ok()" in l[brace:brace + 40]:
                    out.append(l)
                elif apply:
                    out.append(l[:brace + 1] + " " + guard_text(ret) + l[brace + 1:])
                else:
                    missing.append(name); out.append(l)
                i += 1; continue
            if i + 1 < len(lines) and lines[i + 1].strip() == "{":       # { w nastepnej linii
                out.append(l); out.append(lines[i + 1])
                if "thread_ok()" in lines[i + 2]:
                    pass
                elif apply:
                    out.append(indent + "    " + guard_text(ret))
                else:
                    missing.append(name)
                i += 2; continue
        out.append(l); i += 1
    if apply:
        open(path, "w", encoding="utf-8").write("\n".join(out))
    return missing

if __name__ == "__main__":
    apply = "--apply" in sys.argv
    process(apply)
    missing = process(False)
    if missing:
        print("BRAK sprawdzenia watku w: " + ", ".join(missing)); sys.exit(1)
    print("OK: wszystkie funkcje vcn_* dotykajace Louvre maja sprawdzenie watku")
