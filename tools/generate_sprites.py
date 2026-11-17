#!/usr/bin/env python3
"""
Procedural sprite generator: draws every world sprite of the game and writes
them as PNG files into assets/sprites/.

The scale is 128 pixels per tile. Every sprite is anchored by the center of its
bottom edge, which the game places on the bottom center of the tile it stands
on (characters: on their feet), so a sprite larger than 128x128 overflows the
tile upward and to both sides. Canvas sizes are multiples of 32 so that the
game can pack them into a texture atlas and shrink it by halves.

Shapes are drawn 4 times larger and averaged down, which antialiases them.
Hair and beards are drawn in light grays: the game tints them with the hair
color of each character.

Usage: python3 tools/generate_sprites.py [--output DIR] [--preview FILE]
(--preview also writes FILE_ground with the tile textures.)
Requires Pillow and numpy. The output is deterministic.
"""

import argparse
import math
import os
import random

import numpy as np
from PIL import Image, ImageDraw

SUPERSAMPLING = 4
TILE = 128

# ---------------------------------------------------------------------------
# Drawing helpers. Coordinates are in final pixels (floats); canvases store
# premultiplied colors at SUPERSAMPLING times the final size.
# ---------------------------------------------------------------------------


def rgb(color):
    return np.array(color[:3], dtype=np.float32) / 255.0


def mix(a, b, t):
    return a * (1.0 - t) + b * t


class Canvas:
    def __init__(self, width, height):
        self.width, self.height = width, height
        self.big_w, self.big_h = width * SUPERSAMPLING, height * SUPERSAMPLING
        self.color = np.zeros((self.big_h, self.big_w, 3), dtype=np.float32)
        self.alpha = np.zeros((self.big_h, self.big_w), dtype=np.float32)

    # -- Masks -------------------------------------------------------------

    def window(self, x0, y0, x1, y1):
        """Clamped high-resolution window covering a box in final pixels,
        with coordinate grids (in final pixels) of its sample centers."""
        s = SUPERSAMPLING
        bx0 = max(0, int(math.floor(x0 * s)) - 2)
        by0 = max(0, int(math.floor(y0 * s)) - 2)
        bx1 = min(self.big_w, int(math.ceil(x1 * s)) + 2)
        by1 = min(self.big_h, int(math.ceil(y1 * s)) + 2)
        if bx1 <= bx0 or by1 <= by0:
            return None
        xs = (np.arange(bx0, bx1, dtype=np.float32) + 0.5) / s
        ys = (np.arange(by0, by1, dtype=np.float32) + 0.5) / s
        grid_x, grid_y = np.meshgrid(xs, ys)
        return (slice(by0, by1), slice(bx0, bx1)), grid_x, grid_y

    def ellipse_mask(self, cx, cy, rx, ry):
        window = self.window(cx - rx, cy - ry, cx + rx, cy + ry)
        if window is None:
            return None
        where, gx, gy = window
        d = np.sqrt(((gx - cx) / rx) ** 2 + ((gy - cy) / ry) ** 2)
        # Soft edge one high-resolution sample wide.
        edge = 1.0 / (SUPERSAMPLING * max(min(rx, ry), 0.5))
        mask = np.clip((1.0 - d) / edge + 0.5, 0.0, 1.0)
        return where, mask, gx, gy

    def pil_mask(self, draw_fn):
        """Full-size mask drawn with PIL; draw_fn(draw, scale) draws in white."""
        image = Image.new("L", (self.big_w, self.big_h), 0)
        draw_fn(ImageDraw.Draw(image), SUPERSAMPLING)
        return np.asarray(image, dtype=np.float32) / 255.0

    def polygon_mask(self, points):
        def draw(d, s):
            d.polygon([(x * s, y * s) for x, y in points], fill=255)
        return self.pil_mask(draw)

    def line_mask(self, points, width):
        def draw(d, s):
            scaled = [(x * s, y * s) for x, y in points]
            d.line(scaled, fill=255, width=max(1, int(round(width * s))), joint="curve")
            radius = width * s / 2.0
            for x, y in (scaled[0], scaled[-1]):
                d.ellipse((x - radius, y - radius, x + radius, y + radius), fill=255)
        return self.pil_mask(draw)

    def full_grids(self):
        xs = (np.arange(self.big_w, dtype=np.float32) + 0.5) / SUPERSAMPLING
        ys = (np.arange(self.big_h, dtype=np.float32) + 0.5) / SUPERSAMPLING
        return np.meshgrid(xs, ys)

    # -- Painting ----------------------------------------------------------

    def paint(self, where, mask, color, clip=None):
        if clip is not None:
            mask = mask * clip[where]
        m = mask[..., None]
        color = np.asarray(color, dtype=np.float32)
        self.color[where] = color * m + self.color[where] * (1.0 - m)
        self.alpha[where] = mask + self.alpha[where] * (1.0 - mask)

    def paint_full(self, mask, color, clip=None):
        self.paint((slice(None), slice(None)), mask, color, clip)

    def erase_full(self, mask):
        keep = 1.0 - mask
        self.color *= keep[..., None]
        self.alpha *= keep

    def ellipse(self, cx, cy, rx, ry, color, light=0.0, clip=None, opacity=1.0):
        """Ellipse, optionally shaded like a lit dome (light from the upper left)."""
        result = self.ellipse_mask(cx, cy, rx, ry)
        if result is None:
            return
        where, mask, gx, gy = result
        base = rgb(color) if len(color) >= 3 and max(color) > 1.0 else np.asarray(color)
        if light:
            u = (gx - cx) / rx
            v = (gy - cy) / ry
            factor = 1.0 + light * (-0.55 * u - 0.8 * v)
            shaded = np.clip(base[None, None, :] * factor[..., None], 0.0, 1.0)
        else:
            shaded = base
        self.paint(where, mask * opacity, shaded, clip)

    def polygon(self, points, color, clip=None, opacity=1.0):
        self.paint_full(self.polygon_mask(points) * opacity, rgb(color), clip)

    def line(self, points, width, color, clip=None, opacity=1.0):
        self.paint_full(self.line_mask(points, width) * opacity, rgb(color), clip)

    def coverage(self):
        return self.alpha.copy()

    # -- Finishing ---------------------------------------------------------

    def outline(self, width, color, opacity=1.0):
        """Dark silhouette edge behind everything drawn so far."""
        grown = self.alpha.copy()
        steps = int(round(width * SUPERSAMPLING))
        for step in range(steps):
            padded = np.pad(grown, 1)
            shifted = [padded[1:-1, :-2], padded[1:-1, 2:], padded[:-2, 1:-1], padded[2:, 1:-1]]
            if step % 2 == 1:  # Alternate with diagonals: rounder corners.
                shifted += [padded[:-2, :-2], padded[:-2, 2:], padded[2:, :-2], padded[2:, 2:]]
            for other in shifted:
                np.maximum(grown, other, out=grown)
        grown *= opacity
        base = rgb(color)
        # The drawing goes over the outline layer.
        a = self.alpha
        self.color = self.color + base[None, None, :] * (grown * (1.0 - a))[..., None]
        self.alpha = a + grown * (1.0 - a)

    def to_image(self):
        s = SUPERSAMPLING
        h, w = self.height, self.width
        color = self.color.reshape(h, s, w, s, 3).mean(axis=(1, 3))
        alpha = self.alpha.reshape(h, s, w, s).mean(axis=(1, 3))
        straight = np.where(alpha[..., None] > 1e-4, color / np.maximum(alpha[..., None], 1e-4), 0)
        straight = bleed(np.clip(straight, 0.0, 1.0), alpha)
        pixels = np.dstack([straight, alpha])
        return Image.fromarray(np.round(pixels * 255.0).astype(np.uint8), "RGBA")


def bleed(color, alpha, steps=6):
    """Spreads the color of visible pixels into the transparent ones around
    them, so that filtering at the edges never mixes in black."""
    known = alpha > 1e-4
    color = color.copy()
    for _ in range(steps):
        padded = np.pad(color, ((1, 1), (1, 1), (0, 0)))
        padded_known = np.pad(known, 1).astype(np.float32)
        total = np.zeros_like(color)
        count = np.zeros(known.shape, dtype=np.float32)
        for dy in (0, 1, 2):
            for dx in (0, 1, 2):
                weight = padded_known[dy:dy + known.shape[0], dx:dx + known.shape[1]]
                total += padded[dy:dy + known.shape[0], dx:dx + known.shape[1]] * weight[..., None]
                count += weight
        grow = (~known) & (count > 0)
        color[grow] = total[grow] / count[grow][..., None]
        known = known | grow
    return color


def shade(color, factor):
    return tuple(max(0, min(255, int(round(c * factor)))) for c in color[:3])


# ---------------------------------------------------------------------------
# Plants
# ---------------------------------------------------------------------------

TREE_TRUNK = (112, 78, 50)
TREE_LEAVES = (64, 120, 52)
TREE_LEAVES_DARK = (38, 82, 40)
TREE_LEAVES_LIGHT = (122, 170, 74)
TREE_OUTLINE = (24, 38, 22)
GROUND_SHADOW = (0, 0, 0)

# Canvas (width, height) and scale of each growth stage of the saplings: from
# TREE_VOLUME_MIN_STAGE (src/core/tree.h) on, a tree takes room in the map (a
# trunk of wood and a floor of foliage, drawn as tiles), not a sprite.
TREE_SAPLING_STAGES = 2
TREE_STAGES = [((128, 128), 0.24), ((128, 192), 0.42)]


def foliage(canvas, blobs, scale, rng, base, dark, light, clip_extra=None):
    """Clumps of leaves: shaded blobs, then small leaf clusters over them."""
    # Dark underside first, offset down, so every clump has a shadow below it.
    for cx, cy, r in blobs:
        canvas.ellipse(cx, cy + r * 0.12, r, r * 0.92, dark, light=0.2)
    for cx, cy, r in blobs:
        canvas.ellipse(cx, cy - r * 0.05, r * 0.94, r * 0.84, base, light=0.45)
    clip = canvas.coverage()
    if clip_extra is not None:
        clip = clip * clip_extra
    xs = [b[0] for b in blobs]
    ys = [b[1] for b in blobs]
    rs = [b[2] for b in blobs]
    left, right = min(x - r for x, r in zip(xs, rs)), max(x + r for x, r in zip(xs, rs))
    top, bottom = min(y - r for y, r in zip(ys, rs)), max(y + r for y, r in zip(ys, rs))
    count = int(200 * scale * scale) + 16
    leaf = max(2.2, 7.5 * scale)
    for _ in range(count):
        x = rng.uniform(left, right)
        y = rng.uniform(top, bottom)
        # Light from the upper left: lighter leaves there, darker below right.
        t = ((x - left) / max(right - left, 1) * 0.45 + (y - top) / max(bottom - top, 1) * 0.55)
        t += rng.uniform(-0.25, 0.25)
        if t < 0.38:
            color = light
        elif t > 0.72:
            color = dark
        else:
            color = base
        rx = leaf * rng.uniform(0.7, 1.25)
        canvas.ellipse(x, y, rx, rx * rng.uniform(0.6, 0.85), color, light=0.15, clip=clip,
                       opacity=0.8)


def draw_tree(stage):
    """A sapling (the grown trees are tiles of trunk and foliage)."""
    (width, height), scale = TREE_STAGES[stage]
    canvas = Canvas(width, height)
    rng = random.Random(1000 + stage)
    cx = width / 2.0
    base_y = height - 12.0
    trunk_w = max(3.0, 24.0 * scale)
    trunk_h = 190.0 * scale
    top_y = base_y - trunk_h
    # Ground shadow (its own layer so that the outline does not wrap it).
    shadow = Canvas(width, height)
    shadow.ellipse(cx, base_y + 1, 70 * scale + 8, 16 * scale + 3, (0, 0, 0))
    # Trunk, slightly tapering, with a root flare.
    canvas.polygon([(cx - trunk_w * 0.5, base_y), (cx - trunk_w * 0.32, top_y),
                    (cx + trunk_w * 0.32, top_y), (cx + trunk_w * 0.5, base_y)], TREE_TRUNK)
    # Crown: overlapping clumps.
    crown_y = top_y - 20 * scale
    if stage == 0:
        layout = [(0, -20, 34), (-26, 6, 26), (26, 4, 26)]
    else:
        layout = [(0, -40, 50), (-44, -6, 44), (44, -6, 44), (0, 12, 52), (-18, -70, 36)]
    blobs = [(cx + dx * scale, crown_y + dy * scale, r * scale) for dx, dy, r in layout]
    foliage(canvas, blobs, scale, rng, rgb(TREE_LEAVES), rgb(TREE_LEAVES_DARK),
            rgb(TREE_LEAVES_LIGHT))
    canvas.outline(max(1.2, 2.2 * min(1.0, scale + 0.3)), TREE_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.28)


# Canvas (width, height) and scale of each growth stage of the shrubs.
HAZEL_STAGES = [((128, 128), 0.34), ((128, 160), 0.52), ((128, 192), 0.7),
                ((160, 224), 0.86), ((192, 256), 1.0)]
# A hazel is a small tree: only its seedling stages are sprites (growth stages
# below the one where it takes room, see core/tree.h).
HAZEL_SEEDLING_STAGES = 2
BRAMBLE_STAGES = [((128, 128), 0.34), ((128, 128), 0.52), ((128, 128), 0.7),
                  ((160, 128), 0.86), ((192, 160), 1.0)]
# Low plants are drawn in three sizes: young, growing, grown.
HERB_SCALES = [0.5, 0.75, 1.0]

HAZEL_LEAVES = (96, 146, 62)
HAZEL_LEAVES_DARK = (54, 98, 44)
HAZEL_LEAVES_LIGHT = (150, 190, 92)
HAZEL_BARK = (128, 104, 82)
HAZELNUT = (176, 132, 70)
HAZELNUT_HUSK = (150, 172, 88)
BRAMBLE_LEAVES = (58, 104, 50)
BRAMBLE_LEAVES_DARK = (32, 66, 36)
BRAMBLE_LEAVES_LIGHT = (104, 146, 72)
BRAMBLE_CANE = (118, 54, 62)
BLACKBERRY = (52, 24, 52)
BLACKBERRY_UNRIPE = (176, 40, 52)
PLANT_OUTLINE = (24, 40, 22)


def scatter_on(canvas, rng, box, count, draw_fn, attempts=4000):
    """Calls draw_fn(x, y) at up to `count` random spots of `box` well inside
    what is drawn so far (fruit on the foliage)."""
    clip = canvas.coverage()
    x0, y0, x1, y1 = box
    placed = 0
    for _ in range(attempts):
        if placed >= count:
            break
        x, y = rng.uniform(x0, x1), rng.uniform(y0, y1)
        ix, iy = int(x * SUPERSAMPLING), int(y * SUPERSAMPLING)
        if not (0 <= ix < canvas.big_w and 0 <= iy < canvas.big_h):
            continue
        if clip[iy, ix] < 0.99 or clip[max(0, iy - 6 * SUPERSAMPLING), ix] < 0.99:
            continue
        draw_fn(x, y)
        placed += 1


def draw_hazel(stage, nuts):
    """A multi-stemmed shrub: thin grey-brown stems fanning out from the
    ground, round soft leaves; clusters of nuts in their husks in autumn."""
    (width, height), scale = HAZEL_STAGES[stage]
    canvas = Canvas(width, height)
    rng = random.Random(2100 + stage)
    cx = width / 2.0
    base_y = height - 12.0
    shadow = Canvas(width, height)
    shadow.ellipse(cx, base_y + 2, 64 * scale + 6, 15 * scale + 3, (0, 0, 0))
    top = base_y - 210 * scale
    for i, dx in enumerate((-30, -16, -4, 8, 20, 32)):
        lean = dx * 1.6 * scale
        canvas.line([(cx + dx * 0.25 * scale, base_y + 2),
                     (cx + dx * 0.6 * scale + lean * 0.3, base_y - 100 * scale),
                     (cx + lean, top + 40 * scale + (i % 2) * 18 * scale)],
                    max(1.5, 5.5 * scale), shade(HAZEL_BARK, 0.9 + 0.08 * (i % 3)))
    layout = [(0, -190, 44), (-40, -160, 42), (40, -164, 40), (-62, -112, 36), (62, -116, 36),
              (-20, -128, 46), (24, -124, 44), (0, -84, 38), (-46, -70, 28), (46, -72, 28)]
    if stage <= 1:
        layout = [(0, -120, 42), (-38, -84, 36), (38, -86, 36), (0, -60, 34)]
    blobs = [(cx + dx * scale, base_y + dy * scale, r * scale) for dx, dy, r in layout]
    foliage(canvas, blobs, scale, rng, rgb(HAZEL_LEAVES), rgb(HAZEL_LEAVES_DARK),
            rgb(HAZEL_LEAVES_LIGHT))
    if nuts:
        nut_rng = random.Random(3100 + stage)
        radius = max(2.4, 5.0 * scale)

        def nut_cluster(x, y):
            for k in range(nut_rng.choice((2, 3))):
                nx = x + (k - 1) * radius * 1.5
                ny = y + (k % 2) * radius * 0.6
                canvas.ellipse(nx, ny - radius * 0.5, radius * 1.2, radius * 0.9,
                               rgb(HAZELNUT_HUSK), light=0.4)
                canvas.ellipse(nx, ny + radius * 0.3, radius, radius * 1.05, rgb(HAZELNUT),
                               light=0.6)
                canvas.ellipse(nx - radius * 0.3, ny, radius * 0.28, radius * 0.25,
                               (1.0, 0.95, 0.85), opacity=0.7)

        scatter_on(canvas, nut_rng, (cx - 70 * scale, base_y - 210 * scale, cx + 70 * scale,
                                     base_y - 60 * scale), int(4 + 8 * scale), nut_cluster)
    canvas.outline(max(1.2, 2.0 * min(1.0, scale + 0.3)), PLANT_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.25)


def draw_bramble(stage, berries):
    """A low sprawling thicket: arching reddish canes, dark leaves; clusters
    of blackberries, a few still red, at the end of summer."""
    (width, height), scale = BRAMBLE_STAGES[stage]
    canvas = Canvas(width, height)
    rng = random.Random(2000 + stage)
    cx = width / 2.0
    base_y = height - 14.0
    shadow = Canvas(width, height)
    shadow.ellipse(cx, base_y + 2, 80 * scale + 4, 14 * scale + 3, (0, 0, 0))
    layout = [(0, -56, 38), (-44, -38, 36), (44, -40, 36), (-76, -14, 26), (76, -16, 26),
              (-22, -20, 40), (24, -18, 40), (0, -84, 24)]
    blobs = [(cx + dx * scale, base_y + dy * scale, r * scale) for dx, dy, r in layout]
    foliage(canvas, blobs, scale, rng, rgb(BRAMBLE_LEAVES), rgb(BRAMBLE_LEAVES_DARK),
            rgb(BRAMBLE_LEAVES_LIGHT))
    # Canes arching out of the thicket and down to the ground, with thorns.
    for side, start, reach, rise in ((-1, 0.2, 92, 46), (1, 0.1, 96, 40), (-1, 0.5, 70, 30),
                                     (1, 0.45, 76, 34)):
        points = []
        for i in range(9):
            t = i / 8.0
            x = cx + side * (reach * (start + (1.0 - start) * t)) * scale
            y = base_y - 8 * scale - rise * scale * math.sin(math.pi * (0.35 + 0.65 * t))
            y += rng.uniform(-1.5, 1.5) * scale
            points.append((x, y))
        canvas.line(points, max(1.4, 3.6 * scale), BRAMBLE_CANE)
        for x, y in points[1:-1:2]:
            canvas.line([(x, y), (x + side * 3 * scale, y - 5 * scale)], max(1.0, 1.6 * scale),
                        shade(BRAMBLE_CANE, 0.7))
    if berries:
        berry_rng = random.Random(3000 + stage)
        radius = max(2.2, 4.8 * scale)

        def berry_cluster(x, y):
            for k in range(berry_rng.choice((2, 3, 3))):
                bx = x + (k - 1) * radius * 1.4
                by = y + (k % 2) * radius * 0.9
                ripe = berry_rng.random() < 0.8
                color = BLACKBERRY if ripe else BLACKBERRY_UNRIPE
                canvas.ellipse(bx, by, radius, radius * 1.1, rgb(color), light=0.5)
                # The drupelets: small highlights.
                for _ in range(3):
                    canvas.ellipse(bx + berry_rng.uniform(-0.5, 0.3) * radius,
                                   by + berry_rng.uniform(-0.6, 0.3) * radius, radius * 0.22,
                                   radius * 0.22, (0.85, 0.8, 0.95) if ripe else (1.0, 0.7, 0.7),
                                   opacity=0.6)

        scatter_on(canvas, berry_rng, (cx - 84 * scale, base_y - 100 * scale, cx + 84 * scale,
                                       base_y - 6 * scale), int(6 + 18 * scale), berry_cluster)
    canvas.outline(max(1.2, 2.0 * min(1.0, scale + 0.3)), PLANT_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.25)


def leaf(canvas, base, tip, width, color, vein=None):
    """A pointed leaf from `base` to `tip`, `width` across at its widest."""
    (bx, by), (tx, ty) = base, tip
    dx, dy = tx - bx, ty - by
    length = max(math.hypot(dx, dy), 1e-3)
    nx, ny = -dy / length, dx / length
    points = []
    for i in range(11):
        t = i / 10.0
        half = width * 0.5 * math.sin(math.pi * t) ** 0.8
        points.append((bx + dx * t + nx * half, by + dy * t + ny * half))
    for i in range(10, -1, -1):
        t = i / 10.0
        half = width * 0.5 * math.sin(math.pi * t) ** 0.8
        points.append((bx + dx * t - nx * half, by + dy * t - ny * half))
    canvas.polygon(points, color)
    if vein is not None:
        canvas.line([(bx, by), (bx + dx * 0.85, by + dy * 0.85)], max(0.8, width * 0.08), vein,
                    opacity=0.6)


HERB_W, HERB_H = 128, 128


def herb_canvas():
    canvas = Canvas(HERB_W, HERB_H)
    shadow = Canvas(HERB_W, HERB_H)
    return canvas, shadow, HERB_W / 2.0, HERB_H - 14.0


def draw_wild_garlic(size):
    """Broad bright leaves rising from the ground; white star flowers in
    round heads once grown."""
    scale = HERB_SCALES[size]
    canvas, shadow, cx, base_y = herb_canvas()
    rng = random.Random(4000 + size)
    shadow.ellipse(cx, base_y + 2, 44 * scale + 4, 10 * scale + 2, (0, 0, 0))
    green, dark = (86, 168, 72), (52, 120, 48)
    for i in range(9):
        angle = math.radians(-150 + i * 15 + rng.uniform(-6, 6))
        length = rng.uniform(46, 62) * scale
        tip = (cx + math.cos(angle) * length, base_y + math.sin(angle) * length * 0.95)
        base = (cx + (i - 4) * 2.0 * scale, base_y)
        leaf(canvas, base, tip, 15 * scale, dark if i % 3 == 0 else green, (170, 210, 140))
    if size == 2:
        for k in range(3):
            fx = cx + (k - 1) * 22 * scale + rng.uniform(-4, 4)
            fy = base_y - rng.uniform(62, 74) * scale
            canvas.line([(fx, base_y - 6), (fx, fy)], 2.0, (110, 160, 80))
            for _ in range(7):
                canvas.ellipse(fx + rng.uniform(-6, 6), fy + rng.uniform(-6, 5), 2.6, 2.6,
                               (0.97, 0.97, 0.94))
    canvas.outline(1.2, PLANT_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.2)


def draw_nettle(size):
    """Upright stems with pairs of toothed dark leaves, smaller towards the top."""
    scale = HERB_SCALES[size]
    canvas, shadow, cx, base_y = herb_canvas()
    rng = random.Random(4100 + size)
    shadow.ellipse(cx, base_y + 2, 38 * scale + 4, 9 * scale + 2, (0, 0, 0))
    green, dark = (64, 122, 56), (40, 86, 42)
    for stem in range(3):
        sx = cx + (stem - 1) * 24 * scale
        height = rng.uniform(80, 100) * scale
        canvas.line([(sx, base_y), (sx + rng.uniform(-3, 3), base_y - height)],
                    max(1.2, 2.6 * scale), (76, 104, 52))
        for pair in range(5):
            t = 0.2 + pair * 0.18
            y = base_y - height * t
            length = (30 - pair * 4) * scale
            for side in (-1, 1):
                leaf(canvas, (sx, y), (sx + side * length, y - length * 0.45),
                     length * 0.62, dark if (pair + side) % 2 else green, (110, 150, 90))
        leaf(canvas, (sx, base_y - height), (sx, base_y - height - 12 * scale), 8 * scale, green)
    canvas.outline(1.2, PLANT_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.2)


def draw_plantain(size):
    """A flat rosette of narrow ribbed leaves; flower spikes on bare stalks
    once grown."""
    scale = HERB_SCALES[size]
    canvas, shadow, cx, base_y = herb_canvas()
    rng = random.Random(4200 + size)
    shadow.ellipse(cx, base_y - 4, 48 * scale + 4, 14 * scale + 2, (0, 0, 0))
    green, dark = (110, 150, 70), (72, 112, 52)
    center_y = base_y - 10 * scale
    for i in range(12):
        angle = math.radians(i * 30 + rng.uniform(-8, 8))
        length = rng.uniform(40, 52) * scale
        tip = (cx + math.cos(angle) * length, center_y + math.sin(angle) * length * 0.4)
        leaf(canvas, (cx, center_y), tip, 9 * scale, dark if i % 2 else green, (170, 200, 130))
    if size == 2:
        for k in range(2):
            fx = cx + (k * 2 - 1) * 10
            top = center_y - rng.uniform(64, 76)
            canvas.line([(cx, center_y), (fx, top)], 1.6, (130, 140, 80))
            canvas.ellipse(fx, top - 5, 3.6, 7.5, (96, 72, 46), light=0.4)
            for _ in range(6):  # Stamens: a pale ring.
                canvas.ellipse(fx + rng.uniform(-4, 4), top - 5 + rng.uniform(-3, 3), 0.9, 0.9,
                               (0.95, 0.92, 0.75))
    canvas.outline(1.2, PLANT_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.2)


def draw_wild_carrot(size):
    """Feathery, finely cut leaves; a flat white flower head on a stalk once
    grown (the second year)."""
    scale = HERB_SCALES[size]
    canvas, shadow, cx, base_y = herb_canvas()
    rng = random.Random(4300 + size)
    shadow.ellipse(cx, base_y + 2, 40 * scale + 4, 9 * scale + 2, (0, 0, 0))
    green = (96, 150, 70)
    for i in range(7):
        angle = math.radians(-160 + i * 23 + rng.uniform(-5, 5))
        length = rng.uniform(36, 48) * scale
        x1 = cx + math.cos(angle) * length
        y1 = base_y + math.sin(angle) * length
        canvas.line([(cx, base_y), (x1, y1)], max(1.0, 1.8 * scale), (90, 130, 64))
        for j in range(8):  # Tiny leaflets along the stalk.
            t = 0.3 + j * 0.09
            px, py = cx + (x1 - cx) * t, base_y + (y1 - base_y) * t
            for side in (-1, 1):
                canvas.ellipse(px + side * 3.2 * scale, py - 1.5 * scale, 3.0 * scale,
                               1.6 * scale, shade(green, 0.85 + 0.1 * (j % 3)))
    if size == 2:
        top = base_y - 92
        canvas.line([(cx, base_y), (cx + 3, top)], 2.0, (110, 140, 80))
        for _ in range(60):
            angle = rng.uniform(0, 2 * math.pi)
            r = rng.uniform(0, 16) ** 0.9
            canvas.ellipse(cx + 3 + math.cos(angle) * r, top + math.sin(angle) * r * 0.4,
                           1.8, 1.4, (0.98, 0.98, 0.95))
        canvas.ellipse(cx + 3, top, 1.6, 1.4, (0.4, 0.15, 0.25))  # The dark central flower.
    canvas.outline(1.2, PLANT_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.2)


CATTAIL_W, CATTAIL_H = 128, 192


def draw_cattail(size):
    """Tall upright blades; brown cylindrical heads on stiff stalks once grown."""
    scale = HERB_SCALES[size]
    canvas = Canvas(CATTAIL_W, CATTAIL_H)
    shadow = Canvas(CATTAIL_W, CATTAIL_H)
    rng = random.Random(4400 + size)
    cx, base_y = CATTAIL_W / 2.0, CATTAIL_H - 12.0
    shadow.ellipse(cx, base_y + 2, 40 * scale + 4, 9 * scale + 2, (0, 0, 0))
    green, dark = (110, 146, 70), (70, 108, 54)
    for i in range(11):
        x0 = cx + (i - 5) * 4.5 * scale
        height = rng.uniform(110, 160) * scale
        lean = (i - 5) * rng.uniform(2, 5) * scale
        leaf(canvas, (x0, base_y), (x0 + lean, base_y - height), 7 * scale,
             dark if i % 3 == 0 else green)
    if size == 2:
        for k in range(3):
            x = cx + (k - 1) * 16 + rng.uniform(-3, 3)
            top = base_y - rng.uniform(150, 168)
            canvas.line([(x, base_y), (x, top - 18)], 2.0, (120, 140, 80))
            canvas.ellipse(x, top + 14, 5.0, 16.0, (104, 64, 40), light=0.5)
    canvas.outline(1.2, PLANT_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.2)


def draw_mushrooms():
    """A few brown caps on pale stems, in the leaf litter."""
    canvas, shadow, cx, base_y = herb_canvas()
    rng = random.Random(4500)
    shadow.ellipse(cx, base_y + 2, 34, 8, (0, 0, 0))
    for x, y, r in ((-18, -2, 12), (14, -6, 15), (-2, 6, 9), (30, 6, 8)):
        x, y = cx + x, base_y + y
        canvas.polygon([(x - r * 0.3, y), (x - r * 0.25, y - r * 1.4), (x + r * 0.25, y - r * 1.4),
                        (x + r * 0.3, y)], (232, 222, 200))
        canvas.ellipse(x, y - r * 1.45, r, r * 0.6, (168, 116, 70), light=0.6)
        for _ in range(3):
            canvas.ellipse(x + rng.uniform(-0.5, 0.5) * r, y - r * 1.6, r * 0.12, r * 0.08,
                           (0.92, 0.86, 0.74), opacity=0.7)
    canvas.outline(1.2, PLANT_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.2)


# Low plants: file name and drawing function (three sizes, except mushrooms).
HERBS = [("wild_garlic", draw_wild_garlic), ("nettle", draw_nettle),
         ("plantain", draw_plantain), ("wild_carrot", draw_wild_carrot),
         ("cattail", draw_cattail)]


def compose(shadow, canvas, shadow_opacity):
    """Ground shadow under the sprite."""
    result = Canvas(canvas.width, canvas.height)
    result.paint_full(shadow.alpha * shadow_opacity, np.zeros(3, dtype=np.float32))
    result.color = canvas.color + result.color * (1.0 - canvas.alpha)[..., None]
    result.alpha = canvas.alpha + result.alpha * (1.0 - canvas.alpha)
    return result


# ---------------------------------------------------------------------------
# Animals (side view, facing right; the game mirrors them). One sprite per
# species, sex and pose: standing, eating (head down) and asleep (lying).
# The feet are on the bottom center of the canvas, like the characters'.
# ---------------------------------------------------------------------------

ANIMAL_OUTLINE = (34, 26, 20)
ANIMAL_POSES = ["stand", "eat", "sleep"]
ANIMAL_SEXES = ["female", "male"]
ANIMAL_GROUND = 10.0  # Pixels under the feet (the shadow).
ANTLER = (206, 190, 160)


def aim(angle):
    """Unit vector at `angle` degrees: 0 = forward (right), 90 = up."""
    rad = math.radians(angle)
    return math.cos(rad), -math.sin(rad)


def reach(point, angle, length):
    dx, dy = aim(angle)
    return point[0] + dx * length, point[1] + dy * length


def taper(canvas, start, end, start_width, end_width, color, clip=None):
    """A limb from `start` to `end`, getting thinner (or thicker), round at both ends."""
    dx, dy = end[0] - start[0], end[1] - start[1]
    length = max(math.hypot(dx, dy), 1e-3)
    nx, ny = -dy / length, dx / length
    a, b = start_width * 0.5, end_width * 0.5
    canvas.polygon([(start[0] + nx * a, start[1] + ny * a), (end[0] + nx * b, end[1] + ny * b),
                    (end[0] - nx * b, end[1] - ny * b), (start[0] - nx * a, start[1] - ny * a)],
                   color, clip=clip)
    canvas.ellipse(start[0], start[1], a, a, color, clip=clip)
    canvas.ellipse(end[0], end[1], b, b, color, clip=clip)


def leg(canvas, top, ground, length, width, color, bend, hoof):
    """A leg from the body down to the ground, bent at the joint, thinner
    below it, on a dark hoof."""
    knee = (top[0] + bend, top[1] + length * 0.5)
    foot = (top[0] + bend * 0.3, ground - width * 0.25)
    taper(canvas, top, knee, width * 1.25, width * 0.72, color)
    taper(canvas, knee, foot, width * 0.72, width * 0.56, color)
    canvas.ellipse(foot[0] + width * 0.12, ground - width * 0.3, width * 0.42, width * 0.34, hoof)


def ear(canvas, base, angle, length, width, color):
    """A pointed ear: a leaf from its base to its tip."""
    middle = reach(base, angle, length * 0.45)
    canvas.polygon([base, reach(middle, angle + 90, width * 0.5), reach(base, angle, length),
                    reach(middle, angle - 90, width * 0.5)], color)


def antlers(canvas, base, size, points, angle):
    """A beam leaning back from the head with tines pointing up and forward."""
    beam = [base, reach(base, angle + 25, size * 0.45), reach(base, angle + 5, size)]
    canvas.line(beam, max(2.0, size * 0.07), ANTLER)
    for i in range(points):
        t = (i + 0.6) / (points + 0.4)
        start = (beam[0][0] + (beam[2][0] - beam[0][0]) * t,
                 beam[0][1] + (beam[2][1] - beam[0][1]) * t - size * 0.08 * math.sin(math.pi * t))
        canvas.line([start, reach(start, angle - 55, size * (0.34 - 0.05 * i))],
                    max(1.6, size * 0.05), ANTLER)


def draw_deer(spec, male, pose):
    """Roe deer and red deer: long legs, a long neck, a short tail over a
    pale rump; the males carry antlers."""
    width, height = spec["canvas"]
    k = spec["size"] * (1.0 if male else spec["female_size"])
    coat, belly, dark = spec["coat"], spec["belly"], shade(spec["coat"], 0.62)
    canvas, shadow = Canvas(width, height), Canvas(width, height)
    ground = height - ANIMAL_GROUND
    body_len, body_h, leg_len = spec["body"][0] * k, spec["body"][1] * k, spec["leg"] * k
    lying = pose == "sleep"
    cx = width * 0.5 - body_len * 0.1
    cy = ground - body_h * 0.5 - (body_h * 0.12 if lying else leg_len)
    shadow.ellipse(cx + body_len * 0.05, ground, body_len * 0.6, 6.0 * k + 3.0, (0, 0, 0))
    front_x, hind_x = cx + body_len * 0.32, cx - body_len * 0.34
    leg_w = spec["leg_width"] * k
    if not lying:  # The legs of the far side, darker.
        leg(canvas, (front_x + leg_w, cy), ground - 2, leg_len + body_h * 0.4, leg_w, dark,
            leg_w * 0.4, shade(dark, 0.5))
        leg(canvas, (hind_x + leg_w * 1.6, cy), ground - 2, leg_len + body_h * 0.4, leg_w, dark,
            -leg_w * 0.8, shade(dark, 0.5))
    # Trunk: the barrel, a deeper chest, the haunch.
    canvas.ellipse(cx, cy, body_len * 0.5, body_h * 0.5, coat, light=0.22)
    canvas.ellipse(cx + body_len * 0.27, cy + body_h * 0.02, body_len * 0.24, body_h * 0.54, coat,
                   light=0.2)
    canvas.ellipse(cx - body_len * 0.3, cy - body_h * 0.03, body_len * 0.22, body_h * 0.52, coat,
                   light=0.22)
    trunk = canvas.coverage()
    canvas.ellipse(cx, cy + body_h * 0.42, body_len * 0.4, body_h * 0.16, belly, clip=trunk,
                   opacity=0.6)
    canvas.ellipse(cx - body_len * 0.5, cy - body_h * 0.05, body_len * 0.1, body_h * 0.34,
                   spec["rump"], clip=trunk)
    canvas.line([(cx - body_len * 0.49, cy - body_h * 0.3),
                 (cx - body_len * 0.54, cy - body_h * 0.02)], 3.0 * k + 1.0, spec["tail"])
    if lying:  # Legs folded under it: the knees show.
        canvas.ellipse(front_x, ground - 3, body_len * 0.13, 4.0 * k + 1.5, dark)
        canvas.ellipse(hind_x + body_len * 0.08, ground - 3, body_len * 0.16, 4.5 * k + 1.5, dark)
    else:
        leg(canvas, (front_x, cy + body_h * 0.1), ground, leg_len + body_h * 0.3, leg_w, coat,
            leg_w * 0.3, shade(dark, 0.5))
        leg(canvas, (hind_x, cy + body_h * 0.05), ground, leg_len + body_h * 0.35, leg_w, coat,
            -leg_w * 0.9, shade(dark, 0.5))
    # Neck and head.
    neck_angle, head_angle = {"stand": (58, -8), "eat": (-52, -74), "sleep": (20, -30)}[pose]
    shoulder = (cx + body_len * 0.4, cy - body_h * 0.22)
    neck_len, neck_w = spec["neck"][0] * k, spec["neck"][1] * k
    nape = reach(shoulder, neck_angle, neck_len)
    taper(canvas, reach(shoulder, neck_angle + 180, neck_w * 0.4), nape, neck_w * 1.5,
          neck_w * 0.82, coat)
    head_len, head_w = spec["head"][0] * k, spec["head"][1] * k
    nose = reach(nape, head_angle, head_len)
    # Ears behind the skull, then the head.
    ear(canvas, nape, head_angle + 152, spec["ear"] * k, head_w * 0.5, dark)
    if male and spec["antlers"]:
        size, points = spec["antlers"]
        antlers(canvas, reach(nape, head_angle + 100, head_w * 0.3), size * k, points,
                head_angle + 95)
    ear(canvas, nape, head_angle + 118, spec["ear"] * k, head_w * 0.55, coat)
    taper(canvas, nape, nose, head_w * 0.95, head_w * 0.46, coat)
    canvas.ellipse(nape[0], nape[1], head_w * 0.54, head_w * 0.54, coat, light=0.2)
    canvas.ellipse(nose[0] + head_w * 0.05, nose[1], head_w * 0.2, head_w * 0.2, (40, 30, 28))
    eye = reach(reach(nape, head_angle, head_len * 0.2), head_angle + 90, head_w * 0.14)
    if lying:
        canvas.line([(eye[0] - 2.0 * k, eye[1]), (eye[0] + 2.0 * k, eye[1])], 1.6, (30, 22, 20))
    else:
        canvas.ellipse(eye[0], eye[1], 2.2 * k + 0.4, 2.2 * k + 0.4, (24, 18, 16))
    canvas.outline(2.0, ANIMAL_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.3)


DEER = {
    "roe_deer": {"canvas": (128, 128), "size": 1.0, "female_size": 0.94, "body": (70.0, 26.0),
                 "leg": 34.0, "leg_width": 5.0, "neck": (24.0, 11.0), "head": (17.0, 13.0),
                 "ear": 14.0, "antlers": (20.0, 2), "coat": (176, 112, 62),
                 "belly": (222, 190, 150), "rump": (244, 236, 220), "tail": (244, 236, 220)},
    "red_deer": {"canvas": (224, 192), "size": 1.0, "female_size": 0.88, "body": (124.0, 44.0),
                 "leg": 56.0, "leg_width": 8.0, "neck": (42.0, 19.0), "head": (30.0, 21.0),
                 "ear": 19.0, "antlers": (58.0, 4), "coat": (150, 96, 60),
                 "belly": (196, 160, 122), "rump": (226, 200, 158), "tail": (118, 76, 50)},
}


def draw_boar(male, pose):
    """A wild boar: a massive front, short legs, a long head carried low, a
    ridge of bristles along the back; the males have tusks."""
    width, height = 160, 128
    k = 1.0 if male else 0.88
    coat, dark, light = (86, 70, 58), (50, 40, 34), (122, 104, 88)
    canvas, shadow = Canvas(width, height), Canvas(width, height)
    ground = height - ANIMAL_GROUND
    body_len, body_h, leg_len = 96.0 * k, 46.0 * k, 22.0 * k
    lying = pose == "sleep"
    cx = width * 0.5 - body_len * 0.12
    cy = ground - body_h * 0.5 - (body_h * 0.06 if lying else leg_len)
    shadow.ellipse(cx + body_len * 0.08, ground, body_len * 0.62, 8.0, (0, 0, 0))
    front_x, hind_x = cx + body_len * 0.28, cx - body_len * 0.32
    leg_w = 8.0 * k
    if not lying:
        leg(canvas, (front_x + leg_w, cy), ground - 2, leg_len + body_h * 0.4, leg_w, dark, 1.0,
            (24, 20, 18))
        leg(canvas, (hind_x + leg_w * 1.4, cy), ground - 2, leg_len + body_h * 0.4, leg_w, dark,
            -2.0, (24, 20, 18))
    # Bristles along the back, behind the trunk.
    rng = random.Random(7)
    for i in range(22):
        t = i / 21.0
        x = cx - body_len * 0.42 + body_len * 0.8 * t
        y = cy - body_h * (0.42 + 0.1 * math.sin(math.pi * min(1.0, t * 1.25)))
        canvas.line([(x, y + 5), (x - 3 - rng.random() * 2, y - 5 * k - rng.random() * 3)], 2.4,
                    dark)
    canvas.ellipse(cx - body_len * 0.14, cy + body_h * 0.03, body_len * 0.38, body_h * 0.45, coat,
                   light=0.2)
    canvas.ellipse(cx + body_len * 0.16, cy - body_h * 0.03, body_len * 0.34, body_h * 0.52, coat,
                   light=0.22)
    trunk = canvas.coverage()
    canvas.ellipse(cx, cy + body_h * 0.4, body_len * 0.4, body_h * 0.16, light, clip=trunk,
                   opacity=0.5)
    canvas.line([(cx - body_len * 0.5, cy - body_h * 0.12), (cx - body_len * 0.56, cy + 4 * k),
                 (cx - body_len * 0.53, cy + 12 * k)], 2.6, dark)
    if lying:
        canvas.ellipse(front_x, ground - 3, body_len * 0.12, 5.0, dark)
        canvas.ellipse(hind_x + body_len * 0.08, ground - 3, body_len * 0.15, 5.5, dark)
    else:
        leg(canvas, (front_x, cy + body_h * 0.15), ground, leg_len + body_h * 0.25, leg_w, coat,
            1.0, (24, 20, 18))
        leg(canvas, (hind_x, cy + body_h * 0.1), ground, leg_len + body_h * 0.3, leg_w, coat, -2.5,
            (24, 20, 18))
    # The head: a long wedge from the shoulders down to the snout.
    head_angle = {"stand": -16, "eat": -46, "sleep": -24}[pose]
    skull = (cx + body_len * 0.42, cy - body_h * (0.02 if not lying else -0.1))
    snout = reach(skull, head_angle, 31.0 * k)
    ear(canvas, reach(skull, head_angle + 120, 10.0 * k), head_angle + 135, 15.0 * k, 10.0 * k,
        dark)
    canvas.ellipse(skull[0], skull[1], 20.0 * k, 20.0 * k, coat, light=0.2)
    taper(canvas, skull, snout, 36.0 * k, 13.0 * k, coat)
    ear(canvas, reach(skull, head_angle + 100, 11.0 * k), head_angle + 110, 15.0 * k, 11.0 * k,
        shade(coat, 0.8))
    canvas.ellipse(snout[0] + 1.5 * k, snout[1], 4.5 * k, 6.5 * k, (58, 44, 44))
    if male:
        jaw = reach(reach(skull, head_angle, 23.0 * k), head_angle - 90, 5.0 * k)
        canvas.line([jaw, reach(jaw, head_angle + 60, 6.0), reach(jaw, head_angle + 110, 11.0)],
                    3.0, (240, 232, 210))
    eye = reach(reach(skull, head_angle, 8.0 * k), head_angle + 90, 6.0 * k)
    if lying:
        canvas.line([(eye[0] - 2.5, eye[1]), (eye[0] + 2.5, eye[1])], 1.6, (16, 12, 10))
    else:
        canvas.ellipse(eye[0], eye[1], 2.4, 2.4, (16, 12, 10))
    canvas.outline(2.0, ANIMAL_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.3)


def draw_hare(pose):
    """A hare, drawn larger than life so that it shows: long hind legs folded
    under a round back, long ears with black tips."""
    width, height = 64, 64
    coat, dark, belly = (176, 142, 100), (112, 86, 60), (232, 218, 196)
    canvas, shadow = Canvas(width, height), Canvas(width, height)
    ground = height - 6.0
    lying = pose == "sleep"
    cx = width * 0.5 - 4.0
    back = 11.0 if lying else 14.5
    cy = ground - back
    shadow.ellipse(cx + 3, ground, 21.0, 4.0, (0, 0, 0))
    canvas.ellipse(cx - 13.0, cy + back * 0.1, 3.4, 3.4, (244, 240, 230))  # Scut.
    canvas.ellipse(cx, cy + 1.0, 17.0, back, coat, light=0.22)
    canvas.ellipse(cx - 6.0, cy + back * 0.3, 10.0, back * 0.72, coat, light=0.18)  # Haunch.
    trunk = canvas.coverage()
    canvas.ellipse(cx + 2.0, cy + back * 0.75, 12.0, back * 0.3, belly, clip=trunk, opacity=0.8)
    canvas.line([(cx - 9.0, ground - 2.0), (cx + 2.0, ground - 1.5)], 4.2, dark)  # Hind foot.
    if not lying:
        canvas.line([(cx + 10.0, cy + back * 0.3), (cx + 12.0, ground - 1.5)], 3.6, coat)
    head_angle, ear_angle = {"stand": (4, 80), "eat": (-48, 112), "sleep": (-6, 166)}[pose]
    skull = (cx + 15.0, cy - (2.0 if lying else 7.0) + (7.0 if pose == "eat" else 0.0))
    for offset in (10, -8):
        tip = reach(skull, ear_angle + offset, 19.0)
        canvas.line([reach(skull, ear_angle + offset, 3.0), tip], 4.4, coat)
        canvas.line([reach(skull, ear_angle + offset, 15.0), tip], 4.0, (40, 32, 28))
    nose = reach(skull, head_angle, 8.0)
    canvas.line([skull, nose], 9.5, coat)
    canvas.ellipse(skull[0], skull[1], 7.4, 7.0, coat, light=0.2)
    canvas.ellipse(nose[0] + 1.0, nose[1], 1.6, 1.6, (60, 40, 40))
    eye = reach(reach(skull, head_angle, 2.5), head_angle + 90, 2.2)
    if lying:
        canvas.line([(eye[0] - 1.8, eye[1]), (eye[0] + 1.8, eye[1])], 1.4, (24, 18, 16))
    else:
        canvas.ellipse(eye[0], eye[1], 1.9, 1.9, (24, 18, 16))
    canvas.outline(1.6, ANIMAL_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.3)


def draw_animal(species, male, pose):
    if species in DEER:
        return draw_deer(DEER[species], male, pose)
    if species == "boar":
        return draw_boar(male, pose)
    return draw_hare(pose)


ANIMAL_SPECIES = ["roe_deer", "red_deer", "boar", "hare"]


# ---------------------------------------------------------------------------
# Characters (front view). All layers share one canvas: 128x192, feet at the
# bottom center. Layers: body, head (with the face), beard, hair.
# ---------------------------------------------------------------------------

CHAR_W, CHAR_H = 128, 192
CX = 64.0
HEAD_CY = 80.0
HEAD_RX, HEAD_RY = 27.0, 29.0
FEET_Y = 184.0

SKIN_TONES = [(250, 218, 190), (234, 188, 150), (200, 144, 104), (152, 100, 66), (100, 66, 46)]
CHAR_OUTLINE = (34, 24, 20)
EXPRESSIONS = ["neutral", "smiling", "sad", "angry"]

# Body shapes: (name, half width at the shoulders in pixels, narrowing toward
# the bottom, belly bulge).
BODY_SHAPES = [
    ("thin", 28.0, 0.18, 0.0),
    ("average", 33.0, 0.14, 0.02),
    ("heavy", 37.0, 0.02, 0.12),
]
BODY_TOP, BODY_BOTTOM = 100.0, 180.0

HAIR_BASE = (226, 226, 226)
HAIR_DARK = (150, 150, 150)
HAIR_LIGHT = (255, 255, 255)
HAIR_OUTLINE = (70, 70, 70)


def body_half_width(t, half_width, taper, bulge):
    """Half width of the body at height t (0 = top, 1 = bottom): broad rounded
    shoulders, narrowing toward a rounded bottom, maybe a belly."""
    width = half_width * (1.0 - taper * t) * (1.0 + bulge * math.sin(math.pi * min(1.0, t * 1.1)))
    if t < 0.16:  # Shoulders; the flat top is hidden under the head.
        u = 1.0 - t / 0.16
        width *= 0.5 + 0.5 * math.sqrt(max(0.0, 1.0 - u * u))
    elif t > 0.74:  # Rounded bottom.
        u = (t - 0.74) / 0.26
        width *= 0.15 + 0.85 * math.sqrt(max(0.0, 1.0 - u ** 2.2))
    return width


def draw_body(shape, skin):
    """Naked body without arms nor legs: a rounded trunk the head sits on.
    Clothes will be layers drawn over it."""
    _, half_width, taper, bulge = BODY_SHAPES[shape]
    skin_color = SKIN_TONES[skin]
    canvas = Canvas(CHAR_W, CHAR_H)
    shadow = Canvas(CHAR_W, CHAR_H)
    shadow.ellipse(CX, FEET_Y - 3, half_width * 0.9, 7, (0, 0, 0))
    steps = 48
    height = BODY_BOTTOM - BODY_TOP
    right = [(CX + body_half_width(i / steps, half_width, taper, bulge),
              BODY_TOP + height * i / steps) for i in range(steps + 1)]
    outline = right + [(2 * CX - x, y) for x, y in reversed(right)]
    mask = canvas.polygon_mask(outline)
    # Lit from the upper left, rounder looking with darker sides.
    gx, gy = canvas.full_grids()
    u = (gx - CX) / (half_width * 1.1)
    v = (gy - BODY_TOP) / height
    factor = 1.06 - 0.16 * u - 0.12 * v - 0.14 * np.clip(np.abs(u) - 0.55, 0.0, 1.0) * 2.0
    color = np.clip(rgb(skin_color)[None, None, :] * factor[..., None], 0.0, 1.0)
    canvas.paint_full(mask, color)
    # Shoulder highlight and a hint of the navel.
    canvas.ellipse(CX - half_width * 0.45, BODY_TOP + 14, half_width * 0.35, 6,
                   shade(skin_color, 1.1), clip=mask, opacity=0.5)
    navel_y = BODY_TOP + height * (0.62 if bulge <= 0 else 0.6)
    canvas.ellipse(CX + 1, navel_y, 1.6, 2.2, shade(skin_color, 0.62), opacity=0.8)
    if bulge > 0:  # Fold under the belly.
        canvas.line([(CX - half_width * 0.5, BODY_TOP + height * 0.8),
                     (CX, BODY_TOP + height * 0.83),
                     (CX + half_width * 0.5, BODY_TOP + height * 0.8)], 1.6,
                    shade(skin_color, 0.75), clip=mask, opacity=0.7)
    canvas.outline(2.0, CHAR_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.3)


def eye(canvas, x, y, expression):
    dark = (40, 30, 26)
    if expression == "smiling":
        # Closed, happy eyes: upward arcs.
        canvas.line([(x - 4.5, y + 1.5), (x, y - 2.5), (x + 4.5, y + 1.5)], 2.4, dark)
        return
    ry = 4.6 if expression != "sad" else 4.0
    oy = 0.0 if expression != "sad" else 1.0
    canvas.ellipse(x, y + oy, 5.2, ry + 0.6, (250, 248, 240))
    canvas.ellipse(x + 0.5, y + oy + 0.5, 3.4, ry * 0.85, dark)
    canvas.ellipse(x - 0.6, y + oy - 1.4, 1.2, 1.2, (255, 255, 255))


def draw_head(skin, expression):
    skin_color = SKIN_TONES[skin]
    canvas = Canvas(CHAR_W, CHAR_H)
    # Ears, then the face.
    for side in (-1, 1):
        canvas.ellipse(CX + side * (HEAD_RX - 1), HEAD_CY + 3, 6, 8, shade(skin_color, 0.9))
        canvas.ellipse(CX + side * (HEAD_RX - 0.5), HEAD_CY + 3, 3, 4.5, shade(skin_color, 0.75))
    canvas.ellipse(CX, HEAD_CY, HEAD_RX, HEAD_RY, skin_color, light=0.32)
    # Jaw a little narrower than the skull.
    canvas.ellipse(CX, HEAD_CY + 12, HEAD_RX * 0.86, HEAD_RY * 0.62, skin_color, light=0.25)
    eye_y = HEAD_CY + 4
    brow = shade(skin_color, 0.42)
    for side in (-1, 1):
        x = CX + side * 11
        eye(canvas, x, eye_y, expression)
        inner, outer = x - side * 5, x + side * 5
        if expression == "neutral":
            canvas.line([(inner, eye_y - 9), (outer, eye_y - 9.5)], 2.6, brow)
        elif expression == "smiling":
            canvas.line([(inner, eye_y - 9.5), (x, eye_y - 11.5), (outer, eye_y - 10)], 2.6,
                        brow)
        elif expression == "sad":
            canvas.line([(inner, eye_y - 12), (outer, eye_y - 8)], 2.6, brow)
        else:  # angry
            canvas.line([(inner, eye_y - 6.5), (outer, eye_y - 11)], 3.0, brow)
    # Nose: a soft shadow.
    canvas.ellipse(CX + 1.5, HEAD_CY + 12, 3.5, 3, shade(skin_color, 0.8))
    canvas.ellipse(CX, HEAD_CY + 11, 2.5, 2.4, shade(skin_color, 1.05))
    mouth_y = HEAD_CY + 20
    lips = (120, 52, 48)
    if expression == "neutral":
        canvas.line([(CX - 6, mouth_y), (CX + 6, mouth_y)], 2.2, lips)
    elif expression == "smiling":
        canvas.polygon([(CX - 9, mouth_y - 2), (CX + 9, mouth_y - 2), (CX + 5, mouth_y + 4),
                        (CX, mouth_y + 5.5), (CX - 5, mouth_y + 4)], (110, 40, 40))
        canvas.polygon([(CX - 7, mouth_y - 1.5), (CX + 7, mouth_y - 1.5), (CX + 6, mouth_y + 0.8),
                        (CX - 6, mouth_y + 0.8)], (250, 246, 236))
        for side in (-1, 1):  # Blushing cheeks.
            canvas.ellipse(CX + side * 15, HEAD_CY + 13, 5, 3, (236, 120, 110), opacity=0.35)
    elif expression == "sad":
        canvas.line([(CX - 7, mouth_y + 3), (CX, mouth_y - 0.5), (CX + 7, mouth_y + 3)], 2.2,
                    lips)
    else:  # angry
        canvas.polygon([(CX - 7, mouth_y - 1), (CX + 7, mouth_y - 1), (CX + 8, mouth_y + 3),
                        (CX - 8, mouth_y + 3)], (110, 40, 40))
        canvas.line([(CX - 6.5, mouth_y + 1), (CX + 6.5, mouth_y + 1)], 1.6, (250, 246, 236))
        canvas.ellipse(CX, HEAD_CY - 6, 3, 1.4, shade(skin_color, 0.8))  # Frown wrinkle.
    canvas.outline(2.0, CHAR_OUTLINE)
    return canvas


def face_hole(canvas, top):
    """Mask of the visible face below the hairline `top`."""
    result = Canvas(CHAR_W, CHAR_H)
    result.ellipse(CX, HEAD_CY + 5, HEAD_RX - 4, HEAD_RY - 3, (1.0, 1.0, 1.0))
    hole = result.alpha
    _, gy = canvas.full_grids()
    return hole * np.clip((gy - top) * SUPERSAMPLING, 0.0, 1.0)


def hair_strands(canvas, rng, count, x0, x1, y0, y1, length):
    clip = canvas.coverage()
    for _ in range(count):
        x = rng.uniform(x0, x1)
        y = rng.uniform(y0, y1)
        lean = (x - CX) * 0.12
        color = HAIR_DARK if rng.random() < 0.6 else HAIR_LIGHT
        canvas.line([(x, y), (x + lean, y + length * rng.uniform(0.6, 1.2))], 1.3, color,
                    clip=clip, opacity=0.65)


def draw_hair(style):
    canvas = Canvas(CHAR_W, CHAR_H)
    rng = random.Random(4000 + style)
    base = rgb(HAIR_BASE)
    if style == 1:  # Short crop.
        canvas.ellipse(CX, HEAD_CY - 6, HEAD_RX + 2.5, HEAD_RY - 1, base, light=0.3)
        canvas.erase_full(face_hole(canvas, HEAD_CY - 15))
        for i in range(6):  # Fringe tufts.
            x = CX - 15 + i * 6
            canvas.polygon([(x - 4, HEAD_CY - 17), (x + 4, HEAD_CY - 17), (x + 1, HEAD_CY - 10)],
                           HAIR_BASE)
        hair_strands(canvas, rng, 40, CX - 26, CX + 26, HEAD_CY - 32, HEAD_CY - 16, 7)
    elif style == 2:  # Long, falling on the shoulders.
        canvas.ellipse(CX, HEAD_CY - 5, HEAD_RX + 3.5, HEAD_RY, base, light=0.3)
        for side in (-1, 1):
            canvas.polygon([(CX + side * 18, HEAD_CY - 16), (CX + side * 31, HEAD_CY - 4),
                            (CX + side * 33, HEAD_CY + 48), (CX + side * 25, HEAD_CY + 54),
                            (CX + side * 17, HEAD_CY + 30)], HAIR_BASE)
            canvas.polygon([(CX + side * 26, HEAD_CY), (CX + side * 31, HEAD_CY - 4),
                            (CX + side * 33, HEAD_CY + 48), (CX + side * 28, HEAD_CY + 52)],
                           shade(HAIR_BASE, 0.85))
        canvas.erase_full(face_hole(canvas, HEAD_CY - 12))
        # Center parting.
        canvas.line([(CX, HEAD_CY - 33), (CX + 1, HEAD_CY - 20)], 1.6, HAIR_DARK)
        canvas.polygon([(CX - 1, HEAD_CY - 22), (CX - 22, HEAD_CY - 6), (CX - 16, HEAD_CY - 16)],
                       HAIR_BASE)
        canvas.polygon([(CX + 1, HEAD_CY - 22), (CX + 22, HEAD_CY - 6), (CX + 16, HEAD_CY - 16)],
                       HAIR_BASE)
        hair_strands(canvas, rng, 90, CX - 32, CX + 32, HEAD_CY - 32, HEAD_CY + 40, 12)
    elif style == 3:  # Curly volume.
        for i in range(16):
            angle = math.pi * (0.9 + 1.2 * i / 15.0)
            x = CX + math.cos(angle) * 30
            y = HEAD_CY - 6 + math.sin(angle) * 30
            canvas.ellipse(x, y, 11, 11, base, light=0.35)
        canvas.ellipse(CX, HEAD_CY - 10, 30, 26, base, light=0.3)
        canvas.erase_full(face_hole(canvas, HEAD_CY - 12))
        clip = canvas.coverage()
        for _ in range(70):  # Curls.
            x = rng.uniform(CX - 40, CX + 40)
            y = rng.uniform(HEAD_CY - 46, HEAD_CY + 20)
            r = rng.uniform(2.5, 4.5)
            canvas.ellipse(x, y, r, r, HAIR_DARK, clip=clip, opacity=0.55)
            canvas.ellipse(x - 0.8, y - 0.8, r * 0.7, r * 0.7, HAIR_LIGHT, clip=clip, opacity=0.6)
    elif style == 4:  # Bun on top.
        canvas.ellipse(CX, HEAD_CY - 33, 13, 11, base, light=0.4)
        canvas.ellipse(CX, HEAD_CY - 6, HEAD_RX + 2, HEAD_RY - 1, base, light=0.3)
        canvas.erase_full(face_hole(canvas, HEAD_CY - 18))
        canvas.line([(CX - 10, HEAD_CY - 27), (CX + 10, HEAD_CY - 27)], 3, HAIR_DARK)
        # Combed back: strands converging to the bun.
        clip = canvas.coverage()
        for i in range(11):
            x = CX - 24 + i * 4.8
            canvas.line([(x, HEAD_CY - 14), (CX + (x - CX) * 0.3, HEAD_CY - 28)], 1.3, HAIR_DARK,
                        clip=clip, opacity=0.6)
    elif style == 5:  # Two braids.
        canvas.ellipse(CX, HEAD_CY - 5, HEAD_RX + 2.5, HEAD_RY - 1, base, light=0.3)
        for side in (-1, 1):
            for k in range(6):
                y = HEAD_CY + 4 + k * 9.5
                r = 6.5 - k * 0.35
                canvas.ellipse(CX + side * (27 - k * 0.6), y, r, r * 1.05, base, light=0.4)
                canvas.line([(CX + side * (27 - k * 0.6) - r * 0.7, y - r * 0.3),
                             (CX + side * (27 - k * 0.6) + r * 0.7, y + r * 0.3)], 1.2, HAIR_DARK)
            canvas.ellipse(CX + side * 24, HEAD_CY + 63, 4.5, 3, HAIR_DARK)  # Tie.
            canvas.polygon([(CX + side * 21, HEAD_CY + 64), (CX + side * 27, HEAD_CY + 64),
                            (CX + side * 25, HEAD_CY + 74), (CX + side * 22, HEAD_CY + 74)],
                           HAIR_BASE)
        canvas.erase_full(face_hole(canvas, HEAD_CY - 13))
        canvas.line([(CX, HEAD_CY - 33), (CX, HEAD_CY - 16)], 1.6, HAIR_DARK)
        hair_strands(canvas, rng, 30, CX - 26, CX + 26, HEAD_CY - 32, HEAD_CY - 16, 7)
    canvas.outline(1.8, HAIR_OUTLINE)
    return canvas


def draw_beard(style):
    canvas = Canvas(CHAR_W, CHAR_H)
    rng = random.Random(5000 + style)
    mouth_y = HEAD_CY + 20
    base = rgb(HAIR_BASE)
    if style == 1:  # Stubble: sparse dots on the jaw.
        jaw = Canvas(CHAR_W, CHAR_H)
        jaw.ellipse(CX, HEAD_CY + 14, HEAD_RX - 2, HEAD_RY * 0.58, (1.0, 1.0, 1.0))
        clip = jaw.alpha.copy()
        _, gy = canvas.full_grids()
        clip *= np.clip((gy - (HEAD_CY + 9)) * SUPERSAMPLING, 0, 1)
        mouth = Canvas(CHAR_W, CHAR_H)
        mouth.ellipse(CX, mouth_y + 1, 11, 6, (1.0, 1.0, 1.0))
        clip *= 1.0 - mouth.alpha
        for _ in range(260):
            x = rng.uniform(CX - 26, CX + 26)
            y = rng.uniform(HEAD_CY + 8, HEAD_CY + 32)
            canvas.ellipse(x, y, 0.8, 0.8, (0.5, 0.5, 0.5), clip=clip, opacity=0.5)
        return canvas  # No outline: stubble is a shadow, not a shape.
    if style in (2, 3):  # Short or full beard around the mouth.
        drop = 10 if style == 2 else 26
        canvas.polygon([(CX - HEAD_RX + 1, HEAD_CY + 4), (CX - HEAD_RX + 3, HEAD_CY + 16),
                        (CX - 14, HEAD_CY + 26 + drop * 0.6), (CX, HEAD_CY + 30 + drop),
                        (CX + 14, HEAD_CY + 26 + drop * 0.6), (CX + HEAD_RX - 3, HEAD_CY + 16),
                        (CX + HEAD_RX - 1, HEAD_CY + 4), (CX + HEAD_RX - 5, HEAD_CY + 4),
                        (CX + 16, HEAD_CY + 15), (CX - 16, HEAD_CY + 15),
                        (CX - HEAD_RX + 5, HEAD_CY + 4)], HAIR_BASE)
        canvas.ellipse(CX, HEAD_CY + 22 + drop * 0.5, 20, 10 + drop * 0.45, base, light=0.4)
        mouth = Canvas(CHAR_W, CHAR_H)
        mouth.ellipse(CX, mouth_y + 1.5, 10, 5.5, (1.0, 1.0, 1.0))
        canvas.erase_full(mouth.alpha)
        hair_strands(canvas, rng, 60 if style == 3 else 35, CX - 24, CX + 24, HEAD_CY + 8,
                     HEAD_CY + 26 + drop, 6)
    # Mustache (alone for style 4, with the beard for the full one).
    if style in (3, 4):
        for side in (-1, 1):
            canvas.polygon([(CX, mouth_y - 5.5), (CX + side * 9, mouth_y - 5),
                            (CX + side * 13, mouth_y + 1), (CX + side * 8, mouth_y - 1.5),
                            (CX, mouth_y - 1.5)], HAIR_BASE)
    canvas.outline(1.5, HAIR_OUTLINE)
    return canvas


# ---------------------------------------------------------------------------
# Hands and tools. A fist is shown, near the body, while a colonist works; it
# holds the tool of the job. The grip is the middle of both canvases: the game
# draws them centered on the hand and turns the tool around that point.
# ---------------------------------------------------------------------------

FIST_SIZE = 32
TOOL_SIZE = 64
FLINT = (84, 92, 116)
FLINT_LIGHT = (150, 160, 184)
COBBLE = (150, 140, 124)


def draw_fist(skin):
    skin_color = SKIN_TONES[skin]
    canvas = Canvas(FIST_SIZE, FIST_SIZE)
    c = FIST_SIZE / 2.0
    canvas.ellipse(c, c, 8.5, 7.5, skin_color, light=0.4)
    # Folded fingers: three knuckle creases, and the thumb across them.
    for i in range(3):
        x = c - 4.5 + i * 4.5
        canvas.line([(x, c - 6.5), (x + 0.5, c - 2.0)], 1.0, shade(skin_color, 0.7), opacity=0.8)
    canvas.ellipse(c - 1.5, c + 2.5, 5.5, 3.2, shade(skin_color, 1.04), light=0.3)
    canvas.line([(c - 6.5, c + 2.8), (c + 3.5, c + 2.2)], 0.9, shade(skin_color, 0.72),
                opacity=0.6)
    canvas.outline(1.5, CHAR_OUTLINE)
    return canvas


def flint_facets(canvas, rng, box, count, size):
    clip = canvas.coverage()
    x0, y0, x1, y1 = box
    for _ in range(count):
        x, y = rng.uniform(x0, x1), rng.uniform(y0, y1)
        r = size * rng.uniform(0.7, 1.3)
        color = FLINT_LIGHT if rng.random() < 0.5 else shade(FLINT, 0.75)
        canvas.ellipse(x, y, r, r * 0.75, color, clip=clip, opacity=0.45)


def draw_tool(name):
    canvas = Canvas(TOOL_SIZE, TOOL_SIZE)
    rng = random.Random(6000 + len(name))
    c = TOOL_SIZE / 2.0
    if name == "cobble":  # Hammerstone: a rounded stone in the fist.
        canvas.ellipse(c, c - 8, 12, 10, COBBLE, light=0.5)
        for _ in range(14):
            x, y = rng.uniform(c - 9, c + 9), rng.uniform(c - 15, c - 2)
            canvas.ellipse(x, y, 1.0, 1.0, shade(COBBLE, rng.choice((0.75, 1.15))),
                           clip=canvas.coverage(), opacity=0.7)
    elif name == "chopper":  # A cobble with a flaked cutting edge on top.
        canvas.ellipse(c, c - 7, 12, 10, COBBLE, light=0.5)
        canvas.polygon([(c - 11, c - 10), (c - 6, c - 18), (c, c - 19), (c + 6, c - 18),
                        (c + 11, c - 10), (c, c - 13)], FLINT)
        flint_facets(canvas, rng, (c - 10, c - 19, c + 10, c - 11), 8, 2.5)
    elif name == "flake":  # A thin sharp flake.
        canvas.polygon([(c, c - 28), (c + 7, c - 14), (c + 5, c + 2), (c - 5, c + 2),
                        (c - 7, c - 12)], FLINT)
        canvas.line([(c, c - 26), (c + 1, c)], 1.2, FLINT_LIGHT, opacity=0.7)
        flint_facets(canvas, rng, (c - 6, c - 24, c + 6, c), 6, 2.0)
    elif name == "hand_axe":  # Biface: a teardrop, pointed up, held by its broad base.
        points = []
        for i in range(25):
            t = i / 24.0 * 2.0 * math.pi
            # Teardrop: broad at the bottom, pointed at the top.
            x = math.sin(t) * 12.0 * (0.25 + 0.75 * (1.0 - math.cos(t)) / 2.0) ** 0.5
            y = -math.cos(t) * 17.0
            points.append((c + x, c - 12 + y))
        canvas.polygon(points, FLINT)
        flint_facets(canvas, rng, (c - 11, c - 29, c + 11, c + 4), 18, 2.8)
        canvas.line([(c, c - 28), (c, c + 4)], 1.0, FLINT_LIGHT, opacity=0.5)
    canvas.outline(1.5, CHAR_OUTLINE)
    return canvas


TOOL_NAMES = ["cobble", "chopper", "flake", "hand_axe"]


# ---------------------------------------------------------------------------
# Tile textures: the ground seen from above, the cut face of rock walls seen
# from inside them, and grass over the ground. Each one is the same on every
# tile of its kind, so it must tile seamlessly: noise is periodic and shapes
# crossing an edge come back in on the other side. The files are TILE plus a
# wrapped border of TILE_PADDING pixels on every side, so that the game's
# filtering blends each tile with the texture of the next one, never with
# its neighbor in the atlas.
#
# The ground is loose and lit from the upper left (crumbs, pebbles with a
# shadow); walls are flat, dense and dark, without relief, so that they never
# read as ground.
# ---------------------------------------------------------------------------

TILE_PADDING = 32


class WrapCanvas:
    """A tileable TILE x TILE canvas, drawn at SUPERSAMPLING times the size,
    storing premultiplied colors like Canvas."""

    def __init__(self, base=None):
        s = SUPERSAMPLING
        self.size = TILE * s
        if base is None:
            self.color = np.zeros((self.size, self.size, 3), dtype=np.float32)
            self.alpha = np.zeros((self.size, self.size), dtype=np.float32)
        else:
            self.color = np.repeat(np.repeat(base.astype(np.float32), s, 0), s, 1)
            self.alpha = np.ones((self.size, self.size), dtype=np.float32)

    def window(self, x0, y0, x1, y1):
        """Wrapped indices of a box in final pixels and the (unwrapped)
        coordinate grids, in final pixels, of its sample centers."""
        s = SUPERSAMPLING
        bx = np.arange(int(math.floor(x0 * s)) - 1, int(math.ceil(x1 * s)) + 1)
        by = np.arange(int(math.floor(y0 * s)) - 1, int(math.ceil(y1 * s)) + 1)
        gx, gy = np.meshgrid((bx + 0.5) / s, (by + 0.5) / s)
        return np.ix_(by % self.size, bx % self.size), gx, gy

    def paint(self, where, mask, color):
        m = mask[..., None]
        self.color[where] = np.asarray(color, dtype=np.float32) * m + self.color[where] * (1.0 - m)
        self.alpha[where] = mask + self.alpha[where] * (1.0 - mask)

    def ellipse(self, cx, cy, rx, ry, color, angle=0.0, opacity=1.0, light=0.0):
        """Ellipse turned by `angle` (radians), optionally shaded like a lit
        dome (light from the upper left)."""
        r = max(rx, ry)
        where, gx, gy = self.window(cx - r, cy - r, cx + r, cy + r)
        c, s = math.cos(angle), math.sin(angle)
        u = ((gx - cx) * c + (gy - cy) * s) / rx
        v = (-(gx - cx) * s + (gy - cy) * c) / ry
        inside = (u * u + v * v <= 1.0).astype(np.float32)
        base = np.asarray(color, dtype=np.float32)
        if light:
            # Back to screen directions: lit towards the upper left.
            du, dv = (gx - cx) / r, (gy - cy) / r
            factor = 1.0 + light * (-0.55 * du - 0.8 * dv)
            base = np.clip(base[None, None, :] * factor[..., None], 0.0, 1.0)
        self.paint(where, inside * opacity, base)

    def stroke(self, points, r0, r1, color, opacity=1.0):
        """Polyline whose radius goes from r0 at its start to r1 at its end."""
        lengths = [math.dist(points[i], points[i + 1]) for i in range(len(points) - 1)]
        total = max(sum(lengths), 1e-6)
        done = 0.0
        for (ax, ay), (bx, by), length in zip(points, points[1:], lengths):
            ra = r0 + (r1 - r0) * done / total
            rb = r0 + (r1 - r0) * (done + length) / total
            done += length
            r = max(ra, rb)
            where, gx, gy = self.window(min(ax, bx) - r, min(ay, by) - r, max(ax, bx) + r,
                                        max(ay, by) + r)
            dx, dy = bx - ax, by - ay
            t = np.clip(((gx - ax) * dx + (gy - ay) * dy) / max(dx * dx + dy * dy, 1e-9), 0, 1)
            d = np.hypot(gx - ax - t * dx, gy - ay - t * dy)
            inside = (d <= ra + (rb - ra) * t).astype(np.float32)
            self.paint(where, inside * opacity, color)

    def to_pixels(self):
        """Straight RGBA floats at the final size."""
        s = SUPERSAMPLING
        color = self.color.reshape(TILE, s, TILE, s, 3).mean(axis=(1, 3))
        alpha = self.alpha.reshape(TILE, s, TILE, s).mean(axis=(1, 3))
        straight = np.where(alpha[..., None] > 1e-4, color / np.maximum(alpha[..., None], 1e-4), 0)
        return np.dstack([np.clip(straight, 0.0, 1.0), alpha])


def periodic_noise(rng, scale_x, scale_y=None):
    """Tileable noise with features about `scale` pixels wide (stretched when
    the two scales differ): zero mean, unit deviation."""
    scale_y = scale_x if scale_y is None else scale_y
    frequencies = np.fft.fftfreq(TILE)
    fx, fy = np.meshgrid(frequencies, frequencies)
    spectrum = np.fft.fft2(rng.standard_normal((TILE, TILE)))
    spectrum *= np.exp(-2.0 * ((fx * scale_x) ** 2 + (fy * scale_y) ** 2))
    noise = np.real(np.fft.ifft2(spectrum))
    return ((noise - noise.mean()) / max(noise.std(), 1e-9)).astype(np.float32)


def cells(rng, cell):
    """Tileable Voronoi cells over a jittered grid of `cell` pixels (a divisor
    of TILE): for each pixel, the index of its cell and the distances to the
    nearest and second nearest cell centers."""
    count = TILE // cell
    jitter = rng.random((count, count, 2))
    ys, xs = np.mgrid[0:TILE, 0:TILE].astype(np.float32) + 0.5
    best = np.full((TILE, TILE), np.inf, dtype=np.float32)
    second = np.full((TILE, TILE), np.inf, dtype=np.float32)
    index = np.zeros((TILE, TILE), dtype=np.int32)
    home_x, home_y = (xs // cell).astype(int), (ys // cell).astype(int)
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            gx, gy = home_x + ox, home_y + oy
            wx, wy = gx % count, gy % count
            px = (gx + jitter[wy, wx, 0]) * cell
            py = (gy + jitter[wy, wx, 1]) * cell
            d = np.hypot(xs - px, ys - py)
            closer = d < best
            second = np.where(closer, best, np.minimum(second, d))
            index = np.where(closer, wy * count + wx, index)
            best = np.where(closer, d, best)
    return index, best, second


def wrap_bleed(pixels, steps=4):
    """bleed() for tileable textures: the colors of visible pixels spread
    into the transparent ones around them, across the edges too."""
    color, alpha = pixels[..., :3].copy(), pixels[..., 3]
    known = alpha > 1e-4
    for _ in range(steps):
        total = np.zeros_like(color)
        count = np.zeros(known.shape, dtype=np.float32)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                weight = np.roll(known, (dy, dx), (0, 1)).astype(np.float32)
                total += np.roll(color, (dy, dx), (0, 1)) * weight[..., None]
                count += weight
        grow = (~known) & (count > 0)
        color[grow] = total[grow] / count[grow][..., None]
        known = known | grow
    return np.dstack([color, alpha])


def tile_image(pixels):
    """The file of a tile texture: the texture with its wrapped border."""
    padded = np.pad(pixels, ((TILE_PADDING, TILE_PADDING), (TILE_PADDING, TILE_PADDING), (0, 0)),
                    mode="wrap")
    return Image.fromarray(np.round(np.clip(padded, 0, 1) * 255.0).astype(np.uint8), "RGBA")


def tinted(base, factors):
    """A base color (0..255) times a brightness field."""
    return np.clip(rgb(base)[None, None, :] * factors[..., None], 0.0, 1.0)


def crumbs(canvas, rng, count, base, radius, spread, light=0.7, shadow=0.35):
    """Loose clods lit from the upper left, each with its shadow."""
    for _ in range(count):
        x, y = rng.random() * TILE, rng.random() * TILE
        r = radius[0] + (radius[1] - radius[0]) * rng.random() ** 2
        ry = r * (0.65 + 0.35 * rng.random())
        angle = rng.random() * math.pi
        color = np.clip(rgb(base) * (1.0 + spread * (rng.random() * 2.0 - 1.0)), 0, 1)
        canvas.ellipse(x + r * 0.45, y + r * 0.55, r, ry, (0.0, 0.0, 0.0), angle, shadow)
        canvas.ellipse(x, y, r, ry, color, angle, 1.0, light)


def draw_soil():
    rng = np.random.default_rng(6000)
    field = 1.0 + 0.07 * periodic_noise(rng, 9.0) + 0.05 * periodic_noise(rng, 2.5)
    canvas = WrapCanvas(tinted((122, 94, 64), field))
    for _ in range(90):  # Pores and little hollows.
        canvas.ellipse(rng.random() * TILE, rng.random() * TILE, 0.5 + rng.random() * 0.6,
                       0.4 + rng.random() * 0.4, (0.27, 0.2, 0.13), rng.random() * math.pi, 0.6)
    crumbs(canvas, rng, 150, (128, 99, 68), (0.7, 2.3), 0.14)
    crumbs(canvas, rng, 9, (136, 126, 110), (1.0, 1.9), 0.1, light=0.9)  # Small pebbles.
    return canvas.to_pixels()


def draw_rich_soil():
    rng = np.random.default_rng(6100)
    field = 1.0 + 0.09 * periodic_noise(rng, 7.0) + 0.06 * periodic_noise(rng, 2.0)
    canvas = WrapCanvas(tinted((86, 63, 42), field))
    for _ in range(26):  # Bits of rotten leaves and rootlets.
        x, y = rng.random() * TILE, rng.random() * TILE
        angle, length = rng.random() * math.tau, 2.5 + rng.random() * 4.0
        bend = (rng.random() - 0.5) * 1.2
        points = [(x + math.cos(angle + bend * t) * length * t,
                   y + math.sin(angle + bend * t) * length * t) for t in (0.0, 0.5, 1.0)]
        color = rgb((112, 84, 48)) if rng.random() < 0.5 else rgb((58, 40, 26))
        canvas.stroke(points, 0.45, 0.3, color, 0.9)
    crumbs(canvas, rng, 230, (90, 66, 44), (0.6, 2.0), 0.18, light=0.6, shadow=0.45)
    crumbs(canvas, rng, 3, (120, 112, 98), (0.9, 1.5), 0.1, light=0.9)
    return canvas.to_pixels()


def caustics(rng, cell, width):
    """A network of thin bright lines, like light focused by ripples."""
    _, best, second = cells(rng, cell)
    return np.clip(1.0 - (second - best) / width, 0.0, 1.0) ** 2


def draw_shallow_water():
    rng = np.random.default_rng(6200)
    bottom = periodic_noise(rng, 12.0)  # The bottom, seen through the water.
    base = mix(rgb((58, 106, 154)), rgb((86, 122, 132)), np.clip(0.3 + 0.2 * bottom, 0, 1)[..., None])
    light = caustics(rng, 16, 2.2) * (0.6 + 0.4 * np.clip(periodic_noise(rng, 14.0), -1, 1))
    color = base * (1.0 + 0.05 * periodic_noise(rng, 5.0))[..., None]
    color = mix(color, rgb((170, 206, 226)), (0.32 * np.clip(light, 0, 1))[..., None])
    return np.dstack([np.clip(color, 0, 1), np.ones((TILE, TILE), dtype=np.float32)])


def draw_deep_water():
    rng = np.random.default_rng(6300)
    swell = periodic_noise(rng, 22.0, 13.0)
    color = rgb((26, 52, 96))[None, None, :] * (1.0 + 0.1 * swell + 0.04 *
                                                  periodic_noise(rng, 6.0))[..., None]
    crests = np.clip((periodic_noise(rng, 10.0, 4.0) - 1.6) / 0.8, 0, 1)  # Long soft glints.
    color = mix(color, rgb((64, 98, 140)), (0.45 * crests)[..., None])
    return np.dstack([np.clip(color, 0, 1), np.ones((TILE, TILE), dtype=np.float32)])


def draw_granite_wall():
    """Interlocking crystals, polished flat: dark gray, black mica, dim
    feldspar and smoky quartz, with a few hairline cracks."""
    rng = np.random.default_rng(6400)
    index, best, second = cells(rng, 4)
    minerals = np.array([rgb((70, 70, 76)), rgb((30, 30, 34)), rgb((96, 92, 92)),
                         rgb((92, 74, 70)), rgb((56, 60, 70))], dtype=np.float32)
    weights = np.array([0.36, 0.22, 0.14, 0.14, 0.14])
    picks = rng.choice(len(minerals), size=index.max() + 1, p=weights)
    color = minerals[picks[index]]
    color *= (1.0 + 0.06 * rng.standard_normal(index.max() + 1).astype(np.float32))[index][..., None]
    color *= (1.0 - 0.18 * np.clip(1.0 - (second - best) / 0.8, 0, 1))[..., None]  # Grain edges.
    color *= (1.0 + 0.05 * periodic_noise(rng, 18.0))[..., None]
    canvas = WrapCanvas(np.clip(color, 0, 1))
    for _ in range(3):
        x, y, angle = rng.random() * TILE, rng.random() * TILE, rng.random() * math.tau
        points = [(x, y)]
        for _ in range(6):
            angle += (rng.random() - 0.5) * 0.9
            x, y = x + math.cos(angle) * 6.0, y + math.sin(angle) * 6.0
            points.append((x, y))
        canvas.stroke(points, 0.35, 0.2, rgb((18, 18, 22)), 0.8)
    return canvas.to_pixels()


def draw_sandstone_wall():
    """Fine sand pressed into layers: dark ocher and red-brown beds."""
    rng = np.random.default_rng(6500)
    ys = (np.arange(TILE, dtype=np.float32) + 0.5)[:, None] / TILE * math.tau
    warp = 0.35 * periodic_noise(rng, 30.0, 10.0)
    beds = (np.sin(ys * 5 + warp) * 0.5 + np.sin(ys * 11 + 1.3 + warp * 2.0) * 0.3 +
            np.sin(ys * 23 + 0.4 + warp * 3.0) * 0.2)
    color = mix(rgb((50, 32, 28)), rgb((78, 52, 40)), np.clip(0.5 + 0.5 * beds, 0, 1)[..., None])
    grain = 1.0 + 0.07 * rng.standard_normal((TILE, TILE)).astype(np.float32)
    color *= (grain + 0.04 * periodic_noise(rng, 3.0, 1.2))[..., None]
    canvas = WrapCanvas(np.clip(color, 0, 1))
    for _ in range(14):  # Iron spots.
        canvas.ellipse(rng.random() * TILE, rng.random() * TILE, 0.6 + rng.random(),
                       0.5 + rng.random() * 0.5, rgb((36, 20, 15)), 0.0, 0.7)
    return canvas.to_pixels()


def draw_earth_wall():
    """Packed earth: dark, smooth, faintly layered, with stones and dead
    roots caught in it, flat with the cut."""
    rng = np.random.default_rng(6600)
    field = (1.0 + 0.05 * periodic_noise(rng, 14.0) + 0.05 * periodic_noise(rng, 1.5) +
             0.04 * rng.standard_normal((TILE, TILE)).astype(np.float32))
    canvas = WrapCanvas(tinted((46, 35, 26), field))
    for _ in range(5):  # Roots.
        x, y, angle = rng.random() * TILE, rng.random() * TILE, math.pi / 2 + rng.random() - 0.5
        points = [(x, y)]
        for _ in range(5):
            angle += (rng.random() - 0.5) * 1.0
            x, y = x + math.cos(angle) * 3.5, y + math.sin(angle) * 3.5
            points.append((x, y))
        canvas.stroke(points, 0.5, 0.25, rgb((30, 21, 15)), 0.9)
    for _ in range(9):  # Stones, cut flat: a dark rim, no light.
        x, y = rng.random() * TILE, rng.random() * TILE
        r, angle = 2.0 + rng.random() * 2.5, rng.random() * math.pi
        canvas.ellipse(x, y, r + 0.6, r * 0.7 + 0.6, rgb((24, 19, 16)), angle)
        canvas.ellipse(x, y, r, r * 0.7, rgb((70, 65, 60)) * (0.8 + 0.3 * rng.random()), angle)
    return canvas.to_pixels()


# Grass over the ground, by growth: blade length and count, in tufts.
GRASS_STAGES = [(3.0, 150), (5.0, 380), (8.0, 700), (11.0, 1050)]


def draw_grass(stage):
    """Tufts of blades seen from above: they spread out from their base, the
    lower ones darker, the tips lighter."""
    length, count = GRASS_STAGES[stage]
    rng = np.random.default_rng(6700 + stage)
    canvas = WrapCanvas()
    blades = []
    tufts = [(rng.random() * TILE, rng.random() * TILE) for _ in range(count // 5)]
    for i in range(count):
        tx, ty = tufts[i % len(tufts)]
        x, y = tx + rng.normal() * 1.5, ty + rng.normal() * 1.5
        angle = math.atan2(y - ty, x - tx) + rng.normal() * 0.6
        blade = length * (0.6 + 0.6 * rng.random())
        blades.append((rng.random(), x, y, angle, blade, (rng.random() - 0.5) * 0.8))
    blades.sort()  # From the lowest (darkest) to the highest.
    dark, light, tip = rgb((44, 78, 30)), rgb((92, 140, 54)), rgb((150, 178, 86))
    for height, x, y, angle, blade, bend in blades:
        body = mix(dark, light, height)
        points = [(x + math.cos(angle + bend * t) * blade * t,
                   y + math.sin(angle + bend * t) * blade * t) for t in (0.0, 0.35, 0.7, 1.0)]
        width = 0.9 + 0.5 * min(1.0, length / 8.0)
        canvas.stroke(points[:3], width, width * 0.7, body)
        canvas.stroke(points[2:], width * 0.7, 0.2, mix(body, tip, 0.5 * height + 0.2))
    return wrap_bleed(canvas.to_pixels())


def draw_sand():
    """Fine grains in faint wind ripples, pale; a few shell-like specks."""
    rng = np.random.default_rng(6800)
    ripple = periodic_noise(rng, 26.0, 6.0)
    waves = np.sin((np.arange(TILE, dtype=np.float32)[:, None] / TILE * math.tau * 4.0) +
                   1.2 * periodic_noise(rng, 18.0, 8.0))
    field = (1.0 + 0.035 * ripple + 0.03 * waves + 0.035 * rng.standard_normal((TILE, TILE))
             .astype(np.float32) + 0.02 * periodic_noise(rng, 2.0))
    canvas = WrapCanvas(tinted((212, 194, 140), field))
    for _ in range(40):  # Coarser grains with a hint of shadow.
        x, y = rng.random() * TILE, rng.random() * TILE
        light = rng.random() < 0.5
        color = rgb((236, 220, 170)) if light else rgb((176, 154, 104))
        canvas.ellipse(x, y, 0.5 + rng.random() * 0.4, 0.4 + rng.random() * 0.3, color,
                       rng.random() * math.pi, 0.8)
    return canvas.to_pixels()


def draw_gravel():
    """Closely packed pebbles of several grays and browns, each lit from the
    upper left and shadowed, over dark gaps."""
    rng = np.random.default_rng(6900)
    canvas = WrapCanvas(tinted((78, 72, 66), 1.0 + 0.05 * periodic_noise(rng, 3.0)))
    palette = [(138, 132, 122), (156, 148, 134), (118, 114, 108), (150, 130, 106),
               (104, 104, 108), (170, 164, 152)]
    for _ in range(330):
        x, y = rng.random() * TILE, rng.random() * TILE
        r = 2.2 + rng.random() * 3.2
        ry = r * (0.6 + 0.4 * rng.random())
        angle = rng.random() * math.pi
        color = np.clip(rgb(palette[rng.integers(len(palette))]) *
                        (0.9 + 0.2 * rng.random()), 0, 1)
        canvas.ellipse(x + r * 0.3, y + r * 0.4, r, ry, (0.0, 0.0, 0.0), angle, 0.45)
        canvas.ellipse(x, y, r, ry, color, angle, 1.0, 0.8)
    return canvas.to_pixels()


def draw_rough_stone():
    """Bare rock seen from above: broken slabs with lit edges and dark
    cracks between them, lichen-gray; relief, unlike the flat wall faces."""
    rng = np.random.default_rng(7000)
    index, best, second = cells(rng, 32)
    tones = 1.0 + 0.12 * rng.standard_normal(index.max() + 1).astype(np.float32)
    slope = rng.standard_normal((index.max() + 1, 2)).astype(np.float32)
    ys, xs = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    tilt = (slope[index, 0] * (xs % 32 - 16) + slope[index, 1] * (ys % 32 - 16)) / 160.0
    field = (tones[index] + tilt + 0.05 * periodic_noise(rng, 5.0) +
             0.035 * periodic_noise(rng, 1.5))
    # Cracks fade in and out along their length, and wander: not paving.
    wander = 1.2 * periodic_noise(rng, 4.0)
    reach = np.clip((periodic_noise(rng, 7.0) + 0.6) / 1.2, 0.0, 1.0)
    crack = np.clip(1.0 - (second - best + wander) / 1.6, 0.0, 1.0) * reach
    grain = 0.06 * rng.standard_normal((TILE, TILE)).astype(np.float32)
    field = field + grain + 0.07 * periodic_noise(rng, 2.5)
    field = field * (1.0 - 0.6 * crack ** 1.3) + 0.10 * np.roll(crack, (1, 1), (0, 1)) ** 2
    color = tinted((112, 108, 102), field)
    lichen = np.clip((periodic_noise(rng, 6.0) - 1.1) / 0.6, 0, 1)
    color = mix(color, rgb((120, 128, 96)), (0.35 * lichen)[..., None])
    return np.dstack([np.clip(color, 0, 1), np.ones((TILE, TILE), dtype=np.float32)])


def draw_foliage_of_density(seed, clumps, gap, shade, twigs=0):
    """A floor of branches: overlapping leaf clumps over dark gaps, lit on top.
    Without a `gap` color the foliage is thin: leaves and bare branches on a
    transparent background, so that what lies below shows through (the
    thinner, the more of it); `twigs` branches are drawn under the leaves."""
    rng = np.random.default_rng(seed)
    thin = gap is None
    canvas = WrapCanvas(None if thin else tinted(gap, 1.0 + 0.08 * periodic_noise(rng, 6.0)))
    palette = [(52, 98, 40), (66, 114, 46), (84, 136, 56), (44, 84, 38), (102, 150, 64)]
    bark = [(86, 62, 38), (104, 76, 46), (70, 50, 32)]
    for _ in range(twigs):  # Bare branches, slightly bent, some forked.
        x, y = rng.random() * TILE, rng.random() * TILE
        angle = rng.random() * math.pi * 2.0
        length = 22.0 + rng.random() * 34.0
        bend = (rng.random() - 0.5) * 0.9
        points = []
        for k in range(5):
            a = angle + bend * k / 4.0
            points.append((x + math.cos(a) * length * k / 4.0, y + math.sin(a) * length * k / 4.0))
        color = rgb(bark[rng.integers(len(bark))]) * (0.9 + 0.2 * rng.random())
        canvas.stroke(points, 1.7, 0.7, color, 0.95)
        fork = 2 + int(rng.integers(2))
        fa = angle + (0.7 if rng.random() < 0.5 else -0.7)
        sx, sy = points[fork]
        canvas.stroke([(sx, sy), (sx + math.cos(fa) * length * 0.4, sy + math.sin(fa) * length * 0.4)],
                      1.1, 0.5, color * 0.9, 0.9)
    for layer in range(3):
        for _ in range(clumps - round(clumps * 12 / 70) * layer):
            x, y = rng.random() * TILE, rng.random() * TILE
            r = 5.0 + rng.random() * 5.0
            angle = rng.random() * math.pi
            color = np.clip(rgb(palette[min(len(palette) - 1, rng.integers(len(palette)) +
                                            layer // 2)]) * (0.85 + 0.3 * rng.random()) * shade,
                            0, 1)
            canvas.ellipse(x + 1.5, y + 2.0, r, r * 0.6, (0.0, 0.0, 0.0), angle,
                           0.12 if thin else 0.3)
            canvas.ellipse(x, y, r, r * 0.6, color, angle, 1.0, 0.7)
            canvas.stroke([(x - r * 0.7 * math.cos(angle), y - r * 0.7 * math.sin(angle)),
                           (x + r * 0.7 * math.cos(angle), y + r * 0.7 * math.sin(angle))],
                          0.35, 0.35, color * 0.7, 0.6)
    return canvas.to_pixels()


def draw_foliage():
    """Dense foliage, the floor of branches of a tree next to its trunk: a
    dense canopy, darker in the gaps, lit on top."""
    return draw_foliage_of_density(7100, 70, (34, 66, 30), 1.0)


def draw_foliage_medium():
    """Thinner branches: leaves and branches over a transparent background,
    the ground showing through between them."""
    return draw_foliage_of_density(7101, 30, None, 0.95, twigs=9)


def draw_foliage_sparse():
    """The thinnest branches, at the edge of the crown: a few leaf clumps on
    bare branches."""
    return draw_foliage_of_density(7102, 11, None, 0.9, twigs=7)


def draw_grass_roof():
    """Thatch: overlapping rows of dry straw laid across the tile, each row
    shadowed under the one above it, so that rows line up from tile to tile."""
    rng = np.random.default_rng(7200)
    rows = 8
    pitch = TILE / rows
    canvas = WrapCanvas(tinted((196, 168, 96), 1.0 + 0.05 * periodic_noise(rng, 8.0, 3.0)))
    palette = [(200, 172, 98), (216, 188, 112), (180, 150, 80), (190, 160, 90), (222, 198, 126)]
    for row in range(rows):
        y0 = row * pitch
        for _ in range(70):  # Straws lying along the row, slanted every which way.
            x = rng.random() * TILE
            length = 10.0 + rng.random() * 22.0
            slant = (rng.random() - 0.5) * 0.55
            y = y0 + rng.random() * pitch * 0.8
            color = np.clip(rgb(palette[rng.integers(len(palette))]) *
                            (0.92 + 0.16 * rng.random()), 0, 1)
            canvas.stroke([(x, y), (x + length, y + length * slant)], 0.95, 0.6, color)
        # A dark band at the bottom edge of the row and a light one under it.
        canvas.stroke([(-4.0, y0 + pitch - 0.9), (TILE + 4.0, y0 + pitch - 0.9)], 1.1, 1.1,
                      rgb((96, 74, 38)), 0.75)
        canvas.stroke([(-4.0, y0 + pitch + 0.8), (TILE + 4.0, y0 + pitch + 0.8)], 0.5, 0.5,
                      rgb((236, 214, 150)), 0.35)
    return canvas.to_pixels()


GROUND_TEXTURES = {
    "ground_deep_water": draw_deep_water,
    "ground_shallow_water": draw_shallow_water,
    "ground_soil": draw_soil,
    "ground_rich_soil": draw_rich_soil,
    "ground_sand": draw_sand,
    "ground_gravel": draw_gravel,
    "ground_rough_stone": draw_rough_stone,
    "ground_foliage": draw_foliage,
    "ground_foliage_medium": draw_foliage_medium,
    "ground_foliage_sparse": draw_foliage_sparse,
    "ground_grass_roof": draw_grass_roof,
    "wall_granite": draw_granite_wall,
    "wall_sandstone": draw_sandstone_wall,
    "wall_earth": draw_earth_wall,
}


HAIR_STYLE_NAMES = {1: "short", 2: "long", 3: "curly", 4: "bun", 5: "braids"}
BEARD_STYLE_NAMES = {1: "stubble", 2: "short", 3: "full", 4: "mustache"}


def generate(output):
    os.makedirs(output, exist_ok=True)
    sprites = {}
    for stage in range(TREE_SAPLING_STAGES):
        sprites[f"tree_{stage}.png"] = draw_tree(stage)
    for stage in range(HAZEL_SEEDLING_STAGES):  # The grown hazels are tiles, like the trees.
        sprites[f"hazel_{stage}.png"] = draw_hazel(stage, False)
    for stage in range(len(BRAMBLE_STAGES)):
        sprites[f"bramble_{stage}.png"] = draw_bramble(stage, False)
        sprites[f"bramble_fruit_{stage}.png"] = draw_bramble(stage, True)
    for name, draw in HERBS:
        for size in range(len(HERB_SCALES)):
            sprites[f"{name}_{size}.png"] = draw(size)
    sprites["mushrooms_0.png"] = draw_mushrooms()
    for shape, (name, _, _, _) in enumerate(BODY_SHAPES):
        for skin in range(len(SKIN_TONES)):
            sprites[f"body_{name}_{skin}.png"] = draw_body(shape, skin)
    for skin in range(len(SKIN_TONES)):
        for expression in EXPRESSIONS:
            sprites[f"head_{skin}_{expression}.png"] = draw_head(skin, expression)
    for style, name in HAIR_STYLE_NAMES.items():
        sprites[f"hair_{name}.png"] = draw_hair(style)
    for style, name in BEARD_STYLE_NAMES.items():
        sprites[f"beard_{name}.png"] = draw_beard(style)
    for skin in range(len(SKIN_TONES)):
        sprites[f"fist_{skin}.png"] = draw_fist(skin)
    for name in TOOL_NAMES:
        sprites[f"tool_{name}.png"] = draw_tool(name)
    for species in ANIMAL_SPECIES:
        for male, sex in enumerate(ANIMAL_SEXES):
            for pose in ANIMAL_POSES:
                sprites[f"animal_{species}_{sex}_{pose}.png"] = draw_animal(species, bool(male),
                                                                            pose)
    images = {}
    for filename, canvas in sprites.items():
        images[filename] = canvas.to_image()
    for name, draw in GROUND_TEXTURES.items():
        images[f"{name}.png"] = tile_image(draw())
    for stage in range(len(GRASS_STAGES)):
        images[f"grass_{stage}.png"] = tile_image(draw_grass(stage))
    for filename, image in images.items():
        assert image.width % 32 == 0 and image.height % 32 == 0, filename
        image.save(os.path.join(output, filename), optimize=True)
    return images


def tint(image, color):
    pixels = np.asarray(image, dtype=np.float32)
    pixels[..., :3] *= np.array(color, dtype=np.float32) / 255.0
    return Image.fromarray(pixels.astype(np.uint8), "RGBA")


def preview(images, path):
    """Contact sheet on a grass-colored background, for a quick look."""
    sheet = Image.new("RGBA", (1600, 1000), (96, 128, 64, 255))
    x = 10
    for stage in range(TREE_SAPLING_STAGES):
        image = images[f"tree_{stage}.png"]
        sheet.alpha_composite(image, (x, 400 - image.height))
        x += image.width + 4
    x = 10
    for name, fruit, stages in (("hazel", False, (0, 1)), ("bramble", False, (2, 4)),
                                ("bramble", True, (2, 4))):
        for stage in stages:
            image = images[f"{name}_fruit_{stage}.png" if fruit else f"{name}_{stage}.png"]
            sheet.alpha_composite(image, (x, 600 - image.height))
            x += image.width
    x = 10 + 128 * 5 + 300
    for name, _ in HERBS:
        image = images[f"{name}_2.png"]
        sheet.alpha_composite(image, (x, 400 - image.height))
        x += 100
    sheet.alpha_composite(images["mushrooms_0.png"], (x, 400 - 128))
    hair_colors = [(44, 34, 30), (92, 60, 38), (140, 96, 58), (164, 78, 44), (220, 184, 112),
                   (176, 172, 166)]
    x = 10
    for i in range(12):
        skin = i % 5
        body = BODY_SHAPES[i % 3][0]
        expression = EXPRESSIONS[i % 4]
        layers = [images[f"body_{body}_{skin}.png"], images[f"head_{skin}_{expression}.png"]]
        beard = [None, "stubble", None, "short", None, "full", "mustache", None][i % 8]
        if beard:
            layers.append(tint(images[f"beard_{beard}.png"], hair_colors[i % 6]))
        hair = HAIR_STYLE_NAMES.get(i % 6)
        if hair:
            layers.append(tint(images[f"hair_{hair}.png"], hair_colors[(i * 5) % 6]))
        for layer in layers:
            sheet.alpha_composite(layer, (x, 790 - CHAR_H))
        x += CHAR_W
    x = 10
    for skin in range(len(SKIN_TONES)):
        sheet.alpha_composite(images[f"fist_{skin}.png"].resize((64, 64), Image.LANCZOS),
                              (x, 820))
        x += 70
    for name in TOOL_NAMES:
        tool = images[f"tool_{name}.png"].copy()
        tool.alpha_composite(images["fist_2.png"], (16, 16))
        sheet.alpha_composite(tool.resize((128, 128), Image.LANCZOS), (x, 800))
        x += 136
    # Animals next to a settler, for the scale: each species, both sexes, the three poses.
    x, y = 660, 990
    sheet.alpha_composite(images["body_average_1.png"], (520, y - CHAR_H))
    sheet.alpha_composite(images["head_1_neutral.png"], (520, y - CHAR_H))
    for species in ANIMAL_SPECIES:
        for sex in ANIMAL_SEXES:
            image = images[f"animal_{species}_{sex}_stand.png"]
            sheet.alpha_composite(image, (x, y - image.height))
            x += image.width - 20
    x, y = 10, 990
    for species in ANIMAL_SPECIES:
        for pose in ("eat", "sleep"):
            image = images[f"animal_{species}_male_{pose}.png"]
            if x + image.width < 520:
                sheet.alpha_composite(image.resize((image.width // 2, image.height // 2),
                                                   Image.LANCZOS), (x, y - image.height // 2))
                x += image.width // 2
    small = sheet.resize((800, 500), Image.LANCZOS)
    sheet.alpha_composite(small.resize((400, 250), Image.LANCZOS), (1190, 800 - 250 - 10))
    sheet.save(path)


def preview_ground(images, path):
    """Each tile texture repeated over 3 x 3 tiles (seams would show), grass
    over soil at every stage, and a wall next to the ground, at full and at
    a quarter size."""
    def tile(name):
        image = images[f"{name}.png"]
        return image.crop((TILE_PADDING, TILE_PADDING, TILE_PADDING + TILE, TILE_PADDING + TILE))

    def block(layers, tiles=3):
        out = Image.new("RGBA", (TILE * tiles, TILE * tiles))
        for y in range(tiles):
            for x in range(tiles):
                for layer in layers(x, y):
                    out.alpha_composite(tile(layer), (x * TILE, y * TILE))
        return out

    blocks = [block(lambda x, y, n=name: [n]) for name in GROUND_TEXTURES]
    blocks += [block(lambda x, y, k=stage: ["ground_soil", f"grass_{k}"])
               for stage in range(len(GRASS_STAGES))]
    blocks.append(block(lambda x, y: ["wall_granite" if x == 0 else "wall_earth" if x == 1 and y
                                      else "ground_soil"] + (["grass_2"] if x == 2 else [])))
    size = TILE * 3 + 8
    rows = (len(blocks) + 5) // 6
    sheet = Image.new("RGBA", (size * 6, size * rows + size // 2), (20, 20, 24, 255))
    for i, image in enumerate(blocks):
        sheet.alpha_composite(image, ((i % 6) * size, (i // 6) * size))
    for i, image in enumerate(blocks):
        small = image.resize((size // 4 - 2, size // 4 - 2), Image.LANCZOS)
        sheet.alpha_composite(small, ((i % 18) * (size // 4), size * rows + 8))
    sheet.save(path)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--output", default=os.path.join(root, "assets", "sprites"))
    parser.add_argument("--preview", help="also write a contact sheet to this file")
    args = parser.parse_args()
    images = generate(args.output)
    print(f"Wrote {len(images)} sprites to {args.output}")
    if args.preview:
        preview(images, args.preview)
        stem, extension = os.path.splitext(args.preview)
        preview_ground(images, f"{stem}_ground{extension}")


if __name__ == "__main__":
    main()
