#!/usr/bin/env python3
"""Emit the blitz input tables from original-src btlgfx_main.asm.

Port-time tooling (NOT a build/CI dependency): reads the `BlitzCode` and
`BlitzButtonMaskTbl` tables in src/btlgfx/btlgfx_main.asm and emits:

  * include/ostinato/blitz_code_input.h          — BLITZ_CODE
  * include/ostinato/pad_input.h                 — the JOY_* button masks
    (include/hardware.inc)
  * src/data/generated/blitz_code_data.inc       — one BlitzCodeEntry row per
    blitz (8), keyed by the blitz's AttackId, each built by BlitzCode::of()
    exactly as the corpus's `blitz_code`/`end_blitz_code` macros build it.
  * src/data/generated/blitz_button_mask_data.inc — one
    { BlitzCodeInput, BlitzButtonMask } pair per input (15).
  * tests/fixtures/blitz_code_expected.h         — the same data as raw bytes
    and words (the ground-truth contract).

A blitz code is 12 bytes (btlgfx_main.asm:17012-17015): up to eleven inputs,
padded with BLITZ_CODE::NONE, then the input count doubled. The padding and the
count are not written in the source — the `end_blitz_code` macro
(:17024-17031) computes them — so the parser replays that macro.

Structural guarantees, hard-errored at parse/emit time:
  * the blitz count and record size equal the `.scope BlitzCode` constants
    (include/btlgfx/blitz_code.inc:25-28), and the blocks are numbered 0..7;
  * no blitz holds more than eleven inputs, and every blitz ends on
    BLITZ_CODE::A_BUTTON (the documented format, :17014);
  * the mask table holds exactly one word per BLITZ_CODE member, and every
    word is either an exact OR of JOY_* masks or $FFFF;
  * every blitz is named by the ATTACK enumerator the battle command tables
    assign it (CmdWithAttackTbl / CmdAttackOffsetTbl);
  * both tables are identical to the vanilla cartridge over their whole extent.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_blitz_code.py --source-root PATH
    parse_blitz_code.py --source-root PATH --check-only
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
import parse_level_up as plu
from common import ParseError, strip_comment

# --- the tables ----------------------------------------------------------------

# Where the US cartridge holds each table. BlitzCode's own address comment
# (btlgfx_main.asm:17038) matches; the mask table sits three bytes before the
# `@6f6a` label the source carries. Both are re-checked byte for byte.
BLITZ_CODE_SNES = 0xC47A40
BUTTON_MASK_SNES = 0xC16F67

INPUT_SLOTS = 11
CODE_LABEL = "BlitzCode"
MASK_LABEL = "BlitzButtonMaskTbl"
ALL_BUTTONS = 0xFFFF

_RE_LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
_RE_LOCAL = re.compile(r"^@[0-9A-Za-z_]+:\s*")
_RE_BYTE = re.compile(r"^\.byte\s+(.+)$")
_RE_WORD = re.compile(r"^\.word\s+(.+)$")
_RE_OPEN = re.compile(r"^blitz_code\s+(\d+)$")
_RE_CLOSE = re.compile(r"^end_blitz_code\s+(\d+)$")
_RE_SCOPED = re.compile(r"^BLITZ_CODE::([A-Za-z_][A-Za-z0-9_]*)$")
_RE_SCOPE_CONST = re.compile(r"^([A-Z_]+)\s*=\s*(\S+)$")
_RE_JOY = re.compile(r"^JOY_([A-Z]+)\s*=\s*(\S+)$")


def _paths(source_root):
    return {
        "blitz_inc": os.path.join(source_root, "include", "btlgfx",
                                  "blitz_code.inc"),
        "hardware_inc": os.path.join(source_root, "include", "hardware.inc"),
        "const_inc": os.path.join(source_root, "include", "const.inc"),
        "btlgfx_asm": os.path.join(source_root, "src", "btlgfx",
                                   "btlgfx_main.asm"),
        "battle_main_asm": os.path.join(source_root, "src", "battle",
                                        "battle_main.asm"),
    }


# --- symbols -------------------------------------------------------------------

class BlitzSymbols(object):
    """BLITZ_CODE, the BlitzCode scope's two sizes, and the JOY_* masks."""

    def __init__(self, blitz_inc, hardware_inc):
        parsed = common.parse_ca65_constants(blitz_inc)
        enum = parsed.enum("BLITZ_CODE")
        if enum is None:
            raise ParseError(blitz_inc, 0, "expected enum 'BLITZ_CODE' not found")
        self.inputs = [(m.name, m.value) for m in enum.members]
        for position, (name, value) in enumerate(self.inputs):
            if value != position:
                raise ParseError(blitz_inc, 0,
                                 "BLITZ_CODE::{} = {} is not at position {} — "
                                 "the mask table is indexed by value"
                                 .format(name, value, position))
        self._by_name = dict(self.inputs)
        self.scope = read_scope_constants(blitz_inc, CODE_LABEL)
        self.pads = read_joypad_masks(hardware_inc)

    def input_value(self, name, path, lineno):
        if name not in self._by_name:
            raise ParseError(path, lineno, "unknown BLITZ_CODE::{}".format(name))
        return self._by_name[name]

    def input_name(self, value):
        return self.inputs[value][0]


def read_scope_constants(path, scope_name):
    """The `NAME = value` constants inside `.scope <scope_name>`."""
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()
    found = {}
    inside = False
    for lineno, raw in enumerate(lines, 1):
        code, _ = strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        if s.lower().startswith(".scope"):
            inside = s.split(None, 1)[1:] == [scope_name]
            continue
        if s.lower().startswith(".endscope"):
            inside = False
            continue
        if inside:
            m = _RE_SCOPE_CONST.match(s)
            value = common.parse_int_literal(m.group(2)) if m else None
            if value is None:
                raise ParseError(path, lineno,
                                 "unexpected line in .scope {}: '{}'"
                                 .format(scope_name, s))
            found[m.group(1)] = value
    for key in ("ARRAY_LENGTH", "ITEM_SIZE"):
        if key not in found:
            raise ParseError(path, 0,
                             ".scope {} has no {}".format(scope_name, key))
    return found


def read_joypad_masks(path):
    """The JOY_* masks, in source order: [(name, bit)], each a single bit."""
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()
    pads = []
    for lineno, raw in enumerate(lines, 1):
        code, _ = strip_comment(raw)
        m = _RE_JOY.match(code.strip())
        if not m:
            continue
        value = common.parse_int_literal(m.group(2))
        if value is None or value == 0 or value & (value - 1) or value > 0xFFFF:
            raise ParseError(path, lineno,
                             "JOY_{} is not a single bit of a 16-bit word"
                             .format(m.group(1)))
        pads.append((m.group(1), value))
    if not pads:
        raise ParseError(path, 0, "no JOY_* masks found")
    bits = [v for _, v in pads]
    if len(set(bits)) != len(bits):
        raise ParseError(path, 0, "two JOY_* masks share a bit")
    return pads


# --- the blitz codes -----------------------------------------------------------

class Blitz(object):
    """One blitz: its input sequence and the 12 bytes the macros assemble."""

    def __init__(self, index, sequence):
        self.index = index
        self.sequence = list(sequence)
        self.name = None
        self.bytes = (self.sequence + [0] * (INPUT_SLOTS - len(self.sequence))
                      + [len(self.sequence) * 2])


def parse_blitz_codes(asm_path, symbols):
    """Walk BlitzCode, replaying blitz_code / end_blitz_code per block."""
    with open(asm_path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    blitzes = []
    in_table = False
    open_id = None
    sequence = []
    macro_depth = 0
    for lineno, raw in enumerate(lines, 1):
        code, _ = strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        low = s.lower()
        if low.startswith(".endmac"):
            macro_depth = max(0, macro_depth - 1)
            continue
        if low.startswith(".mac"):
            macro_depth += 1
            continue
        if macro_depth:
            continue

        label = _RE_LABEL.match(s)
        if label:
            if label.group(1) == CODE_LABEL:
                in_table = True
                continue
            if in_table:
                raise ParseError(asm_path, lineno,
                                 "label '{}' inside {}".format(label.group(1),
                                                               CODE_LABEL))
            continue
        if not in_table:
            continue
        if low.startswith(".popseg"):
            break

        opened = _RE_OPEN.match(s)
        if opened:
            if open_id is not None:
                raise ParseError(asm_path, lineno,
                                 "blitz_code {} opened inside blitz {}"
                                 .format(opened.group(1), open_id))
            open_id = int(opened.group(1))
            if open_id != len(blitzes):
                raise ParseError(asm_path, lineno,
                                 "blitz_code {} out of order (expected {})"
                                 .format(open_id, len(blitzes)))
            sequence = []
            continue
        closed = _RE_CLOSE.match(s)
        if closed:
            if open_id is None or int(closed.group(1)) != open_id:
                raise ParseError(asm_path, lineno,
                                 "end_blitz_code {} does not close blitz {}"
                                 .format(closed.group(1), open_id))
            _check_sequence(open_id, sequence, symbols, asm_path, lineno)
            blitzes.append(Blitz(open_id, sequence))
            open_id = None
            continue

        byte_line = _RE_BYTE.match(s)
        if byte_line:
            if open_id is None:
                raise ParseError(asm_path, lineno,
                                 ".byte outside a blitz_code block")
            for term in byte_line.group(1).split(","):
                scoped = _RE_SCOPED.match(term.strip())
                if not scoped:
                    raise ParseError(asm_path, lineno,
                                     "'{}' is not a BLITZ_CODE reference "
                                     "(grammar not covered — escalate, never "
                                     "guess)".format(term.strip()))
                sequence.append(symbols.input_value(scoped.group(1),
                                                    asm_path, lineno))
            continue
        raise ParseError(asm_path, lineno,
                         "unexpected line inside {}: '{}'".format(CODE_LABEL, s))

    if not in_table:
        raise ParseError(asm_path, 0, "{} not found".format(CODE_LABEL))
    if open_id is not None:
        raise ParseError(asm_path, 0, "blitz_code {} never closed".format(open_id))
    if len(blitzes) != symbols.scope["ARRAY_LENGTH"]:
        raise ParseError(asm_path, 0,
                         "{} holds {} blitzes; BlitzCode::ARRAY_LENGTH is {}"
                         .format(CODE_LABEL, len(blitzes),
                                 symbols.scope["ARRAY_LENGTH"]))
    if INPUT_SLOTS + 1 != symbols.scope["ITEM_SIZE"]:
        raise ParseError(asm_path, 0,
                         "BlitzCode::ITEM_SIZE is {}, not {} input slots + the "
                         "count byte".format(symbols.scope["ITEM_SIZE"],
                                             INPUT_SLOTS))
    return blitzes


def _check_sequence(index, sequence, symbols, path, lineno):
    if not sequence:
        raise ParseError(path, lineno, "blitz {} has no inputs".format(index))
    if len(sequence) > INPUT_SLOTS:
        raise ParseError(path, lineno,
                         "blitz {} has {} inputs; the record holds {}"
                         .format(index, len(sequence), INPUT_SLOTS))
    if symbols.input_name(sequence[-1]) != "A_BUTTON":
        raise ParseError(path, lineno,
                         "blitz {} ends on {}, not A_BUTTON"
                         .format(index, symbols.input_name(sequence[-1])))
    if any(symbols.input_name(v) == "NONE" for v in sequence):
        raise ParseError(path, lineno,
                         "blitz {} writes NONE inside its sequence"
                         .format(index))


# --- the button masks ----------------------------------------------------------

def parse_button_masks(asm_path, symbols):
    """The BlitzButtonMaskTbl words, one per BLITZ_CODE value."""
    with open(asm_path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()
    words = []
    in_table = False
    for lineno, raw in enumerate(lines, 1):
        code, _ = strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        label = _RE_LABEL.match(s)
        if label:
            if label.group(1) == MASK_LABEL:
                in_table = True
                continue
            if in_table:
                break
            continue
        if not in_table:
            continue
        s = _RE_LOCAL.sub("", s)
        word_line = _RE_WORD.match(s)
        if not word_line:
            break
        for term in word_line.group(1).split(","):
            value = common.parse_int_literal(term.strip())
            if value is None or value > 0xFFFF:
                raise ParseError(asm_path, lineno,
                                 "'{}' is not a 16-bit literal".format(term))
            words.append(value)
    if len(words) != len(symbols.inputs):
        raise ParseError(asm_path, 0,
                         "{} holds {} words; BLITZ_CODE has {} members"
                         .format(MASK_LABEL, len(words), len(symbols.inputs)))
    for value, word in enumerate(words):
        decompose_mask(word, symbols.pads, asm_path)
    return words


def decompose_mask(word, pads, path):
    """The JOY_* names a mask is made of, in source order; None for $FFFF."""
    if word == ALL_BUTTONS:
        return None
    names = [name for name, bit in pads if word & bit]
    rebuilt = 0
    for name, bit in pads:
        if name in names:
            rebuilt |= bit
    if not names or rebuilt != word:
        raise ParseError(path, 0,
                         "mask ${:04X} is not an exact OR of JOY_* masks"
                         .format(word))
    return names


# --- the cartridge -------------------------------------------------------------

def assert_rom(rom, blitzes, words, path):
    want = [b for blitz in blitzes for b in blitz.bytes]
    _compare(rom, BLITZ_CODE_SNES, want, CODE_LABEL, path)
    want = []
    for word in words:
        want += [word & 0xFF, word >> 8]
    _compare(rom, BUTTON_MASK_SNES, want, MASK_LABEL, path)


def _compare(rom, snes, want, label, path):
    base = common.hirom_file_offset(snes)
    got = list(rom[base:base + len(want)])
    if got != want:
        at = next((i for i, (g, w) in enumerate(zip(got, want)) if g != w),
                  min(len(got), len(want)))
        raise ParseError(path, 0,
                         "ROM MISMATCH in {} at ${:06X}+{}: the cartridge "
                         "differs from the assembled source"
                         .format(label, snes, at))


# --- rendering -----------------------------------------------------------------

_REGEN = ("// DO NOT EDIT BY HAND — regenerate via:\n"
          "//   python3 tools/asm_parser/parse_blitz_code.py \\\n"
          "//       --source-root  original-src\n")


def render_input_h(symbols):
    width = max(len(n) for n, _ in symbols.inputs)
    out = ["// AUTO-GENERATED by tools/asm_parser/parse_blitz_code.py\n"
           "// Source: original-src/include/btlgfx/blitz_code.inc (ca65 .enum "
           "BLITZ_CODE)\n"
           "// (original-src pinned at 1ea47b5)\n", _REGEN,
           "//\n"
           "// One input in a blitz's button sequence: a face or shoulder\n"
           "// button, or one of the eight pad directions. NONE pads a\n"
           "// sequence out to its fixed length.\n"
           "#pragma once\n\n#include <cstdint>\n\nnamespace ostinato {\n\n"
           "enum class BlitzCodeInput : std::uint8_t {\n"]
    for name, value in symbols.inputs:
        out.append("    {}{} = {},\n".format(name, " " * (width - len(name)),
                                             value))
    out.append("};\n\n}  // namespace ostinato\n")
    return "".join(out)


def render_pad_h(symbols):
    width = max(len(n) for n, _ in symbols.pads)
    out = ["// AUTO-GENERATED by tools/asm_parser/parse_blitz_code.py\n"
           "// Source: original-src/include/hardware.inc (JOY_* button masks)\n"
           "// (original-src pinned at 1ea47b5)\n", _REGEN,
           "//\n"
           "// The twelve inputs of the game's controller. Each enumerator is the\n"
           "// input's bit in a 16-bit button mask, so a set of inputs is the OR\n"
           "// of its members.\n"
           "#pragma once\n\n#include <cstdint>\n\nnamespace ostinato {\n\n"
           "enum class PadInput : std::uint16_t {\n"]
    for name, value in symbols.pads:
        out.append("    {}{} = 0x{:04X},\n".format(
            name, " " * (width - len(name)), value))
    out.append("};\n\n}  // namespace ostinato\n")
    return "".join(out)


_INC_HEADER = (
    "// AUTO-GENERATED by tools/asm_parser/parse_blitz_code.py\n"
    "// Source: src/btlgfx/btlgfx_main.asm (BlitzCode, 8 records x 12 bytes,\n"
    "//         ROM $C4/7A40; BlitzButtonMaskTbl, 15 words, ROM $C1/6F67)\n"
    "// Source: include/btlgfx/blitz_code.inc (BLITZ_CODE),\n"
    "//         include/hardware.inc (JOY_*), include/const.inc (ATTACK)\n"
    "// (original-src pinned at 1ea47b5; both tables cross-checked against the\n"
    "// vanilla ROM over their whole extent)\n" + _REGEN + "\n")


def render_code_inc(blitzes, symbols):
    out = [_INC_HEADER,
           "// BlitzCodeEntry rows in blitz order, #included inside the\n"
           "// kBlitzCodes array in src/data/blitz_codes.cpp. Each row's identity\n"
           "// is its .id field — the blitz's AttackId. BlitzCode::of() pads the\n"
           "// sequence with NONE and stores the doubled input count, so the\n"
           "// packed record is byte-identical to the 12 ROM bytes.\n\n"]
    for blitz in blitzes:
        out.append("    BlitzCodeEntry{\n")
        out.append("        .id = AttackId::{},\n".format(blitz.name))
        out.append("        .record = BlitzCode::of({\n")
        for value in blitz.sequence:
            out.append("            BlitzCodeInput::{},\n".format(
                symbols.input_name(value)))
        out.append("        }),\n    },\n")
    return "".join(out)


def render_mask_inc(words, symbols):
    out = [_INC_HEADER,
           "// { BlitzCodeInput, BlitzButtonMask } pairs in BLITZ_CODE order,\n"
           "// #included inside the kBlitzButtonMasks array in\n"
           "// src/data/blitz_codes.cpp. The mask is what a pressed input must\n"
           "// share a bit with for the input to count; a compile-time assert\n"
           "// verifies input == position.\n\n"]
    for value, word in enumerate(words):
        names = decompose_mask(word, symbols.pads, "blitz masks")
        if names is None:
            mask = "BlitzButtonMask::allButtons()"
        else:
            mask = "BlitzButtonMask::of({})".format(
                ", ".join("PadInput::" + n for n in names))
        out.append("    {{ BlitzCodeInput::{}, {} }},\n".format(
            symbols.input_name(value), mask))
    return "".join(out)


_FIXTURE_STRUCTS = (
    "// One raw 12-byte blitz record (btlgfx_main.asm:17012-17015): the eleven\n"
    "// input bytes, NONE-padded, then the input count doubled. Deliberately\n"
    "// independent of the typed rows in blitz_code_data.inc, so drift in\n"
    "// either artifact fails the full-corpus comparison.\n"
    "struct ExpectedBlitzCode {\n"
    "    std::uint8_t inputs[11];\n"
    "    std::uint8_t doubledInputCount;\n"
    "};\n"
    "static_assert(sizeof(ExpectedBlitzCode) == 12,\n"
    "              \"fixture record must stay identical to a ROM blitz record\");\n"
    "\n"
    "// One fixture entry: the blitz's position (raw decimal — the fixture\n"
    "// stays independent of the port's AttackId header) and its record.\n"
    "struct ExpectedBlitzCodeEntry {\n"
    "    std::uint8_t id;\n"
    "    ExpectedBlitzCode record;\n"
    "};\n"
    "\n"
    "// One mask word: the BLITZ_CODE value it belongs to and the word itself.\n"
    "struct ExpectedBlitzButtonMask {\n"
    "    std::uint8_t input;\n"
    "    std::uint16_t mask;\n"
    "};\n"
)


def render_fixture(blitzes, words):
    out = [_INC_HEADER,
           "// Test fixture for tests/test_blitz_codes.cpp — the ground-truth\n"
           "// bytes of both blitz tables.\n"
           "\n#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           "namespace ostinato::test {\n\n", _FIXTURE_STRUCTS,
           "\ninline constexpr std::array<ExpectedBlitzCodeEntry, {}>\n"
           "    kExpectedBlitzCodes = {{{{\n".format(len(blitzes))]
    for blitz in blitzes:
        out.append("    {{ .id = {}, .record = {{ .inputs = {{ {} }}, "
                   ".doubledInputCount = {} }} }},\n".format(
                       blitz.index,
                       ", ".join("{:>2}".format(b) for b in blitz.bytes[:-1]),
                       blitz.bytes[-1]))
    out.append("}}}};\n\n"
               "inline constexpr std::array<ExpectedBlitzButtonMask, {}>\n"
               "    kExpectedBlitzButtonMasks = {{{{\n".format(len(words)))
    for value, word in enumerate(words):
        out.append("    {{ .input = {:>2}, .mask = 0x{:04X} }},\n".format(
            value, word))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver --------------------------------------------------------------------

def read_all(source_root):
    paths = _paths(source_root)
    symbols = BlitzSymbols(paths["blitz_inc"], paths["hardware_inc"])
    blitzes = parse_blitz_codes(paths["btlgfx_asm"], symbols)
    names = plu.read_ability_names(paths, _const_parsed(paths), "BLITZ",
                                   len(blitzes))
    for blitz, name in zip(blitzes, names):
        blitz.name = name
    words = parse_button_masks(paths["btlgfx_asm"], symbols)
    return paths, symbols, blitzes, words


def _const_parsed(paths):
    import parse_const_enums as pce
    return common.parse_ca65_constants(paths["const_inc"],
                                       skip_body_enums=pce.SKIP)


def run(source_root, outputs, check_only=False):
    paths, symbols, blitzes, words = read_all(source_root)
    assert_rom(common.load_vanilla_rom(source_root), blitzes, words,
               paths["btlgfx_asm"])
    if check_only:
        print("OK: {} blitzes x 12 bytes and {} mask words, cartridge-identical."
              .format(len(blitzes), len(words)))
        return 0
    _write(outputs["input_enum"], render_input_h(symbols))
    _write(outputs["pad_enum"], render_pad_h(symbols))
    _write(outputs["code_inc"], render_code_inc(blitzes, symbols))
    _write(outputs["mask_inc"], render_mask_inc(words, symbols))
    _write(outputs["fixture"], render_fixture(blitzes, words))
    for key in ("input_enum", "pad_enum", "code_inc", "mask_inc", "fixture"):
        print("Emitted -> {}".format(outputs[key]))
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
    ap.add_argument("--code-inc-out",
                    default="src/data/generated/blitz_code_data.inc")
    ap.add_argument("--mask-inc-out",
                    default="src/data/generated/blitz_button_mask_data.inc")
    ap.add_argument("--fixture-out",
                    default="tests/fixtures/blitz_code_expected.h")
    ap.add_argument("--input-enum-out",
                    default="include/ostinato/blitz_code_input.h")
    ap.add_argument("--pad-enum-out", default="include/ostinato/pad_input.h")
    ap.add_argument("--check-only", action="store_true")
    args = ap.parse_args(argv)
    outputs = {"code_inc": args.code_inc_out, "mask_inc": args.mask_inc_out,
               "fixture": args.fixture_out, "input_enum": args.input_enum_out,
               "pad_enum": args.pad_enum_out}
    try:
        return run(args.source_root, outputs, check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
