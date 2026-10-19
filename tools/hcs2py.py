#!/usr/bin/env python3
"""Minimalny transpiler podzbioru HackerScript -> Python.

Sluzy WYLACZNIE do testowania czystej logiki (layout.hcs, grab_rect) bez
kompilatora hackerc. Obsluguje: const, struct, fun, let, przypisania,
if/elif/else, while, `end [wyrazenie]`, and/or/not, komentarze `!!`.
Linie `native {...}` i `include` sa pomijane, a dzielenie calkowite `/` -> `//`.
"""
import re, sys

def conv_expr(e):
    e = re.sub(r"\.clone\(\)", "", e)
    e = re.sub(r"(?<![/])/(?![/])", "//", e)
    e = re.sub(r"(\w+(?:\[[^\]]*\])?)\.len\(\)", r"len(\1)", e)
    e = e.replace("true", "True").replace("false", "False")
    return e

def convert(src):
    out, ind = [], 0
    lines = src.split("\n")
    i = 0
    def emit(t): out.append("    " * ind + t)
    while i < len(lines):
        raw = lines[i]; i += 1
        l = raw.strip()
        if "!!" in l: l = l.split("!!")[0].strip()
        if not l or l.startswith("include "): continue
        if l.startswith("native"):
            d = 1 if l.endswith("[") else 0
            while d and i < len(lines):
                t = lines[i].strip(); i += 1
                d += t.count("[") - t.count("]")
            emit("pass"); continue
        m = re.match(r"const (\w+)\s*:\s*\w+\s*=\s*(.*)$", l)
        if m: emit(f"{m[1]} = {conv_expr(m[2])}"); continue
        m = re.match(r"struct (\w+) \[$", l)
        if m:
            fields = []
            while True:
                t = lines[i].strip(); i += 1
                if t == "]": break
                t = t.split("!!")[0].strip().rstrip(",")
                if t: fields.append(t.split(":")[0].strip())
            emit(f"class {m[1]}:")
            emit(f"    def __init__(self, {', '.join(fields)}):")
            for f in fields: emit(f"        self.{f} = {f}")
            emit(f"    def __repr__(self): return '{m[1]}(' + ', '.join(str(getattr(self,k)) for k in {fields!r}) + ')'")
            emit(f"    def __eq__(self, o): return all(getattr(self,k)==getattr(o,k) for k in {fields!r})")
            continue
        m = re.match(r"fun (\w+)\((.*)\)(\s*->\s*.+?)?\s*\[$", l)
        if m:
            args = ", ".join(a.split(":")[0].strip() for a in m[2].split(",") if a.strip())
            emit(f"def {m[1]}({args}):"); ind += 1; continue
        m = re.match(r"\]\s*elif (.*) \[$", l)
        if m: ind -= 1; emit(f"elif {conv_expr(m[1])}:"); ind += 1; continue
        if re.match(r"\]\s*else \[$", l): ind -= 1; emit("else:"); ind += 1; continue
        if l == "]":
            ind -= 1
            if out and out[-1].rstrip().endswith(":"): emit("    pass")
            continue
        m = re.match(r"if (.*) \[$", l)
        if m: emit(f"if {conv_expr(m[1])}:"); ind += 1; continue
        m = re.match(r"while (.*) \[$", l)
        if m: emit(f"while {conv_expr(m[1])}:"); ind += 1; continue
        m = re.match(r"let (\w+)\s*(?::\s*[^=]+)?=\s*(.*)$", l)
        if m:
            v = conv_expr(m[2]); emit(f"{m[1]} = {'[]' if v == '[]' else v}"); continue
        if l == "end": emit("return"); continue
        m = re.match(r"end (.*)$", l)
        if m: emit(f"return {conv_expr(m[1])}"); continue
        l = re.sub(r"\.push\(", ".append(", conv_expr(l))
        l = re.sub(r"(\w+)\.len\(\)", r"len(\1)", l)
        emit(l)
    return "\n".join(out) + "\n"

if __name__ == "__main__":
    sys.stdout.write(convert(open(sys.argv[1], encoding="utf-8").read()))
