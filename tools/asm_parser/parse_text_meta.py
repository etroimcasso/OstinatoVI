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
offset symbol per record (variable-length records located by an offset table,
built in a later pass). This script reads the structure straight off disk and
emits:

  * src/data/generated/text_metadata_data.inc — one designated-initializer
    TextClassMetadata row per class (identity as the TextClass enumerator, kind
    as TextClassKind, count/size as decimal magnitudes); the kTextClassMetadata
    array #includes it.
  * tests/fixtures/text_metadata_expected.h — the same rows with decimal
    identity for a full-corpus equivalence test.

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
    parse_text_meta.py --source-root PATH --inc-out FILE --fixture-out FILE
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
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

_RE_OFFSET_SYM = re.compile(r"^_(\d+)\s*:=")


class ClassMeta(object):
    """Structure read from one class .inc (single language)."""

    __slots__ = ("array_length", "item_size", "offset_count", "has_metadata")

    def __init__(self, array_length, item_size, offset_count, has_metadata):
        self.array_length = array_length
        self.item_size = item_size
        self.offset_count = offset_count
        self.has_metadata = has_metadata


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
    offset_count = 0

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
        if _RE_OFFSET_SYM.match(s):
            offset_count += 1
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
    return ClassMeta(array_length, item_size, offset_count, has_metadata)


class Row(object):
    """One emitted metadata row: identity, on-disk file stem, kind, count, size."""

    __slots__ = ("enum", "stem", "kind", "record_count", "record_size")

    def __init__(self, enum, stem, kind, record_count, record_size):
        self.enum = enum
        self.stem = stem
        self.kind = kind
        self.record_count = record_count
        self.record_size = record_size


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
    return Row(enum, basename, kind, meta.array_length, 0)


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


# --- driver ------------------------------------------------------------------

def run(text_inc_dir, src_text_dir, inc_out, fixture_out, check_only=False):
    rows = build_rows(text_inc_dir, src_text_dir=src_text_dir)
    skeletons = verify_jp_skeletons(text_inc_dir)

    if check_only:
        fixed = sum(1 for r in rows if r.kind == FIXED)
        ptr = sum(1 for r in rows if r.kind == POINTER)
        print("OK: {} classes ({} fixed, {} pointer); {} JP skeletons verified."
              .format(len(rows), fixed, ptr, skeletons))
        return 0

    _write(inc_out, render_inc(rows))
    _write(fixture_out, render_fixture(rows))
    print("Emitted {} metadata rows -> {}".format(len(rows), inc_out))
    print("Emitted fixture -> {}".format(fixture_out))
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
    ap.add_argument("--inc-out",
                    default="src/data/generated/text_metadata_data.inc",
                    help="output path for the TextClassMetadata rows")
    ap.add_argument("--fixture-out",
                    default="tests/fixtures/text_metadata_expected.h",
                    help="output path for the value fixture")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    text_inc_dir = args.text_inc_dir
    src_text_dir = args.src_text_dir
    if args.source_root:
        if not text_inc_dir:
            text_inc_dir = os.path.join(args.source_root, "include", "text")
        if not src_text_dir:
            src_text_dir = os.path.join(args.source_root, "src", "text")
    if not text_inc_dir:
        ap.error("provide --source-root or --text-inc-dir")
    try:
        return run(text_inc_dir, src_text_dir, args.inc_out, args.fixture_out,
                   check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
