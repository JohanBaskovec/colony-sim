"""Herbe / grass tuft sprites (modelled on cocksfoot, Dactylis glomerata), 5 growth stages, 128x128 RGBA.

Usage: python3 grass_tuft.py [output_dir]
Needs only the standard library + `rsvg-convert` (librsvg) to rasterise the SVGs.
Writes grass_N.svg, grass_N.png (128x128) and grass_N_x4.png (512x512).

Stage 0: a few sprouts.  Stage 1: small tuft.  Stage 2: medium tuft.
Stage 3: full tuft, first flower heads (green, flushed purple).  Stage 4: tall tuft in seed,
straw-coloured seed heads and drying blade tips.
The flower heads are those of cocksfoot (Dactylis glomerata): dense clusters on short branches.
"""
import math, os, random, subprocess, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "out"

# ---- palette / global knobs -------------------------------------------------
OUTLINE = "#1b2b16"
DARK = (54, 94, 44)
MID = (86, 136, 60)
LIGHT = (124, 174, 78)
HILITE = (176, 212, 120)
STRAW = (206, 184, 106)      # dry tips, ripe seed heads
STRAW_D = (156, 130, 70)
STRAW_L = (236, 216, 148)
SPIKE_G = (148, 176, 96)     # unripe (green) flower heads
BASE = (64.0, 115.0)         # where the tuft meets the ground
OUT_W = 1.5                  # outline thickness (total extra stroke width)


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


def poly(pts, close=True):
    return "M" + " L".join("%.2f,%.2f" % p for p in pts) + ("Z" if close else "")


def blade(bx, angle, length, width, shade, rng, droop=0.0, dry=0.0):
    """One grass blade: narrow, tapering to a point, arching over at the tip.
    angle: lean in degrees from vertical (negative = left), droop: how far the tip
    falls (fraction of length), dry: how much of the tip has turned straw-coloured."""
    a = math.radians(angle)
    p0 = (BASE[0] + bx, BASE[1])
    up = (math.sin(a * 0.45), -math.cos(a * 0.45))
    p1 = (p0[0] + up[0] * length * 0.6, p0[1] + up[1] * length * 0.6)
    p2 = (p0[0] + math.sin(a) * length, p0[1] - math.cos(a) * length + droop * length)
    p2 = (p2[0], min(p2[1], BASE[1] - 2))
    n = 18
    C, L, R = [], [], []
    for k in range(n + 1):
        s = k / n
        (x, y), tg = bez(p0, p1, p2, s)
        w = width * (1 - s) ** 0.85 * (0.8 + 0.2 * min(1, s / 0.1))
        nx, ny = -tg[1], tg[0]
        C.append((x, y))
        L.append((x - nx * w, y - ny * w))
        R.append((x + nx * w, y + ny * w))
    # the side facing the light (upper left) is lit
    lit_left = math.sin(a) >= -0.15
    fill = mix(DARK, LIGHT, shade)
    shd = mix(fill, DARK, 0.5)
    hl = mix(MID, HILITE, shade)
    out = poly(L + R[::-1])
    s = f'<path d="{out}" fill="{OUTLINE}" stroke="{OUTLINE}" stroke-width="{OUT_W}"/>'
    s += f'<path d="{poly(L + C[::-1])}" fill="{hx(fill if lit_left else shd)}"/>'
    s += f'<path d="{poly(C + R[::-1])}" fill="{hx(shd if lit_left else fill)}"/>'
    if dry > 0:
        k0 = int(n * (1 - dry))
        s += f'<path d="{poly(L[k0:] + C[k0:][::-1])}" fill="{hx(STRAW_L if lit_left else STRAW)}"/>'
        s += f'<path d="{poly(C[k0:] + R[k0:][::-1])}" fill="{hx(STRAW if lit_left else STRAW_D)}"/>'
    edge = L if lit_left else R
    s += f'<path d="{poly(edge[1:-3], False)}" fill="none" stroke="{hx(hl)}" stroke-width="0.5" opacity="0.8"/>'
    return s


def seed_head(bx, angle, height, shade, ripe, rng):
    """Flowering stem of cocksfoot (Dactylis glomerata): a thin culm ending in a
    one-sided panicle of dense, rounded clusters of spikelets on short stiff branches
    (the lowest branch is the longest and stands out from the stem).
    ripe: 0 green-purple in flower .. 1 straw-coloured in seed."""
    a = math.radians(angle)
    side = 1 if angle >= 0 else -1
    p0 = (BASE[0] + bx, BASE[1])
    p1 = (p0[0] + math.sin(a * 0.4) * height * 0.6, p0[1] - height * 0.6)
    top = (p0[0] + math.sin(a) * height, p0[1] - math.cos(a) * height)
    stem = [bez(p0, p1, top, t / 30)[0] for t in range(31)]
    # where the panicle sits along the top of the stem: (t, branch length, cluster size, side)
    spots = [(0.80, 7.5, 3.4, side), (0.87, 4.2, 2.9, -side), (0.93, 2.4, 2.6, side), (1.0, 0.0, 3.0, 0)]
    branches, clusters = [], []
    for t, L, r, sd in spots:
        (x, y), tg = bez(p0, p1, top, t)
        if L:
            ang = math.atan2(tg[1], tg[0]) + sd * math.radians(rng.uniform(40, 55))
            ex, ey = x + math.cos(ang) * L, y + math.sin(ang) * L
            branches.append((x, y, ex, ey))
        else:
            ex, ey = x + tg[0] * 1.5, y + tg[1] * 1.5
        # a tight clump of small oval spikelets
        grains = []
        for _ in range(9):
            gx = ex + rng.uniform(-1, 1) * r * 0.75
            gy = ey + rng.uniform(-1, 1) * r * 0.65
            grains.append((gx, gy, rng.uniform(0, 180), rng.uniform(1.1, 1.5)))
        grains.sort(key=lambda g: g[1])
        clusters.append(grains)
    young = (122, 138, 86)                             # green, flushed with purple in flower
    purple = (128, 96, 116)
    gc = mix(mix(young, purple, 0.35), STRAW, ripe)
    gd = mix(mix(DARK, purple, 0.4), STRAW_D, ripe)
    gl = mix(mix(young, HILITE, 0.5), STRAW_L, ripe)
    st = mix(mix(MID, STRAW_D, ripe), mix(LIGHT, STRAW, ripe), shade)
    sp = poly(stem, False)
    br = " ".join("M%.2f,%.2f L%.2f,%.2f" % b for b in branches)

    def ell(g, extra=0.0):
        gx, gy, deg, L = g
        return (f'<ellipse cx="{gx:.2f}" cy="{gy:.2f}" rx="{L + extra:.2f}" ry="{L * 0.68 + extra:.2f}" '
                f'transform="rotate({deg:.1f} {gx:.2f} {gy:.2f})"')
    s = f'<path d="{sp} {br}" fill="none" stroke="{OUTLINE}" stroke-width="{0.9 + OUT_W}"/>'
    s += "".join(ell(g, OUT_W * 0.5) + f' fill="{OUTLINE}"/>' for cl in clusters for g in cl)
    s += f'<path d="{sp} {br}" fill="none" stroke="{hx(st)}" stroke-width="0.9"/>'
    for cl in clusters:
        for i, g in enumerate(cl):
            s += ell(g) + f' fill="{hx(gd if i < 3 else gc)}"/>'
        for g in cl[:4]:                               # the upper-left spikelets catch the light
            s += f'<circle cx="{g[0] - 0.35:.2f}" cy="{g[1] - 0.35:.2f}" r="0.45" fill="{hx(gl)}"/>'
    return f'<g stroke-linecap="round" stroke-linejoin="round">{s}</g>'


def shadow(rx, ry):
    return f'<ellipse cx="{BASE[0]}" cy="{BASE[1] + 2}" rx="{rx}" ry="{ry}" fill="#000" opacity="0.2"/>'


def svg(body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="128" height="128">'
            + body + "</svg>")


def tuft(n, length, width, spread, seed, droop=0.3, dry=0.0, heads=(), lean_bias=0.0):
    """A tuft of n blades fanned over +-spread degrees, drawn back to front:
    upright blades at the back, outer arching blades in front.
    heads: list of (foot_x, angle, height, ripe) flowering stems, drawn among the back blades."""
    rng = random.Random(seed)
    specs = []
    for i in range(n):
        f = (i + 0.5) / n * 2 - 1                         # -1 .. 1 across the fan
        f = math.copysign(abs(f) ** 1.1, f)
        ang = f * spread + lean_bias + rng.uniform(-6, 6)
        L = length * (1 - 0.4 * abs(f) ** 1.5) * rng.uniform(0.72, 1.12)
        dr = droop * (0.2 + 0.7 * abs(f)) * rng.uniform(0.6, 1.15)
        bx = f * 4.5 + rng.uniform(-1.2, 1.2)
        shade = lerp(0.15, 1.0, abs(f) ** 0.7) * rng.uniform(0.75, 1.0)
        d = dry * rng.uniform(0.35, 1.0) if rng.random() < 0.6 else 0.0
        order = -abs(f) + rng.uniform(-0.25, 0.25)        # central blades first, outer ones last
        specs.append((order, "B", (bx, ang, L, width, shade, dr, d)))
    for k, (fx, ang, h, ripe) in enumerate(heads):
        specs.append((-0.7 + 0.08 * k, "H", (fx, ang, h, 0.6 + 0.2 * (k % 2), ripe)))
    specs.sort(key=lambda s: s[0])
    out = ""
    for _, kind, a in specs:
        if kind == "B":
            bx, ang, L, w, sh, dr, d = a
            out += blade(bx, ang, L, w, sh, rng, droop=dr, dry=d)
        else:
            out += seed_head(*a, rng)
    return '<g stroke-linejoin="round" stroke-linecap="round">' + out + "</g>"


SPRITES = (
    # a few sprouts
    ("grass_0", shadow(11, 3) + tuft(9, 17, 1.05, 26, seed=2, droop=0.04)),
    # small tuft
    ("grass_1", shadow(18, 4) + tuft(22, 30, 1.15, 42, seed=4, droop=0.14)),
    # medium tuft
    ("grass_2", shadow(25, 5) + tuft(36, 44, 1.25, 48, seed=6, droop=0.24)),
    # full tuft, first flower heads
    ("grass_3", shadow(31, 6) + tuft(46, 56, 1.3, 52, seed=8, droop=0.32, dry=0.08,
                                   heads=[(-2, -8, 74, 0.0), (2, 10, 68, 0.1), (0, 1, 82, 0.0)])),
    # tall tuft in seed
    ("grass_4", shadow(35, 6.5) + tuft(52, 62, 1.3, 55, seed=10, droop=0.38, dry=0.4,
                                     heads=[(-4, -16, 84, 1.0), (-1, -4, 96, 0.9), (2, 8, 92, 1.0),
                                            (5, 20, 80, 0.8), (0, -26, 70, 1.0)])),
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
