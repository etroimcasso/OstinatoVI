#!/usr/bin/env python3
"""Emit the formation-core tables from original-src.

Port-time tooling (NOT a build/CI dependency). Three sibling ROM tables define
what a battle is:

  * battle_monsters.dat (BattleMonsters, ROM CF/6200, 576 x 15 bytes;
    battle_main.asm:16446) — the monsters in each formation and where they
    stand. mon_data_get (btlgfx_main.asm:1990-2028) and InitMonsters
    (battle_main.asm:7672) name every byte.
  * battle_prop.dat (BattleProp, ROM CF/5900, 576 x 4 bytes;
    battle_main.asm:16442) — how each formation's battle begins. LoadBattleProp
    (battle_main.asm:7940) reads it.
  * cond_battle.dat (CondBattle, ROM CF/3780, 16 x 4 bytes;
    battle_main.asm:16456) — conditional-battle formation substitutions.

This script derives the FormationId enum from the formation compositions,
cross-checks its monster tokens against the shipped MonsterId enum, and emits
byte-identical generated rows + raw-byte fixtures for all three tables. It
recomputes the FormationId derivation on every run, so the names can never
silently drift from the corpus. Outputs:

  * include/ostinato/formation_id.h — the 576-enumerator FormationId enum.
  * src/data/generated/formation_data.inc — FormationEntry rows built through
    Formation::of(...) so each slot names its monster.
  * src/data/generated/formation_aux_data.inc — FormationAuxEntry rows built
    through FormationAux::of(...) so entrance/battle-types/flags/song are named.
  * src/data/generated/cond_battle_data.inc — ConditionalBattle rows built
    through FormationRef::of(FormationId::...).
  * tests/fixtures/{formation,formation_aux,cond_battle}_expected.h — the raw
    ROM bytes with decimal identity, the ground-truth side of the equivalence
    tests.

FormationId derivation (composition-based, mechanical):
  * A formation's name is its monsters in slot order, each the const.inc MONSTER
    symbol (the same source MonsterId was emitted from). A monster appearing n>1
    times is written NAME_Xn; tokens join in first-seen slot order
    (BLEARY_X2_CRAWLY, PIRANHA_X5_RIZOPAS). Empty slots ($1FF) are skipped;
    a zero-monster formation is UNUSED_<f>.
  * Formations that produce the same name get _2, _3, ... suffixes in ascending
    formation-index order (first occurrence unsuffixed): KEFKA, KEFKA_2, ...

Structural guarantees, hard-errored at emit time:
  * each .dat is exactly its expected size (wrong length = wrong artifact);
  * every formation slot id is a valid MonsterId (<= 383) or the $1FF empty
    sentinel; the bg1 mask and byte-14 high bits are zero corpus-wide;
  * aux byte-1 bits 0/4/5/6 are zero, and the character-AI index is zero
    whenever the AI-enable bit is clear;
  * every conditional-battle formation word (low 15 bits) is a valid formation;
  * the recomputed FormationId list is collision-free and its monster tokens
    match the shipped MonsterId enum.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_formations.py --source-root PATH [--formation-id-out FILE ...]
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
import parse_const_enums as pce
from common import ParseError

FORMATION_COUNT = 576
COND_COUNT = 16
MONSTER_COUNT = 384
EMPTY_SLOT = 0x1FF

# Mirror of include/ostinato/monster_entrance_type.h — the 18 entry-script
# names, formation nibble order. The formation aux byte selects one in 0..15.
ENTRANCE_NAMES = (
    "PRE_DRAWN",                      # 0
    "SMOKE",                          # 1
    "DROP_FROM_CEILING",              # 2
    "SLIDE_FROM_SIDES_INDIVIDUAL",    # 3
    "OUT_OF_WATER",                   # 4
    "TOP_SWIRL",                      # 5
    "RISE_FROM_BELOW",                # 6
    "SLIDE_FROM_SIDES_SYNCHRONIZED",  # 7
    "FADE_IN_FROM_TOP",               # 8
    "FADE_IN_FROM_BOTTOM",            # 9
    "FADE_IN_CHECKERED",              # 10
    "FADE_IN_DIAGONAL",               # 11
    "BOSS_DEATH",                     # 12
    "FLASH",                          # 13
    "LIGHT_AND_FLASHES",              # 14
    "FINAL_KEFKA_DESCENT",            # 15
    "UNUSED_16",                      # 16
    "FINAL_KEFKA_DEATH",              # 17
)

# Mirror of include/ostinato/battle_song.h — the 8 values of the 3-bit song
# field.
BATTLE_SONG_NAMES = (
    "BATTLE_THEME",         # 0
    "THE_DECISIVE_BATTLE",  # 1
    "THE_FIERCE_BATTLE",    # 2
    "RETURNERS",            # 3
    "SAVE_THEM",            # 4
    "DANCING_MAD",          # 5
    "NO_CHANGE_6",          # 6
    "NO_CHANGE_7",          # 7
)


class Symbols(object):
    """The const.inc MONSTER enum, cross-checked against the shipped MonsterId
    enum so the derived formation tokens can never diverge from what ships."""

    def __init__(self, const_inc, monster_id_h):
        parsed = common.parse_ca65_constants(const_inc, skip_body_enums=pce.SKIP)
        if parsed.enum("MONSTER") is None:
            raise ParseError(const_inc, 0, "expected enum 'MONSTER' not found")
        self.monster_names = {}
        for m in parsed.enum("MONSTER").members:
            self.monster_names.setdefault(m.value, m.name)
        if max(self.monster_names) != MONSTER_COUNT - 1:
            raise ParseError(const_inc, 0,
                             "MONSTER max id {} != {} (index-space mismatch)"
                             .format(max(self.monster_names), MONSTER_COUNT - 1))
        self._cross_check_monster_id(monster_id_h)

    def _cross_check_monster_id(self, monster_id_h):
        """Every MonsterId enumerator must equal the const.inc MONSTER name at
        the same id — the formation tokens resolve against MonsterId, so a
        divergence would emit a token that does not name a real monster."""
        shipped = {}
        with open(monster_id_h, "r", encoding="utf-8") as fh:
            for lineno, line in enumerate(fh, 1):
                m = re.match(r"\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"
                             r"0x([0-9A-Fa-f]+)\s*,", line)
                if m:
                    shipped.setdefault(int(m.group(2), 16), m.group(1))
        for value, name in self.monster_names.items():
            if shipped.get(value) != name:
                raise ParseError(monster_id_h, 0,
                                 "MonsterId at id {} is {!r} but const.inc "
                                 "MONSTER is {!r} — regenerate monster_id.h"
                                 .format(value, shipped.get(value), name))


# --- readers (structural asserts live here) --------------------------------

def read_formations(dat_path):
    """576 x 15-byte records with per-field range asserts."""
    with open(dat_path, "rb") as fh:
        data = fh.read()
    if len(data) != FORMATION_COUNT * 15:
        raise ParseError(dat_path, 0,
                         "{} bytes, expected {} (576 x 15 — wrong artifact)"
                         .format(len(data), FORMATION_COUNT * 15))
    records = []
    for f in range(FORMATION_COUNT):
        r = data[f * 15:f * 15 + 15]
        if r[0] & 0x0F or r[1] & 0xC0:
            raise ParseError(dat_path, 0,
                             "formation {} bg1 mask nonzero ({:#04x},{:#04x}) "
                             "— corpus-zero invariant broken; escalate"
                             .format(f, r[0], r[1]))
        if r[14] & 0xC0:
            raise ParseError(dat_path, 0,
                             "formation {} byte-14 bits 6-7 nonzero ({:#04x})"
                             .format(f, r[14]))
        if (r[0] >> 4) > 12:
            raise ParseError(dat_path, 0,
                             "formation {} VRAM map {} > 12".format(f, r[0] >> 4))
        for i in range(6):
            idv = (((r[14] >> i) & 1) << 8) | r[2 + i]
            if idv != EMPTY_SLOT and idv >= MONSTER_COUNT:
                raise ParseError(dat_path, 0,
                                 "formation {} slot {} id {:#05x} is neither a "
                                 "MonsterId (<{}) nor the $1FF empty sentinel"
                                 .format(f, i, idv, MONSTER_COUNT))
        records.append(r)
    return records


def read_aux(dat_path):
    """576 x 4-byte records with the unused-bit + character-AI asserts."""
    with open(dat_path, "rb") as fh:
        data = fh.read()
    if len(data) != FORMATION_COUNT * 4:
        raise ParseError(dat_path, 0,
                         "{} bytes, expected {} (576 x 4 — wrong artifact)"
                         .format(len(data), FORMATION_COUNT * 4))
    records = []
    for f in range(FORMATION_COUNT):
        r = data[f * 4:f * 4 + 4]
        if r[1] & 0x71:
            raise ParseError(dat_path, 0,
                             "formation {} aux byte-1 unused bits set "
                             "({:#04x} & 0x71)".format(f, r[1]))
        if not (r[1] & 0x80) and r[2] != 0:
            raise ParseError(dat_path, 0,
                             "formation {} has a character-AI index ({:#04x}) "
                             "but the AI-enable bit is clear".format(f, r[2]))
        records.append(r)
    return records


def read_cond(dat_path):
    """16 x (trigger word, replacement word)."""
    with open(dat_path, "rb") as fh:
        data = fh.read()
    if len(data) != COND_COUNT * 4:
        raise ParseError(dat_path, 0,
                         "{} bytes, expected {} (16 x 4 — wrong artifact)"
                         .format(len(data), COND_COUNT * 4))
    entries = []
    for i in range(COND_COUNT):
        trig = int.from_bytes(data[i * 4:i * 4 + 2], "little")
        repl = int.from_bytes(data[i * 4 + 2:i * 4 + 4], "little")
        for word in (trig, repl):
            if (word & 0x7FFF) >= FORMATION_COUNT:
                raise ParseError(dat_path, 0,
                                 "cond_battle entry {} references formation {} "
                                 ">= {}".format(i, word & 0x7FFF,
                                                FORMATION_COUNT))
        entries.append((trig, repl))
    return entries


# --- FormationId derivation --------------------------------------------------

def formation_slots(record):
    """The slot ids of a formation in slot order, empties dropped."""
    hi = record[14]
    out = []
    for i in range(6):
        idv = (((hi >> i) & 1) << 8) | record[2 + i]
        if idv != EMPTY_SLOT:
            out.append(idv)
    return out


def _base_name(record, monster_names):
    ids = formation_slots(record)
    if not ids:
        return None  # zero-monster; the caller names it UNUSED_<f>
    order = []
    counts = {}
    for idv in ids:
        if idv not in counts:
            order.append(idv)
            counts[idv] = 0
        counts[idv] += 1
    parts = []
    for idv in order:
        name = monster_names[idv]
        parts.append(name if counts[idv] == 1
                     else "{}_X{}".format(name, counts[idv]))
    return "_".join(parts)


def derive_formation_ids(records, monster_names):
    """The 576 FormationId enumerator names. Deterministic and collision-free
    (a collision is a hard error — the suffix rule is meant to prevent one)."""
    bases = []
    for f, record in enumerate(records):
        base = _base_name(record, monster_names)
        bases.append("UNUSED_{}".format(f) if base is None else base)
    groups = {}
    for f, base in enumerate(bases):
        groups.setdefault(base, []).append(f)
    names = [None] * len(records)
    for base, formations in groups.items():
        for rank, f in enumerate(formations):
            names[f] = base if rank == 0 else "{}_{}".format(base, rank + 1)
    if len(set(names)) != len(records):
        seen = {}
        for f, n in enumerate(names):
            if n in seen:
                raise ParseError("formation_id", 0,
                                 "FormationId collision: formations {} and {} "
                                 "both derive {!r}".format(seen[n], f, n))
            seen[n] = f
    for n in names:
        if not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", n):
            raise ParseError("formation_id", 0,
                             "derived name {!r} is not a valid identifier"
                             .format(n))
    return names


# --- rendering ---------------------------------------------------------------

_REGEN = (
    "//   python3 tools/asm_parser/parse_formations.py \\\n"
    "//       --source-root  original-src\n"
)


def _inc_header(dat_label, rom):
    return (
        "// AUTO-GENERATED by tools/asm_parser/parse_formations.py\n"
        "// Source: src/battle/{}.dat ({}, ROM {})\n"
        "// Source: include/const.inc (MONSTER values)\n"
        "// (original-src pinned at 1ea47b5)\n"
        "// DO NOT EDIT BY HAND — regenerate via:\n"
        "{}\n".format(dat_label, dat_label, rom, _REGEN))


def render_formation_id_h(names):
    lines = [
        "// AUTO-GENERATED by tools/asm_parser/parse_formations.py — "
        "DO NOT EDIT.\n"
        "// Source: src/battle/battle_monsters.dat compositions + "
        "include/const.inc (MONSTER)\n"
        "// (original-src pinned at 1ea47b5)\n"
        "//\n"
        "// Each formation is named for the monsters it contains, in slot\n"
        "// order (NAME_Xn for repeats); a zero-monster formation is\n"
        "// UNUSED_<index>, and formations that share a name are disambiguated\n"
        "// with _2, _3, ... in index order. The value is the formation index\n"
        "// (0-575).\n"
        "#pragma once\n\n"
        "#include <cstdint>\n\n"
        "namespace ostinato {\n\n"
        "enum class FormationId : std::uint16_t {\n"]
    width = max(len(n) for n in names)
    for f, name in enumerate(names):
        lines.append("    {}{} = {},\n".format(
            name, " " * (width - len(name)), f))
    lines.append("};\n\n"
                 "static_assert(sizeof(FormationId) == 2,\n"
                 "              \"FormationId indexes the 576-formation "
                 "space\");\n\n"
                 "}  // namespace ostinato\n")
    return "".join(lines)


def _slot_literal(record, slot, monster_names):
    hi = record[14]
    idv = (((hi >> slot) & 1) << 8) | record[2 + slot]
    x = record[8 + slot] >> 4
    y = record[8 + slot] & 0x0F
    present = bool((record[1] >> slot) & 1)
    if idv == EMPTY_SLOT:
        # Empty slot: no monster. Almost always fully default ({}); render any
        # nonzero position faithfully so of() round-trips it.
        if x == 0 and y == 0 and not present:
            return "{}"
        fields = []
        if x:
            fields.append(".x = {}".format(x))
        if y:
            fields.append(".y = {}".format(y))
        if present:
            fields.append(".present = true")
        return "{{ {} }}".format(", ".join(fields))
    return ("{{ .monster = MonsterId::{}, .x = {}, .y = {}, .present = {} }}"
            .format(monster_names[idv], x, y,
                    "true" if present else "false"))


def render_formation_inc(records, names, monster_names):
    out = [_inc_header("battle_monsters", "CF/6200"),
           "// FormationEntry rows in formation-index order, one per formation,\n"
           "// #included inside the kFormations array in\n"
           "// src/data/formations.cpp. Each row's identity is its .id field\n"
           "// (the FormationId enumerator); a compile-time assert verifies\n"
           "// id == position. Slots are built through Formation::of so each\n"
           "// names its monster; empty slots are {}.\n\n"]
    for f, record in enumerate(records):
        slots = ",\n            ".join(
            _slot_literal(record, i, monster_names) for i in range(6))
        out.append(
            "    FormationEntry{{  // [{}]\n"
            "        .id = FormationId::{},\n"
            "        .record = Formation::of({{\n"
            "            .vramMap = {},\n"
            "            .slots = {{{{\n"
            "            {},\n"
            "            }}}},\n"
            "        }}),\n"
            "    }},\n".format(f, names[f], record[0] >> 4, slots))
    return "".join(out)


def render_aux_inc(records, names):
    out = [_inc_header("battle_prop", "CF/5900"),
           "// FormationAuxEntry rows in formation-index order, #included\n"
           "// inside the kFormationAux array in src/data/formations.cpp. Rows\n"
           "// build through FormationAux::of so the entrance type, possible\n"
           "// battle types, flags, character-AI index, and battle song are\n"
           "// all named; the record stays byte-identical to the 4 ROM bytes.\n"
           "\n"]
    for f, r in enumerate(records):
        entrance = ENTRANCE_NAMES[r[0] & 0x0F]
        possible = (r[0] & 0xF0) ^ 0xF0
        song = BATTLE_SONG_NAMES[(r[3] >> 3) & 0x07]
        out.append(
            "    FormationAuxEntry{{ .id = FormationId::{}, "
            ".record = FormationAux::of({{\n"
            "        .entrance = MonsterEntranceType::{},\n"
            "        .frontPossible = {}, .backPossible = {}, "
            ".pincerPossible = {}, .sidePossible = {},\n"
            "        .fanfareDisabled = {}, .jokerDoomDisabled = {}, "
            ".leapDisabled = {}, .characterAiEnabled = {},\n"
            "        .characterAi = {},\n"
            "        .runningDisabled = {}, .veldtDisabled = {}, "
            ".preemptiveDisabled = {},\n"
            "        .song = BattleSong::{}, .continueCurrentMusic = {}, "
            ".unknownBit40 = {} }}) }},\n".format(
                names[f], entrance,
                _b(possible & 0x10), _b(possible & 0x20),
                _b(possible & 0x40), _b(possible & 0x80),
                _b(r[1] & 0x02), _b(r[1] & 0x04), _b(r[1] & 0x08),
                _b(r[1] & 0x80), r[2],
                _b(r[3] & 0x01), _b(r[3] & 0x02), _b(r[3] & 0x04),
                song, _b(r[3] & 0x80), _b(r[3] & 0x40)))
    return "".join(out)


def render_cond_inc(entries, names):
    out = [_inc_header("cond_battle", "CF/3780"),
           "// ConditionalBattle rows in entry order (0-15), #included inside\n"
           "// the kConditionalBattles array in src/data/formations.cpp. Each\n"
           "// formation word builds through FormationRef::of(FormationId::...)\n"
           "// so the trigger and replacement name their formations. Only\n"
           "// entries 0-7 are reachable in-game; 8-15 are dead ROM bytes.\n\n"]
    for i, (trig, repl) in enumerate(entries):
        out.append(
            "    ConditionalBattle{{ .trigger = {}, .replacement = {} }},"
            "  // [{}]\n".format(_ref(trig, names), _ref(repl, names), i))
    return "".join(out)


def _ref(word, names):
    fid = names[word & 0x7FFF]
    if word & 0x8000:
        return "FormationRef::of(FormationId::{}, /*randomizePlus3=*/true)" \
            .format(fid)
    return "FormationRef::of(FormationId::{})".format(fid)


def _b(flag):
    return "true" if flag else "false"


# --- fixtures ----------------------------------------------------------------

def _fixture_head(struct_name, array_name, fields, count, note):
    return (
        "// AUTO-GENERATED by tools/asm_parser/parse_formations.py\n"
        "// {}\n"
        "// (original-src pinned at 1ea47b5) — DO NOT EDIT BY HAND.\n"
        "#pragma once\n\n"
        "#include <array>\n"
        "#include <cstdint>\n\n"
        "namespace ostinato::test {{\n\n"
        "struct {} {{\n{}}};\n\n"
        "inline constexpr std::array<{}, {}> {} = {{{{\n".format(
            note, struct_name, fields, struct_name, count, array_name))


def render_formation_fixture(records):
    fields = ("    std::uint16_t id;\n"
              "    std::array<std::uint8_t, 15> bytes;\n")
    out = [_fixture_head(
        "ExpectedFormation", "kExpectedFormations", fields, len(records),
        "Ground-truth battle_monsters bytes (.id decimal, .bytes raw ROM).")]
    for f, r in enumerate(records):
        hexb = ", ".join("0x{:02X}".format(b) for b in r)
        out.append("    {{ .id = {:>3}, .bytes = {{ {} }} }},\n".format(f, hexb))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


def render_aux_fixture(records):
    fields = ("    std::uint16_t id;\n"
              "    std::array<std::uint8_t, 4> bytes;\n")
    out = [_fixture_head(
        "ExpectedFormationAux", "kExpectedFormationAux", fields, len(records),
        "Ground-truth battle_prop bytes (.id decimal, .bytes raw ROM).")]
    for f, r in enumerate(records):
        hexb = ", ".join("0x{:02X}".format(b) for b in r)
        out.append("    {{ .id = {:>3}, .bytes = {{ {} }} }},\n".format(f, hexb))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


def render_cond_fixture(entries):
    fields = ("    std::uint16_t index;\n"
              "    std::uint16_t trigger;\n"
              "    std::uint16_t replacement;\n")
    out = [_fixture_head(
        "ExpectedConditionalBattle", "kExpectedConditionalBattles", fields,
        len(entries),
        "Ground-truth cond_battle words (.index decimal, raw formation "
        "words).")]
    for i, (trig, repl) in enumerate(entries):
        out.append("    {{ .index = {:>2}, .trigger = 0x{:04X}, "
                   ".replacement = 0x{:04X} }},\n".format(i, trig, repl))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver ------------------------------------------------------------------

def run(paths, outs, check_only=False):
    symbols = Symbols(paths["const_inc"], paths["monster_id_h"])
    formations = read_formations(paths["formation_dat"])
    aux = read_aux(paths["aux_dat"])
    cond = read_cond(paths["cond_dat"])
    names = derive_formation_ids(formations, symbols.monster_names)

    if check_only:
        print("OK: {} formations, {} aux, {} cond; {} unique FormationId names; "
              "all structural asserts pass.".format(
                  len(formations), len(aux), len(cond), len(set(names))))
        return 0

    _write(outs["formation_id_h"], render_formation_id_h(names))
    _write(outs["formation_inc"],
           render_formation_inc(formations, names, symbols.monster_names))
    _write(outs["aux_inc"], render_aux_inc(aux, names))
    _write(outs["cond_inc"], render_cond_inc(cond, names))
    _write(outs["formation_fixture"], render_formation_fixture(formations))
    _write(outs["aux_fixture"], render_aux_fixture(aux))
    _write(outs["cond_fixture"], render_cond_fixture(cond))
    print("Emitted FormationId ({} names) -> {}".format(
        len(names), outs["formation_id_h"]))
    print("Emitted formation/aux/cond rows + fixtures.")
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
                    help="disassembly root (the .dat files and const.inc "
                         "resolve under it)")
    ap.add_argument("--repo-root", default=".",
                    help="repo root for default output paths and monster_id.h")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    root = args.source_root or "original-src"
    src = os.path.join(root, "src")
    repo = args.repo_root
    paths = {
        "formation_dat": os.path.join(src, "battle", "battle_monsters.dat"),
        "aux_dat": os.path.join(src, "battle", "battle_prop.dat"),
        "cond_dat": os.path.join(src, "battle", "cond_battle.dat"),
        "const_inc": os.path.join(root, "include", "const.inc"),
        "monster_id_h": os.path.join(repo, "include", "ostinato",
                                     "monster_id.h"),
    }
    gen = os.path.join(repo, "src", "data", "generated")
    fix = os.path.join(repo, "tests", "fixtures")
    outs = {
        "formation_id_h": os.path.join(repo, "include", "ostinato",
                                       "formation_id.h"),
        "formation_inc": os.path.join(gen, "formation_data.inc"),
        "aux_inc": os.path.join(gen, "formation_aux_data.inc"),
        "cond_inc": os.path.join(gen, "cond_battle_data.inc"),
        "formation_fixture": os.path.join(fix, "formation_expected.h"),
        "aux_fixture": os.path.join(fix, "formation_aux_expected.h"),
        "cond_fixture": os.path.join(fix, "cond_battle_expected.h"),
    }
    try:
        return run(paths, outs, check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
