#!/usr/bin/env python3
"""Generuje lib/ffi_ext.hcs: wrappery `ffi_*` dla funkcji shima, ktorych nie ma jeszcze w lib/ffi.hcs.

Obejmuje tylko funkcje o argumentach int64_t/double (bez lancuchow C - dla tych sluza bufory 0/1).
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(__file__))
from shim_protos import protos

root = os.path.join(os.path.dirname(__file__), "..")
ffi = open(os.path.join(root, "lib", "ffi.hcs"), encoding="utf-8").read()
declared = set(re.findall(r"fn (vcn_\w+)\(", ffi))
HS = {"int64_t": ("Int", "i64", "0"), "double": ("Float", "f64", "0.0")}
out = ["!! WYGENEROWANE przez tools/gen_ffi.py z lib/shim.hcs - nie edytuj recznie.",
       "!! Wrappery funkcji shima, ktorych nie ma w ffi.hcs.", ""]
n = 0
for ret, name, params in protos(os.path.join(root, "lib", "shim.hcs")):
    if name in declared:
        continue
    if ret not in HS and ret != "void":
        continue
    if any(p.split()[0] not in HS or "*" in p for p in params):
        continue
    names = [p.split()[-1] for p in params]
    types = [p.split()[0] for p in params]
    hs_sig = ", ".join(f"{a}: {HS[t][0]}" for a, t in zip(names, types))
    rs_sig = ", ".join(f"{a}: {HS[t][1]}" for a, t in zip(names, types))
    call = f"{name}({', '.join(names)})"
    short = name[len("vcn_"):]
    if ret == "void":
        out.append(f"fun ffi_{short}({hs_sig}) [\n    native {{Rust}} [\n        extern \"C\" {{ fn {name}({rs_sig}); }}\n        unsafe {{ {call} }};\n    ]\n    end\n]\n")
    else:
        h, r, zero = HS[ret]
        out.append(f"fun ffi_{short}({hs_sig}) -> {h} [\n    let out = {zero}\n    native {{Rust}} [\n        extern \"C\" {{ fn {name}({rs_sig}) -> {r}; }}\n        out = unsafe {{ {call} }};\n    ]\n    end out\n]\n")
    n += 1
open(os.path.join(root, "lib", "ffi_ext.hcs"), "w", encoding="utf-8").write("\n".join(out))
print(f"lib/ffi_ext.hcs: {n} wrapperow")
