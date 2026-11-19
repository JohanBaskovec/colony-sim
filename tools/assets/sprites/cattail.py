"""Massette / cattail (Typha latifolia) sprites, 3 growth stages, 128x128 RGBA.

Usage: python3 cattail.py [output_dir]
Needs only the standard library + `rsvg-convert` (librsvg) to rasterise the SVGs.
Writes cattail_N.svg, cattail_N.png (128x128) and cattail_N_x4.png (512x512).

Stage 0: young shoots.  Stage 1: young leafy clump, no spike yet.
Stage 2: tall leaves, one flowering spike (green female spike, yellow male spike).
Stage 3: full-size clump, spikes thickening and turning olive-brown.
Stage 4: mature clump with brown "cigars".
"""
import math, os, random, subprocess, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "out"

# ---- palette / global knobs -------------------------------------------------
OUTLINE = "#1c2c1c"
DARK = (54, 88, 60)          # Typha leaves are a slightly blue-grey green
MID = (88, 128, 78)
LIGHT = (122, 160, 94)
HILITE = (170, 198, 124)
STEM = (128, 140, 82)
FOOT = (150, 132, 92)        # pale sheathing leaf bases
SPIKE_D = (78, 44, 26)       # ripe female spike (the "cigar")
SPIKE_M = (118, 70, 38)
SPIKE_L = (156, 102, 58)
POLLEN = (214, 186, 88)      # flowering male spike
BASE = (64.0, 115.0)
OUT_W = 1.7


def lerp(a, b, t):
    return a + (b - a) * t


def mix(c1, c2, t):
    return tuple(lerp(a, b, t) for a, b in zip(c1, c2))


def hx(c):
    return "#%02x%02x%02x" % tuple(max(0, min(255, round(v))) for v in c)


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


def blade(bx, angle, length, width, shade, rng, bend=0.0, twist=None, lit_left=True):
    """One strap-shaped leaf.  angle: lean in degrees from vertical (neg = left),
    bend: how much the tip flops over (fraction of length), twist: position (0..1)
    where the ribbon turns over, swapping its lit and shaded halves."""
    a = math.radians(angle)
    p0 = (BASE[0] + bx, BASE[1])
    up = (math.sin(a * 0.3), -math.cos(a * 0.3))
    p1 = (p0[0] + up[0] * length * 0.62, p0[1] + up[1] * length * 0.62)
    p2 = (p0[0] + math.sin(a) * length, p0[1] - math.cos(a) * length + bend * length)
    n = 26
    C, L, R = [], [], []
    for k in range(n + 1):
        s = k / n
        (x, y), tg = bez(p0, p1, p2, s)
        w = width * (1 - s ** 3) ** 0.85 * (0.8 + 0.2 * min(1, s / 0.12))
        if twist is not None:
            w *= 0.45 + 0.55 * abs(math.cos(math.pi * max(0, min(1, (s - twist) / 0.22))))
        w *= rng.uniform(0.96, 1.04)
        nx, ny = -tg[1], tg[0]
        C.append((x, y))
        L.append((x - nx * w, y - ny * w))
        R.append((x + nx * w, y + ny * w))
    kt = n + 1 if twist is None else int(round((twist + 0.11) * n))
    fill = mix(DARK, LIGHT, shade)
    shd = mix(fill, DARK, 0.5)
    hl = mix(MID, HILITE, shade)
    s = '<g stroke-linejoin="round" stroke-linecap="round">'
    s += f'<path d="{poly(L + R[::-1], True)}" fill="{OUTLINE}" stroke="{OUTLINE}" stroke-width="{OUT_W}"/>'
    for i0, i1, left_lit in ((0, kt, lit_left), (kt - 1, n + 1, not lit_left)):
        if i1 - i0 < 2:
            continue
        c, l, r = C[i0:i1], L[i0:i1], R[i0:i1]
        s += f'<path d="{poly(l + c[::-1], True)}" fill="{hx(fill if left_lit else shd)}"/>'
        s += f'<path d="{poly(c + r[::-1], True)}" fill="{hx(shd if left_lit else fill)}"/>'
        edge = l if left_lit else r
        s += f'<path d="{poly(edge[1:-2])}" fill="none" stroke="{hx(hl)}" stroke-width="0.6" opacity="0.8"/>'
    foot = L[:4] + R[:4][::-1]
    s += f'<path d="{poly(foot, True)}" fill="{hx(mix(FOOT, fill, 0.35))}" opacity="0.9"/>'
    s += "</g>"
    return s


def spike(bx, angle, height, shade, ripe, rng):
    """Flowering stalk: bare stem, female spike, then the thin male spike on top."""
    a = math.radians(angle)
    p0 = (BASE[0] + bx, BASE[1])
    top = (p0[0] + math.sin(a) * height, p0[1] - math.cos(a) * height)
    p1 = (p0[0] + math.sin(a * 0.3) * height * 0.55, p0[1] - height * 0.55)
    pts = [bez(p0, p1, top, t / 30)[0] for t in range(31)]
    stem = poly(pts)
    st = mix(mix(STEM, DARK, 0.4), STEM, shade)
    s = '<g fill="none" stroke-linecap="round" stroke-linejoin="round">'
    s += f'<path d="{stem}" stroke="{OUTLINE}" stroke-width="{1.4 + OUT_W}"/>'
    s += f'<path d="{stem}" stroke="{hx(st)}" stroke-width="1.4"/>'
    # ripe: 0 = flowering (green female spike, yellow pollen on the male spike),
    # 0.5 = developing (olive spike, male spike drying), 1 = ripe brown cigar
    r = max(0.0, min(1.0, float(ripe)))
    fem = pts[21:27] if r >= 0.5 else pts[22:27]
    fw = lerp(4.4, 6.2, r)
    wm = min(1.0, r * 2)                       # how withered the male spike is
    male = pts[28:31] if r >= 0.5 else pts[27:31]
    mw = lerp(2.8, 1.3, wm)
    mcol = mix(POLLEN, mix(STEM, FOOT, 0.6), wm)
    green = (mix(DARK, SPIKE_D, 0.3), mix(MID, SPIKE_M, 0.25), mix(LIGHT, HILITE, 0.4))
    brown = (SPIKE_D, SPIKE_M, SPIKE_L)
    fd, fm, fl = (mix(g, b, r ** 0.8) for g, b in zip(green, brown))
    ms = poly(male)
    s += f'<path d="{ms}" stroke="{OUTLINE}" stroke-width="{mw + OUT_W}"/>'
    s += f'<path d="{ms}" stroke="{hx(mcol)}" stroke-width="{mw}"/>'
    if r < 0.3:  # fresh pollen
        s += f'<path d="{ms}" stroke="{hx(mix(POLLEN, (255, 244, 180), 0.6))}" stroke-width="{mw * 0.35}" transform="translate(-0.5,0)"/>'
    fs = poly(fem)
    s += f'<path d="{fs}" stroke="{OUTLINE}" stroke-width="{fw + OUT_W}"/>'
    s += f'<path d="{fs}" stroke="{hx(mix(fd, fm, shade))}" stroke-width="{fw}"/>'
    s += f'<path d="{fs}" stroke="{hx(mix(fm, fl, shade))}" stroke-width="{fw * 0.55}" transform="translate(-{fw * 0.17:.2f},0)"/>'
    s += f'<path d="{poly(fem[1:-1])}" stroke="{hx(mix(fl, (230, 200, 150), 0.4))}" stroke-width="{fw * 0.16}" opacity="0.8" transform="translate(-{fw * 0.3:.2f},0)"/>'
    s += "</g>"
    return s


def shadow(rx, ry):
    return f'<ellipse cx="{BASE[0]}" cy="{BASE[1] + 2}" rx="{rx}" ry="{ry}" fill="#000" opacity="0.2"/>'


def svg(body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="128" height="128">'
            + body + "</svg>")


def build(items, seed):
    """items are drawn back to front.
    ("L", foot_x, angle, length, width, shade, bend, twist, lit_left) = leaf
    ("S", foot_x, angle, height, shade, ripe)                       = flowering spike
    ripe: 0 flowering .. 0.5 developing .. 1 ripe brown cigar"""
    rng = random.Random(seed)
    out = ""
    for it in items:
        if it[0] == "L":
            bx, ang, ln, w, sh, bend, tw, ll = it[1:]
            out += blade(bx, ang, ln, w, sh, rng, bend=bend, twist=tw, lit_left=ll)
        else:
            out += spike(*it[1:], rng)
    return out


STAGE0 = [
    ("L", -1, -6, 38, 1.9, 0.25, 0.0, None, False),
    ("L", 2, 8, 35, 1.9, 0.3, 0.02, None, True),
    ("L", -3, -20, 29, 1.9, 0.5, 0.08, None, False),
    ("L", 4, 22, 28, 1.9, 0.6, 0.1, 0.62, True),
    ("L", 0, -1, 26, 1.9, 0.85, 0.0, None, True),
    ("L", -2, -36, 19, 1.8, 0.8, 0.14, None, False),
    ("L", 3, 38, 18, 1.8, 0.9, 0.16, None, True),
]
STAGE1 = [
    ("L", -2, -5, 62, 2.3, 0.2, 0.02, None, False),
    ("L", 3, 8, 58, 2.3, 0.25, 0.03, 0.62, True),
    ("L", -4, -17, 52, 2.3, 0.35, 0.1, None, False),
    ("L", 5, 20, 50, 2.3, 0.4, 0.12, None, True),
    ("L", -5, -32, 38, 2.2, 0.55, 0.2, 0.55, False),
    ("L", 6, 34, 37, 2.2, 0.6, 0.22, None, True),
    ("L", -1, -8, 46, 2.3, 0.75, 0.05, None, True),
    ("L", 3, 12, 43, 2.3, 0.85, 0.08, 0.65, False),
    ("L", -3, -46, 27, 2.1, 0.9, 0.22, None, False),
    ("L", 4, 48, 26, 2.1, 0.95, 0.24, None, True),
    ("L", 0, -2, 30, 2.2, 1.0, 0.03, None, False),
]
STAGE2 = [
    ("L", -2, -5, 92, 2.6, 0.15, 0.03, None, False),
    ("L", 3, 7, 88, 2.6, 0.2, 0.04, 0.6, True),
    ("L", -5, -15, 80, 2.6, 0.3, 0.12, None, False),
    ("L", 6, 18, 78, 2.6, 0.35, 0.14, None, True),
    ("S", 1, 2, 74, 0.5, 0.0),
    ("L", -6, -30, 62, 2.5, 0.5, 0.22, 0.55, False),
    ("L", 7, 32, 60, 2.5, 0.55, 0.24, None, True),
    ("L", -1, -9, 70, 2.6, 0.7, 0.06, None, True),
    ("L", 3, 13, 66, 2.6, 0.8, 0.1, 0.65, False),
    ("L", -4, -44, 40, 2.4, 0.85, 0.24, None, False),
    ("L", 5, 46, 38, 2.4, 0.9, 0.26, None, True),
    ("L", 0, -2, 44, 2.5, 1.0, 0.04, None, False),
]
STAGE3 = [
    ("L", -4, -7, 102, 2.9, 0.1, 0.04, None, False),
    ("L", 4, 6, 103, 2.9, 0.12, 0.03, 0.62, True),
    ("L", -7, -17, 92, 2.8, 0.2, 0.14, None, False),
    ("L", 8, 19, 90, 2.8, 0.22, 0.16, None, True),
    ("L", -9, -30, 78, 2.8, 0.3, 0.24, 0.5, False),
    ("L", 10, 33, 75, 2.8, 0.32, 0.26, None, True),
    ("S", -3, -5, 84, 0.35, 0.5),
    ("S", 5, 7, 90, 0.5, 0.45),
    ("L", 0, -1, 90, 2.9, 0.45, 0.04, 0.55, True),
    ("L", -10, -46, 54, 2.7, 0.5, 0.3, None, False),
    ("L", 11, 48, 52, 2.7, 0.55, 0.32, None, True),
    ("L", -6, -12, 75, 2.8, 0.65, 0.08, None, True),
    ("L", 7, 15, 73, 2.8, 0.7, 0.12, 0.6, False),
    ("L", -7, -28, 56, 2.7, 0.8, 0.2, None, False),
    ("L", 8, 30, 54, 2.7, 0.85, 0.22, None, True),
    ("L", 2, 4, 48, 2.7, 1.0, 0.05, None, False),
    ("L", -2, -58, 32, 2.5, 0.95, 0.3, None, True),
    ("L", 4, 60, 30, 2.5, 0.95, 0.3, None, False),
]
STAGE4 = [
    ("L", -4, -7, 108, 3.0, 0.1, 0.04, None, False),
    ("L", 4, 6, 109, 3.0, 0.12, 0.03, 0.62, True),
    ("L", -8, -18, 98, 2.9, 0.2, 0.14, None, False),
    ("L", 9, 20, 96, 2.9, 0.22, 0.16, None, True),
    ("L", -10, -30, 84, 2.9, 0.3, 0.24, 0.5, False),
    ("L", 11, 33, 80, 2.9, 0.32, 0.26, None, True),
    ("S", -3, -5, 88, 0.35, 1.0),
    ("S", 5, 7, 95, 0.5, 1.0),
    ("L", 0, -1, 96, 3.0, 0.45, 0.04, 0.55, True),
    ("L", -11, -46, 58, 2.8, 0.5, 0.3, None, False),
    ("L", 12, 48, 56, 2.8, 0.55, 0.32, None, True),
    ("S", 1, -12, 76, 0.7, 1.0),
    ("L", -6, -12, 80, 2.9, 0.65, 0.08, None, True),
    ("L", 7, 15, 78, 2.9, 0.7, 0.12, 0.6, False),
    ("L", -8, -28, 60, 2.8, 0.8, 0.2, None, False),
    ("L", 9, 30, 58, 2.8, 0.85, 0.22, None, True),
    ("L", 2, 4, 52, 2.8, 1.0, 0.05, None, False),
    ("L", -2, -58, 34, 2.6, 0.95, 0.3, None, True),
    ("L", 4, 60, 32, 2.6, 0.95, 0.3, None, False),
]

SPRITES = (
    ("cattail_0", shadow(17, 4) + build(STAGE0, 3)),
    ("cattail_1", shadow(24, 5) + build(STAGE1, 6)),
    ("cattail_2", shadow(30, 6) + build(STAGE2, 8)),
    ("cattail_3", shadow(35, 7) + build(STAGE3, 11)),
    ("cattail_4", shadow(38, 7.5) + build(STAGE4, 13)),
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
