"""Wild carrot (Daucus carota) sprites, 3 growth stages, 128x128 RGBA.
Draws SVG, rasterises with rsvg-convert."""
import math, random, subprocess, sys, os

OUT = sys.argv[1] if len(sys.argv) > 1 else "out"
OUTLINE = "#182816"
BASE = (64.0, 116.5)
OUTW = 1.9


def lerp(a, b, t):
    return a + (b - a) * t


def mix(c1, c2, t):
    return "#%02x%02x%02x" % tuple(round(lerp(a, b, t)) for a, b in zip(c1, c2))


DARK = (52, 88, 42)
MID = (80, 128, 58)
LIGHT = (112, 166, 76)
HILITE = (150, 196, 100)


def bez(p0, p1, p2, t):
    u = 1 - t
    x = u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0]
    y = u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]
    dx = 2 * u * (p1[0] - p0[0]) + 2 * t * (p2[0] - p1[0])
    dy = 2 * u * (p1[1] - p0[1]) + 2 * t * (p2[1] - p1[1])
    n = math.hypot(dx, dy) or 1
    return (x, y), (dx / n, dy / n)


def rot(v, a):
    c, s = math.cos(a), math.sin(a)
    return (v[0] * c - v[1] * s, v[0] * s + v[1] * c)


def leaflet(p, d, L, rng, wk=0.42):
    """Deeply cut leaflet starting at p, pointing along d, length L."""
    n = (-d[1], d[0])
    w = L * wk
    # (position along axis, half-width factor, forward sweep of the point)
    prof = [(0.0, 0.10, 0), (0.10, 0.72, 0.10), (0.27, 0.16, 0), (0.36, 1.0, 0.12), (0.53, 0.18, 0),
            (0.62, 0.72, 0.10), (0.77, 0.16, 0), (1.0, 0.0, 0)]
    left, right = [], []
    for s_, k, sw in prof:
        for side, acc in ((1, left), (-1, right)):
            kk = k * (1 + rng.uniform(-0.15, 0.15))
            ss = s_ + sw * (1 + rng.uniform(-0.3, 0.3))
            acc.append((p[0] + d[0] * L * ss + side * n[0] * w * kk, p[1] + d[1] * L * ss + side * n[1] * w * kk))
    pts = left + right[::-1][1:]
    return "M" + " L".join("%.2f,%.2f" % q for q in pts) + "Z"


def frond(angle, length, width, shade, rng, droop=0.25, pairs=None, base=BASE, t0=0.42):
    """angle: degrees from vertical (negative = left). Long bare petiole, triangular feathery blade."""
    a = math.radians(angle)
    d = (math.sin(a), -math.cos(a))
    p0 = base
    up = (math.sin(a * 0.4), -math.cos(a * 0.4))
    p1 = (p0[0] + up[0] * length * 0.6, p0[1] + up[1] * length * 0.6)
    p2 = (p0[0] + d[0] * length, p0[1] + d[1] * length + droop * length * abs(math.sin(a)))
    pairs = pairs or max(3, round(length * (1 - t0) / 4.3))
    shapes, veins = [], []
    for i in range(pairs):
        u = i / pairs
        t = lerp(t0, 1.0, u)
        (x, y), tg = bez(p0, p1, p2, t)
        L = width * (1 - 0.72 * u) * (0.86 if i == 0 else 1) * rng.uniform(0.92, 1.08)
        for side in (-1, 1):
            dd = rot(tg, side * math.radians(rng.uniform(58, 70)))
            shapes.append(leaflet((x, y), dd, L, rng))
            veins.append("M%.2f,%.2f L%.2f,%.2f" % (x, y, x + dd[0] * L * 0.6, y + dd[1] * L * 0.6))
    (xt, yt), tg = bez(p0, p1, p2, 0.94)
    shapes.append(leaflet((xt, yt), tg, width * 0.5, rng))
    (xe, ye), _ = bez(p0, p1, p2, 1.0)
    rib = "M%.2f,%.2f Q%.2f,%.2f %.2f,%.2f" % (*p0, *p1, xe, ye)
    dpath = " ".join(shapes)
    fill = mix(DARK, LIGHT, shade)
    hl = mix(MID, HILITE, shade)
    s = '<g stroke-linejoin="round" stroke-linecap="round">'
    s += f'<path d="{dpath}" fill="{OUTLINE}" stroke="{OUTLINE}" stroke-width="{OUTW}"/>'
    s += f'<path d="{rib}" fill="none" stroke="{OUTLINE}" stroke-width="{OUTW + 1.0}"/>'
    s += f'<path d="{dpath}" fill="{fill}"/>'
    s += f'<path d="{" ".join(veins)}" fill="none" stroke="{hl}" stroke-width="0.75" opacity="0.9"/>'
    s += f'<path d="{rib}" fill="none" stroke="{hl}" stroke-width="1.2"/>'
    s += "</g>"
    return s


def shadow(rx, ry, cy=None):
    cy = cy or BASE[1] + 2.5
    return (f'<ellipse cx="{BASE[0]}" cy="{cy}" rx="{rx}" ry="{ry}" fill="#000" opacity="0.2"/>')


def crown():
    x, y = BASE
    return (f'<path d="M{x-3.2},{y+1.5} Q{x},{y-5} {x+3.2},{y+1.5} Q{x},{y+3.5} {x-3.2},{y+1.5}Z" '
            f'fill="{mix(DARK, MID, 0.3)}" stroke="{OUTLINE}" stroke-width="1.3" stroke-linejoin="round"/>')


def rosette(specs, seed):
    rng = random.Random(seed)
    return "".join(frond(a, L, w, sh, rng, droop=dr) for (a, L, w, sh, dr) in specs)


def umbel(cx, cy, rx, ry, rng, rays=11, top=None, dot=True):
    """Flat-topped umbel centred at (cx, cy); stalk top at `top`."""
    top = top or (cx, cy + ry + 7)
    s = '<g stroke-linecap="round" stroke-linejoin="round">'
    # bracts (thin, drooping, forked)
    br = []
    for k in range(7):
        a = math.radians(lerp(-78, 78, k / 6) + rng.uniform(-5, 5))
        L = rng.uniform(5.5, 7.5)
        ex, ey = top[0] + math.sin(a) * L, top[1] + abs(math.cos(a)) * 1.5 + 2.2 * abs(math.sin(a))
        br.append("M%.2f,%.2f Q%.2f,%.2f %.2f,%.2f" % (*top, top[0] + math.sin(a) * L * 0.6, top[1] - 1.0, ex, ey))
    s += f'<path d="{" ".join(br)}" fill="none" stroke="{OUTLINE}" stroke-width="2.4"/>'
    s += f'<path d="{" ".join(br)}" fill="none" stroke="{mix(DARK, MID, 0.6)}" stroke-width="0.9"/>'
    # rays + umbellets positions on an ellipse seen slightly from above
    pts = []
    for k in range(rays):
        u = k / (rays - 1)
        px = cx + lerp(-rx, rx, u)
        py = cy - ry * math.sqrt(max(0, 1 - (2 * u - 1) ** 2)) * 0.9 + rng.uniform(-0.6, 0.6)
        pts.append((px, py, 1.0))
    for k in range(rays - 4):  # front row, a bit lower
        u = (k + 0.5) / (rays - 4)
        px = cx + lerp(-rx * 0.72, rx * 0.72, u)
        py = cy + ry * 0.45 + rng.uniform(-0.5, 0.5)
        pts.append((px, py, 1.08))
    for k in range(rays - 2):  # middle row fills the disc
        u = (k + 0.5) / (rays - 2)
        pts.insert(rays, (cx + lerp(-rx * 0.86, rx * 0.86, u), cy - ry * 0.15 + rng.uniform(-0.5, 0.5), 1.0))
    rp = " ".join("M%.2f,%.2f Q%.2f,%.2f %.2f,%.2f" % (*top, lerp(top[0], px, 0.35), lerp(top[1], py, 0.75), px, py)
                  for px, py, _ in pts)
    s += f'<path d="{rp}" fill="none" stroke="{OUTLINE}" stroke-width="2.0"/>'
    s += f'<path d="{rp}" fill="none" stroke="{mix(MID, LIGHT, 0.5)}" stroke-width="0.8"/>'
    blobs_o, blobs_f, blobs_s = "", "", ""
    for px, py, sc in pts:
        for j in range(5):
            a = rng.uniform(0, 6.28)
            r0 = 0 if j == 0 else rng.uniform(1.3, 2.1) * sc
            bx, by = px + math.cos(a) * r0 * 1.15, py + math.sin(a) * r0 * 0.6
            r = rng.uniform(1.35, 1.75) * sc
            blobs_o += f'<circle cx="{bx:.2f}" cy="{by:.2f}" r="{r + 1.0:.2f}"/>'
            blobs_f += f'<circle cx="{bx:.2f}" cy="{by:.2f}" r="{r:.2f}"/>'
            blobs_s += f'<circle cx="{bx - 0.35:.2f}" cy="{by - 0.45:.2f}" r="{r * 0.62:.2f}"/>'
    s += f'<g fill="{OUTLINE}">{blobs_o}</g><g fill="#cfdcc0">{blobs_f}</g><g fill="#f7f8ee">{blobs_s}</g>'
    # the single dark floret typical of wild carrot
    if dot:
        s += f'<circle cx="{cx:.2f}" cy="{cy + ry * 0.05:.2f}" r="1.4" fill="#5a1f33" stroke="{OUTLINE}" stroke-width="0.6"/>'
    s += "</g>"
    return s


def stalk(path, w=2.1, col=None):
    col = col or mix(MID, LIGHT, 0.45)
    return (f'<path d="{path}" fill="none" stroke="{OUTLINE}" stroke-width="{w + 2}" stroke-linecap="round"/>'
            f'<path d="{path}" fill="none" stroke="{col}" stroke-width="{w}" stroke-linecap="round"/>'
            f'<path d="{path}" fill="none" stroke="{mix(LIGHT, HILITE, 0.7)}" stroke-width="{w * 0.35}" '
            f'stroke-linecap="round" transform="translate(-0.45,0)" opacity="0.8"/>')


def svg(body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="128" height="128">'
            + body + "</svg>")


# (angle, length, width, shade, droop) — drawn back to front
STAGE0 = [(-12, 25, 8.5, 0.3, 0.1), (30, 23, 8.5, 0.4, 0.1), (-60, 24, 8.5, 0.55, 0.2),
          (62, 24, 8.5, 0.65, 0.2), (-34, 20, 8, 0.85, 0.2), (10, 18, 8, 1.0, 0.1)]
STAGE1 = [(-6, 41, 11.5, 0.25, 0.05), (28, 38, 11.5, 0.3, 0.1), (-76, 33, 10, 0.4, 0.2), (76, 34, 10, 0.4, 0.2),
          (-38, 39, 12, 0.5, 0.15), (54, 36, 11.5, 0.6, 0.2), (-58, 29, 10.5, 0.8, 0.22),
          (-20, 27, 10.5, 0.9, 0.1), (20, 24, 10, 1.0, 0.1)]
STAGE2 = [(-14, 49, 13, 0.22, 0.05), (22, 47, 13, 0.28, 0.1), (-80, 43, 11.5, 0.35, 0.18), (80, 44, 11.5, 0.35, 0.18),
          (-38, 48, 13.5, 0.45, 0.15), (44, 47, 13.5, 0.5, 0.15), (-61, 42, 12.5, 0.62, 0.22), (63, 40, 12.5, 0.66, 0.22)]
STAGE2_FRONT = [(-40, 30, 11.5, 0.82, 0.22), (-14, 28, 11, 0.95, 0.1), (24, 26, 10.5, 1.0, 0.1), (48, 27, 10.5, 0.85, 0.22)]


def stage0():
    return svg(shadow(22, 4.5) + crown() + rosette(STAGE0, 3))


def stage1():
    return svg(shadow(32, 6) + crown() + rosette(STAGE1, 11))


def stage2():
    rng = random.Random(5)
    x, y = BASE
    main = f"M{x},{y - 2} C{x - 3},{y - 40} {x + 4},{y - 70} {x + 1},{y - 86}"
    side = f"M{x + 0.5},{y - 46} C{x + 6},{y - 54} {x + 15},{y - 58} {x + 19},{y - 68}"
    body = shadow(42, 7.5) + crown() + rosette(STAGE2, 21)
    body += stalk(side, 1.6)
    body += stalk(main, 2.2)
    # small stem leaf at the fork
    body += frond(-62, 15, 5.5, 0.6, rng, droop=0.3, pairs=3, base=(x - 0.3, y - 42), t0=0.3)
    body += umbel(x + 19.5, y - 76, 8.5, 3.6, rng, rays=6, top=(x + 19, y - 68), dot=False)
    body += umbel(x + 1, y - 96.5, 17.5, 6.0, rng, rays=12, top=(x + 1, y - 86))
    body += rosette(STAGE2_FRONT, 22)
    return svg(body)


os.makedirs(OUT, exist_ok=True)
for name, fn in (("wild_carrot_0", stage0), ("wild_carrot_1", stage1), ("wild_carrot_2", stage2)):
    p = os.path.join(OUT, name + ".svg")
    open(p, "w").write(fn())
    subprocess.run(["rsvg-convert", "-w", "128", "-h", "128", p, "-o", os.path.join(OUT, name + ".png")], check=True)
    subprocess.run(["rsvg-convert", "-w", "512", "-h", "512", p, "-o", os.path.join(OUT, name + "_x4.png")], check=True)
