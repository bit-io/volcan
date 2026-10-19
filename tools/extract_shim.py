#!/usr/bin/env python3
"""Wyciaga blok `native {C++} [ ... ]` z lib/shim.hcs do zwyklego pliku .cpp.

Uzycie: extract_shim.py lib/shim.hcs build/volcan_shim.cpp
Zrodlem prawdy pozostaje shim.hcs - plik .cpp jest tylko wygenerowany.
"""
import sys

def extract(path):
    src = open(path, encoding="utf-8").read()
    start = src.index("native {C++} [") + len("native {C++} [")
    depth, j = 1, start
    while depth:
        c = src[j]
        depth += (c == "[") - (c == "]")
        j += 1
    body = src[start:j - 1]
    lines = body.split("\n")
    inc = [l.strip() for l in lines if l.strip().startswith("#")]
    rest = [l for l in lines if not l.strip().startswith("#")]
    return ("// WYGENEROWANE z lib/shim.hcs - nie edytuj recznie.\n" + "\n".join(inc) +
            '\nextern "C" void __hks_native_cpp_block_0() {\n' + "\n".join(rest) + "\n}\n")

if __name__ == "__main__":
    open(sys.argv[2], "w", encoding="utf-8").write(extract(sys.argv[1]))
