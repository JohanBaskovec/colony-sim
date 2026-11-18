#!/usr/bin/env python3
"""
Blood trail preview: an animated GIF of blood on the ground as the game draws
it (core/blood.h, render_blood() in src/game/render.c), with the game's own
sprites. Those who bleed leave drops behind them as they walk, more and
larger the worse they bleed; a pool grows under one who stands still; drops
dry (darken) and fade.

It follows the game's rules and shapes (drops on steps, their sizes and
droplets, the pool, fresh and dry colors) but runs faster: a stain lasts a
few seconds here, two game hours in the game. Change one, change the other.

Usage: python3 tools/preview_blood.py [--output blood.gif] [--sheet sheet.png]
Requires Pillow.
"""
import argparse
import math
import os
import random

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPRITES = os.path.join(ROOT, "assets", "sprites")

SS = 2
TILE = 80 * SS
W, H = 620 * SS, 300 * SS
FRAME_MS = 40
FPS = 1000 // FRAME_MS
LANES = (118 * SS, 250 * SS)     # ground lines (feet) of the two walkers
FONT = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 14 * SS)
SMALL = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 12 * SS)

# The game's gait (src/game/render.c): a hop and a rock at every step.
HOP = 0.06
ROCK = 5.5

FRESH = (150, 12, 12)
DRY = (78, 22, 14)

_cache = {}


def sprite(name):
    if name not in _cache:
        img = Image.open(os.path.join(SPRITES, name + ".png")).convert("RGBA")
        s = TILE / 128.0
        _cache[name] = img.resize((round(img.width * s), round(img.height * s)), Image.LANCZOS)
    return _cache[name]


def tinted(img, color):
    r, g, b = color
    c = img.split()
    return Image.merge("RGBA", (c[0].point(lambda v: v * r // 255),
                                c[1].point(lambda v: v * g // 255),
                                c[2].point(lambda v: v * b // 255), c[3]))


def settler():
    body = sprite("body_average_2_side").copy()
    body.alpha_composite(sprite("head_2_sad_side"))
    body.alpha_composite(tinted(sprite("hair_long_side"), (172, 86, 48)))
    return body


def ground():
    soil, grass = sprite("ground_soil"), sprite("grass_1")
    bg = Image.new("RGBA", (W, H))
    for y in range(0, H, soil.height):
        for x in range(0, W, soil.width):
            bg.alpha_composite(soil, (x, y))
            bg.alpha_composite(grass, (x, y))
    bg.alpha_composite(Image.new("RGBA", (W, H), (20, 25, 15, 60)))
    return bg


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


class Drop:
    def __init__(self, x, y, size, born, rng, smear=0.0):
        self.x, self.y, self.size, self.born, self.smear = x, y, size, born, smear
        # a few droplets around the main one
        self.bits = [(rng.uniform(-1.6, 1.6), rng.uniform(-0.9, 0.9), rng.uniform(0.15, 0.35))
                     for _ in range(rng.randint(1, 3))]
        self.squash = rng.uniform(0.5, 0.7)  # seen from above at a slant

    def draw(self, d, now, life):
        age = (now - self.born) / life
        if age >= 1:
            return
        color = mix(FRESH, DRY, min(1.0, age * 1.6))
        alpha = 230 if age < 0.6 else int(230 * (1 - (age - 0.6) / 0.4))
        r = self.size
        rx, ry = r * (1 + self.smear), r * self.squash
        x, y = self.x - r * self.smear, self.y
        d.ellipse((x - rx, y - ry, x + rx, y + ry), fill=color + (alpha,))
        for bx, by, br in self.bits:
            cx, cy, rr = self.x + bx * r, self.y + by * r * self.squash, br * r
            d.ellipse((cx - rr, cy - rr * self.squash, cx + rr, cy + rr * self.squash),
                      fill=color + (alpha,))
        if age < 0.35:  # fresh blood shines
            hr = r * 0.3
            hx, hy = self.x - r * 0.25, self.y - r * 0.15
            d.ellipse((hx - hr, hy - hr * 0.6, hx + hr, hy + hr * 0.6),
                      fill=(220, 90, 80, int(alpha * (1 - age / 0.35) * 0.7)))


class Walker:
    def __init__(self, img, lane, speed, step_s, bleeding, start_x, stop_x, seed):
        self.img, self.lane, self.speed, self.step_s = img, lane, speed, step_s
        self.bleeding = bleeding   # 0..1: drops per step and their size
        self.start_x, self.stop_x = start_x, stop_x
        self.rng = random.Random(seed)
        self.drops = []
        self.last_step = -1
        self.last_pool = 0.0
        self.stopped_at = None

    def x_at(self, t):
        return min(self.stop_x, self.start_x + self.speed * TILE * t)

    def moving(self, t):
        return self.start_x + self.speed * TILE * t < self.stop_x

    def update(self, t):
        x = self.x_at(t)
        if self.moving(t):
            step = int(t / self.step_s)
            if step != self.last_step:
                self.last_step = step
                # light: a small drop now and then; heavy: one or two larger every step
                chance = 0.25 + 0.75 * self.bleeding
                count = 1 + (self.rng.random() < self.bleeding * 0.6)
                for _ in range(count):
                    if self.rng.random() < chance:
                        size = TILE * self.rng.uniform(0.025, 0.04) * (0.8 + 1.4 * self.bleeding)
                        smear = self.rng.uniform(0.3, 0.9) if self.rng.random() < self.bleeding * 0.4 else 0
                        self.drops.append(Drop(x + self.rng.uniform(-0.12, 0.12) * TILE,
                                               self.lane + self.rng.uniform(-0.10, 0.04) * TILE,
                                               size, t, self.rng, smear))
        else:
            if self.stopped_at is None:
                self.stopped_at = t
            # standing still: drops fall on the same spot and spread into a pool
            if t - self.last_pool > 0.3:
                self.last_pool = t
                self.drops.append(Drop(x + self.rng.uniform(-0.08, 0.08) * TILE,
                                       self.lane + self.rng.uniform(-0.06, 0.02) * TILE,
                                       TILE * self.rng.uniform(0.04, 0.07) * (0.8 + self.bleeding),
                                       t, self.rng))

    def draw_pool(self, d, t):
        """Blood pooling under someone who stands still and bleeds."""
        if self.stopped_at is None:
            return
        grow = min(1.0, (t - self.stopped_at) / 2.5)
        rx = TILE * (0.08 + 0.26 * grow) * (0.6 + 0.5 * self.bleeding)
        ry = rx * 0.45
        x, y = self.x_at(t) + 0.04 * TILE, self.lane - 0.02 * TILE
        d.ellipse((x - rx, y - ry, x + rx, y + ry), fill=(120, 14, 12, 225))
        d.ellipse((x - rx * 0.8, y - ry * 0.75, x + rx * 0.6, y + ry * 0.5), fill=(150, 18, 16, 235))
        hx, hy = x - rx * 0.35, y - ry * 0.3
        d.ellipse((hx - rx * 0.2, hy - ry * 0.18, hx + rx * 0.2, hy + ry * 0.18),
                  fill=(210, 90, 80, 120))

    def draw(self, frame, t):
        x = self.x_at(t)
        lift, rock = 0.0, 0.0
        if self.moving(t):
            phase = math.sin((t / (self.step_s * 2)) * 2 * math.pi)
            lift, rock = abs(phase) * HOP * TILE, phase * ROCK
        img = self.img
        if rock:
            pad = Image.new("RGBA", (img.width, img.height * 2))
            pad.alpha_composite(img, (0, 0))
            img = pad.rotate(-rock, resample=Image.BICUBIC, center=(img.width / 2, img.height))
            top = self.lane - img.height // 2 - lift
        else:
            top = self.lane - img.height
        frame.alpha_composite(img, (int(x - img.width / 2), int(top)))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--output", default="blood.gif")
    parser.add_argument("--sheet", help="also write four frames side by side")
    args = parser.parse_args()
    duration = 8.0
    life = 4.5   # how long a drop lasts in the preview (hours in the game)
    wolf = Walker(sprite("animal_wolf_female_stand"), LANES[0], 1.5, 0.12, 0.15,
                  -0.6 * TILE, W + 1.5 * TILE, 3)
    man = Walker(settler(), LANES[1], 1.0, 0.27, 0.9, -0.4 * TILE, W * 0.62, 7)
    walkers = [wolf, man]
    bg = ground()
    frames = []
    for f in range(int(duration * FPS)):
        t = f / FPS
        for w in walkers:
            w.update(t)
        frame = bg.copy()
        layer = Image.new("RGBA", (W, H))
        d = ImageDraw.Draw(layer)
        for w in walkers:
            for drop in w.drops:
                drop.draw(d, t, life)
            w.draw_pool(d, t)
        frame.alpha_composite(layer)
        for w in walkers:
            w.draw(frame, t)
        d = ImageDraw.Draw(frame)
        d.rectangle((0, 0, W, 26 * SS), fill=(18, 22, 30, 220))
        d.text((10 * SS, 5 * SS), "Blood trail", font=FONT, fill=(240, 236, 226))
        for label, y in (("Light bleeding", LANES[0] + 10 * SS),
                         ("Heavy bleeding, then standing still", LANES[1] + 12 * SS)):
            tw = d.textlength(label, font=SMALL)
            d.rectangle((8 * SS, y - 2 * SS, 16 * SS + tw, y + 16 * SS), fill=(18, 22, 30, 170))
            d.text((12 * SS, y), label, font=SMALL, fill=(220, 214, 200))
        frames.append(frame.resize((W // SS, H // SS), Image.LANCZOS).convert("RGB"))
    fw, fh = frames[0].size
    montage = Image.new("RGB", (fw * 2, fh))
    montage.paste(frames[len(frames) // 4], (0, 0))
    montage.paste(frames[-1], (fw, 0))
    palette = montage.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    gif = [fr.quantize(palette=palette, dither=Image.Dither.NONE) for fr in frames]
    gif[0].save(args.output, save_all=True, append_images=gif[1:], duration=FRAME_MS, loop=0,
                optimize=True)
    if args.sheet:
        picks = [len(frames) // 5, 2 * len(frames) // 5, 3 * len(frames) // 5, len(frames) - 1]
        sheet = Image.new("RGB", (fw * 2, fh * 2))
        for i, p in enumerate(picks):
            sheet.paste(frames[p], ((i % 2) * fw, (i // 2) * fh))
        sheet.save(args.sheet)


if __name__ == "__main__":
    main()
