#!/usr/bin/env python3
"""
Attack animation preview: an animated GIF of blows as the game plays them
(docs/COMBAT.md §5), one scene per attack, drawn with the game's own sprites.
The attacker draws back, then lunges; when the blow lands, the one struck
flinches, flashes and shows the attack's mark for a moment.

It mirrors the game: the timeline is core/strike_anim.c's (in ticks, 60 a
second), the marks are src/game/strike_marks.c's shapes (same sizes, in
tiles). Change one, change the other. To try a new attack or mark, add a
scene to SCENES (and a mark function to MARKS when it needs a new shape).

Captions are read from the game's translation files (--lang).

Usage: python3 tools/preview_attacks.py [--lang fr] [--output attacks.gif]
           [--sheet sheet.png]
Requires Pillow.
"""

import argparse
import math
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPRITES = os.path.join(ROOT, "assets", "sprites")
LANG = os.path.join(ROOT, "assets", "lang")

SS = 2                      # supersampling
TILE = 96 * SS              # pixels per tile in the preview
W, H = 460 * SS, 230 * SS
BASE_Y = 190 * SS           # the ground line (feet)
FRAME_MS = 40
TICKS_PER_SECOND = 60
FONT_DIR = "/usr/share/fonts/truetype/dejavu"
HAIR_BROWN = (100, 66, 44)  # HAIR_COLOR_DARK_BROWN in src/game/render.c

# ---- Timeline: core/strike_anim.h -----------------------------------------------
WIND_TICKS = 18
LUNGE_TICKS = 7
HIT_TICKS = WIND_TICKS + LUNGE_TICKS
RETURN_TICKS = 30
MARK_DRAW_TICKS = 7
MARK_HOLD_TICKS = 24
MARK_FADE_TICKS = 22
ANIM_TICKS = HIT_TICKS + MARK_HOLD_TICKS + MARK_FADE_TICKS
BACK, FORWARD = 0.20, 0.32
LEAN_BACK, LEAN_FORWARD = 5.0, 6.0
RECOIL, RECOIL_TICKS = 0.07, 14
FLASH, FLASH_TICKS = 0.4, 5
# A still moment before and after each scene.
IDLE_TICKS = 20
TAIL_TICKS = 16


def clamp(t, lo=0.0, hi=1.0):
    return max(lo, min(hi, t))


def ease_in_out(t):
    return 0.5 - 0.5 * math.cos(math.pi * clamp(t))


def ease_in(t):
    t = clamp(t)
    return t * t


def ease_out(t):
    t = 1.0 - clamp(t)
    return 1.0 - t * t * t


def attacker_pose(ticks):
    """(advance in tiles toward the target, lean in degrees: head toward it)."""
    if ticks < 0 or ticks >= HIT_TICKS + RETURN_TICKS:
        return 0.0, 0.0
    if ticks < WIND_TICKS:
        t = ease_in_out(ticks / WIND_TICKS)
        return -BACK * t, -LEAN_BACK * t
    if ticks < HIT_TICKS:
        t = ease_in((ticks - WIND_TICKS + 1) / LUNGE_TICKS)
        return -BACK + (BACK + FORWARD) * t, -LEAN_BACK + (LEAN_BACK + LEAN_FORWARD) * t
    left = 1.0 - ease_out((ticks - HIT_TICKS) / RETURN_TICKS)
    return FORWARD * left, LEAN_FORWARD * left


def recoil(ticks):
    since = ticks - HIT_TICKS
    if since < 0 or since >= RECOIL_TICKS:
        return 0.0
    return RECOIL * (1.0 - ease_out(since / RECOIL_TICKS))


def flash_amount(ticks):
    since = ticks - HIT_TICKS
    return FLASH if 0 <= since < FLASH_TICKS else 0.0


def mark_phase(ticks):
    """(drawn, opacity) of the mark, or None."""
    since = ticks - HIT_TICKS
    if since < 0 or ticks >= ANIM_TICKS:
        return None
    drawn = clamp((since + 1) / MARK_DRAW_TICKS)
    fading = since - MARK_HOLD_TICKS
    return drawn, 1.0 if fading < 0 else 1.0 - fading / MARK_FADE_TICKS


# ---- Marks: src/game/strike_marks.c ----------------------------------------------
WHITE = (255, 255, 255)
SHADOW = (41, 31, 31)
SHADOW_ALPHA = 0.55
BLOOD = (179, 23, 23)


def ink(color, opacity, alpha=1.0):
    return color + (int(255 * opacity * alpha),)


def stroke(d, a, b, width, share, fill, steps=12):
    """Tapered at both ends, grown from a to `share` of its length."""
    if share <= 0:
        return
    (ax, ay), (bx, by) = a, b
    dx, dy = bx - ax, by - ay
    length = math.hypot(dx, dy) or 1.0
    nx, ny = -dy / length, dx / length
    left, right = [], []
    for i in range(steps + 1):
        u = share * i / steps
        half = 0.5 * width * math.sin(math.pi * u) ** 0.8
        x, y = ax + dx * u, ay + dy * u
        left.append((x + nx * half, y + ny * half))
        right.append((x - nx * half, y - ny * half))
    d.polygon(left + right[::-1], fill=fill)


def inked_stroke(d, a, b, width, share, opacity):
    stroke(d, a, b, width * 1.5, share, ink(SHADOW, opacity, SHADOW_ALPHA))
    stroke(d, a, b, width, share, ink(WHITE, opacity))


def disc(d, x, y, r, fill):
    d.ellipse((x - r, y - r, x + r, y + r), fill=fill)


def claws(d, cx, cy, s, side, drawn, opacity):
    for i in range(3):
        off = (i - 1) * 0.1 * s
        a = (cx + side * 0.10 * s + off, cy - 0.24 * s + off * 0.25)
        b = (cx - side * 0.12 * s + off, cy + 0.20 * s + off * 0.25)
        inked_stroke(d, a, b, 0.05 * s, clamp(drawn * 1.35 - i * 0.12), opacity)


def jaw(d, cx, cy, rx, ry, upper, s, opacity):
    sign = -1 if upper else 1
    thick = 0.045 * s
    steps = 16

    def arc(shift):
        return [(cx - rx * math.cos(math.pi * i / steps),
                 cy + sign * ry * math.sin(math.pi * i / steps) + shift) for i in range(steps + 1)]

    d.polygon(arc(sign * thick * 0.35) + arc(-sign * thick * 1.35)[::-1],
              fill=ink(SHADOW, opacity, SHADOW_ALPHA))
    for grow, fill in ((1.4, ink(SHADOW, opacity, SHADOW_ALPHA)), (1.0, ink(WHITE, opacity))):
        for i in range(6):
            t = math.pi * (i + 0.5) / 6
            x, y = cx - rx * 0.92 * math.cos(t), cy + sign * ry * 0.92 * math.sin(t)
            size = 0.055 * s * (0.7 + 0.3 * math.sin(t)) * grow
            d.polygon([(x - size * 0.5, y), (x + size * 0.5, y), (x, y - sign * size * 1.5)],
                      fill=fill)
    d.polygon(arc(0) + arc(-sign * thick)[::-1], fill=ink(WHITE, opacity))


def bite(d, cx, cy, s, side, drawn, opacity):
    rx, ry = 0.21 * s, 0.13 * s
    gap = (1 - drawn * drawn) * 0.18 * s + 0.02 * s
    jaw(d, cx, cy - gap, rx, ry, True, s, opacity)
    jaw(d, cx, cy + gap, rx, ry, False, s, opacity)
    if drawn >= 1:
        for i in range(5):
            disc(d, cx - rx * 0.75 + i * rx * 0.375, cy, 0.018 * s, ink(BLOOD, opacity))


def cut(d, cx, cy, s, side, drawn, opacity):
    a = (cx - side * 0.30 * s, cy + 0.07 * s)
    b = (cx + side * 0.30 * s, cy - 0.07 * s)
    share = min(1.0, drawn * 1.5)
    stroke(d, a, b, 0.085 * s, share, ink(SHADOW, opacity, SHADOW_ALPHA))
    stroke(d, a, b, 0.06 * s, share, ink(WHITE, opacity))
    stroke(d, a, b, 0.02 * s, share, ink(BLOOD, opacity))
    if drawn >= 1:
        for ox, oy, r in ((0.36, -0.12, 0.022), (0.43, -0.03, 0.016), (0.33, 0.04, 0.013)):
            disc(d, cx + side * ox * s, cy + oy * s + (1 - opacity) * 0.1 * s, r * s,
                 ink(BLOOD, opacity))


def burst(d, cx, cy, s, side, drawn, opacity):
    inner, outer = (0.07 + 0.08 * drawn) * s, (0.13 + 0.13 * drawn) * s
    for i in range(8):
        angle = (i + 0.25) * 2 * math.pi / 8
        reach = 1.0 if i % 2 == 0 else 0.7
        c, sn = math.cos(angle), math.sin(angle)
        end = inner + (outer - inner) * reach
        inked_stroke(d, (cx + c * inner, cy + sn * inner), (cx + c * end, cy + sn * end),
                     0.045 * s, 1.0, opacity)


MARKS = {"claws": claws, "bite": bite, "cut": cut, "blow": burst}

# ---- Sprites ----------------------------------------------------------------------
_cache = {}


def sprite(name):
    if name not in _cache:
        img = Image.open(os.path.join(SPRITES, name + ".png")).convert("RGBA")
        scale = TILE / 128.0
        _cache[name] = img.resize((round(img.width * scale), round(img.height * scale)),
                                  Image.LANCZOS)
    return _cache[name]


def tinted(img, color):
    r, g, b = color
    channels = img.split()
    return Image.merge("RGBA", (channels[0].point(lambda v: v * r // 255),
                                channels[1].point(lambda v: v * g // 255),
                                channels[2].point(lambda v: v * b // 255), channels[3]))


def settler():
    body = sprite("body_average_1_side").copy()
    body.alpha_composite(sprite("head_1_neutral_side"))
    body.alpha_composite(tinted(sprite("hair_short_side"), HAIR_BROWN))
    return body


def flashed(img, amount):
    """Additive, like the game: the sprite's colors added over itself."""
    if amount <= 0:
        return img
    rgb = img.convert("RGB")
    brighter = Image.eval(rgb, lambda v: min(255, int(v * (1 + amount))))
    out = brighter.convert("RGBA")
    out.putalpha(img.getchannel("A"))
    return out


def ground():
    soil, grass = sprite("ground_soil"), sprite("grass_1")
    bg = Image.new("RGBA", (W, H))
    for y in range(0, H, soil.height):
        for x in range(0, W, soil.width):
            bg.alpha_composite(soil, (x, y))
            bg.alpha_composite(grass, (x, y))
    bg.alpha_composite(Image.new("RGBA", (W, H), (20, 25, 15, 70)))
    return bg


def spear_layer(img_w, img_h, left, top, thrust):
    """A thrusting spear held at chest height, pointing right (no sprite yet)."""
    layer = Image.new("RGBA", (W, H))
    d = ImageDraw.Draw(layer)
    hand_x, hand_y = left + img_w * 0.62, top + img_h * 0.70
    back, front = hand_x - 0.25 * TILE + thrust, hand_x + 0.95 * TILE + thrust
    tilt = -0.04 * TILE
    shaft = [(back, hand_y + 0.03 * TILE), (front, hand_y + tilt)]
    d.line(shaft, fill=(60, 40, 25, 255), width=int(0.06 * TILE))
    d.line(shaft, fill=(150, 108, 64, 255), width=int(0.035 * TILE))
    tip_w = 0.07 * TILE
    d.polygon([(front - 0.02 * TILE, hand_y + tilt - tip_w),
               (front + 0.2 * TILE, hand_y + tilt - 0.01 * TILE),
               (front - 0.02 * TILE, hand_y + tilt + tip_w)],
              fill=(120, 120, 128, 255), outline=(50, 50, 55, 255))
    return layer


def place(frame, img, x_center, lean=0.0, light=0.0, flip=False):
    """Draws `img` standing on the ground line at x_center, leaning `lean`
    degrees clockwise around its feet; returns its top-left corner."""
    if flip:
        img = img.transpose(Image.FLIP_LEFT_RIGHT)
    img = flashed(img, light)
    if lean:
        pad = Image.new("RGBA", (img.width, img.height * 2))
        pad.alpha_composite(img, (0, 0))
        img = pad.rotate(-lean, resample=Image.BICUBIC, center=(img.width / 2, img.height))
        top = BASE_Y - img.height // 2
    else:
        top = BASE_Y - img.height
    left = int(x_center - img.width / 2)
    frame.alpha_composite(img, (left, int(top)))
    return left, top


# ---- Scenes -----------------------------------------------------------------------
class Scene:
    def __init__(self, attack_key, attacker, target, mark, gap, mark_height, spear=False):
        self.attack_key = attack_key      # translation key of the attack's name
        self.attacker = attacker          # (sprite function, translation key)
        self.target = target
        self.mark = mark                  # a MARKS key (StrikeMark)
        self.gap = gap                    # tiles between them, beyond their sizes
        self.mark_height = mark_height    # share of the target sprite's height
        self.spear = spear


def wolf():
    return sprite("animal_wolf_male_stand")


def roe_deer():
    return sprite("animal_roe_deer_female_stand")


SCENES = [
    Scene("attack.claws", (wolf, "animal.wolf"), (roe_deer, "animal.roe_deer"), "claws", 0.5,
          0.55),
    Scene("attack.bite", (wolf, "animal.wolf"), (settler, "work_panel.colonist"), "bite", 0.5,
          0.30),
    Scene("attack.spear_thrust", (settler, "work_panel.colonist"), (wolf, "animal.wolf"), "cut",
          1.3, 0.50, spear=True),
    Scene("attack.hoof_strike", (roe_deer, "animal.roe_deer"), (wolf, "animal.wolf"), "blow",
          0.5, 0.50),
]


def load_lang(code):
    strings = {}
    with open(os.path.join(LANG, code + ".lang"), encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                strings[key.strip()] = value.strip()
    return strings


def render_scene(scene, strings, fonts):
    attacker, target = scene.attacker[0](), scene.target[0]()
    title = strings.get(scene.attack_key, scene.attack_key)
    subtitle = "{} → {}".format(strings.get(scene.attacker[1], ""),
                                strings.get(scene.target[1], ""))
    bg = ground()
    sep = scene.gap * TILE + (attacker.width + target.width) / 2 * 0.8
    ax0, tx0 = W / 2 - sep / 2, W / 2 + sep / 2
    frames = []
    total_ms = (IDLE_TICKS + ANIM_TICKS + TAIL_TICKS) * 1000 // TICKS_PER_SECOND
    for ms in range(0, total_ms, FRAME_MS):
        ticks = ms * TICKS_PER_SECOND // 1000 - IDLE_TICKS
        frame = bg.copy()
        advance, lean = attacker_pose(ticks)
        push = recoil(ticks)
        place(frame, target, tx0 + push * TILE, light=flash_amount(ticks), flip=True)
        left, _ = place(frame, attacker, ax0 + advance * TILE,
                        lean=lean * (0.4 if scene.spear else 1.0))
        if scene.spear:
            thrust = 0.0
            if WIND_TICKS <= ticks < HIT_TICKS + 8:
                thrust = 0.12 * TILE * ease_in((ticks - WIND_TICKS + 1) / LUNGE_TICKS)
            frame.alpha_composite(spear_layer(attacker.width, attacker.height, left,
                                              BASE_Y - attacker.height, thrust))
        phase = mark_phase(ticks)
        if phase:
            layer = Image.new("RGBA", (W, H))
            MARKS[scene.mark](ImageDraw.Draw(layer), tx0 + push * TILE,
                              BASE_Y - target.height * scene.mark_height, TILE, 1.0, *phase)
            frame.alpha_composite(layer)
        d = ImageDraw.Draw(frame)
        d.rectangle((0, 0, W, 34 * SS), fill=(18, 22, 30, 220))
        d.text((12 * SS, 7 * SS), title, font=fonts[0], fill=(240, 236, 226))
        d.text((24 * SS + d.textlength(title, font=fonts[0]), 10 * SS), subtitle, font=fonts[1],
               fill=(170, 176, 186))
        frames.append((ticks, frame.resize((W // SS, H // SS), Image.LANCZOS).convert("RGB")))
    return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--lang", default="en")
    parser.add_argument("--output", default="attacks.gif")
    parser.add_argument("--sheet", help="also write a few frames of each scene side by side")
    args = parser.parse_args()
    strings = load_lang(args.lang)
    fonts = (ImageFont.truetype(os.path.join(FONT_DIR, "DejaVuSans-Bold.ttf"), 17 * SS),
             ImageFont.truetype(os.path.join(FONT_DIR, "DejaVuSans.ttf"), 13 * SS))
    scenes = [render_scene(scene, strings, fonts) for scene in SCENES]
    frames = [frame for scene in scenes for _, frame in scene]
    # One palette for every frame, from moments where everything shows.
    fw, fh = frames[0].size
    montage = Image.new("RGB", (fw * len(scenes), fh * 2))
    for i, scene in enumerate(scenes):
        for row, moment in enumerate((HIT_TICKS - 2, HIT_TICKS + 2)):
            frame = min(scene, key=lambda item: abs(item[0] - moment))[1]
            montage.paste(frame, (i * fw, row * fh))
    palette = montage.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    gif = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames]
    gif[0].save(args.output, save_all=True, append_images=gif[1:], duration=FRAME_MS, loop=0,
                optimize=True)
    if args.sheet:
        picks = (WIND_TICKS - 1, HIT_TICKS - 1, HIT_TICKS + 2, HIT_TICKS + 12)
        sheet = Image.new("RGB", (fw * len(picks), fh * len(scenes)))
        for row, scene in enumerate(scenes):
            for col, pick in enumerate(picks):
                frame = min(scene, key=lambda item: abs(item[0] - pick))[1]
                sheet.paste(frame, (col * fw, row * fh))
        sheet.save(args.sheet)


if __name__ == "__main__":
    main()
