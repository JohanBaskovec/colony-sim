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

# Canvas (width, height) and scale of each growth stage.
TREE_STAGES = [((128, 128), 0.24), ((128, 192), 0.42), ((192, 256), 0.6),
               ((256, 320), 0.8), ((256, 384), 1.0)]
BUSH_STAGES = [((128, 128), 0.34), ((128, 128), 0.52), ((128, 128), 0.7),
               ((160, 160), 0.86), ((192, 160), 1.0)]


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
    if stage >= 2:
        canvas.ellipse(cx, base_y - 2, trunk_w * 0.85, trunk_w * 0.28, TREE_TRUNK)
        # Bark: a darker right side and a few vertical marks.
        canvas.polygon([(cx + trunk_w * 0.1, base_y), (cx + trunk_w * 0.12, top_y),
                        (cx + trunk_w * 0.32, top_y), (cx + trunk_w * 0.5, base_y)],
                       shade(TREE_TRUNK, 0.72))
        for i in range(3):
            x = cx - trunk_w * 0.25 + i * trunk_w * 0.2
            y0 = base_y - trunk_h * rng.uniform(0.15, 0.4)
            canvas.line([(x, y0), (x + rng.uniform(-1, 1), y0 - trunk_h * 0.25)],
                        max(1.0, 2.2 * scale), shade(TREE_TRUNK, 0.6))
        # Two branches reaching into the crown.
        for side in (-1, 1):
            y = top_y + trunk_h * 0.18
            canvas.line([(cx + side * trunk_w * 0.2, y + trunk_h * 0.1),
                         (cx + side * 46 * scale, y - 30 * scale)], max(2.0, 9 * scale),
                        TREE_TRUNK)
    # Crown: overlapping clumps, broadest in the middle.
    crown_y = top_y - 20 * scale
    layout = [(0, -70, 58), (-52, -34, 54), (52, -34, 54), (0, -20, 66), (-62, 18, 46),
              (62, 18, 46), (-26, 34, 50), (30, 34, 50), (-30, -96, 40), (32, -92, 42)]
    if stage == 0:
        layout = [(0, -20, 34), (-26, 6, 26), (26, 4, 26)]
    elif stage == 1:
        layout = [(0, -40, 50), (-44, -6, 44), (44, -6, 44), (0, 12, 52), (-18, -70, 36)]
    blobs = [(cx + dx * scale, crown_y + dy * scale, r * scale) for dx, dy, r in layout]
    foliage(canvas, blobs, scale, rng, rgb(TREE_LEAVES), rgb(TREE_LEAVES_DARK),
            rgb(TREE_LEAVES_LIGHT))
    canvas.outline(max(1.2, 2.2 * min(1.0, scale + 0.3)), TREE_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.28)


BUSH_LEAVES = (78, 128, 56)
BUSH_LEAVES_DARK = (44, 88, 42)
BUSH_LEAVES_LIGHT = (134, 176, 82)
BUSH_OUTLINE = (24, 40, 22)
BERRY = (168, 36, 70)
BERRY_DARK = (96, 18, 48)


def draw_bush(stage, berries):
    (width, height), scale = BUSH_STAGES[stage]
    canvas = Canvas(width, height)
    rng = random.Random(2000 + stage)
    cx = width / 2.0
    base_y = height - 14.0
    shadow = Canvas(width, height)
    shadow.ellipse(cx, base_y + 2, 72 * scale + 4, 14 * scale + 3, (0, 0, 0))
    # A few woody stems peek out at the bottom.
    for dx in (-14, 0, 13):
        canvas.line([(cx + dx * scale, base_y + 2), (cx + dx * 1.6 * scale, base_y - 40 * scale)],
                    max(1.5, 5 * scale), (96, 70, 46))
    layout = [(0, -62, 42), (-44, -40, 38), (44, -42, 38), (-70, -14, 26), (70, -16, 26),
              (-24, -20, 40), (26, -18, 40), (0, -96, 26)]
    blobs = [(cx + dx * scale, base_y + dy * scale, r * scale) for dx, dy, r in layout]
    foliage(canvas, blobs, scale, rng, rgb(BUSH_LEAVES), rgb(BUSH_LEAVES_DARK),
            rgb(BUSH_LEAVES_LIGHT))
    if berries:
        clip = canvas.coverage()
        berry_rng = random.Random(3000 + stage)
        count = int(6 + 20 * scale)
        radius = max(2.2, 5.2 * scale)
        placed = 0
        attempts = 0
        while placed < count and attempts < 4000:
            attempts += 1
            x = berry_rng.uniform(cx - 80 * scale, cx + 80 * scale)
            y = berry_rng.uniform(base_y - 110 * scale, base_y - 4 * scale)
            ix, iy = int(x * SUPERSAMPLING), int(y * SUPERSAMPLING)
            if not (0 <= ix < canvas.big_w and 0 <= iy < canvas.big_h):
                continue
            if clip[iy, ix] < 0.99 or clip[max(0, iy - 12 * SUPERSAMPLING // 2), ix] < 0.99:
                continue
            # Small clusters of two or three berries.
            for k in range(berry_rng.choice((1, 2, 3))):
                bx = x + (k - 1) * radius * 1.3
                by = y + (k % 2) * radius * 0.9
                canvas.ellipse(bx, by + radius * 0.15, radius * 1.12, radius * 1.12,
                               rgb(BERRY_DARK))
                canvas.ellipse(bx, by, radius, radius, rgb(BERRY), light=0.6)
                canvas.ellipse(bx - radius * 0.35, by - radius * 0.4, radius * 0.32,
                               radius * 0.28, (1.0, 0.92, 0.95), opacity=0.85)
            placed += 1
    canvas.outline(max(1.2, 2.0 * min(1.0, scale + 0.3)), BUSH_OUTLINE)
    return compose(shadow, canvas, shadow_opacity=0.25)


def compose(shadow, canvas, shadow_opacity):
    """Ground shadow under the sprite."""
    result = Canvas(canvas.width, canvas.height)
    result.paint_full(shadow.alpha * shadow_opacity, np.zeros(3, dtype=np.float32))
    result.color = canvas.color + result.color * (1.0 - canvas.alpha)[..., None]
    result.alpha = canvas.alpha + result.alpha * (1.0 - canvas.alpha)
    return result


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


HAIR_STYLE_NAMES = {1: "short", 2: "long", 3: "curly", 4: "bun", 5: "braids"}
BEARD_STYLE_NAMES = {1: "stubble", 2: "short", 3: "full", 4: "mustache"}


def generate(output):
    os.makedirs(output, exist_ok=True)
    sprites = {}
    for stage in range(len(TREE_STAGES)):
        sprites[f"tree_{stage}.png"] = draw_tree(stage)
    for stage in range(len(BUSH_STAGES)):
        sprites[f"bush_{stage}.png"] = draw_bush(stage, False)
        sprites[f"bush_berries_{stage}.png"] = draw_bush(stage, True)
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
    images = {}
    for filename, canvas in sprites.items():
        image = canvas.to_image()
        assert image.width % 32 == 0 and image.height % 32 == 0, filename
        image.save(os.path.join(output, filename), optimize=True)
        images[filename] = image
    return images


def tint(image, color):
    pixels = np.asarray(image, dtype=np.float32)
    pixels[..., :3] *= np.array(color, dtype=np.float32) / 255.0
    return Image.fromarray(pixels.astype(np.uint8), "RGBA")


def preview(images, path):
    """Contact sheet on a grass-colored background, for a quick look."""
    sheet = Image.new("RGBA", (1600, 1000), (96, 128, 64, 255))
    x = 10
    for stage in range(5):
        image = images[f"tree_{stage}.png"]
        sheet.alpha_composite(image, (x, 400 - image.height))
        x += image.width + 4
    x = 10
    for berries in (False, True):
        for stage in range(5):
            name = f"bush_berries_{stage}.png" if berries else f"bush_{stage}.png"
            image = images[name]
            sheet.alpha_composite(image, (x, 580 - image.height))
            x += image.width
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
    small = sheet.resize((800, 500), Image.LANCZOS)
    sheet.alpha_composite(small.resize((400, 250), Image.LANCZOS), (1190, 800 - 250 - 10))
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


if __name__ == "__main__":
    main()
