#!/usr/bin/env python3
"""
Sprite workbench: a window to tune the sprite scripts of tools/assets/sprites/
(Python scripts that draw a sprite or a ground texture) and see the result as
the game draws it (SPRITE_PIXELS_PER_TILE pixels per tile; a sprite anchored
by the middle of its bottom edge on the middle of the bottom edge of its tile,
a ground texture repeated over the tiles; src/game/sprites.h).

  - pick a script and one of its pictures;
  - pick a ground (assets/sprites/ground_*.png, or a ground texture drawn in
    the workbench), with grass over it or not;
  - change the script's knobs and see the picture drawn again;
  - save or copy the picture (PNG, SVG; a ground texture in the game's own
    format), copy the knobs as Python, or write them back into a copy of the
    script (or the script itself).

The scripts need no change. What the workbench reads in them:

  - knobs: the top-level assignments of an UPPER_CASE name to a literal
    (number, string, tuple, list of tuples...): `DARK = (56, 86, 40)`, and
    those read from the command line with a literal default
    (`SEED = int(sys.argv[2]) if len(sys.argv) > 2 else 3`). Three whole
    numbers of 0 to 255 or a "#rrggbb" string are a color, a list of colors a
    palette, two to four other numbers a vector, a list of tuples a table. The comment
    at the end of the line is shown under the knob; the comment lines just
    above it head a section (a list of names in parentheses there names the
    columns of a table, as `("F", foot_x, angle, ...)` in a comment or
    docstring names those of the rows that start with "F").
  - pictures, in either of two ways:
      * SVG scripts: a top-level tuple or list of ("name", expression) pairs,
        assigned (`SPRITES = (("bracken_0", ...), ...)`) or looped over
        (`for name, fn in (("wild_carrot_0", stage0), ...)`). The expression
        is an SVG string or a function returning one; an SVG fragment is
        wrapped by the script's own svg() function. The script's other
        top-level statements (its loops, its `if __name__ == "__main__":`
        block) do not run. The numbers given to functions in a sprite's
        expression (a seed, the size of a shadow: `shadow(30, 6) +
        build(STAGE0, 4)`) are knobs too, named after the parameters.
      * any other script runs whole, as from the command line with no
        argument, and every picture it would write is caught instead: images
        saved with Pillow (`tile.save(...)`) and SVG files written with
        open(). Nothing reaches the disk and no program is started
        (subprocess.run() and its kin do nothing while a script runs).

An opaque picture whose sides are whole tiles is a ground texture: the preview
repeats it over the tiles, and the Ground menu offers it under the other
sprites. A script changed on disk is read again (the knobs changed in the
window are kept). Scripts run in the background, so that a slow one (a noise
texture) does not freeze the window.

Usage: python3 tools/sprite_workbench.py [script.py]
Requires Tk, Pillow, and for SVG librsvg: through PyGObject (gi, Rsvg 2.0)
and pycairo, else the rsvg-convert program. Copying a picture needs wl-copy or
xclip.
"""
import argparse
import ast
import builtins
import copy
import glob
import inspect
import io
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
import tkinter as tk
import traceback
from dataclasses import dataclass, field
from tkinter import colorchooser, filedialog, font as tkfont, messagebox, ttk

from PIL import Image, ImageDraw, ImageTk

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPRITES_DIR = os.path.join(ROOT, "assets", "sprites")
SCRIPTS_DIR = os.path.join(ROOT, "tools", "assets", "sprites")

TILE = 128  # SPRITE_PIXELS_PER_TILE (src/game/sprites.h)
TILE_PADDING = 32  # TILE_SPRITE_PADDING (src/game/sprites.c): a ground file is 192 x 192.
ZOOMS = ("25%", "50%", "100%", "150%", "200%", "300%", "400%")
SCALES = ("×1", "×2", "×4")
GAME_TILE = "game tile"
CHECKER = "Checkerboard"
NO_GRASS = "None"
LIVE = " (workbench)"
RENDER_DELAY_MS = 60
POLL_MS = 1000

# ---------------------------------------------------------------------------
# SVG to pixels

_run = subprocess.run  # The workbench's own, whatever a script does.


def _rsvg_module():
    try:
        import gi

        gi.require_version("Rsvg", "2.0")
        import cairo
        from gi.repository import Rsvg

        return Rsvg, cairo
    except (ImportError, ValueError):
        return None


RSVG = _rsvg_module()


def svg_size(svg_text):
    """The size of the picture in pixels, as written in the SVG."""
    if RSVG is not None:
        Rsvg, _ = RSVG
        handle = Rsvg.Handle.new_from_data(svg_text.encode())
        try:
            ok, width, height = handle.get_intrinsic_size_in_pixels()
            if ok:
                return max(1, round(width)), max(1, round(height))
        except AttributeError:
            dims = handle.get_dimensions()
            return dims.width, dims.height
    match_w = re.search(r'<svg[^>]*\bwidth="([\d.]+)', svg_text)
    match_h = re.search(r'<svg[^>]*\bheight="([\d.]+)', svg_text)
    if match_w and match_h:
        return round(float(match_w.group(1))), round(float(match_h.group(1)))
    return TILE, TILE


def rasterize(svg_text, width, height):
    """The SVG drawn at width x height, as an RGBA image."""
    if RSVG is not None:
        Rsvg, cairo = RSVG
        handle = Rsvg.Handle.new_from_data(svg_text.encode())
        surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, width, height)
        context = cairo.Context(surface)
        viewport = Rsvg.Rectangle()
        viewport.x, viewport.y, viewport.width, viewport.height = 0, 0, width, height
        handle.render_document(context, viewport)
        surface.flush()
        # Cairo's ARGB32 is premultiplied BGRA in memory on little-endian machines.
        return Image.frombuffer(
            "RGBA", (width, height), bytes(surface.get_data()), "raw", "BGRa", surface.get_stride(), 1
        ).copy()
    if shutil.which("rsvg-convert"):
        result = _run(
            ["rsvg-convert", "-w", str(width), "-h", str(height)],
            input=svg_text.encode(),
            capture_output=True,
            check=True,
        )
        return Image.open(io.BytesIO(result.stdout)).convert("RGBA")
    raise RuntimeError("no SVG renderer: install PyGObject with librsvg, or rsvg-convert")


# ---------------------------------------------------------------------------
# Catching what a running script writes
#
# While a script runs (in the workbench's render thread), Pillow's save, the
# script's open() for writing, os.makedirs and the subprocess functions are
# caught: pictures are kept in memory, the rest does nothing. Other threads
# (the window saving a PNG) are not affected.


class _Capture(threading.local):
    def __init__(self):
        super().__init__()
        self.pictures = None  # [(name, PIL image or SVG text)] while a script runs here.


CAPTURE = _Capture()


def _picture_name(path, kind):
    if isinstance(path, (str, os.PathLike)):
        return os.path.splitext(os.path.basename(os.fspath(path)))[0]
    return f"{kind} {len(CAPTURE.pictures) + 1}"


def _keep(name, picture):
    if all(existing != name for existing, _ in CAPTURE.pictures):
        CAPTURE.pictures.append((name, picture))


_pillow_save = Image.Image.save


def _save(self, fp, format=None, **params):
    if CAPTURE.pictures is None or not isinstance(fp, (str, os.PathLike)):
        return _pillow_save(self, fp, format, **params)
    _keep(_picture_name(fp, "image"), self.copy())
    return None


def _quiet(original, result=None):
    def call(*args, **kwargs):
        if CAPTURE.pictures is None:
            return original(*args, **kwargs)
        return result(*args) if callable(result) else result

    return call


Image.Image.save = _save
os.makedirs = _quiet(os.makedirs)
os.mkdir = _quiet(os.mkdir)
subprocess.run = _quiet(subprocess.run, lambda args=None, *rest: subprocess.CompletedProcess(args, 0, b"", b""))
subprocess.call = _quiet(subprocess.call, 0)
subprocess.check_call = _quiet(subprocess.check_call, 0)
subprocess.check_output = _quiet(subprocess.check_output, b"")


class _CaughtFile(io.StringIO):
    """A text file a script writes: an SVG is kept as a picture when closed."""

    def __init__(self, path):
        super().__init__()
        self.path = path

    def close(self):
        if not self.closed:
            text = self.getvalue()
            if CAPTURE.pictures is not None and "<svg" in text[:2000]:
                _keep(_picture_name(self.path, "svg"), text)
        super().close()


def _open(file, mode="r", *args, **kwargs):
    if CAPTURE.pictures is not None and any(flag in mode for flag in "wax+"):
        return io.BytesIO() if "b" in mode else _CaughtFile(file)
    return builtins.open(file, mode, *args, **kwargs)


SCRIPT_BUILTINS = dict(builtins.__dict__, open=_open)

# ---------------------------------------------------------------------------
# Literals


def format_literal(value, multiline=False):
    """Python source for a literal value (one item per line if multiline)."""
    if isinstance(value, bool) or value is None:
        return repr(value)
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return repr(round(value, 6))
    if isinstance(value, str):
        return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if isinstance(value, (list, tuple)):
        items = [format_literal(item) for item in value]
        opening, closing = ("[", "]") if isinstance(value, list) else ("(", ")")
        if multiline and items:
            return opening + "\n" + "".join(f"    {item},\n" for item in items) + closing
        body = ", ".join(items)
        if isinstance(value, tuple) and len(value) == 1:
            body += ","
        return opening + body + closing
    if isinstance(value, dict):
        return "{" + ", ".join(f"{format_literal(k)}: {format_literal(v)}" for k, v in value.items()) + "}"
    return repr(value)


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def number_step(value):
    if isinstance(value, int):
        return 1
    size = abs(value)
    return 0.01 if size <= 2 else 0.1 if size <= 50 else 0.5


def number_range(value):
    if isinstance(value, float) and 0 <= value <= 1:
        return 0.0, 1.0
    reach = max(abs(value) * 2, 10 if isinstance(value, int) else 1.0)
    return (-reach, reach) if value < 0 else (0, reach)


def decimals(step):
    return 0 if step >= 1 else 1 if step >= 0.1 else 2


def format_number(value, step):
    if isinstance(value, int):
        return str(value)
    return f"{value:.{max(1, decimals(step))}f}"


def parse_number(text, like):
    """The number typed, an int if the knob is one and the number whole; None if not a number."""
    try:
        value = float(text.replace(",", "."))
    except ValueError:
        return None
    if isinstance(like, int) and value.is_integer() and "." not in text:
        return int(value)
    return value


def is_color(value):
    """A "#rrggbb" string, or three or four whole numbers of 0 to 255."""
    if isinstance(value, str) and re.fullmatch(r"#[0-9a-fA-F]{6}", value):
        return True
    return (
        isinstance(value, (tuple, list))
        and len(value) in (3, 4)
        and all(isinstance(v, int) and not isinstance(v, bool) and 0 <= v <= 255 for v in value)
    )


def color_hex(value):
    if isinstance(value, str):
        return value.lower()
    return "#%02x%02x%02x" % tuple(max(0, min(255, round(v))) for v in value[:3])


def color_from_hex(hex_text, like):
    """A color in the form of `like` (a hex string, or a tuple or list of ints or floats)."""
    if isinstance(like, str):
        return hex_text.lower()
    rgb = [int(hex_text[i : i + 2], 16) for i in (1, 3, 5)]
    if all(isinstance(v, float) for v in like[:3]):
        rgb = [float(v) for v in rgb]
    return type(like)(rgb + list(like[3:]))


GAME_TILE_SIZE = TILE + 2 * TILE_PADDING


def is_game_tile(image):
    """Already in the game's file format of a ground texture (see game_tile())."""
    return image.size == (GAME_TILE_SIZE, GAME_TILE_SIZE)


def is_texture(image):
    """An opaque picture whose sides are whole tiles, or a ground file of the game: a
    ground texture."""
    if (image.width % TILE or image.height % TILE) and not is_game_tile(image):
        return False
    return "A" not in image.getbands() or image.getchannel("A").getextrema()[0] == 255


def texture_tiles(image):
    """The part of a ground texture that repeats (a game file without its border)."""
    if is_game_tile(image):
        return image.crop((TILE_PADDING, TILE_PADDING, TILE_PADDING + TILE, TILE_PADDING + TILE))
    return image


def game_tile(image):
    """A ground texture of one tile in the game's file format: 192 x 192 RGBA, the
    tile with TILE_PADDING pixels of its opposite edges around it (sprites.c)."""
    if is_game_tile(image):
        return image.convert("RGBA")
    tile = image.convert("RGBA")
    tiled = Image.new("RGBA", (TILE * 3, TILE * 3))
    for j in range(3):
        for i in range(3):
            tiled.paste(tile, (i * TILE, j * TILE))
    return tiled.crop((TILE - TILE_PADDING, TILE - TILE_PADDING, 2 * TILE + TILE_PADDING, 2 * TILE + TILE_PADDING))


# ---------------------------------------------------------------------------
# Scripts

KNOB_NAME = re.compile(r"[A-Z][A-Z0-9_]*$")
KEPT_STATEMENTS = (
    ast.Import,
    ast.ImportFrom,
    ast.FunctionDef,
    ast.AsyncFunctionDef,
    ast.ClassDef,
    ast.Assign,
    ast.AnnAssign,
    ast.AugAssign,
)
TAG_SCHEMA = re.compile(r'\(\s*"(\w+)"\s*,\s*([A-Za-z_][\w\[\], ]*)\)')
COLUMN_LIST = re.compile(r"\(([A-Za-z_][\w ]*(?:,\s*[A-Za-z_][\w ]*)+)\)")


@dataclass
class Knob:
    name: str
    default: object
    node: ast.Assign
    kind: str  # number, color, palette, vector, table, text
    comment: str = ""
    section: str = ""
    columns: dict = None  # Column names: "" for untagged rows, else by first-cell tag.
    multiline: bool = False
    argv: bool = False  # Read from the command line: the literal is the default.
    hidden: bool = False

    @property
    def literal_node(self):
        """The node holding the literal (what a write-back replaces)."""
        return self.node.value.orelse if self.argv else self.node.value


@dataclass
class SpriteEntry:
    name: str
    expr: ast.expr = None  # SVG scripts only.
    # (key, call node index in ast.walk order, arg position or keyword, default)
    numbers: list = field(default_factory=list)


def walk_calls(expr):
    return [node for node in ast.walk(expr) if isinstance(node, ast.Call)]


def literal_number(node):
    if isinstance(node, ast.Constant) and is_number(node.value):
        return node.value
    if (
        isinstance(node, ast.UnaryOp)
        and isinstance(node.op, ast.USub)
        and isinstance(node.operand, ast.Constant)
        and is_number(node.operand.value)
    ):
        return -node.operand.value
    return None


class Script:
    """A sprite script read from disk: its knobs, its pictures, and running it.
    Running happens in the render thread only."""

    def __init__(self, path):
        self.path = path
        self.mtime = os.path.getmtime(path)
        with open(path, encoding="utf-8") as handle:
            self.source = handle.read()
        self.tree = ast.parse(self.source, path)
        self.lines = self.source.splitlines()
        self.sprites, sprite_statements = self._find_sprites()
        self.svg = bool(self.sprites)  # Else run whole, its pictures caught.
        self.knobs = self._find_knobs()
        if self.svg:
            self.body = [
                statement
                for statement in self.tree.body
                if statement not in sprite_statements
                and (
                    isinstance(statement, KEPT_STATEMENTS)
                    or (isinstance(statement, ast.Expr) and isinstance(statement.value, ast.Constant))
                )
            ]
        else:
            self.body = list(self.tree.body)
        self._cache_key = None
        self._cache = None
        self.namespace_seen = None  # The last namespace run (read by the window).

    # -- reading -------------------------------------------------------------

    def _find_sprites(self):
        sprites, statements = [], set()
        for statement in self.tree.body:
            if isinstance(statement, ast.Assign):
                candidate = statement.value
            elif isinstance(statement, ast.For):
                candidate = statement.iter
            else:
                continue
            if not isinstance(candidate, (ast.Tuple, ast.List)) or not candidate.elts:
                continue
            pairs = []
            for element in candidate.elts:
                if (
                    isinstance(element, ast.Tuple)
                    and len(element.elts) == 2
                    and isinstance(element.elts[0], ast.Constant)
                    and isinstance(element.elts[0].value, str)
                ):
                    pairs.append(SpriteEntry(element.elts[0].value, element.elts[1]))
                else:
                    pairs = []
                    break
            if pairs:
                sprites.extend(pairs)
                if isinstance(statement, ast.Assign):
                    statements.add(statement)
        for entry in sprites:
            for call_index, call in enumerate(walk_calls(entry.expr)):
                for position, arg in enumerate(call.args):
                    value = literal_number(arg)
                    if value is not None:
                        entry.numbers.append((f"{call_index}:{position}", call_index, position, value))
                for keyword in call.keywords:
                    value = literal_number(keyword.value)
                    if keyword.arg and value is not None:
                        key = f"{call_index}:{keyword.arg}"
                        entry.numbers.append((key, call_index, keyword.arg, value))
        return sprites, statements

    def _comment_after(self, node):
        line = self.lines[node.end_lineno - 1].encode()
        tail = line[node.end_col_offset :].decode(errors="replace")
        match = re.search(r"#\s*(.*)", tail)
        return match.group(1).strip() if match else ""

    def _comments_above(self, node):
        found, line = [], node.lineno - 2
        while line >= 0 and self.lines[line].strip().startswith("#"):
            found.insert(0, self.lines[line].strip().lstrip("#").strip())
            line -= 1
        return " ".join(found).strip(" -=")

    @staticmethod
    def _knob_value(node):
        """(default, read from the command line) of an assignment's value, or None."""
        try:
            return ast.literal_eval(node), False
        except (ValueError, SyntaxError, TypeError):
            pass
        if isinstance(node, ast.IfExp) and "argv" in ast.unparse(node.test):
            try:
                return ast.literal_eval(node.orelse), True
            except (ValueError, SyntaxError, TypeError):
                pass
        return None

    def _find_knobs(self):
        schemas = {}
        for match in TAG_SCHEMA.finditer(self.source):
            names = [name.strip(" []") for name in match.group(2).replace("[", ",").split(",")]
            schemas.setdefault(match.group(1), [name for name in names if name])
        knobs, last_columns = [], {}
        for statement in self.tree.body:
            if not (
                isinstance(statement, ast.Assign)
                and len(statement.targets) == 1
                and isinstance(statement.targets[0], ast.Name)
                and KNOB_NAME.match(statement.targets[0].id)
            ):
                continue
            found = self._knob_value(statement.value)
            if found is None:
                continue
            value, argv = found
            section = self._comments_above(statement)
            knob = Knob(statement.targets[0].id, value, statement, "text", self._comment_after(statement),
                        section, argv=argv)
            # An output folder or file name from the command line: nothing to tune here.
            knob.hidden = argv and not is_number(value)
            rows = isinstance(value, (list, tuple)) and value and all(
                isinstance(row, (list, tuple)) for row in value
            )
            # Columns named in the comment above, other than (R, G, B): a table of numbers,
            # whatever they look like.
            named = COLUMN_LIST.search(section)
            names = [name.strip().lower() for name in named.group(1).split(",")] if named else []
            named = bool(rows and len(names) == len(value[0]) and names[:3] != ["r", "g", "b"])
            if is_number(value):
                knob.kind = "number"
            elif is_color(value) and not isinstance(value, list):
                knob.kind = "color"
            elif rows and not named and len(value) >= 2 and all(
                is_color(v) and not isinstance(v, list) for v in value
            ):
                knob.kind = "palette"
                knob.multiline = statement.value.end_lineno > statement.value.lineno
            elif isinstance(value, (list, tuple)) and 2 <= len(value) <= 4 and all(is_number(v) for v in value):
                knob.kind = "vector"
            elif (
                isinstance(value, (list, tuple))
                and value
                and all(isinstance(row, (list, tuple)) for row in value)
            ):
                knob.kind = "table"
                knob.multiline = statement.value.end_lineno > statement.value.lineno
                width = len(value[0])
                match = COLUMN_LIST.search(section)
                if match:
                    names = [name.strip() for name in match.group(1).split(",")]
                    if len(names) == width:
                        last_columns[width] = names
                knob.columns = {"": last_columns.get(width)}
                knob.columns.update(schemas)
            knobs.append(knob)
        return knobs

    # -- running -------------------------------------------------------------

    def run(self, overrides):
        """Runs the script with knobs replaced (cached for the same knobs): its namespace,
        and the pictures it wrote (scripts run whole)."""
        key = format_literal(sorted(overrides.items()))
        if key == self._cache_key:
            return self._cache
        by_node = {knob.node: knob for knob in self.knobs if knob.name in overrides or knob.argv}
        body = []
        for statement in self.body:
            knob = by_node.get(statement)
            if knob is not None:
                literal = format_literal(overrides.get(knob.name, knob.default))
                value = ast.parse(literal, mode="eval").body
                statement = ast.copy_location(ast.Assign(targets=statement.targets, value=value), statement)
            body.append(statement)
        module = ast.fix_missing_locations(ast.Module(body=body, type_ignores=[]))
        namespace = {
            "__name__": "sprite_script" if self.svg else "__main__",
            "__file__": self.path,
            "__builtins__": SCRIPT_BUILTINS,
        }
        CAPTURE.pictures = []
        try:
            exec(compile(module, self.path, "exec"), namespace)
            pictures = CAPTURE.pictures
        finally:
            CAPTURE.pictures = None
        self._cache_key, self._cache = key, (namespace, pictures)
        self.namespace_seen = namespace
        return self._cache

    def number_labels(self, entry, namespace):
        """Names of the numbers of a sprite's expression: function and parameter."""
        calls = walk_calls(entry.expr)
        labels = {}
        for key, call_index, position, _ in entry.numbers:
            call = calls[call_index]
            function = ast.unparse(call.func)
            parameter = position if isinstance(position, str) else f"argument {position + 1}"
            target = namespace.get(function) if namespace else None
            if isinstance(position, int) and callable(target):
                try:
                    names = list(inspect.signature(target).parameters)
                    if position < len(names):
                        parameter = names[position]
                except (TypeError, ValueError):
                    pass
            labels[key] = f"{function}: {parameter}"
        return labels

    def sprite_expression(self, entry, numbers):
        """The sprite's expression with its numbers replaced."""
        expr = ast.parse(ast.unparse(entry.expr), mode="eval").body  # A copy.
        calls = walk_calls(expr)
        for key, call_index, position, _ in entry.numbers:
            if key not in numbers:
                continue
            node = ast.Constant(numbers[key])
            if isinstance(position, int):
                calls[call_index].args[position] = node
            else:
                for keyword in calls[call_index].keywords:
                    if keyword.arg == position:
                        keyword.value = node
        return expr

    def sprite_svg(self, entry, overrides, numbers):
        namespace, _ = self.run(overrides)
        expr = ast.fix_missing_locations(ast.Expression(self.sprite_expression(entry, numbers)))
        CAPTURE.pictures = []
        try:
            result = eval(compile(expr, self.path, "eval"), namespace)
            if callable(result):
                result = result()
        finally:
            CAPTURE.pictures = None
        if not isinstance(result, str):
            raise TypeError(f"{entry.name}: the expression gives {type(result).__name__}, not SVG text")
        if not result.lstrip().startswith(("<svg", "<?xml")):
            wrap = namespace.get("svg")
            if callable(wrap):
                result = wrap(result)
            else:
                result = (
                    f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {TILE} {TILE}" '
                    f'width="{TILE}" height="{TILE}">{result}</svg>'
                )
        return result

    # -- writing -------------------------------------------------------------

    def source_with(self, overrides, sprite_numbers):
        """The script's source with knobs and sprite numbers replaced (the rest unchanged)."""
        edits = []  # (first line, first column, last line, last column, text): byte columns.
        for knob in self.knobs:
            if knob.name in overrides and overrides[knob.name] != knob.default:
                value = knob.literal_node
                text = format_literal(overrides[knob.name], knob.multiline)
                edits.append((value.lineno, value.col_offset, value.end_lineno, value.end_col_offset, text))
        for entry in self.sprites:
            numbers = sprite_numbers.get(entry.name, {})
            calls = walk_calls(entry.expr)
            for key, call_index, position, default in entry.numbers:
                if key not in numbers or numbers[key] == default:
                    continue
                call = calls[call_index]
                if isinstance(position, int):
                    node = call.args[position]
                else:
                    node = next(k.value for k in call.keywords if k.arg == position)
                text = format_literal(numbers[key])
                edits.append((node.lineno, node.col_offset, node.end_lineno, node.end_col_offset, text))
        lines = [line.encode() for line in self.source.splitlines(keepends=True)]
        starts = [0]
        for line in lines:
            starts.append(starts[-1] + len(line))
        data = b"".join(lines)
        for first, col, last, end_col, text in sorted(edits, reverse=True):
            begin, end = starts[first - 1] + col, starts[last - 1] + end_col
            data = data[:begin] + text.encode() + data[end:]
        return data.decode()


@dataclass
class Drawing:
    """What the render thread gives back."""

    script: Script
    names: list  # The script's pictures.
    name: str = None  # The one drawn.
    image: Image.Image = None
    svg_text: str = None
    labels: dict = None  # Names of a sprite's numbers.
    error: str = None
    milliseconds: float = 0


def draw(script, name, overrides, numbers):
    """Runs in the render thread: the picture `name` of the script (its first if None)."""
    started = time.perf_counter()
    result = Drawing(script, [entry.name for entry in script.sprites])
    try:
        if script.svg:
            entry = next((e for e in script.sprites if e.name == name), script.sprites[0])
            result.name = entry.name
            result.svg_text = script.sprite_svg(entry, overrides, numbers.get(entry.name, {}))
            result.image = rasterize(result.svg_text, *svg_size(result.svg_text))
            result.labels = script.number_labels(entry, script.namespace_seen)
        else:
            _, pictures = script.run(overrides)
            result.names = [picture_name for picture_name, _ in pictures]
            if not pictures:
                raise RuntimeError("the script wrote no picture (Pillow's save, or an SVG file)")
            result.name, picture = next(((n, p) for n, p in pictures if n == name), pictures[0])
            if isinstance(picture, str):
                result.svg_text = picture
                result.image = rasterize(picture, *svg_size(picture))
            else:
                result.image = picture.convert("RGBA") if picture.mode != "RGBA" else picture
    except Exception as error:  # noqa: BLE001 - any error of the script is shown.
        traceback.print_exc()
        result.error = f"{type(error).__name__}: {error}"
    result.milliseconds = (time.perf_counter() - started) * 1000
    return result


# ---------------------------------------------------------------------------
# Widgets


class Tooltip:
    def __init__(self, widget, text):
        self.widget, self.text, self.window = widget, text, None
        widget.bind("<Enter>", self.show, add="+")
        widget.bind("<Leave>", self.hide, add="+")

    def show(self, _event=None):
        if self.window or not self.text:
            return
        x, y = self.widget.winfo_pointerx() + 14, self.widget.winfo_pointery() + 10
        self.window = tk.Toplevel(self.widget)
        self.window.wm_overrideredirect(True)
        self.window.geometry(f"+{x}+{y}")
        tk.Label(self.window, text=self.text, background="#ffffe0", relief="solid", borderwidth=1,
                 justify="left", padx=4, pady=2).pack()

    def hide(self, _event=None):
        if self.window:
            self.window.destroy()
            self.window = None


WHEEL_EVENTS = ("<Button-4>", "<Button-5>", "<MouseWheel>")


def wheel_scrolls_panel(widget):
    """The mouse wheel over `widget` scrolls the panel it is in, and leaves its value alone
    (spin boxes and sliders change their value with the wheel, a trap while scrolling)."""

    def scroll(event):
        holder = widget.master
        while holder is not None and not isinstance(holder, ScrollFrame):
            holder = holder.master
        if holder is not None:
            holder.scroll(event)
        return "break"

    for sequence in WHEEL_EVENTS:
        widget.bind(sequence, scroll)


class ScrollFrame(ttk.Frame):
    """A frame whose content scrolls vertically (with the mouse wheel too)."""

    def __init__(self, parent):
        super().__init__(parent)
        self.canvas = tk.Canvas(self, highlightthickness=0, borderwidth=0)
        bar = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=bar.set)
        self.inner = ttk.Frame(self.canvas)
        window = self.canvas.create_window((0, 0), window=self.inner, anchor="nw")
        self.inner.bind("<Configure>", lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self.canvas.bind("<Configure>", lambda e: self.canvas.itemconfigure(window, width=e.width))
        self.canvas.pack(side="left", fill="both", expand=True)
        bar.pack(side="right", fill="y")
        for sequence in WHEEL_EVENTS:
            self.bind_all(sequence, self._wheel, add="+")

    def _wheel(self, event):
        # The path only: winfo_containing() fails over Tk's own widgets (a combo box's list).
        path = str(self.tk.call("winfo", "containing", event.x_root, event.y_root))
        if not path.startswith(str(self)):
            return
        self.scroll(event)

    def scroll(self, event):
        if event.num == 4 or getattr(event, "delta", 0) > 0:
            self.canvas.yview_scroll(-2, "units")
        else:
            self.canvas.yview_scroll(2, "units")

    def to_top(self):
        self.canvas.yview_moveto(0)


class NumberField(ttk.Frame):
    """A number: a spin box, with a slider or not."""

    def __init__(self, parent, value, on_change, slider=True, width=8):
        super().__init__(parent)
        self.like, self.on_change, self.busy = value, on_change, False
        self.step = number_step(value)
        self.var = tk.StringVar(value=format_number(value, self.step))
        fmt = "%.0f" if isinstance(value, int) else f"%.{max(1, decimals(self.step))}f"
        self.spin = ttk.Spinbox(self, from_=-1e9, to=1e9, increment=self.step, textvariable=self.var,
                                width=width, format=fmt)
        self.scale = None
        if slider:
            low, high = number_range(value)
            self.scale = ttk.Scale(self, from_=low, to=high, value=value, command=self._slid)
            self.scale.pack(side="left", fill="x", expand=True, padx=(0, 4))
        self.spin.pack(side="left")
        for widget in (self.spin, self.scale):
            if widget is not None:
                wheel_scrolls_panel(widget)
        self.var.trace_add("write", self._typed)

    def _slid(self, text):
        if self.busy:
            return
        value = round(float(text) / self.step) * self.step
        value = int(round(value)) if isinstance(self.like, int) else round(value, 4)
        self.busy = True
        self.var.set(format_number(value, self.step))
        self.busy = False
        self.on_change(value)

    def _typed(self, *_args):
        if self.busy:
            return
        value = parse_number(self.var.get(), self.like)
        if value is None:
            return
        if self.scale is not None:
            self.busy = True
            low, high = float(self.scale.cget("from")), float(self.scale.cget("to"))
            if value < low:
                self.scale.configure(from_=value)
            if value > high:
                self.scale.configure(to=value)
            self.scale.set(value)
            self.busy = False
        self.on_change(value)


class ColorField(ttk.Frame):
    def __init__(self, parent, value, on_change, show_numbers=True):
        super().__init__(parent)
        self.like, self.on_change = value, on_change
        self.swatch = tk.Label(self, width=4, background=color_hex(value), relief="solid", borderwidth=1,
                               cursor="hand2")
        self.swatch.pack(side="left", padx=(0, 6), ipady=1)
        self.swatch.bind("<Button-1>", self._pick)
        self.var = tk.StringVar(value=color_hex(value))
        ttk.Entry(self, textvariable=self.var, width=9).pack(side="left")
        self.numbers = None
        if show_numbers and not isinstance(value, str):
            self.numbers = ttk.Label(self, foreground="#666")
            self.numbers.pack(side="left", padx=6)
            self.numbers.configure(text=format_literal(tuple(value)))
        self.var.trace_add("write", self._typed)

    def _pick(self, _event):
        chosen = colorchooser.askcolor(color=self.var.get(), parent=self)
        if chosen and chosen[1]:
            self.var.set(chosen[1])

    def _typed(self, *_args):
        text = self.var.get().strip()
        if not re.fullmatch(r"#[0-9a-fA-F]{6}", text):
            return
        value = color_from_hex(text, self.like)
        self.swatch.configure(background=text)
        if self.numbers is not None:
            self.numbers.configure(text=format_literal(tuple(value)))
        self.on_change(value)


# ---------------------------------------------------------------------------
# The window


def checker_tile(size=TILE, square=16):
    image = Image.new("RGBA", (size, size), (150, 150, 150, 255))
    draw_ = ImageDraw.Draw(image)
    for y in range(0, size, square):
        for x in range(0, size, square):
            if (x // square + y // square) % 2:
                draw_.rectangle([x, y, x + square - 1, y + square - 1], fill=(110, 110, 110, 255))
    return image


def tile_texture(path):
    """The tile of a ground sprite file: its middle TILE x TILE (the rest is padding, sprites.c)."""
    image = Image.open(path).convert("RGBA")
    if image.width > TILE:
        pad_x, pad_y = (image.width - TILE) // 2, (image.height - TILE) // 2
        image = image.crop((pad_x, pad_y, pad_x + TILE, pad_y + TILE))
    return image.resize((TILE, TILE)) if image.size != (TILE, TILE) else image


class Workbench(tk.Tk):
    def __init__(self, first_script=None):
        super().__init__()
        self.title("Sprite workbench")
        self.geometry("1280x820")
        self.minsize(900, 600)
        style = ttk.Style(self)
        if "clam" in style.theme_names():
            style.theme_use("clam")
        bold = tkfont.nametofont("TkDefaultFont").copy()
        bold.configure(weight="bold")
        small = tkfont.nametofont("TkDefaultFont").copy()
        small.configure(size=max(7, small.cget("size") - 1))
        style.configure("Section.TLabel", font=bold)
        style.configure("Changed.TLabel", font=bold, foreground="#1d5fbf")
        style.configure("Hint.TLabel", font=small, foreground="#777")
        style.configure("Error.TLabel", foreground="#b00020")

        self.scripts = {}  # path -> Script
        self.overrides = {}  # path -> {knob: value}
        self.numbers = {}  # path -> {sprite: {key: value}}
        self.chosen_sprite = {}  # path -> picture name
        self.picture_names = {}  # path -> the pictures a whole-run script wrote last time
        self.script = None
        self.drawing = None  # The last Drawing shown.
        self.live_grounds = {}  # name -> ground texture drawn here
        self.render_job = None
        self.scene_job = None
        self.rendering = False
        self.render_again = False
        self.results = queue.Queue()
        self.labels_missing = False

        self.grounds = {os.path.splitext(os.path.basename(p))[0][len("ground_") :]: p
                        for p in sorted(glob.glob(os.path.join(SPRITES_DIR, "ground_*.png")))}
        self.grasses = {os.path.splitext(os.path.basename(p))[0]: p
                        for p in sorted(glob.glob(os.path.join(SPRITES_DIR, "grass_*.png")))}
        self.texture_cache = {}

        self._build_ui()
        paths = sorted(glob.glob(os.path.join(SCRIPTS_DIR, "*.py")))
        if first_script:
            first_script = os.path.abspath(first_script)
            if first_script not in paths:
                paths.append(first_script)
        self.script_paths = paths
        self.script_box["values"] = [self._script_label(p) for p in paths]
        if paths:
            self._open_script(first_script or paths[0])
        else:
            self._status(f"No script in {SCRIPTS_DIR}: open one.", error=True)
        self.after(POLL_MS, self._poll)

    # -- layout --------------------------------------------------------------

    def _build_ui(self):
        panes = ttk.PanedWindow(self, orient="horizontal")
        panes.pack(fill="both", expand=True)
        left = ttk.Frame(panes, padding=8)
        right = ttk.Frame(panes, padding=(0, 8, 8, 8))
        panes.add(left, weight=0)
        panes.add(right, weight=1)

        top = ttk.Frame(left)
        top.pack(fill="x")
        top.columnconfigure(1, weight=1)
        ttk.Label(top, text="Script").grid(row=0, column=0, sticky="w", pady=2)
        self.script_box = ttk.Combobox(top, state="readonly", width=34)
        self.script_box.grid(row=0, column=1, sticky="ew", padx=4)
        self.script_box.bind("<<ComboboxSelected>>",
                             lambda e: self._open_script(self.script_paths[self.script_box.current()]))
        ttk.Button(top, text="Open…", command=self._ask_script).grid(row=0, column=2)
        ttk.Label(top, text="Picture").grid(row=1, column=0, sticky="w", pady=2)
        self.sprite_box = ttk.Combobox(top, state="readonly")
        self.sprite_box.grid(row=1, column=1, columnspan=2, sticky="ew", padx=4)
        self.sprite_box.bind("<<ComboboxSelected>>", lambda e: self._choose_sprite(self.sprite_box.get()))

        ttk.Label(top, text="Ground").grid(row=2, column=0, sticky="w", pady=2)
        self.ground_var = tk.StringVar(value="soil" if "soil" in self.grounds else CHECKER)
        self.ground_box = ttk.Combobox(top, state="readonly", textvariable=self.ground_var)
        self.ground_box.grid(row=2, column=1, columnspan=2, sticky="ew", padx=4)
        self._ground_choices()
        ttk.Label(top, text="Grass").grid(row=3, column=0, sticky="w", pady=2)
        self.grass_var = tk.StringVar(value=NO_GRASS)
        grass_box = ttk.Combobox(top, state="readonly", textvariable=self.grass_var,
                                 values=[NO_GRASS] + list(self.grasses))
        grass_box.grid(row=3, column=1, columnspan=2, sticky="ew", padx=4)
        ttk.Label(top, text="Zoom").grid(row=4, column=0, sticky="w", pady=2)
        self.zoom_var = tk.StringVar(value="300%")
        zoom_box = ttk.Combobox(top, state="readonly", textvariable=self.zoom_var, values=ZOOMS, width=8)
        zoom_box.grid(row=4, column=1, sticky="w", padx=4)
        for box in (self.ground_box, grass_box, zoom_box):
            box.bind("<<ComboboxSelected>>", lambda e: self._schedule_scene())

        checks = ttk.Frame(left)
        checks.pack(fill="x", pady=(4, 0))
        self.mirror_var = tk.BooleanVar(value=False)
        self.fill_var = tk.BooleanVar(value=False)
        self.grid_var = tk.BooleanVar(value=False)
        for text, var, tip in (
            ("Mirrored", self.mirror_var, "Flipped left to right, as the game does on some tiles"),
            ("Fill around", self.fill_var, "The same sprite on every tile, some mirrored, back to front"),
            ("Tile grid", self.grid_var, "The edges of the tiles"),
        ):
            check = ttk.Checkbutton(checks, text=text, variable=var, command=self._schedule_scene)
            check.pack(side="left", padx=(0, 10))
            Tooltip(check, tip)

        ttk.Separator(left).pack(fill="x", pady=8)
        head = ttk.Frame(left)
        head.pack(fill="x")
        ttk.Label(head, text="Knobs", style="Section.TLabel").pack(side="left")
        ttk.Button(head, text="Reset all", command=self._reset_all).pack(side="right")
        self.knob_scroll = ScrollFrame(left)
        self.knob_scroll.pack(fill="both", expand=True, pady=(4, 0))
        self.knob_scroll.configure(width=470)

        self.canvas = tk.Canvas(right, background="#202020", highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)
        self.canvas.bind("<Configure>", lambda e: self._schedule_scene())

        bottom = ttk.Frame(right, padding=(0, 8, 0, 0))
        bottom.pack(fill="x")
        thumb_frame = ttk.Frame(bottom)
        thumb_frame.pack(side="left", padx=(0, 12))
        self.thumb = ttk.Label(thumb_frame)
        self.thumb.pack()
        self.size_label = ttk.Label(thumb_frame, style="Hint.TLabel", text="")
        self.size_label.pack()

        buttons = ttk.Frame(bottom)
        buttons.pack(side="left", fill="x", expand=True, anchor="n")
        row1 = ttk.Frame(buttons)
        row1.pack(fill="x", pady=2)
        ttk.Button(row1, text="Save PNG…", command=self._save_png).pack(side="left", padx=(0, 4))
        self.svg_button = ttk.Button(row1, text="Save SVG…", command=self._save_svg)
        self.svg_button.pack(side="left", padx=4)
        ttk.Label(row1, text="Size").pack(side="left", padx=(12, 4))
        self.export_var = tk.StringVar(value=SCALES[0])
        self.export_box = ttk.Combobox(row1, state="readonly", textvariable=self.export_var, values=SCALES,
                                       width=9)
        self.export_box.pack(side="left")
        Tooltip(self.export_box, f"{GAME_TILE}: a ground texture as the game reads it, "
                                 f"{GAME_TILE_SIZE} × {GAME_TILE_SIZE} with a border "
                                 f"of its opposite edges")
        row2 = ttk.Frame(buttons)
        row2.pack(fill="x", pady=2)
        ttk.Button(row2, text="Copy picture", command=self._copy_sprite).pack(side="left", padx=(0, 4))
        ttk.Button(row2, text="Copy preview", command=self._copy_preview).pack(side="left", padx=4)
        ttk.Button(row2, text="Copy knobs", command=self._copy_knobs).pack(side="left", padx=4)
        ttk.Button(row2, text="Save script as…", command=self._save_script).pack(side="left", padx=4)
        self.status = ttk.Label(buttons, text="", wraplength=640, justify="left")
        self.status.pack(fill="x", pady=(6, 0))

        self.bind("<Control-s>", lambda e: self._save_png())
        self.bind("<F5>", lambda e: self._reload(force=True))

    def _status(self, text, error=False):
        self.status.configure(text=text, style="Error.TLabel" if error else "TLabel")

    def _script_label(self, path):
        return os.path.relpath(path, SCRIPTS_DIR) if path.startswith(SCRIPTS_DIR + os.sep) else path

    def _ground_choices(self):
        self.ground_box["values"] = [CHECKER] + list(self.live_grounds) + list(self.grounds)

    # -- scripts and pictures ------------------------------------------------

    def _ask_script(self):
        path = filedialog.askopenfilename(parent=self, initialdir=SCRIPTS_DIR,
                                          filetypes=[("Python", "*.py"), ("All files", "*")])
        if path:
            path = os.path.abspath(path)
            if path not in self.script_paths:
                self.script_paths.append(path)
                self.script_box["values"] = [self._script_label(p) for p in self.script_paths]
            self._open_script(path)

    def _load(self, path):
        try:
            script = Script(path)
        except (OSError, SyntaxError) as error:
            self._status(f"{os.path.basename(path)}: {error}", error=True)
            return None
        self.scripts[path] = script
        return script

    def _open_script(self, path):
        script = self.scripts.get(path) or self._load(path)
        if script is None:
            return
        self.script = script
        self.script_box.current(self.script_paths.index(path))
        names = [entry.name for entry in script.sprites] if script.svg else self.picture_names.get(path, [])
        self.sprite_box["values"] = names
        self.overrides.setdefault(path, {})
        self.numbers.setdefault(path, {})
        self._choose_sprite(self.chosen_sprite.get(path))

    def _choose_sprite(self, name):
        if self.script is None:
            return
        names = list(self.sprite_box["values"])
        if name not in names:
            name = names[-1] if self.script.svg and names else (names[0] if names else None)
        self.chosen_sprite[self.script.path] = name
        self.sprite_box.set(name or "")
        self._build_knobs()
        self.knob_scroll.to_top()
        self._schedule_render()

    def _entry(self):
        name = self.chosen_sprite.get(self.script.path) if self.script else None
        return next((e for e in self.script.sprites if e.name == name), None) if name else None

    def _reload(self, force=False):
        script = self.script
        if script is None or self.rendering:
            return
        try:
            mtime = os.path.getmtime(script.path)
        except OSError:
            return
        if not force and mtime == script.mtime:
            return
        fresh = self._load(script.path)
        if fresh is None:
            return
        defaults = {knob.name: knob.default for knob in fresh.knobs}
        overrides = self.overrides[script.path]
        for name in list(overrides):
            if name not in defaults or overrides[name] == defaults[name]:
                del overrides[name]
        self._open_script(script.path)
        self._status(f"{os.path.basename(script.path)} read again.")

    def _poll(self):
        self._reload()
        self.after(POLL_MS, self._poll)

    # -- knobs ---------------------------------------------------------------

    def _value(self, knob):
        return self.overrides[self.script.path].get(knob.name, knob.default)

    def _set(self, knob, value, title):
        overrides = self.overrides[self.script.path]
        if value == knob.default:
            overrides.pop(knob.name, None)
        else:
            overrides[knob.name] = value
        title.configure(style="Changed.TLabel" if knob.name in overrides else "TLabel")
        self._schedule_render()

    def _set_number(self, entry, key, default, value, title):
        numbers = self.numbers[self.script.path].setdefault(entry.name, {})
        if value == default:
            numbers.pop(key, None)
        else:
            numbers[key] = value
        title.configure(style="Changed.TLabel" if key in numbers else "TLabel")
        self._schedule_render()

    def _reset_all(self):
        if self.script is None:
            return
        self.overrides[self.script.path].clear()
        self.numbers[self.script.path].clear()
        self._build_knobs()
        self._schedule_render()

    def _build_knobs(self):
        inner = self.knob_scroll.inner
        for child in inner.winfo_children():
            child.destroy()
        inner.columnconfigure(1, weight=1)
        self.row = 0
        script, entry = self.script, self._entry()
        if script.svg and entry is not None and entry.numbers:
            namespace = script.namespace_seen
            self.labels_missing = namespace is None
            labels = script.number_labels(entry, namespace)
            self._section(f"{entry.name}: {ast.unparse(entry.expr)}")
            numbers = self.numbers[script.path].get(entry.name, {})
            for key, _, _, default in entry.numbers:
                title = self._title(labels[key], key in numbers)
                value = numbers.get(key, default)
                field_ = NumberField(inner, value,
                                     lambda v, k=key, d=default, t=title: self._set_number(entry, k, d, v, t))
                field_.grid(row=self.row, column=1, sticky="ew", pady=1)
                reset = ttk.Button(inner, text="↺", width=2,
                                   command=lambda k=key: self._reset_number(entry, k))
                reset.grid(row=self.row, column=2, padx=(4, 0))
                self.row += 1
        section = None
        for knob in script.knobs:
            if knob.hidden:
                continue
            if knob.section and knob.section != section:
                self._section(knob.section)
                section = knob.section
            self._knob(knob)

    def _reset_number(self, entry, key):
        self.numbers[self.script.path].get(entry.name, {}).pop(key, None)
        self._rebuild_keeping_scroll()

    def _reset_knob(self, knob):
        self.overrides[self.script.path].pop(knob.name, None)
        self._rebuild_keeping_scroll()

    def _rebuild_keeping_scroll(self):
        position = self.knob_scroll.canvas.yview()[0]
        self._build_knobs()
        self.update_idletasks()
        self.knob_scroll.canvas.yview_moveto(position)
        self._schedule_render()

    def _section(self, text):
        label = ttk.Label(self.knob_scroll.inner, text=text, style="Section.TLabel", wraplength=440)
        label.grid(row=self.row, column=0, columnspan=3, sticky="w", pady=(10, 2))
        self.row += 1

    def _title(self, text, changed):
        label = ttk.Label(self.knob_scroll.inner, text=text, style="Changed.TLabel" if changed else "TLabel")
        label.grid(row=self.row, column=0, sticky="nw", padx=(0, 8), pady=2)
        return label

    def _knob(self, knob):
        inner = self.knob_scroll.inner
        value = self._value(knob)
        title = self._title(knob.name, knob.name in self.overrides[self.script.path])
        if knob.comment:
            Tooltip(title, knob.comment)

        def change(v, k=knob, t=title):
            self._set(k, v, t)

        wide = knob.kind in ("table", "palette")
        if knob.kind == "number":
            editor = NumberField(inner, value, change)
        elif knob.kind == "color":
            editor = ColorField(inner, value, change)
        elif knob.kind == "palette":
            editor = self._palette_editor(inner, knob, value, change)
        elif knob.kind == "vector":
            editor = self._vector_editor(inner, value, change)
        elif knob.kind == "table":
            editor = self._table_editor(inner, knob, value, change)
        else:
            editor = self._text_editor(inner, value, change)
        ttk.Button(inner, text="↺", width=2, command=lambda: self._reset_knob(knob)).grid(
            row=self.row, column=2, sticky="n", padx=(4, 0))
        if wide:
            if knob.comment:
                ttk.Label(inner, text=knob.comment, style="Hint.TLabel", wraplength=300).grid(
                    row=self.row, column=1, sticky="w")
            self.row += 1
            editor.grid(row=self.row, column=0, columnspan=3, sticky="w", padx=(8, 0), pady=(0, 4))
        else:
            editor.grid(row=self.row, column=1, sticky="ew", pady=1)
        self.row += 1
        if knob.comment and not wide:
            ttk.Label(inner, text=knob.comment, style="Hint.TLabel", wraplength=360).grid(
                row=self.row, column=1, sticky="w")
            self.row += 1

    def _vector_editor(self, parent, value, change):
        frame = ttk.Frame(parent)
        current = list(value)

        def set_axis(axis, v):
            current[axis] = v
            change(type(value)(current))

        for axis, cell in enumerate(value):
            ttk.Label(frame, text="xyzw"[axis]).pack(side="left", padx=(0 if axis == 0 else 8, 2))
            NumberField(frame, cell, lambda v, a=axis: set_axis(a, v), slider=False, width=6).pack(side="left")
        return frame

    def _palette_editor(self, parent, knob, value, change):
        frame = ttk.Frame(parent)
        current = list(value)

        def set_color(index, v):
            current[index] = v
            change(type(value)(current))

        for index, color in enumerate(value):
            ttk.Label(frame, text=str(index), style="Hint.TLabel").grid(row=index, column=0, sticky="e",
                                                                       padx=(0, 4))
            ColorField(frame, color, lambda v, i=index: set_color(i, v)).grid(row=index, column=1, sticky="w",
                                                                              pady=1)
        ttk.Button(frame, text="Edit as text…", command=lambda: self._edit_as_text(knob)).grid(
            row=len(value), column=0, columnspan=2, sticky="w", pady=(2, 4))
        return frame

    def _text_editor(self, parent, value, change):
        var = tk.StringVar(value=format_literal(value))
        entry = ttk.Entry(parent, textvariable=var)

        def typed(*_args):
            try:
                change(ast.literal_eval(var.get()))
                entry.configure(foreground="")
            except (ValueError, SyntaxError):
                entry.configure(foreground="#b00020")

        var.trace_add("write", typed)
        return entry

    def _table_editor(self, parent, knob, value, change):
        frame = ttk.Frame(parent)
        rows = [list(row) for row in value]
        row_types = [type(row) for row in value]
        container = type(value)

        def emit():
            change(container(kind(row) for kind, row in zip(row_types, rows)))

        def set_cell(r, c, v):
            rows[r][c] = v
            emit()

        tags = {row[0] for row in rows if row and isinstance(row[0], str)}
        notes = [f"{tag} = ({', '.join(knob.columns[tag])})" for tag in sorted(tags) if knob.columns.get(tag)]
        plain = knob.columns.get("")
        grid_row = 0
        if notes:
            ttk.Label(frame, text="\n".join(notes), style="Hint.TLabel", wraplength=360,
                      justify="left").grid(row=grid_row, column=0, columnspan=12, sticky="w")
            grid_row += 1
        if plain and not tags:
            for c, name in enumerate(plain):
                ttk.Label(frame, text=name, style="Hint.TLabel").grid(row=grid_row, column=c + 1, sticky="w")
            grid_row += 1
        for r, row in enumerate(rows):
            ttk.Label(frame, text=str(r), style="Hint.TLabel").grid(row=grid_row, column=0, sticky="e",
                                                                   padx=(0, 4))
            names = None
            if row and isinstance(row[0], str):
                names = knob.columns.get(row[0])
                names = ([row[0]] + names) if names else None
            elif plain:
                names = plain
            for c, cell in enumerate(row):
                if is_number(cell):
                    widget = NumberField(frame, cell, lambda v, rr=r, cc=c: set_cell(rr, cc, v), slider=False,
                                         width=4)
                    tip = widget.spin
                else:
                    widget = ttk.Label(frame, text=format_literal(cell))
                    tip = widget
                widget.grid(row=grid_row, column=c + 1, sticky="w", padx=1, pady=1)
                if names and c < len(names):
                    Tooltip(tip, names[c])
            grid_row += 1
        ttk.Button(frame, text="Edit as text…", command=lambda: self._edit_as_text(knob)).grid(
            row=grid_row, column=0, columnspan=6, sticky="w", pady=(2, 4))
        return frame

    def _edit_as_text(self, knob):
        window = tk.Toplevel(self)
        window.title(knob.name)
        window.transient(self)
        text = tk.Text(window, width=70, height=18, font="TkFixedFont", undo=True)
        text.insert("1.0", format_literal(self._value(knob), multiline=True))
        text.pack(fill="both", expand=True, padx=8, pady=8)
        message = ttk.Label(window, text="Rows can be added, removed, lengthened.", style="Hint.TLabel")
        message.pack(fill="x", padx=8)

        def apply():
            try:
                value = ast.literal_eval(text.get("1.0", "end"))
            except (ValueError, SyntaxError) as error:
                message.configure(text=f"Not a literal: {error}", style="Error.TLabel")
                return
            if value == knob.default:
                self.overrides[self.script.path].pop(knob.name, None)
            else:
                self.overrides[self.script.path][knob.name] = value
            window.destroy()
            self._rebuild_keeping_scroll()

        buttons = ttk.Frame(window, padding=8)
        buttons.pack(fill="x")
        ttk.Button(buttons, text="Apply", command=apply).pack(side="right")
        ttk.Button(buttons, text="Cancel", command=window.destroy).pack(side="right", padx=6)

    # -- rendering (in a thread) ---------------------------------------------

    def _schedule_render(self):
        if self.render_job is not None:
            self.after_cancel(self.render_job)
        self.render_job = self.after(RENDER_DELAY_MS, self._render)

    def _render(self):
        self.render_job = None
        if self.script is None:
            return
        if self.rendering:
            self.render_again = True
            return
        script = self.script
        name = self.chosen_sprite.get(script.path)
        overrides = copy.deepcopy(self.overrides[script.path])
        numbers = copy.deepcopy(self.numbers[script.path])
        self.rendering = True
        self.render_again = False
        threading.Thread(target=lambda: self.results.put(draw(script, name, overrides, numbers)),
                         daemon=True).start()
        self.after(250, self._show_busy)
        self.after(20, self._collect)

    def _show_busy(self):
        if self.rendering:
            self._status(f"{self.chosen_sprite.get(self.script.path) or self.script_box.get()}: drawing…")

    def _collect(self):
        try:
            result = self.results.get_nowait()
        except queue.Empty:
            self.after(20, self._collect)
            return
        self.rendering = False
        self._shown(result)
        if self.render_again:
            self._render()

    def _shown(self, result):
        script = result.script
        if script is not self.script:
            return  # Another script was opened meanwhile.
        if not script.svg and result.names:
            self.picture_names[script.path] = result.names
        if list(self.sprite_box["values"]) != result.names:
            self.sprite_box["values"] = result.names
        if result.name and self.chosen_sprite.get(script.path) not in result.names:
            # None chosen yet, or the script no longer writes it: its first.
            self.chosen_sprite[script.path] = result.name
            self.sprite_box.set(result.name)
        if self.labels_missing and script.svg and result.labels:
            self._rebuild_keeping_scroll_quietly()
        if result.error:
            self._status(f"{result.name or os.path.basename(script.path)}: {result.error}", error=True)
            return
        if result.name != self.chosen_sprite.get(script.path):
            return  # Another picture was chosen meanwhile: drawn next.
        self.drawing = result
        image = result.image
        texture = is_texture(image)
        if texture:
            self.live_grounds[result.name + LIVE] = texture_tiles(image)
            self._ground_choices()
        one_tile = texture and texture_tiles(image).size == (TILE, TILE)
        choices = ((GAME_TILE,) if one_tile else ()) + SCALES
        if tuple(self.export_box["values"]) != choices:
            self.export_box["values"] = choices
            self.export_var.set(choices[0])
        self.svg_button.state(["!disabled"] if result.svg_text else ["disabled"])
        changed = len(self.overrides[script.path]) + len(self.numbers[script.path].get(result.name, {}))
        what = "sprite"
        if texture:
            what = "ground texture (a game file, with its border)" if is_game_tile(image) else "ground texture"
        self._status(f"{result.name}: {what}, {image.width} × {image.height}, drawn in {result.milliseconds:.0f} ms"
                     + (f", {changed} knob(s) changed" if changed else ""))
        self._show_thumb()
        self._schedule_scene()

    def _rebuild_keeping_scroll_quietly(self):
        position = self.knob_scroll.canvas.yview()[0]
        self._build_knobs()
        self.update_idletasks()
        self.knob_scroll.canvas.yview_moveto(position)

    def _show_thumb(self):
        image = self.drawing.image
        shown = image
        if max(image.size) > GAME_TILE_SIZE:  # A ground file shows whole.
            factor = -(-max(image.size) // GAME_TILE_SIZE)
            shown = image.reduce(factor)
        background = checker_tile(max(shown.size), 8).crop((0, 0) + shown.size)
        background.alpha_composite(shown.convert("RGBA"))
        self.thumb_photo = ImageTk.PhotoImage(background)
        self.thumb.configure(image=self.thumb_photo)
        note = "actual size" if shown is image else f"1/{image.width // shown.width} size"
        self.size_label.configure(text=f"{note}, {image.width} × {image.height}")

    def _texture(self, name, path_of):
        if name not in self.texture_cache:
            self.texture_cache[name] = tile_texture(path_of[name])
        return self.texture_cache[name]

    def _ground(self, picture_is_ground):
        """The ground of the preview: a picture whose sides are whole tiles, repeated."""
        if picture_is_ground:
            base = texture_tiles(self.drawing.image).convert("RGBA")
        else:
            ground = self.ground_var.get()
            if ground in self.live_grounds:
                base = self.live_grounds[ground].convert("RGBA")
            elif ground in self.grounds:
                base = self._texture(ground, self.grounds).copy()
            else:
                base = checker_tile()
        grass = self.grass_var.get()
        if grass in self.grasses:
            base = base.copy()
            overlay = self._texture(grass, self.grasses)
            for y in range(0, base.height, TILE):
                for x in range(0, base.width, TILE):
                    base.alpha_composite(overlay, (x, y))
        return base

    def _schedule_scene(self):
        if self.scene_job is not None:
            self.after_cancel(self.scene_job)
        self.scene_job = self.after(15, self._draw_scene)

    def scene(self, width, height):
        """The preview, width x height screen pixels at the zoom chosen: the sprite on its tile
        and the ground around, or a ground texture repeated."""
        zoom = int(self.zoom_var.get().rstrip("%")) / 100
        native_w, native_h = int(width / zoom) + 1, int(height / zoom) + 1
        sprite = self.drawing.image if self.drawing else None
        is_ground = sprite is not None and is_texture(sprite)
        if is_ground:
            sprite_h = 0
        else:
            sprite_h = sprite.height if sprite is not None else TILE
        # The middle tile in the middle, lowered so that a sprite taller than its tile shows whole.
        origin_x = native_w / 2 - TILE / 2
        origin_y = native_h / 2 - TILE / 2 + max(0, (sprite_h - TILE) / 2)
        first_i = -int(origin_x // TILE) - 1
        first_j = -int(origin_y // TILE) - 1
        last_i = int((native_w - origin_x) // TILE) + 1
        last_j = int((native_h - origin_y) // TILE) + 1
        image = Image.new("RGBA", (native_w, native_h), (32, 32, 32, 255))
        ground = self._ground(is_ground)
        period_x, period_y = ground.width // TILE, ground.height // TILE
        for j in range(first_j - first_j % period_y, last_j + 1, period_y):
            for i in range(first_i - first_i % period_x, last_i + 1, period_x):
                image.paste(ground, (round(origin_x + i * TILE), round(origin_y + j * TILE)))
        if sprite is not None and not is_ground:
            mirrored = sprite.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            spots = [(0, 0)]
            if self.fill_var.get():
                spots = [(i, j) for j in range(first_j, last_j + 2) for i in range(first_i, last_i + 1)]
            for i, j in spots:
                flip = self.mirror_var.get() if (i, j) == (0, 0) else ((i * 73856093) ^ (j * 19349663)) & 4
                anchor_x = origin_x + (i + 0.5) * TILE
                anchor_y = origin_y + (j + 1) * TILE
                left, top = int(anchor_x - sprite.width / 2), int(anchor_y - sprite.height)
                if left + sprite.width < 0 or top + sprite.height < 0 or left > native_w or top > native_h:
                    continue
                layer = Image.new("RGBA", image.size)
                layer.paste(mirrored if flip else sprite, (left, top))
                image.alpha_composite(layer)
        size = (max(1, round(native_w * zoom)), max(1, round(native_h * zoom)))
        image = image.resize(size, Image.Resampling.NEAREST if zoom >= 1 else Image.Resampling.BOX)
        if self.grid_var.get():
            draw_ = ImageDraw.Draw(image, "RGBA")
            for i in range(first_i, last_i + 2):
                x = round((origin_x + i * TILE) * zoom)
                draw_.line([(x, 0), (x, size[1])], fill=(255, 255, 255, 70))
            for j in range(first_j, last_j + 2):
                y = round((origin_y + j * TILE) * zoom)
                draw_.line([(0, y), (size[0], y)], fill=(255, 255, 255, 70))
        return image.crop((0, 0, width, height))

    def _draw_scene(self):
        self.scene_job = None
        width, height = self.canvas.winfo_width(), self.canvas.winfo_height()
        if width < 2 or height < 2:
            return
        self.scene_photo = ImageTk.PhotoImage(self.scene(width, height))
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, image=self.scene_photo, anchor="nw")

    # -- saving and copying --------------------------------------------------

    def _export_image(self):
        image, choice = self.drawing.image, self.export_var.get()
        if choice == GAME_TILE:
            return game_tile(image)
        scale = int(choice.lstrip("×"))
        if scale == 1:
            return image
        if self.drawing.svg_text:
            width, height = svg_size(self.drawing.svg_text)
            return rasterize(self.drawing.svg_text, width * scale, height * scale)
        return image.resize((image.width * scale, image.height * scale), Image.Resampling.NEAREST)

    def _export_name(self, extension):
        choice = self.export_var.get()
        suffix = "" if choice in (GAME_TILE, "×1") or extension == ".svg" else f"_x{choice.lstrip('×')}"
        return f"{self.drawing.name}{suffix}{extension}"

    def _ready(self):
        if self.drawing is None:
            self._status("Nothing drawn yet.", error=True)
            return False
        return True

    def _save_png(self):
        if not self._ready():
            return
        path = filedialog.asksaveasfilename(parent=self, initialdir=SPRITES_DIR,
                                            initialfile=self._export_name(".png"), defaultextension=".png",
                                            filetypes=[("PNG", "*.png")])
        if path:
            image = self._export_image()
            image.save(path)
            self._status(f"Saved {path} ({image.width} × {image.height})")

    def _save_svg(self):
        if not self._ready() or not self.drawing.svg_text:
            return
        path = filedialog.asksaveasfilename(parent=self, initialdir=SPRITES_DIR,
                                            initialfile=self._export_name(".svg"), defaultextension=".svg",
                                            filetypes=[("SVG", "*.svg")])
        if path:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(self.drawing.svg_text)
            self._status(f"Saved {path}")

    def _copy_png(self, image, what):
        buffer = io.BytesIO()
        image.save(buffer, "PNG")
        if os.environ.get("WAYLAND_DISPLAY") and shutil.which("wl-copy"):
            command = ["wl-copy", "--type", "image/png"]
        elif shutil.which("xclip"):
            command = ["xclip", "-selection", "clipboard", "-t", "image/png", "-i"]
        else:
            self._status("Copying a picture needs wl-copy or xclip.", error=True)
            return
        try:
            _run(command, input=buffer.getvalue(), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                 timeout=5, check=True)
        except (subprocess.SubprocessError, OSError) as error:
            self._status(f"Copy failed: {error}", error=True)
            return
        self._status(f"{what} copied ({image.width} × {image.height} PNG).")

    def _copy_sprite(self):
        if self._ready():
            self._copy_png(self._export_image(), "Picture")

    def _copy_preview(self):
        if self._ready():
            self._copy_png(self.scene(self.canvas.winfo_width(), self.canvas.winfo_height()), "Preview")

    def _copy_knobs(self):
        if self.script is None:
            return
        overrides = self.overrides[self.script.path]
        lines = [f"{knob.name} = {format_literal(overrides[knob.name], knob.multiline)}"
                 for knob in self.script.knobs if knob.name in overrides]
        entry = self._entry()
        numbers = self.numbers[self.script.path].get(entry.name, {}) if entry and self.script.svg else {}
        if numbers:
            expr = self.script.sprite_expression(entry, numbers)
            lines.append(f'("{entry.name}", {ast.unparse(expr)})')
        if not lines:
            self._status("No knob changed: nothing to copy.")
            return
        self.clipboard_clear()
        self.clipboard_append("\n".join(lines) + "\n")
        self._status(f"{len(lines)} line(s) of Python copied.")

    def _save_script(self):
        if self.script is None:
            return
        script = self.script
        source = script.source_with(self.overrides[script.path], self.numbers[script.path])
        path = filedialog.asksaveasfilename(parent=self, initialdir=os.path.dirname(script.path),
                                            initialfile=os.path.basename(script.path), defaultextension=".py",
                                            filetypes=[("Python", "*.py")])
        if not path:
            return
        try:
            ast.parse(source)
        except SyntaxError as error:
            messagebox.showerror("Sprite workbench", f"The script would not be valid Python: {error}")
            return
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(source)
        path = os.path.abspath(path)
        if path == script.path:
            # The knobs are its defaults now.
            self.overrides[path].clear()
            self.numbers[path].clear()
            self._reload(force=True)
        else:
            if path not in self.script_paths:
                self.script_paths.append(path)
                self.script_box["values"] = [self._script_label(p) for p in self.script_paths]
            self.overrides[path] = {}
            self.numbers[path] = {}
            self.chosen_sprite[path] = self.chosen_sprite.get(script.path)
            self._open_script(path)
        self._status(f"Saved {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("script", nargs="?", help="a sprite script (default: the first of tools/assets/sprites)")
    args = parser.parse_args()
    if RSVG is None and not shutil.which("rsvg-convert"):
        print("No SVG renderer (PyGObject with librsvg, or rsvg-convert): SVG scripts will fail.",
              file=sys.stderr)
    sys.argv = sys.argv[:1]  # Scripts run as from the command line with no argument.
    Workbench(args.script).mainloop()


if __name__ == "__main__":
    main()
