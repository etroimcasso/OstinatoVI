#!/usr/bin/env python3
"""Emit the battle picture's compiled-in tables from original-src btlgfx_main.asm.

Port-time tooling (NOT a build/CI dependency). Reads the small tables the battle
graphics program places its battlers, windows, numerals and messages with, and
emits one .inc per table family plus a raw-byte fixture.

Structural guarantees, hard-errored at parse/emit time:
  * every table holds exactly the entries its consumer indexes;
  * every value sits in the space its consumer reads it as (a tile offset on a
    tile boundary, a frame below 32 or a flipped frame below $50, a window tile
    inside the 28-tile wallpaper sheet, a gauge cell between empty and full, a
    flag byte that is exactly one of its two values);
  * the name-kind table's special entries name the two name lists their routines
    actually read;
  * every table of the English build is identical to the vanilla cartridge over
    its whole extent.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_battle_gfx_tables.py --source-root PATH
    parse_battle_gfx_tables.py --source-root PATH --check-only
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
import parse_const_enums as pce
import parse_window_data as pwd
from common import ParseError, strip_comment

EN = "en"
JP = "jp"
LANGUAGES = (EN, JP)

_RE_LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
_RE_LOCAL = re.compile(r"^@[0-9A-Za-z_]+:\s*")
_RE_DIRECTIVE = re.compile(r"^\.(byte|word)\s+(.+)$", re.IGNORECASE)
_RE_MACRO_OPEN = re.compile(r"^\.mac(?:ro)?\s+([A-Za-z_][A-Za-z0-9_]*)\s*(.*)$",
                            re.IGNORECASE)
_RE_MACRO_CLOSE = re.compile(r"^\.endmac(?:ro)?\b", re.IGNORECASE)
_RE_CALL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s+(.+)$")
_RE_SCOPED = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)::([A-Za-z_][A-Za-z0-9_]*)$")
_RE_RAM = re.compile(r"^w7e([0-9a-f]{4})$")
_RE_CONSTANT = re.compile(r"^\s*([A-Z_][A-Z0-9_]*)\s*=\s*(\S+)\s*$")
_RE_IF = re.compile(r"^\.if\s+(.+)$", re.IGNORECASE)


def _paths(source_root):
    return {
        "const_inc": os.path.join(source_root, "include", "const.inc"),
        "btlgfx_asm": os.path.join(source_root, "src", "btlgfx", "btlgfx_main.asm"),
        "btlgfx_ram": os.path.join(source_root, "src", "btlgfx", "btlgfx_ram.inc"),
    }


# --- the conditional walker ------------------------------------------------------

def _condition_holds(cond, language, path, lineno):
    """Whether `.if cond` is assembled for `language`, or None when the
    condition is not one this reader decides."""
    cond = cond.strip()
    if cond == "LANG_EN":
        return language == EN
    if cond == "!LANG_EN":
        return language != EN
    return None


def active_lines(path, language):
    """(lineno, code) for every line assembled for `language`. A line inside a
    condition this reader does not decide comes back with code None, so a table
    that reaches one is an error rather than a guess."""
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()
    stack = []   # each frame: True (taking), False (skipping), None (undecided)
    out = []
    for lineno, raw in enumerate(lines, 1):
        code, _ = strip_comment(raw)
        s = code.strip()
        opened = _RE_IF.match(s)
        if opened:
            stack.append(_condition_holds(opened.group(1), language, path, lineno))
            continue
        if s.lower().startswith((".ifdef", ".ifndef")):
            stack.append(None)
            continue
        if s.lower() == ".else":
            if not stack:
                raise ParseError(path, lineno, ".else without .if")
            frame = stack.pop()
            stack.append(None if frame is None else not frame)
            continue
        if s.lower() == ".endif":
            if not stack:
                raise ParseError(path, lineno, ".endif without .if")
            stack.pop()
            continue
        if any(frame is False for frame in stack):
            continue
        if any(frame is None for frame in stack):
            out.append((lineno, None))
            continue
        out.append((lineno, s))
    if stack:
        raise ParseError(path, len(lines), "unclosed .if")
    return out


# --- symbols -----------------------------------------------------------------

class Symbols(object):
    """What the tables' expressions name: const.inc enums, the btlgfx RAM
    constants for one language, and the address-named RAM buffers."""

    def __init__(self, paths, language):
        parsed = common.parse_ca65_constants(paths["const_inc"],
                                             skip_body_enums=pce.SKIP)
        self.enums = {}
        for name in ("BATTLE_CHAR_PAL", "CHAR_GFX", "BATTLE_CMD"):
            enum = parsed.enum(name)
            if enum is None:
                raise ParseError(paths["const_inc"], 0,
                                 "expected enum '{}' not found".format(name))
            self.enums[name] = enum
        self.constants = {}
        for lineno, code in active_lines(paths["btlgfx_ram"], language):
            if code is None:
                continue
            found = _RE_CONSTANT.match(code)
            if found:
                value = common.parse_int_literal(found.group(2))
                if value is not None:
                    self.constants[found.group(1)] = value

    def member_value(self, scope, member, path, lineno):
        enum = self.enums.get(scope)
        if enum is None:
            raise ParseError(path, lineno,
                             "'{}' is not an enum these tables resolve against "
                             "(grammar not covered — escalate, never guess)"
                             .format(scope))
        for m in enum.members:
            if m.name == member:
                return m.value
        raise ParseError(path, lineno, "unknown {}::{}".format(scope, member))

    def members_by_value(self, scope):
        return {m.value: m.name for m in self.enums[scope].members}

    def first_member_named(self, scope):
        """value -> the first enumerator declared with that value."""
        out = {}
        for m in self.enums[scope].members:
            out.setdefault(m.value, m.name)
        return out


class RamRef(object):
    """A RAM buffer plus a byte offset into it, as a table names one."""

    def __init__(self, buffer, offset):
        self.buffer = buffer
        self.offset = offset

    def address(self):
        return int(_RE_RAM.match(self.buffer).group(1), 16) + self.offset


def evaluate(expr, symbols, path, lineno):
    """A table term: a literal, an enum member, a RAM constant, a RAM buffer, or
    a sum of those. A term naming a RAM buffer comes back as a RamRef."""
    total = 0
    ram = None
    for sign, term in _terms(expr, path, lineno):
        literal = common.parse_int_literal(term)
        if literal is not None:
            total += sign * literal
            continue
        scoped = _RE_SCOPED.match(term)
        if scoped:
            total += sign * symbols.member_value(scoped.group(1), scoped.group(2),
                                                 path, lineno)
            continue
        if _RE_RAM.match(term):
            if ram is not None or sign < 0:
                raise ParseError(path, lineno,
                                 "'{}' names more than one RAM buffer".format(expr))
            ram = term
            continue
        if term in symbols.constants:
            total += sign * symbols.constants[term]
            continue
        raise ParseError(path, lineno, "unknown term '{}' (grammar not covered — "
                         "escalate, never guess)".format(term))
    if ram is not None:
        return RamRef(ram, total)
    return total


def _terms(expr, path, lineno):
    tokens = re.split(r"\s*([+-])\s*", expr.strip())
    if not tokens or tokens[0] == "":
        raise ParseError(path, lineno, "empty expression '{}'".format(expr))
    out = [(1, tokens[0])]
    for i in range(1, len(tokens), 2):
        if i + 1 >= len(tokens) or not tokens[i + 1]:
            raise ParseError(path, lineno, "dangling operator in '{}'".format(expr))
        out.append((1 if tokens[i] == "+" else -1, tokens[i + 1]))
    return out


# --- the table reader -------------------------------------------------------------

class Macro(object):
    def __init__(self, name, params, body):
        self.name = name
        self.params = params
        self.body = body   # (lineno, code)


def read_macros(lines, path):
    macros = {}
    current = None
    for lineno, code in lines:
        if code is None:
            continue
        opened = _RE_MACRO_OPEN.match(code)
        if opened:
            params = [p.strip() for p in opened.group(2).split(",") if p.strip()]
            current = Macro(opened.group(1), params, [])
            continue
        if current is not None:
            if _RE_MACRO_CLOSE.match(code):
                macros[current.name] = current
                current = None
            elif code:
                current.body.append((lineno, code))
    return macros


class Item(object):
    """One assembled value: its width in bytes and its value (an int, or a
    RamRef for a word naming a RAM buffer)."""

    def __init__(self, size, value, lineno, text):
        self.size = size
        self.value = value
        self.lineno = lineno
        self.text = text

    def number(self):
        if isinstance(self.value, RamRef):
            return self.value.address() & 0xFFFF
        return self.value


def _directive_items(kind, args, symbols, path, lineno):
    size = 1 if kind.lower() == "byte" else 2
    items = []
    for term in args.split(","):
        value = evaluate(term, symbols, path, lineno)
        number = value.address() & 0xFFFF if isinstance(value, RamRef) else value
        if isinstance(value, RamRef) and size != 2:
            raise ParseError(path, lineno, "a RAM buffer in a .byte")
        low = -0x80 if size == 1 else -0x8000
        high = 0xFF if size == 1 else 0xFFFF
        if not low <= number <= high:
            raise ParseError(path, lineno,
                             "'{}' is outside a {}".format(term.strip(), kind))
        if not isinstance(value, RamRef):
            value &= high
        items.append(Item(size, value, lineno, term.strip()))
    return items


def _line_items(code, macros, symbols, path, lineno):
    found = _RE_DIRECTIVE.match(code)
    if found:
        return _directive_items(found.group(1), found.group(2), symbols, path, lineno)
    call = _RE_CALL.match(code)
    if call and call.group(1) in macros:
        macro = macros[call.group(1)]
        args = [a.strip() for a in call.group(2).split(",")]
        if len(args) != len(macro.params):
            raise ParseError(path, lineno, "{} takes {} arguments, not {}".format(
                macro.name, len(macro.params), len(args)))
        items = []
        for _, body in macro.body:
            expanded = body
            for param, arg in zip(macro.params, args):
                expanded = re.sub(r"\b{}\b".format(re.escape(param)), arg, expanded)
            inner = _RE_DIRECTIVE.match(expanded)
            if not inner:
                raise ParseError(path, lineno,
                                 "macro {} holds '{}', not data".format(macro.name, body))
            items += _directive_items(inner.group(1), inner.group(2), symbols,
                                      path, lineno)
        return items
    raise ParseError(path, lineno, "unexpected line inside a table: '{}' "
                     "(grammar not covered — escalate, never guess)".format(code))


def read_table(lines, macros, label, symbols, path):
    """The values assembled after `label`, up to the next label after data. A
    label directly beneath another before any data is a second name for it."""
    items = []
    inside = False
    for lineno, code in lines:
        if code is None:
            if inside:
                raise ParseError(path, lineno,
                                 "{} reaches a condition this reader does not decide"
                                 .format(label))
            continue
        if not code:
            continue
        found = _RE_LABEL.match(code)
        if found:
            if found.group(1) == label:
                if inside:
                    raise ParseError(path, lineno, "{} defined twice".format(label))
                inside = True
                continue
            if inside and items:
                break
            continue
        if not inside:
            continue
        code = _RE_LOCAL.sub("", code)
        if not code:
            continue
        items += _line_items(code, macros, symbols, path, lineno)
    if not inside:
        raise ParseError(path, 0, "{} not found".format(label))
    return items


def table_bytes(items):
    out = []
    for item in items:
        number = item.number()
        out.append(number & 0xFF)
        if item.size == 2:
            out.append((number >> 8) & 0xFF)
    return out


# --- the tables ------------------------------------------------------------------

# Where each table sits in the English cartridge (FF3 1.1 US).
ADDRESSES = {
    "_c2c745": 0xC2C745,
    "_c2c6a9": 0xC2C6A9,
    "CharPalTbl": 0xC2CE2B,
    "CharRowOffsetTbl": 0xC2CEE3,
    "CharYOffsetTbl": 0xC2CF03,
    "CharXAngleTbl": 0xC2CF17,
    "DmgNumBounceTbl": 0xC2CF93,
    "MenuWindowTbl": 0xC2DD9E,
    "_c2df9e": 0xC2DF9E,
    "CharFlipTbl": 0xC2E43E,
    "CharDirTbl": 0xC2E44E,
    "_c2e49a": 0xC2E49A,
    "MonsterAlignOffset": 0xC11583,
    "_c23fcb": 0xC13FC8,
    "WindowTileTbl": 0xC154FD,
    "WindowBorderTileTbl": 0xC1550D,
    "GaugeTextTbl": 0xC168A9,
    "_c197a1": 0xC1979E,
    "MsgSpeedTbl": 0xC1986F,
    "_c2c689": 0xC2C689,
    # CharGfxPtrs carries no local label; it is the 72 bytes before _c2ce8b's @ce8b.
    "CharGfxPtrs": 0xC2CE43,
}

# CharGfxPtrs is .repeat-generated from the map sprite sheets' places, so it is read by its own
# reader (read_character_sheets) rather than the table reader.
GENERATED_TABLES = ("CharGfxPtrs",)
STATUS_COLOR_ROWS = 4                # _c2c689: one row of four colors per party slot
STATUS_COLORS_PER_ROW = 4
SHEET_POINTERS = 24                  # CharGfxPtrs: the 23 named graphics and the green soldier
MAP_SPRITE_GFX_PATH = "src/gfx/map_sprite_gfx/%s.4bpp"
MAP_SPRITE_GFX_ASM = os.path.join("src", "gfx", "map_sprite_gfx.asm")
MAP_SPRITE_GFX_DIR = os.path.join("src", "gfx", "map_sprite_gfx")
_RE_SHEET_ITEM = re.compile(r'^inc_map_sprite_gfx\s+([A-Z_0-9]+)\s*,\s*"([a-z_0-9]+)"$')
_RE_SHEET_REPEAT = re.compile(r"^\.repeat\s+(\d+)\s*,\s*i$")
_RE_SHEET_ADDR = re.compile(r"^\.faraddr\s+array_item\s+MapSpriteGfx\s*,\s*(i|MAP_SPRITE_GFX::([A-Z_0-9]+))$")

FRAMES = 32
FRAME_TILES = 8
TILE_BYTES = 32
BLANK_TILE = 0xFFFF
POSES = 39
POSE_STEPS = 4
NO_FRAME = 0xFF
MIRRORED_FRAME = 0x30
PALETTE_SLOTS = 24
BATTLE_TYPES = 4
PARTY_SLOTS = 4
PARTY_Y_ROWS = BATTLE_TYPES + 1      # the four battle types, then the Magitek train's
MIRRORED = 0x40                      # CharFlipTbl: the sprite's h-flip attribute bit
HANDS_SWAPPED = 0x01
WAITING_POSES = 41
BOUNCE_FRAMES = 65
GAUGE_STEPS = 32
GAUGE_CELLS = 4
GAUGE_CELL_FULL = 8
MESSAGE_SPEEDS = 8
ALIGNMENTS = 5
NUMERAL_GLYPHS = 21
WALLPAPER_SIZE = 4
BORDER_PARTS = ("top", "bottom", "corners", "left", "right")
FIRST_BATTLE_WINDOW_TILE = 0x20      # the window tiles' place in the battle's menu art
WINDOWS = 15
TEXT_PLACEMENTS = 19
NAME_KINDS = 6
SPECIAL_NAME = 0x80                  # _c197a1: bit 7 routes through _c2bb11
SPECIAL_NAME_INDEX = 0x0F
MAP_COLUMNS = 32
CELL_BYTES = 2

# The tile layer each RAM buffer is the tilemap of.
LAYERS = {
    "w7e8d13": "MENU_WINDOWS",
    "w7e5855": "MENU_TEXT",
    "w7ea97f": "BATTLEFIELD",
}

# The window numbers, by the names DrawMenuWindow's comment block gives them.
WINDOW_NAMES = (
    ("monster name", "MONSTER_NAMES"),
    ("character name/hp/gauge", "PARTY_STATUS"),
    ("command (window mode)", "COMMANDS"),
    ("slot window", "SLOTS"),
    ("item/dialog/etc.", "ITEMS"),
    ("magic/lore mp required", "MP_COST"),
    ("weapon select", "WEAPONS"),
    ("magic/lore select", "SPELLS"),
    ("esper", "ESPERS"),
    ("row/def. (all positions)", "ROW_DEFEND"),
    ("swdtech", "BUSHIDO"),
    ("wide message (top of screen)", "WIDE_MESSAGE"),
    ("narrow message (top of screen)", "NARROW_MESSAGE"),
    ("command (short mode)", "SHORT_COMMANDS"),
    ("command (relm's \"command\" ability)", "CONTROL_COMMANDS"),
)

# The text placements, by the names _c2df9e's comments give them. Two carry no
# name and keep their number.
PLACEMENT_NAMES = (
    ("monster names", "MONSTER_NAMES"),
    ("character names", "CHARACTER_NAMES"),
    ("hp", "HP"),
    ("gauge", "GAUGES"),
    ("row (top)", "ROW_0"),
    ("row", "ROW_1"),
    ("row", "ROW_2"),
    ("row (bottom)", "ROW_3"),
    ("def. (top)", "DEFEND_0"),
    ("def.", "DEFEND_1"),
    ("def.", "DEFEND_2"),
    ("def. (bottom)", "DEFEND_3"),
    ("hp", "MENU_HP"),
    ("gauge", "MENU_GAUGES"),
    ("", "PLACEMENT_14"),
    ("row (short)", "ROW_SHORT"),
    ("def. (short)", "DEFEND_SHORT"),
    ("blank status names", "STATUS_NAMES"),
    ("", "PLACEMENT_18"),
)

# The text commands a name kind may splice its name with, and the name list each
# one's handler reads in the English build (the Japanese build's attack-name
# command reads spell and esper names only).
NAME_COMMANDS = {
    0x0C: ("COMMAND_NAME", "DlgTextCmd_0c", "BattleCmdName"),
    0x0E: ("ITEM_NAME", "DlgTextCmd_0e", "ItemName"),
    0x0F: ("ATTACK_NAME", "DlgTextCmd_0f", "AttackName"),
}

# The two special name lists, by their place in _c2bb18, and the list each
# routine reads.
SPECIAL_NAMES = {
    0: ("esperAttackName", "GenjuAttackName"),
    1: ("bushidoName", "BushidoName"),
}


class Tables(object):
    """Every table, for one language, as read and checked."""

    def __init__(self, language):
        self.language = language
        self.raw = {}          # label -> the table's assembled bytes
        self.items = {}        # label -> the read items


def _expect(path, condition, message):
    if not condition:
        raise ParseError(path, 0, message)


def _numbers(items):
    return [item.number() for item in items]


def _signed(value, bits):
    return value - (1 << bits) if value >= 1 << (bits - 1) else value


def _comment_lines(path):
    with open(path, "r", encoding="utf-8") as fh:
        return fh.readlines()


def window_comment_names(path):
    """The window names DrawMenuWindow's comment block lists, in order."""
    raw = _comment_lines(path)
    names = []
    for index, line in enumerate(raw):
        if line.startswith("DrawMenuWindow:"):
            block = raw[max(0, index - 20):index]
            for text in block:
                found = re.match(r"^;\s+\$([0-9a-f]{2}):\s*(.*?)\s*$", text)
                if found:
                    names.append((int(found.group(1), 16), found.group(2)))
            break
    return names


def placement_comment_names(path):
    """The placement names on _c2df9e's lines, by number."""
    raw = _comment_lines(path)
    names = {}
    inside = False
    for line in raw:
        if line.startswith("_c2df9e:"):
            inside = True
            continue
        if inside:
            if line.startswith("; ----"):
                break
            found = re.search(r";\s*\$([0-9a-f]{2}):\s*(.*?)\s*$", line)
            if found:
                number = int(found.group(1), 16)
                text = found.group(2)
                if number in names and names[number] != text:
                    raise ParseError(path, 0, "placement ${:02X} is named twice".format(number))
                names[number] = text
    return names


def routine_name_lists(lines, label):
    """The `...Name` lists the routine at `label` reads or sizes its copy by, up
    to its return."""
    reads = set()
    inside = False
    for _, code in lines:
        if code is None:
            continue
        if code.startswith(label + ":"):
            inside = True
            continue
        if not inside:
            continue
        reads.update(re.findall(r"\bf:([A-Za-z]+Name)\b", code))
        reads.update(re.findall(r"\b([A-Za-z]+Name)::ITEM_SIZE\b", code))
        if re.search(r"\b(rts|rtl)\b", code):
            break
    return reads


def special_routines(lines, path):
    """The routine labels _c2bb18 lists, in order."""
    items = []
    inside = False
    for _, code in lines:
        if code is None:
            continue
        if code.startswith("_c2bb18:"):
            inside = True
            continue
        if inside:
            body = _RE_LOCAL.sub("", code)
            found = re.match(r"^\.addr\s+(.+)$", body)
            if not found:
                break
            items += [t.strip() for t in found.group(1).split(",")]
    _expect(path, items, "_c2bb18 not found")
    return items


def command_routine(lines, code_byte, path):
    """The handler DlgTextCmdTbl names for `code_byte`."""
    handlers = []
    inside = False
    for _, code in lines:
        if code is None:
            continue
        if code.startswith("DlgTextCmdTbl:"):
            inside = True
            continue
        if inside:
            body = _RE_LOCAL.sub("", code)
            found = re.match(r"^\.addr\s+(.+)$", body)
            if not found:
                break
            handlers += [t.strip() for t in found.group(1).split(",")]
    _expect(path, len(handlers) == 0x20, "DlgTextCmdTbl holds {} handlers, not 32".format(
        len(handlers)))
    return handlers[code_byte]


def read_tables(source_root, language):
    paths = _paths(source_root)
    symbols = Symbols(paths, language)
    path = paths["btlgfx_asm"]
    lines = active_lines(path, language)
    macros = read_macros(lines, path)
    tables = Tables(language)
    for label in ADDRESSES:
        if label in GENERATED_TABLES:
            continue
        items = read_table(lines, macros, label, symbols, path)
        tables.items[label] = items
        tables.raw[label] = table_bytes(items)
    tables.sheets = read_character_sheets(source_root, lines, path)
    tables.raw["CharGfxPtrs"] = sheet_pointer_bytes(tables.sheets)
    tables.gauge_empty = symbols.constants.get("GAUGE_EMPTY_CHAR")
    _expect(paths["btlgfx_ram"], tables.gauge_empty is not None,
            "GAUGE_EMPTY_CHAR is not defined for {}".format(language))
    check(tables, symbols, lines, paths)
    return paths, symbols, tables


def check(tables, symbols, lines, paths):
    path = paths["btlgfx_asm"]
    items = tables.items

    frames = _numbers(items["_c2c745"])
    _expect(path, len(frames) == FRAMES * FRAME_TILES,
            "_c2c745 holds {} tiles, not {} frames of {}".format(len(frames), FRAMES, FRAME_TILES))
    for i, word in enumerate(frames):
        _expect(path, word == BLANK_TILE or word % TILE_BYTES == 0,
                "_c2c745 entry {} (${:04X}) is neither blank nor on a tile".format(i, word))
    for frame in range(1, FRAMES):
        row = frames[frame * FRAME_TILES:(frame + 1) * FRAME_TILES]
        _expect(path, row[0] == BLANK_TILE and row[1] == BLANK_TILE,
                "frame {} draws in its top row, which only the fallen frame does".format(frame))

    poses = _numbers(items["_c2c6a9"])
    _expect(path, len(poses) == POSES * POSE_STEPS,
            "_c2c6a9 holds {} bytes, not {} poses of {}".format(len(poses), POSES, POSE_STEPS))
    for i, value in enumerate(poses):
        plain = value < FRAMES
        mirrored = MIRRORED_FRAME <= value < MIRRORED_FRAME + FRAMES
        none = value == NO_FRAME
        _expect(path, plain or mirrored or none,
                "_c2c6a9 entry {} (${:02X}) is not a frame".format(i, value))
        _expect(path, none == (i < POSE_STEPS),
                "only pose 0 shows no frame (entry {})".format(i))

    palettes = items["CharPalTbl"]
    _expect(path, len(palettes) == PALETTE_SLOTS,
            "CharPalTbl holds {} slots, not {}".format(len(palettes), PALETTE_SLOTS))
    graphics = symbols.members_by_value("CHAR_GFX")
    _expect(path, len(graphics) == PALETTE_SLOTS - 1,
            "CHAR_GFX names {} graphics, not one fewer than the palette slots".format(len(graphics)))
    for slot, item in enumerate(palettes):
        scoped = _RE_SCOPED.match(item.text)
        _expect(path, scoped and scoped.group(1) == "BATTLE_CHAR_PAL",
                "CharPalTbl slot {} is not a BATTLE_CHAR_PAL member".format(slot))
    _expect(path, palettes[-1].text == "BATTLE_CHAR_PAL::GREEN_SOLDIER",
            "the slot past the named graphics is not the green soldier")

    for label, count, size in (("CharRowOffsetTbl", BATTLE_TYPES * PARTY_SLOTS, 2),
                               ("CharXAngleTbl", BATTLE_TYPES * PARTY_SLOTS, 2),
                               ("CharYOffsetTbl", PARTY_Y_ROWS * PARTY_SLOTS, 1),
                               ("CharFlipTbl", BATTLE_TYPES * PARTY_SLOTS, 1),
                               ("CharDirTbl", BATTLE_TYPES * PARTY_SLOTS, 1)):
        _expect(path, len(items[label]) == count and all(i.size == size for i in items[label]),
                "{} holds {} entries, not {}".format(label, len(items[label]), count))
    for value in _numbers(items["CharFlipTbl"]):
        _expect(path, value in (0, MIRRORED), "CharFlipTbl holds ${:02X}".format(value))
    for value in _numbers(items["CharDirTbl"]):
        _expect(path, value in (0, HANDS_SWAPPED), "CharDirTbl holds ${:02X}".format(value))

    waiting = _numbers(items["_c2e49a"])
    _expect(path, len(waiting) == WAITING_POSES,
            "_c2e49a holds {} commands, not {}".format(len(waiting), WAITING_POSES))
    for command, pose in enumerate(waiting):
        _expect(path, pose < POSES, "command {}'s waiting pose {} is not a pose".format(
            command, pose))
    named = {value: name for value, name in symbols.first_member_named("BATTLE_CMD").items()
             if value < WAITING_POSES}
    _expect(path, sorted(named) == list(range(len(named))),
            "the named battle commands are not a run from 0")
    tables.named_commands = named

    bounce = _numbers(items["DmgNumBounceTbl"])
    _expect(path, len(bounce) == BOUNCE_FRAMES,
            "DmgNumBounceTbl holds {} frames, not {}".format(len(bounce), BOUNCE_FRAMES))
    _expect(path, bounce[-1] == 0, "the numeral's last frame is not on the ground")

    gauge = _numbers(items["GaugeTextTbl"])
    _expect(path, len(gauge) == GAUGE_STEPS * GAUGE_CELLS,
            "GaugeTextTbl holds {} cells, not {} steps of {}".format(len(gauge), GAUGE_STEPS,
                                                                      GAUGE_CELLS))
    fills = [value - tables.gauge_empty for value in gauge]
    for i, fill in enumerate(fills):
        _expect(path, 0 <= fill <= GAUGE_CELL_FULL,
                "GaugeTextTbl cell {} is not between empty and full".format(i))
    for step in range(GAUGE_STEPS):
        row = fills[step * GAUGE_CELLS:(step + 1) * GAUGE_CELLS]
        _expect(path, sum(row) == step + 1,
                "gauge step {} fills {} pixels, not {}".format(step, sum(row), step + 1))
        _expect(path, row == sorted(row, reverse=True),
                "gauge step {} does not fill left to right".format(step))
    tables.gauge_fills = fills

    status = items["_c2c689"]
    _expect(path, len(status) == STATUS_COLOR_ROWS * STATUS_COLORS_PER_ROW
            and all(i.size == 2 for i in status),
            "_c2c689 holds {} entries, not {} rows of {} color words".format(
                len(status), STATUS_COLOR_ROWS, STATUS_COLORS_PER_ROW))
    for i, value in enumerate(_numbers(status)):
        _expect(path, value < 0x8000, "_c2c689 color {} sets the bit no color uses".format(i))

    _expect(path, len(items["MsgSpeedTbl"]) == MESSAGE_SPEEDS, "MsgSpeedTbl is not 8 speeds")
    _expect(path, len(items["MonsterAlignOffset"]) == ALIGNMENTS,
            "MonsterAlignOffset is not 5 alignments")
    _expect(path, len(items["_c23fcb"]) == NUMERAL_GLYPHS,
            "_c23fcb holds {} glyphs, not {}".format(len(items["_c23fcb"]), NUMERAL_GLYPHS))

    for label, count in (("WindowTileTbl", WALLPAPER_SIZE * WALLPAPER_SIZE),
                         ("WindowBorderTileTbl", len(BORDER_PARTS) * WALLPAPER_SIZE)):
        values = _numbers(items[label])
        _expect(path, len(values) == count, "{} holds {} tiles, not {}".format(
            label, len(values), count))
        for value in values:
            try:
                window_tile_name(value)
            except ValueError:
                raise ParseError(path, 0, "{} holds ${:02X}, not a window tile".format(label, value))
    for value in _numbers(items["WindowTileTbl"]):
        _expect(path, window_tile_name(value).startswith("WALLPAPER_"),
                "WindowTileTbl names a border tile")

    windows = windows_of(items["MenuWindowTbl"], path)
    _expect(path, len(windows) == WINDOWS, "MenuWindowTbl holds {} windows, not {}".format(
        len(windows), WINDOWS))
    comment_names = window_comment_names(path)
    _expect(path, [n for n, _ in comment_names] == list(range(WINDOWS))
            and [t for _, t in comment_names] == [t for t, _ in WINDOW_NAMES],
            "DrawMenuWindow's window list no longer reads as the names this parser gives")

    placements = placements_of(items["_c2df9e"], path)
    _expect(path, len(placements) == TEXT_PLACEMENTS,
            "_c2df9e holds {} placements, not {}".format(len(placements), TEXT_PLACEMENTS))
    found_names = placement_comment_names(path)
    for number, (text, _) in enumerate(PLACEMENT_NAMES):
        _expect(path, found_names.get(number, "") == text,
                "_c2df9e's placement ${:02X} no longer reads '{}'".format(number, text))

    kinds = _numbers(items["_c197a1"])
    _expect(path, len(kinds) == NAME_KINDS, "_c197a1 holds {} kinds, not {}".format(
        len(kinds), NAME_KINDS))
    routines = special_routines(lines, path)
    for kind, value in enumerate(kinds):
        if value & SPECIAL_NAME:
            index = value & SPECIAL_NAME_INDEX
            _expect(path, value == SPECIAL_NAME | index and index in SPECIAL_NAMES
                    and index < len(routines),
                    "name kind {} (${:02X}) is not one of the special name lists".format(kind, value))
            reads = routine_name_lists(lines, routines[index])
            _expect(path, reads == {SPECIAL_NAMES[index][1]},
                    "{} reads {}, not {}".format(routines[index], sorted(reads),
                                                 SPECIAL_NAMES[index][1]))
        elif value != 0:
            _expect(path, value in NAME_COMMANDS,
                    "name kind {} writes ${:02X}, not a name command".format(kind, value))
            _, handler, reads = NAME_COMMANDS[value]
            _expect(path, command_routine(lines, value, path) == handler,
                    "DlgTextCmdTbl does not send ${:02X} to {}".format(value, handler))
            if tables.language == EN:
                _expect(path, reads in routine_name_lists_through(lines, handler),
                        "{} does not read {}".format(handler, reads))


def routine_name_lists_through(lines, label):
    """The `...Name` lists read between `label` and the next routine label."""
    reads = set()
    inside = False
    for _, code in lines:
        if code is None:
            continue
        if code.startswith(label + ":"):
            inside = True
            continue
        if not inside:
            continue
        if re.match(r"^DlgTextCmd_[0-9a-f]{2}:", code):
            break
        reads.update(re.findall(r"\bf:([A-Za-z]+Name)\b", code))
        reads.update(re.findall(r"\b([A-Za-z]+Name)::ITEM_SIZE\b", code))
    return reads


def window_tile_name(battle_tile):
    return pwd.tile_name(battle_tile - FIRST_BATTLE_WINDOW_TILE + pwd.FIRST_TILE)


def _cell(ref, path, what):
    _expect(path, isinstance(ref, RamRef) and ref.buffer in LAYERS,
            "{} is not in a known tilemap".format(what))
    _expect(path, ref.offset % CELL_BYTES == 0, "{} is not on a cell".format(what))
    cell = ref.offset // CELL_BYTES
    return LAYERS[ref.buffer], cell % MAP_COLUMNS, cell // MAP_COLUMNS


def windows_of(items, path):
    """(layer, column, row, width, height) per window."""
    out = []
    for i in range(0, len(items), 3):
        width, height, where = items[i], items[i + 1], items[i + 2]
        _expect(path, width.size == 1 and height.size == 1 and where.size == 2,
                "MenuWindowTbl row {} is not width, height, buffer".format(i // 3))
        layer, column, row = _cell(where.value, path, "window {}".format(i // 3))
        out.append((layer, column, row, width.value, height.value))
    return out


def placements_of(items, path):
    """(layer, column, row, width in cells, height) per placement."""
    out = []
    for i in range(0, len(items), 4):
        source, destination, width, height = items[i:i + 4]
        _expect(path, isinstance(source.value, RamRef),
                "placement {}'s text is not read from a buffer".format(i // 4))
        layer, column, row = _cell(destination.value, path, "placement {}".format(i // 4))
        _expect(path, width.value % CELL_BYTES == 0,
                "placement {}'s width is not whole cells".format(i // 4))
        out.append((layer, column, row, width.value // CELL_BYTES, height.value))
    return out


# --- the party's sprite sheets ---------------------------------------------------------
#
# CharGfxPtrs points each of the 24 battle graphics slots at a map sprite sheet. The source
# generates it (.repeat over the first 23 sheets, then the soldier's again for the green soldier)
# from the sheets' places in MapSpriteGfx, which are the ripped files laid end to end. The reader
# lays them end to end the same way, requires them to fill the ripped family exactly, and hands
# back the pointer table for the cartridge check.

class CharacterSheet(object):
    """One battle graphics slot's sheet: which item of MapSpriteGfx, and where it lies."""

    def __init__(self, slot, item, name, offset, size):
        self.slot = slot      # 0-23, the CharGfxPtrs entry
        self.item = item      # the MAP_SPRITE_GFX item index
        self.name = name      # the item's enumerator name
        self.offset = offset  # bytes from the start of the family
        self.size = size      # the sheet's own bytes


def read_map_sprite_items(source_root):
    """[(enumerator, file stem)] for every map sprite sheet, in the family's order."""
    path = os.path.join(source_root, MAP_SPRITE_GFX_ASM)
    items = []
    for lineno, code in active_lines(path, EN):
        if code is None:
            raise ParseError(path, lineno, "a condition this reader does not decide")
        found = _RE_SHEET_ITEM.match(code)
        if found:
            items.append((found.group(1), found.group(2)))
    _expect(path, len(items) > 0, "no map sprite sheets found")
    return items


def read_character_sheets(source_root, lines, path):
    """The 24 CharacterSheet entries CharGfxPtrs names."""
    import parse_rom_regions  # the rip-list reader; its own import of this module is at top level

    items = read_map_sprite_items(source_root)
    offsets = []
    sizes = []
    total = 0
    for _, stem in items:
        file_path = os.path.join(source_root, MAP_SPRITE_GFX_DIR, stem + ".4bpp")
        if not os.path.isfile(file_path):
            raise ParseError(file_path, 0, "ripped sheet missing — run `make rip` in original-src")
        size = os.path.getsize(file_path)
        offsets.append(total)
        sizes.append(size)
        total += size
    rows = parse_rom_regions.read_rip_list(source_root, "en",
                                           parse_rom_regions.read_rom_size(source_root))
    family = _rip_row(rows, MAP_SPRITE_GFX_PATH, path)
    _expect(path, total == family.size, "the {} ripped map sprite sheets hold {} bytes, but the "
            "family is {}".format(len(items), total, family.size))

    # The pointer table's source: a .repeat over the first N sheets, then one named sheet.
    body = []
    inside = False
    for lineno, code in lines:
        if code is None or not code:
            continue
        if not inside:
            if code == "CharGfxPtrs:":
                inside = True
            continue
        if _RE_LABEL.match(code):
            break
        body.append((lineno, code))
    _expect(path, inside, "CharGfxPtrs not found")
    names = {name: index for index, (name, _) in enumerate(items)}
    sheets = []
    i = 0
    while i < len(body):
        lineno, code = body[i]
        repeat = _RE_SHEET_REPEAT.match(code)
        if repeat:
            _expect(path, i + 2 < len(body) and body[i + 2][1] == ".endrep",
                    "CharGfxPtrs' .repeat is not one line long")
            addr = _RE_SHEET_ADDR.match(body[i + 1][1])
            _expect(path, addr is not None and addr.group(1) == "i",
                    "CharGfxPtrs' .repeat does not name item i")
            for item in range(int(repeat.group(1))):
                sheets.append(CharacterSheet(len(sheets), item, items[item][0], offsets[item],
                                             sizes[item]))
            i += 3
            continue
        addr = _RE_SHEET_ADDR.match(code)
        if addr is None or addr.group(2) is None:
            raise ParseError(path, lineno, "'{}' is not a CharGfxPtrs entry".format(code))
        _expect(path, addr.group(2) in names, "CharGfxPtrs names an unknown sheet {}".format(
            addr.group(2)))
        item = names[addr.group(2)]
        sheets.append(CharacterSheet(len(sheets), item, items[item][0], offsets[item], sizes[item]))
        i += 1
    _expect(path, len(sheets) == SHEET_POINTERS, "CharGfxPtrs holds {} pointers, not {}".format(
        len(sheets), SHEET_POINTERS))
    for sheet in sheets:
        sheet.family_begin = family.begin
    return sheets


def sheet_pointer_bytes(sheets):
    """CharGfxPtrs as the cartridge holds it: a long address per entry."""
    out = []
    for sheet in sheets:
        address = sheet.family_begin + sheet.offset
        out += [address & 0xFF, (address >> 8) & 0xFF, (address >> 16) & 0xFF]
    return out


def assert_rom(rom, tables, path):
    for label, snes in ADDRESSES.items():
        want = tables.raw[label]
        base = common.hirom_file_offset(snes)
        got = list(rom[base:base + len(want)])
        if got != list(want):
            raise ParseError(path, 0,
                             "ROM MISMATCH: {} at ${:06X} differs from the assembled source"
                             .format(label, snes))


# --- the cartridge regions ----------------------------------------------------------
#
# Three families the battle's picture reads out of the cartridge rather than compiling in: the
# pointer tables that find each battle background's graphics blocks and tilemaps, and the palette
# a dying monster fades through. parse_rom_regions.py places them through these resolvers.

BATTLE_BG_ASM = os.path.join("src", "gfx", "battle_bg.asm")
BG_GFX_PTR_BYTES = 3
BG_TILES_PTR_BYTES = 2
BG_PAL_PATH = "src/gfx/battle_bg_pal/%s.pal"
BG_GFX_PATH = "src/gfx/battle_bg_gfx/%s.4bpp.lz"
BG_TILES_PATH = "src/gfx/battle_bg_tiles/%s.scr.lz"
MAP_GFX_PATH = "src/gfx/map_gfx/%s.4bpp"
DEATH_PAL_BANKS = (0xC10000, 0xC20000)   # the battle graphics program's two banks
BANK_SIZE = 0x10000
LDA_LONG_X = 0xBF

_RE_TOP_LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):(?![:=])")
_RE_RES = re.compile(r"^\.res\s+(\d+)\s*\*\s*(\d+)\s*,\s*0$")
_RE_REPEAT = re.compile(r"^\.repeat\s+\$([0-9a-fA-F]+)\s*-\s*BattleBGTiles::ARRAY_LENGTH$")


class BackgroundPointerLayout(object):
    """What battle_bg.asm states about the two pointer tables."""

    def __init__(self, palette_padding, gfx_count, gfx_padding, tiles_total):
        self.palette_padding = palette_padding  # bytes reserved after the last palette
        self.gfx_count = gfx_count              # graphics blocks the gfx table points at
        self.gfx_padding = gfx_padding          # empty entries after them
        self.tiles_total = tiles_total          # tilemap entries, padding included

    @property
    def gfx_size(self):
        return (self.gfx_count + self.gfx_padding) * BG_GFX_PTR_BYTES

    @property
    def tiles_size(self):
        return self.tiles_total * BG_TILES_PTR_BYTES


def _label_bodies(path):
    """{label: [(lineno, code)]} for every top-level label in the file."""
    with open(path, "r", encoding="utf-8") as fh:
        raw = fh.readlines()
    bodies = {}
    current = None
    for lineno, line in enumerate(raw, 1):
        code, _ = strip_comment(line)
        s = code.strip()
        found = _RE_TOP_LABEL.match(s)
        if found:
            current = found.group(1)
            bodies[current] = []
            continue
        if current is not None and s:
            bodies[current].append((lineno, s))
    return bodies


def read_background_pointer_layout(source_root):
    path = os.path.join(source_root, BATTLE_BG_ASM)
    bodies = _label_bodies(path)
    for label in ("BattleBGPal", "BattleBGGfxPtrs", "BattleBGTilesPtrs", "BattleBGGfx"):
        _expect(path, label in bodies, "{} not found".format(label))

    palette = bodies["BattleBGPal"]
    padding = _RE_RES.match(palette[-1][1])
    _expect(path, padding is not None, "the palettes do not end in a .res run")
    palette_padding = int(padding.group(1)) * int(padding.group(2))

    gfx = [code for _, code in bodies["BattleBGGfxPtrs"]]
    _expect(path, len(gfx) == 2 and gfx[0] == "ptr_tbl_far BattleBGGfx",
            "BattleBGGfxPtrs is not one far pointer table and a .res run")
    gfx_padding = _RE_RES.match(gfx[1])
    _expect(path, gfx_padding is not None and int(gfx_padding.group(2)) == BG_GFX_PTR_BYTES,
            "BattleBGGfxPtrs' padding is not whole far pointers")

    tiles = [code for _, code in bodies["BattleBGTilesPtrs"]]
    _expect(path, len(tiles) == 4 and tiles[0] == "ptr_tbl BattleBGTiles"
            and tiles[2] == ".addr BattleBGTiles" and tiles[3] == ".endrep",
            "BattleBGTilesPtrs is not a pointer table padded with BattleBGTiles")
    repeat = _RE_REPEAT.match(tiles[1])
    _expect(path, repeat is not None, "BattleBGTilesPtrs' padding is not a .repeat to a total")

    count = 0
    for lineno, code in bodies["BattleBGGfx"]:
        if re.match(r"^inc_battle_bg_gfx(_lang)?\s", code) or \
                re.match(r"^BattleBGGfx::_\d+\s*:=\s*array_item MapGfx,", code):
            count += 1
        else:
            raise ParseError(path, lineno, "unexpected line in BattleBGGfx: '{}'".format(code))
    return BackgroundPointerLayout(palette_padding, count, int(gfx_padding.group(1)),
                                   int(repeat.group(1), 16))


def _rip_row(rows, wanted, path):
    found = [row for row in rows if row.path == wanted]
    _expect(path, len(found) == 1, "the rip list holds {} rows for {}".format(len(found), wanted))
    return found[0]


def _word(rom, snes):
    at = common.hirom_file_offset(snes)
    return rom[at] | rom[at + 1] << 8


def place_background_pointers(source_root, rip_rows_by_language, rom):
    """The two background pointer tables, both languages.

    The source lays the tables end to end after the palettes' reserved run and before the first
    tilemap, and both ends are ripped, so the tables are pinned between ripped neighbors. With a
    cartridge, every English entry is checked: the padding empty or pointing at the first tilemap,
    each tilemap pointer inside the tilemaps in ascending order, each graphics pointer inside the
    battle backgrounds' own blocks or the field's map graphics.

    Returns ({"en": Placement, "jp": Placement} for the graphics table, the same for the tilemaps).
    """
    import parse_font_metrics  # the Placement type parse_rom_regions consumes

    path = os.path.join(source_root, BATTLE_BG_ASM)
    layout = read_background_pointer_layout(source_root)
    gfx_places = []
    tiles_places = []
    for language in ("en", "jp"):
        rows = rip_rows_by_language[language]
        palettes = _rip_row(rows, BG_PAL_PATH, path)
        tilemaps = _rip_row(rows, BG_TILES_PATH, path)
        gfx_begin = palettes.end + 1 + layout.palette_padding
        tiles_begin = gfx_begin + layout.gfx_size
        _expect(path, tiles_begin + layout.tiles_size == tilemaps.begin,
                "{}: the pointer tables do not end where the first tilemap begins "
                "(${:06x} vs ${:06x})".format(language, tiles_begin + layout.tiles_size,
                                               tilemaps.begin))
        proved_gfx = proved_tiles = 0
        if language == "en" and rom is not None:
            proved_gfx = _prove_gfx_pointers(rom, rows, layout, gfx_begin, path)
            proved_tiles = _prove_tiles_pointers(rom, layout, tiles_begin, tilemaps, path)
        gfx_places.append(parse_font_metrics.Placement(
            language=language, begin=gfx_begin, size=layout.gfx_size, proved=proved_gfx))
        tiles_places.append(parse_font_metrics.Placement(
            language=language, begin=tiles_begin, size=layout.tiles_size, proved=proved_tiles))
    return gfx_places, tiles_places


def _prove_gfx_pointers(rom, rows, layout, begin, path):
    blocks = _rip_row(rows, BG_GFX_PATH, path)
    map_gfx = _rip_row(rows, MAP_GFX_PATH, path)
    for entry in range(layout.gfx_count + layout.gfx_padding):
        at = begin + entry * BG_GFX_PTR_BYTES
        pointer = _word(rom, at) | rom[common.hirom_file_offset(at) + 2] << 16
        if entry >= layout.gfx_count:
            _expect(path, pointer == 0, "graphics pointer {} past the blocks is not empty".format(
                entry))
            continue
        inside = (blocks.begin <= pointer <= blocks.end) or (map_gfx.begin <= pointer <= map_gfx.end)
        _expect(path, inside, "graphics pointer {} (${:06x}) lands in neither the battle "
                "backgrounds' blocks nor the map graphics".format(entry, pointer))
    return layout.gfx_size


def _prove_tiles_pointers(rom, layout, begin, tilemaps, path):
    bank = tilemaps.begin & 0xFF0000
    first = tilemaps.begin & 0xFFFF
    previous = None
    used = None
    for entry in range(layout.tiles_total):
        pointer = bank | _word(rom, begin + entry * BG_TILES_PTR_BYTES)
        if previous is not None and pointer <= previous:
            used = entry
            break
        _expect(path, tilemaps.begin <= pointer <= tilemaps.end,
                "tilemap pointer {} (${:06x}) is outside the tilemaps".format(entry, pointer))
        previous = pointer
    _expect(path, used is not None and bank | _word(rom, begin) == tilemaps.begin,
            "the tilemap pointers do not start at the first tilemap and then repeat it")
    for entry in range(used, layout.tiles_total):
        _expect(path, _word(rom, begin + entry * BG_TILES_PTR_BYTES) == first,
                "tilemap padding entry {} does not point at the first tilemap".format(entry))
    return layout.tiles_size


def place_monster_death_pal(source_root, rom):
    """The dying monster's palette: English only, read off the cartridge.

    The palette's 32 bytes must occur once across the battle graphics program's banks, and the one
    `lda long,x` naming that address must be the only instruction there that does.
    """
    import parse_font_metrics

    paths = _paths(source_root)
    path = paths["btlgfx_asm"]
    lines = active_lines(path, EN)
    symbols = Symbols(paths, EN)
    palette = table_bytes(read_table(lines, read_macros(lines, path), "MonsterDeathPal",
                                     symbols, path))
    _expect(path, len(palette) == 32, "MonsterDeathPal is {} bytes, not 16 colors".format(
        len(palette)))
    if rom is None:
        raise ParseError(path, 0, "the palette's address is read off the cartridge — set "
                         "FF6_VANILLA_ROM")
    hits = []
    for bank in DEATH_PAL_BANKS:
        base = common.hirom_file_offset(bank)
        data = bytes(rom[base:base + BANK_SIZE])
        start = 0
        while True:
            found = data.find(bytes(palette), start)
            if found < 0:
                break
            hits.append(bank | found)
            start = found + 1
    _expect(path, len(hits) == 1, "MonsterDeathPal's bytes occur {} times in the battle "
            "graphics banks, expected exactly once".format(len(hits)))
    begin = hits[0]
    load = bytes([LDA_LONG_X, begin & 0xFF, (begin >> 8) & 0xFF, (begin >> 16) & 0xFF])
    loads = 0
    for bank in DEATH_PAL_BANKS:
        base = common.hirom_file_offset(bank)
        loads += bytes(rom[base:base + BANK_SIZE]).count(load)
    _expect(path, loads == 1, "{} long loads of ${:06x}, expected MonsterDeathAnim's one".format(
        loads, begin))
    return [parse_font_metrics.Placement(language="en", begin=begin, size=len(palette),
                                         proved=len(palette))]


# --- the decompressor ------------------------------------------------------------
#
# Decompress_ext is the cartridge's own LZ decoder, a routine of 65816 code the game
# places on a machine at runtime and calls (src/assets/cartridge_routines.h). The
# region row that locates it is proved here: the routine's source is assembled
# instruction by instruction and the bytes must occur exactly once in the battle
# graphics program's banks, at the in-bank address its own local label states, with
# every `jsl Decompress_ext` in the source matched by a long call to that address in
# the cartridge. The assembler covers the routine's instruction set and no more; a
# line it does not know stops the parse.

DECOMPRESSOR_LABEL = "Decompress_ext"
JSL = 0x22

# The flag-width directives the macros expand to (include/macros.inc:57-69).
_PSW_A = 0x20

# Instruction encodings by (mnemonic, operand shape). A shape is: imm8 / imm16 / dp
# (direct page) / dpil (direct page indirect long) / dpily (… indexed Y) / long /
# absx (absolute, X) / absy (absolute, Y) / rel (a branch) / none.
_OPCODES = {
    ("phb", "none"): 0x8B, ("phd", "none"): 0x0B, ("phx", "none"): 0xDA,
    ("pld", "none"): 0x2B, ("pha", "none"): 0x48, ("plb", "none"): 0xAB,
    ("inx", "none"): 0xE8, ("iny", "none"): 0xC8, ("xba", "none"): 0xEB,
    ("clc", "none"): 0x18, ("tay", "none"): 0xA8, ("tdc", "none"): 0x7B,
    ("lsr", "none"): 0x4A, ("rtl", "none"): 0x6B,
    ("ldx", "imm16"): 0xA2, ("ldy", "imm16"): 0xA0,
    ("rep", "imm8"): 0xC2, ("sep", "imm8"): 0xE2,
    ("lda", "imm8"): 0xA9, ("and", "imm8"): 0x29, ("adc", "imm8"): 0x69,
    ("ora", "imm8"): 0x09,
    ("lda", "dp"): 0xA5, ("sta", "dp"): 0x85, ("sty", "dp"): 0x84, ("ldy", "dp"): 0xA4,
    ("dec", "dp"): 0xC6, ("lsr", "dp"): 0x46, ("cpy", "dp"): 0xC4,
    ("lda", "dpil"): 0xA7, ("lda", "dpily"): 0xB7,
    ("sta", "long"): 0x8F,
    ("sta", "absx"): 0x9D, ("lda", "absy"): 0xB9,
    ("bne", "rel"): 0xD0, ("bcc", "rel"): 0x90, ("bra", "rel"): 0x80,
}

# The macros the routine uses, as the instructions they expand to.
_ROUTINE_MACROS = {
    "longa": [("rep", "#${:02x}".format(_PSW_A))],
    "shorta": [("sep", "#${:02x}".format(_PSW_A))],
    "clr_a": [("tdc", "")],
    "lsr3": [("lsr", ""), ("lsr", ""), ("lsr", "")],
}

_RE_INSTRUCTION = re.compile(r"^([a-z][a-z0-9_]{2,6})(?:\s+(.*))?$")
_RE_LOCAL_LABEL = re.compile(r"^(@[0-9a-f]+):\s*(.*)$")
_RE_HARDWARE = re.compile(r"^(h[A-Z0-9]+)\s*:=\s*\$([0-9a-fA-F]+)\s*$")


def read_hardware_ports(source_root):
    """The `hNAME := $addr` constants of include/hardware.inc."""
    path = os.path.join(source_root, "include", "hardware.inc")
    ports = {}
    with open(path, "r", encoding="utf-8") as fh:
        for raw in fh:
            code, _ = strip_comment(raw)
            found = _RE_HARDWARE.match(code.strip())
            if found:
                ports[found.group(1)] = int(found.group(2), 16)
    if not ports:
        raise ParseError(path, 0, "no hardware ports found")
    return ports


def _operand_shape(operand, labels, ports, path, lineno):
    """(shape, value) for one operand. A branch target comes back as its label."""
    operand = operand.strip()
    if operand == "":
        return "none", None
    if operand.startswith("#"):
        value = common.parse_int_literal(operand[1:])
        if value is None:
            raise ParseError(path, lineno, "unknown immediate '{}'".format(operand))
        return "imm", value
    found = re.match(r"^\[(\$[0-9a-f]+)\](,y)?$", operand)
    if found:
        return ("dpily" if found.group(2) else "dpil"), common.parse_int_literal(found.group(1))
    found = re.match(r"^f:(h[A-Z0-9]+)$", operand)
    if found:
        if found.group(1) not in ports:
            raise ParseError(path, lineno, "unknown port '{}'".format(found.group(1)))
        return "long", ports[found.group(1)]
    found = re.match(r"^a:(\$[0-9a-f]{4}),x$", operand)
    if found:
        return "absx", common.parse_int_literal(found.group(1))
    found = re.match(r"^(\$[0-9a-f]{4}),y$", operand)
    if found:
        return "absy", common.parse_int_literal(found.group(1))
    if operand.startswith("@"):
        return "rel", operand
    found = re.match(r"^\$([0-9a-f]{2})$", operand)
    if found:
        return "dp", int(found.group(1), 16)
    raise ParseError(path, lineno, "operand '{}' is not a shape the decompressor's "
                     "assembler knows (grammar not covered — escalate, never guess)"
                     .format(operand))


def assemble_decompressor(lines, ports, path):
    """Assemble Decompress_ext from `lines` (active_lines of btlgfx_main.asm).

    Returns (bytes, in_bank_address) where the address is what the routine's first
    local label states. The routine runs with 16-bit index registers throughout and
    moves the accumulator's width with longa / shorta; an immediate under a width the
    source has not said, or any instruction outside the routine's set, is an error.
    """
    body = []        # (lineno, label or None, mnemonic, operand)
    inside = False
    start = None
    for lineno, code in lines:
        if code is None:
            if inside:
                raise ParseError(path, lineno, "{} reaches a condition this reader does not "
                                 "decide".format(DECOMPRESSOR_LABEL))
            continue
        if not code:
            continue
        if not inside:
            if _RE_LABEL.match(code) and _RE_LABEL.match(code).group(1) == DECOMPRESSOR_LABEL:
                inside = True
            continue
        label = None
        found = _RE_LOCAL_LABEL.match(code)
        if found:
            label, code = found.group(1), found.group(2).strip()
            if start is None:
                start = int(label[1:], 16)
        if not code:
            body.append((lineno, label, None, ""))
            continue
        if _RE_LABEL.match(code):
            raise ParseError(path, lineno, "{} runs into another routine before its rtl"
                             .format(DECOMPRESSOR_LABEL))
        instruction = _RE_INSTRUCTION.match(code)
        if not instruction:
            raise ParseError(path, lineno, "'{}' is not an instruction".format(code))
        mnemonic, operand = instruction.group(1), (instruction.group(2) or "").strip()
        if mnemonic in _ROUTINE_MACROS:
            if operand:
                raise ParseError(path, lineno, "{} takes no operand".format(mnemonic))
            expanded = _ROUTINE_MACROS[mnemonic]
            body.append((lineno, label, expanded[0][0], expanded[0][1]))
            for inner in expanded[1:]:
                body.append((lineno, None, inner[0], inner[1]))
        else:
            body.append((lineno, label, mnemonic, operand))
        if mnemonic == "rtl":
            break
    if not inside:
        raise ParseError(path, 0, "{} not found".format(DECOMPRESSOR_LABEL))
    if start is None or body[-1][2] != "rtl":
        raise ParseError(path, body[-1][0] if body else 0,
                         "{} has no local label or does not end in rtl".format(DECOMPRESSOR_LABEL))

    # Pass one: sizes and label offsets. Pass two: bytes.
    offsets = {}
    at = 0
    sized = []
    accumulator_wide = False
    for lineno, label, mnemonic, operand in body:
        if label is not None:
            offsets[label] = at
        if mnemonic is None:
            continue
        shape, value = _operand_shape(operand, offsets, ports, path, lineno)
        if shape == "imm":
            if mnemonic in ("ldx", "ldy"):
                shape = "imm16"
            elif mnemonic in ("rep", "sep"):
                shape = "imm8"
            elif accumulator_wide:
                raise ParseError(path, lineno, "a 16-bit accumulator immediate is not in the "
                                 "decompressor's instruction set")
            else:
                shape = "imm8"
        key = (mnemonic, shape)
        if key not in _OPCODES:
            raise ParseError(path, lineno, "'{} {}' is not in the decompressor's instruction set "
                             "(grammar not covered — escalate, never guess)"
                             .format(mnemonic, operand))
        if mnemonic == "rep" and value & _PSW_A:
            accumulator_wide = True
        if mnemonic == "sep" and value & _PSW_A:
            accumulator_wide = False
        size = {"none": 1, "imm8": 2, "imm16": 3, "dp": 2, "dpil": 2, "dpily": 2, "long": 4,
                "absx": 3, "absy": 3, "rel": 2}[shape]
        sized.append((lineno, key, shape, value, at))
        at += size
    out = []
    for lineno, key, shape, value, offset in sized:
        out.append(_OPCODES[key])
        if shape in ("imm8", "dp", "dpil", "dpily"):
            out.append(value & 0xFF)
        elif shape in ("imm16", "absx", "absy"):
            out += [value & 0xFF, (value >> 8) & 0xFF]
        elif shape == "long":
            out += [value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF]
        elif shape == "rel":
            if value not in offsets:
                raise ParseError(path, lineno, "branch to unknown label {}".format(value))
            distance = offsets[value] - (offset + 2)
            if not -128 <= distance <= 127:
                raise ParseError(path, lineno, "branch to {} is out of range".format(value))
            out.append(distance & 0xFF)
    return bytes(out), start


def count_decompressor_calls(lines):
    """How many times the source calls Decompress_ext with jsl."""
    return sum(1 for _, code in lines
               if code is not None and re.match(r"^jsl\s+" + DECOMPRESSOR_LABEL + r"\b", code))


def place_decompressor(source_root, rom):
    """The decompressor's bytes: English only, assembled from the source and found
    once in the battle graphics program's banks, where its own label puts it."""
    import parse_font_metrics

    paths = _paths(source_root)
    path = paths["btlgfx_asm"]
    lines = active_lines(path, EN)
    routine, in_bank = assemble_decompressor(lines, read_hardware_ports(source_root), path)
    if rom is None:
        raise ParseError(path, 0, "the decompressor's address is read off the cartridge — set "
                         "FF6_VANILLA_ROM")
    hits = []
    for bank in DEATH_PAL_BANKS:
        base = common.hirom_file_offset(bank)
        data = bytes(rom[base:base + BANK_SIZE])
        start = 0
        while True:
            found = data.find(routine, start)
            if found < 0:
                break
            hits.append(bank | found)
            start = found + 1
    _expect(path, len(hits) == 1, "the decompressor's {} assembled bytes occur {} times in the "
            "battle graphics banks, expected exactly once".format(len(routine), len(hits)))
    begin = hits[0]
    _expect(path, (begin & 0xFFFF) == in_bank, "the decompressor is at ${:06x}, but its label "
            "says ${:04x}".format(begin, in_bank))
    call = bytes([JSL, begin & 0xFF, (begin >> 8) & 0xFF, (begin >> 16) & 0xFF])
    calls = 0
    for bank in DEATH_PAL_BANKS:
        base = common.hirom_file_offset(bank)
        calls += bytes(rom[base:base + BANK_SIZE]).count(call)
    # Bank $C2 also holds cutscene code that calls the decompressor, so the banks may hold
    # more calls than the battle graphics source makes, never fewer.
    in_source = count_decompressor_calls(lines)
    _expect(path, in_source > 0 and calls >= in_source, "{} long calls of ${:06x} in the battle "
            "graphics banks, but the battle graphics source makes {}".format(calls, begin,
                                                                             in_source))
    return [parse_font_metrics.Placement(language="en", begin=begin, size=len(routine),
                                         proved=len(routine))]


# --- rendering -----------------------------------------------------------------

_HEADER = (
    "// AUTO-GENERATED by tools/asm_parser/parse_battle_gfx_tables.py\n"
    "// Source: {}\n"
    "// (original-src pinned at 1ea47b5; the English build's table cross-checked\n"
    "// against the vanilla ROM over its whole extent)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_battle_gfx_tables.py \\\n"
    "//       --source-root  original-src\n\n")

_ENUM_HEADER = (
    "// AUTO-GENERATED by tools/asm_parser/parse_battle_gfx_tables.py — DO NOT EDIT.\n"
    "// Source: {}\n"
    "// (original-src pinned at 1ea47b5)\n"
    "//\n")


def _source(label, language=EN):
    snes = ADDRESSES[label]
    where = "ROM ${:02X}/{:04X}".format(snes >> 16, snes & 0xFFFF)
    if language == JP:
        where = "Japanese build"
    return "src/btlgfx/btlgfx_main.asm ({}, {})".format(label, where)


def _note(text):
    return "// {}\n\n".format(text.replace("\n", "\n// "))


def render_frames_inc(tables):
    frames = _numbers(tables.items["_c2c745"])
    out = [_HEADER.format(_source("_c2c745")),
           _note("CharacterFrameEntry rows in frame order, #included inside the\n"
                     "kCharacterFrames array in src/data/battle_gfx_tables.cpp. Each\n"
                     "SheetTile names an 8x8 tile of the character's sprite sheet by\n"
                     "its place in the sheet, or blank. A compile-time assert verifies\n"
                     "frame == position.")]
    for frame in range(FRAMES):
        row = frames[frame * FRAME_TILES:(frame + 1) * FRAME_TILES]
        tiles = ["SheetTile::blank()" if w == BLANK_TILE else "SheetTile::at({})".format(w // TILE_BYTES)
                 for w in row]
        out.append("    {{ .frame = {}, .tiles = {{ {} }} }},\n".format(frame, ", ".join(tiles)))
    return "".join(out)


def _pose_frame(value):
    if value == NO_FRAME:
        return "PoseFrame::none()"
    if value >= MIRRORED_FRAME:
        return "PoseFrame::mirrored({})".format(value - MIRRORED_FRAME)
    return "PoseFrame::frame({})".format(value)


def render_poses_inc(tables):
    poses = _numbers(tables.items["_c2c6a9"])
    out = [_HEADER.format(_source("_c2c6a9")),
           _note("CharacterPoseEntry rows in pose order, #included inside the\n"
                     "kCharacterPoses array in src/data/battle_gfx_tables.cpp. Each step\n"
                     "is a frame, a mirrored frame, or none. A compile-time assert\n"
                     "verifies pose == position.")]
    for pose in range(POSES):
        row = poses[pose * POSE_STEPS:(pose + 1) * POSE_STEPS]
        out.append("    {{ .pose = {}, .steps = {{ {} }} }},\n".format(
            pose, ", ".join(_pose_frame(v) for v in row)))
    return "".join(out)


def render_palettes_inc(tables, symbols):
    graphics = symbols.members_by_value("CHAR_GFX")
    out = [_HEADER.format(_source("CharPalTbl") + "; include/const.inc (CHAR_GFX, BATTLE_CHAR_PAL)"),
           _note("{ CharacterGfxId, BattleCharacterPalette } pairs in graphics order,\n"
                     "#included inside the kCharacterPalettes array in\n"
                     "src/data/battle_gfx_tables.cpp. The last slot, past the named\n"
                     "graphics, is kGreenSoldierGraphics. A compile-time assert verifies\n"
                     "graphics == position.")]
    for slot, item in enumerate(tables.items["CharPalTbl"]):
        palette = _RE_SCOPED.match(item.text).group(2)
        key = ("CharacterGfxId::" + graphics[slot]) if slot in graphics else "kGreenSoldierGraphics"
        out.append("    {{ {}, BattleCharacterPalette::{} }},\n".format(key, palette))
    return "".join(out)


def render_sheets_inc(tables, symbols):
    graphics = symbols.members_by_value("CHAR_GFX")
    out = [_HEADER.format(_source("CharGfxPtrs") + "; src/gfx/map_sprite_gfx.asm (MapSpriteGfx)"),
           _note("{ CharacterGfxId, SheetPlace } pairs in graphics order, #included\n"
                     "inside the kCharacterSheets array in src/data/battle_gfx_tables.cpp:\n"
                     "where each slot's sprite sheet lies in the MAP_SPRITE_GFX family, in\n"
                     "bytes from the family's start, and how long the sheet is. The last\n"
                     "slot, kGreenSoldierGraphics, draws the soldier's sheet. A compile-time\n"
                     "assert verifies graphics == position.")]
    for sheet in tables.sheets:
        key = ("CharacterGfxId::" + graphics[sheet.slot]) if sheet.slot in graphics \
            else "kGreenSoldierGraphics"
        out.append("    {{ {}, SheetPlace{{ .offset = {}, .bytes = {} }} }},\n".format(
            key, sheet.offset, sheet.size))
    return "".join(out)


def render_status_colors_inc(tables):
    colors = _numbers(tables.items["_c2c689"])
    out = [_HEADER.format(_source("_c2c689")),
           _note("StatusColorEntry rows in party-slot order, #included inside the\n"
                     "kStatusColors array in src/data/battle_gfx_tables.cpp: the four colors\n"
                     "that follow a party member's twelve, as the cartridge's color words. A\n"
                     "compile-time assert verifies slot == position.")]
    for slot in range(STATUS_COLOR_ROWS):
        row = colors[slot * STATUS_COLORS_PER_ROW:(slot + 1) * STATUS_COLORS_PER_ROW]
        out.append("    {{ .slot = {}, .colors = {{ {} }} }},\n".format(
            slot, ", ".join("CartridgeColor{{0x{:04X}}}".format(v) for v in row)))
    return "".join(out)


def render_placement_inc(tables):
    rows = [_signed(v, 16) for v in _numbers(tables.items["CharRowOffsetTbl"])]
    ys = _numbers(tables.items["CharYOffsetTbl"])
    angles = _numbers(tables.items["CharXAngleTbl"])
    flips = _numbers(tables.items["CharFlipTbl"])
    hands = _numbers(tables.items["CharDirTbl"])
    out = [_HEADER.format("src/btlgfx/btlgfx_main.asm (CharYOffsetTbl, CharXAngleTbl, "
                          "CharRowOffsetTbl, CharFlipTbl, CharDirTbl)"),
           _note("PartyPlacementEntry rows in layout order (front, back, pincer,\n"
                     "side), #included inside the kPartyPlacements array in\n"
                     "src/data/battle_gfx_tables.cpp: each slot's y, line angle, back-row\n"
                     "angle, and whether it is mirrored and has its hands swapped, gathered\n"
                     "from five tables keyed the same way. A compile-time assert verifies\n"
                     "layout == position.")]
    for layout in range(BATTLE_TYPES):
        slots = []
        for slot in range(PARTY_SLOTS):
            i = layout * PARTY_SLOTS + slot
            slots.append("{{ .y = {}, .angle = {}, .backRowAngle = {}, .mirrored = {}, "
                         ".handsSwapped = {} }}".format(
                             ys[i], angles[i], rows[i], "true" if flips[i] else "false",
                             "true" if hands[i] else "false"))
        out.append("    {{ .layout = {}, .slots = {{{{\n        {},\n    }}}} }},\n".format(
            layout, ",\n        ".join(slots)))
    return "".join(out)


def render_magitek_inc(tables):
    ys = _numbers(tables.items["CharYOffsetTbl"])[BATTLE_TYPES * PARTY_SLOTS:]
    out = [_HEADER.format(_source("CharYOffsetTbl") + ", its last row"),
           _note("{ slot, y } pairs, #included inside the kMagitekTrainParty array in\n"
                     "src/data/battle_gfx_tables.cpp. A compile-time assert verifies\n"
                     "slot == position.")]
    for slot, y in enumerate(ys):
        out.append("    {{ {}, {} }},\n".format(slot, y))
    return "".join(out)


def render_waiting_inc(tables):
    poses = _numbers(tables.items["_c2e49a"])
    out = [_HEADER.format(_source("_c2e49a") + "; include/const.inc (BATTLE_CMD)"),
           _note("{ BattleCommandId, pose } pairs for the named commands, #included\n"
                     "inside the kCommandWaitingPoses array in\n"
                     "src/data/battle_gfx_tables.cpp. A compile-time assert verifies\n"
                     "command == position.")]
    for command in range(len(tables.named_commands)):
        out.append("    {{ BattleCommandId::{}, {} }},\n".format(
            tables.named_commands[command], poses[command]))
    return "".join(out)


def render_unnamed_waiting_inc(tables):
    poses = _numbers(tables.items["_c2e49a"])
    first = len(tables.named_commands)
    out = [_HEADER.format(_source("_c2e49a") + ", past the named commands"),
           _note("{ command, pose } pairs for the commands past the named ones,\n"
                     "#included inside the kUnnamedCommandWaitingPoses array in\n"
                     "src/data/battle_gfx_tables.cpp. A compile-time assert verifies\n"
                     "command == position + the named commands' count.")]
    for command in range(first, WAITING_POSES):
        out.append("    {{ {}, {} }},\n".format(command, poses[command]))
    return "".join(out)


def render_bounce_inc(tables):
    out = [_HEADER.format(_source("DmgNumBounceTbl")),
           _note("{ frame, lift } pairs, #included inside the kDamageNumberBounce array\n"
                     "in src/data/battle_gfx_tables.cpp. A compile-time assert verifies\n"
                     "frame == position.")]
    for frame, lift in enumerate(_numbers(tables.items["DmgNumBounceTbl"])):
        out.append("    {{ {}, {} }},\n".format(frame, lift))
    return "".join(out)


def render_gauge_inc(tables):
    out = [_HEADER.format(_source("GaugeTextTbl")),
           _note("GaugeStepEntry rows in step order, #included inside the\n"
                     "kGaugeStepCells array in src/data/battle_gfx_tables.cpp: how many\n"
                     "pixel columns of each cell are filled. The table stores each cell\n"
                     "as the empty-cell glyph plus its fill; the glyph is per language\n"
                     "(battle_gauge_empty_glyph_data.inc). A compile-time assert verifies\n"
                     "step == position.")]
    fills = tables.gauge_fills
    for step in range(GAUGE_STEPS):
        row = fills[step * GAUGE_CELLS:(step + 1) * GAUGE_CELLS]
        out.append("    {{ .step = {}, .cellFills = {{ {} }} }},\n".format(
            step, ", ".join(str(v) for v in row)))
    return "".join(out)


def render_gauge_glyph_inc(by_language):
    out = [_HEADER.format("src/btlgfx/btlgfx_ram.inc (GAUGE_EMPTY_CHAR, both builds)"),
           _note("{ Language, glyph } pairs in Language order, #included inside the\n"
                     "kGaugeEmptyGlyphs array in src/data/battle_gfx_tables.cpp. A\n"
                     "compile-time assert verifies language == position.")]
    for name, language in (("JP", JP), ("EN", EN)):
        out.append("    {{ Language::{}, 0x{:02X} }},\n".format(name, by_language[language].gauge_empty))
    return "".join(out)


def render_message_speed_inc(tables):
    out = [_HEADER.format(_source("MsgSpeedTbl")),
           _note("{ setting, frames } pairs, #included inside the kMessageSpeedFrames\n"
                     "array in src/data/battle_gfx_tables.cpp. A compile-time assert\n"
                     "verifies setting == position.")]
    for setting, frames in enumerate(_numbers(tables.items["MsgSpeedTbl"])):
        out.append("    {{ {}, {} }},\n".format(setting, frames))
    return "".join(out)


ALIGNMENT_NAMES = ("CEILING", "GROUND", "BURIED", "FLOATING", "FLYING")


def render_align_inc(tables):
    out = [_HEADER.format(_source("MonsterAlignOffset")),
           _note("{ MonsterVerticalAlignment, yOffset } pairs, #included inside the\n"
                     "kMonsterAlignOffsets array in src/data/battle_gfx_tables.cpp. The\n"
                     "CEILING row is never read. A compile-time assert verifies\n"
                     "alignment == position.")]
    for alignment, value in enumerate(_numbers(tables.items["MonsterAlignOffset"])):
        out.append("    {{ MonsterVerticalAlignment::{}, {} }},\n".format(
            ALIGNMENT_NAMES[alignment], _signed(value, 8)))
    return "".join(out)


def render_numeral_glyphs_inc(tables):
    out = [_HEADER.format(_source("_c23fcb", tables.language)),
           _note("{{ slot, glyph }} pairs, #included inside the kNumeralGlyphs{} array in\n"
                     "src/data/battle_gfx_tables.cpp. Each glyph is a small-font code. A\n"
                     "compile-time assert verifies slot == position.".format(
                         "En" if tables.language == EN else "Jp"))]
    for slot, glyph in enumerate(_numbers(tables.items["_c23fcb"])):
        out.append("    {{ {}, 0x{:02X} }},\n".format(slot, glyph))
    return "".join(out)


def _tiles(values):
    return "{{ {} }}".format(", ".join("WindowTile::" + window_tile_name(v) for v in values))


def render_wallpaper_inc(tables):
    values = _numbers(tables.items["WindowTileTbl"])
    out = [_HEADER.format(_source("WindowTileTbl")),
           _note("BattleWindowWallpaperRow rows, #included inside the\n"
                     "kBattleWindowWallpaper array in src/data/battle_gfx_tables.cpp.\n"
                     "A compile-time assert verifies row == position.")]
    for row in range(WALLPAPER_SIZE):
        out.append("    {{ .row = {}, .tiles = {} }},\n".format(
            row, _tiles(values[row * WALLPAPER_SIZE:(row + 1) * WALLPAPER_SIZE])))
    return "".join(out)


def render_border_inc(tables):
    values = _numbers(tables.items["WindowBorderTileTbl"])
    out = [_HEADER.format(_source("WindowBorderTileTbl")),
           _note("The BattleWindowBorder's fields, #included inside the\n"
                     "kBattleWindowBorder initializer in src/data/battle_gfx_tables.cpp.")]
    for part, name in enumerate(BORDER_PARTS):
        out.append("    .{} = {},\n".format(
            name, _tiles(values[part * WALLPAPER_SIZE:(part + 1) * WALLPAPER_SIZE])))
    return "".join(out)


def render_windows_inc(tables):
    path = "src/btlgfx/btlgfx_main.asm"
    windows = windows_of(tables.items["MenuWindowTbl"], path)
    out = [_HEADER.format(_source("MenuWindowTbl", tables.language)),
           _note("BattleWindowEntry rows in window order, #included inside the\n"
                     "kBattleWindows{} array in src/data/battle_gfx_tables.cpp. A\n"
                     "compile-time assert verifies window == position.".format(
                         "En" if tables.language == EN else "Jp"))]
    for window, (layer, column, row, width, height) in enumerate(windows):
        out.append("    {{ .window = BattleWindowId::{}, .layer = BattleTileLayer::{}, .column = {}, "
                   ".row = {}, .width = {}, .height = {} }},\n".format(
                       WINDOW_NAMES[window][1], layer, column, row, width, height))
    return "".join(out)


def render_placements_inc(tables):
    path = "src/btlgfx/btlgfx_main.asm"
    placements = placements_of(tables.items["_c2df9e"], path)
    out = [_HEADER.format(_source("_c2df9e", tables.language)),
           _note("BattleTextPlacementEntry rows in placement order, #included inside\n"
                     "the kBattleTextPlacements{} array in src/data/battle_gfx_tables.cpp.\n"
                     "A compile-time assert verifies placement == position.".format(
                         "En" if tables.language == EN else "Jp"))]
    for number, (layer, column, row, width, height) in enumerate(placements):
        out.append("    {{ .placement = BattleTextPlacementId::{}, .layer = BattleTileLayer::{}, "
                   ".column = {}, .row = {}, .width = {}, .height = {} }},\n".format(
                       PLACEMENT_NAMES[number][1], layer, column, row, width, height))
    return "".join(out)


def _name_kind_text(value):
    if value == 0:
        return "NameKindText::nothing()"
    if value & SPECIAL_NAME:
        return "NameKindText::{}()".format(SPECIAL_NAMES[value & SPECIAL_NAME_INDEX][0])
    return "NameKindText::splicedBy(BattleTextCommand::{})".format(NAME_COMMANDS[value][0])


def render_name_kinds_inc(tables):
    out = [_HEADER.format(_source("_c197a1")),
           _note("{ kind, NameKindText } pairs, #included inside the kNameKinds array in\n"
                     "src/data/battle_gfx_tables.cpp. A compile-time assert verifies\n"
                     "kind == position.")]
    for kind, value in enumerate(_numbers(tables.items["_c197a1"])):
        out.append("    {{ {}, {} }},\n".format(kind, _name_kind_text(value)))
    return "".join(out)


def _render_enum_h(name, source, note, members):
    width = max(len(m) for m in members)
    out = [_ENUM_HEADER.format(source),
           "// " + note.replace("\n", "\n// ") + "\n",
           "#pragma once\n\n#include <cstdint>\n\nnamespace ostinato {\n\n",
           "enum class {} : std::uint8_t {{\n".format(name)]
    for value, member in enumerate(members):
        out.append("    {} = {},\n".format(member.ljust(width), value))
    out.append("};\n\n}  // namespace ostinato\n")
    return "".join(out)


def render_window_id_h():
    return _render_enum_h(
        "BattleWindowId",
        "original-src/src/btlgfx/btlgfx_main.asm (DrawMenuWindow's window numbers)",
        "The fifteen windows a battle draws, by the window number the battle graphics\n"
        "program draws each by: the HUD's monster list and party status, the command\n"
        "menus, the item, spell, esper and Bushido lists, the row and defend choices,\n"
        "and the two message windows at the top of the screen.",
        [name for _, name in WINDOW_NAMES])


def render_placement_id_h():
    return _render_enum_h(
        "BattleTextPlacementId",
        "original-src/src/btlgfx/btlgfx_main.asm (_c2df9e)",
        "The runs of text a battle copies onto its windows: the HUD's monster names,\n"
        "party names, HP and gauges, each party slot's row and defend choices, the\n"
        "menus' HP and gauges, and the status names. Two runs have no name and keep\n"
        "their number.",
        [name for _, name in PLACEMENT_NAMES])


def render_fixture(by_language):
    en = by_language[EN]
    jp = by_language[JP]
    out = [_HEADER.format("src/btlgfx/btlgfx_main.asm, btlgfx_ram.inc (both builds)"),
           "// Test fixture for tests/test_battle_gfx_tables.cpp — every table's raw\n"
           "// bytes, deliberately independent of the typed rows in the generated\n"
           "// tables. Tables the two builds share appear once; the four that differ\n"
           "// appear for each.\n"
           "\n#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           "namespace ostinato::test {\n\n"]
    variant = {label for label in ADDRESSES if en.raw[label] != jp.raw[label]}
    for label in ADDRESSES:
        for language, tables in ((EN, en), (JP, jp)):
            if language == JP and label not in variant:
                continue
            raw = tables.raw[label]
            suffix = ("En" if language == EN else "Jp") if label in variant else ""
            name = "kExpected" + _fixture_name(label) + suffix
            out.append("// {}\n".format(_source(label, language)))
            out.append("inline constexpr std::array<std::uint8_t, {}> {} = {{\n".format(len(raw), name))
            for i in range(0, len(raw), 16):
                out.append("    " + ", ".join("0x{:02X}".format(v) for v in raw[i:i + 16]) + ",\n")
            out.append("};\n\n")
    out.append("// The empty gauge cell's small-font glyph in each build.\n"
               "inline constexpr std::uint8_t kExpectedGaugeEmptyGlyphEn = 0x{:02X};\n"
               "inline constexpr std::uint8_t kExpectedGaugeEmptyGlyphJp = 0x{:02X};\n\n"
               .format(en.gauge_empty, jp.gauge_empty))
    out.append("// Where the map sprite sheets begin, which every CharGfxPtrs pointer counts from.\n"
               "inline constexpr std::uint32_t kExpectedMapSpriteGfxBegin = 0x{:06X};\n\n"
               "}}  // namespace ostinato::test\n".format(en.sheets[0].family_begin))
    return "".join(out)


FIXTURE_NAMES = {
    "_c2c745": "CharacterFrames",
    "_c2c6a9": "CharacterPoses",
    "CharPalTbl": "CharacterPalettes",
    "CharRowOffsetTbl": "BackRowAngles",
    "CharYOffsetTbl": "PartyY",
    "CharXAngleTbl": "PartyAngles",
    "DmgNumBounceTbl": "DamageNumberBounce",
    "MenuWindowTbl": "BattleWindows",
    "_c2df9e": "TextPlacements",
    "CharFlipTbl": "PartyMirrored",
    "CharDirTbl": "PartyHandsSwapped",
    "_c2e49a": "CommandWaitingPoses",
    "MonsterAlignOffset": "MonsterAlignOffsets",
    "_c23fcb": "NumeralGlyphs",
    "WindowTileTbl": "WindowWallpaper",
    "WindowBorderTileTbl": "WindowBorder",
    "GaugeTextTbl": "GaugeSteps",
    "_c197a1": "NameKinds",
    "MsgSpeedTbl": "MessageSpeeds",
    "_c2c689": "StatusColors",
    "CharGfxPtrs": "CharacterSheetPointers",
}


def _fixture_name(label):
    return FIXTURE_NAMES[label]


# --- driver --------------------------------------------------------------------

def run(source_root, outputs, check_only=False):
    by_language = {}
    for language in LANGUAGES:
        paths, symbols, tables = read_tables(source_root, language)
        by_language[language] = tables
        if language == EN:
            en_symbols = symbols
    assert_rom(common.load_vanilla_rom(source_root), by_language[EN], paths["btlgfx_asm"])
    variant = sorted(label for label in ADDRESSES
                     if by_language[EN].raw[label] != by_language[JP].raw[label])
    _expect(paths["btlgfx_asm"], variant == sorted(["GaugeTextTbl", "MenuWindowTbl", "_c23fcb",
                                                    "_c2df9e"]),
            "the tables the two builds differ in are {}".format(variant))
    _expect(paths["btlgfx_asm"], by_language[EN].gauge_fills == by_language[JP].gauge_fills,
            "the two builds' gauges fill differently")
    if check_only:
        print("OK: {} tables, {} bytes; {} differ by language; the English build "
              "cartridge-identical.".format(len(ADDRESSES),
                                            sum(len(v) for v in by_language[EN].raw.values()),
                                            len(variant)))
        return 0
    en = by_language[EN]
    jp = by_language[JP]
    emitted = {
        "frames": render_frames_inc(en),
        "poses": render_poses_inc(en),
        "palettes": render_palettes_inc(en, en_symbols),
        "sheets": render_sheets_inc(en, en_symbols),
        "status_colors": render_status_colors_inc(en),
        "placement": render_placement_inc(en),
        "magitek": render_magitek_inc(en),
        "waiting": render_waiting_inc(en),
        "unnamed_waiting": render_unnamed_waiting_inc(en),
        "bounce": render_bounce_inc(en),
        "gauge": render_gauge_inc(en),
        "gauge_glyph": render_gauge_glyph_inc(by_language),
        "message_speed": render_message_speed_inc(en),
        "align": render_align_inc(en),
        "numerals_en": render_numeral_glyphs_inc(en),
        "numerals_jp": render_numeral_glyphs_inc(jp),
        "wallpaper": render_wallpaper_inc(en),
        "border": render_border_inc(en),
        "windows_en": render_windows_inc(en),
        "windows_jp": render_windows_inc(jp),
        "placements_en": render_placements_inc(en),
        "placements_jp": render_placements_inc(jp),
        "name_kinds": render_name_kinds_inc(en),
        "window_id": render_window_id_h(),
        "placement_id": render_placement_id_h(),
        "fixture": render_fixture(by_language),
    }
    for key, text in emitted.items():
        _write(outputs[key], text)
        print("Emitted -> {}".format(outputs[key]))
    return 0


def _write(path, text):
    directory = os.path.dirname(path)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


OUTPUTS = {
    "frames": "src/data/generated/battle_character_frames_data.inc",
    "poses": "src/data/generated/battle_character_poses_data.inc",
    "palettes": "src/data/generated/battle_character_palettes_data.inc",
    "sheets": "src/data/generated/battle_character_sheets_data.inc",
    "status_colors": "src/data/generated/battle_status_colors_data.inc",
    "placement": "src/data/generated/battle_party_placement_data.inc",
    "magitek": "src/data/generated/battle_magitek_train_party_data.inc",
    "waiting": "src/data/generated/battle_command_waiting_poses_data.inc",
    "unnamed_waiting": "src/data/generated/battle_unnamed_command_waiting_poses_data.inc",
    "bounce": "src/data/generated/battle_damage_number_bounce_data.inc",
    "gauge": "src/data/generated/battle_gauge_steps_data.inc",
    "gauge_glyph": "src/data/generated/battle_gauge_empty_glyph_data.inc",
    "message_speed": "src/data/generated/battle_message_speed_data.inc",
    "align": "src/data/generated/battle_monster_align_offset_data.inc",
    "numerals_en": "src/data/generated/battle_numeral_glyphs_en_data.inc",
    "numerals_jp": "src/data/generated/battle_numeral_glyphs_jp_data.inc",
    "wallpaper": "src/data/generated/battle_window_wallpaper_data.inc",
    "border": "src/data/generated/battle_window_border_data.inc",
    "windows_en": "src/data/generated/battle_windows_en_data.inc",
    "windows_jp": "src/data/generated/battle_windows_jp_data.inc",
    "placements_en": "src/data/generated/battle_text_placements_en_data.inc",
    "placements_jp": "src/data/generated/battle_text_placements_jp_data.inc",
    "name_kinds": "src/data/generated/battle_name_kinds_data.inc",
    "window_id": "include/ostinato/battle_window_id.h",
    "placement_id": "include/ostinato/battle_text_placement_id.h",
    "fixture": "tests/fixtures/battle_gfx_tables_expected.h",
}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root", default="original-src")
    ap.add_argument("--check-only", action="store_true")
    args = ap.parse_args(argv)
    try:
        return run(args.source_root, dict(OUTPUTS), check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
