#!/usr/bin/env python3
"""Generuje bindings/volcan.py (ctypes) i bindings/volcan.rs (extern "C") z prototypow shima."""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from shim_protos import protos

root = os.path.join(os.path.dirname(__file__), "..")
ps = protos(os.path.join(root, "lib", "shim.hcs"))
import re

def split_param(p):
    m = re.match(r"^(.*?)(\w+)$", p.strip())
    return m.group(1).strip(), m.group(2)

os.makedirs(os.path.join(root, "bindings"), exist_ok=True)

def pytype(t):
    t = t.strip()
    if t.startswith("const char"): return "ctypes.c_char_p"
    if "*" in t: return "ctypes.c_void_p"     # bufor/wskaznik: ctypes.create_string_buffer(n), ctypes.byref(...)
    return {"int64_t": "ctypes.c_int64", "double": "ctypes.c_double", "void": "None"}[t.split()[0]]

L = ['"""WYGENEROWANE przez tools/gen_bindings.py z lib/shim.hcs - nie edytuj recznie.\n\n'
     'Wiazania ctypes do libvolcan.so. Funkcje `*_c` biora/zwracaja bytes; `*_buf` biora bufor\n'
     'z ctypes.create_string_buffer(n) i zwracaja pelna dlugosc napisu.\n"""',
     "import ctypes", "", "SIGNATURES = {"]
for ret, name, params in ps:
    L.append(f'    "{name}": ({pytype(ret)}, [{", ".join(pytype(split_param(p)[0]) for p in params)}]),')
L += ["}", "", "", "def load(path='libvolcan.so'):",
      '    """Laduje biblioteke i ustawia sygnatury; brakujace symbole (starsza libvolcan) sa pomijane."""',
      "    lib = ctypes.CDLL(path)", "    lib.missing = []",
      "    for name, (res, args) in SIGNATURES.items():",
      "        try:", "            fn = getattr(lib, name)", "        except AttributeError:",
      "            lib.missing.append(name)", "            continue",
      "        fn.restype = res", "        fn.argtypes = args", "    return lib", "",
      "", "def read_string(fn, *args, size=256):",
      '    """Czyta napis przez funkcje *_buf (np. vcn_toplevel_title_buf), powiekszajac bufor w razie potrzeby."""',
      "    buf = ctypes.create_string_buffer(size)",
      "    n = fn(*args, buf, size)", "    if n >= size:",
      "        buf = ctypes.create_string_buffer(n + 1)", "        fn(*args, buf, n + 1)",
      "    return buf.value.decode('utf-8', 'replace')", ""]
open(os.path.join(root, "bindings", "volcan.py"), "w", encoding="utf-8").write("\n".join(L))

def rstype(t):
    t = t.strip()
    const = t.startswith("const")
    base = t.replace("const", "").replace("*", "").strip()
    inner = {"char": "std::os::raw::c_char", "uint8_t": "u8", "int64_t": "i64", "double": "f64", "void": ""}[base]
    if "*" in t:
        return ("*const " if const else "*mut ") + inner
    return inner

R = ["// WYGENEROWANE przez tools/gen_bindings.py z lib/shim.hcs - nie edytuj recznie.",
     "// Wiazania FFI do libvolcan.so: #[link(name = \"volcan\")] (pkg-config: volcan).",
     "#![allow(non_camel_case_types, dead_code)]", "", '#[link(name = "volcan")]', 'extern "C" {']
for ret, name, params in ps:
    args = []
    for p in params:
        ptype, pname = split_param(p)
        args.append(f"{pname}: {rstype(ptype)}")
    r = rstype(ret)
    R.append(f"    pub fn {name}({', '.join(args)}){' -> ' + r if r else ''};")
R += ["}", ""]
open(os.path.join(root, "bindings", "volcan.rs"), "w", encoding="utf-8").write("\n".join(R))
print(f"bindings: {len(ps)} funkcji (python + rust)")
