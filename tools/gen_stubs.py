#!/usr/bin/env python3
"""Generuje slabe (weak) zaslepki wszystkich funkcji vcn_* - uzywane przez testy z atrapa kompozytora.

Uzycie: gen_stubs.py wyjscie.cpp
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from shim_protos import protos

root = os.path.join(os.path.dirname(__file__), "..")
L = ["// WYGENEROWANE przez tools/gen_stubs.py - slabe zaslepki, nadpisywane przez atrape.",
     "#include <stdint.h>", 'extern "C" {']
for ret, name, params in protos(os.path.join(root, "lib", "shim.hcs")):
    sig = ", ".join(params) if params else "void"
    body = {"void": "", "double": " return 0.0;", "int64_t": " return 0;"}.get(ret, ' return "";')
    L.append(f'__attribute__((weak)) {ret}{"" if ret.endswith("*") else " "}{name}({sig}) {{{body} }}')
L.append("}")
open(sys.argv[1], "w").write("\n".join(L) + "\n")
