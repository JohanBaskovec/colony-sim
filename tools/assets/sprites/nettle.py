"""Ortie / stinging nettle (Urtica dioica) sprites, 3 growth stages, 128x128 RGBA.

Usage: python3 nettle.py [output_dir]
Needs only the standard library + `rsvg-convert` (librsvg) to rasterise the SVGs.
Writes nettle_N.svg, nettle_N.png (128x128) and nettle_N_x4.png (512x512).

Stage 0: young shoots (the tender tops you pick to eat).  Stage 1: leafy stems.
Stage 2: tall stems with hanging strings of greenish flowers.
"""
import math, os, random, subprocess, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "out"

# ---- palette / global knobs -------------------------------------------------
OUTLINE = "#172615"
DARK = (40, 74, 36)          # nettles are a deep, matt green
MID = (64, 104, 48)
LIGHT = (96, 140, 62)
HILITE = (146, 182, 98)
STEM = (92, 116, 56)
STEM_BASE = (112, 66, 58)    # purplish foot of the stems
FLOWER = (142, 152, 92)
FLOWER_L = (192, 192, 132)
BASE = (64.0, 115.0)
OUT_W = 1.7


def lerp(a, b, t):
    return a + (b - a) * t


def mix(c1, c2, t):
    return tuple(lerp(a, b, t) for a, b in zip(c1, c2))


def hx(c):
    return "#%02x%02x%02x" % tuple(max(0, min(255, round(v))) for v in c)


def rot(v, a):
    c, s = math.cos(a), math.sin(a)
    return (v[0] * c - v[1] * s, v[0] * s + v[1] * c)


def bez(p0, p1, p2, t):
    """Point and unit tangent on a quadratic Bezier."""
    u = 1 - t
    x = u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0]
    y = u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]
    dx = 2 * u * (p1[0] - p0[0]) + 2 * t * (p2[0] - p1[0])
    dy = 2 * u * (p1[1] - p0[1]) + 2 * t * (p2[1] - p1[1])
    n = math.hypot(dx, dy) or 1
    return (x, y), (dx / n, dy / n)


def poly(pts, close=False):
    return "M" + " L".join("%.2f,%.2f" % p for p in pts) + ("Z" if close else "")


def leaf(p, d, size, shade, rng, lit_left=True, curl=0.0):
    """Heart-shaped, coarsely toothed nettle leaf on a short petiole.
    p: node, d: direction, size: blade length.  Returns an SVG fragment."""
    pet = 0.22 * size
    b0 = (p[0] + d[0] * pet, p[1] + d[1] * pet)
    W = 0.27 * size
    teeth = 8
    d = rot(d, curl * 0.3)

    def at(t, hw):
        # blade axis bends slightly (curl) towards the tip
        dd = rot(d, curl * t)
        n = (-dd[1], dd[0])
        x = t - 0.07 * math.sin(math.pi * min(1, t / 0.3))  # cordate base: lobes swing back
        return (b0[0] + dd[0] * size * x + n[0] * hw, b0[1] + dd[1] * size * x + n[1] * hw)

    def prof(t):
        return W * math.sin(math.pi * t ** 0.58) ** 0.8

    sides = {}
    for side in (-1, 1):
        pts = [at(0.0, 0.0)]
        for k in range(teeth):
            tv = (k + 0.15) / (teeth + 0.6)
            tp = (k + 0.75) / (teeth + 0.6)
            j = rng.uniform(0.95, 1.05)
            pts.append(at(tv, side * prof(tv) * 0.84 * j))
            pts.append(at(tp, side * prof(tp) * 1.04 * j))
        pts.append(at(1.0, 0.0))
        sides[side] = pts
    mid = [at(t / 10, 0.0) for t in range(11)]
    outline = sides[-1] + sides[1][::-1][1:]
    fill = mix(DARK, LIGHT, shade)
    shd = mix(fill, DARK, 0.45)
    hl = mix(MID, HILITE, shade)
    veins = []
    for tv in (0.12, 0.3, 0.5, 0.68):
        for side in (-1, 1):
            a = at(tv, 0.0)
            e = at(tv + 0.17, side * prof(tv + 0.17) * 0.75)
            veins.append("M%.2f,%.2f L%.2f,%.2f" % (*a, *e))
    s = '<g stroke-linejoin="round" stroke-linecap="round">'
    s += f'<path d="M{p[0]:.2f},{p[1]:.2f} L{b0[0]:.2f},{b0[1]:.2f}" stroke="{OUTLINE}" stroke-width="{1.0 + OUT_W}"/>'
    s += f'<path d="{poly(outline, True)}" fill="{OUTLINE}" stroke="{OUTLINE}" stroke-width="{OUT_W}"/>'
    s += f'<path d="M{p[0]:.2f},{p[1]:.2f} L{b0[0]:.2f},{b0[1]:.2f}" stroke="{hx(mix(STEM, fill, 0.5))}" stroke-width="1.0"/>'
    ll, rr = (sides[-1], sides[1]) if lit_left else (sides[1], sides[-1])
    s += f'<path d="{poly(ll + mid[::-1], True)}" fill="{hx(fill)}"/>'
    s += f'<path d="{poly(rr + mid[::-1], True)}" fill="{hx(shd)}"/>'
    s += f'<path d="{" ".join(veins)}" fill="none" stroke="{hx(hl)}" stroke-width="0.45" opacity="0.7"/>'
    s += f'<path d="{poly(mid[:9])}" fill="none" stroke="{hx(hl)}" stroke-width="0.65" opacity="0.9"/>'
    s += "</g>"
    return s


def catkins(p, side, length, shade, rng):
    """Two or three hanging strings of tiny bead-like flowers from a leaf axil."""
    beads_o, beads_f, beads_h, stalks = "", "", "", ""
    for k in range(rng.choice((2, 3))):
        L = length * rng.uniform(0.7, 1.05)
        spread = side * rng.uniform(1.5, 5.0)
        p1 = (p[0] + spread, p[1] + L * 0.15)
        p2 = (p[0] + spread * 1.2, p[1] + L)
        pts = [bez(p, p1, p2, t / 14)[0] for t in range(15)]
        stalks += poly(pts) + " "
        for q in pts[2:]:
            x, y = q[0] + rng.uniform(-0.5, 0.5), q[1]
            r = rng.uniform(0.75, 1.0)
            beads_o += f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{r + 0.85:.2f}"/>'
            beads_f += f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{r:.2f}"/>'
            beads_h += f'<circle cx="{x - 0.3:.2f}" cy="{y - 0.3:.2f}" r="{r * 0.45:.2f}"/>'
    fl = mix(mix(FLOWER, DARK, 0.35), FLOWER, shade)
    return (f'<path d="{stalks}" fill="none" stroke="{OUTLINE}" stroke-width="1.6" stroke-linecap="round"/>'
            f'<g fill="{OUTLINE}">{beads_o}</g><g fill="{hx(fl)}">{beads_f}</g>'
            f'<g fill="{hx(mix(fl, FLOWER_L, 0.4 + 0.5 * shade))}">{beads_h}</g>')


def stem(bx, angle, height, nodes, leaf_size, shade, rng, flowers=False):
    """Upright stem with opposite leaf pairs (one pair side-on, the next one
    foreshortened), a tuft of young leaves at the tip, optional flower strings."""
    a = math.radians(angle)
    p0 = (BASE[0] + bx, BASE[1])
    p1 = (p0[0] + math.sin(a * 0.4) * height * 0.5, p0[1] - math.cos(a * 0.4) * height * 0.5)
    p2 = (p0[0] + math.sin(a) * height, p0[1] - math.cos(a) * height)
    pts = [bez(p0, p1, p2, t / 24)[0] for t in range(25)]
    st = mix(mix(STEM, DARK, 0.4), STEM, shade)
    s = '<g fill="none" stroke-linecap="round" stroke-linejoin="round">'
    s += f'<path d="{poly(pts)}" stroke="{OUTLINE}" stroke-width="{1.7 + OUT_W}"/>'
    s += f'<path d="{poly(pts)}" stroke="{hx(st)}" stroke-width="1.7"/>'
    s += f'<path d="{poly(pts[:5])}" stroke="{hx(mix(STEM_BASE, st, 0.35))}" stroke-width="1.7"/>'
    s += "</g>"
    flw, lvs = "", ""
    for i in range(nodes):
        u = i / max(1, nodes - 1)
        t = lerp(0.2, 0.86, u ** 0.9)
        (x, y), tg = bez(p0, p1, p2, t)
        sz = leaf_size * (1 - 0.42 * u) * rng.uniform(0.92, 1.06)
        front = i % 2 == 1          # decussate: every other pair is seen end-on
        for side in (-1, 1):
            if front:
                ang = side * lerp(52, 34, u) + rng.uniform(-6, 6)
                sz2 = sz * 0.7
            else:
                ang = side * lerp(100, 62, u) + rng.uniform(-6, 6)
                sz2 = sz
            d = rot(tg, math.radians(ang))
            lvs += leaf((x, y), d, sz2, shade * (0.85 if front else 1.0), rng,
                        lit_left=side < 0, curl=-side * math.radians(10))
            if flowers and 0.35 < u < 0.95:
                flw += catkins((x + side * 1.0, y + 1.0), side, sz * 0.75, shade, rng)
    # tip tuft of young leaves
    (xt, yt), tgt = bez(p0, p1, p2, 1.0)
    for ang in (-28, 28, 0):
        d = rot(tgt, math.radians(ang + rng.uniform(-5, 5)))
        lvs += leaf((xt, yt), d, leaf_size * (0.42 if ang else 0.48), min(1, shade + 0.15), rng,
                    lit_left=ang <= 0, curl=0)
    return s + lvs + flw


def shadow(rx, ry):
    return f'<ellipse cx="{BASE[0]}" cy="{BASE[1] + 2}" rx="{rx}" ry="{ry}" fill="#000" opacity="0.2"/>'


def svg(body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="128" height="128">'
            + body + "</svg>")


def build(items, seed, flowers=False):
    """items are drawn back to front: (foot_x, angle, height, nodes, leaf_size, shade)"""
    rng = random.Random(seed)
    return "".join(stem(*it, rng, flowers=flowers) for it in items)


STAGE0 = [
    (-3, -18, 20, 2, 12, 0.35),
    (4, 20, 18, 2, 12, 0.5),
    (0, 0, 25, 2, 13, 0.85),
]
STAGE1 = [
    (-6, -10, 56, 4, 17, 0.2),
    (7, 12, 52, 4, 17, 0.3),
    (-10, -26, 36, 3, 15, 0.5),
    (11, 28, 34, 3, 15, 0.6),
    (0, 2, 64, 4, 18, 0.85),
]
STAGE2 = [
    (-7, -8, 92, 5, 20, 0.15),
    (8, 10, 88, 5, 20, 0.25),
    (-15, -22, 66, 4, 18, 0.4),
    (16, 24, 62, 4, 18, 0.45),
    (-2, -2, 100, 6, 20, 0.7),
    (4, 8, 54, 3, 18, 0.9),
]

SPRITES = (
    ("nettle_0", shadow(16, 4) + build(STAGE0, 5)),
    ("nettle_1", shadow(30, 6) + build(STAGE1, 10)),
    ("nettle_2", shadow(38, 7.5) + build(STAGE2, 16, flowers=True)),
)

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for name, body in SPRITES:
        p = os.path.join(OUT, name + ".svg")
        with open(p, "w") as f:
            f.write(svg(body))
        for size, suffix in ((128, ""), (512, "_x4")):
            subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), p,
                            "-o", os.path.join(OUT, f"{name}{suffix}.png")], check=True)
