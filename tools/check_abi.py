#!/usr/bin/env python3
"""Sprawdza zgodnosc deklaracji `extern "C"` w lib/ffi.hcs z prototypami w lib/shim.hcs."""
import os, re, sys
sys.path.insert(0, os.path.dirname(__file__))
from shim_protos import protos

root = os.path.join(os.path.dirname(__file__), "..")
shim = {n: (r, p) for r, n, p in protos(os.path.join(root, "lib", "shim.hcs"))}
ffi = "".join(open(os.path.join(root, "lib", f), encoding="utf-8").read() for f in ("ffi.hcs", "ffi_ext.hcs"))
MAP = {"int64_t": "i64", "double": "f64", "void": None}
errors, used = [], set()
for m in re.finditer(r'extern \\?"C\\?" \{ fn (vcn_\w+)\(([^)]*)\)(?: -> (\w+))?; \}', ffi):
    name, args, ret = m.group(1), m.group(2).strip(), m.group(3)
    used.add(name)
    if name not in shim:
        errors.append(f"{name}: brak w shimie"); continue
    sret, sparams = shim[name]
    if MAP.get(sret, "?") != ret:
        errors.append(f"{name}: zwracany typ ffi={ret} shim={sret}")
    fparams = [a.split(":")[1].strip() for a in args.split(",") if a.strip()]
    sp = [MAP.get(p.split()[0], "?") for p in sparams]
    if fparams != sp:
        errors.append(f"{name}: parametry ffi={fparams} shim={sp}")
c_only = sorted(n for n in shim if n not in used)
print(f"ffi.hcs + ffi_ext.hcs: {len(used)} deklaracji, shim: {len(shim)} funkcji")
print("tylko w shimie (API dla innych jezykow):", ", ".join(c_only) or "-")
if errors:
    print("BLEDY:"); [print("  " + e) for e in errors]; sys.exit(1)
print("OK: ffi.hcs/ffi_ext.hcs zgodne z shim.hcs")
