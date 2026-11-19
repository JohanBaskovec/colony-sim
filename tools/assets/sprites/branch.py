"""Branche / fallen branch sprites, 4 variants, 128x128 RGBA.  One branch per sprite.

Usage: python3 branch.py [output_dir]
Needs only the standard library + `rsvg-convert` (librsvg) to rasterise the SVGs.
Writes branch_N.svg, branch_N.png (128x128) and branch_N_x4.png (512x512).

Each branch is a small tree of "limbs" described at the bottom of the file.
The drawing is centred automatically on CENTER, so a spec only describes the shape.
"""
import math, os, random, subprocess, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "out"

# ---- palette / global knobs -------------------------------------------------
OUTLINE = "#2a1d14"
DARK = (78, 56, 38)          # bark, shaded side
MID = (116, 86, 58)
LIGHT = (150, 116, 80)       # bark, lit side
HILITE = (192, 160, 116)
WOOD = (222, 196, 148)       # pale wood where the branch snapped
WOOD_D = (176, 142, 96)
CENTER = (64.0, 66.0)        # where the middle of the branch lands in the tile
OUT_W = 1.7                  # outline thickness (total extra stroke width)
SUN = (-0.45, -0.9)          # light comes from the upper left
SHADOW = (1.2, 2.8)          # offset of the shadow on the ground


def lerp(a, b, t):
    return a + (b - a) * t


def mix(c1, c2, t):
    return tuple(lerp(a, b, t) for a, b in zip(c1, c2))


def hx(c):
    return "#%02x%02x%02x" % tuple(max(0, min(255, round(v))) for v in c)


def poly(pts, close=False):
    return "M" + " L".join("%.2f,%.2f" % p for p in pts) + ("Z" if close else "")


def chaikin(pts):
    """One round of corner cutting: turns sharp kinks into soft bends."""
    out = [pts[0]]
    for a, b in zip(pts, pts[1:]):
        out.append((lerp(a[0], b[0], 0.25), lerp(a[1], b[1], 0.25)))
        out.append((lerp(a[0], b[0], 0.75), lerp(a[1], b[1], 0.75)))
    out.append(pts[-1])
    return out


class Limb:
    """A tapering piece of wood: centre line, tangents, and width at each point."""

    def __init__(self, p0, angle, length, w0, w1, bend, rng, depth, wiggle=0.11):
        n = max(3, round(length / 5))
        a = math.radians(angle)
        pts = [p0]
        for _ in range(n):
            a += math.radians(bend) / n + rng.uniform(-wiggle, wiggle)
            pts.append((pts[-1][0] + math.cos(a) * length / n, pts[-1][1] - math.sin(a) * length / n))
        self.pts = chaikin(pts) if n > 3 else pts
        m = len(self.pts)
        self.tan = []
        for i in range(m):
            a_, b_ = self.pts[max(0, i - 1)], self.pts[min(m - 1, i + 1)]
            d = math.hypot(b_[0] - a_[0], b_[1] - a_[1]) or 1
            self.tan.append(((b_[0] - a_[0]) / d, (b_[1] - a_[1]) / d))
        self.w = [lerp(w0, w1, (i / (m - 1)) ** 0.9) * rng.uniform(0.95, 1.05) for i in range(m)]
        self.depth, self.w0, self.w1, self.length = depth, w0, w1, length
        # which side of the ribbon faces the light (+1 = left normal)
        mx = sum(t[0] for t in self.tan) / m
        my = sum(t[1] for t in self.tan) / m
        self.lit = 1 if (-my * SUN[0] + mx * SUN[1]) > 0 else -1

    def at(self, t):
        """Point and tangent angle (degrees) at fraction t of the limb."""
        f = t * (len(self.pts) - 1)
        i = min(len(self.pts) - 2, int(f))
        a, b = self.pts[i], self.pts[i + 1]
        tg = self.tan[i]
        return (lerp(a[0], b[0], f - i), lerp(a[1], b[1], f - i)), math.degrees(math.atan2(-tg[1], tg[0]))

    def off(self, k0, k1=None, i0=0, i1=None):
        """Points offset sideways by k (fraction of the half width, + = lit side)."""
        i1 = len(self.pts) if i1 is None else i1
        out = []
        for i in range(i0, i1):
            (x, y), (tx, ty), w = self.pts[i], self.tan[i], self.w[i] / 2
            k = k0 if k1 is None else lerp(k0, k1, (i - i0) / max(1, i1 - i0 - 1))
            out.append((x - ty * w * k * self.lit, y + tx * w * k * self.lit))
        return out

    def ribbon(self, k0=-1.0, k1=1.0):
        return self.off(k0) + self.off(k1)[::-1]


def grow(spec, p0, angle, rng, depth=0, out=None):
    """spec = (length, base_width, tip_width, bend_degrees, [children])
    child = (position 0..1 along the parent, angle relative to the parent, spec)"""
    out = [] if out is None else out
    length, w0, w1, bend, kids = spec
    limb = Limb(p0, angle, length, w0, w1, bend, rng, depth)
    out.append(limb)
    for t, rel, sub in kids:
        p, a = limb.at(t)
        grow(sub, p, a + rel, rng, depth + 1, out)
    return out


def splinter(limb, end, rng):
    """Jagged pale wood where the limb snapped.  end: 0 = base, 1 = tip."""
    i = 0 if end == 0 else len(limb.pts) - 1
    (x, y), (tx, ty), w = limb.pts[i], limb.tan[i], limb.w[i] / 2
    sgn = -1 if end == 0 else 1          # splinters stick out away from the limb
    if limb.length < 6:                  # a stub: just a small round scar of pale wood
        cx, cy = x + tx * 0.4, y + ty * 0.4
        return [(cx + math.cos(k * math.pi / 4) * w * 0.8, cy + math.sin(k * math.pi / 4) * w * 0.8) for k in range(8)]
    n = 5 if w > 1.6 else 3
    pts = []
    for k in range(n):
        u = lerp(-1, 1, k / (n - 1))
        out = (0.2 if k % 2 == 0 else 1.0) * rng.uniform(0.7, 1.2) * (1.1 + w * 0.75)
        pts.append((x - ty * w * u + sgn * tx * out, y + tx * w * u + sgn * ty * out))
    back = [(x - ty * w - sgn * tx * 1.2, y + tx * w - sgn * ty * 1.2),
            (x + ty * w - sgn * tx * 1.2, y - tx * w - sgn * ty * 1.2)]
    return pts + back


def branch(angle, spec, seed):
    """Returns the SVG body for one branch lying on the ground."""
    rng = random.Random(seed)
    limbs = grow(spec, (0.0, 0.0), angle, rng)
    # centre the whole thing in the tile
    xs = [p[0] for l in limbs for p in l.pts]
    ys = [p[1] for l in limbs for p in l.pts]
    dx = CENTER[0] - (min(xs) + max(xs)) / 2
    dy = CENTER[1] - (min(ys) + max(ys)) / 2
    for l in limbs:
        l.pts = [(x + dx, y + dy) for x, y in l.pts]
    main = limbs[0]
    ends = [(main, 0)] + [(l, 1) for l in limbs if l.w1 >= 1.5]   # snapped ends
    cuts = [splinter(l, e, rng) for l, e in ends]
    order = sorted(limbs, key=lambda l: -l.depth)                  # twigs first, main limb on top

    shapes = " ".join(poly(l.ribbon(), True) for l in limbs) + " " + " ".join(poly(c, True) for c in cuts)
    s = f'<g opacity="0.22" transform="translate({SHADOW[0]},{SHADOW[1]})">'
    s += f'<path d="{shapes}" fill="#000" stroke="#000" stroke-width="{OUT_W}" stroke-linejoin="round"/></g>'
    s += '<g stroke-linejoin="round" stroke-linecap="round">'
    s += f'<path d="{shapes}" fill="{OUTLINE}" stroke="{OUTLINE}" stroke-width="{OUT_W}"/>'
    for c in cuts:
        s += f'<path d="{poly(c, True)}" fill="{hx(WOOD)}"/>'
        if len(c) < 8:   # shade under the splinters (not on the round stub scars)
            s += f'<path d="{poly(c[:len(c) - 2])}" fill="none" stroke="{hx(WOOD_D)}" stroke-width="0.5" transform="translate(0.3,0.4)"/>'
    for l in order:
        s += f'<path d="{poly(l.ribbon(), True)}" fill="{hx(MID)}"/>'
    for l in order:
        if l.w0 < 2.0:      # thin twigs: one flat tone, a bit lighter
            s += f'<path d="{poly(l.ribbon(-0.2, 1.0), True)}" fill="{hx(mix(MID, LIGHT, 0.6))}"/>'
            continue
        s += f'<path d="{poly(l.ribbon(-1.0, -0.2), True)}" fill="{hx(DARK)}"/>'
        s += f'<path d="{poly(l.ribbon(0.15, 1.0), True)}" fill="{hx(LIGHT)}"/>'
        m = len(l.pts)
        if m > 6:
            s += (f'<path d="{poly(l.off(0.55, i0=1, i1=m - 2))}" fill="none" stroke="{hx(HILITE)}" '
                  f'stroke-width="0.6" stroke-dasharray="{rng.uniform(4, 7):.1f} {rng.uniform(1.5, 3):.1f} '
                  f'{rng.uniform(7, 11):.1f} {rng.uniform(2, 4):.1f}" opacity="0.85"/>')
        # bark: a few short dark streaks running along the limb
        streaks = []
        for _ in range(int(l.length / 9)):
            i = rng.randrange(1, max(2, m - 4))
            k = rng.uniform(-0.55, 0.35)
            streaks.append(poly(l.off(k, i0=i, i1=min(m, i + rng.choice((2, 3))))))
        if streaks:
            s += f'<path d="{" ".join(streaks)}" fill="none" stroke="{hx(mix(DARK, (40, 28, 20), 0.5))}" stroke-width="0.5" opacity="0.75"/>'
    s += "</g>"
    return s


def svg(body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="128" height="128">'
            + body + "</svg>")


# ---- the branches -----------------------------------------------------------
# limb  = (length, base_width, tip_width, bend_degrees, [children])
# child = (position 0..1 along the parent, angle relative to the parent in degrees, limb)
# Angles: 0 = pointing right, positive = turning up (counter-clockwise).
# A tip width >= 1.5 is drawn as a snapped end (pale wood), thinner tips are pointed.
def twig(length, w=1.5, bend=0, kids=()):
    return (length, w, 0.5, bend, list(kids))


def stub(length=3.0, w=2.4):
    return (length, w, 2.0, 0, [])


# 0: an almost straight stick with a couple of twigs
BRANCH0 = (14, (84, 4.6, 1.2, -12, [
    (0.30, -42, stub()),
    (0.48, 36, twig(20, 1.9, -10, [(0.55, -40, twig(8, 1.1))])),
    (0.72, -30, twig(15, 1.6, 8)),
]))
# 1: a forked branch (Y)
BRANCH1 = (38, (46, 5.0, 3.4, -8, [
    (0.95, 30, (40, 3.2, 1.0, -14, [(0.5, 38, twig(11, 1.3))])),
    (0.95, -34, (34, 2.9, 1.0, 16, [(0.45, -40, twig(10, 1.2))])),
    (0.40, 50, stub(3.2)),
]))
# 2: a curved, twiggy branch
BRANCH2 = (-22, (82, 4.2, 0.9, 62, [
    (0.22, 44, twig(18, 1.8, -12, [(0.5, 42, twig(8, 1.0))])),
    (0.42, -46, twig(22, 1.9, 14, [(0.4, -38, twig(9, 1.1)), (0.7, 34, twig(7, 1.0))])),
    (0.62, 40, twig(15, 1.5, -8)),
    (0.80, -36, twig(11, 1.2)),
]))
# 3: a short, stout piece snapped at both ends
BRANCH3 = (-18, (58, 6.2, 4.6, 10, [
    (0.35, 52, (16, 2.8, 1.0, -10, [(0.5, -36, twig(7, 1.1))])),
    (0.68, -48, stub(4.0, 2.6)),
]))

SPRITES = (
    ("branch_0", branch(*BRANCH0, seed=3)),
    ("branch_1", branch(*BRANCH1, seed=7)),
    ("branch_2", branch(*BRANCH2, seed=12)),
    ("branch_3", branch(*BRANCH3, seed=21)),
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
