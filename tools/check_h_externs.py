#!/usr/bin/env python3
"""Sprawdza, czy `extern dynamic [c, "volcan"]` w plikach .h# zgadza sie z prototypami shima."""
import glob, os, re, sys
sys.path.insert(0, os.path.dirname(__file__))
from shim_protos import protos

root = os.path.join(os.path.dirname(__file__), "..")
shim = {n: (r, p) for r, n, p in protos(os.path.join(root, "lib", "shim.hcs"))}
T = {"int": "int64_t", "f64": "double", "string": "const char *", None: "void"}
bad, total = [], 0
for path in glob.glob(os.path.join(root, "examples", "**", "*.h#"), recursive=True):
    src = open(path, encoding="utf-8").read()
    for blk in re.finditer(r'extern dynamic \[c, "volcan"\] is(.*?)\nend', src, re.S):
        for m in re.finditer(r"fn (\w+)\(([^)]*)\)(?:\s*->\s*(\w+))?", blk.group(1)):
            name, args, ret = m.group(1), m.group(2), m.group(3)
            total += 1
            if name not in shim:
                bad.append(f"{path}: {name} nie istnieje w shimie"); continue
            sret, sp = shim[name]
            if T[ret] != sret:
                bad.append(f"{name}: zwracany typ {ret} vs {sret}")
            hp = [T[a.split(":")[1].strip()] for a in args.split(",") if a.strip()]
            cp = ["const char *" if "char" in p else p.split()[0] for p in sp]
            if hp != cp:
                bad.append(f"{name}: parametry {hp} vs {cp}")
print(f"H#: {total} deklaracji extern sprawdzonych")
if bad:
    print("BLEDY:"); [print("  " + b) for b in bad]; sys.exit(1)
print("OK: deklaracje H# zgodne z libvolcan")
