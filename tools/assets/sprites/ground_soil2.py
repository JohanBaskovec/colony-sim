"""Bare soil ground tile, seamless on all four sides (painted look).

Usage: python3 ground_soil2.py [output_dir] [seed] [size]
  output_dir: default "out"
  seed      : default 7 (another seed gives another tile of the same soil)
  size      : default 192 (side of the tile in pixels; the patterns follow the scale)

Needs numpy and Pillow.
Writes ground_soil.png and ground_soil_tiled.png (3x3 repeat, to check the seams by eye).

Everything is periodic: the noise is filtered in the Fourier domain, and every clod,
pebble or twig that leaves through one edge comes back through the opposite one.
"""
import os
import sys

import numpy as np
from PIL import Image

# ----------------------------------------------------------------------------- knobs
# Palette, from the darkest to the lightest (R, G, B).
SHADE = [
    (58, 41, 28),
    (86, 62, 42),
    (112, 83, 56),
    (136, 104, 72),
    (160, 128, 92),
]
BLOTCH = 0.16      # large light/dark blotches (higher = more visible repetition)
MARBLE = 0.30      # medium-sized marbling
GRAIN = 0.10       # fine grain
RELIEF = 0.55      # strength of the lighting on the clods
UNDULATION = 0.35  # gentle bumps of the ground between the clods
BANDS = 7          # number of flat color bands (painted look); 0 = smooth gradient
BAND_MIX = 0.55    # 0 = smooth, 1 = crisp bands
TINT = 0.012       # hue variation (redder / greyer areas)

CLODS = 110        # small clods of earth
CLOD_R = (2.0, 6.0)
CRUMBS = 220       # crumbs (1 to 2 px)
PEBBLES = 5        # pebbles; 0 for none
PEBBLE_R = (1.8, 3.2)
TWIGS = 5          # twigs; 0 for none
TWIG_LEN = (7, 15)

LIGHT = (-0.7, -0.7)  # direction the light comes from (x, y): the top left
# -----------------------------------------------------------------------------


def periodic_noise(rng, n, wavelength, sharp=1.0):
    """Periodic n x n noise whose patterns are about `wavelength` px across."""
    f = np.fft.fftfreq(n)
    fx, fy = np.meshgrid(f, f)
    r = np.hypot(fx, fy)
    f0 = 1.0 / wavelength
    band = np.exp(-(((r - f0) / (f0 * 0.7 * sharp)) ** 2))
    band[0, 0] = 0.0
    spec = np.fft.fft2(rng.standard_normal((n, n))) * band
    out = np.fft.ifft2(spec).real
    return out / (out.std() + 1e-9)


def wrap_delta(coord, centre, n):
    """coord - centre wrapped into [-n/2, n/2[ (distance on the torus)."""
    return (coord - centre + n / 2.0) % n - n / 2.0


def smooth(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def ellipse_field(xx, yy, n, cx, cy, rx, ry, ang):
    """Normalised distance to the centre of a rotated ellipse (1.0 = its edge)."""
    dx, dy = wrap_delta(xx, cx, n), wrap_delta(yy, cy, n)
    c, s = np.cos(ang), np.sin(ang)
    u, v = dx * c + dy * s, -dx * s + dy * c
    return np.sqrt((u / rx) ** 2 + (v / ry) ** 2)


def ramp(t):
    """Value 0..1 -> color interpolated in SHADE."""
    pal = np.array(SHADE, dtype=float)
    pos = np.clip(t, 0.0, 1.0) * (len(pal) - 1)
    i = np.clip(np.floor(pos).astype(int), 0, len(pal) - 2)
    frac = (pos - i)[..., None]
    return pal[i] * (1.0 - frac) + pal[i + 1] * frac


def make_tile(n=192, seed=7):
    rng = np.random.default_rng(seed)
    k = n / 192.0  # scale of the patterns
    yy, xx = np.mgrid[0:n, 0:n].astype(float)

    # --- base tone: large blotches + marbling + grain
    tone = (
        BLOTCH * periodic_noise(rng, n, 96 * k)
        + MARBLE * 0.6 * periodic_noise(rng, n, 30 * k)
        + MARBLE * 0.4 * periodic_noise(rng, n, 12 * k)
        + GRAIN * periodic_noise(rng, n, 3.2 * k, sharp=1.6)
    )

    # --- relief: gentle undulation + clods + crumbs
    height = UNDULATION * (periodic_noise(rng, n, 22 * k) + 0.5 * periodic_noise(rng, n, 9 * k))
    clod_mask = np.zeros((n, n))
    for _ in range(CLODS):
        r = rng.uniform(*CLOD_R) * k
        d = ellipse_field(xx, yy, n, rng.uniform(0, n), rng.uniform(0, n),
                          r, r * rng.uniform(0.6, 1.0), rng.uniform(0, np.pi))
        dome = np.sqrt(np.clip(1.0 - d * d, 0.0, None)) * r * rng.uniform(0.35, 0.6)
        height = np.maximum(height, height * 0.5 + dome)
        clod_mask = np.maximum(clod_mask, smooth(1.0, 0.5, d) * rng.uniform(0.4, 1.0))
    for _ in range(CRUMBS):
        r = rng.uniform(0.8, 1.5) * k
        d = ellipse_field(xx, yy, n, rng.uniform(0, n), rng.uniform(0, n), r, r, 0.0)
        height += np.clip(1.0 - d * d, 0.0, None) * rng.uniform(0.4, 0.9)

    gx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5
    gy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5
    lx, ly = np.array(LIGHT) / np.hypot(*LIGHT)
    # a slope facing the light gets lighter, the opposite slope darker
    light = np.tanh(-(gx * lx + gy * ly) * 1.3)
    light = np.where(light < 0, light * 1.25, light * 0.8)

    shade = 0.47 + tone * 0.20 + clod_mask * 0.05 + light * RELIEF * 0.30
    shade = np.clip(shade, 0.0, 1.0)
    if BANDS:
        flat = (np.floor(shade * BANDS) + 0.5) / BANDS
        shade = shade * (1.0 - BAND_MIX) + flat * BAND_MIX

    rgb = ramp(shade)

    # --- slight hue variation: areas a little redder or greyer
    hue = periodic_noise(rng, n, 60 * k)[..., None] * TINT
    rgb = rgb * (1.0 + hue * np.array([1.0, 0.0, -1.2]))

    # --- pebbles
    for _ in range(PEBBLES):
        cx, cy = rng.uniform(0, n), rng.uniform(0, n)
        r = rng.uniform(*PEBBLE_R) * k
        ry, ang = r * rng.uniform(0.65, 0.9), rng.uniform(0, np.pi)
        grey = rng.uniform(0.85, 1.1)
        # cast shadow, offset away from the light
        ds = ellipse_field(xx, yy, n, cx - lx * 1.4 * k, cy - ly * 1.4 * k, r * 1.15, ry * 1.15, ang)
        rgb *= (1.0 - 0.42 * smooth(1.15, 0.5, ds))[..., None]
        d = ellipse_field(xx, yy, n, cx, cy, r, ry, ang)
        cover = smooth(1.08, 0.82, d)[..., None]
        dh = ellipse_field(xx, yy, n, cx + lx * 0.35 * r, cy + ly * 0.35 * r, r, ry, ang)
        lit = (1.0 - np.clip(dh, 0, 1.4) * 0.42)[..., None]
        stone = np.array([132.0, 118.0, 100.0]) * grey * lit
        rgb = rgb * (1.0 - cover) + stone * cover

    # --- twigs
    for _ in range(TWIGS):
        cx, cy = rng.uniform(0, n), rng.uniform(0, n)
        ang = rng.uniform(0, np.pi)
        half = rng.uniform(*TWIG_LEN) * k / 2.0
        w = rng.uniform(0.6, 0.95) * k
        bend = rng.uniform(-0.012, 0.012) / k
        c, s = np.cos(ang), np.sin(ang)

        def twig_cover(ox, oy):
            dx, dy = wrap_delta(xx, cx + ox, n), wrap_delta(yy, cy + oy, n)
            u = dx * c + dy * s
            v = -dx * s + dy * c - bend * u * u
            along = np.clip(np.abs(u) - half, 0.0, None)
            taper = w * (1.0 - 0.35 * np.clip(u / half, -1, 1))
            return smooth(0.6, -0.4, np.hypot(along, v) - taper)

        rgb *= (1.0 - 0.38 * twig_cover(-lx * 1.1 * k, -ly * 1.1 * k))[..., None]
        cover = twig_cover(0.0, 0.0)[..., None]
        top = twig_cover(lx * 0.5 * k, ly * 0.5 * k)[..., None]
        wood = np.array([70.0, 50.0, 34.0]) * (1.0 - top) + np.array([124.0, 96.0, 66.0]) * top
        rgb = rgb * (1.0 - cover) + wood * cover

    return Image.fromarray(np.clip(rgb + 0.5, 0, 255).astype(np.uint8), "RGB")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "out"
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 7
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 192
    os.makedirs(out, exist_ok=True)

    tile = make_tile(n, seed)
    tile.save(os.path.join(out, "ground_soil.png"))

    tiled = Image.new("RGB", (n * 3, n * 3))
    for i in range(3):
        for j in range(3):
            tiled.paste(tile, (i * n, j * n))
    tiled.save(os.path.join(out, "ground_soil_tiled.png"))
    print(f"ground_soil.png ({n}x{n}, seed {seed}) and ground_soil_tiled.png written to {out}/")


if __name__ == "__main__":
    main()
