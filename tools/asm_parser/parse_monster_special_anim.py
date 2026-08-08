#!/usr/bin/env python3
"""Emit the monster special-attack animation table from original-src
monster_special_anim.dat.

Port-time tooling (NOT a build/CI dependency). monster_special_anim.dat is the
raw 384-byte table (ROM CF/37C0; label MonsterSpecialAnim at the incbin site,
battle_main.asm:16476-16477): one byte per monster (notes/battle-ram.txt:961
"Special Attack Animation Index"), loaded per monster to $3C81
(battle_main.asm:7420, rage path :1004-1005) and copied to $B7 when a monster
special attack fires (:8193-8194). The battle graphics code consumes it as
the row index into the 35-row Monster Attack Animation Data table
(ROM EC/E6E8; notes/rom-map.txt:260) — InitWeaponAnim multiplies it by 8 and
reads that table's 8-byte record (btlgfx_main.asm:23661-23677).

The rows are a symbol set and emit through the MonsterAttackAnimation enum
(include/ostinato/monster_attack_animation.h). The upstream has no symbolic
names for the rows; the enumerators derive MECHANICALLY from the corpus:
each row's name is the dominant special-attack display name among the
monsters whose special uses it (the Monster Special Attack Names text table,
ROM CFD0D0 / rom-map.txt:127, ripped to src/text/monster_special_name_en.json),
ties broken by the earliest monster index, unused rows named UNUSED_n. This
script recomputes that derivation on every run and hard-errors if it
disagrees with the mirror table below (which mirrors the C++ header), so the
naming can never silently drift from the corpus. Outputs:

  * src/data/generated/monster_special_anim_data.inc — one
    designated-initializer MonsterSpecialAnimEntry row per monster
    (384 rows), identity as the MonsterId enumerator field and the value as
    its MonsterAttackAnimation enumerator; the kMonsterSpecialAnims array
    #includes it.
  * tests/fixtures/monster_special_anim_expected.h — the same 384 bytes with
    decimal identity (the ground-truth byte contract) for a full-corpus
    equivalence test.

Identity names resolve against original-src/include/const.inc (MONSTER).

Structural guarantees, hard-errored at emit time:
  * the .dat is exactly 384 bytes (one per MONSTER id — any other length is
    the wrong artifact);
  * every byte is a valid row index (< 35);
  * the recomputed dominant-name derivation matches the mirror table;
  * every record index has a MONSTER name, and the MONSTER space is exactly
    384 ids.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_monster_special_anim.py --source-root PATH --inc-out FILE \\
                                  --fixture-out FILE
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

import common
import parse_const_enums as pce
from common import ParseError

RECORD_COUNT = 384
TABLE_ROWS = 35

# Mirror of include/ostinato/monster_attack_animation.h — one enumerator per
# Monster Attack Animation Data row. derive_names() recomputes this from the
# corpus and _verify_names() hard-errors on any drift.
_ANIMATION_NAMES = (
    "HIT",         # 0
    "SICKLE",      # 1
    "DIVE",        # 2
    "CRITICAL",    # 3
    "WING",        # 4
    "SEIZE",       # 5
    "SLASH",       # 6
    "TAIL",        # 7
    "SCRATCH",     # 8
    "RAPIER",      # 9
    "POUNCE",      # 10
    "UMBRAWLER",   # 11
    "RUSH",        # 12
    "AXE",         # 13
    "BITE",        # 14
    "IRONNEEDLE",  # 15
    "INK",         # 16
    "PAUSE",       # 17
    "BONE",        # 18
    "WRENCH",      # 19
    "BRAINSTORM",  # 20
    "NEAR_FATAL",  # 21
    "METAL_ARM",   # 22
    "SLIME",       # 23
    "INVIZ",       # 24
    "YAWN",        # 25
    "SMIRK",       # 26
    "WHEEL",       # 27
    "IMPMARE",     # 28
    "UNUSED_29",   # 29
    "CLING",       # 30
    "DRILL",       # 31
    "IRON_BALL",   # 32
    "TRADEOFF",    # 33
    "UNUSED_34",   # 34
)


class Symbols(object):
    """The const.inc MONSTER enum the identity names resolve against."""

    def __init__(self, const_inc):
        self.parsed = common.parse_ca65_constants(const_inc,
                                                  skip_body_enums=pce.SKIP)
        if self.parsed.enum("MONSTER") is None:
            raise ParseError(const_inc, 0,
                             "expected enum 'MONSTER' not found")
        # First declaration wins (trailing aliases never shadow the primary).
        self.monster_names = {}
        for m in self.parsed.enum("MONSTER").members:
            self.monster_names.setdefault(m.value, m.name)
        if max(self.monster_names) != RECORD_COUNT - 1:
            raise ParseError(const_inc, 0,
                             "MONSTER max id {} != {} (index-space mismatch)"
                             .format(max(self.monster_names),
                                     RECORD_COUNT - 1))


def read_values(dat_path):
    with open(dat_path, "rb") as fh:
        data = fh.read()
    if len(data) != RECORD_COUNT:
        raise ParseError(dat_path, 0,
                         "{} bytes, expected {} (one per MONSTER id — wrong "
                         "artifact)".format(len(data), RECORD_COUNT))
    for index, value in enumerate(data):
        if value >= TABLE_ROWS:
            raise ParseError(dat_path, 0,
                             "monster {} animation byte {:#04x} outside the "
                             "{}-row table — escalate, never guess"
                             .format(index, value, TABLE_ROWS))
    return list(data)


# --- enumerator-name derivation ----------------------------------------------

def sanitize_name(display_name):
    """A display name -> a C++ enumerator: non-alphanumerics to '_', upper,
    N-prefixed if it would start with a digit."""
    s = re.sub(r"[^A-Za-z0-9]+", "_", display_name).strip("_").upper()
    return re.sub(r"^([0-9])", r"N\1", s)


def derive_names(values, display_names):
    """Recompute the per-row enumerator names from the corpus: each row's
    name is the dominant special-attack display name among its users (ties
    broken by the earliest monster index); unused rows are UNUSED_n."""
    users = {}
    for monster, row in enumerate(values):
        users.setdefault(row, []).append(display_names[monster])
    derived = []
    for row in range(TABLE_ROWS):
        row_users = users.get(row)
        if not row_users:
            derived.append("UNUSED_{}".format(row))
            continue
        counts = {}
        for name in row_users:
            counts[name] = counts.get(name, 0) + 1
        best = max(counts,
                   key=lambda n: (counts[n], -row_users.index(n)))
        derived.append(sanitize_name(best))
    return derived


def read_display_names(json_path):
    with open(json_path, "r", encoding="utf-8") as fh:
        payload = json.load(fh)
    names = payload.get("text")
    if not isinstance(names, list) or len(names) != RECORD_COUNT:
        raise ParseError(json_path, 0,
                         "expected a 'text' list of {} special-attack names"
                         .format(RECORD_COUNT))
    return names


def _verify_names(values, name_json_path):
    derived = derive_names(values, read_display_names(name_json_path))
    if tuple(derived) != _ANIMATION_NAMES:
        diffs = ["row {}: derived {} != mirror {}".format(i, d, m)
                 for i, (d, m) in enumerate(zip(derived, _ANIMATION_NAMES))
                 if d != m]
        raise ParseError(name_json_path, 0,
                         "MonsterAttackAnimation derivation drifted from the "
                         "mirror table — {} (update the header + mirror "
                         "together, never one side)".format("; ".join(diffs)))


# --- rendering ---------------------------------------------------------------

_HEADER_COMMON = (
    "// AUTO-GENERATED by tools/asm_parser/parse_monster_special_anim.py\n"
    "// Source: src/battle/monster_special_anim.dat (MonsterSpecialAnim,\n"
    "//         ROM CF/37C0, 384 bytes — one Monster Attack Animation Data\n"
    "//         row index per monster; incbin at battle_main.asm:16476-16477,\n"
    "//         documented at notes/battle-ram.txt:961)\n"
    "// Source: src/text/monster_special_name_en.json (dominant-name\n"
    "//         derivation check for the MonsterAttackAnimation enumerators)\n"
    "// Source: include/const.inc (MONSTER values)\n"
    "// (original-src pinned at 1ea47b5)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_monster_special_anim.py \\\n"
    "//       --source-root  original-src \\\n"
    "//       --inc-out      src/data/generated/monster_special_anim_data.inc \\\n"
    "//       --fixture-out  tests/fixtures/monster_special_anim_expected.h\n"
    "\n"
)


def render_inc(values, symbols):
    lines = [_HEADER_COMMON,
             "// MonsterSpecialAnimEntry rows in MONSTER index order\n"
             "// ($000..$17F), one row per monster, #included inside the\n"
             "// kMonsterSpecialAnims array in\n"
             "// src/data/monster_special_anim.cpp. Each row's identity is\n"
             "// its .id field — the MonsterId enumerator; a compile-time\n"
             "// assert verifies id == position. .specialAnim is the row the\n"
             "// monster's special attack selects in the Monster Attack\n"
             "// Animation Data table, as its MonsterAttackAnimation\n"
             "// enumerator.\n\n"]
    name_width = max(len(symbols.monster_names[i]) for i in range(len(values)))
    for index, value in enumerate(values):
        name = symbols.monster_names[index]
        lines.append("    {{ .id = MonsterId::{},{} .specialAnim = "
                     "MonsterAttackAnimation::{} }},\n".format(
                         name, " " * (name_width - len(name)),
                         _ANIMATION_NAMES[value]))
    return "".join(lines)


def render_fixture(values):
    lines = [_HEADER_COMMON,
             "// Test fixture for tests/test_monster_tables.cpp — the\n"
             "// ground-truth copy of the table (.id decimal identity,\n"
             "// .specialAnim raw ROM byte, hex — this side of the\n"
             "// equivalence test IS the ROM byte). The full-corpus test\n"
             "// asserts kMonsterSpecialAnims matches this array entry by\n"
             "// entry, so a hand edit or re-emit drift in either file\n"
             "// fails loudly.\n"
             "\n"
             "#pragma once\n"
             "\n"
             "#include <array>\n"
             "#include <cstdint>\n"
             "\n"
             "namespace ostinato::test {\n"
             "\n"
             "// Mirrors ostinato::MonsterSpecialAnimEntry\n"
             "// (src/data/monster_special_anim.h) without depending on it.\n"
             "struct ExpectedMonsterSpecialAnimEntry {\n"
             "    std::uint16_t id;\n"
             "    std::uint8_t specialAnim;\n"
             "};\n"
             "\n",
             "inline constexpr std::array<ExpectedMonsterSpecialAnimEntry, "
             "{}>\n"
             "kExpectedMonsterSpecialAnimEntries = {{{{  "
             "// ROM MonsterSpecialAnim\n".format(len(values))]
    for index, value in enumerate(values):
        lines.append("    {{ .id = {:>3}, .specialAnim = 0x{:02X} }},\n"
                     .format(index, value))
    lines.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(lines)


# --- driver ----------------------------------------------------------------

def run(dat_path, name_json, const_inc, inc_out, fixture_out,
        check_only=False):
    symbols = Symbols(const_inc)
    values = read_values(dat_path)
    _verify_names(values, name_json)

    if check_only:
        print("OK: {} animation bytes; all rows in range; enumerator "
              "derivation matches the mirror.".format(len(values)))
        return 0

    _write(inc_out, render_inc(values, symbols))
    _write(fixture_out, render_fixture(values))
    print("Emitted {} rows -> {}".format(len(values), inc_out))
    print("Emitted fixture -> {}".format(fixture_out))
    return 0


def _write(path, text):
    directory = os.path.dirname(path)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root",
                    help="disassembly root (monster_special_anim.dat, the "
                         "special-name json, and const.inc resolved under it)")
    ap.add_argument("--special-anim-dat",
                    help="path to monster_special_anim.dat")
    ap.add_argument("--special-name-json",
                    help="path to monster_special_name_en.json")
    ap.add_argument("--const-inc", help="path to const.inc")
    ap.add_argument("--inc-out",
                    default="src/data/generated/monster_special_anim_data.inc",
                    help="output path for the MonsterSpecialAnimEntry rows")
    ap.add_argument("--fixture-out",
                    default="tests/fixtures/monster_special_anim_expected.h",
                    help="output path for the raw-byte fixture")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    dat_path = args.special_anim_dat
    name_json = args.special_name_json
    const_inc = args.const_inc
    if args.source_root:
        if not dat_path:
            dat_path = os.path.join(args.source_root, "src", "battle",
                                    "monster_special_anim.dat")
        if not name_json:
            name_json = os.path.join(args.source_root, "src", "text",
                                     "monster_special_name_en.json")
        if not const_inc:
            const_inc = os.path.join(args.source_root, "include", "const.inc")
    if not dat_path or not name_json or not const_inc:
        ap.error("provide --source-root, or --special-anim-dat, "
                 "--special-name-json, and --const-inc")
    try:
        return run(dat_path, name_json, const_inc, args.inc_out,
                   args.fixture_out, check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
