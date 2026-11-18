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


def draw_moss(size):
    """A low carpet of soft green cushions, dense and uneven."""
    scale = HERB_SCALES[size]
    canvas, shadow, cx, base_y = herb_canvas()
    rng = random.Random(4600 + size)
    shadow.ellipse(cx, base_y + 2, 44 * scale + 4, 8 * scale + 2, (0, 0, 0))
    greens = [(70, 118, 54), (88, 142, 62), (56, 100, 48), (112, 160, 72)]
    for _ in range(int(14 + 22 * scale)):
        x = cx + rng.uniform(-46, 46) * scale
        y = base_y - 2 + rng.uniform(-9, 5) * scale
        r = rng.uniform(7, 13) * scale
        canvas.ellipse(x, y, r, r * 0.55, rng.choice(greens), light=0.5)
    canvas.outline(1.0, PLANT_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.15)


def draw_wood_sorrel(size):
    """Clover-like leaves of three heart-shaped leaflets on thin stalks; a
    few white flowers once grown."""
    scale = HERB_SCALES[size]
    canvas, shadow, cx, base_y = herb_canvas()
    rng = random.Random(4700 + size)
    shadow.ellipse(cx, base_y + 2, 36 * scale + 4, 8 * scale + 2, (0, 0, 0))
    green, light = (92, 158, 70), (132, 190, 96)
    for i in range(6):
        sx = cx + (i - 2.5) * 12 * scale
        top = base_y - rng.uniform(34, 52) * scale
        canvas.line([(sx, base_y), (sx + rng.uniform(-4, 4), top)], 1.4, (100, 140, 70))
        tx = sx
        for k in (-1, 0, 1):
            ox = tx + k * 9 * scale
            canvas.ellipse(ox, top - abs(k) * -2 * scale, 7.5 * scale, 6.5 * scale,
                           light if k == 0 else green, light=0.3)
    if size == 2:
        for k in range(2):
            fx = cx + (k * 2 - 1) * 20
            fy = base_y - rng.uniform(40, 52)
            canvas.line([(fx, base_y), (fx, fy)], 1.2, (130, 160, 90))
            canvas.ellipse(fx, fy, 4.2, 3.6, (0.98, 0.97, 0.98))
            canvas.ellipse(fx, fy, 1.2, 1.2, (0.9, 0.8, 0.3))
    canvas.outline(1.2, PLANT_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.18)


def draw_bracken(size):
    """Tall fronds of many leaflets, arching out and up from the ground."""
    scale = HERB_SCALES[size]
    canvas, shadow, cx, base_y = herb_canvas()
    rng = random.Random(4800 + size)
    shadow.ellipse(cx, base_y + 2, 48 * scale + 4, 10 * scale + 2, (0, 0, 0))
    green, dark = (104, 150, 62), (66, 108, 46)
    for i in range(6):
        side = -1 if i % 2 == 0 else 1
        lean = (0.25 + 0.2 * (i // 2)) * side
        height = rng.uniform(70, 104) * scale
        stem = []
        for j in range(9):
            t = j / 8.0
            stem.append((cx + lean * height * (t ** 1.4) * 1.2 + (i - 2.5) * 5 * scale,
                         base_y - height * t))
        canvas.line(stem, max(1.2, 2.4 * scale), (96, 112, 60))
        for j in range(2, 9):
            px, py = stem[j]
            t = j / 8.0
            width = (1.0 - t) * 26 * scale + 4
            for sign in (-1, 1):
                leaf(canvas, (px, py), (px + sign * width, py - width * 0.25 + 4 * scale),
                     7 * scale + 1, dark if (i + j) % 3 == 0 else green, (150, 180, 110))
    canvas.outline(1.2, PLANT_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.2)


def draw_wild_strawberry(size, fruit):
    """Rosettes of toothed trefoil leaves with runners; white flowers, and red
    berries when it bears fruit."""
    scale = HERB_SCALES[size]
    canvas, shadow, cx, base_y = herb_canvas()
    rng = random.Random(4900 + size)
    shadow.ellipse(cx, base_y + 2, 42 * scale + 4, 9 * scale + 2, (0, 0, 0))
    green, dark = (82, 140, 62), (50, 98, 46)
    for i in range(7):
        angle = math.radians(-165 + i * 25 + rng.uniform(-6, 6))
        length = rng.uniform(28, 40) * scale
        x1 = cx + math.cos(angle) * length
        y1 = base_y - 6 * scale + math.sin(angle) * length * 0.9
        canvas.line([(cx, base_y), (x1, y1)], 1.2, (96, 120, 64))
        for k in (-1, 0, 1):
            canvas.ellipse(x1 + k * 8 * scale, y1 - abs(k) * -1.5 * scale, 8 * scale, 6.5 * scale,
                           dark if (i + k) % 2 else green, light=0.3)
    if size == 2:
        for k in range(3):
            fx = cx + (k - 1) * 22 + rng.uniform(-4, 4)
            fy = base_y - rng.uniform(26, 38)
            canvas.line([(cx, base_y), (fx, fy)], 1.0, (110, 140, 76))
            if fruit:
                canvas.ellipse(fx, fy + 2, 4.0, 4.6, (200, 36, 48), light=0.5)
                for _ in range(3):
                    canvas.ellipse(fx + rng.uniform(-1.6, 1.6), fy + 2 + rng.uniform(-2, 2), 0.5,
                                   0.5, (0.96, 0.9, 0.5))
            else:
                canvas.ellipse(fx, fy, 3.8, 3.4, (0.98, 0.97, 0.98))
                canvas.ellipse(fx, fy, 1.2, 1.2, (0.95, 0.8, 0.2))
    canvas.outline(1.2, PLANT_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.18)


def draw_bilberry(size, fruit):
    """A low tuft of thin green stems with small oval leaves; blue-black
    berries when it bears fruit."""
    scale = HERB_SCALES[size]
    canvas, shadow, cx, base_y = herb_canvas()
    rng = random.Random(5000 + size)
    shadow.ellipse(cx, base_y + 2, 40 * scale + 4, 9 * scale + 2, (0, 0, 0))
    green, dark = (96, 148, 72), (58, 104, 52)
    points = []
    for i in range(9):
        sx = cx + (i - 4) * 8 * scale
        top = base_y - rng.uniform(30, 52) * scale
        canvas.line([(sx, base_y), (sx + rng.uniform(-5, 5), top)], 1.4, (88, 120, 60))
        for j in range(5):
            t = 0.3 + j * 0.17
            y = base_y + (top - base_y) * t
            for side in (-1, 1):
                canvas.ellipse(sx + side * 5 * scale, y, 5.2 * scale, 3.0 * scale,
                               dark if (i + j) % 2 else green, light=0.3)
                points.append((sx + side * 5 * scale, y))
    if fruit and size >= 1:
        for x, y in rng.sample(points, min(len(points), 5 + 4 * size)):
            canvas.ellipse(x, y + 3, 3.4, 3.4, (46, 52, 96), light=0.5)
            canvas.ellipse(x - 0.8, y + 2.2, 0.8, 0.8, (0.7, 0.75, 0.9), opacity=0.7)
    canvas.outline(1.2, PLANT_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.2)


def draw_dog_rose(size, fruit):
    """A thorny shrub of arching green canes with compound leaves; red hips
    when it bears fruit."""
    scale = HERB_SCALES[size]
    canvas, shadow, cx, base_y = herb_canvas()
    rng = random.Random(5100 + size)
    shadow.ellipse(cx, base_y + 2, 52 * scale + 4, 11 * scale + 2, (0, 0, 0))
    green, dark = (74, 124, 58), (46, 86, 42)
    tips = []
    for side, reach, rise in ((-1, 52, 70), (1, 58, 74), (-1, 30, 88), (1, 28, 92), (0, 0, 98)):
        points = []
        for i in range(8):
            t = i / 7.0
            x = cx + side * reach * t * scale
            y = base_y - rise * scale * math.sin(math.pi * (0.2 + 0.5 * t))
            points.append((x, y))
        canvas.line(points, max(1.2, 2.8 * scale), (112, 74, 58))
        for x, y in points[2:]:
            for k in (-1, 0, 1):
                canvas.ellipse(x + k * 7 * scale, y - abs(k) * -1.0 * scale - 2, 6.5 * scale,
                               4.5 * scale, dark if k else green, light=0.3)
        tips.extend(points[3:])
    if fruit and size >= 1:
        for x, y in rng.sample(tips, min(len(tips), 4 + 4 * size)):
            canvas.ellipse(x, y + 3, 3.6, 4.6, (204, 44, 40), light=0.5)
    canvas.outline(1.2, PLANT_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.22)


def draw_heather(size):
    """A low mound of fine woody sprigs, dotted with small purple bells."""
    scale = HERB_SCALES[size]
    canvas, shadow, cx, base_y = herb_canvas()
    rng = random.Random(5200 + size)
    shadow.ellipse(cx, base_y + 2, 44 * scale + 4, 9 * scale + 2, (0, 0, 0))
    green, dark = (86, 112, 66), (58, 80, 52)
    for i in range(11):
        sx = cx + (i - 5) * 7 * scale
        top = base_y - rng.uniform(22, 40) * scale * (1.0 - abs(i - 5) / 9.0)
        canvas.line([(sx, base_y), (sx + (i - 5) * 1.6 * scale, top)], 1.4, (104, 84, 66))
        for j in range(4):
            y = base_y + (top - base_y) * (0.35 + 0.2 * j)
            canvas.ellipse(sx + (i - 5) * 0.8 * scale, y, 3.6 * scale, 5.0 * scale,
                           dark if (i + j) % 2 else green, light=0.25)
    if size >= 1:
        for _ in range(int(16 * scale + 8)):
            x = cx + rng.uniform(-38, 38) * scale
            y = base_y - rng.uniform(14, 40) * scale
            canvas.ellipse(x, y, 2.2, 2.6, (158, 76, 142), light=0.4)
    canvas.outline(1.2, PLANT_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.2)


def draw_reed(size):
    """Tall stiff stems with long blades, topped with feathery brown plumes
    once grown."""
    scale = HERB_SCALES[size]
    canvas = Canvas(CATTAIL_W, CATTAIL_H)
    shadow = Canvas(CATTAIL_W, CATTAIL_H)
    rng = random.Random(5300 + size)
    cx, base_y = CATTAIL_W / 2.0, CATTAIL_H - 12.0
    shadow.ellipse(cx, base_y + 2, 36 * scale + 4, 8 * scale + 2, (0, 0, 0))
    green, dark = (128, 150, 84), (86, 112, 62)
    for i in range(9):
        x0 = cx + (i - 4) * 5 * scale
        height = rng.uniform(120, 170) * scale
        lean = (i - 4) * rng.uniform(3, 6) * scale
        canvas.line([(x0, base_y), (x0 + lean, base_y - height)], max(1.2, 2.4 * scale),
                    (150, 150, 92))
        for j in range(3):
            t = 0.35 + j * 0.22
            leaf(canvas, (x0 + lean * t, base_y - height * t),
                 (x0 + lean * t + (22 + 6 * j) * scale * (1 if i % 2 else -1),
                  base_y - height * t - 30 * scale), 6 * scale,
                 dark if (i + j) % 3 == 0 else green)
        if size == 2 and i % 2 == 0:
            canvas.ellipse(x0 + lean, base_y - height - 6, 4.0, 12.0, (150, 112, 96), light=0.4)
    canvas.outline(1.2, PLANT_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.2)


# Low plants: file name and drawing function (three sizes, except mushrooms).
HERBS = [("wild_garlic", draw_wild_garlic), ("nettle", draw_nettle),
         ("plantain", draw_plantain), ("wild_carrot", draw_wild_carrot),
         ("cattail", draw_cattail), ("moss", draw_moss), ("wood_sorrel", draw_wood_sorrel),
         ("bracken", draw_bracken), ("heather", draw_heather), ("reed", draw_reed)]
# The same, with a variant that bears fruit (named `_fruit_`, like the bramble's).
FRUIT_HERBS = [("wild_strawberry", draw_wild_strawberry), ("bilberry", draw_bilberry),
               ("dog_rose", draw_dog_rose)]


def compose(shadow, canvas, shadow_opacity):
    """Ground shadow under the sprite."""
    result = Canvas(canvas.width, canvas.height)
    result.paint_full(shadow.alpha * shadow_opacity, np.zeros(3, dtype=np.float32))
    result.color = canvas.color + result.color * (1.0 - canvas.alpha)[..., None]
    result.alpha = canvas.alpha + result.alpha * (1.0 - canvas.alpha)
    return result


# ---------------------------------------------------------------------------
# Animals. One sprite per species, sex and pose: standing, eating (head down)
# and asleep (lying), all three seen from the side, facing right (the game
# mirrors them), then standing seen from the front and from behind.
# The feet are on the bottom center of the canvas, like the characters'.
# ---------------------------------------------------------------------------

ANIMAL_OUTLINE = (34, 26, 20)
ANIMAL_POSES = ["stand", "eat", "sleep", "front", "back"]
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
    "roe_deer": {"canvas": (128, 128), "facing_canvas": (96, 128), "size": 1.0, "female_size": 0.94, "body": (70.0, 26.0),
                 "leg": 34.0, "leg_width": 5.0, "neck": (24.0, 11.0), "head": (17.0, 13.0),
                 "ear": 14.0, "antlers": (20.0, 2), "coat": (176, 112, 62),
                 "belly": (222, 190, 150), "rump": (244, 236, 220), "tail": (244, 236, 220)},
    "red_deer": {"canvas": (224, 192), "facing_canvas": (128, 192), "size": 1.0, "female_size": 0.88, "body": (124.0, 44.0),
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


# Standing animals seen from the front (walking down the screen) and from
# behind (walking up): the same sizes and colors as the side views, the feet
# on the bottom center of a narrower canvas.

def antlers_facing(canvas, cx, base_y, head_w, size, points):
    """Both antlers seen from the front or from behind: beams spreading up
    and out, tines pointing up."""
    for side in (-1, 1):
        base = (cx + side * head_w * 0.24, base_y)
        elbow = (cx + side * size * 0.3, base_y - size * 0.32)
        tip = (cx + side * size * 0.36, base_y - size * 0.68)
        canvas.line([base, elbow, tip], max(2.0, size * 0.07), ANTLER)
        for i in range(points):
            t = (i + 0.5) / (points + 0.3)
            start = (base[0] + (tip[0] - base[0]) * t + side * size * 0.06 * math.sin(math.pi * t),
                     base[1] + (tip[1] - base[1]) * t)
            lean = -side * (0.12 if i else -0.2)
            canvas.line([start, (start[0] + size * lean, start[1] - size * (0.26 - 0.03 * i))],
                        max(1.6, size * 0.05), ANTLER)


def draw_deer_facing(spec, male, back):
    width, height = spec["facing_canvas"]
    k = spec["size"] * (1.0 if male else spec["female_size"])
    coat, belly, dark = spec["coat"], spec["belly"], shade(spec["coat"], 0.62)
    hoof = shade(dark, 0.5)
    canvas, shadow = Canvas(width, height), Canvas(width, height)
    ground = height - ANIMAL_GROUND
    body_h, leg_len = spec["body"][1] * k, spec["leg"] * k
    body_w = body_h * 0.92
    leg_w = spec["leg_width"] * k
    cx = width * 0.5
    cy = ground - leg_len - body_h * 0.5
    shadow.ellipse(cx, ground, body_w * 0.75, 6.0 * k + 3.0, (0, 0, 0))
    neck_len, neck_w = spec["neck"][0] * k, spec["neck"][1] * k
    head_len, head_w = spec["head"][0] * k, spec["head"][1] * k
    shoulder = (cx, cy - body_h * 0.2)
    nape = (cx, shoulder[1] - neck_len * 0.85)

    def far_legs():
        for side in (-1, 1):
            leg(canvas, (cx + side * body_w * 0.36, cy), ground - 2, leg_len + body_h * 0.4, leg_w,
                dark, 0.0, hoof)

    def near_legs():
        for side in (-1, 1):
            leg(canvas, (cx + side * body_w * 0.24, cy + body_h * 0.15), ground,
                leg_len + body_h * 0.3, leg_w, coat, 0.0, hoof)

    def ears_and_antlers():
        for side in (-1, 1):
            ear(canvas, (cx + side * head_w * 0.34, nape[1] - head_w * 0.12), 90 - side * 58,
                spec["ear"] * k, head_w * 0.52, shade(coat, 0.85) if back else dark)
        if male and spec["antlers"]:
            size, points = spec["antlers"]
            antlers_facing(canvas, cx, nape[1] - head_w * 0.38, head_w, size * k, points)

    def trunk():
        canvas.ellipse(cx, cy, body_w * 0.5, body_h * 0.54, coat, light=0.22)

    if back:
        far_legs()  # The forelegs, between the hind ones.
        taper(canvas, shoulder, nape, neck_w * 1.25, neck_w * 0.82, shade(coat, 0.92))
        ears_and_antlers()
        canvas.ellipse(nape[0], nape[1], head_w * 0.52, head_w * 0.54, coat, light=0.2)
        trunk()
        mask = canvas.coverage()
        canvas.ellipse(cx, cy + body_h * 0.08, body_w * 0.34, body_h * 0.4, spec["rump"], clip=mask)
        canvas.line([(cx, cy - body_h * 0.3), (cx, cy + body_h * 0.02)], 3.0 * k + 1.0,
                    spec["tail"])
        near_legs()
    else:
        far_legs()  # The hind legs, behind and wider apart.
        trunk()
        mask = canvas.coverage()
        canvas.ellipse(cx, cy + body_h * 0.42, body_w * 0.36, body_h * 0.2, belly, clip=mask,
                       opacity=0.6)
        near_legs()
        taper(canvas, shoulder, nape, neck_w * 1.3, neck_w * 0.85, coat)
        ears_and_antlers()
        nose = (cx, nape[1] + head_len * 0.6)
        canvas.ellipse(nape[0], nape[1], head_w * 0.52, head_w * 0.54, coat, light=0.2)
        taper(canvas, nape, nose, head_w * 0.92, head_w * 0.46, coat)
        canvas.ellipse(nose[0], nose[1] + head_w * 0.06, head_w * 0.2, head_w * 0.15, (40, 30, 28))
        for side in (-1, 1):
            canvas.ellipse(cx + side * head_w * 0.3, nape[1] + head_len * 0.08, 2.2 * k + 0.4,
                           2.2 * k + 0.4, (24, 18, 16))
    canvas.outline(2.0, ANIMAL_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.3)


def draw_boar_facing(male, back):
    width, height = 128, 128
    k = 1.0 if male else 0.88
    coat, dark, light = (86, 70, 58), (50, 40, 34), (122, 104, 88)
    hoof = (24, 20, 18)
    canvas, shadow = Canvas(width, height), Canvas(width, height)
    ground = height - ANIMAL_GROUND
    body_w, body_h, leg_len = 54.0 * k, 46.0 * k, 22.0 * k
    cx = width * 0.5
    cy = ground - leg_len - body_h * 0.5
    leg_w = 8.5 * k
    shadow.ellipse(cx, ground, body_w * 0.62, 8.0, (0, 0, 0))
    for side in (-1, 1):  # The legs of the far end, wider apart.
        leg(canvas, (cx + side * body_w * 0.36, cy), ground - 2, leg_len + body_h * 0.4,
            leg_w * 0.9, dark, 0.0, hoof)
    if back:  # The ears show over the back.
        for side in (-1, 1):
            ear(canvas, (cx + side * 13.0 * k, cy - body_h * 0.36), 90 - side * 30, 16.0 * k,
                11.0 * k, dark)
    # Bristles on top of the back.
    rng = random.Random(7)
    top = cy - body_h * 0.5
    for i in range(9):
        t = i / 8.0 - 0.5
        x = cx + t * body_w * 0.5
        y = top + body_h * 0.5 * t * t
        canvas.line([(x, y + 6), (x + t * 5 + rng.random() - 0.5, y - 5 * k - rng.random() * 3)],
                    2.4, dark)
    canvas.ellipse(cx, cy, body_w * 0.5, body_h * 0.52, coat, light=0.22)
    trunk = canvas.coverage()
    canvas.ellipse(cx, cy + body_h * 0.42, body_w * 0.4, body_h * 0.16, light, clip=trunk,
                   opacity=0.5)
    for side in (-1, 1):
        leg(canvas, (cx + side * body_w * 0.25, cy + body_h * 0.2), ground,
            leg_len + body_h * 0.25, leg_w, coat, 0.0, hoof)
    if back:
        # The ridge of bristles runs down the middle of the back, to the tail.
        for i in range(7):
            y = top + 3.0 + i * body_h * 0.07
            canvas.line([(cx - 3.5 * k, y - 2.0), (cx, y + 2.5), (cx + 3.5 * k, y - 2.0)], 2.0,
                        dark, clip=trunk)
        canvas.line([(cx, cy + body_h * 0.02), (cx + 2.0 * k, cy + body_h * 0.2),
                     (cx + 1.0 * k, cy + body_h * 0.34)], 2.6, dark)
        canvas.ellipse(cx + 1.0 * k, cy + body_h * 0.36, 2.4 * k, 3.4 * k, dark)
    else:
        # The head, low in front of the chest: a wedge narrowing down to the snout.
        skull = (cx, cy + body_h * 0.02)
        snout = (cx, skull[1] + 24.0 * k)
        for side in (-1, 1):
            ear(canvas, (skull[0] + side * 14.0 * k, skull[1] - 11.0 * k), 90 - side * 38,
                17.0 * k, 12.0 * k, dark)
        canvas.ellipse(skull[0], skull[1], 20.0 * k, 19.0 * k, shade(coat, 1.12), light=0.2)
        taper(canvas, skull, snout, 37.0 * k, 17.0 * k, shade(coat, 1.12))
        canvas.line([(cx, skull[1] - 4.0 * k), (cx, snout[1] - 7.0 * k)], 5.0 * k,
                    shade(coat, 0.95), opacity=0.7)
        canvas.ellipse(snout[0], snout[1] + 1.0 * k, 8.5 * k, 6.5 * k, (58, 44, 44))
        for side in (-1, 1):
            canvas.ellipse(snout[0] + side * 3.2 * k, snout[1] + 1.2 * k, 1.5 * k, 2.2 * k,
                           (28, 20, 20))
            if male:
                jaw = (snout[0] + side * 8.0 * k, snout[1] - 1.0 * k)
                canvas.line([jaw, (jaw[0] + side * 4.0, jaw[1] - 5.0),
                             (jaw[0] + side * 4.5, jaw[1] - 11.0)], 3.0, (240, 232, 210))
            canvas.ellipse(skull[0] + side * 10.0 * k, skull[1] - 0.5 * k, 2.4, 2.4, (16, 12, 10))
    canvas.outline(2.0, ANIMAL_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.3)


def draw_hare_facing(back):
    width, height = 64, 64
    coat, dark, belly = (176, 142, 100), (112, 86, 60), (232, 218, 196)
    canvas, shadow = Canvas(width, height), Canvas(width, height)
    ground = height - 6.0
    cx = width * 0.5
    cy = ground - 13.5
    shadow.ellipse(cx, ground, 15.0, 4.0, (0, 0, 0))
    skull = (cx, cy - 11.0)

    def ears():
        for side in (-1, 1):
            root = (skull[0] + side * 3.0, skull[1] - 3.0)
            tip = (skull[0] + side * 6.5, skull[1] - 21.0)
            canvas.line([root, tip], 4.4, coat)
            canvas.line([(root[0] + (tip[0] - root[0]) * 0.75, root[1] + (tip[1] - root[1]) * 0.75),
                         tip], 4.0, (40, 32, 28))

    if back:
        ears()
        canvas.ellipse(skull[0], skull[1], 7.2, 6.8, coat, light=0.2)
    for side in (-1, 1):  # Hind feet, out on both sides.
        canvas.line([(cx + side * 6.0, ground - 2.0), (cx + side * 12.0, ground - 1.5)], 4.2, dark)
    canvas.ellipse(cx, cy + 1.0, 12.0, 13.0, coat, light=0.22)
    trunk = canvas.coverage()
    if back:
        canvas.ellipse(cx, cy + 7.0, 4.2, 4.0, (244, 240, 230))  # Scut.
    else:
        canvas.ellipse(cx, cy + 6.0, 6.5, 7.5, belly, clip=trunk, opacity=0.8)
        for side in (-1, 1):  # Forelegs.
            canvas.line([(cx + side * 3.5, cy + 3.0), (cx + side * 3.5, ground - 1.5)], 3.4, coat)
        ears()
        canvas.ellipse(skull[0], skull[1], 7.4, 7.0, coat, light=0.2)
        canvas.ellipse(skull[0], skull[1] + 3.6, 4.6, 3.6, shade(coat, 1.12))
        canvas.ellipse(skull[0], skull[1] + 2.6, 1.6, 1.3, (60, 40, 40))
        for side in (-1, 1):
            canvas.ellipse(skull[0] + side * 4.6, skull[1] - 0.8, 1.8, 1.9, (24, 18, 16))
    canvas.outline(1.6, ANIMAL_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.3)


# Canids: the wolf (grey, lean, long legs) and the fox (red, black stockings,
# a bush of a tail with a white tip). No antlers, no tusks, a short neck and a
# pointed muzzle; the males are a little larger.
CANIDS = {
    "wolf": {"canvas": (192, 128), "facing_canvas": (96, 128), "female_size": 0.9,
             "body": (82.0, 29.0), "leg": 38.0, "leg_width": 7.0, "neck": 15.0,
             "head": (27.0, 17.0), "ear": 13.0, "tail": 32.0, "coat": (126, 122, 114),
             "back": (82, 78, 74), "belly": (196, 190, 176), "legs": (104, 98, 90),
             "tail_tip": (60, 56, 54), "muzzle": (200, 194, 180)},
    "fox": {"canvas": (128, 96), "facing_canvas": (64, 96), "female_size": 0.92,
            "body": (50.0, 17.0), "leg": 21.0, "leg_width": 4.6, "neck": 9.0,
            "head": (19.0, 11.5), "ear": 11.0, "tail": 30.0, "coat": (204, 104, 44),
            "back": (170, 78, 34), "belly": (244, 234, 218), "legs": (54, 40, 34),
            "tail_tip": (246, 240, 228), "muzzle": (246, 238, 224)},
}


def draw_canid(spec, male, pose):
    """A wolf or a fox from the side, facing right: standing with the head
    carried low, eating (head down), or asleep (curled, the muzzle on its paws)."""
    width, height = spec["canvas"]
    k = 1.0 if male else spec["female_size"]
    coat, back, belly, dark = spec["coat"], spec["back"], spec["belly"], spec["legs"]
    canvas, shadow = Canvas(width, height), Canvas(width, height)
    ground = height - ANIMAL_GROUND
    body_len, body_h, leg_len = spec["body"][0] * k, spec["body"][1] * k, spec["leg"] * k
    lying = pose == "sleep"
    cx = width * 0.5 + body_len * 0.02
    cy = ground - body_h * 0.5 - (body_h * 0.08 if lying else leg_len)
    shadow.ellipse(cx + body_len * 0.06, ground, body_len * 0.62, 5.0 * k + 2.5, (0, 0, 0))
    front_x, hind_x = cx + body_len * 0.33, cx - body_len * 0.32
    leg_w = spec["leg_width"] * k
    paw = shade(dark, 0.6)
    if not lying:  # The legs of the far side, darker.
        leg(canvas, (front_x + leg_w, cy), ground - 2, leg_len + body_h * 0.4, leg_w,
            shade(dark, 0.85), leg_w * 0.3, paw)
        leg(canvas, (hind_x + leg_w * 1.5, cy), ground - 2, leg_len + body_h * 0.4, leg_w,
            shade(dark, 0.85), -leg_w * 0.7, paw)
    # The brush: a thick tail trailing behind, its tip another color.
    tail_base = (cx - body_len * 0.46, cy - body_h * 0.12)
    tail_angle = {"stand": 232, "eat": 222, "sleep": 175}[pose]
    tail_tip = reach(tail_base, tail_angle, spec["tail"] * k)
    taper(canvas, tail_base, tail_tip, body_h * 0.62, body_h * 0.78, back)
    canvas.ellipse(tail_tip[0], tail_tip[1], body_h * 0.42, body_h * 0.42, spec["tail_tip"])
    # The trunk: a deep chest, a lean flank.
    canvas.ellipse(cx, cy, body_len * 0.5, body_h * 0.5, coat, light=0.22)
    canvas.ellipse(cx + body_len * 0.26, cy + body_h * 0.04, body_len * 0.24, body_h * 0.54, coat,
                   light=0.2)
    canvas.ellipse(cx - body_len * 0.28, cy - body_h * 0.02, body_len * 0.22, body_h * 0.5, coat,
                   light=0.22)
    trunk = canvas.coverage()
    canvas.ellipse(cx, cy - body_h * 0.4, body_len * 0.42, body_h * 0.2, back, clip=trunk,
                   opacity=0.8)
    canvas.ellipse(cx + body_len * 0.1, cy + body_h * 0.42, body_len * 0.4, body_h * 0.18, belly,
                   clip=trunk, opacity=0.8)
    if lying:  # Legs folded under it.
        canvas.ellipse(front_x, ground - 3, body_len * 0.13, 4.0 * k + 1.0, dark)
        canvas.ellipse(hind_x + body_len * 0.08, ground - 3, body_len * 0.16, 4.5 * k + 1.0, dark)
    else:
        leg(canvas, (front_x, cy + body_h * 0.1), ground, leg_len + body_h * 0.3, leg_w, dark,
            leg_w * 0.2, paw)
        leg(canvas, (hind_x, cy + body_h * 0.05), ground, leg_len + body_h * 0.35, leg_w, dark,
            -leg_w * 0.8, paw)
    neck_angle, head_angle = {"stand": (28, -14), "eat": (-38, -62), "sleep": (8, -28)}[pose]
    shoulder = (cx + body_len * 0.4, cy - body_h * 0.2)
    nape = reach(shoulder, neck_angle, spec["neck"] * k)
    taper(canvas, reach(shoulder, neck_angle + 180, 4.0 * k), nape, body_h * 0.7, body_h * 0.5,
          coat)
    head_len, head_w = spec["head"][0] * k, spec["head"][1] * k
    nose = reach(nape, head_angle, head_len)
    ear(canvas, reach(nape, head_angle + 120, head_w * 0.2), head_angle + 118,
        spec["ear"] * k, head_w * 0.5, shade(coat, 0.7))
    taper(canvas, nape, nose, head_w * 0.95, head_w * 0.4, coat)
    canvas.ellipse(nape[0], nape[1], head_w * 0.56, head_w * 0.54, coat, light=0.2)
    taper(canvas, reach(nape, head_angle, head_len * 0.35), nose, head_w * 0.5, head_w * 0.36,
          spec["muzzle"])
    ear(canvas, reach(nape, head_angle + 105, head_w * 0.25), head_angle + 95, spec["ear"] * k,
        head_w * 0.5, back)
    canvas.ellipse(nose[0] + head_w * 0.04, nose[1], head_w * 0.16, head_w * 0.14, (30, 22, 20))
    eye = reach(reach(nape, head_angle, head_len * 0.22), head_angle + 90, head_w * 0.16)
    if lying:
        canvas.line([(eye[0] - 1.8 * k, eye[1]), (eye[0] + 1.8 * k, eye[1])], 1.5, (24, 18, 16))
    else:
        canvas.ellipse(eye[0], eye[1], 1.9 * k + 0.3, 1.7 * k + 0.3, (230, 190, 70))
        canvas.ellipse(eye[0] + 0.4, eye[1], 0.9 * k, 0.9 * k, (20, 14, 12))
    canvas.outline(2.0, ANIMAL_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.3)


def draw_canid_facing(spec, male, back_view):
    width, height = spec["facing_canvas"]
    k = 1.0 if male else spec["female_size"]
    coat, back, belly, dark = spec["coat"], spec["back"], spec["belly"], spec["legs"]
    paw = shade(dark, 0.6)
    canvas, shadow = Canvas(width, height), Canvas(width, height)
    ground = height - ANIMAL_GROUND
    body_h, leg_len = spec["body"][1] * k, spec["leg"] * k
    body_w = body_h * 0.95
    leg_w = spec["leg_width"] * k
    cx = width * 0.5
    cy = ground - leg_len - body_h * 0.5
    shadow.ellipse(cx, ground, body_w * 0.8, 5.0 * k + 2.5, (0, 0, 0))
    head_w = spec["head"][1] * k
    nape = (cx, cy - body_h * 0.28 - spec["neck"] * k * 0.4)
    for side in (-1, 1):  # The legs of the far end.
        leg(canvas, (cx + side * body_w * 0.36, cy), ground - 2, leg_len + body_h * 0.4, leg_w,
            shade(dark, 0.85), 0.0, paw)
    if back_view:  # The brush hangs down the middle of the rump.
        for side in (-1, 1):
            ear(canvas, (cx + side * head_w * 0.34, nape[1] - head_w * 0.1), 90 - side * 22,
                spec["ear"] * k, head_w * 0.5, back)
        canvas.ellipse(nape[0], nape[1], head_w * 0.52, head_w * 0.5, coat, light=0.2)
    canvas.ellipse(cx, cy, body_w * 0.5, body_h * 0.54, coat, light=0.22)
    trunk = canvas.coverage()
    for side in (-1, 1):
        leg(canvas, (cx + side * body_w * 0.24, cy + body_h * 0.18), ground,
            leg_len + body_h * 0.3, leg_w, dark, 0.0, paw)
    if back_view:
        canvas.ellipse(cx, cy - body_h * 0.1, body_w * 0.3, body_h * 0.46, back, clip=trunk,
                       opacity=0.8)
        tail_tip = (cx, cy + body_h * 0.62)
        taper(canvas, (cx, cy - body_h * 0.05), tail_tip, body_h * 0.4, body_h * 0.46, back)
        canvas.ellipse(tail_tip[0], tail_tip[1], body_h * 0.28, body_h * 0.26, spec["tail_tip"])
    else:
        canvas.ellipse(cx, cy + body_h * 0.2, body_w * 0.32, body_h * 0.34, belly, clip=trunk,
                       opacity=0.85)
        for side in (-1, 1):
            ear(canvas, (cx + side * head_w * 0.34, nape[1] - head_w * 0.1), 90 - side * 22,
                spec["ear"] * k, head_w * 0.5, back)
        canvas.ellipse(nape[0], nape[1], head_w * 0.54, head_w * 0.5, coat, light=0.2)
        snout = (cx, nape[1] + head_w * 0.55)
        taper(canvas, (cx, nape[1] + head_w * 0.1), snout, head_w * 0.62, head_w * 0.3,
              spec["muzzle"])
        canvas.ellipse(snout[0], snout[1] + 0.6, head_w * 0.15, head_w * 0.12, (30, 22, 20))
        for side in (-1, 1):
            canvas.ellipse(cx + side * head_w * 0.22, nape[1] - head_w * 0.06, 1.7 * k + 0.3,
                           1.5 * k + 0.3, (230, 190, 70))
            canvas.ellipse(cx + side * head_w * 0.22, nape[1] - head_w * 0.06, 0.8 * k, 0.8 * k,
                           (20, 14, 12))
    canvas.outline(2.0, ANIMAL_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.3)


def draw_animal(species, male, pose):
    if pose in ("front", "back"):
        if species in DEER:
            return draw_deer_facing(DEER[species], male, pose == "back")
        if species == "boar":
            return draw_boar_facing(male, pose == "back")
        if species in CANIDS:
            return draw_canid_facing(CANIDS[species], male, pose == "back")
        return draw_hare_facing(pose == "back")
    if species in DEER:
        return draw_deer(DEER[species], male, pose)
    if species == "boar":
        return draw_boar(male, pose)
    if species in CANIDS:
        return draw_canid(CANIDS[species], male, pose)
    return draw_hare(pose)


ANIMAL_SPECIES = ["roe_deer", "red_deer", "boar", "hare", "wolf", "fox"]


# ---------------------------------------------------------------------------
# Characters. All layers share one canvas: 128x192, feet at the bottom
# center. Layers: body, head (with the face), beard, hair. Each comes in three
# views: from the front, from the side (facing right, the game mirrors it) and
# from behind (no face: one head per skin, no beard).
# ---------------------------------------------------------------------------

CHAR_VIEWS = ["front", "side", "back"]
# Seen from the side, a body is this much of its width from the front.
BODY_DEPTH = 0.68

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


def draw_body(shape, skin, view="front"):
    """Naked body without arms nor legs: a rounded trunk the head sits on.
    Clothes will be layers drawn over it."""
    _, half_width, taper, bulge = BODY_SHAPES[shape]
    skin_color = SKIN_TONES[skin]
    canvas = Canvas(CHAR_W, CHAR_H)
    shadow = Canvas(CHAR_W, CHAR_H)
    steps = 48
    height = BODY_BOTTOM - BODY_TOP
    if view == "side":
        # The belly leads, the back is straighter with a hint of buttocks.
        depth = half_width * BODY_DEPTH
        half_width = depth
        shadow.ellipse(CX, FEET_Y - 3, depth * 1.05, 7, (0, 0, 0))
        front = [(CX + body_half_width(i / steps, depth, taper, bulge * 2.4 + 0.06),
                  BODY_TOP + height * i / steps) for i in range(steps + 1)]
        behind = []
        for i in range(steps + 1):
            t = i / steps
            rump = 0.12 * math.sin(math.pi * min(1.0, max(0.0, (t - 0.45) / 0.5)))
            behind.append((CX - body_half_width(t, depth, taper * 0.5, 0.0) * (1.0 + rump),
                           BODY_TOP + height * t))
        outline = front + list(reversed(behind))
    else:
        shadow.ellipse(CX, FEET_Y - 3, half_width * 0.9, 7, (0, 0, 0))
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
    # Shoulder highlight.
    canvas.ellipse(CX - half_width * 0.45, BODY_TOP + 14, half_width * 0.35, 6,
                   shade(skin_color, 1.1), clip=mask, opacity=0.5)
    crease = shade(skin_color, 0.75)
    if view == "front":  # A hint of the navel, a fold under a belly.
        navel_y = BODY_TOP + height * (0.62 if bulge <= 0 else 0.6)
        canvas.ellipse(CX + 1, navel_y, 1.6, 2.2, shade(skin_color, 0.62), opacity=0.8)
        if bulge > 0:
            canvas.line([(CX - half_width * 0.5, BODY_TOP + height * 0.8),
                         (CX, BODY_TOP + height * 0.83),
                         (CX + half_width * 0.5, BODY_TOP + height * 0.8)], 1.6, crease,
                        clip=mask, opacity=0.7)
    elif view == "back":  # The spine and the buttocks.
        canvas.line([(CX, BODY_TOP + height * 0.2), (CX, BODY_TOP + height * 0.62)], 1.6, crease,
                    clip=mask, opacity=0.45)
        canvas.line([(CX, BODY_TOP + height * 0.78), (CX, BODY_TOP + height * 0.97)], 1.6, crease,
                    clip=mask, opacity=0.7)
    canvas.outline(2.0, CHAR_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.3)


def eye(canvas, x, y, expression, squeeze=1.0):
    """An eye; `squeeze` narrows it for a head seen from the side."""
    dark = (40, 30, 26)
    if expression == "smiling":
        # Closed, happy eyes: upward arcs.
        canvas.line([(x - 4.5 * squeeze, y + 1.5), (x, y - 2.5), (x + 4.5 * squeeze, y + 1.5)],
                    2.4, dark)
        return
    ry = 4.6 if expression != "sad" else 4.0
    oy = 0.0 if expression != "sad" else 1.0
    look = 0.5 if squeeze == 1.0 else 1.2  # Looking ahead.
    canvas.ellipse(x, y + oy, 5.2 * squeeze, ry + 0.6, (250, 248, 240))
    canvas.ellipse(x + look, y + oy + 0.5, 3.4 * squeeze, ry * 0.85, dark)
    canvas.ellipse(x + look - 1.1, y + oy - 1.4, 1.2, 1.2, (255, 255, 255))


# A head seen from the side: the skull sits a little back, the face leads.
SIDE_SKULL_X = CX - 2.0
SIDE_EAR_X = CX - 6.0


def draw_head_side(skin, expression):
    skin_color = SKIN_TONES[skin]
    canvas = Canvas(CHAR_W, CHAR_H)
    # The jaw first: only its chin shows under the skull.
    canvas.ellipse(CX + 3, HEAD_CY + 12, HEAD_RX * 0.82, HEAD_RY * 0.62, shade(skin_color, 0.94),
                   light=0.2)
    canvas.ellipse(SIDE_SKULL_X, HEAD_CY, HEAD_RX, HEAD_RY, skin_color, light=0.32)
    # The nose sticks out of the profile.
    canvas.ellipse(CX + 25.5, HEAD_CY + 10.5, 4.6, 3.8, skin_color, light=0.2)
    canvas.ellipse(CX + 24, HEAD_CY + 13.5, 2.6, 1.4, shade(skin_color, 0.8))
    canvas.ellipse(SIDE_EAR_X, HEAD_CY + 4, 5.5, 8, shade(skin_color, 0.9))
    canvas.ellipse(SIDE_EAR_X - 0.5, HEAD_CY + 4, 2.8, 4.8, shade(skin_color, 0.75))
    eye_x, eye_y = CX + 15.0, HEAD_CY + 4
    brow = shade(skin_color, 0.42)
    eye(canvas, eye_x, eye_y, expression, squeeze=0.72)
    front, behind = eye_x + 5.5, eye_x - 5
    if expression == "neutral":
        canvas.line([(front, eye_y - 9), (behind, eye_y - 9.5)], 2.6, brow)
    elif expression == "smiling":
        canvas.line([(front, eye_y - 9.5), (eye_x, eye_y - 11.5), (behind, eye_y - 10)], 2.6, brow)
    elif expression == "sad":
        canvas.line([(front, eye_y - 12), (behind, eye_y - 8)], 2.6, brow)
    else:  # angry
        canvas.line([(front, eye_y - 6.5), (behind, eye_y - 11)], 3.0, brow)
    mouth_y = HEAD_CY + 20
    lips = (120, 52, 48)
    if expression == "neutral":
        canvas.line([(CX + 13, mouth_y), (CX + 21.5, mouth_y)], 2.2, lips)
    elif expression == "smiling":
        canvas.line([(CX + 21.5, mouth_y + 1), (CX + 15, mouth_y + 1.5), (CX + 10.5, mouth_y - 3)],
                    2.4, (110, 40, 40))
        canvas.ellipse(CX + 9, HEAD_CY + 13, 5, 3, (236, 120, 110), opacity=0.35)
    elif expression == "sad":
        canvas.line([(CX + 21.5, mouth_y), (CX + 15, mouth_y), (CX + 11, mouth_y + 3.5)], 2.2,
                    lips)
    else:  # angry
        canvas.polygon([(CX + 11, mouth_y - 1), (CX + 21.5, mouth_y - 1), (CX + 21.5, mouth_y + 3),
                        (CX + 10, mouth_y + 3)], (110, 40, 40))
        canvas.line([(CX + 12, mouth_y + 1), (CX + 21, mouth_y + 1)], 1.6, (250, 246, 236))
    canvas.outline(2.0, CHAR_OUTLINE)
    return canvas


def draw_head_back(skin):
    """The back of a head: no face, so one sprite per skin serves every mood."""
    skin_color = SKIN_TONES[skin]
    canvas = Canvas(CHAR_W, CHAR_H)
    for side in (-1, 1):
        canvas.ellipse(CX + side * (HEAD_RX - 1), HEAD_CY + 3, 6, 8, shade(skin_color, 0.9))
    # The jaw first: only its lower edge shows under the skull.
    canvas.ellipse(CX, HEAD_CY + 12, HEAD_RX * 0.86, HEAD_RY * 0.62, shade(skin_color, 0.9),
                   light=0.2)
    canvas.ellipse(CX, HEAD_CY, HEAD_RX, HEAD_RY, skin_color, light=0.32)
    canvas.outline(2.0, CHAR_OUTLINE)
    return canvas


def draw_head(skin, expression, view="front", eyes=True):
    if view == "side":
        return draw_head_side(skin, expression)
    if view == "back":
        return draw_head_back(skin)
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
        if eyes:
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


def draw_closed_eyes(skin):
    """A layer over the front head, for a settler asleep: the skin of the face
    (copied from a head drawn without eyes, so that it matches) covering both
    open eyes, with a closed lid line in each. It leaves the brows alone, so
    it serves every expression."""
    bare = draw_head(skin, "neutral", "front", eyes=False)
    cover = Canvas(CHAR_W, CHAR_H)
    eye_y = HEAD_CY + 4
    for side in (-1, 1):
        cover.ellipse(CX + side * 11, eye_y + 0.2, 6.4, 5.6, (1.0, 1.0, 1.0))
    canvas = Canvas(CHAR_W, CHAR_H)
    canvas.color = bare.color * cover.alpha[..., None]
    canvas.alpha = bare.alpha * cover.alpha
    lid = shade(SKIN_TONES[skin], 0.4)
    for side in (-1, 1):
        x = CX + side * 11
        canvas.line([(x - 4.8, eye_y - 0.6), (x - 2.0, eye_y + 1.6), (x + 2.0, eye_y + 1.6),
                     (x + 4.8, eye_y - 0.6)], 2.2, lid)
    return canvas


# A "Z" of sleep, white with a dark blue edge: the game draws a few of them
# rising from a sleeper's head, growing and fading.
SLEEP_Z_SIZE = 32
SLEEP_Z_FILL = (236, 240, 255)
SLEEP_Z_EDGE = (30, 40, 90)


def draw_sleep_z():
    canvas = Canvas(SLEEP_Z_SIZE, SLEEP_Z_SIZE)
    canvas.polygon([(6, 6), (26, 6), (26, 11), (14, 21), (26, 21), (26, 26), (6, 26), (6, 21),
                    (18, 11), (6, 11)], SLEEP_Z_FILL)
    canvas.outline(2.0, SLEEP_Z_EDGE)
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


def side_face_hole(canvas, top, ear=False):
    """Mask of the face of a head seen from the side, below the hairline
    `top`; with the ear when the hair is short enough to show it."""
    result = Canvas(CHAR_W, CHAR_H)
    result.ellipse(CX + 21, HEAD_CY + 8, 23, HEAD_RY - 1, (1.0, 1.0, 1.0))
    hole = result.alpha
    _, gy = canvas.full_grids()
    hole = hole * np.clip((gy - top) * SUPERSAMPLING, 0.0, 1.0)
    if ear:
        lobe = Canvas(CHAR_W, CHAR_H)
        lobe.ellipse(SIDE_EAR_X + 1, HEAD_CY + 6, 7.5, 10, (1.0, 1.0, 1.0))
        hole = np.maximum(hole, lobe.alpha)
    return hole


def braid(canvas, x, top, side):
    """A braid hanging from `top`, leaning a little to `side`, with its tie."""
    base = rgb(HAIR_BASE)
    for k in range(6):
        y = top + k * 9.5
        r = 6.5 - k * 0.35
        bx = x - side * k * 0.6
        canvas.ellipse(bx, y, r, r * 1.05, base, light=0.4)
        canvas.line([(bx - r * 0.7, y - r * 0.3), (bx + r * 0.7, y + r * 0.3)], 1.2, HAIR_DARK)
    end = x - side * 3.0
    canvas.ellipse(end, top + 59, 4.5, 3, HAIR_DARK)  # Tie.
    canvas.polygon([(end - 3, top + 60), (end + 3, top + 60), (end + 1, top + 70),
                    (end - 2, top + 70)], HAIR_BASE)


def draw_hair_side(style):
    canvas = Canvas(CHAR_W, CHAR_H)
    rng = random.Random(4100 + style)
    base = rgb(HAIR_BASE)
    sx = SIDE_SKULL_X
    if style == 1:  # Short crop.
        canvas.ellipse(sx, HEAD_CY - 6, HEAD_RX + 2.5, HEAD_RY - 1, base, light=0.3)
        canvas.erase_full(side_face_hole(canvas, HEAD_CY - 15, ear=True))
        for i in range(4):  # Fringe tufts over the forehead.
            x = CX + 6 + i * 6
            canvas.polygon([(x - 4, HEAD_CY - 17), (x + 4, HEAD_CY - 17), (x + 2, HEAD_CY - 10)],
                           HAIR_BASE)
        hair_strands(canvas, rng, 40, sx - 28, sx + 26, HEAD_CY - 32, HEAD_CY + 10, 7)
    elif style == 2:  # Long, falling down the back.
        canvas.ellipse(sx, HEAD_CY - 5, HEAD_RX + 3.5, HEAD_RY, base, light=0.3)
        canvas.polygon([(sx - 29, HEAD_CY - 8), (sx - 32, HEAD_CY + 48), (sx - 24, HEAD_CY + 55),
                        (CX - 2, HEAD_CY + 51), (CX + 5, HEAD_CY + 30), (CX + 5, HEAD_CY - 10)],
                       HAIR_BASE)
        canvas.polygon([(sx - 29, HEAD_CY), (sx - 32, HEAD_CY + 48), (sx - 27, HEAD_CY + 53),
                        (sx - 24, HEAD_CY + 4)], shade(HAIR_BASE, 0.85))
        canvas.erase_full(side_face_hole(canvas, HEAD_CY - 12))
        # The lock in front of the ear.
        canvas.polygon([(CX + 2, HEAD_CY - 24), (CX + 17, HEAD_CY - 13), (CX + 9, HEAD_CY - 4),
                        (CX + 5, HEAD_CY + 14), (CX - 6, HEAD_CY + 10)], HAIR_BASE)
        hair_strands(canvas, rng, 90, sx - 32, CX + 14, HEAD_CY - 32, HEAD_CY + 40, 12)
    elif style == 3:  # Curly volume.
        for i in range(18):
            angle = math.pi * (0.62 + 1.3 * i / 17.0)
            canvas.ellipse(sx - 1 + math.cos(angle) * 30, HEAD_CY - 6 + math.sin(angle) * 30, 11,
                           11, base, light=0.35)
        canvas.ellipse(sx - 2, HEAD_CY - 8, 30, 28, base, light=0.3)
        canvas.erase_full(side_face_hole(canvas, HEAD_CY - 12))
        clip = canvas.coverage()
        for _ in range(70):  # Curls.
            x = rng.uniform(sx - 42, sx + 40)
            y = rng.uniform(HEAD_CY - 46, HEAD_CY + 26)
            r = rng.uniform(2.5, 4.5)
            canvas.ellipse(x, y, r, r, HAIR_DARK, clip=clip, opacity=0.55)
            canvas.ellipse(x - 0.8, y - 0.8, r * 0.7, r * 0.7, HAIR_LIGHT, clip=clip, opacity=0.6)
    elif style == 4:  # Bun, on top toward the back.
        bun = (sx - 10, HEAD_CY - 32)
        canvas.ellipse(bun[0], bun[1], 13, 11, base, light=0.4)
        canvas.ellipse(sx, HEAD_CY - 6, HEAD_RX + 2, HEAD_RY - 1, base, light=0.3)
        canvas.erase_full(side_face_hole(canvas, HEAD_CY - 18, ear=True))
        canvas.line([(bun[0] - 10, bun[1] + 7), (bun[0] + 9, bun[1] + 5)], 3, HAIR_DARK)
        clip = canvas.coverage()
        for i in range(11):  # Combed back: strands converging to the bun.
            x = sx - 26 + i * 5.0
            canvas.line([(x, HEAD_CY - 8 + abs(i - 5) * 1.5),
                         (bun[0] + (x - bun[0]) * 0.3, bun[1] + 7)], 1.3, HAIR_DARK, clip=clip,
                        opacity=0.6)
    elif style == 5:  # Two braids: the near one hangs over the ear.
        canvas.ellipse(sx, HEAD_CY - 5, HEAD_RX + 2.5, HEAD_RY - 1, base, light=0.3)
        canvas.erase_full(side_face_hole(canvas, HEAD_CY - 13))
        braid(canvas, SIDE_EAR_X - 2, HEAD_CY + 4, 1)
        hair_strands(canvas, rng, 30, sx - 28, sx + 26, HEAD_CY - 32, HEAD_CY - 6, 7)
    canvas.outline(1.8, HAIR_OUTLINE)
    return canvas


def draw_hair_back(style):
    canvas = Canvas(CHAR_W, CHAR_H)
    rng = random.Random(4200 + style)
    base = rgb(HAIR_BASE)
    if style == 1:  # Short crop, down to the nape.
        canvas.ellipse(CX, HEAD_CY - 6, HEAD_RX + 2.5, HEAD_RY - 1, base, light=0.3)
        for i in range(7):  # Tufts on the nape.
            x = CX - 18 + i * 6
            canvas.polygon([(x - 4, HEAD_CY + 16), (x + 4, HEAD_CY + 16), (x + 1, HEAD_CY + 25)],
                           HAIR_BASE)
        hair_strands(canvas, rng, 70, CX - 26, CX + 26, HEAD_CY - 32, HEAD_CY + 14, 7)
    elif style == 2:  # Long, covering the back down to the shoulder blades.
        canvas.ellipse(CX, HEAD_CY - 5, HEAD_RX + 3.5, HEAD_RY, base, light=0.3)
        canvas.polygon([(CX - 31, HEAD_CY - 4), (CX - 33, HEAD_CY + 48), (CX - 25, HEAD_CY + 54),
                        (CX, HEAD_CY + 58), (CX + 25, HEAD_CY + 54), (CX + 33, HEAD_CY + 48),
                        (CX + 31, HEAD_CY - 4)], HAIR_BASE)
        for side in (-1, 1):
            canvas.polygon([(CX + side * 26, HEAD_CY), (CX + side * 31, HEAD_CY - 4),
                            (CX + side * 33, HEAD_CY + 48), (CX + side * 28, HEAD_CY + 52)],
                           shade(HAIR_BASE, 0.85))
        canvas.line([(CX, HEAD_CY - 33), (CX + 1, HEAD_CY - 12)], 1.6, HAIR_DARK)
        hair_strands(canvas, rng, 140, CX - 32, CX + 32, HEAD_CY - 32, HEAD_CY + 44, 12)
    elif style == 3:  # Curly volume.
        for i in range(24):
            angle = math.pi * 2.0 * i / 24.0
            canvas.ellipse(CX + math.cos(angle) * 30, HEAD_CY - 6 + math.sin(angle) * 30, 11, 11,
                           base, light=0.35)
        canvas.ellipse(CX, HEAD_CY - 6, 32, 32, base, light=0.3)
        clip = canvas.coverage()
        for _ in range(110):  # Curls.
            x = rng.uniform(CX - 40, CX + 40)
            y = rng.uniform(HEAD_CY - 46, HEAD_CY + 34)
            r = rng.uniform(2.5, 4.5)
            canvas.ellipse(x, y, r, r, HAIR_DARK, clip=clip, opacity=0.55)
            canvas.ellipse(x - 0.8, y - 0.8, r * 0.7, r * 0.7, HAIR_LIGHT, clip=clip, opacity=0.6)
    elif style == 4:  # Bun on top.
        canvas.ellipse(CX, HEAD_CY - 6, HEAD_RX + 2, HEAD_RY - 1, base, light=0.3)
        clip = canvas.coverage()
        for i in range(13):  # Combed up: strands converging to the bun.
            x = CX - 26 + i * 4.3
            canvas.line([(x, HEAD_CY + 16 - abs(i - 6) * 2.0), (CX + (x - CX) * 0.25, HEAD_CY - 26)],
                        1.3, HAIR_DARK, clip=clip, opacity=0.6)
        canvas.ellipse(CX, HEAD_CY - 31, 13, 11, base, light=0.4)
        canvas.line([(CX - 10, HEAD_CY - 23), (CX + 10, HEAD_CY - 23)], 3, HAIR_DARK)
    elif style == 5:  # Two braids down the back.
        canvas.ellipse(CX, HEAD_CY - 5, HEAD_RX + 2.5, HEAD_RY - 1, base, light=0.3)
        canvas.line([(CX, HEAD_CY - 33), (CX, HEAD_CY + 20)], 1.6, HAIR_DARK)
        hair_strands(canvas, rng, 50, CX - 26, CX + 26, HEAD_CY - 32, HEAD_CY + 10, 7)
        for side in (-1, 1):
            braid(canvas, CX + side * 20, HEAD_CY + 12, side)
    canvas.outline(1.8, HAIR_OUTLINE)
    return canvas


def draw_hair(style, view="front"):
    if view == "side":
        return draw_hair_side(style)
    if view == "back":
        return draw_hair_back(style)
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


def draw_beard_side(style):
    canvas = Canvas(CHAR_W, CHAR_H)
    rng = random.Random(5100 + style)
    mouth_y = HEAD_CY + 20
    base = rgb(HAIR_BASE)
    mouth = Canvas(CHAR_W, CHAR_H)
    mouth.ellipse(CX + 17.5, mouth_y + 1, 7.5, 4.6, (1.0, 1.0, 1.0))
    if style == 1:  # Stubble: sparse dots on the jaw.
        jaw = Canvas(CHAR_W, CHAR_H)
        jaw.ellipse(CX + 4, HEAD_CY + 14, HEAD_RX * 0.8, HEAD_RY * 0.58, (1.0, 1.0, 1.0))
        gx, gy = canvas.full_grids()
        clip = jaw.alpha * np.clip((gy - (HEAD_CY + 9)) * SUPERSAMPLING, 0, 1)
        clip *= np.clip((gx - (SIDE_EAR_X + 5)) * SUPERSAMPLING, 0, 1) * (1.0 - mouth.alpha)
        for _ in range(170):
            x = rng.uniform(CX - 4, CX + 26)
            y = rng.uniform(HEAD_CY + 8, HEAD_CY + 32)
            canvas.ellipse(x, y, 0.8, 0.8, (0.5, 0.5, 0.5), clip=clip, opacity=0.5)
        return canvas
    if style in (2, 3):  # Short or full beard, from the ear around the chin.
        drop = 10 if style == 2 else 26
        canvas.polygon([(SIDE_EAR_X + 4, HEAD_CY + 3), (SIDE_EAR_X + 4, HEAD_CY + 18),
                        (CX + 8, HEAD_CY + 28 + drop * 0.7), (CX + 17, HEAD_CY + 30 + drop),
                        (CX + 24, HEAD_CY + 27 + drop * 0.6), (CX + 23, HEAD_CY + 23),
                        (CX + 10, HEAD_CY + 15), (SIDE_EAR_X + 9, HEAD_CY + 3)], HAIR_BASE)
        canvas.ellipse(CX + 14, HEAD_CY + 23 + drop * 0.5, 11, 9 + drop * 0.45, base, light=0.4)
        canvas.erase_full(mouth.alpha)
        hair_strands(canvas, rng, 45 if style == 3 else 25, CX - 2, CX + 24, HEAD_CY + 8,
                     HEAD_CY + 26 + drop, 6)
    if style in (3, 4):  # Mustache.
        canvas.polygon([(CX + 23, mouth_y - 5.5), (CX + 14, mouth_y - 5), (CX + 9, mouth_y + 1),
                        (CX + 15, mouth_y - 1.5), (CX + 23.5, mouth_y - 1.5)], HAIR_BASE)
    canvas.outline(1.5, HAIR_OUTLINE)
    return canvas


def draw_beard(style, view="front"):
    if view == "side":
        return draw_beard_side(style)
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


def draw_foliage_of_density(seed, clumps, gap, shade, twigs=0, leaves=True):
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
    for layer in range(3 if leaves else 0):  # None in winter: bare branches only.
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


def draw_foliage_bare():
    """The dense crown in winter: a net of bare branches, the ground showing
    through (drawn under the leaves, which fade in and out with the seasons)."""
    return draw_foliage_of_density(7103, 0, (44, 36, 28), 1.0, twigs=26, leaves=False)


def draw_foliage_medium_bare():
    return draw_foliage_of_density(7104, 0, None, 1.0, twigs=12, leaves=False)


def draw_foliage_sparse_bare():
    return draw_foliage_of_density(7105, 0, None, 1.0, twigs=6, leaves=False)


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
    "ground_foliage_bare": draw_foliage_bare,
    "ground_foliage_medium_bare": draw_foliage_medium_bare,
    "ground_foliage_sparse_bare": draw_foliage_sparse_bare,
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
    for name, draw in FRUIT_HERBS:
        for size in range(len(HERB_SCALES)):
            sprites[f"{name}_{size}.png"] = draw(size, False)
            sprites[f"{name}_fruit_{size}.png"] = draw(size, True)
    sprites["mushrooms_0.png"] = draw_mushrooms()
    # The front view keeps the bare name; the others end with their view.
    for view in CHAR_VIEWS:
        suffix = "" if view == "front" else f"_{view}"
        for shape, (name, _, _, _) in enumerate(BODY_SHAPES):
            for skin in range(len(SKIN_TONES)):
                sprites[f"body_{name}_{skin}{suffix}.png"] = draw_body(shape, skin, view)
        for skin in range(len(SKIN_TONES)):
            if view == "back":  # No face: one head for every expression.
                sprites[f"head_{skin}_back.png"] = draw_head(skin, None, view)
                continue
            for expression in EXPRESSIONS:
                sprites[f"head_{skin}_{expression}{suffix}.png"] = draw_head(skin, expression,
                                                                             view)
        for style, name in HAIR_STYLE_NAMES.items():
            sprites[f"hair_{name}{suffix}.png"] = draw_hair(style, view)
        if view != "back":  # A beard does not show from behind.
            for style, name in BEARD_STYLE_NAMES.items():
                sprites[f"beard_{name}{suffix}.png"] = draw_beard(style, view)
    for skin in range(len(SKIN_TONES)):
        sprites[f"fist_{skin}.png"] = draw_fist(skin)
        sprites[f"eyes_closed_{skin}.png"] = draw_closed_eyes(skin)
    sprites["sleep_z.png"] = draw_sleep_z()
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
    for name, _ in HERBS + FRUIT_HERBS:
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


def preview_views(images, path):
    """Settlers from the front, the side and behind (one row per view), then
    every animal standing: side on, from the front and from behind."""
    hair_colors = [(44, 34, 30), (92, 60, 38), (140, 96, 58), (164, 78, 44), (220, 184, 112),
                   (176, 172, 166)]
    people = [  # Body, skin, expression, hair style, beard style.
        (0, 0, "neutral", 1, 0), (1, 1, "smiling", 2, 0), (2, 2, "sad", 3, 3),
        (1, 3, "angry", 4, 2), (0, 4, "neutral", 5, 0), (2, 1, "smiling", 0, 4),
        (1, 2, "neutral", 1, 1), (2, 0, "angry", 0, 0)]
    step_x, step_y = CHAR_W - 20, CHAR_H - 30
    sheet = Image.new("RGBA", (1560, 3 * step_y + 440), (96, 128, 64, 255))
    for column, (body, skin, expression, hair, beard) in enumerate(people):
        for row, view in enumerate(CHAR_VIEWS):
            suffix = "" if view == "front" else f"_{view}"
            head = f"head_{skin}_back" if view == "back" else f"head_{skin}_{expression}{suffix}"
            layers = [images[f"body_{BODY_SHAPES[body][0]}_{skin}{suffix}.png"],
                      images[f"{head}.png"]]
            if beard and view != "back":
                layers.append(tint(images[f"beard_{BEARD_STYLE_NAMES[beard]}{suffix}.png"],
                                   hair_colors[column % 6]))
            if hair:
                layers.append(tint(images[f"hair_{HAIR_STYLE_NAMES[hair]}{suffix}.png"],
                                   hair_colors[(column * 5) % 6]))
            for layer in layers:
                sheet.alpha_composite(layer, (10 + column * step_x, 10 + row * step_y))
    left, top = 10, 3 * step_y + 30
    for species in ANIMAL_SPECIES:
        right = left
        for row, sex in enumerate(ANIMAL_SEXES):
            x = left
            for pose in ("stand", "front", "back"):
                image = images[f"animal_{species}_{sex}_{pose}.png"]
                sheet.alpha_composite(image, (x, top + row * 200 + 192 - image.height))
                x += image.width - 10
            right = max(right, x)
        left = right + 20
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
        preview_views(images, f"{stem}_views{extension}")


if __name__ == "__main__":
    main()
