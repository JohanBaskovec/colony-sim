"""Roseau commun / common reed (Phragmites australis) sprites, 3 growth stages, 128x128 RGBA.

Usage: python3 common_reed.py [output_dir]
Needs only the standard library + `rsvg-convert` (librsvg) to rasterise the SVGs.
Writes common_reed_N.svg, common_reed_N.png (128x128) and common_reed_N_x4.png (512x512).

Stage 0: young shoots.  Stage 1: tall leafy canes.  Stage 2: canes topped with
drooping purple-brown feathery plumes (panicles).
"""
import math, os, random, subprocess, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "out"

# ---- palette / global knobs -------------------------------------------------
OUTLINE = "#1c2b1a"
DARK = (58, 92, 56)
MID = (92, 134, 70)
LIGHT = (126, 166, 84)
HILITE = (172, 202, 120)
CANE = (156, 160, 96)        # straw-green culm
NODE = (96, 88, 52)
PLUME_D = (92, 62, 74)       # panicle: purple-brown ...
PLUME_M = (138, 100, 104)
PLUME_L = (196, 170, 150)    # ... with silvery-beige silky hairs
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


def leaf(p, d, L, W, droop, rng):
    """Lanceolate grass leaf from p, leaving along d, arching down by `droop`.
    Returns (outline polygon pts, midrib pts)."""
    p1 = (p[0] + d[0] * L * 0.55, p[1] + d[1] * L * 0.55)
    tip_y = p[1] + d[1] * L * 0.9 + droop * L
    p2 = (p[0] + d[0] * L * 0.9, min(tip_y, BASE[1] - 1))   # never hang below the ground
    n = 16
    C, Lf, R = [], [], []
    for k in range(n + 1):
        s = k / n
        (x, y), tg = bez(p, p1, p2, s)
        w = W * min(1, 0.5 + s * 4) * (1 - s) ** 1.05 * rng.uniform(0.95, 1.05)
        nx, ny = -tg[1], tg[0]
        C.append((x, y))
        Lf.append((x - nx * w, y - ny * w))
        R.append((x + nx * w, y + ny * w))
    return Lf + R[::-1], C


def plume(top, tg, side, Lp, shade, rng):
    """Nodding, silky panicle continuing the cane past `top` and arching to `side`."""
    p1 = (top[0] + tg[0] * Lp * 0.62, top[1] + tg[1] * Lp * 0.62)
    p2 = (top[0] + tg[0] * Lp * 0.7 + side * Lp * 0.5, top[1] + tg[1] * Lp * 0.7 + Lp * 0.18)
    axis = [bez(top, p1, p2, t / 20)[0] for t in range(21)]
    br = []
    nb = 22
    for k in range(nb):
        s = 0.04 + 0.96 * k / (nb - 1)
        (x, y), t_ = bez(top, p1, p2, s)
        bl = (Lp * 0.24 * math.sin(math.pi * min(1, s * 1.1) ** 0.7) + 1.5) * rng.uniform(0.8, 1.15)
        sd = 1 if k % 2 else -1
        d = rot(t_, sd * math.radians(rng.uniform(14, 30)))
        q1 = (x + d[0] * bl * 0.55, y + d[1] * bl * 0.55)
        q2 = (x + d[0] * bl + side * bl * 0.15, y + d[1] * bl + bl * 0.45)   # hairs sag under their weight
        br.append((x, y, q1, q2))
    bpath = " ".join("M%.2f,%.2f Q%.2f,%.2f %.2f,%.2f" % (x, y, *q1, *q2) for x, y, q1, q2 in br)
    tips = " ".join("M%.2f,%.2f Q%.2f,%.2f %.2f,%.2f" % (lerp(x, q1[0], 0.4), lerp(y, q1[1], 0.4), *q1, *q2)
                    for x, y, q1, q2 in br)
    ax = poly(axis)
    s = '<g fill="none" stroke-linecap="round" stroke-linejoin="round">'
    s += f'<path d="{bpath} {ax}" stroke="{OUTLINE}" stroke-width="{1.7 + OUT_W}"/>'
    s += f'<path d="{bpath} {ax}" stroke="{hx(mix(PLUME_D, PLUME_M, shade))}" stroke-width="1.7"/>'
    s += f'<path d="{tips}" stroke="{hx(mix(PLUME_M, PLUME_L, 0.35 + 0.45 * shade))}" stroke-width="1.15"/>'
    s += f'<path d="{tips}" stroke="{hx(mix(PLUME_L, (242, 234, 220), shade * 0.6))}" stroke-width="0.5" opacity="0.9" transform="translate(-0.3,-0.35)"/>'
    s += "</g>"
    return s


def cane(bx, angle, height, nleaves, leaf_len, leaf_w, shade, rng, plume_len=0, side0=1):
    """One reed stem: straight cane with nodes, alternate arching leaves,
    either a rolled young leaf or a plume at the top."""
    a = math.radians(angle)
    p0 = (BASE[0] + bx, BASE[1])
    p1 = (p0[0] + math.sin(a * 0.5) * height * 0.5, p0[1] - math.cos(a * 0.5) * height * 0.5)
    p2 = (p0[0] + math.sin(a) * height, p0[1] - math.cos(a) * height)
    stem = [bez(p0, p1, p2, t / 24)[0] for t in range(25)]
    fill = mix(DARK, LIGHT, shade)
    under = mix(mix(fill, HILITE, 0.25), (150, 160, 140), 0.25)   # grey-green leaf underside
    hl = mix(MID, HILITE, shade)
    cn = mix(mix(CANE, DARK, 0.45), CANE, shade)
    leaves, nodes = [], []
    side = side0
    for i in range(nleaves):
        u = i / max(1, nleaves - 1)
        t = lerp(0.16, 0.86 if plume_len else 0.8, u ** 0.85)
        (x, y), tg = bez(p0, p1, p2, t)
        nodes.append((x, y, tg))
        L = leaf_len * (1.05 - 0.35 * u) * rng.uniform(0.9, 1.1)
        d = rot(tg, side * math.radians(lerp(58, 34, u) + rng.uniform(-6, 6)))
        leaves.append((leaf((x, y), d, L, leaf_w, lerp(0.75, 0.35, u) * rng.uniform(0.8, 1.2), rng), d[1] > -0.35))
        side = -side
    s = '<g stroke-linecap="round" stroke-linejoin="round">'
    # top: young rolled leaf (spike) or plume, drawn behind the leaves
    _, tgt = bez(p0, p1, p2, 1.0)
    top_leaf = None
    if not plume_len:
        top_leaf = leaf(p2, tgt, leaf_len * 0.55, leaf_w * 0.55, 0.05, rng)
    sp = poly(stem)
    s += f'<path d="{sp}" fill="none" stroke="{OUTLINE}" stroke-width="{1.6 + OUT_W}"/>'
    s += f'<path d="{sp}" fill="none" stroke="{hx(cn)}" stroke-width="1.6"/>'
    s += f'<path d="{poly(stem[2:-2])}" fill="none" stroke="{hx(mix(cn, HILITE, 0.5))}" stroke-width="0.5" transform="translate(-0.4,0)"/>'
    for x, y, tg in nodes:
        nx, ny = -tg[1] * 1.3, tg[0] * 1.3
        s += f'<path d="M{x - nx:.2f},{y - ny:.2f} L{x + nx:.2f},{y + ny:.2f}" stroke="{hx(NODE)}" stroke-width="0.8"/>'
    if plume_len:
        s += plume(p2, tgt, 1 if angle >= 0 else -1, plume_len, shade, rng)
    for (pts, mid), drooping in leaves + ([(top_leaf, False)] if top_leaf else []):
        s += f'<path d="{poly(pts, True)}" fill="{OUTLINE}" stroke="{OUTLINE}" stroke-width="{OUT_W}"/>'
        s += f'<path d="{poly(pts, True)}" fill="{hx(fill)}"/>'
        if drooping:   # the far, hanging half of the leaf shows its paler underside
            k = len(mid) // 2
            n = len(mid)
            Lf, R = pts[:n], pts[n:][::-1]
            seg = Lf[k:] + R[k:][::-1]
            s += f'<path d="{poly(seg, True)}" fill="{hx(under)}"/>'
        s += f'<path d="{poly(mid[1:-2])}" fill="none" stroke="{hx(hl)}" stroke-width="0.6" opacity="0.85"/>'
    s += "</g>"
    return s


def shadow(rx, ry):
    return f'<ellipse cx="{BASE[0]}" cy="{BASE[1] + 2}" rx="{rx}" ry="{ry}" fill="#000" opacity="0.2"/>'


def svg(body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="128" height="128">'
            + body + "</svg>")


def build(items, seed):
    """items are drawn back to front:
    (foot_x, angle, height, n_leaves, leaf_len, leaf_w, shade, plume_len, first_side)"""
    rng = random.Random(seed)
    return "".join(cane(bx, a, h, n, ll, lw, sh, rng, plume_len=pl, side0=sd)
                   for (bx, a, h, n, ll, lw, sh, pl, sd) in items)


STAGE0 = [
    (-2, -6, 28, 3, 19, 1.6, 0.3, 0, 1),
    (3, 9, 24, 3, 18, 1.6, 0.45, 0, -1),
    (-5, -20, 19, 2, 16, 1.5, 0.7, 0, -1),
    (5, 22, 17, 2, 15, 1.5, 0.8, 0, 1),
    (0, 2, 14, 2, 13, 1.5, 1.0, 0, 1),
]
STAGE1 = [
    (-3, -4, 84, 5, 33, 2.0, 0.15, 0, 1),
    (4, 6, 78, 5, 32, 2.0, 0.25, 0, -1),
    (-8, -14, 68, 4, 30, 1.9, 0.4, 0, -1),
    (9, 16, 64, 4, 30, 1.9, 0.5, 0, 1),
    (0, 1, 56, 4, 28, 1.9, 0.7, 0, 1),
    (-6, -24, 40, 3, 24, 1.8, 0.85, 0, 1),
    (7, 26, 36, 3, 23, 1.8, 0.95, 0, -1),
]
STAGE2 = [
    (-4, -5, 90, 6, 36, 2.2, 0.12, 26, 1),
    (5, 7, 86, 6, 36, 2.2, 0.2, 25, -1),
    (-10, -15, 78, 5, 34, 2.1, 0.32, 23, -1),
    (11, 17, 74, 5, 34, 2.1, 0.4, 22, 1),
    (1, 2, 82, 6, 35, 2.2, 0.55, 24, 1),
    (-7, -24, 60, 4, 30, 2.0, 0.65, 0, 1),
    (8, 28, 56, 4, 30, 2.0, 0.75, 0, -1),
    (-2, -9, 48, 4, 28, 2.0, 0.9, 0, -1),
    (3, 13, 42, 3, 26, 1.9, 1.0, 0, 1),
]

SPRITES = (
    ("common_reed_0", shadow(17, 4) + build(STAGE0, 2)),
    ("common_reed_1", shadow(32, 6) + build(STAGE1, 7)),
    ("common_reed_2", shadow(40, 7.5) + build(STAGE2, 12)),
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
