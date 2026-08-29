#!/usr/bin/env python3
"""Emit the ROM region table, the asset id space, and the ROM identity table.

Port-time tooling (NOT a build/CI dependency). The game reads its content
directly out of the player's cartridge image, so it needs three things the
upstream disassembly already states: which content families exist, where each
one lives in the SNES address space, and how to tell the three supported ROM
revisions apart.

Sources:

  * tools/rip_list_en.json / rip_list_jp.json — one entry per content family,
    each carrying an inclusive SNES `asset_range`. These are the tables
    extract_assets.py walks (extract_assets.py:247-266). Upstream keys them by
    LANGUAGE, not by revision: both US ROMs share the `en` list because their
    ripped data is identical (extract_assets.py:323-331).
  * tools/extract_assets.py — the three CRC32 branches that name a ROM, the
    language each maps to, and the copier-header size its diagnostic names.
  * cfg/ff6-en.cfg — the linker's bank map, whose ROM-region extent gives the
    image size.
  * src/data/generated/text_metadata_data.inc (repo-side) — the per-text-class
    record counts and widths, used to prove each fixed-record text range is
    exactly as long as its records require.

Emitted artifacts:

  * RomAsset          include/ostinato/rom_asset.h
  * RomIdentity       include/ostinato/rom_identity.h
  * region rows       src/data/generated/rom_regions_data.inc
  * fixtures          tests/fixtures/rom_regions_expected.h
                      tests/fixtures/rom_identity_expected.h

A family's identity is its output PATH, not its filename stem: the same stem
appears under different extensions for genuinely different assets (a graphics
blob and its palette, a tile sheet and its tilemap). Ten stems carry more than
one family and take an explicit role suffix (_ROLE_NAMES below); every other
stem names its family directly, with the `_en` / `_jp` suffix dropped because
that difference is the row's Language column rather than a separate asset.

Structural guarantees, hard-errored at emit time:
  * every asset_range parses, ends at or after it begins, and lies inside the
    HiROM ROM region the linker declares;
  * the stems that carry multiple families are EXACTLY the ten _ROLE_NAMES
    covers — a new one is a grammar change, not something to name on the fly;
  * every fixed-record text range is exactly recordCount x recordSize bytes;
  * extract_assets.py holds exactly three CRC branches, each naming a ROM this
    port supports and assigning it the language that ROM's revision implies;
  * every single-file EN range is byte-identical to the bytes at the same place
    in the vanilla ROM (skipped, and reported, when no ROM is available).

Python 3 standard library only; targets 3.9+.

Usage:
    parse_rom_regions.py --source-root PATH --repo-root PATH
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

# The SNES address of the first ROM bank. HiROM banks $c0-$ff hold the image;
# everything below is RAM or hardware, so a bank map entry under this is not
# part of the cartridge.
ROM_REGION_BASE = 0xC00000

# The list sections, in the order extract_assets.py walks them and in the order
# their families appear in the emitted enum.
SECTIONS = ("text", "data", "array")

# Upstream ROM names -> the GameVersion enumerator each is. Every name the CRC
# chain produces must appear here; a name that does not is a new or renamed ROM
# revision and stops the parse.
_ROM_NAMES = {
    "Final Fantasy VI 1.0 (J)":  "JP_1_0",
    "Final Fantasy III 1.0 (U)": "US_1_0",
    "Final Fantasy III 1.1 (U)": "US_1_1",
}

# The language each supported revision ships, mirroring ostinato::language() in
# include/ostinato/game_version.h. The parse fails if the upstream script
# assigns a revision to the other list.
_ROM_LANGUAGES = {"JP_1_0": "jp", "US_1_0": "en", "US_1_1": "en"}

# The ten output paths whose filename stem is shared with another family. The
# value is the enumerator name; where one path still covers two families (the
# monster stencils, which differ only in record width) it maps item_size to the
# name instead. Stems absent from here name their family directly.
_ROLE_NAMES = {
    "src/gfx/attack_gfx.2bpp":            "ATTACK_GFX_2BPP",
    "src/gfx/attack_gfx.3bpp":            "ATTACK_GFX_3BPP",
    "src/gfx/attack_mode7.4bpp.lz":       "ATTACK_MODE7_GFX",
    "src/gfx/attack_mode7.scr.lz":        "ATTACK_MODE7_TILEMAP",
    "src/gfx/magitek_train.cgx.lz":       "MAGITEK_TRAIN_GFX",
    "src/gfx/magitek_train.pal":          "MAGITEK_TRAIN_PAL",
    "src/gfx/monster_gfx/%s.trm":         "MONSTER_GFX_TRIMMED",
    "src/gfx/monster_gfx/%s.pal":         "MONSTER_GFX_PAL",
    "src/gfx/monster_gfx/%s.stn":         {8: "MONSTER_STENCIL_SMALL",
                                           32: "MONSTER_STENCIL_LARGE"},
    "src/gfx/portrait_gfx/%s.4bpp":       "PORTRAIT_GFX",
    "src/gfx/portrait_gfx/%s.pal":        "PORTRAIT_PAL",
    "src/gfx/vector_approach.4bpp.lz":    "VECTOR_APPROACH_GFX",
    "src/gfx/vector_approach.scr.lz":     "VECTOR_APPROACH_TILEMAP",
    "src/gfx/vector_approach.pal":        "VECTOR_APPROACH_PAL",
    "src/gfx/window/window_%s.4bpp":      "WINDOW_GFX",
    "src/gfx/window/window_%s.pal":       "WINDOW_PAL",
    "src/gfx/world_1_bg.4bpp.lz":         "WORLD_1_BG_GFX",
    "src/gfx/world_1_bg.pal":             "WORLD_1_BG_PAL",
    "src/gfx/world_2_bg.4bpp.lz":         "WORLD_2_BG_GFX",
    "src/gfx/world_2_bg.pal":             "WORLD_2_BG_PAL",
    "src/gfx/world_backdrop.4bpp.lz":     "WORLD_BACKDROP_GFX",
    "src/gfx/world_backdrop.scr.lz":      "WORLD_BACKDROP_TILEMAP",
}

# The list writes most ranges with a lowercase 0x prefix and at least one with
# an uppercase 0X, so both are accepted.
_RE_RANGE = re.compile(r"^0[xX]([0-9A-Fa-f]{6})-0[xX]([0-9A-Fa-f]{6})$")
_RE_LANG_SUFFIX = re.compile(r"_(en|jp)$")
_RE_CRC_BRANCH = re.compile(
    r"crc32 == (0x[0-9A-Fa-f]{8}):\s*\n"
    r"\s*rom_name = '([^']+)'\s*\n"
    r"\s*rom_language = '(en|jp)'")
_RE_COPIER_HEADER = re.compile(r"(\d+)-byte copier header")
_RE_BANK = re.compile(
    r"^\s*\w+:\s*start\s*=\s*\$([0-9a-fA-F]+),\s*size\s*=\s*\$([0-9a-fA-F]+)",
    re.M)
_RE_METADATA_ROW = re.compile(
    r"\.id = TextClass::(\w+),\s*\.fileStem = \"([^\"]+)\",\s*"
    r"\.kind = TextClassKind::(\w+),\s*\.recordCount =\s*(\d+),\s*"
    r"\.recordSize = (\d+)")


class RegionRow(object):
    """One content family in one language: where it lives and how big it is."""

    def __init__(self, asset, language, section, path, begin, end, item_size):
        self.asset = asset          # enumerator name
        self.language = language    # "en" / "jp"
        self.section = section      # "text" / "data" / "array"
        self.path = path            # the upstream output path, verbatim
        self.begin = begin          # inclusive SNES start address
        self.end = end              # inclusive SNES end address
        self.item_size = item_size  # per-item width where the list states one

    @property
    def size(self):
        return self.end - self.begin + 1


# --- rip lists ---------------------------------------------------------------

def _as_int(value):
    """An item_size field as an int; the lists write some in hex, some plain."""
    if value is None:
        return None
    return int(value, 0) if isinstance(value, str) else int(value)


def _normalize_path(path):
    """The output path with the language suffix dropped from its stem.

    `%s`-globbed paths carry no language suffix — the directory names the
    family and the glob stands for the per-item names — so they pass through.
    """
    directory, base = os.path.split(path)
    if "%s" in base:
        return path
    parts = base.split(".")
    parts[0] = _RE_LANG_SUFFIX.sub("", parts[0])
    return os.path.join(directory, ".".join(parts))


def _family_key(path, item_size):
    """What makes two rows the same family: the language-neutral path, plus the
    record width for the one path that covers two families."""
    normalized = _normalize_path(path)
    role = _ROLE_NAMES.get(normalized)
    if isinstance(role, dict):
        return (normalized, item_size)
    return (normalized, None)


def _default_name(path):
    """The enumerator a path names on its own, before role suffixes apply."""
    directory, base = os.path.split(_normalize_path(path))
    if "%s" in base:
        return os.path.basename(directory).upper()
    return base.split(".")[0].upper()


def _asset_name(path, item_size, source_path):
    normalized = _normalize_path(path)
    role = _ROLE_NAMES.get(normalized)
    if isinstance(role, dict):
        if item_size not in role:
            raise ParseError(
                source_path, 0,
                "{}: record width {} has no name — the shared-stem family "
                "changed upstream; escalate before naming it"
                .format(normalized, item_size))
        return role[item_size]
    if role is not None:
        return role
    return _default_name(path)


def _parse_range(text, source_path, path):
    match = _RE_RANGE.match(text or "")
    if match is None:
        raise ParseError(source_path, 0,
                         "{}: asset_range {!r} is not 0xBBBBBB-0xEEEEEE"
                         .format(path, text))
    begin = int(match.group(1), 16)
    end = int(match.group(2), 16)
    if end < begin:
        raise ParseError(source_path, 0,
                         "{}: asset_range ends before it begins (${:06x} > "
                         "${:06x})".format(path, begin, end))
    return begin, end


def read_rip_list(source_root, language, rom_size):
    """Every family in one language's list, in section then list order."""
    path = os.path.join(source_root, "tools",
                        "rip_list_{}.json".format(language))
    if not os.path.isfile(path):
        raise ParseError(path, 0, "rip list not found")
    with open(path, "r", encoding="utf-8") as fh:
        listing = json.load(fh)

    missing = [s for s in SECTIONS if s not in listing]
    if missing:
        raise ParseError(path, 0,
                         "rip list is missing section(s) {} — the list's shape "
                         "changed upstream".format(", ".join(missing)))

    rows = []
    for section in SECTIONS:
        for entry in listing[section]:
            output = entry.get("json_path") or entry.get("file_path")
            if output is None:
                raise ParseError(path, 0,
                                 "{} entry has neither json_path nor file_path"
                                 .format(section))
            item_size = _as_int(entry.get("item_size"))
            begin, end = _parse_range(entry.get("asset_range"), path, output)
            limit = ROM_REGION_BASE + rom_size
            if begin < ROM_REGION_BASE or end >= limit:
                raise ParseError(path, 0,
                                 "{}: ${:06x}-${:06x} falls outside the ROM "
                                 "region ${:06x}-${:06x}"
                                 .format(output, begin, end,
                                         ROM_REGION_BASE, limit - 1))
            rows.append(RegionRow(
                asset=_asset_name(output, item_size, path),
                language=language, section=section, path=output,
                begin=begin, end=end, item_size=item_size))
    return rows


def assert_role_names_complete(rows_by_language, source_root):
    """The stems carrying more than one family are exactly the ten named above.

    A stem that starts covering two families upstream would otherwise collapse
    silently into one enumerator, and two different places in the ROM would
    answer to the same name.
    """
    families = {}
    for rows in rows_by_language.values():
        for row in rows:
            families.setdefault(_default_name(row.path), set()).add(
                _family_key(row.path, row.item_size))
    shared = {name: keys for name, keys in families.items() if len(keys) > 1}

    named = set()
    for path, role in _ROLE_NAMES.items():
        named.add(_default_name(path))
    unnamed = sorted(set(shared) - named)
    if unnamed:
        raise ParseError(
            os.path.join(source_root, "tools", "rip_list_en.json"), 0,
            "stem(s) {} now cover more than one family and have no role name — "
            "escalate; naming them here is a surface decision"
            .format(", ".join(unnamed)))

    stale = sorted(named - set(shared))
    if stale:
        raise ParseError(
            os.path.join(source_root, "tools", "rip_list_en.json"), 0,
            "stem(s) {} no longer share a name with another family — the role "
            "suffixes for them are stale".format(", ".join(stale)))


# --- ROM image facts ---------------------------------------------------------

def read_rom_size(source_root):
    """The image size, as the extent of the ROM banks the linker declares."""
    path = os.path.join(source_root, "cfg", "ff6-en.cfg")
    if not os.path.isfile(path):
        raise ParseError(path, 0, "linker config not found")
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()
    ends = [int(m.group(1), 16) + int(m.group(2), 16)
            for m in _RE_BANK.finditer(text)
            if int(m.group(1), 16) >= ROM_REGION_BASE]
    if not ends:
        raise ParseError(path, 0,
                         "no ROM-region banks in the memory map — the config's "
                         "shape changed upstream")
    size = max(ends) - ROM_REGION_BASE
    if size <= 0 or size % 0x10000:
        raise ParseError(path, 0,
                         "ROM extent {} is not a whole number of banks"
                         .format(size))
    return size


def read_copier_header_bytes(source_root):
    """The copier-header size, from the diagnostic that tells a player to strip
    one before extracting."""
    path = os.path.join(source_root, "tools", "extract_assets.py")
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()
    sizes = {int(m.group(1)) for m in _RE_COPIER_HEADER.finditer(text)}
    if len(sizes) != 1:
        raise ParseError(path, 0,
                         "expected one copier-header size, found {}"
                         .format(sorted(sizes)))
    return sizes.pop()


def read_rom_identities(source_root):
    """The three (revision, CRC32) pairs, checked against the language each
    revision ships."""
    path = os.path.join(source_root, "tools", "extract_assets.py")
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()

    identities = []
    for match in _RE_CRC_BRANCH.finditer(text):
        crc = int(match.group(1), 16)
        name = match.group(2)
        language = match.group(3)
        version = _ROM_NAMES.get(name)
        if version is None:
            raise ParseError(path, 0,
                             "unknown ROM name {!r} — a revision this port does "
                             "not model".format(name))
        if _ROM_LANGUAGES[version] != language:
            raise ParseError(path, 0,
                             "{} is assigned the {!r} list but ships {!r}"
                             .format(name, language, _ROM_LANGUAGES[version]))
        identities.append((version, crc, name))

    if len(identities) != len(_ROM_NAMES):
        raise ParseError(path, 0,
                         "expected {} CRC branches, found {}"
                         .format(len(_ROM_NAMES), len(identities)))
    found = {version for version, _crc, _name in identities}
    if found != set(_ROM_NAMES.values()):
        raise ParseError(path, 0,
                         "CRC branches cover {} — expected {}"
                         .format(sorted(found), sorted(_ROM_NAMES.values())))
    order = list(_ROM_NAMES.values())
    identities.sort(key=lambda item: order.index(item[0]))
    return identities


# --- cross-checks ------------------------------------------------------------

def read_text_metadata(repo_root):
    """The per-text-class record counts and widths this port already ships."""
    path = os.path.join(repo_root, "src", "data", "generated",
                        "text_metadata_data.inc")
    if not os.path.isfile(path):
        raise ParseError(path, 0, "text metadata not found")
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()
    rows = {}
    for match in _RE_METADATA_ROW.finditer(text):
        rows[match.group(2)] = (match.group(1), match.group(3),
                                int(match.group(4)), int(match.group(5)))
    if not rows:
        raise ParseError(path, 0,
                         "no metadata rows parsed — the emitted row shape "
                         "changed")
    return rows


def assert_fixed_text_sizes(rows, metadata, repo_root):
    """A fixed-record text range holds exactly its records and nothing else."""
    path = os.path.join(repo_root, "src", "data", "generated",
                        "text_metadata_data.inc")
    checked = 0
    by_name = {}
    for row in rows:
        if row.section == "text" and row.language == "en":
            by_name[_default_name(row.path).lower()] = row
    for stem, (klass, kind, count, width) in sorted(metadata.items()):
        if kind != "FIXED":
            continue
        row = by_name.get(stem)
        if row is None:
            raise ParseError(path, 0,
                             "text class {} (stem {!r}) has no range in the en "
                             "rip list".format(klass, stem))
        if row.size != count * width:
            raise ParseError(path, 0,
                             "text class {}: range is {} bytes, records need "
                             "{} ({} x {})"
                             .format(klass, row.size, count * width, count,
                                     width))
        checked += 1
    return checked


def assert_ranges_match_rom(rows, source_root):
    """Every single-file EN range holds the bytes the rip wrote from it.

    The port reads these places out of the cartridge itself, so the addresses
    are only right if the bytes at them are the bytes upstream extracted. Only
    families the rip writes whole are comparable: a `%s` path is split per item
    and a text range is decoded to JSON, so neither is a byte-for-byte match.

    Returns (checked, skipped_missing) or None when no ROM is available.
    """
    rom_path = common.find_vanilla_rom(source_root)
    if rom_path is None:
        return None
    rom = common.load_vanilla_rom(source_root)

    checked = 0
    missing = 0
    for row in rows:
        if row.language != "en" or row.section == "text":
            continue
        if "%s" in row.path:
            continue
        ripped = os.path.join(source_root, row.path)
        if not os.path.isfile(ripped):
            missing += 1
            continue
        with open(ripped, "rb") as fh:
            expected = fh.read()
        offset = common.hirom_file_offset(row.begin)
        actual = rom[offset:offset + row.size]
        if len(expected) != row.size:
            raise ParseError(ripped, 0,
                             "{}: file is {} bytes, range declares {}"
                             .format(row.asset, len(expected), row.size))
        if actual != expected:
            raise ParseError(ripped, 0,
                             "{}: ROM bytes at ${:06x} differ from the ripped "
                             "file — the range is wrong; escalate"
                             .format(row.asset, row.begin))
        checked += 1
    return checked, missing


# --- assembly ----------------------------------------------------------------

def build_asset_order(rows_by_language):
    """Enumerator order: section by section, each language's list in its own
    order, every family taken the first time it is seen."""
    order = []
    seen = set()
    for section in SECTIONS:
        for language in ("en", "jp"):
            for row in rows_by_language[language]:
                if row.section != section or row.asset in seen:
                    continue
                seen.add(row.asset)
                order.append((section, row.asset))
    return order


def build_region_rows(rows_by_language, asset_order):
    """Every (family, language) row, sorted by asset then language."""
    index = {name: i for i, (_section, name) in enumerate(asset_order)}
    rows = []
    for language in ("jp", "en"):
        rows.extend(rows_by_language[language])
    rows.sort(key=lambda r: (index[r.asset], 0 if r.language == "jp" else 1))

    seen = set()
    for row in rows:
        key = (row.asset, row.language)
        if key in seen:
            raise ParseError("<rip lists>", 0,
                             "{} appears twice in the {} list"
                             .format(row.asset, row.language))
        seen.add(key)
    return rows


# --- rendering ---------------------------------------------------------------

def _banner(source_lines):
    body = "".join("// Source: {}\n".format(s) for s in source_lines)
    return ("// AUTO-GENERATED by tools/asm_parser/parse_rom_regions.py\n"
            + body +
            "// (original-src pinned at 1ea47b5)\n"
            "// DO NOT EDIT BY HAND — regenerate via:\n"
            "//   python3 tools/asm_parser/parse_rom_regions.py \\\n"
            "//       --source-root original-src --repo-root .\n\n")


_SECTION_COMMENTS = {
    "text": "// The script: name tables, dialogue banks, descriptions, and the\n"
            "// DTE pair table. Every one is language-specific.",
    "data": "// Graphics, palettes, tile sheets, sound samples, and the numeric\n"
            "// tables the game reads directly.",
    "array": "// Pointer-indexed arrays: a table of offsets followed by the\n"
             "// variable-length records it addresses.",
}


def render_asset_header(asset_order):
    out = [_banner(["tools/rip_list_{en,jp}.json (the asset tables "
                    "extract_assets.py walks)"]),
           "#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\n",
           "// Every content family the cartridge holds. The id names WHAT a\n"
           "// family is; where it lives differs per language, so an address\n"
           "// comes from romRegion(asset, language) (src/data/rom_regions.h)\n"
           "// rather than from the id itself.\n"
           "//\n"
           "// A family is identified by the file the upstream rip writes for\n"
           "// it, so a graphics blob and its palette are separate ids even\n"
           "// where they share a name. Ids are dense and stable in the order\n"
           "// below; nothing persists them, so the order is free to grow.\n"
           "namespace ostinato {\n\n",
           "enum class RomAsset : std::uint8_t {\n"]
    current = None
    for section, name in asset_order:
        if section != current:
            out.append(("" if current is None else "\n")
                       + "".join("    " + line + "\n"
                                 for line in _SECTION_COMMENTS[section]
                                 .split("\n")))
            current = section
        out.append("    {},\n".format(name))
    out.append("};\n\n")
    out.append("// How many families there are. The region table's asset column\n"
               "// never exceeds this.\n")
    out.append("inline constexpr std::size_t kRomAssetCount = {};\n\n"
               .format(len(asset_order)))
    out.append("}  // namespace ostinato\n")
    return "".join(out)


def render_identity_header(identities, rom_size, copier_header):
    out = [_banner(["tools/extract_assets.py:323-331 (the CRC32 branches)",
                    "cfg/ff6-en.cfg (the ROM bank extent)"]),
           "#pragma once\n\n#include <array>\n#include <cstddef>\n"
           "#include <cstdint>\n\n#include \"ostinato/game_version.h\"\n\n",
           "namespace ostinato {\n\n",
           "// The size of a headerless image. Every supported revision is this\n"
           "// long, so a file of any other size is not one of them.\n",
           "inline constexpr std::size_t kRomSizeBytes = {};\n\n"
           .format(rom_size),
           "// Some dumps carry a copier header ahead of the image. It is not\n"
           "// part of the cartridge and is dropped before anything reads or\n"
           "// identifies the bytes.\n",
           "inline constexpr std::size_t kCopierHeaderBytes = {};\n\n"
           .format(copier_header),
           "// One supported revision and the CRC32 of its headerless image.\n"
           "struct RomIdentityEntry {\n"
           "    GameVersion   version;\n"
           "    std::uint32_t crc32;\n"
           "};\n\n",
           "// The revisions this port accepts, in GameVersion order.\n"
           "inline constexpr std::array<RomIdentityEntry, {}>\n"
           "kRomIdentities = {{{{\n".format(len(identities))]
    width = max(len(version) for version, _crc, _name in identities)
    for version, crc, _name in identities:
        out.append("    {{ .version = GameVersion::{:<{w}} .crc32 = 0x{:08X} }},\n"
                   .format(version + ",", crc, w=width + 1))
    out.append("}};\n\n}  // namespace ostinato\n")
    return "".join(out)


def render_regions_inc(rows):
    out = [_banner(["tools/rip_list_{en,jp}.json (asset_range per family)"]),
           "// RomRegionEntry rows, #included inside the kRomRegions array in\n"
           "// src/data/rom_regions.cpp. Each row is one family in one\n"
           "// language: the two typed identity fields, then the place the\n"
           "// bytes live. Rows are sorted by asset then language, and a family\n"
           "// a language does not ship has no row (romRegion returns nullopt).\n"
           "//\n"
           "// .at is the SNES address the rip list states, verbatim — an\n"
           "// opaque ROM fact, so hex. .size is a byte count, so decimal, and\n"
           "// covers the whole inclusive range. The VM resolves .at in the\n"
           "// machine's decoded address space, so a family that crosses a bank\n"
           "// boundary reads straight through.\n\n"]
    width = max(len(row.asset) for row in rows)
    size_width = max(len(str(row.size)) for row in rows)
    for row in rows:
        out.append("    {{ .asset = RomAsset::{:<{w}} .language = Language::{}, "
                   ".region = {{ .at = 0x{:06X}, .size = {:>{s}} }} }},\n"
                   .format(row.asset + ",", row.language.upper(), row.begin,
                           row.size, w=width + 1, s=size_width))
    return "".join(out)


def render_regions_fixture(rows, asset_order):
    index = {name: i for i, (_section, name) in enumerate(asset_order)}
    struct = (
        "// One region row: the asset and language as their decimal enumerator\n"
        "// values, and the place the family's bytes live.\n"
        "struct ExpectedRomRegion {\n"
        "    std::uint8_t  asset;\n"
        "    std::uint8_t  language;\n"
        "    std::uint32_t at;\n"
        "    std::uint32_t size;\n"
        "};\n")
    out = [_banner(["tools/rip_list_{en,jp}.json (asset_range per family)"]),
           "#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           "namespace ostinato::test {\n\n" + struct + "\n",
           "inline constexpr std::array<ExpectedRomRegion, {}>\n"
           "kExpectedRomRegions = {{{{\n".format(len(rows))]
    asset_width = max(len(str(index[row.asset])) for row in rows)
    size_width = max(len(str(row.size)) for row in rows)
    for row in rows:
        out.append("    {{ .asset = {:>{a}}, .language = {}, .at = 0x{:06X}, "
                   ".size = {:>{s}} }},\n"
                   .format(index[row.asset],
                           0 if row.language == "jp" else 1,
                           row.begin, row.size,
                           a=asset_width, s=size_width))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


def render_identity_fixture(identities, rom_size, copier_header):
    struct = (
        "// One accepted revision: the GameVersion enumerator as its decimal\n"
        "// value, and the CRC32 of that revision's headerless image.\n"
        "struct ExpectedRomIdentity {\n"
        "    std::uint8_t  version;\n"
        "    std::uint32_t crc32;\n"
        "};\n")
    order = list(_ROM_NAMES.values())
    out = [_banner(["tools/extract_assets.py:323-331 (the CRC32 branches)",
                    "cfg/ff6-en.cfg (the ROM bank extent)"]),
           "#pragma once\n\n#include <array>\n#include <cstddef>\n"
           "#include <cstdint>\n\nnamespace ostinato::test {\n\n"
           + struct + "\n",
           "inline constexpr std::size_t kExpectedRomSizeBytes = {};\n"
           .format(rom_size),
           "inline constexpr std::size_t kExpectedCopierHeaderBytes = {};\n\n"
           .format(copier_header),
           "inline constexpr std::array<ExpectedRomIdentity, {}>\n"
           "kExpectedRomIdentities = {{{{\n".format(len(identities))]
    for version, crc, _name in identities:
        out.append("    {{ .version = {}, .crc32 = 0x{:08X} }},\n"
                   .format(order.index(version), crc))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver ------------------------------------------------------------------

def _outputs(repo_root):
    j = os.path.join
    return {
        "asset_h": j(repo_root, "include", "ostinato", "rom_asset.h"),
        "identity_h": j(repo_root, "include", "ostinato", "rom_identity.h"),
        "regions_inc": j(repo_root, "src", "data", "generated",
                         "rom_regions_data.inc"),
        "regions_fix": j(repo_root, "tests", "fixtures",
                         "rom_regions_expected.h"),
        "identity_fix": j(repo_root, "tests", "fixtures",
                          "rom_identity_expected.h"),
    }


def run(source_root, repo_root, check_only=False):
    rom_size = read_rom_size(source_root)
    copier_header = read_copier_header_bytes(source_root)
    identities = read_rom_identities(source_root)

    rows_by_language = {
        language: read_rip_list(source_root, language, rom_size)
        for language in ("en", "jp")
    }
    assert_role_names_complete(rows_by_language, source_root)

    metadata = read_text_metadata(repo_root)
    fixed = assert_fixed_text_sizes(rows_by_language["en"], metadata, repo_root)

    asset_order = build_asset_order(rows_by_language)
    region_rows = build_region_rows(rows_by_language, asset_order)

    rom_check = assert_ranges_match_rom(rows_by_language["en"], source_root)
    if rom_check is None:
        rom_note = ("ROM cross-check SKIPPED (no vanilla ROM; set "
                    "FF6_VANILLA_ROM)")
    else:
        rom_note = ("{} en ranges byte-identical to the ROM ({} not ripped "
                    "on this machine)".format(rom_check[0], rom_check[1]))

    summary = ("{} assets / {} region rows (en {}, jp {}); ROM {} B, copier "
               "header {} B, {} revisions; {} fixed text ranges verified; {}"
               .format(len(asset_order), len(region_rows),
                       len(rows_by_language["en"]), len(rows_by_language["jp"]),
                       rom_size, copier_header, len(identities), fixed,
                       rom_note))

    if check_only:
        print("OK: " + summary)
        return 0

    out = _outputs(repo_root)
    _write(out["asset_h"], render_asset_header(asset_order))
    _write(out["identity_h"],
           render_identity_header(identities, rom_size, copier_header))
    _write(out["regions_inc"], render_regions_inc(region_rows))
    _write(out["regions_fix"], render_regions_fixture(region_rows, asset_order))
    _write(out["identity_fix"],
           render_identity_fixture(identities, rom_size, copier_header))
    print("Emitted 5 files: " + summary)
    return 0


def _write(path, text):
    directory = os.path.dirname(path)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root", default="original-src",
                    help="disassembly root")
    ap.add_argument("--repo-root", default=".", help="repo root for outputs")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)
    try:
        return run(args.source_root, args.repo_root,
                   check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
