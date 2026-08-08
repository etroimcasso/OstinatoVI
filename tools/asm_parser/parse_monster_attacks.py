#!/usr/bin/env python3
"""Emit the monster attack-slot tables (rage / sketch / control) from
original-src.

Port-time tooling (NOT a build/CI dependency): interprets the ca65 macro DSLs
in src/battle/monster_rage.asm (256 records x 2 bytes, ROM CF/4600),
src/battle/monster_sketch.asm (384 records x 2 bytes, ROM CF/4300), and
src/battle/monster_control.asm (384 records x 4 bytes, ROM CF/3D00), and
emits one designated-initializer .inc plus one raw-byte fixture per table:

  * src/data/generated/monster_rage_data.inc    + tests/fixtures/monster_rage_expected.h
  * src/data/generated/monster_sketch_data.inc  + tests/fixtures/monster_sketch_expected.h
  * src/data/generated/monster_control_data.inc + tests/fixtures/monster_control_expected.h

The DSLs: after each table's label anchor, exactly one macro invocation per
monster, each argument a known ATTACK member. The macro *bodies* are asserted,
not skipped — they are the slot-shape authority:

  * make_monster_rage takes only the SECOND attack; its body emits
    `ATTACK::BATTLE` into slot 0 (monster_rage.asm:3-5) — slot 0 is
    structurally always the fight command.
  * make_monster_sketch emits its two arguments in order
    (monster_sketch.asm:3-5).
  * make_monster_control emits `ATTACK::BATTLE` into slot 0 and pads blank
    arguments with `ATTACK::NONE` ($FF, the consumers' empty sentinel —
    monster_control.asm:3-20).

Symbol values resolve against original-src/include/const.inc (ATTACK for the
record bytes, MONSTER for the identity names); an unknown symbol is a hard
error with a file:line citation — a deviation surfaces loudly, never as a
guessed byte.

Structural guarantees, hard-errored at emit time:
  * each macro body matches its documented shape (slot-0 BATTLE for rage and
    control, argument order for sketch, NONE padding for control);
  * each table's label anchors it and appears exactly once;
  * record counts are exactly 256 / 384 / 384; the MONSTER index space is
    exactly 384 ids (the rage table's 256 bound is the consumers' 8-bit
    index — monsters 256-383 have no rage row, and that absence is
    contract);
  * every rage/control record's slot-0 byte resolves to ATTACK::BATTLE.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_monster_attacks.py --source-root PATH \\
        --rage-inc-out FILE --rage-fixture-out FILE \\
        --sketch-inc-out FILE --sketch-fixture-out FILE \\
        --control-inc-out FILE --control-fixture-out FILE
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import sys

import common
import parse_const_enums as pce
from common import ParseError, strip_comment

MONSTER_COUNT = 384

# One spec per table: the file's macro name, label anchor, expected
# (whitespace-normalized) macro body, legal invocation arg counts, expected
# record count, and how an invocation's args expand to slot member names.
# The bodies are the slot-shape authority — a repin that changes one must
# hard-error, never silently re-shape the records.


def _expand_rage(args):
    return ["BATTLE", args[0]]


def _expand_sketch(args):
    return list(args)


def _expand_control(args):
    return ["BATTLE"] + list(args) + ["NONE"] * (3 - len(args))


class TableSpec(object):

    def __init__(self, key, macro, label, body, arg_counts, count, expand,
                 slots):
        self.key = key                # 'rage' / 'sketch' / 'control'
        self.macro = macro
        self.label = label
        self.body = body              # tuple of normalized body lines
        self.arg_counts = arg_counts  # legal invocation arg counts
        self.count = count            # expected record count
        self.expand = expand          # args -> slot member names
        self.slots = slots            # slot count (record width in bytes)


RAGE_SPEC = TableSpec(
    key="rage", macro="make_monster_rage", label="MonsterRage",
    body=(".byte ATTACK::BATTLE, ATTACK::attack2",),
    arg_counts=(1,), count=256, expand=_expand_rage, slots=2)

SKETCH_SPEC = TableSpec(
    key="sketch", macro="make_monster_sketch", label="MonsterSketch",
    body=(".byte ATTACK::attack1, ATTACK::attack2",),
    arg_counts=(2,), count=384, expand=_expand_sketch, slots=2)

CONTROL_SPEC = TableSpec(
    key="control", macro="make_monster_control", label="MonsterControl",
    body=(".byte ATTACK::BATTLE",
          ".ifnblank attack2", ".byte ATTACK::attack2",
          ".else", ".byte ATTACK::NONE", ".endif",
          ".ifnblank attack3", ".byte ATTACK::attack3",
          ".else", ".byte ATTACK::NONE", ".endif",
          ".ifnblank attack4", ".byte ATTACK::attack4",
          ".else", ".byte ATTACK::NONE", ".endif"),
    arg_counts=(0, 1, 2, 3), count=384, expand=_expand_control, slots=4)


class Record(object):
    """One monster's attack slots: ATTACK member names, resolved bytes, and
    identity."""

    def __init__(self, index, attacks, attack_bytes):
        self.index = index
        self.attacks = attacks          # list[str] of ATTACK member names
        self.bytes = attack_bytes       # list[int]
        self.name = None                # MONSTER member name, filled by index


class Symbols(object):
    """The const.inc enums the DSLs resolve against."""

    def __init__(self, const_inc):
        self.parsed = common.parse_ca65_constants(const_inc,
                                                  skip_body_enums=pce.SKIP)
        for enum_name in ("ATTACK", "MONSTER"):
            if self.parsed.enum(enum_name) is None:
                raise ParseError(const_inc, 0,
                                 "expected enum '{}' not found".format(enum_name))
        # First declaration wins (trailing aliases never shadow the primary).
        self.monster_names = {}
        for m in self.parsed.enum("MONSTER").members:
            self.monster_names.setdefault(m.value, m.name)
        if max(self.monster_names) != MONSTER_COUNT - 1:
            raise ParseError(const_inc, 0,
                             "MONSTER max id {} != {} (index-space mismatch)"
                             .format(max(self.monster_names),
                                     MONSTER_COUNT - 1))

    def attack_value(self, member, path, lineno):
        val = self.parsed.enum("ATTACK").value_of(member)
        if val is None:
            raise ParseError(path, lineno,
                             "unknown ATTACK::{}".format(member))
        return val


def parse_table(asm_path, spec, symbols):
    with open(asm_path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    records = []
    macro_body = []
    macro_depth = 0
    label_seen = False

    for idx, raw in enumerate(lines):
        lineno = idx + 1
        code, _comment = strip_comment(raw)
        if not code:
            continue
        s = code.strip()
        low = s.lower()

        # --- the macro definition: capture the body (slot-shape authority) ---
        if low.startswith(".endmac"):
            if macro_depth == 0:
                raise ParseError(asm_path, lineno, ".endmac without .mac")
            macro_depth -= 1
            continue
        if low.startswith(".mac"):
            macro_depth += 1
            continue
        if macro_depth > 0:
            macro_body.append(" ".join(s.split()))
            continue

        # --- the structural anchor ---
        if s == spec.label + ":":
            if label_seen:
                raise ParseError(asm_path, lineno,
                                 "duplicate {} label".format(spec.label))
            label_seen = True
            continue

        # --- record invocations ---
        parts = s.split(None, 1)
        if parts[0] == spec.macro:
            if not label_seen:
                raise ParseError(asm_path, lineno,
                                 "{} before the {} label".format(spec.macro,
                                                                 spec.label))
            args = ([a.strip() for a in parts[1].split(",")]
                    if len(parts) > 1 else [])
            if len(args) not in spec.arg_counts or any(not a for a in args):
                raise ParseError(asm_path, lineno,
                                 "{} expects {} args, got {}"
                                 .format(spec.macro, spec.arg_counts,
                                         len(args)))
            names = spec.expand(args)
            attack_bytes = [symbols.attack_value(n, asm_path, lineno)
                            for n in names]
            records.append(Record(len(records), names, attack_bytes))
            continue

        raise ParseError(asm_path, lineno,
                         "unrecognized line: '{}' (grammar not covered — "
                         "escalate, never guess)".format(s))

    if not label_seen:
        raise ParseError(asm_path, len(lines),
                         "{} label not found (structural anchor missing)"
                         .format(spec.label))
    if tuple(macro_body) != spec.body:
        raise ParseError(asm_path, 0,
                         "{} macro body {} does not match the documented "
                         "slot shape {}".format(spec.macro, macro_body,
                                                list(spec.body)))
    _verify(records, spec, symbols, asm_path)
    return records


def _verify(records, spec, symbols, path):
    if len(records) != spec.count:
        raise ParseError(path, 0,
                         "{} produced {} records; expected {}"
                         .format(spec.macro, len(records), spec.count))
    battle = symbols.parsed.enum("ATTACK").value_of("BATTLE")
    for rec in records:
        rec.name = symbols.monster_names[rec.index]
        for b in rec.bytes:
            if not (0 <= b <= 0xFF):
                raise ParseError(path, 0,
                                 "byte value {} out of range 0..255".format(b))
        if spec.key in ("rage", "control") and rec.bytes[0] != battle:
            raise ParseError(path, 0,
                             "record {} slot 0 byte {:#04x} != ATTACK::BATTLE"
                             .format(rec.index, rec.bytes[0]))


# --- rendering -------------------------------------------------------------

def _header(spec):
    source_lines = {
        "rage": ("// Source: src/battle/monster_rage.asm (MonsterRage, ROM "
                 "CF/4600,\n"
                 "//         256 make_monster_rage rows x 2 bytes; the macro "
                 "takes only\n"
                 "//         the second attack — slot 0 is always "
                 "ATTACK::BATTLE)\n"),
        "sketch": ("// Source: src/battle/monster_sketch.asm (MonsterSketch, "
                   "ROM CF/4300,\n"
                   "//         384 make_monster_sketch rows x 2 bytes)\n"),
        "control": ("// Source: src/battle/monster_control.asm "
                    "(MonsterControl, ROM CF/3D00,\n"
                    "//         384 make_monster_control rows x 4 bytes; "
                    "slot 0 is always\n"
                    "//         ATTACK::BATTLE, blank arguments emit "
                    "ATTACK::NONE)\n"),
    }[spec.key]
    return (
        "// AUTO-GENERATED by tools/asm_parser/parse_monster_attacks.py\n"
        + source_lines +
        "// Source: include/const.inc (ATTACK / MONSTER values)\n"
        "// (original-src pinned at 1ea47b5)\n"
        "// DO NOT EDIT BY HAND — regenerate via:\n"
        "//   python3 tools/asm_parser/parse_monster_attacks.py \\\n"
        "//       --source-root  original-src \\\n"
        "//       --rage-inc-out      src/data/generated/monster_rage_data.inc \\\n"
        "//       --rage-fixture-out  tests/fixtures/monster_rage_expected.h \\\n"
        "//       --sketch-inc-out    src/data/generated/monster_sketch_data.inc \\\n"
        "//       --sketch-fixture-out tests/fixtures/monster_sketch_expected.h \\\n"
        "//       --control-inc-out   src/data/generated/monster_control_data.inc \\\n"
        "//       --control-fixture-out tests/fixtures/monster_control_expected.h\n"
        "\n"
    )


# Per-slot comments carry the consumers' semantics so no slot is positionally
# opaque: the rage pick coin-flips between the two slots
# (battle_main.asm:985-990); the sketch effect picks slot 1 at 3/4 and slot 0
# at 1/4 (battle_main.asm:9543-9549); control slots list in the control menu
# with $FF as the empty sentinel (battle_main.asm:8876-8894).
_SLOT_COMMENTS = {
    "rage": ("slot 0 (1/2, always BATTLE)", "slot 1 (1/2)"),
    "sketch": ("slot 0 (1/4)", "slot 1 (3/4)"),
    "control": ("slot 0 (always BATTLE)", "slot 1", "slot 2", "slot 3"),
}

_ENTRY_TYPES = {
    "rage": ("MonsterRageEntry", "MonsterRage"),
    "sketch": ("MonsterSketchEntry", "MonsterSketch"),
    "control": ("MonsterControlEntry", "MonsterControl"),
}

_INC_BLURBS = {
    "rage": ("// MonsterRageEntry rows in MONSTER index order ($000..$0FF),\n"
             "// one designated-initializer row per monster, #included inside\n"
             "// the kMonsterRages array in src/data/monster_attacks.cpp. The\n"
             "// table covers monsters 0-255 only (the consumers index it\n"
             "// 8-bit; monsters 256-383 have no rage row — that absence is\n"
             "// contract). Each row's identity is its .id field — the\n"
             "// MonsterId enumerator; a compile-time assert verifies\n"
             "// id == position. The packed .record stays byte-identical to\n"
             "// the 2 ROM bytes; slot comments carry the consumer's\n"
             "// coin-flip semantics.\n\n"),
    "sketch": ("// MonsterSketchEntry rows in MONSTER index order\n"
               "// ($000..$17F), one designated-initializer row per monster,\n"
               "// #included inside the kMonsterSketches array in\n"
               "// src/data/monster_attacks.cpp. Each row's identity is its\n"
               "// .id field — the MonsterId enumerator; a compile-time\n"
               "// assert verifies id == position. The packed .record stays\n"
               "// byte-identical to the 2 ROM bytes; slot comments carry the\n"
               "// sketch effect's pick probabilities.\n\n"),
    "control": ("// MonsterControlEntry rows in MONSTER index order\n"
                "// ($000..$17F), one designated-initializer row per monster,\n"
                "// #included inside the kMonsterControls array in\n"
                "// src/data/monster_attacks.cpp. Each row's identity is its\n"
                "// .id field — the MonsterId enumerator; a compile-time\n"
                "// assert verifies id == position. The packed .record stays\n"
                "// byte-identical to the 4 ROM bytes; empty slots are\n"
                "// AttackId::NONE (the consumers' $FF sentinel).\n\n"),
}


def render_inc(records, spec):
    entry_type, record_type = _ENTRY_TYPES[spec.key]
    slot_comments = _SLOT_COMMENTS[spec.key]
    lines = [_header(spec), _INC_BLURBS[spec.key]]
    for rec in records:
        lines.append("    {}{{  // [${:03X}]\n".format(entry_type, rec.index))
        lines.append("        .id = MonsterId::{},\n".format(rec.name))
        lines.append("        .record = {}{{ .attacks = {{\n"
                     .format(record_type))
        width = max(len(a) for a in rec.attacks)
        for slot, attack in enumerate(rec.attacks):
            lines.append("            AttackId::{},{}  // {}\n".format(
                attack, " " * (width - len(attack)), slot_comments[slot]))
        lines.append("        } },\n")
        lines.append("    },\n")
    return "".join(lines)


_FIXTURE_TYPES = {
    "rage": ("ExpectedMonsterRageRecord", "ExpectedMonsterRageEntry",
             "kExpectedMonsterRageEntries"),
    "sketch": ("ExpectedMonsterSketchRecord", "ExpectedMonsterSketchEntry",
               "kExpectedMonsterSketchEntries"),
    "control": ("ExpectedMonsterControlRecord", "ExpectedMonsterControlEntry",
                "kExpectedMonsterControlEntries"),
}


def _fixture_struct(spec):
    record_type, entry_type, _ = _FIXTURE_TYPES[spec.key]
    slot_fields = ", ".join("slot{}".format(i) for i in range(spec.slots))
    return (
        "// One raw {size}-byte record: the ATTACK bytes in slot order.\n"
        "// Values are the exact ROM bytes with every upstream symbol\n"
        "// resolved — deliberately independent of the enum-symbol rows in\n"
        "// the generated .inc, so symbol/value drift in either artifact\n"
        "// fails the full-corpus byte-equivalence test.\n"
        "struct {record} {{\n"
        "    std::uint8_t {fields};\n"
        "}};\n"
        "static_assert(sizeof({record}) == {size},\n"
        "              \"fixture record must stay byte-identical to a ROM "
        "record\");\n"
        "\n"
        "// One fixture entry: the record's identity as a typed field (raw\n"
        "// decimal index — the fixture stays independent of the port's\n"
        "// MonsterId header) alongside the raw record bytes.\n"
        "struct {entry} {{\n"
        "    std::uint16_t id;\n"
        "    {record} record;\n"
        "}};\n"
    ).format(size=spec.slots, record=record_type, entry=entry_type,
             fields=slot_fields)


def render_fixture(records, spec):
    record_type, entry_type, array_name = _FIXTURE_TYPES[spec.key]
    lines = [_header(spec),
             "// Test fixture for tests/test_monster_tables.cpp — the\n"
             "// ground-truth record bytes. The full-corpus test asserts, per\n"
             "// entry: fixture id == position, table id enumerator ==\n"
             "// position, and a {}-byte memcmp of the packed record against\n"
             "// the generated .inc's row.\n"
             "\n"
             "#pragma once\n"
             "\n"
             "#include <array>\n"
             "#include <cstdint>\n"
             "\n"
             "namespace ostinato::test {{\n"
             "\n".format(spec.slots),
             _fixture_struct(spec),
             "\n",
             "inline constexpr std::array<{}, {}> "
             "{} = {{{{  // ROM {}\n".format(entry_type, len(records),
                                             array_name, spec.label)]
    for rec in records:
        slots = ", ".join(".slot{} = 0x{:02X}".format(i, b)
                          for i, b in enumerate(rec.bytes))
        lines.append("    {{ .id = {:>3},  // ${:03X} {}\n"
                     "      .record = {{ {} }} }},\n".format(
                         rec.index, rec.index, rec.name, slots))
    lines.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(lines)


# --- driver ----------------------------------------------------------------

def run(rage_asm, sketch_asm, control_asm, const_inc, outs, check_only=False):
    symbols = Symbols(const_inc)
    tables = (
        (RAGE_SPEC, rage_asm, outs.get("rage_inc"), outs.get("rage_fixture")),
        (SKETCH_SPEC, sketch_asm, outs.get("sketch_inc"),
         outs.get("sketch_fixture")),
        (CONTROL_SPEC, control_asm, outs.get("control_inc"),
         outs.get("control_fixture")),
    )
    for spec, asm_path, inc_out, fixture_out in tables:
        records = parse_table(asm_path, spec, symbols)
        if check_only:
            print("OK: {} {} records; macro body anchored, all bytes "
                  "resolved.".format(len(records), spec.key))
            continue
        _write(inc_out, render_inc(records, spec))
        _write(fixture_out, render_fixture(records, spec))
        print("Emitted {} {} records -> {}".format(len(records), spec.key,
                                                   inc_out))
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
                    help="disassembly root (the three table .asm files + "
                         "const.inc resolved under it)")
    ap.add_argument("--rage-asm", help="path to monster_rage.asm")
    ap.add_argument("--sketch-asm", help="path to monster_sketch.asm")
    ap.add_argument("--control-asm", help="path to monster_control.asm")
    ap.add_argument("--const-inc", help="path to const.inc")
    ap.add_argument("--rage-inc-out",
                    default="src/data/generated/monster_rage_data.inc")
    ap.add_argument("--rage-fixture-out",
                    default="tests/fixtures/monster_rage_expected.h")
    ap.add_argument("--sketch-inc-out",
                    default="src/data/generated/monster_sketch_data.inc")
    ap.add_argument("--sketch-fixture-out",
                    default="tests/fixtures/monster_sketch_expected.h")
    ap.add_argument("--control-inc-out",
                    default="src/data/generated/monster_control_data.inc")
    ap.add_argument("--control-fixture-out",
                    default="tests/fixtures/monster_control_expected.h")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    rage_asm, sketch_asm, control_asm = (args.rage_asm, args.sketch_asm,
                                         args.control_asm)
    const_inc = args.const_inc
    if args.source_root:
        battle = os.path.join(args.source_root, "src", "battle")
        rage_asm = rage_asm or os.path.join(battle, "monster_rage.asm")
        sketch_asm = sketch_asm or os.path.join(battle, "monster_sketch.asm")
        control_asm = control_asm or os.path.join(battle,
                                                  "monster_control.asm")
        const_inc = const_inc or os.path.join(args.source_root, "include",
                                              "const.inc")
    if not (rage_asm and sketch_asm and control_asm and const_inc):
        ap.error("provide --source-root, or all three table paths and "
                 "--const-inc")
    outs = {
        "rage_inc": args.rage_inc_out,
        "rage_fixture": args.rage_fixture_out,
        "sketch_inc": args.sketch_inc_out,
        "sketch_fixture": args.sketch_fixture_out,
        "control_inc": args.control_inc_out,
        "control_fixture": args.control_fixture_out,
    }
    try:
        return run(rage_asm, sketch_asm, control_asm, const_inc, outs,
                   check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
