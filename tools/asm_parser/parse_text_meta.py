#!/usr/bin/env python3
"""Emit the FF6 text-corpus structural metadata from original-src.

Port-time tooling (NOT a build/CI dependency). Every FF6 text class ships as a
raw byte `.dat` file the game reads at runtime; to slice a `.dat` into records
the loader needs each class's structure — how many records, and (for the
fixed-length classes) how many bytes per record. Those numbers live in the
rip-generated ca65 include files under original-src/include/text/*.inc:

    .scope <Label>
        ARRAY_LENGTH = <N>          ; record count (every non-skeleton class)
        ITEM_SIZE    = <M>          ; bytes/record  (fixed-length classes only)
        _0 := <Label> + $0000       ; per-record offset (pointer classes only)
        _1 := <Label> + $0059
        ...
    .endscope

A class is FIXED-length when its scope carries an ITEM_SIZE (padded records, no
terminator); it is POINTER-indexed when the scope instead carries one `_N :=`
offset symbol per record (variable-length records located by an offset table).
This script reads the structure straight off disk and emits:

  * src/data/generated/text_metadata_data.inc — one designated-initializer
    TextClassMetadata row per class (identity as the TextClass enumerator, kind
    as TextClassKind, count/size as decimal magnitudes); the kTextClassMetadata
    array #includes it.
  * tests/fixtures/text_metadata_expected.h — the same rows with decimal
    identity for a full-corpus equivalence test.
  * src/data/generated/text_offsets_data.inc — one constexpr std::uint32_t
    array per POINTER class of per-record byte offsets. dlg1 + dlg2 collapse
    into ONE combined array of offsets into the concatenated dlg1+dlg2 byte
    stream (dlg2's offsets shifted by len(dlg1_en.dat)); the ROM's u16 pointer
    + DlgBankInc bank-increment shape never enters the surface.
  * tests/fixtures/text_offsets_expected.h — an independent copy of every
    offset array for a full-corpus drift test.

The record COUNTS and SIZES are read from the `.inc` files (never hand-typed);
only the per-class kind + language availability + the TextClass enumerator
mapping are port-design knowledge encoded in the registry below. The parser
hard-errors on any structural surprise (a FIXED class with offset symbols, a
POINTER class whose offset count disagrees with ARRAY_LENGTH, an EN class that
ripped as a skeleton, an unexpected kind) so a grammar or rip change escalates
rather than silently emitting wrong structure.

The DTE char table (dte_tbl_en.dat) has no `.inc` — its structure is fixed by
the format: 128 entries (one per DTE code $80..$ff) of 2 glyph bytes each. That
structural pair is validated against the real `.dat` size when present.

JP is a U-ROM skeleton rip: every JP name/description `.inc` is an empty scope
(no ARRAY_LENGTH) except mte_tbl_jp (the one real ripped JP table). The parser
verifies that skeleton shape structurally — a JP file that unexpectedly carries
metadata means a J-ROM rip landed and the JP surface must be built, so it is a
hard error here rather than a silent pass.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_text_meta.py --source-root PATH \
        --inc-out FILE --fixture-out FILE \
        --offsets-out FILE --offsets-fixture-out FILE
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

import common
from common import ParseError

# --- the class registry ------------------------------------------------------
# (file_basename, TextClass enumerator, kind, en_only)
# Order here IS the TextClass enum order and the emitted-row order. Counts and
# sizes are NOT listed — they are read from the .inc files.

FIXED = "FIXED"
POINTER = "POINTER"

_REGISTRY = [
    # Fixed-length name tables (padded records, ITEM_SIZE bytes each).
    ("char_name",            "CHAR_NAME",            FIXED,   False),
    ("item_name",            "ITEM_NAME",            FIXED,   False),
    ("magic_name",           "MAGIC_NAME",           FIXED,   False),
    ("attack_name",          "ATTACK_NAME",          FIXED,   False),
    ("monster_name",         "MONSTER_NAME",         FIXED,   False),
    ("monster_special_name", "MONSTER_SPECIAL_NAME", FIXED,   False),
    ("status_name",          "STATUS_NAME",          FIXED,   False),
    ("genju_name",           "GENJU_NAME",           FIXED,   False),
    ("genju_attack_name",    "GENJU_ATTACK_NAME",    FIXED,   False),
    ("genju_bonus_name",     "GENJU_BONUS_NAME",     FIXED,   False),
    ("dance_name",           "DANCE_NAME",           FIXED,   False),
    ("bushido_name",         "BUSHIDO_NAME",         FIXED,   False),
    ("battle_cmd_name",      "BATTLE_CMD_NAME",      FIXED,   False),
    ("item_type_name",       "ITEM_TYPE_NAME",       FIXED,   True),   # EN-only
    ("rare_item_name",       "RARE_ITEM_NAME",       FIXED,   False),
    # Pointer-indexed variable-length tables (offset table emitted later).
    ("dlg1",                 "DLG1",                 POINTER, False),
    ("dlg2",                 "DLG2",                 POINTER, False),
    ("attack_msg",           "ATTACK_MSG",           POINTER, False),
    ("battle_dlg",           "BATTLE_DLG",           POINTER, False),
    ("monster_dlg",          "MONSTER_DLG",          POINTER, False),
    ("map_title",            "MAP_TITLE",            POINTER, False),
    ("item_desc",            "ITEM_DESC",            POINTER, False),
    ("magic_desc",           "MAGIC_DESC",           POINTER, False),
    ("lore_desc",            "LORE_DESC",            POINTER, False),
    ("blitz_desc",           "BLITZ_DESC",           POINTER, False),
    ("bushido_desc",         "BUSHIDO_DESC",         POINTER, False),
    ("genju_attack_desc",    "GENJU_ATTACK_DESC",    POINTER, False),
    ("genju_bonus_desc",     "GENJU_BONUS_DESC",     POINTER, False),
    ("rare_item_desc",       "RARE_ITEM_DESC",       POINTER, False),
]

# The DTE char table has no .inc: its structure is format-fixed (128 codes
# $80..$ff, 2 glyph bytes per expansion). Emitted as a FIXED row after the
# registry classes; validated against the real .dat size when present.
_DTE_BASENAME = "dte_tbl"
_DTE_ENUM = "DTE_TABLE"
_DTE_RECORD_COUNT = 128
_DTE_RECORD_SIZE = 2

# JP files expected to be real (ripped) despite the U-ROM skeleton: only the
# MTE table. Every other JP name/desc .inc must be a skeleton.
_JP_REAL = {"mte_tbl"}
_MTE_EXPECTED_LEN = 24

# A pointer-class record offset symbol: `_N := <Label> + $HHHH`. The first
# regex detects the line; the second pins the full grammar so a malformed offset
# hard-errors rather than being silently miscounted.
_RE_OFFSET_DETECT = re.compile(r"^_(\d+)\s*:=")
_RE_OFFSET_FULL = re.compile(
    r"^_(\d+)\s*:=\s*[A-Za-z0-9_]+\s*\+\s*\$([0-9a-fA-F]+)\s*$")


class ClassMeta(object):
    """Structure read from one class .inc (single language)."""

    __slots__ = ("array_length", "item_size", "offsets", "has_metadata")

    def __init__(self, array_length, item_size, offsets, has_metadata):
        self.array_length = array_length
        self.item_size = item_size
        # offsets: {record index -> byte offset into the class's own stream},
        # captured from the `_N :=` symbols (empty for fixed-length classes).
        self.offsets = offsets
        self.has_metadata = has_metadata

    @property
    def offset_count(self):
        return len(self.offsets)


def read_class_inc(path):
    """Read a class .inc into a ClassMeta.

    Captures ARRAY_LENGTH / ITEM_SIZE assignments and counts `_N :=` offset
    symbols inside the (single) `.scope`. A `Start := bank_start` line and the
    outer include-guard assignment are ignored. A scope with no ARRAY_LENGTH is
    a skeleton (has_metadata False) — the JP U-ROM rip shape.
    """
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    scope_depth = 0
    array_length = None
    item_size = None
    offsets = {}

    for idx, raw in enumerate(lines):
        code, _comment = common.strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        low = s.lower()
        if low.startswith(".scope"):
            scope_depth += 1
            continue
        if low.startswith(".endscope"):
            if scope_depth == 0:
                raise ParseError(path, idx + 1, ".endscope without .scope")
            scope_depth -= 1
            continue
        if scope_depth == 0:
            continue  # outside the scope: guards, .global, .list

        # inside the scope
        if _RE_OFFSET_DETECT.match(s):
            full = _RE_OFFSET_FULL.match(s)
            if not full:
                raise ParseError(path, idx + 1,
                                 "malformed offset symbol: {!r}".format(s))
            n = int(full.group(1))
            if n in offsets:
                raise ParseError(path, idx + 1,
                                 "duplicate offset symbol _{}".format(n))
            offsets[n] = int(full.group(2), 16)
            continue
        if ":=" in s:
            continue  # Start := bank_start <Label> — not a record symbol
        if "=" in s:
            name, _sep, rhs = s.partition("=")
            name = name.strip()
            rhs = rhs.strip()
            if name == "ARRAY_LENGTH":
                array_length = common.parse_int_literal(rhs)
                if array_length is None:
                    raise ParseError(path, idx + 1,
                                     "malformed ARRAY_LENGTH {!r}".format(rhs))
            elif name == "ITEM_SIZE":
                item_size = common.parse_int_literal(rhs)
                if item_size is None:
                    raise ParseError(path, idx + 1,
                                     "malformed ITEM_SIZE {!r}".format(rhs))
            # any other scope-local assignment is ignored on purpose
            continue
        # unexpected non-assignment, non-symbol line inside the scope
        raise ParseError(path, idx + 1,
                         "unexpected line inside scope: {!r}".format(s))

    if scope_depth != 0:
        raise ParseError(path, len(lines), "unterminated .scope (no .endscope)")

    has_metadata = array_length is not None
    return ClassMeta(array_length, item_size, offsets, has_metadata)


class Row(object):
    """One emitted metadata row: identity, on-disk file stem, kind, count, size.

    For POINTER classes .offsets is the ordered list of per-record byte offsets
    into the class's own stream (index 0..record_count-1); None for FIXED.
    """

    __slots__ = ("enum", "stem", "kind", "record_count", "record_size",
                 "offsets")

    def __init__(self, enum, stem, kind, record_count, record_size,
                 offsets=None):
        self.enum = enum
        self.stem = stem
        self.kind = kind
        self.record_count = record_count
        self.record_size = record_size
        self.offsets = offsets


def row_for_class(basename, enum, kind, meta, path):
    """Validate one class's ClassMeta against its expected kind and build a Row.

    Raises ParseError on any structural deviation: a skeleton EN class, a FIXED
    class missing ITEM_SIZE or carrying offset symbols, a POINTER class carrying
    an ITEM_SIZE or whose offset count disagrees with ARRAY_LENGTH.
    """
    if not meta.has_metadata:
        raise ParseError(path, 0,
                         "EN class '{}' ripped as a skeleton (no ARRAY_LENGTH) "
                         "— rip missing?".format(basename))
    if kind == FIXED:
        if meta.item_size is None:
            raise ParseError(path, 0,
                             "FIXED class '{}' has no ITEM_SIZE".format(basename))
        if meta.offset_count != 0:
            raise ParseError(path, 0,
                             "FIXED class '{}' unexpectedly carries {} offset "
                             "symbols".format(basename, meta.offset_count))
        return Row(enum, basename, kind, meta.array_length, meta.item_size)
    # POINTER
    if meta.item_size is not None:
        raise ParseError(path, 0,
                         "POINTER class '{}' unexpectedly carries an ITEM_SIZE"
                         .format(basename))
    if meta.offset_count != meta.array_length:
        raise ParseError(path, 0,
                         "POINTER class '{}' offset count {} != ARRAY_LENGTH {}"
                         .format(basename, meta.offset_count, meta.array_length))
    ordered = _ordered_offsets(meta.offsets, meta.array_length, basename, path)
    return Row(enum, basename, kind, meta.array_length, 0, offsets=ordered)


def _ordered_offsets(offsets, count, basename, path):
    """Flatten the {index -> offset} map into a dense list 0..count-1.

    Hard-errors on a gap (a missing `_N`) or a non-decreasing violation — the
    records are laid out sequentially in the `.dat`, so each offset must be >=
    the previous (equal when a record is zero-length, e.g. dlg1's `_0`/`_1`).
    """
    ordered = []
    prev = 0
    for n in range(count):
        if n not in offsets:
            raise ParseError(path, 0,
                             "POINTER class '{}' missing offset _{}"
                             .format(basename, n))
        off = offsets[n]
        if off < prev:
            raise ParseError(path, 0,
                             "POINTER class '{}' offset _{} = {} decreases "
                             "below previous {}".format(basename, n, off, prev))
        ordered.append(off)
        prev = off
    return ordered


def build_rows(text_inc_dir, src_text_dir=None):
    """Validate every class and return the ordered list of emitted Rows.

    Reads each registry class's EN .inc, checks it against the expected kind,
    and reads its count (+ size for fixed). Then appends the DTE row. Raises
    ParseError on any structural deviation.
    """
    rows = []
    for basename, enum, kind, _en_only in _REGISTRY:
        path = os.path.join(text_inc_dir, "{}_en.inc".format(basename))
        if not os.path.isfile(path):
            raise ParseError(path, 0,
                             "EN include missing for class '{}'".format(basename))
        meta = read_class_inc(path)
        rows.append(row_for_class(basename, enum, kind, meta, path))

    # DTE char table: structure is format-fixed; validate against .dat size.
    if src_text_dir is not None:
        dat = os.path.join(src_text_dir, "{}_en.dat".format(_DTE_BASENAME))
        if os.path.isfile(dat):
            size = os.path.getsize(dat)
            expected = _DTE_RECORD_COUNT * _DTE_RECORD_SIZE
            if size != expected:
                raise ParseError(dat, 0,
                                 "dte_tbl_en.dat is {} bytes, expected {} "
                                 "({}x{})".format(size, expected,
                                                  _DTE_RECORD_COUNT,
                                                  _DTE_RECORD_SIZE))
    rows.append(Row(_DTE_ENUM, _DTE_BASENAME, FIXED, _DTE_RECORD_COUNT,
                    _DTE_RECORD_SIZE))
    return rows


def verify_jp_skeletons(text_inc_dir):
    """Structurally confirm the JP U-ROM skeleton shape.

    Every JP name/description .inc must be a skeleton (no ARRAY_LENGTH) except
    the MTE table, which is a real ripped POINTER table of _MTE_EXPECTED_LEN
    records. A JP file that violates this means a J-ROM rip landed and the JP
    surface must be built — a hard error, not a silent pass. Returns the number
    of skeleton files verified.
    """
    skeletons = 0
    for basename, _enum, _kind, en_only in _REGISTRY:
        if en_only:
            continue
        path = os.path.join(text_inc_dir, "{}_jp.inc".format(basename))
        if not os.path.isfile(path):
            continue  # some classes have no JP variant at all
        meta = read_class_inc(path)
        if meta.has_metadata:
            raise ParseError(path, 0,
                             "JP class '{}' unexpectedly carries metadata "
                             "(J-ROM rip landed? build the JP surface)"
                             .format(basename))
        skeletons += 1

    # The one real JP table.
    mte = os.path.join(text_inc_dir, "mte_tbl_jp.inc")
    if os.path.isfile(mte):
        meta = read_class_inc(mte)
        if not meta.has_metadata or meta.array_length != _MTE_EXPECTED_LEN:
            raise ParseError(mte, 0,
                             "mte_tbl_jp expected {} records, got {}"
                             .format(_MTE_EXPECTED_LEN, meta.array_length))
    return skeletons


# --- offset tables -----------------------------------------------------------

_DLG1_DAT = "dlg1_en.dat"


class OffsetTable(object):
    """One emitted offset array: C++ name, the offsets, a human note."""

    __slots__ = ("cpp_name", "offsets", "note")

    def __init__(self, cpp_name, offsets, note):
        self.cpp_name = cpp_name
        self.offsets = offsets
        self.note = note

    @property
    def count(self):
        return len(self.offsets)


def _camel(stem):
    return "".join(part.capitalize() for part in stem.split("_"))


def _assert_non_decreasing(offsets, name):
    prev = -1
    for i, off in enumerate(offsets):
        if off < prev:
            raise ParseError("<offsets>", 0,
                             "{} offset[{}] = {} decreases below previous {}"
                             .format(name, i, off, prev))
        prev = off


def build_offset_tables(rows, src_text_dir):
    """Build the emitted offset arrays from the pointer-class Rows.

    dlg1 + dlg2 collapse into ONE combined array of u32 offsets into the
    concatenated dlg1+dlg2 byte stream (D4): dlg1's own offsets, then dlg2's
    offsets each shifted by len(dlg1_en.dat) so record index >= dlg1 count
    addresses into the dlg2 region. The ROM's u16 pointer + DlgBankInc
    bank-increment mechanism never enters the surface. Every other pointer
    class gets a plain array of offsets into its own `.dat`.
    """
    by_stem = {r.stem: r for r in rows if r.kind == POINTER}
    dlg1 = by_stem.get("dlg1")
    dlg2 = by_stem.get("dlg2")
    if dlg1 is None or dlg2 is None:
        raise ParseError("<offsets>", 0,
                         "dialogue offset build requires dlg1 and dlg2 rows")
    if src_text_dir is None:
        raise ParseError("<offsets>", 0,
                         "dialogue offsets need --src-text-dir (dlg1_en.dat "
                         "size is the concatenation base)")
    dlg1_dat = os.path.join(src_text_dir, _DLG1_DAT)
    if not os.path.isfile(dlg1_dat):
        raise ParseError(dlg1_dat, 0,
                         "dlg1_en.dat missing — needed for the concatenated "
                         "dialogue offset base")
    dlg1_len = os.path.getsize(dlg1_dat)
    if dlg2.offsets[0] != 0:
        raise ParseError(dlg1_dat, 0,
                         "dlg2 _0 offset expected 0, got {}"
                         .format(dlg2.offsets[0]))

    combined = list(dlg1.offsets) + [dlg1_len + o for o in dlg2.offsets]
    if combined[0] != 0 or combined[1] != 0:
        raise ParseError("<offsets>", 0,
                         "dialogue _0/_1 duplicate ($0000) not preserved: "
                         "{} / {}".format(combined[0], combined[1]))
    _assert_non_decreasing(combined, "Dialogue")
    expected = dlg1.record_count + dlg2.record_count
    if len(combined) != expected:
        raise ParseError("<offsets>", 0,
                         "dialogue offset count {} != dlg1 {} + dlg2 {}".format(
                             len(combined), dlg1.record_count,
                             dlg2.record_count))

    tables = [OffsetTable(
        "kDialogueOffsets", combined,
        "concatenated dlg1+dlg2 stream; index < {} addresses dlg1_en.dat "
        "(len {}), the rest dlg2_en.dat".format(dlg1.record_count, dlg1_len))]

    for r in rows:
        if r.kind != POINTER or r.stem in ("dlg1", "dlg2"):
            continue
        _assert_non_decreasing(r.offsets, r.stem)
        tables.append(OffsetTable(
            "k{}Offsets".format(_camel(r.stem)), list(r.offsets),
            "{} records into {}_en.dat".format(r.record_count, r.stem)))
    return tables


# --- menu-description decode cross-check -------------------------------------
# The eight null-terminated description classes are cross-checked against the
# upstream decoded reference text (`<stem>_en.json` "text" array): each record
# is sliced from its `.dat`, decoded through the EN char tables, and — for the
# records whose bytes are all unambiguous — asserted equal to the upstream
# string. This proves the shipped `.dat` decodes to the real game text. The
# per-record expected string + an "unambiguous" flag + the glyph char map are
# emitted for the C++ codec test to repeat the decode against its own tokenizer.

_MENU_DESC_STEMS = [
    "item_desc", "magic_desc", "lore_desc", "blitz_desc", "bushido_desc",
    "genju_attack_desc", "genju_bonus_desc", "rare_item_desc",
]

# EN char tables a description record draws from (null_terminated first so its
# control glyphs — {n} newline, {pad}, space — take precedence).
_MENU_DESC_CHAR_TABLES = ["null_terminated_en", "text_en", "big_symbols_en"]


def load_glyph_map(char_table_dir):
    """Merge the EN description char tables into byte -> str + an ambiguous set.

    A char-table entry whose value is a LIST means the upstream decoder had
    several candidate glyphs for that byte — it cannot be rendered to a single
    string, so its byte is 'ambiguous' and any record using it is excluded from
    the exact cross-check. Single-valued entries form the decode map.
    """
    single = {}
    ambiguous = set()
    for name in _MENU_DESC_CHAR_TABLES:
        path = os.path.join(char_table_dir, "{}.json".format(name))
        if not os.path.isfile(path):
            raise ParseError(path, 0, "char table '{}' missing".format(name))
        with open(path, "r", encoding="utf-8") as fh:
            table = json.load(fh)
        for key, val in table.items():
            byte = int(key, 16)
            if isinstance(val, list):
                ambiguous.add(byte)
            else:
                single[byte] = val
    return single, ambiguous


def decode_menu_record(record, single, ambiguous):
    """Decode one description record to (string, unambiguous, terminated).

    Stops at the 0x00 terminator. `unambiguous` is False if any byte is
    list-valued or is not mapped by any table. `terminated` is True if a 0x00
    was reached inside the slice — a record whose slice ends without one was
    truncated by an overlapping (shared) pointer and is not independently
    decodable.
    """
    out = []
    ok = True
    terminated = False
    for b in record:
        if b == 0x00:
            terminated = True
            break
        if b in ambiguous:
            ok = False
        elif b in single:
            out.append(single[b])
        else:
            ok = False
    return "".join(out), ok, terminated


class MenuDescClass(object):
    """One description class's per-record cross-check results."""

    __slots__ = ("stem", "cpp_name", "records")

    def __init__(self, stem, cpp_name, records):
        self.stem = stem
        self.cpp_name = cpp_name
        # records: list of (upstream_text, unambiguous)
        self.records = records


def _read_json_text(path):
    with open(path, "r", encoding="utf-8") as fh:
        doc = json.load(fh)
    if "text" not in doc or not isinstance(doc["text"], list):
        raise ParseError(path, 0, "no 'text' array in {}".format(path))
    return doc["text"]


def build_menu_desc(rows, src_text_dir, char_table_dir):
    """Cross-check every description class's `.dat` decode against its `.json`.

    Returns (single_map, ambiguous_set, [MenuDescClass, ...]). Raises ParseError
    if an unambiguous record's decode disagrees with the upstream text (a real
    contract break — the char tables, the `.dat`, and the reference `.json` must
    agree) or if a class's record count and reference-text length differ.
    """
    if src_text_dir is None:
        raise ParseError("<menu-desc>", 0, "menu-desc cross-check needs "
                         "--src-text-dir (the `.dat` + `.json` corpus)")
    single, ambiguous = load_glyph_map(char_table_dir)
    by_stem = {r.stem: r for r in rows if r.kind == POINTER}
    classes = []
    for stem in _MENU_DESC_STEMS:
        row = by_stem.get(stem)
        if row is None:
            raise ParseError("<menu-desc>", 0,
                             "no pointer row for '{}'".format(stem))
        dat_path = os.path.join(src_text_dir, "{}_en.dat".format(stem))
        json_path = os.path.join(src_text_dir, "{}_en.json".format(stem))
        with open(dat_path, "rb") as fh:
            dat = fh.read()
        text_array = _read_json_text(json_path)
        if len(text_array) != row.record_count:
            raise ParseError(json_path, 0,
                             "'{}' reference text has {} entries, "
                             "ARRAY_LENGTH is {}".format(
                                 stem, len(text_array), row.record_count))
        records = []
        for i in range(row.record_count):
            start = row.offsets[i]
            end = (row.offsets[i + 1] if i + 1 < len(row.offsets)
                   else len(dat))
            rec = dat[start:end]
            upstream = text_array[i]
            decoded, ok, terminated = decode_menu_record(rec, single, ambiguous)
            is_last = (i == row.record_count - 1)
            if len(rec) == 0:
                # A zero-length slice is a shared-pointer alias (offsets equal,
                # like the dlg1 _0/_1 duplicate): the record's pointer aliases
                # the next record's string. The data layer preserves it verbatim
                # as a zero-length record; it is not independently decodable
                # unless the upstream string is genuinely empty too.
                checkable = (upstream == "")
            else:
                # A slice without a terminator was cut short by an overlapping
                # pointer — likewise not independently decodable.
                checkable = ok and (terminated or is_last)
            if checkable and decoded != upstream:
                raise ParseError(dat_path, 0,
                                 "'{}' record {}: decoded {!r} != upstream "
                                 "{!r}".format(stem, i, decoded, upstream))
            records.append((upstream, checkable))
        classes.append(MenuDescClass(stem, _camel(stem), records))
    return single, ambiguous, classes


# --- rendering ---------------------------------------------------------------

_HEADER_COMMON = (
    "// AUTO-GENERATED by tools/asm_parser/parse_text_meta.py\n"
    "// Source: original-src/include/text/*_en.inc (ARRAY_LENGTH / ITEM_SIZE /\n"
    "//         offset-symbol counts) + the DTE char-table format.\n"
    "// (original-src pinned at 1ea47b5)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_text_meta.py \\\n"
    "//       --source-root  original-src \\\n"
    "//       --inc-out      src/data/generated/text_metadata_data.inc \\\n"
    "//       --fixture-out  tests/fixtures/text_metadata_expected.h\n"
    "\n"
)


def render_inc(rows):
    lines = [_HEADER_COMMON,
             "// TextClassMetadata rows in TextClass enum order, #included\n"
             "// inside the kTextClassMetadata array in src/data/text_metadata.cpp.\n"
             "// Each row's identity is its .id field (the TextClass enumerator;\n"
             "// a compile-time assert verifies it == position). .fileStem is the\n"
             "// on-disk name (\"<stem>.dat\" under the language directory). .kind\n"
             "// is FIXED (padded ITEM_SIZE-byte records) or POINTER\n"
             "// (variable-length, offset-indexed). .recordCount and .recordSize\n"
             "// are decimal magnitudes; .recordSize is 0 for POINTER classes.\n\n"]
    enum_w = max(len(r.enum) for r in rows)
    stem_w = max(len(r.stem) for r in rows)
    kind_w = max(len("TextClassKind::" + r.kind) for r in rows)
    count_w = max(len(str(r.record_count)) for r in rows)
    for r in rows:
        enum_tok = "TextClass::{},".format(r.enum)
        stem_tok = '"{}",'.format(r.stem)
        kind_tok = "TextClassKind::{},".format(r.kind)
        lines.append(
            "    {{ .id = {enum:<{ew}} .fileStem = {stem:<{sw}} "
            ".kind = {kind:<{kw}} .recordCount = {count:>{cw}}, "
            ".recordSize = {size} }},\n".format(
                enum=enum_tok, ew=enum_w + len("TextClass::") + 1,
                stem=stem_tok, sw=stem_w + 3,
                kind=kind_tok, kw=kind_w + 1,
                count=r.record_count, cw=count_w,
                size=r.record_size))
    return "".join(lines)


def render_fixture(rows):
    lines = [_HEADER_COMMON,
             "// Test fixture for tests/test_text_metadata.cpp — the\n"
             "// ground-truth copy of the metadata table (.id decimal identity,\n"
             "// .kind 0=FIXED/1=POINTER, decimal count/size). The full-corpus\n"
             "// test asserts kTextClassMetadata matches this array entry by\n"
             "// entry, so a hand edit or re-emit drift in either file fails.\n"
             "\n"
             "#pragma once\n"
             "\n"
             "#include <array>\n"
             "#include <cstdint>\n"
             "\n"
             "namespace ostinato::test {\n"
             "\n"
             "// Mirrors ostinato::TextClassMetadata (src/data/text_metadata.h)\n"
             "// without depending on it. kind: 0 = FIXED, 1 = POINTER.\n"
             "struct ExpectedTextClassMetadata {\n"
             "    std::uint8_t id;\n"
             "    std::uint8_t kind;\n"
             "    std::uint16_t recordCount;\n"
             "    std::uint8_t recordSize;\n"
             "};\n"
             "\n",
             "inline constexpr std::array<ExpectedTextClassMetadata, {}>\n"
             "kExpectedTextClassMetadata = {{{{  // FF6 text-class structure\n"
             .format(len(rows))]
    kind_num = {FIXED: 0, POINTER: 1}
    count_w = max(len(str(r.record_count)) for r in rows)
    for index, r in enumerate(rows):
        lines.append(
            "    {{ .id = {id:>2}, .kind = {kind}, .recordCount = {count:>{cw}}, "
            ".recordSize = {size} }},\n".format(
                id=index, kind=kind_num[r.kind],
                count=r.record_count, cw=count_w, size=r.record_size))
    lines.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(lines)


_OFFSETS_HEADER_COMMON = (
    "// AUTO-GENERATED by tools/asm_parser/parse_text_meta.py\n"
    "// Source: original-src/include/text/*_en.inc `_N :=` record offsets\n"
    "//         (+ dlg1_en.dat byte length for the concatenated dialogue base).\n"
    "// (original-src pinned at 1ea47b5)\n"
    "// DO NOT EDIT BY HAND — regenerate via the four-output invocation:\n"
    "//   python3 tools/asm_parser/parse_text_meta.py \\\n"
    "//       --source-root         original-src \\\n"
    "//       --inc-out             src/data/generated/text_metadata_data.inc \\\n"
    "//       --fixture-out         tests/fixtures/text_metadata_expected.h \\\n"
    "//       --offsets-out         src/data/generated/text_offsets_data.inc \\\n"
    "//       --offsets-fixture-out tests/fixtures/text_offsets_expected.h\n"
    "\n"
)


def _format_u32_rows(offsets, indent="    ", per_line=12):
    """Render a list of ints as comma-separated rows of `per_line` each."""
    lines = []
    for start in range(0, len(offsets), per_line):
        chunk = offsets[start:start + per_line]
        lines.append(indent + ", ".join(str(o) for o in chunk) + ",\n")
    return "".join(lines)


def render_offsets_inc(tables):
    lines = [_OFFSETS_HEADER_COMMON,
             "// Per-record byte offsets for the POINTER text classes, one\n"
             "// constexpr std::uint32_t array each. #included inside an\n"
             "// anonymous namespace in src/data/text_offsets.cpp; the record\n"
             "// slicer reads span [offset[i], offset[i+1]) (last record runs to\n"
             "// the end of the backing bytes). Values are decimal byte offsets.\n"
             "\n"]
    for table in tables:
        lines.append("// {} — {}.\n".format(table.cpp_name, table.note))
        lines.append("constexpr std::uint32_t {}[{}] = {{\n".format(
            table.cpp_name, table.count))
        lines.append(_format_u32_rows(table.offsets))
        lines.append("};\n\n")
    return "".join(lines)


def render_offsets_fixture(tables):
    lines = [_OFFSETS_HEADER_COMMON,
             "// Test fixture for tests/test_text_offsets.cpp — an independent\n"
             "// parser-emitted copy of every offset array. The full-corpus test\n"
             "// asserts the arrays compiled into text_offsets.cpp match these\n"
             "// entry by entry, so a hand edit or re-emit drift in either file\n"
             "// fails. Values are decimal byte offsets.\n"
             "\n"
             "#pragma once\n"
             "\n"
             "#include <cstdint>\n"
             "\n"
             "namespace ostinato::test {\n"
             "\n"]
    for table in tables:
        expected = table.cpp_name.replace("k", "kExpected", 1)
        lines.append("inline constexpr std::uint32_t {}[{}] = {{\n".format(
            expected, table.count))
        lines.append(_format_u32_rows(table.offsets))
        lines.append("};\n\n")
    lines.append("}  // namespace ostinato::test\n")
    return "".join(lines)


_MENU_DESC_HEADER = (
    "// AUTO-GENERATED by tools/asm_parser/parse_text_meta.py\n"
    "// Source: original-src/src/text/<class>_en.{dat,json} decoded through\n"
    "//         original-src/tools/char_table/*.json (the upstream char tables).\n"
    "// (original-src pinned at 1ea47b5)\n"
    "// DO NOT EDIT BY HAND — regenerate via parse_text_meta.py.\n"
    "\n"
)


def _c_escape(text):
    """Escape a Python string for a C++ narrow string literal (UTF-8 bytes)."""
    out = []
    for ch in text:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ch == "\r":
            out.append("\\r")
        else:
            out.append(ch)
    return "".join(out)


def render_menu_desc_fixture(single, ambiguous, classes):
    """Emit the glyph char map + per-class expected decoded strings.

    kEnGlyph[256] maps a glyph byte to its decoded string (nullptr for an
    unmapped or ambiguous byte). Each kXxxExpected[] row is the upstream
    reference text plus whether the record's bytes are all unambiguous — the
    C++ codec test decodes each unambiguous record through its tokenizer and
    kEnGlyph and asserts the result equals `text`.
    """
    lines = [_MENU_DESC_HEADER,
             "// Menu-description decode cross-check fixture for\n"
             "// tests/test_text_codec.cpp.\n"
             "\n"
             "#pragma once\n"
             "\n"
             "#include <cstddef>\n"
             "\n"
             "namespace ostinato::test {\n"
             "\n"
             "// Glyph byte -> decoded string (the EN description char tables\n"
             "// merged; nullptr where a byte is unmapped or list-valued).\n"
             "inline constexpr const char* kEnGlyph[256] = {\n"]
    for base in range(0, 256, 8):
        cells = []
        for b in range(base, base + 8):
            if b in single and b not in ambiguous:
                cells.append('"{}"'.format(_c_escape(single[b])))
            else:
                cells.append("nullptr")
        lines.append("    " + ", ".join(cells) + ",\n")
    lines.append("};\n\n")

    lines.append("struct ExpectedMenuDesc {\n"
                 "    const char* text;      // upstream reference decode\n"
                 "    bool unambiguous;      // all bytes single-valued\n"
                 "};\n\n")
    for cls in classes:
        lines.append(
            "inline constexpr ExpectedMenuDesc k{}Expected[{}] = {{\n".format(
                cls.cpp_name, len(cls.records)))
        for text, ok in cls.records:
            lines.append('    {{ "{}", {} }},\n'.format(
                _c_escape(text), "true" if ok else "false"))
        lines.append("};\n\n")
    lines.append("}  // namespace ostinato::test\n")
    return "".join(lines)


# --- driver ------------------------------------------------------------------

def run(text_inc_dir, src_text_dir, char_table_dir, inc_out, fixture_out,
        offsets_out, offsets_fixture_out, menu_desc_out, check_only=False):
    rows = build_rows(text_inc_dir, src_text_dir=src_text_dir)
    skeletons = verify_jp_skeletons(text_inc_dir)
    tables = build_offset_tables(rows, src_text_dir)
    single, ambiguous, menu_classes = build_menu_desc(rows, src_text_dir,
                                                      char_table_dir)

    if check_only:
        fixed = sum(1 for r in rows if r.kind == FIXED)
        ptr = sum(1 for r in rows if r.kind == POINTER)
        offs = sum(t.count for t in tables)
        clean = sum(1 for c in menu_classes for _t, ok in c.records if ok)
        total = sum(len(c.records) for c in menu_classes)
        print("OK: {} classes ({} fixed, {} pointer); {} JP skeletons verified; "
              "{} offset tables, {} offsets total; menu-desc cross-check {}/{} "
              "records unambiguous across {} classes.".format(
                  len(rows), fixed, ptr, skeletons, len(tables), offs,
                  clean, total, len(menu_classes)))
        return 0

    _write(inc_out, render_inc(rows))
    _write(fixture_out, render_fixture(rows))
    _write(offsets_out, render_offsets_inc(tables))
    _write(offsets_fixture_out, render_offsets_fixture(tables))
    _write(menu_desc_out, render_menu_desc_fixture(single, ambiguous,
                                                   menu_classes))
    clean = sum(1 for c in menu_classes for _t, ok in c.records if ok)
    total = sum(len(c.records) for c in menu_classes)
    print("Emitted {} metadata rows -> {}".format(len(rows), inc_out))
    print("Emitted fixture -> {}".format(fixture_out))
    print("Emitted {} offset tables ({} offsets) -> {}".format(
        len(tables), sum(t.count for t in tables), offsets_out))
    print("Emitted offsets fixture -> {}".format(offsets_fixture_out))
    print("Emitted menu-desc fixture ({}/{} records unambiguous) -> {}".format(
        clean, total, menu_desc_out))
    print("Verified {} JP skeleton includes.".format(skeletons))
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
                    help="disassembly root (include/text + src/text under it)")
    ap.add_argument("--text-inc-dir", help="path to include/text")
    ap.add_argument("--src-text-dir", help="path to src/text (for DTE .dat check)")
    ap.add_argument("--char-table-dir",
                    help="path to tools/char_table (menu-desc cross-check)")
    ap.add_argument("--inc-out",
                    default="src/data/generated/text_metadata_data.inc",
                    help="output path for the TextClassMetadata rows")
    ap.add_argument("--fixture-out",
                    default="tests/fixtures/text_metadata_expected.h",
                    help="output path for the value fixture")
    ap.add_argument("--offsets-out",
                    default="src/data/generated/text_offsets_data.inc",
                    help="output path for the pointer-class offset arrays")
    ap.add_argument("--offsets-fixture-out",
                    default="tests/fixtures/text_offsets_expected.h",
                    help="output path for the offset value fixture")
    ap.add_argument("--menu-desc-out",
                    default="tests/fixtures/text_menu_desc_expected.h",
                    help="output path for the menu-desc decode fixture")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    text_inc_dir = args.text_inc_dir
    src_text_dir = args.src_text_dir
    char_table_dir = args.char_table_dir
    if args.source_root:
        if not text_inc_dir:
            text_inc_dir = os.path.join(args.source_root, "include", "text")
        if not src_text_dir:
            src_text_dir = os.path.join(args.source_root, "src", "text")
        if not char_table_dir:
            char_table_dir = os.path.join(args.source_root, "tools",
                                          "char_table")
    if not text_inc_dir:
        ap.error("provide --source-root or --text-inc-dir")
    try:
        return run(text_inc_dir, src_text_dir, char_table_dir, args.inc_out,
                   args.fixture_out, args.offsets_out, args.offsets_fixture_out,
                   args.menu_desc_out, check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
