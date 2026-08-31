#!/usr/bin/env python3
"""Unit tests for parse_rom_regions.py.

Three layers per the parser-test discipline:
  1. pure helpers (path normalization, family keying, enumerator naming, range
     parsing);
  2. synthetic inputs exercising every structural assert (malformed ranges,
     ranges outside the ROM region, an unnamed shared stem, a stale role name,
     a missing list section, the CRC-branch contract, the fixed-text-size
     cross-check, a family listed twice);
  3. end-to-end against the real original-src rip lists and linker config
     (skipped cleanly when the disassembly is absent).

Python 3 stdlib only. Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import json
import os
import shutil
import tempfile
import unittest

import parse_rom_regions as prr
from common import ParseError

ROM_SIZE = 0x300000


# --- synthetic source trees --------------------------------------------------

_EXTRACT_ASSETS = """
        if crc32 == 0x45EF5AC8:
            rom_name = 'Final Fantasy VI 1.0 (J)'
            rom_language = 'jp'
        elif crc32 == 0xA27F1C7A:
            rom_name = 'Final Fantasy III 1.0 (U)'
            rom_language = 'en'
        elif crc32 == 0xC0FA0464:
            rom_name = 'Final Fantasy III 1.1 (U)'
            rom_language = 'en'
        else:
            continue

    print('If your ROM has a 512-byte copier header, please remove it first.')
"""

_CFG = """memory {
    bank_7e: start = $7e5000, size = $3000, type = ro, fill = no;
    bank_c0: start = $c00000, size = $10000, type = ro, fill = yes;
    bank_ee: start = $ee0000, size = $20000, type = ro, fill = yes;
}
"""

_METADATA = (
    '    { .id = TextClass::CHAR_NAME, .fileStem = "char_name", '
    '.kind = TextClassKind::FIXED,   .recordCount =   64, .recordSize = 6 },\n'
    '    { .id = TextClass::DLG1,      .fileStem = "dlg1",      '
    '.kind = TextClassKind::POINTER, .recordCount = 1574, .recordSize = 0 },\n'
)


def _minimal_lists():
    """A two-family list pair: one fixed-record text table and one data blob."""
    text = [{"json_path": "src/text/char_name_%s.json",
             "asset_range": "0xC478C0-0xC47A3F"}]
    data = [{"file_path": "src/field/map_parallax.dat",
             "asset_range": "0xC0FE40-0xC0FEA7"}]
    return {"text": text, "data": data, "array": []}


class _Tree(object):
    """A throwaway source root holding only what the parser reads."""

    def __init__(self, lists=None, extract=_EXTRACT_ASSETS, cfg=_CFG,
                 metadata=_METADATA):
        self.root = tempfile.mkdtemp()
        os.makedirs(os.path.join(self.root, "tools"))
        os.makedirs(os.path.join(self.root, "cfg"))
        os.makedirs(os.path.join(self.root, "src", "data", "generated"))
        for language in ("en", "jp"):
            listing = json.loads(json.dumps(lists or _minimal_lists()))
            for entry in listing["text"]:
                if "%s" in entry["json_path"]:
                    entry["json_path"] = entry["json_path"] % language
            self._write(os.path.join("tools",
                                     "rip_list_{}.json".format(language)),
                        json.dumps(listing))
        self._write(os.path.join("tools", "extract_assets.py"), extract)
        self._write(os.path.join("cfg", "ff6-en.cfg"), cfg)
        self._write(os.path.join("src", "data", "generated",
                                 "text_metadata_data.inc"), metadata)

    def _write(self, relative, text):
        path = os.path.join(self.root, relative)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)

    def close(self):
        shutil.rmtree(self.root, ignore_errors=True)


# --- Layer 1: pure helpers ---------------------------------------------------

class HelperTests(unittest.TestCase):

    def test_as_int_accepts_both_notations(self):
        self.assertIsNone(prr._as_int(None))
        self.assertEqual(prr._as_int(32), 32)
        self.assertEqual(prr._as_int("0x0320"), 800)

    def test_normalize_path_drops_the_language_suffix(self):
        self.assertEqual(prr._normalize_path("src/text/dlg1_en.json"),
                         "src/text/dlg1.json")
        self.assertEqual(prr._normalize_path("src/text/dlg1_jp.json"),
                         "src/text/dlg1.json")

    def test_normalize_path_keeps_a_globbed_path_whole(self):
        # The directory names the family; the glob stands for per-item names.
        self.assertEqual(prr._normalize_path("src/gfx/window/window_%s.4bpp"),
                         "src/gfx/window/window_%s.4bpp")

    def test_normalize_path_only_touches_the_stem(self):
        # A compound extension survives; only the first component is stripped.
        self.assertEqual(prr._normalize_path("src/gfx/status_en.4bpp.lz"),
                         "src/gfx/status.4bpp.lz")

    def test_default_name_from_a_single_file(self):
        self.assertEqual(prr._default_name("src/field/map_prop.dat"),
                         "MAP_PROP")
        self.assertEqual(prr._default_name("src/menu/item_prop_en.dat"),
                         "ITEM_PROP")

    def test_default_name_from_a_globbed_directory(self):
        self.assertEqual(prr._default_name("src/gfx/battle_bg_gfx/%s.4bpp.lz"),
                         "BATTLE_BG_GFX")
        self.assertEqual(prr._default_name("src/sound/sfx_brr/sfx_%s.brr"),
                         "SFX_BRR")

    def test_asset_name_applies_a_role_suffix(self):
        self.assertEqual(prr._asset_name("src/gfx/world_1_bg.4bpp.lz", None,
                                         "list"), "WORLD_1_BG_GFX")
        self.assertEqual(prr._asset_name("src/gfx/world_1_bg.pal", None,
                                         "list"), "WORLD_1_BG_PAL")

    def test_asset_name_splits_the_stencils_by_record_width(self):
        path = "src/gfx/monster_gfx/%s.stn"
        self.assertEqual(prr._asset_name(path, 8, "list"),
                         "MONSTER_STENCIL_SMALL")
        self.assertEqual(prr._asset_name(path, 32, "list"),
                         "MONSTER_STENCIL_LARGE")

    def test_asset_name_rejects_an_unknown_record_width(self):
        with self.assertRaises(ParseError) as caught:
            prr._asset_name("src/gfx/monster_gfx/%s.stn", 16, "list")
        self.assertIn("no name", str(caught.exception))

    def test_family_key_joins_the_two_languages(self):
        self.assertEqual(prr._family_key("src/text/dlg1_en.json", None),
                         prr._family_key("src/text/dlg1_jp.json", None))

    def test_family_key_separates_the_stencil_widths(self):
        path = "src/gfx/monster_gfx/%s.stn"
        self.assertNotEqual(prr._family_key(path, 8), prr._family_key(path, 32))

    def test_parse_range_accepts_either_prefix_case(self):
        self.assertEqual(prr._parse_range("0xC478C0-0xC47A3F", "l", "p"),
                         (0xC478C0, 0xC47A3F))
        self.assertEqual(prr._parse_range("0XCF3B40-0XCF3CFF", "l", "p"),
                         (0xCF3B40, 0xCF3CFF))

    def test_parse_range_rejects_a_malformed_range(self):
        for text in ("0xC478C0", "C478C0-C47A3F", "0xC478C0:0xC47A3F", None):
            with self.assertRaises(ParseError):
                prr._parse_range(text, "l", "p")

    def test_parse_range_rejects_a_backwards_range(self):
        with self.assertRaises(ParseError) as caught:
            prr._parse_range("0xC47A3F-0xC478C0", "l", "p")
        self.assertIn("ends before it begins", str(caught.exception))


# --- Layer 2: synthetic inputs -----------------------------------------------

class ListTests(unittest.TestCase):

    def setUp(self):
        self.tree = _Tree()
        self.addCleanup(self.tree.close)

    def test_minimal_tree_parses(self):
        rows = prr.read_rip_list(self.tree.root, "en", ROM_SIZE)
        self.assertEqual([r.asset for r in rows], ["CHAR_NAME", "MAP_PARALLAX"])
        self.assertEqual(rows[0].size, 384)

    def test_size_covers_the_inclusive_range(self):
        rows = prr.read_rip_list(self.tree.root, "en", ROM_SIZE)
        self.assertEqual(rows[0].begin, 0xC478C0)
        self.assertEqual(rows[0].end, 0xC47A3F)
        self.assertEqual(rows[0].size, rows[0].end - rows[0].begin + 1)

    def test_a_range_below_the_rom_region_is_rejected(self):
        lists = _minimal_lists()
        lists["data"][0]["asset_range"] = "0x7E0000-0x7E00FF"
        tree = _Tree(lists=lists)
        self.addCleanup(tree.close)
        with self.assertRaises(ParseError) as caught:
            prr.read_rip_list(tree.root, "en", ROM_SIZE)
        self.assertIn("outside the ROM region", str(caught.exception))

    def test_a_range_past_the_rom_end_is_rejected(self):
        lists = _minimal_lists()
        lists["data"][0]["asset_range"] = "0xEFFF00-0xF00000"
        tree = _Tree(lists=lists)
        self.addCleanup(tree.close)
        with self.assertRaises(ParseError):
            prr.read_rip_list(tree.root, "en", ROM_SIZE)

    def test_a_missing_section_is_rejected(self):
        lists = _minimal_lists()
        del lists["array"]
        tree = _Tree(lists=lists)
        self.addCleanup(tree.close)
        with self.assertRaises(ParseError) as caught:
            prr.read_rip_list(tree.root, "en", ROM_SIZE)
        self.assertIn("missing section", str(caught.exception))

    def test_an_entry_without_an_output_path_is_rejected(self):
        lists = _minimal_lists()
        lists["data"][0] = {"asset_range": "0xC00000-0xC000FF"}
        tree = _Tree(lists=lists)
        self.addCleanup(tree.close)
        with self.assertRaises(ParseError) as caught:
            prr.read_rip_list(tree.root, "en", ROM_SIZE)
        self.assertIn("neither json_path nor file_path", str(caught.exception))

    def test_a_family_listed_twice_in_one_language_is_rejected(self):
        lists = _minimal_lists()
        lists["data"].append(dict(lists["data"][0]))
        tree = _Tree(lists=lists)
        self.addCleanup(tree.close)
        rows = {lang: prr.read_rip_list(tree.root, lang, ROM_SIZE)
                for lang in ("en", "jp")}
        order = prr.build_asset_order(rows)
        with self.assertRaises(ParseError) as caught:
            prr.build_region_rows(rows, order)
        self.assertIn("appears twice", str(caught.exception))


class RoleNameTests(unittest.TestCase):

    def test_a_new_shared_stem_is_rejected(self):
        # Two families whose stems collide, with no role name to tell them
        # apart: naming them is a surface decision, so the parse stops.
        lists = _minimal_lists()
        lists["data"] = [
            {"file_path": "src/gfx/sample.4bpp", "asset_range":
             "0xC00000-0xC000FF"},
            {"file_path": "src/gfx/sample.pal", "asset_range":
             "0xC00100-0xC0011F"},
        ]
        tree = _Tree(lists=lists)
        self.addCleanup(tree.close)
        rows = {lang: prr.read_rip_list(tree.root, lang, ROM_SIZE)
                for lang in ("en", "jp")}
        with self.assertRaises(ParseError) as caught:
            prr.assert_role_names_complete(rows, tree.root)
        self.assertIn("SAMPLE", str(caught.exception))

    def test_a_role_name_with_nothing_left_to_disambiguate_is_rejected(self):
        tree = _Tree()
        self.addCleanup(tree.close)
        rows = {lang: prr.read_rip_list(tree.root, lang, ROM_SIZE)
                for lang in ("en", "jp")}
        with self.assertRaises(ParseError) as caught:
            prr.assert_role_names_complete(rows, tree.root)
        self.assertIn("stale", str(caught.exception))


class RomFactTests(unittest.TestCase):

    def setUp(self):
        self.tree = _Tree()
        self.addCleanup(self.tree.close)

    def test_rom_size_is_the_bank_extent(self):
        # $ee0000 + $20000 == $f00000, minus the $c00000 base.
        self.assertEqual(prr.read_rom_size(self.tree.root), 0x300000)

    def test_rom_size_ignores_the_wram_bank(self):
        # bank_7e sits below the ROM region and must not set the extent.
        self.assertLess(prr.read_rom_size(self.tree.root), 0x800000)

    def test_rom_size_rejects_a_config_with_no_rom_banks(self):
        tree = _Tree(cfg="memory {\n    bank_7e: start = $7e5000, "
                         "size = $3000, type = ro;\n}\n")
        self.addCleanup(tree.close)
        with self.assertRaises(ParseError) as caught:
            prr.read_rom_size(tree.root)
        self.assertIn("no ROM-region banks", str(caught.exception))

    def test_copier_header_size(self):
        self.assertEqual(prr.read_copier_header_bytes(self.tree.root), 512)

    def test_copier_header_rejects_two_different_sizes(self):
        tree = _Tree(extract=_EXTRACT_ASSETS
                     + "\n    # a 1024-byte copier header\n")
        self.addCleanup(tree.close)
        with self.assertRaises(ParseError) as caught:
            prr.read_copier_header_bytes(tree.root)
        self.assertIn("one copier-header size", str(caught.exception))

    def test_identities_are_in_game_version_order(self):
        identities = prr.read_rom_identities(self.tree.root)
        self.assertEqual([v for v, _c, _n in identities],
                         ["JP_1_0", "US_1_0", "US_1_1"])
        self.assertEqual([c for _v, c, _n in identities],
                         [0x45EF5AC8, 0xA27F1C7A, 0xC0FA0464])

    def test_a_fourth_revision_is_rejected(self):
        tree = _Tree(extract=_EXTRACT_ASSETS.replace(
            "        else:",
            "        elif crc32 == 0x12345678:\n"
            "            rom_name = 'Final Fantasy VI 2.0 (J)'\n"
            "            rom_language = 'jp'\n"
            "        else:"))
        self.addCleanup(tree.close)
        with self.assertRaises(ParseError) as caught:
            prr.read_rom_identities(tree.root)
        self.assertIn("unknown ROM name", str(caught.exception))

    def test_a_missing_revision_is_rejected(self):
        tree = _Tree(extract=_EXTRACT_ASSETS.replace(
            "        elif crc32 == 0xC0FA0464:\n"
            "            rom_name = 'Final Fantasy III 1.1 (U)'\n"
            "            rom_language = 'en'\n", ""))
        self.addCleanup(tree.close)
        with self.assertRaises(ParseError) as caught:
            prr.read_rom_identities(tree.root)
        self.assertIn("expected 3 CRC branches", str(caught.exception))

    def test_a_revision_assigned_the_wrong_list_is_rejected(self):
        tree = _Tree(extract=_EXTRACT_ASSETS.replace(
            "            rom_name = 'Final Fantasy VI 1.0 (J)'\n"
            "            rom_language = 'jp'",
            "            rom_name = 'Final Fantasy VI 1.0 (J)'\n"
            "            rom_language = 'en'"))
        self.addCleanup(tree.close)
        with self.assertRaises(ParseError) as caught:
            prr.read_rom_identities(tree.root)
        self.assertIn("is assigned the 'en' list", str(caught.exception))


class FixedTextSizeTests(unittest.TestCase):

    def test_a_fixed_range_holding_exactly_its_records_passes(self):
        tree = _Tree()
        self.addCleanup(tree.close)
        rows = prr.read_rip_list(tree.root, "en", ROM_SIZE)
        metadata = prr.read_text_metadata(tree.root)
        self.assertEqual(prr.assert_fixed_text_sizes(rows, metadata,
                                                     tree.root), 1)

    def test_a_short_fixed_range_is_rejected(self):
        lists = _minimal_lists()
        lists["text"][0]["asset_range"] = "0xC478C0-0xC47A3E"  # one byte short
        tree = _Tree(lists=lists)
        self.addCleanup(tree.close)
        rows = prr.read_rip_list(tree.root, "en", ROM_SIZE)
        metadata = prr.read_text_metadata(tree.root)
        with self.assertRaises(ParseError) as caught:
            prr.assert_fixed_text_sizes(rows, metadata, tree.root)
        self.assertIn("records need", str(caught.exception))

    def test_a_text_class_with_no_range_is_rejected(self):
        lists = _minimal_lists()
        lists["text"] = []
        tree = _Tree(lists=lists)
        self.addCleanup(tree.close)
        rows = prr.read_rip_list(tree.root, "en", ROM_SIZE)
        metadata = prr.read_text_metadata(tree.root)
        with self.assertRaises(ParseError) as caught:
            prr.assert_fixed_text_sizes(rows, metadata, tree.root)
        self.assertIn("no range", str(caught.exception))

    def test_unparsable_metadata_is_rejected(self):
        tree = _Tree(metadata="// nothing this parser recognizes\n")
        self.addCleanup(tree.close)
        with self.assertRaises(ParseError) as caught:
            prr.read_text_metadata(tree.root)
        self.assertIn("no metadata rows", str(caught.exception))


class OrderingTests(unittest.TestCase):

    def setUp(self):
        self.tree = _Tree()
        self.addCleanup(self.tree.close)
        self.rows = {lang: prr.read_rip_list(self.tree.root, lang, ROM_SIZE)
                     for lang in ("en", "jp")}

    def test_assets_are_ordered_by_section(self):
        order = prr.build_asset_order(self.rows)
        self.assertEqual(order, [("text", "CHAR_NAME"),
                                 ("data", "MAP_PARALLAX")])

    def test_region_rows_are_sorted_by_asset_then_language(self):
        order = prr.build_asset_order(self.rows)
        rows = prr.build_region_rows(self.rows, order)
        self.assertEqual([(r.asset, r.language) for r in rows],
                         [("CHAR_NAME", "jp"), ("CHAR_NAME", "en"),
                          ("MAP_PARALLAX", "jp"), ("MAP_PARALLAX", "en")])

    def test_a_language_only_family_still_gets_an_id(self):
        lists = _minimal_lists()
        tree_all = _Tree(lists=lists)
        self.addCleanup(tree_all.close)
        rows = {"en": prr.read_rip_list(tree_all.root, "en", ROM_SIZE),
                "jp": prr.read_rip_list(tree_all.root, "jp", ROM_SIZE)}
        rows["en"] = [r for r in rows["en"] if r.asset != "MAP_PARALLAX"]
        order = prr.build_asset_order(rows)
        self.assertIn(("data", "MAP_PARALLAX"), order)
        region_rows = prr.build_region_rows(rows, order)
        parallax = [r for r in region_rows if r.asset == "MAP_PARALLAX"]
        self.assertEqual([r.language for r in parallax], ["jp"])


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "tools", "rip_list_en.json")):
        return root
    return None


def _repo_root():
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")


@unittest.skipUnless(_find_source_root(),
                     "original-src rip lists not present")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.root = _find_source_root()
        cls.repo = _repo_root()
        cls.size = prr.read_rom_size(cls.root)
        cls.rows = {lang: prr.read_rip_list(cls.root, lang, cls.size)
                    for lang in ("en", "jp")}
        cls.order = prr.build_asset_order(cls.rows)
        cls.regions = prr.build_region_rows(cls.rows, cls.order)

    def test_corpus_counts(self):
        self.assertEqual(len(self.rows["en"]), 155)
        self.assertEqual(len(self.rows["jp"]), 154)
        self.assertEqual(len(self.order), 156)
        self.assertEqual(len(self.regions), 309)

    def test_rom_facts(self):
        self.assertEqual(self.size, 3145728)
        self.assertEqual(prr.read_copier_header_bytes(self.root), 512)
        identities = prr.read_rom_identities(self.root)
        self.assertEqual(identities[2][0], "US_1_1")
        self.assertEqual(identities[2][1], 0xC0FA0464)

    def test_every_shared_stem_has_a_role_name(self):
        prr.assert_role_names_complete(self.rows, self.root)

    def test_world_mod_tiles_matches_the_shipped_block(self):
        # The 1.K world-modification tile pool: 1,182 bytes in both languages,
        # at different places.
        by_language = {r.language: r for r in self.regions
                       if r.asset == "WORLD_MOD_TILES"}
        self.assertEqual(by_language["en"].begin, 0xCEF648)
        self.assertEqual(by_language["en"].size, 1182)
        self.assertEqual(by_language["jp"].begin, 0xCEB048)
        self.assertEqual(by_language["jp"].size, 1182)

    def test_char_name_matches_the_shipped_text_metadata(self):
        # 64 characters x 6 bytes, the 1.H fixed-record contract.
        row = next(r for r in self.regions
                   if r.asset == "CHAR_NAME" and r.language == "en")
        self.assertEqual(row.begin, 0xC478C0)
        self.assertEqual(row.size, 64 * 6)

    def test_dialogue_crosses_a_bank_boundary(self):
        # The VM resolves regions in decoded address space, so a family wider
        # than the bank it starts in is read straight through.
        row = next(r for r in self.regions
                   if r.asset == "DLG1" and r.language == "en")
        self.assertNotEqual(row.begin >> 16, row.end >> 16)

    def test_language_only_families(self):
        languages = {}
        for row in self.regions:
            languages.setdefault(row.asset, set()).add(row.language)
        self.assertEqual(languages["ITEM_TYPE_NAME"], {"en"})
        self.assertEqual(languages["CHAR_TITLE"], {"jp"})

    def test_the_shared_stems_resolve_to_distinct_ids(self):
        names = [name for _section, name in self.order]
        self.assertEqual(len(names), len(set(names)))
        for name in ("WORLD_1_BG_GFX", "WORLD_1_BG_PAL",
                     "MONSTER_STENCIL_SMALL", "MONSTER_STENCIL_LARGE",
                     "VECTOR_APPROACH_GFX", "VECTOR_APPROACH_TILEMAP",
                     "VECTOR_APPROACH_PAL", "ATTACK_GFX_2BPP",
                     "ATTACK_GFX_3BPP"):
            self.assertIn(name, names)

    def test_fixed_text_ranges_hold_exactly_their_records(self):
        metadata = prr.read_text_metadata(self.repo)
        self.assertEqual(
            prr.assert_fixed_text_sizes(self.rows["en"], metadata, self.repo),
            16)

    def test_en_ranges_are_byte_identical_to_the_rom(self):
        result = prr.assert_ranges_match_rom(self.rows["en"], self.root)
        if result is None:
            self.skipTest("no vanilla ROM available (set FF6_VANILLA_ROM)")
        checked, _missing = result
        self.assertGreater(checked, 0)

    def test_run_check_only_ok(self):
        self.assertEqual(prr.run(self.root, self.repo, check_only=True), 0)


if __name__ == "__main__":
    unittest.main()
