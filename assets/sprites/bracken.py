"""Bracken fern (Pteridium aquilinum) sprites, 3 growth stages, 128x128 RGBA.

Usage: python3 bracken.py [output_dir]
Needs only the standard library + `rsvg-convert` (librsvg) to rasterise the SVGs.
Writes bracken_N.svg, bracken_N.png (128x128) and bracken_N_x4.png (512x512).
"""
import math, os, random, subprocess, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "out"

# ---- palette / global knobs -------------------------------------------------
OUTLINE = "#1f311a"
DARK = (56, 86, 40)
MID = (86, 126, 54)
LIGHT = (108, 152, 64)
HILITE = (156, 190, 96)
STIPE = (150, 152, 84)       # straw-green stalk
STIPE_BASE = (112, 82, 48)   # brown foot of the stalk
BASE = (64.0, 115.0)         # where the plant meets the ground
OUT_W = 1.7                  # outline thickness (total extra stroke width)
PIN_W = 1.9                  # thickness of one pinnule (small rounded lobe)


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


def poly(pts):
    return "M" + " L".join("%.2f,%.2f" % p for p in pts)


def segs(ss):
    return " ".join("M%.2f,%.2f L%.2f,%.2f" % s for s in ss)


def pinna(p, d, L, bend, pmax, rng):
    """One side branch of the frond: an axis carrying two rows of pinnules.
    Returns (axis polyline, pinnule segments)."""
    n = max(2, round(L / 2.3))
    pos, pts, out = p, [p], []
    for j in range(1, n + 1):
        dr = rot(d, bend * j / n)
        pos = (pos[0] + dr[0] * L / n, pos[1] + dr[1] * L / n)
        pts.append(pos)
        s = j / n
        pl = pmax * (1 - 0.8 * s) * rng.uniform(0.9, 1.1)
        if j < n:
            for side in (-1, 1):
                dd = rot(dr, side * math.radians(68))
                out.append((pos[0], pos[1], pos[0] + dd[0] * pl, pos[1] + dd[1] * pl))
    return pts, out


def frond(bx, angle, length, width, shade, rng, droop=0.12, t0=0.42, upright=0.25):
    """A bracken frond: long bare stipe, then a broad triangular blade.
    bx: x offset of the foot, angle: lean in degrees from vertical (negative = left),
    width: length of the lowest (longest) pinnae, shade: 0 dark/back .. 1 light/front."""
    a = math.radians(angle)
    d = (math.sin(a), -math.cos(a))
    p0 = (BASE[0] + bx, BASE[1])
    up = (math.sin(a * upright), -math.cos(a * upright))
    p1 = (p0[0] + up[0] * length * 0.62, p0[1] + up[1] * length * 0.62)
    p2 = (p0[0] + d[0] * length, p0[1] + d[1] * length + droop * length * abs(math.sin(a)) + droop * length * 0.25)
    blade = length * (1 - t0)
    spacing = max(3.2, width * 0.31)
    pairs = max(3, round(blade / spacing))
    lit_ax, lit_pn, dim_ax, dim_pn = [], [], [], []
    for i in range(pairs):
        u = i / pairs
        (x, y), tg = bez(p0, p1, p2, lerp(t0, 1.0, u))
        L = width * (1 - 0.82 * u) * rng.uniform(0.94, 1.06)
        pmax = spacing * 0.5 * (1 - 0.55 * u)
        for side in (-1, 1):
            ang = side * math.radians(lerp(66, 50, u) + rng.uniform(-3, 3))
            dd = rot(tg, ang)
            # pinnae curve gently towards the frond tip
            ax, pn = pinna((x, y), dd, L, -side * math.radians(14), pmax, rng)
            lit = dd[1] < 0.15  # pinnae pointing up catch the light, the others are in shade
            (lit_ax if lit else dim_ax).append(ax)
            (lit_pn if lit else dim_pn).extend(pn)
    rachis = [bez(p0, p1, p2, t / 30)[0] for t in range(31)]
    stipe = [bez(p0, p1, p2, t0 * t / 12)[0] for t in range(13)]
    foot = [bez(p0, p1, p2, 0.16 * t / 6)[0] for t in range(7)]
    fill = mix(DARK, LIGHT, shade)
    dim = mix(fill, DARK, 0.45)
    hl = mix(MID, HILITE, shade)
    st = mix(mix(STIPE, DARK, 0.45), STIPE, shade)
    all_ax = " ".join(poly(a_) for a_ in lit_ax + dim_ax)
    all_pn = segs(lit_pn + dim_pn)
    s = '<g fill="none" stroke-linecap="round" stroke-linejoin="round">'
    s += f'<path d="{all_pn} {all_ax}" stroke="{OUTLINE}" stroke-width="{PIN_W + OUT_W}"/>'
    s += f'<path d="{poly(rachis)}" stroke="{OUTLINE}" stroke-width="{1.5 + OUT_W}"/>'
    s += f'<path d="{poly(stipe)}" stroke="{OUTLINE}" stroke-width="{2.1 + OUT_W}"/>'
    if dim_pn:
        s += f'<path d="{segs(dim_pn)} {" ".join(poly(a_) for a_ in dim_ax)}" stroke="{hx(dim)}" stroke-width="{PIN_W}"/>'
    if lit_pn:
        s += f'<path d="{segs(lit_pn)} {" ".join(poly(a_) for a_ in lit_ax)}" stroke="{hx(fill)}" stroke-width="{PIN_W}"/>'
    s += f'<path d="{all_ax}" stroke="{hx(hl)}" stroke-width="0.6" opacity="0.75"/>'
    s += f'<path d="{poly(rachis)}" stroke="{hx(mix(st, hl, 0.5))}" stroke-width="1.3"/>'
    s += f'<path d="{poly(stipe)}" stroke="{hx(st)}" stroke-width="2.1"/>'
    s += f'<path d="{poly(foot)}" stroke="{hx(mix(STIPE_BASE, st, 0.25))}" stroke-width="2.1"/>'
    s += "</g>"
    return s


def fiddlehead(bx, angle, height, shade, rng, r=4.7, turns=1.3):
    """A young crosier: a stalk ending in a tight spiral."""
    a = math.radians(angle)
    p0 = (BASE[0] + bx, BASE[1])
    top = (p0[0] + math.sin(a) * height, p0[1] - math.cos(a) * height)
    p1 = (p0[0] + math.sin(a * 0.2) * height * 0.6, p0[1] - height * 0.6)
    stalk = [bez(p0, p1, top, t / 14)[0] for t in range(15)]
    _, tg = bez(p0, p1, top, 1.0)
    sgn = 1 if angle >= 0 else -1          # curl towards the side it leans to
    nrm = (-tg[1] * sgn, tg[0] * sgn)
    c = (top[0] + nrm[0] * r, top[1] + nrm[1] * r)
    a0 = math.atan2(top[1] - c[1], top[0] - c[0])
    sp = []
    for k in range(1, 33):
        f = k / 32
        ang = a0 + sgn * f * turns * 2 * math.pi
        rr = r * (1 - 0.7 * f)
        sp.append((c[0] + math.cos(ang) * rr, c[1] + math.sin(ang) * rr))
    path = poly(stalk + sp)
    col = mix(mix(STIPE, DARK, 0.35), mix(STIPE, HILITE, 0.4), shade)
    foot = poly(stalk[:4])
    s = '<g fill="none" stroke-linecap="round" stroke-linejoin="round">'
    s += f'<path d="{path}" stroke="{OUTLINE}" stroke-width="{2.0 + OUT_W}"/>'
    s += f'<path d="{path}" stroke="{hx(col)}" stroke-width="2.0"/>'
    s += f'<path d="{poly(sp[8:])}" stroke="{hx(mix(col, LIGHT, 0.6))}" stroke-width="2.0"/>'
    s += f'<path d="{foot}" stroke="{hx(mix(STIPE_BASE, col, 0.25))}" stroke-width="2.0"/>'
    s += "</g>"
    return s


def shadow(rx, ry):
    return f'<ellipse cx="{BASE[0]}" cy="{BASE[1] + 2}" rx="{rx}" ry="{ry}" fill="#000" opacity="0.2"/>'


def svg(body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="128" height="128">'
            + body + "</svg>")


def build(items, seed):
    """items are drawn back to front.
    ("F", foot_x, angle, length, width, shade[, droop]) = frond
    ("C", foot_x, angle, height, shade)                 = crosier (fiddlehead)"""
    rng = random.Random(seed)
    out = ""
    for it in items:
        if it[0] == "F":
            out += frond(*it[1:], rng) if len(it) == 6 else frond(*it[1:6], rng, droop=it[6])
        else:
            out += fiddlehead(*it[1:], rng)
    return out


STAGE0 = [
    ("F", -3, -30, 58, 12, 0.25),
    ("F", 4, 33, 54, 12, 0.35),
    ("C", -1, -6, 36, 0.5),
    ("F", -8, -58, 36, 10, 0.7, 0.2),
    ("F", 8, 60, 34, 10, 0.8, 0.2),
    ("C", 5, 14, 21, 0.9),
]
STAGE1 = [
    ("F", -2, -14, 85, 16, 0.15),
    ("F", 5, 22, 80, 16, 0.25),
    ("F", -7, -44, 66, 15, 0.45),
    ("F", 9, 47, 64, 15, 0.5),
    ("C", 2, 8, 44, 0.6),
    ("F", -11, -66, 47, 13, 0.75, 0.2),
    ("F", 12, 68, 45, 13, 0.85, 0.2),
    ("F", -2, -8, 44, 13, 1.0, 0.45),
]
STAGE2 = [
    ("F", -3, -10, 107, 19, 0.1),
    ("F", 6, 16, 102, 19, 0.2),
    ("F", -8, -31, 86, 18, 0.35),
    ("F", 10, 33, 84, 18, 0.4),
    ("F", -12, -52, 62, 16, 0.55, 0.16),
    ("F", 13, 53, 60, 16, 0.6, 0.16),
    ("F", -14, -72, 42, 13.5, 0.8, 0.2),
    ("F", 15, 73, 41, 13.5, 0.85, 0.2),
    ("F", 1, 6, 56, 15, 1.0, 0.5),
]

SPRITES = (
    ("bracken_0", shadow(30, 6) + build(STAGE0, 4)),
    ("bracken_1", shadow(46, 8) + build(STAGE1, 9)),
    ("bracken_2", shadow(52, 9.5) + build(STAGE2, 15)),
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
