#!/usr/bin/env python3
"""Emit the field-encounter tables + the battle-background enum from original-src.

Port-time tooling (NOT a build/CI dependency). The field-encounter family decides
which formation the player runs into and how often. Six binary tables plus five
inline tables in field/battle.asm, all read by CheckBattleWorld
(field/battle.asm:97), CheckBattleSub (:319), and GetVeldtBattle (:269):

  * rand_battle_group.dat  (RandBattleGroup,  ROM CF/4800, 256 x 4 words;
    field/battle.asm:510) — four candidate formation words per random group.
  * event_battle_group.dat (EventBattleGroup, ROM CF/5000, 256 x 2 words;
    :514) — two candidate formation words per event group.
  * world_battle_group.dat (WorldBattleGroup, ROM CF/5400, 512 bytes; :518) —
    per world-map sector, the rand-group index ($FF = a Veldt sector).
  * sub_battle_group.dat   (SubBattleGroup,   ROM CF/5600, 512 bytes; :522) —
    per map id, the rand-group index.
  * world_battle_rate.dat  (WorldBattleRate,  ROM CF/5800, 128 bytes; :526) —
    four 2-bit rate classes per world sector byte.
  * sub_battle_rate.dat    (SubBattleRate,    ROM CF/5880, 128 bytes; :530) —
    a 2-bit rate class per map (four maps per byte).

Inline field/battle.asm tables (parsed by label anchor + exact grammar):

  * WorldBattleBGTbl    (:219, 16 BATTLE_BG bytes) — battle background per world
    map sector slot.
  * BattleBGRateTbl     (:242, 8 bytes)  — which 2-bit field of the world rate
    byte each background reads.
  * BattleBGGroupTbl    (:246, 8 bytes)  — the bg-group offset each background
    adds when picking a world battle group.
  * WorldBattleRateTbl  (:251, 16 words) — per charm state, the 16-bit counter
    increment for each world rate class.
  * SubBattleRateTbl    (:259, 16 words) — per charm state, the increment for
    each map rate class.

Plus the BATTLE_BG enum (include/gfx/battle_bg.inc) → BattleBackgroundId, whose
values name the WorldBattleBGTbl entries. Formation words name their formation
via FormationId (reusing parse_formations' composition derivation), so a group
row reads FormationRef::of(FormationId::LOBO) rather than a bare index.

Structural guarantees, hard-errored at emit time:
  * each .dat is exactly its expected size (wrong length = wrong artifact);
  * every group formation word (low 15 bits) is a valid formation (< 576);
  * every 2-bit rate-class field is 0-2 (class 3 "no battles" is unused corpus-
    wide — the enum defines it for completeness, the data never selects it);
  * every inline directive matches the expected .byte/.word grammar and count;
  * every WorldBattleBGTbl symbol resolves to a real BATTLE_BG enumerator.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_encounters.py --source-root PATH --repo-root PATH
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
import parse_formations as pf
from common import ParseError

RAND_GROUPS = 256
RAND_WORDS_PER = 4
EVENT_GROUPS = 256
EVENT_WORDS_PER = 2
WORLD_GROUP_LEN = 512
SUB_GROUP_LEN = 512
WORLD_RATE_LEN = 128
SUB_RATE_LEN = 128
FORMATION_COUNT = 576
VELDT_SECTOR = 0xFF

_LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):$")
_BATTLE_BG_REF = re.compile(r"^BATTLE_BG::([A-Za-z_][A-Za-z0-9_]*)$")


# --- BATTLE_BG enum ----------------------------------------------------------

def read_battle_bg(inc_path):
    """The BATTLE_BG enumerators (name -> value) from include/gfx/battle_bg.inc.

    Uses the shared ca65 grammar; the file's `.scope BattleBG ... .endscope`
    helper blocks are skipped by common.parse_ca65_constants."""
    parsed = common.parse_ca65_constants(inc_path)
    enum = parsed.enum("BATTLE_BG")
    if enum is None:
        raise ParseError(inc_path, 0, "expected enum 'BATTLE_BG' not found")
    names = [(m.name, m.value) for m in enum.members]
    if names[0] != ("FIELD_WOB", 0):
        raise ParseError(inc_path, enum.src_line,
                         "BATTLE_BG[0] is {} = {}, expected FIELD_WOB = 0"
                         .format(names[0][0], names[0][1]))
    return names


# --- binary readers (structural asserts live here) ---------------------------

def _read_bytes(path, expected_len, label):
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) != expected_len:
        raise ParseError(path, 0,
                         "{} is {} bytes, expected {} (wrong artifact)"
                         .format(label, len(data), expected_len))
    return data


def read_group_words(path, groups, words_per, label):
    """A group table: `groups` rows of `words_per` little-endian formation
    words. Every word's low 15 bits must name a valid formation."""
    data = _read_bytes(path, groups * words_per * 2, label)
    rows = []
    for g in range(groups):
        row = []
        for w in range(words_per):
            off = (g * words_per + w) * 2
            word = data[off] | (data[off + 1] << 8)
            if (word & 0x7FFF) >= FORMATION_COUNT:
                raise ParseError(path, 0,
                                 "{} group {} slot {} references formation {} "
                                 ">= {}".format(label, g, w, word & 0x7FFF,
                                                FORMATION_COUNT))
            row.append(word)
        rows.append(row)
    return rows


def read_group_bytes(path, length, label):
    """A per-sector/per-map rand-group index table (raw bytes; $FF = Veldt for
    the world table, a plain index elsewhere)."""
    return list(_read_bytes(path, length, label))


def read_rate_bytes(path, length, label):
    """A packed rate table: each byte holds four 2-bit rate classes. Every
    field must be 0-2 (class 3 is unused corpus-wide)."""
    data = _read_bytes(path, length, label)
    for i, byte in enumerate(data):
        for field in range(4):
            cls = (byte >> (2 * field)) & 0x03
            if cls == 3:
                raise ParseError(path, 0,
                                 "{} byte {} field {} is rate class 3 "
                                 "(unused corpus-wide) — escalate"
                                 .format(label, i, field))
    return list(data)


# --- inline field/battle.asm tables ------------------------------------------

def _directive_block(path, lines, label, directive, count):
    """Collect exactly `count` comma-separated `directive` args from the block
    beginning at `label:`. Comment-only and blank lines inside the block are
    skipped; any other content before `count` args are gathered is a hard
    error (the block must be exactly the table it claims to be)."""
    start = None
    for i, raw in enumerate(lines):
        code, _ = common.strip_comment(raw)
        if code.strip() == label + ":":
            start = i + 1
            break
    if start is None:
        raise ParseError(path, 0, "inline table label '{}:' not found"
                         .format(label))
    args = []
    j = start
    while len(args) < count:
        if j >= len(lines):
            raise ParseError(path, start,
                             "{}: ran out of lines collecting {} {} args "
                             "(got {})".format(label, count, directive,
                                               len(args)))
        code, _ = common.strip_comment(lines[j])
        s = code.strip()
        j += 1
        if not s:
            continue
        if not s.startswith(directive):
            raise ParseError(path, j,
                             "{}: expected '{}' line, got {!r}"
                             .format(label, directive, s))
        for tok in s[len(directive):].split(","):
            args.append(tok.strip())
    if len(args) != count:
        raise ParseError(path, start,
                         "{}: expected {} args, gathered {}"
                         .format(label, count, len(args)))
    return args


def _int_tokens(path, tokens, label):
    out = []
    for tok in tokens:
        val = common.parse_int_literal(tok)
        if val is None:
            raise ParseError(path, 0, "{}: {!r} is not an integer literal"
                             .format(label, tok))
        out.append(val)
    return out


def read_inline_tables(path, bg_values):
    """Parse the five inline field/battle.asm tables. bg_values maps a BATTLE_BG
    enumerator name to its value (for WorldBattleBGTbl symbol resolution)."""
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    bg_tokens = _directive_block(path, lines, "WorldBattleBGTbl", ".byte", 16)
    world_bg = []  # list of (name, value)
    for tok in bg_tokens:
        m = _BATTLE_BG_REF.match(tok)
        if not m:
            raise ParseError(path, 0,
                             "WorldBattleBGTbl: {!r} is not a BATTLE_BG:: "
                             "reference".format(tok))
        name = m.group(1)
        if name not in bg_values:
            raise ParseError(path, 0,
                             "WorldBattleBGTbl references BATTLE_BG::{} which "
                             "is not a BATTLE_BG enumerator".format(name))
        world_bg.append((name, bg_values[name]))

    rate_slot = _int_tokens(
        path, _directive_block(path, lines, "BattleBGRateTbl", ".byte", 8),
        "BattleBGRateTbl")
    group_off = _int_tokens(
        path, _directive_block(path, lines, "BattleBGGroupTbl", ".byte", 8),
        "BattleBGGroupTbl")
    for i, v in enumerate(rate_slot):
        if v > 3:
            raise ParseError(path, 0, "BattleBGRateTbl[{}] = {} > 3".format(i, v))
    for i, v in enumerate(group_off):
        if v > 3:
            raise ParseError(path, 0, "BattleBGGroupTbl[{}] = {} > 3".format(i, v))

    world_rate = _int_tokens(
        path, _directive_block(path, lines, "WorldBattleRateTbl", ".word", 16),
        "WorldBattleRateTbl")
    sub_rate = _int_tokens(
        path, _directive_block(path, lines, "SubBattleRateTbl", ".word", 16),
        "SubBattleRateTbl")

    return {
        "world_bg": world_bg,        # 16 (name, value)
        "rate_slot": rate_slot,      # 8 ints
        "group_off": group_off,      # 8 ints
        "world_rate": world_rate,    # 16 words (4 charm states x 4 classes)
        "sub_rate": sub_rate,        # 16 words
    }


# --- rendering: battle_background_id.h ----------------------------------------

_BANNER = ("// AUTO-GENERATED by tools/asm_parser/parse_encounters.py — "
           "DO NOT EDIT.\n")


def render_battle_bg_h(names):
    width = "std::uint16_t" if max(v for _, v in names) > 0xFF \
        else "std::uint8_t"
    name_w = max(len(n) for n, _ in names)
    out = [_BANNER,
           "// Source: original-src/include/gfx/battle_bg.inc (ca65 .enum "
           "BATTLE_BG)\n"
           "// (original-src pinned at 1ea47b5)\n"
           "//\n"
           "// The battle background shown behind a formation. WorldBattleBGTbl\n"
           "// (src/data/generated/encounter_bg_tables_data.inc) selects one per\n"
           "// world-map sector slot; DEFAULT ($FF) is the loader's fallback.\n"
           "#pragma once\n\n"
           "#include <cstdint>\n\n"
           "namespace ostinato {\n\n",
           "enum class BattleBackgroundId : " + width + " {\n"]
    for name, value in names:
        out.append("    {}{} = 0x{:02X},\n".format(
            name, " " * (name_w - len(name)), value))
    out.append("};\n\n}  // namespace ostinato\n")
    return "".join(out)


# --- rendering: generated .inc files -----------------------------------------

_REGEN = ("//   python3 tools/asm_parser/parse_encounters.py \\\n"
          "//       --source-root  original-src\n")


def _inc_header(dat_label, rom, extra=""):
    return ("// AUTO-GENERATED by tools/asm_parser/parse_encounters.py\n"
            "// Source: src/field/{}.dat ({}, ROM {})\n"
            "{}"
            "// (original-src pinned at 1ea47b5)\n"
            "// DO NOT EDIT BY HAND — regenerate via:\n"
            "{}\n".format(dat_label, dat_label, rom, extra, _REGEN))


def render_group_words_inc(rows, names, entry_struct, record_struct,
                           dat_label, rom):
    """Render RandomBattleGroupEntry / EventBattleGroupEntry rows: each carries
    its group index as .index (identity is a typed field, never a comment), and
    each formation word names its formation through FormationRef::of."""
    out = [_inc_header(dat_label, rom,
                       "// Source: src/battle/battle_monsters.dat compositions "
                       "(FormationId)\n"),
           "// {} rows in group-index order, #included inside the matching\n"
           "// array in src/data/encounters.cpp. Each row carries its group\n"
           "// index as .index; each formation word names its formation via\n"
           "// FormationRef::of (bit 15 -> randomizePlus3).\n\n"
           .format(entry_struct)]
    for g, row in enumerate(rows):
        refs = ",\n            ".join(pf._ref(w, names) for w in row)
        out.append(
            "    {}{{  // [{}]\n"
            "        .index = {},\n"
            "        .record = {}{{ .formations = {{{{\n"
            "            {},\n"
            "        }}}} }},\n"
            "    }},\n".format(entry_struct, g, g, record_struct, refs))
    return "".join(out)


def render_index_value_inc(values, struct, dat_label, rom, value_note):
    """Render a flat per-index byte table (world/sub group + rate) as
    { .index = N, .value = 0xNN } rows, RNG-table shape."""
    out = [_inc_header(dat_label, rom),
           "// {} rows in index order, #included inside the matching array in\n"
           "// src/data/encounters.cpp. {} Identity is the .index field; a\n"
           "// compile-time assert verifies index == position.\n\n"
           .format(struct, value_note)]
    for i, v in enumerate(values):
        out.append("    {}{{ .index = {}, .value = {} }},\n".format(
            struct, i, v))
    return "".join(out)


def render_inline_tables_inc(inline):
    """Render the five inline field/battle.asm tables as one bundle of named
    constexpr definitions, #included at namespace scope in encounters.h."""
    out = ["// AUTO-GENERATED by tools/asm_parser/parse_encounters.py\n"
           "// Source: src/field/battle.asm (inline tables) + "
           "include/gfx/battle_bg.inc\n"
           "// (original-src pinned at 1ea47b5)\n"
           "// DO NOT EDIT BY HAND — regenerate via:\n"
           "{}\n".format(_REGEN)]

    # WorldBattleBGTbl — 2 worlds x 8 background slots.
    out.append(
        "// WorldBattleBGTbl (battle.asm:219): the battle background for each\n"
        "// of a world's 8 sector-slot values, indexed [world][slot].\n"
        "inline constexpr std::array<std::array<BattleBackgroundId, 8>, 2>\n"
        "    kWorldBattleBackgrounds = {{\n")
    for w, world_label in ((0, "world of balance"), (1, "world of ruin")):
        entries = ", ".join(
            "BattleBackgroundId::" + inline["world_bg"][w * 8 + i][0]
            for i in range(8))
        out.append("    {{ " + entries + " }},  // " + world_label + "\n")
    out.append("}};\n\n")

    # BattleBGRateTbl / BattleBGGroupTbl.
    out.append(
        "// BattleBGRateTbl (battle.asm:242): which 2-bit field of the world\n"
        "// rate byte each background reads (0-3).\n"
        "inline constexpr std::array<std::uint8_t, 8> kBattleBgRateSlot = {{ "
        + ", ".join(str(v) for v in inline["rate_slot"]) + " }};\n\n")
    out.append(
        "// BattleBGGroupTbl (battle.asm:246): the bg-group offset each\n"
        "// background adds when selecting a world battle group (0-3).\n"
        "inline constexpr std::array<std::uint8_t, 8> kBattleBgGroupOffset = {{ "
        + ", ".join(str(v) for v in inline["group_off"]) + " }};\n\n")

    # WorldBattleRateTbl / SubBattleRateTbl — 4 charm states x 4 rate classes.
    charm_labels = ("no charm", "charm bangle", "moogle charm", "unused")
    for name, key, comment in (
        ("kWorldBattleRateIncrements", "world_rate",
         "WorldBattleRateTbl (battle.asm:251): the 16-bit random-battle counter"),
        ("kSubBattleRateIncrements", "sub_rate",
         "SubBattleRateTbl (battle.asm:259): the 16-bit random-battle counter"),
    ):
        out.append(
            "// " + comment + " increment for each\n"
            "// rate class, indexed [CharmState].\n"
            "inline constexpr std::array<BattleRateIncrements, 4> " + name
            + " = {{\n")
        for c in range(4):
            vals = ", ".join(str(inline[key][c * 4 + r]) for r in range(4))
            out.append("    BattleRateIncrements{ .byRate = {{ " + vals
                       + " }} },  // " + charm_labels[c] + "\n")
        out.append("}};\n\n")

    return "".join(out)


# --- rendering: fixtures ------------------------------------------------------

def _fixture_banner():
    return (_BANNER +
            "// (original-src pinned at 1ea47b5) — DO NOT EDIT BY HAND.\n"
            "#pragma once\n\n"
            "#include <array>\n"
            "#include <cstdint>\n\n"
            "namespace ostinato::test {\n\n")


def render_group_words_fixture(rows, struct, array_name, words_per):
    out = [_fixture_banner(),
           "// Ground-truth {} words (.index decimal, .bytes raw little-endian "
           "ROM).\n".format(struct),
           "struct {} {{\n"
           "    std::uint16_t index;\n"
           "    std::array<std::uint8_t, {}> bytes;\n"
           "}};\n\n"
           "inline constexpr std::array<{}, {}> {} = {{{{\n".format(
               struct, words_per * 2, struct, len(rows), array_name)]
    for g, row in enumerate(rows):
        raw = []
        for w in row:
            raw.append(w & 0xFF)
            raw.append((w >> 8) & 0xFF)
        hexb = ", ".join("0x{:02X}".format(b) for b in raw)
        out.append("    {{ .index = {:>3}, .bytes = {{ {} }} }},\n".format(
            g, hexb))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


def render_index_value_fixture(values, struct, array_name, note):
    out = [_fixture_banner(),
           "// Ground-truth {} (.index decimal, .value raw ROM byte).\n".format(
               note),
           "struct {} {{\n"
           "    std::uint16_t index;\n"
           "    std::uint8_t value;\n"
           "}};\n\n"
           "inline constexpr std::array<{}, {}> {} = {{{{\n".format(
               struct, struct, len(values), array_name)]
    for i, v in enumerate(values):
        out.append("    {{ .index = {:>3}, .value = {} }},\n".format(i, v))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


def render_inline_tables_fixture(inline):
    out = [_fixture_banner(),
           "// Ground-truth inline field/battle.asm tables, raw values.\n"]

    def _arr(t, name, vals, fmt):
        return ("inline constexpr std::array<{}, {}> {} = {{{{ {} }}}};\n\n"
                .format(t, len(vals), name,
                        ", ".join(fmt.format(v) for v in vals)))

    out.append(_arr("std::uint8_t", "kExpectedWorldBattleBg",
                    [v for _, v in inline["world_bg"]], "0x{:02X}"))
    out.append(_arr("std::uint8_t", "kExpectedBattleBgRateSlot",
                    inline["rate_slot"], "{}"))
    out.append(_arr("std::uint8_t", "kExpectedBattleBgGroupOffset",
                    inline["group_off"], "{}"))
    out.append(_arr("std::uint16_t", "kExpectedWorldBattleRateIncrements",
                    inline["world_rate"], "{}"))
    out.append(_arr("std::uint16_t", "kExpectedSubBattleRateIncrements",
                    inline["sub_rate"], "{}"))
    out.append("}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver ------------------------------------------------------------------

def run(paths, outs, check_only=False):
    bg_names = read_battle_bg(paths["battle_bg_inc"])
    bg_values = {n: v for n, v in bg_names}

    symbols = pf.Symbols(paths["const_inc"], paths["monster_id_h"])
    formations = pf.read_formations(paths["formation_dat"])
    formation_names = pf.derive_formation_ids(formations, symbols.monster_names)

    rand = read_group_words(paths["rand_dat"], RAND_GROUPS, RAND_WORDS_PER,
                            "rand_battle_group")
    event = read_group_words(paths["event_dat"], EVENT_GROUPS, EVENT_WORDS_PER,
                             "event_battle_group")
    world_group = read_group_bytes(paths["world_group_dat"], WORLD_GROUP_LEN,
                                   "world_battle_group")
    sub_group = read_group_bytes(paths["sub_group_dat"], SUB_GROUP_LEN,
                                 "sub_battle_group")
    world_rate = read_rate_bytes(paths["world_rate_dat"], WORLD_RATE_LEN,
                                 "world_battle_rate")
    sub_rate = read_rate_bytes(paths["sub_rate_dat"], SUB_RATE_LEN,
                               "sub_battle_rate")
    inline = read_inline_tables(paths["battle_asm"], bg_values)

    if check_only:
        print("OK: BATTLE_BG {} names; rand {} event {} groups; world/sub group "
              "{}/{}; world/sub rate {}/{}; inline tables ok; all structural "
              "asserts pass.".format(len(bg_names), len(rand), len(event),
                                     len(world_group), len(sub_group),
                                     len(world_rate), len(sub_rate)))
        return 0

    _write(outs["battle_bg_h"], render_battle_bg_h(bg_names))
    _write(outs["rand_inc"], render_group_words_inc(
        rand, formation_names, "RandomBattleGroupEntry", "RandomBattleGroup",
        "rand_battle_group", "CF/4800"))
    _write(outs["event_inc"], render_group_words_inc(
        event, formation_names, "EventBattleGroupEntry", "EventBattleGroup",
        "event_battle_group", "CF/5000"))
    _write(outs["world_group_inc"], render_index_value_inc(
        world_group, "WorldBattleGroupEntry", "world_battle_group", "CF/5400",
        "Each value is a rand-group index ($FF = a Veldt sector)."))
    _write(outs["sub_group_inc"], render_index_value_inc(
        sub_group, "SubBattleGroupEntry", "sub_battle_group", "CF/5600",
        "Each value is the map's rand-group index."))
    _write(outs["world_rate_inc"], render_index_value_inc(
        world_rate, "WorldBattleRateEntry", "world_battle_rate", "CF/5800",
        "Each byte packs four 2-bit rate classes."))
    _write(outs["sub_rate_inc"], render_index_value_inc(
        sub_rate, "SubBattleRateEntry", "sub_battle_rate", "CF/5880",
        "Each byte packs four maps' 2-bit rate classes."))
    _write(outs["inline_inc"], render_inline_tables_inc(inline))

    _write(outs["rand_fixture"], render_group_words_fixture(
        rand, "ExpectedRandBattleGroup", "kExpectedRandBattleGroups", 4))
    _write(outs["event_fixture"], render_group_words_fixture(
        event, "ExpectedEventBattleGroup", "kExpectedEventBattleGroups", 2))
    _write(outs["world_group_fixture"], render_index_value_fixture(
        world_group, "ExpectedWorldBattleGroup", "kExpectedWorldBattleGroup",
        "world_battle_group bytes"))
    _write(outs["sub_group_fixture"], render_index_value_fixture(
        sub_group, "ExpectedSubBattleGroup", "kExpectedSubBattleGroup",
        "sub_battle_group bytes"))
    _write(outs["world_rate_fixture"], render_index_value_fixture(
        world_rate, "ExpectedWorldBattleRate", "kExpectedWorldBattleRate",
        "world_battle_rate bytes"))
    _write(outs["sub_rate_fixture"], render_index_value_fixture(
        sub_rate, "ExpectedSubBattleRate", "kExpectedSubBattleRate",
        "sub_battle_rate bytes"))
    _write(outs["inline_fixture"], render_inline_tables_fixture(inline))

    print("Emitted BattleBackgroundId ({} names) -> {}".format(
        len(bg_names), outs["battle_bg_h"]))
    print("Emitted encounter group/rate rows + inline tables + fixtures.")
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
                    help="disassembly root (.dat files, battle.asm, includes "
                         "resolve under it)")
    ap.add_argument("--repo-root", default=".",
                    help="repo root for output paths and monster_id.h")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    root = args.source_root or "original-src"
    src = os.path.join(root, "src")
    field = os.path.join(src, "field")
    repo = args.repo_root
    paths = {
        "rand_dat": os.path.join(field, "rand_battle_group.dat"),
        "event_dat": os.path.join(field, "event_battle_group.dat"),
        "world_group_dat": os.path.join(field, "world_battle_group.dat"),
        "sub_group_dat": os.path.join(field, "sub_battle_group.dat"),
        "world_rate_dat": os.path.join(field, "world_battle_rate.dat"),
        "sub_rate_dat": os.path.join(field, "sub_battle_rate.dat"),
        "battle_asm": os.path.join(field, "battle.asm"),
        "battle_bg_inc": os.path.join(root, "include", "gfx", "battle_bg.inc"),
        "const_inc": os.path.join(root, "include", "const.inc"),
        "formation_dat": os.path.join(src, "battle", "battle_monsters.dat"),
        "monster_id_h": os.path.join(repo, "include", "ostinato",
                                     "monster_id.h"),
    }
    gen = os.path.join(repo, "src", "data", "generated")
    fix = os.path.join(repo, "tests", "fixtures")
    outs = {
        "battle_bg_h": os.path.join(repo, "include", "ostinato",
                                    "battle_background_id.h"),
        "rand_inc": os.path.join(gen, "rand_battle_group_data.inc"),
        "event_inc": os.path.join(gen, "event_battle_group_data.inc"),
        "world_group_inc": os.path.join(gen, "world_battle_group_data.inc"),
        "sub_group_inc": os.path.join(gen, "sub_battle_group_data.inc"),
        "world_rate_inc": os.path.join(gen, "world_battle_rate_data.inc"),
        "sub_rate_inc": os.path.join(gen, "sub_battle_rate_data.inc"),
        "inline_inc": os.path.join(gen, "encounter_bg_tables_data.inc"),
        "rand_fixture": os.path.join(fix, "rand_battle_group_expected.h"),
        "event_fixture": os.path.join(fix, "event_battle_group_expected.h"),
        "world_group_fixture": os.path.join(
            fix, "world_battle_group_expected.h"),
        "sub_group_fixture": os.path.join(fix, "sub_battle_group_expected.h"),
        "world_rate_fixture": os.path.join(fix, "world_battle_rate_expected.h"),
        "sub_rate_fixture": os.path.join(fix, "sub_battle_rate_expected.h"),
        "inline_fixture": os.path.join(fix, "encounter_bg_tables_expected.h"),
    }
    try:
        return run(paths, outs, check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
