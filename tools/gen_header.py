#!/usr/bin/env python3
"""Generuje include/volcan.h (naglowek C) z prototypow w lib/shim.hcs."""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from shim_protos import protos

root = os.path.join(os.path.dirname(__file__), "..")
ps = protos(os.path.join(root, "lib", "shim.hcs"))
L = ["/* WYGENEROWANE przez tools/gen_header.py z lib/shim.hcs - nie edytuj recznie. */",
     "#ifndef VOLCAN_H", "#define VOLCAN_H", "", "#include <stdint.h>", "",
     "#ifdef __cplusplus", 'extern "C" {', "#endif", "",
     "/* Uchwyty (surface, toplevel, output...) to liczby int64_t; 0 oznacza brak.",
     " * Funkcje *_c zwracaja wskaznik wazny do okolo 64 kolejnych wywolan funkcji *_c. */", ""]
for ret, name, params in ps:
    L.append(f"{ret}{'' if ret.endswith('*') else ' '}{name}({', '.join(params) if params else 'void'});")
L += ["", "#ifdef __cplusplus", "}", "#endif", "", "#endif /* VOLCAN_H */", ""]
open(os.path.join(root, "include", "volcan.h"), "w", encoding="utf-8").write("\n".join(L))
print(f"include/volcan.h: {len(ps)} funkcji")
