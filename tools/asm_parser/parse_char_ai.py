#!/usr/bin/env python3
"""Emit the character-A.I. battle setup table from original-src char_ai.asm.

Port-time tooling (NOT a build/CI dependency): reads the `CharAI` table in
src/btlgfx/char_ai.asm and emits:

  * include/ostinato/char_ai_id.h              — CHAR_AI
  * src/data/generated/char_ai_data.inc        — one designated-initializer
    CharAiSetupEntry row per record (24), keyed by the CharAiId enumerator its
    position names.
  * tests/fixtures/char_ai_expected.h          — the same 24 records as raw
    24-byte rows (the ground-truth byte contract).

A record is four header bytes followed by four five-byte character slots
(char_ai.asm:14-31). The table's expressions are richer than a plain symbol:
terms are OR'd together, and ca65's low-byte and complement operators appear —
`<~0` is how the corpus writes "every monster is a valid target".

Structural guarantees, hard-errored at parse/emit time:
  * every record emits exactly 24 bytes, each in 0..255;
  * the record count equals the CHAR_AI index space (24), and every record
    index has a CHAR_AI name;
  * an empty character slot is exactly `FF 00 00 FF FF` — a slot whose first
    byte is the $FF sentinel but whose remaining bytes differ is a grammar
    deviation and stops the run;
  * the assembled 576 bytes are identical to the vanilla cartridge at
    $D0/FD00 over the table's whole extent.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_char_ai.py --source-root PATH
    parse_char_ai.py --source-root PATH --check-only
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
import parse_const_enums as pce
from common import ParseError, strip_comment

# --- the table -----------------------------------------------------------------

# char_ai.asm:42 — the table's own address comment.
CHAR_AI_SNES = 0xD0FD00
SLOT_BYTES = 5
SLOT_COUNT = 4
HEADER_BYTES = 4
RECORD_BYTES = HEADER_BYTES + SLOT_COUNT * SLOT_BYTES   # 24
TABLE_LABEL = "CharAI"

# An unused slot is one whose first byte is the $FF sentinel. The corpus writes
# the four bytes after it two different ways — all sentinel, or with the
# graphics and script bytes zeroed — and the loader reads neither, so both are
# carried exactly as written rather than normalized to one form.
EMPTY_SLOT_SENTINEL = (0xFF, 0xFF, 0xFF, 0xFF, 0xFF)
EMPTY_SLOT_ZEROED = (0xFF, 0x00, 0x00, 0xFF, 0xFF)
EMPTY_SLOTS = (EMPTY_SLOT_SENTINEL, EMPTY_SLOT_ZEROED)

# char_ai.inc:10-14 — two flag sets that share bit values and mean different
# things, so they never share a type.
RECORD_FLAG_HIDE_NAMES = 0x01
RECORD_FLAG_HIDE_PARTY = 0x80
SLOT_FLAG_ENEMY = 0x40
SLOT_FLAG_NOT_IN_PARTY = 0x80
SLOT_CHARACTER_MASK = 0x3F

# char_ai.asm:28 — "$ff to use default for character properties".
GRAPHICS_FROM_CHARACTER = 0xFF

_RE_LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
_RE_BYTE = re.compile(r"^\.byte\s+(.+)$")
_RE_SCOPED = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)::([A-Za-z_][A-Za-z0-9_]*)$")

# Where each enum the table references is defined.
_ENUM_SOURCES = (
    (os.path.join("include", "btlgfx", "char_ai.inc"), ("CHAR_AI",)),
    (os.path.join("include", "gfx", "battle_bg.inc"), ("BATTLE_BG",)),
    (os.path.join("include", "sound", "song_script.inc"), ("SONG",)),
    (os.path.join("include", "const.inc"),
     ("CHAR_PROP", "CHAR_GFX", "MONSTER")),
)


class Symbols(object):
    """Every enum and bare constant the table's expressions resolve against."""

    def __init__(self, source_root):
        self.enums = {}
        self.globals = {}
        for rel, wanted in _ENUM_SOURCES:
            path = os.path.join(source_root, rel)
            skip = pce.SKIP if rel.endswith("const.inc") else None
            parsed = common.parse_ca65_constants(path, skip_body_enums=skip)
            for name in wanted:
                enum = parsed.enum(name)
                if enum is None:
                    raise ParseError(path, 0,
                                     "expected enum '{}' not found".format(name))
                self.enums[name] = enum
            for key, value in parsed.globals.items():
                self.globals.setdefault(key, value)

    def enum_value(self, scope, member, path, lineno):
        enum = self.enums.get(scope)
        if enum is None:
            raise ParseError(path, lineno,
                             "'{}' is not an enum this table resolves against "
                             "(grammar not covered — escalate, never guess)"
                             .format(scope))
        value = enum.value_of(member)
        if value is None:
            raise ParseError(path, lineno, "unknown {}::{}".format(scope, member))
        return value

    def member_named(self, scope, value):
        """The member of `scope` holding `value`, or None."""
        enum = self.enums.get(scope)
        if enum is None:
            return None
        for member in enum.members:
            if member.value == value:
                return member.name
        return None

    def names_by_index(self, scope):
        return {m.value: m.name for m in self.enums[scope].members}


def evaluate(expr, symbols, path, lineno):
    """One `.byte` term: OR'd operands, each optionally `<`, `>` or `~`-prefixed."""
    total = 0
    for piece in expr.split("|"):
        total |= _evaluate_term(piece.strip(), symbols, path, lineno)
    return total


def _evaluate_term(text, symbols, path, lineno):
    if not text:
        raise ParseError(path, lineno, "empty term in expression")
    ops = []
    while text and text[0] in "<>~":
        ops.append(text[0])
        text = text[1:].strip()
    if not text:
        raise ParseError(path, lineno, "expression has operators but no operand")

    literal = common.parse_int_literal(text)
    if literal is not None:
        value = literal
    else:
        scoped = _RE_SCOPED.match(text)
        if scoped:
            value = symbols.enum_value(scoped.group(1), scoped.group(2),
                                       path, lineno)
        elif text in symbols.globals:
            value = symbols.globals[text]
        else:
            raise ParseError(path, lineno,
                             "'{}' is neither a literal, a known constant nor a "
                             "scoped enum reference (grammar not covered — "
                             "escalate, never guess)".format(text))

    for op in reversed(ops):
        if op == "~":
            value = ~value & 0xFFFFFFFF
        elif op == "<":
            value &= 0xFF
        else:                                   # ">"
            value = (value >> 8) & 0xFF
    if not (0 <= value <= 0xFF):
        raise ParseError(path, lineno,
                         "term '{}' resolves to {}, outside a byte"
                         .format(text, value))
    return value


class Slot(object):
    """One five-byte character slot."""

    def __init__(self, raw):
        self.bytes = list(raw)

    @property
    def empty(self):
        return tuple(self.bytes) in EMPTY_SLOTS

    @property
    def builder(self):
        """Which named builder reproduces this unused slot's exact bytes."""
        return ("unused" if tuple(self.bytes) == EMPTY_SLOT_SENTINEL
                else "unusedZeroed")

    @property
    def character_bits(self):
        return self.bytes[0]


class Record(object):
    """One CharAI battle setup: four header bytes plus four slots."""

    def __init__(self, index, raw):
        self.index = index
        self.name = None
        self.bytes = list(raw)
        self.flags = raw[0]
        self.background = raw[1]
        self.targets = raw[2]
        self.song = raw[3]
        self.slots = [Slot(raw[HEADER_BYTES + i * SLOT_BYTES:
                               HEADER_BYTES + (i + 1) * SLOT_BYTES])
                      for i in range(SLOT_COUNT)]


def parse_char_ai(asm_path, symbols):
    """Walk CharAI, returning one Record per 24 assembled bytes."""
    with open(asm_path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    values = []
    in_table = False
    macro_depth = 0
    for idx, raw in enumerate(lines, 1):
        code, _comment = strip_comment(raw)
        if not code:
            continue
        s = code.strip()
        low = s.lower()
        if low.startswith(".endmac"):
            macro_depth = max(0, macro_depth - 1)
            continue
        if low.startswith(".mac"):
            macro_depth += 1
            continue
        if macro_depth > 0:
            continue

        label = _RE_LABEL.match(s)
        if label:
            if label.group(1) == TABLE_LABEL:
                in_table = True
            elif in_table:
                break
            continue
        if not in_table:
            continue

        byte_line = _RE_BYTE.match(s)
        if byte_line:
            for term in byte_line.group(1).split(","):
                term = term.strip()
                if term:
                    values.append(evaluate(term, symbols, asm_path, idx))
            continue
        if s.startswith("."):
            if low.startswith((".segment", ".include", ".export", ".global",
                               ".import", ".list", ".setcpu", ".p816")):
                continue
            raise ParseError(asm_path, idx,
                             "unexpected directive '{}' inside {} (grammar not "
                             "covered — escalate, never guess)"
                             .format(s, TABLE_LABEL))

    if not values:
        raise ParseError(asm_path, 0, "{} not found or empty".format(TABLE_LABEL))
    if len(values) % RECORD_BYTES:
        raise ParseError(asm_path, 0,
                         "{} assembles {} bytes, not a whole number of {}-byte "
                         "records".format(TABLE_LABEL, len(values), RECORD_BYTES))

    records = [Record(i // RECORD_BYTES, values[i:i + RECORD_BYTES])
               for i in range(0, len(values), RECORD_BYTES)]
    _verify(records, symbols, asm_path)
    return records


def _verify(records, symbols, path):
    names = symbols.names_by_index("CHAR_AI")
    if len(records) != len(names):
        raise ParseError(path, 0,
                         "{} produced {} records; the CHAR_AI index space is {}"
                         .format(TABLE_LABEL, len(records), len(names)))
    for record in records:
        if record.index not in names:
            raise ParseError(path, 0,
                             "record index {} has no CHAR_AI name"
                             .format(record.index))
        record.name = names[record.index]
        for position, slot in enumerate(record.slots):
            if slot.bytes[0] == 0xFF and not slot.empty:
                raise ParseError(
                    path, 0,
                    "record {} ({}) slot {} opens with the $FF sentinel but "
                    "reads {} — the corpus writes an unused slot as {} or {}"
                    .format(record.index, record.name, position,
                            " ".join("{:02X}".format(b) for b in slot.bytes),
                            " ".join("{:02X}".format(b) for b in EMPTY_SLOT_SENTINEL),
                            " ".join("{:02X}".format(b) for b in EMPTY_SLOT_ZEROED)))


def assert_rom(rom, records, path):
    """The assembled table must match the cartridge over its whole extent."""
    base = common.hirom_file_offset(CHAR_AI_SNES)
    want = [b for record in records for b in record.bytes]
    got = list(rom[base:base + len(want)])
    if len(got) != len(want):
        raise ParseError(path, 0,
                         "ROM MISMATCH: cartridge holds {} bytes at ${:06X}, "
                         "source assembles {}".format(len(got), CHAR_AI_SNES,
                                                      len(want)))
    for i, (g, w) in enumerate(zip(got, want)):
        if g != w:
            record, offset = divmod(i, RECORD_BYTES)
            raise ParseError(path, 0,
                             "ROM MISMATCH at record {} ({}) byte {}: cartridge "
                             "${:02X} != source ${:02X}"
                             .format(record, records[record].name, offset, g, w))


# --- rendering -----------------------------------------------------------------

_BANNER = ("// AUTO-GENERATED by tools/asm_parser/parse_char_ai.py — "
           "DO NOT EDIT.\n")

_REGEN = ("// DO NOT EDIT BY HAND — regenerate via:\n"
          "//   python3 tools/asm_parser/parse_char_ai.py \\\n"
          "//       --source-root  original-src\n")

_INC_HEADER = (
    "// AUTO-GENERATED by tools/asm_parser/parse_char_ai.py\n"
    "// Source: src/btlgfx/char_ai.asm (CharAI, 24 records x 24 bytes,\n"
    "//         ROM $D0/FD00)\n"
    "// Source: include/btlgfx/char_ai.inc (CHAR_AI + the two flag sets)\n"
    "// Source: include/const.inc, include/gfx/battle_bg.inc,\n"
    "//         include/sound/song_script.inc (CHAR_PROP / CHAR_GFX /\n"
    "//         MONSTER / BATTLE_BG / SONG values)\n"
    "// (original-src pinned at 1ea47b5)\n"
    + _REGEN + "\n")


def render_id_h(symbols):
    members = [(m.name, m.value) for m in symbols.enums["CHAR_AI"].members]
    name_w = max(len(n) for n, _ in members)
    out = [_BANNER,
           "// Source: original-src/include/btlgfx/char_ai.inc (ca65 .enum "
           "CHAR_AI)\n"
           "// (original-src pinned at 1ea47b5)\n"
           "//\n"
           "// Which scripted battle setup a battle runs under. A setup places\n"
           "// its own cast in the character slots and can override the\n"
           "// background and music; NONE is the ordinary case, where the\n"
           "// player's party fights.\n"
           "#pragma once\n\n#include <cstdint>\n\nnamespace ostinato {\n\n",
           "enum class CharAiId : std::uint8_t {\n"]
    for name, value in members:
        out.append("    {}{} = 0x{:02X},\n".format(
            name, " " * (name_w - len(name)), value))
    out.append("};\n\n}  // namespace ostinato\n")
    return "".join(out)


def _slot_literal(slot, symbols):
    if slot.empty:
        return "CharAiSlot::{}()".format(slot.builder)
    bits = slot.character_bits
    character = symbols.member_named("CHAR_PROP", bits & SLOT_CHARACTER_MASK)
    if character is None:
        raise ParseError("char_ai.asm", 0,
                         "slot character {} has no CHAR_PROP name"
                         .format(bits & SLOT_CHARACTER_MASK))
    if slot.bytes[1] == GRAPHICS_FROM_CHARACTER:
        graphics_text = "CharAiSlotGraphics::fromCharacter()"
    else:
        graphics = symbols.member_named("CHAR_GFX", slot.bytes[1])
        if graphics is None:
            raise ParseError("char_ai.asm", 0,
                             "slot graphics {} has no CHAR_GFX name and is not "
                             "the ${:02X} use-the-character's-own sentinel"
                             .format(slot.bytes[1], GRAPHICS_FROM_CHARACTER))
        graphics_text = "CharAiSlotGraphics::of(CharacterGfxId::{})".format(
            graphics)
    return (
        "CharAiSlot{{\n"
        "                .character = CharAiSlotCharacter::{}(\n"
        "                    CharacterPropId::{}),\n"
        "                .graphics = {},\n"
        "                .aiScript = {},\n"
        "                .x = {}, .y = {},\n"
        "            }}".format(
            _slot_builder(bits), character, graphics_text,
            slot.bytes[2], slot.bytes[3], slot.bytes[4]))


def _slot_builder(bits):
    enemy = bool(bits & SLOT_FLAG_ENEMY)
    absent = bool(bits & SLOT_FLAG_NOT_IN_PARTY)
    if enemy and absent:
        return "absentEnemy"
    if enemy:
        return "enemy"
    if absent:
        return "notInParty"
    return "ally"


def _flags_literal(value):
    parts = []
    if value & RECORD_FLAG_HIDE_NAMES:
        parts.append("hideNames")
    if value & RECORD_FLAG_HIDE_PARTY:
        parts.append("hideParty")
    if not parts:
        return "CharAiFlags::none()"
    return "CharAiFlags::" + "().".join(parts) + "()" if len(parts) == 1 \
        else "CharAiFlags::hideNames().andHideParty()"


def render_inc(records, symbols):
    out = [_INC_HEADER,
           "// CharAiSetupEntry rows in CHAR_AI record-index order ($00..$17),\n"
           "// one designated-initializer row per record, #included inside the\n"
           "// kCharAiSetups array in src/data/char_ai.cpp. Each row's identity\n"
           "// is its .id field — the CharAiId enumerator\n"
           "// (include/ostinato/char_ai_id.h) — and a compile-time assert\n"
           "// verifies id == position. The packed .record stays byte-identical\n"
           "// to the 24 ROM bytes.\n\n"]
    for record in records:
        background = symbols.member_named("BATTLE_BG", record.background)
        song = symbols.member_named("SONG", record.song)
        out.append("    CharAiSetupEntry{{  // [${:02X}]\n".format(record.index))
        out.append("        .id = CharAiId::{},\n".format(record.name))
        out.append("        .record = CharAiSetup{\n")
        out.append("            .flags = {},\n".format(_flags_literal(record.flags)))
        out.append("            .background = BattleBackgroundId::{},\n"
                   .format(background))
        out.append("            .validMonsterTargets = BattleSlotMask{{0x{:02X}}},\n"
                   .format(record.targets))
        out.append("            .song = SongId::{},\n".format(song))
        out.append("            .slots = {\n")
        for slot in record.slots:
            out.append("                {},\n".format(
                _slot_literal(slot, symbols).replace("\n", "\n    ")))
        out.append("            },\n        },\n    },\n")
    return "".join(out)


_FIXTURE_STRUCT = (
    "// One raw 24-byte CharAI record; field names and order mirror\n"
    "// char_ai.asm:14-31. Values are the exact ROM bytes with every upstream\n"
    "// symbol resolved and the flag bits folded in — deliberately independent\n"
    "// of the symbol rows in char_ai_data.inc, so symbol/value drift in either\n"
    "// artifact fails the full-corpus byte-equivalence test.\n"
    "struct ExpectedCharAiSlot {\n"
    "    std::uint8_t character;  // CHAR_PROP | enemy bit 6 | not-in-party bit 7\n"
    "    std::uint8_t graphics;   // CHAR_GFX ($FF = default for the character)\n"
    "    std::uint8_t aiScript;   // monster a.i. script, low byte\n"
    "    std::uint8_t x, y;       // position, doubled by the consumer\n"
    "};\n"
    "static_assert(sizeof(ExpectedCharAiSlot) == 5,\n"
    "              \"fixture slot must stay identical to a ROM slot\");\n"
    "\n"
    "struct ExpectedCharAiRecord {\n"
    "    std::uint8_t flags;      // hide names bit 0 | hide party bit 7\n"
    "    std::uint8_t background; // BATTLE_BG ($FF = keep the default)\n"
    "    std::uint8_t targets;    // valid monster target slots\n"
    "    std::uint8_t song;       // SONG ($FF = the default battle song)\n"
    "    ExpectedCharAiSlot slots[4];\n"
    "};\n"
    "static_assert(sizeof(ExpectedCharAiRecord) == 24,\n"
    "              \"fixture record must stay identical to a ROM CharAI record\");\n"
    "\n"
    "// One fixture entry: the record's identity as a typed field (raw decimal\n"
    "// index — the fixture stays independent of the port's CharAiId header)\n"
    "// alongside the raw record bytes.\n"
    "struct ExpectedCharAiEntry {\n"
    "    std::uint8_t id;\n"
    "    ExpectedCharAiRecord record;\n"
    "};\n"
)


def render_fixture(records):
    out = [_INC_HEADER,
           "// Test fixture for tests/test_char_ai.cpp — the ground-truth record\n"
           "// bytes. The full-corpus test asserts, per entry: fixture id ==\n"
           "// position, table id enumerator == position, and a 24-byte memcmp\n"
           "// of the packed record against\n"
           "// src/data/generated/char_ai_data.inc's row.\n"
           "\n#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           "namespace ostinato::test {\n\n",
           _FIXTURE_STRUCT,
           "\ninline constexpr std::array<ExpectedCharAiEntry, {}>\n"
           "    kExpectedCharAiEntries = {{{{  // ROM CharAI\n"
           .format(len(records))]
    for record in records:
        out.append("    {{ .id = {:>2},  // ${:02X} {}\n".format(
            record.index, record.index, record.name))
        out.append("      .record = {{ .flags = 0x{:02X}, .background = 0x{:02X}, "
                   ".targets = 0x{:02X}, .song = 0x{:02X},\n".format(
                       record.flags, record.background, record.targets,
                       record.song))
        out.append("                  .slots = {\n")
        for slot in record.slots:
            out.append("                      {{ .character = 0x{:02X}, "
                       ".graphics = 0x{:02X}, .aiScript = 0x{:02X}, "
                       ".x = {:>3}, .y = {:>3} }},\n".format(
                           slot.bytes[0], slot.bytes[1], slot.bytes[2],
                           slot.bytes[3], slot.bytes[4]))
        out.append("                  } } },\n")
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver --------------------------------------------------------------------

def run(source_root, outputs, check_only=False):
    symbols = Symbols(source_root)
    asm_path = os.path.join(source_root, "src", "btlgfx", "char_ai.asm")
    records = parse_char_ai(asm_path, symbols)
    assert_rom(common.load_vanilla_rom(source_root), records, asm_path)

    populated = sum(1 for r in records for s in r.slots if not s.empty)
    if check_only:
        print("OK: {} records x {} bytes, cartridge-identical at ${:06X}; "
              "{} populated character slots of {}.".format(
                  len(records), RECORD_BYTES, CHAR_AI_SNES, populated,
                  len(records) * SLOT_COUNT))
        return 0

    _write(outputs["id_enum"], render_id_h(symbols))
    _write(outputs["inc"], render_inc(records, symbols))
    _write(outputs["fixture"], render_fixture(records))
    print("Emitted {} records -> {}".format(len(records), outputs["inc"]))
    print("Emitted fixture -> {}".format(outputs["fixture"]))
    print("Emitted CharAiId ({} names) -> {}".format(
        len(symbols.enums["CHAR_AI"].members), outputs["id_enum"]))
    return 0


def _write(path, text):
    directory = os.path.dirname(path)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root", default="original-src")
    ap.add_argument("--inc-out", default="src/data/generated/char_ai_data.inc")
    ap.add_argument("--fixture-out", default="tests/fixtures/char_ai_expected.h")
    ap.add_argument("--id-enum-out", default="include/ostinato/char_ai_id.h")
    ap.add_argument("--check-only", action="store_true")
    args = ap.parse_args(argv)

    outputs = {"inc": args.inc_out, "fixture": args.fixture_out,
               "id_enum": args.id_enum_out}
    try:
        return run(args.source_root, outputs, check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
