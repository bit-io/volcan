#!/usr/bin/env python3
"""Testy czystej logiki layout.hcs i grab_rect (przez tools/hcs2py.py)."""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from hcs2py import convert
root = os.path.join(os.path.dirname(__file__), "..", "lib")
ns = {"toplevel_resize": lambda *a: calls.append(("resize",) + a),
      "surface_move": lambda *a: calls.append(("move",) + a),
      "toplevel_window_x": lambda h: 0, "toplevel_window_y": lambda h: 0}
calls = []
for f in ("layout.hcs",):
    exec(compile(convert(open(os.path.join(root, f), encoding="utf-8").read()), f, "exec"), ns)
g = open(os.path.join(root, "grab.hcs"), encoding="utf-8").read()
exec(compile(convert(g), "grab.hcs", "exec"), ns)
Rect, lc = ns["Rect"], ns["layout_compute"]

def overlap(a, b):
    return a.x < b.x + b.w and b.x < a.x + a.w and a.y < b.y + b.h and b.y < a.y + a.h

def inside(r, area):
    return r.x >= area.x and r.y >= area.y and r.x + r.w <= area.x + area.w and r.y + r.h <= area.y + area.h

area = Rect(10, 20, 1920, 1080)
n_checked = 0
for kind in range(4):
    for n in range(0, 13):
        for gap in (0, 8):
            rs = lc(kind, n, area, gap, 550)
            assert len(rs) == n, (kind, n, len(rs))
            for r in rs:
                assert r.w >= 1 and r.h >= 1 and inside(r, area), (kind, n, gap, r)
            if kind != 3:
                for i in range(n):
                    for j in range(i + 1, n):
                        assert not overlap(rs[i], rs[j]), (kind, n, gap, rs[i], rs[j])
            n_checked += 1
# konkretne wartosci
assert lc(0, 1, area, 8, 550) == [Rect(10, 20, 1920, 1080)]
two = lc(0, 2, area, 8, 500)
assert two == [Rect(10, 20, 956, 1080), Rect(974, 20, 956, 1080)], two
three = lc(0, 3, area, 8, 500)
assert three[1].h + three[2].h + 8 == 1080 and three[1].y == 20 and three[2].y == 20 + three[1].h + 8, three
cols = lc(1, 3, Rect(0, 0, 100, 50), 5, 500)
assert sum(r.w for r in cols) + 10 == 100 and cols[0].x == 0 and cols[2].x + cols[2].w == 100, cols
grid5 = lc(2, 5, Rect(0, 0, 90, 60), 0, 500)
assert [r.w for r in grid5[3:]] == [45, 45] and grid5[0].w == 30, grid5
# layout_apply
calls.clear()
ns["layout_apply"]([7, 9], 0, Rect(0, 0, 200, 100), 0, 500)
assert calls == [("resize", 7, 100, 100), ("move", 7, 0, 0), ("resize", 9, 100, 100), ("move", 9, 100, 0)], calls
# grab_rect
G, gr = ns["Grab"], ns["grab_rect"]
mv = G(1, 3, 50, 50, 100, 200, 400, 300, 0)
assert gr(mv, 15, -10) == Rect(115, 190, 400, 300)
rr = G(2, 3, 0, 0, 100, 200, 400, 300, 8 + 2)   # prawa + dolna
assert gr(rr, 20, 30) == Rect(100, 200, 420, 330)
rl = G(2, 3, 0, 0, 100, 200, 400, 300, 4 + 1)   # lewa + gorna
assert gr(rl, 20, 30) == Rect(120, 230, 380, 270), gr(rl, 20, 30)
small = gr(rl, 1000, 1000)
assert small.w == 80 and small.h == 80 and small.x == 100 + 400 - 80 and small.y == 200 + 300 - 80
print(f"OK: {n_checked} ukladow, wartosci referencyjne i grab_rect")
