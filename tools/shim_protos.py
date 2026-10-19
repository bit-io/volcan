"""Wspolny parser prototypow vcn_* z shim.hcs."""
import re, os, sys
sys.path.insert(0, os.path.dirname(__file__))
from extract_shim import extract

PROTO = re.compile(r"^\s*(int64_t|double|void|const char \*)\s*(vcn_\w+)\(([^)]*)\)\s*(?:\{.*)?$", re.M)

def protos(shim_path):
    cpp = extract(shim_path)
    out = []
    for m in PROTO.finditer(cpp):
        line = m.group(0)
        if line.lstrip().startswith("static"):
            continue
        ret, name, args = m.group(1).strip(), m.group(2), m.group(3).strip()
        params = [] if args in ("", "void") else [a.strip() for a in args.split(",")]
        out.append((ret, name, params))
    return out
