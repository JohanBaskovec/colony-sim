"""Small items lying on the ground: brindille (twig), silex (flint nodule), galet (pebble).
128x128 RGBA, one item per sprite, same style as the plants and branches.

Usage: python3 ground_items.py [output_dir]
Needs only the standard library + `rsvg-convert` (librsvg) to rasterise the SVGs.
Writes twig / flint / pebble .svg, .png (128x128) and _x4.png (512x512).
"""
import math, os, random, subprocess, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "out"

# ---- global knobs -----------------------------------------------------------
CENTER = (64.0, 66.0)        # where the middle of the item lands in the tile
OUT_W = 1.7                  # outline thickness (total extra stroke width)
SUN = (-0.45, -0.9)          # light comes from the upper left
SHADOW = (1.2, 2.6)          # offset of the shadow on the ground
SHADOW_OPACITY = 0.0         # 0: no shadow in the sprite (the item is turned in the game)

# twig (same bark palette as branch.py)
BARK_OUTLINE = "#2a1d14"
BARK_D = (78, 56, 38)
BARK_M = (116, 86, 58)
BARK_L = (150, 116, 80)
BARK_HL = (192, 160, 116)
WOOD = (222, 196, 148)
WOOD_D = (176, 142, 96)
# flint: chalky white cortex around dark glassy stone
STONE_OUTLINE = "#1e1c1c"
CORTEX = (214, 204, 180)
CORTEX_D = (164, 152, 128)
CORTEX_L = (240, 234, 218)
PIT = (146, 134, 112)
FLINT = (62, 66, 78)
FLINT_D = (40, 42, 52)
FLINT_L = (104, 112, 128)
FLINT_HL = (206, 214, 228)
# pebble: smooth blue-grey river stone with a quartz vein
PEB = (128, 130, 132)
PEB_D = (88, 90, 96)
PEB_L = (164, 166, 164)
PEB_HL = (222, 224, 220)
VEIN = (226, 222, 210)


def lerp(a, b, t):
    return a + (b - a) * t


def mix(c1, c2, t):
    return tuple(lerp(a, b, t) for a, b in zip(c1, c2))


def hx(c):
    return "#%02x%02x%02x" % tuple(max(0, min(255, round(v))) for v in c)


def poly(pts, close=True):
    return "M" + " L".join("%.2f,%.2f" % p for p in pts) + ("Z" if close else "")


def spline(pts, closed=True):
    """Smooth curve through the points (Catmull-Rom converted to cubic Beziers)."""
    n = len(pts)
    d = "M%.2f,%.2f" % pts[0]
    for i in range(n if closed else n - 1):
        if closed:
            p0, p1, p2, p3 = pts[(i - 1) % n], pts[i], pts[(i + 1) % n], pts[(i + 2) % n]
        else:
            p0, p1, p2, p3 = pts[max(0, i - 1)], pts[i], pts[i + 1], pts[min(n - 1, i + 2)]
        c1 = (p1[0] + (p2[0] - p0[0]) / 6, p1[1] + (p2[1] - p0[1]) / 6)
        c2 = (p2[0] - (p3[0] - p1[0]) / 6, p2[1] - (p3[1] - p1[1]) / 6)
        d += " C%.2f,%.2f %.2f,%.2f %.2f,%.2f" % (*c1, *c2, *p2)
    return d + ("Z" if closed else "")


def rim_masks(prefix, d, dx, dy):
    """Form shading: the shape minus the same shape shifted towards the light leaves a
    crescent on the shaded side (<prefix>Low); shifted the other way, on the lit side (<prefix>Top)."""
    s = ""
    for name, (ox, oy) in ((prefix + "Low", (dx, dy)), (prefix + "Top", (-dx, -dy))):
        s += (f'<mask id="{name}"><rect width="128" height="128" fill="#fff"/>'
              f'<path d="{d}" fill="#000" transform="translate({ox},{oy})"/></mask>')
    return s


def shadow(d):
    if SHADOW_OPACITY <= 0.0:
        return ""
    return (f'<g opacity="{SHADOW_OPACITY}" transform="translate({SHADOW[0]},{SHADOW[1]})">'
            f'<path d="{d}" fill="#000" stroke="#000" stroke-width="{OUT_W}" stroke-linejoin="round"/></g>')


def blob(cx, cy, rx, ry, rot, waves, rng, n=14):
    """Closed lumpy outline: an ellipse whose radius is modulated by a few waves."""
    ph = [rng.uniform(0, 2 * math.pi) for _ in waves]
    c, s = math.cos(math.radians(rot)), math.sin(math.radians(rot))
    pts = []
    for k in range(n):
        a = 2 * math.pi * k / n
        r = 1 + sum(amp * math.cos(f * a + p) for (f, amp), p in zip(waves, ph))
        x, y = rx * math.cos(a) * r, ry * math.sin(a) * r
        pts.append((cx + x * c - y * s, cy + x * s + y * c))
    return pts


# ---- twig (same construction as branch.py, smaller) -------------------------
def chaikin(pts):
    out = [pts[0]]
    for a, b in zip(pts, pts[1:]):
        out.append((lerp(a[0], b[0], 0.25), lerp(a[1], b[1], 0.25)))
        out.append((lerp(a[0], b[0], 0.75), lerp(a[1], b[1], 0.75)))
    out.append(pts[-1])
    return out


class Limb:
    """A tapering piece of wood: centre line, tangents, and width at each point."""

    def __init__(self, p0, angle, length, w0, w1, bend, rng, depth, wiggle=0.12):
        n = max(3, round(length / 4))
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
        mx = sum(t[0] for t in self.tan) / m
        my = sum(t[1] for t in self.tan) / m
        self.lit = 1 if (-my * SUN[0] + mx * SUN[1]) > 0 else -1

    def at(self, t):
        f = t * (len(self.pts) - 1)
        i = min(len(self.pts) - 2, int(f))
        a, b = self.pts[i], self.pts[i + 1]
        tg = self.tan[i]
        return (lerp(a[0], b[0], f - i), lerp(a[1], b[1], f - i)), math.degrees(math.atan2(-tg[1], tg[0]))

    def off(self, k0, k1=None, i0=0, i1=None):
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


def snapped_end(limb, rng):
    """Jagged pale wood where the twig snapped (at its base)."""
    (x, y), (tx, ty), w = limb.pts[0], limb.tan[0], limb.w[0] / 2
    pts = []
    for k in range(3):
        u = lerp(-1, 1, k / 2)
        out = (0.2 if k % 2 == 0 else 1.0) * rng.uniform(0.8, 1.2) * (1.0 + w * 0.7)
        pts.append((x - ty * w * u - tx * out, y + tx * w * u - ty * out))
    return pts + [(x + ty * w + tx * 1.0, y - tx * w + ty * 1.0), (x - ty * w + tx * 1.0, y + tx * w + ty * 1.0)]


TWIG = (14, (50, 2.7, 0.7, -14, [
    (0.38, 38, (13, 1.4, 0.5, -8, [])),
    (0.62, -34, (10, 1.2, 0.5, 10, [(0.5, 40, (4.5, 0.9, 0.45, 0, []))])),
    (0.82, 30, (7, 1.0, 0.45, 0, [])),
]))


def twig(seed=5):
    rng = random.Random(seed)
    angle, spec = TWIG
    limbs = grow(spec, (0.0, 0.0), angle, rng)
    xs = [p[0] for l in limbs for p in l.pts]
    ys = [p[1] for l in limbs for p in l.pts]
    dx = CENTER[0] - (min(xs) + max(xs)) / 2
    dy = CENTER[1] - (min(ys) + max(ys)) / 2
    for l in limbs:
        l.pts = [(x + dx, y + dy) for x, y in l.pts]
    main = limbs[0]
    cut = snapped_end(main, rng)
    order = sorted(limbs, key=lambda l: -l.depth)
    shapes = " ".join(poly(l.ribbon()) for l in limbs) + " " + poly(cut)
    s = shadow(shapes)
    s += '<g stroke-linejoin="round" stroke-linecap="round">'
    s += f'<path d="{shapes}" fill="{BARK_OUTLINE}" stroke="{BARK_OUTLINE}" stroke-width="{OUT_W}"/>'
    s += f'<path d="{poly(cut)}" fill="{hx(WOOD)}"/>'
    s += f'<path d="{poly(cut[:3], False)}" fill="none" stroke="{hx(WOOD_D)}" stroke-width="0.45" transform="translate(0.3,0.3)"/>'
    for l in order:
        s += f'<path d="{poly(l.ribbon())}" fill="{hx(BARK_M)}"/>'
    for l in order:
        if l is not main:
            s += f'<path d="{poly(l.ribbon(-0.2, 1.0))}" fill="{hx(mix(BARK_M, BARK_L, 0.6))}"/>'
            continue
        s += f'<path d="{poly(l.ribbon(-1.0, -0.15))}" fill="{hx(BARK_D)}"/>'
        s += f'<path d="{poly(l.ribbon(0.2, 1.0))}" fill="{hx(BARK_L)}"/>'
        m = len(l.pts)
        s += (f'<path d="{poly(l.off(0.55, i0=1, i1=m - 3), False)}" fill="none" stroke="{hx(BARK_HL)}" '
              f'stroke-width="0.5" stroke-dasharray="5 2 8 3" opacity="0.85"/>')
        # bud scars: tiny dark notches along the twig
        for t in (0.22, 0.5, 0.74):
            i = int(t * (m - 1))
            a, b = l.off(-0.9, i0=i, i1=i + 1)[0], l.off(-0.1, i0=i, i1=i + 1)[0]
            s += f'<path d="M{a[0]:.2f},{a[1]:.2f} L{b[0]:.2f},{b[1]:.2f}" stroke="{hx(mix(BARK_D, (40, 28, 20), 0.5))}" stroke-width="0.45"/>'
    s += "</g>"
    return s


# ---- flint nodule -----------------------------------------------------------
def flint(seed=11):
    rng = random.Random(seed)
    # lumpy chalky nodule, one end knocked off to show the dark stone inside
    pts = blob(CENTER[0], CENTER[1], 19.0, 10.5, -8, [(2, 0.06), (3, 0.09), (4, 0.06), (7, 0.035)], rng, n=20)
    body = spline(pts)
    # fracture: the broken face covers the right-hand end of the nodule
    face = [(70.6, 50.0), (72.4, 57.4), (70.8, 64.0), (73.2, 70.4), (72.0, 82.0),
            (92.0, 82.0), (92.0, 50.0)]
    edge = face[:5]                                   # the sharp edge between cortex and break
    ix, iy = 72.4, 57.4                               # point of percussion (ripples spread from it)
    s = '<defs>'
    s += f'<clipPath id="nod"><path d="{body}"/></clipPath>'
    s += f'<clipPath id="face"><path d="{poly(face)}"/></clipPath>'
    s += rim_masks("nod", body, -1.4, -1.9)
    s += '</defs>'
    s += shadow(body)
    s += '<g stroke-linejoin="round" stroke-linecap="round">'
    s += f'<path d="{body}" fill="{STONE_OUTLINE}" stroke="{STONE_OUTLINE}" stroke-width="{OUT_W}"/>'
    # cortex
    s += f'<path d="{body}" fill="{hx(CORTEX)}"/>'
    s += f'<path d="{body}" fill="{hx(CORTEX_D)}" mask="url(#nodLow)"/>'
    s += f'<path d="{body}" fill="{hx(CORTEX_L)}" mask="url(#nodTop)"/>'
    s += '<g clip-path="url(#nod)">'
    pits = ""
    for _ in range(16):
        x, y = rng.uniform(46, 70), rng.uniform(57, 76)
        r = rng.uniform(0.35, 0.7)
        pits += f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{r:.2f}"/>'
    s += f'<g fill="{hx(PIT)}" opacity="0.8">{pits}</g>'
    # broken face: dark glassy stone, lighter towards the light, conchoidal ripples
    s += '<g clip-path="url(#face)">'
    s += f'<path d="{body}" fill="{hx(FLINT)}"/>'
    s += f'<path d="{body}" fill="{hx(FLINT_D)}" mask="url(#nodLow)"/>'
    s += (f'<path d="M{ix:.2f},{iy:.2f} L88,46 L92,62 Z" fill="{hx(FLINT_L)}" opacity="0.55"/>')
    ripples = []
    for r in (3.6, 6.4, 9.4, 12.6, 16.0):
        a0, a1 = math.radians(-35), math.radians(80)
        arc = [(ix + math.cos(lerp(a0, a1, k / 10)) * r, iy + math.sin(lerp(a0, a1, k / 10)) * r * 0.85)
               for k in range(11)]
        ripples.append(spline(arc, closed=False))
    s += f'<path d="{" ".join(ripples)}" fill="none" stroke="{hx(FLINT_L)}" stroke-width="0.55" opacity="0.7"/>'
    s += f'<path d="{" ".join(ripples[1::2])}" fill="none" stroke="{hx(FLINT_D)}" stroke-width="0.5" opacity="0.6" transform="translate(0.5,0.6)"/>'
    # glossy highlight
    s += f'<path d="M75.4,60.2 Q78.6,58.4 81.6,59.4" fill="none" stroke="{hx(FLINT_HL)}" stroke-width="0.9" opacity="0.9"/>'
    s += f'<circle cx="78.0" cy="65.0" r="0.55" fill="{hx(FLINT_HL)}" opacity="0.9"/>'
    s += '</g></g>'
    # the white rind seen in section, then the sharp edge
    s += f'<path d="{poly(edge, False)}" fill="none" stroke="{hx(CORTEX_L)}" stroke-width="1.6" transform="translate(-0.9,0)" clip-path="url(#nod)"/>'
    s += f'<path d="{poly(edge, False)}" fill="none" stroke="{STONE_OUTLINE}" stroke-width="1.0" clip-path="url(#nod)"/>'
    s += "</g>"
    return s


# ---- pebble -----------------------------------------------------------------
def pebble(seed=3):
    rng = random.Random(seed)
    pts = blob(CENTER[0], CENTER[1], 14.0, 9.0, -10, [(2, 0.05), (3, 0.04)], rng, n=12)
    body = spline(pts)
    s = '<defs>'
    s += f'<clipPath id="peb"><path d="{body}"/></clipPath>'
    s += rim_masks("peb", body, -1.8, -2.4)
    s += rim_masks("pebSoft", body, -4.0, -5.0)
    s += '</defs>'
    s += shadow(body)
    s += '<g stroke-linejoin="round" stroke-linecap="round">'
    s += f'<path d="{body}" fill="{STONE_OUTLINE}" stroke="{STONE_OUTLINE}" stroke-width="{OUT_W}"/>'
    s += f'<path d="{body}" fill="{hx(PEB)}"/>'
    s += f'<path d="{body}" fill="{hx(PEB_D)}" mask="url(#pebSoftLow)" opacity="0.45"/>'
    s += f'<path d="{body}" fill="{hx(PEB_D)}" mask="url(#pebLow)"/>'
    s += f'<path d="{body}" fill="{hx(PEB_L)}" mask="url(#pebTop)"/>'
    s += '<g clip-path="url(#peb)">'
    # quartz vein running across the stone
    s += f'<path d="M44,74 Q58,62 70,63 T88,52" fill="none" stroke="{hx(VEIN)}" stroke-width="1.5" opacity="0.9"/>'
    s += f'<path d="M44,74 Q58,62 70,63 T88,52" fill="none" stroke="{hx(PEB_D)}" stroke-width="0.4" opacity="0.6" transform="translate(0.4,1.1)"/>'
    specks = ""
    for _ in range(14):
        x, y = rng.uniform(52, 77), rng.uniform(58, 75)
        specks += f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{rng.uniform(0.3, 0.5):.2f}"/>'
    s += f'<g fill="{hx(PEB_D)}" opacity="0.7">{specks}</g>'
    s += '</g>'
    # polished highlight
    s += f'<ellipse cx="57.0" cy="61.8" rx="3.8" ry="1.5" transform="rotate(-14 57 61.8)" fill="{hx(PEB_HL)}" opacity="0.75"/>'
    s += f'<circle cx="62.6" cy="60.6" r="0.6" fill="{hx(PEB_HL)}" opacity="0.8"/>'
    s += "</g>"
    return s


def svg(body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="128" height="128">'
            + body + "</svg>")


SPRITES = (
    ("twig", twig()),
    ("flint", flint()),
    ("pebble", pebble()),
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
