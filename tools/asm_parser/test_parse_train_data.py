#!/usr/bin/env python3
"""Unit tests for parse_train_data.py.

Three layers, matching the parser test discipline: pure helpers against hand-made
inputs, synthetic assembly fragments exercising the grammar's edges and its error
paths, and an end-to-end pass over the real disassembly (skipped when it is not
present) that pins the corpus's own counts and quirks.
"""

from __future__ import annotations

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import common  # noqa: E402
import parse_train_data as ptd  # noqa: E402
from common import ParseError  # noqa: E402


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def _source_root():
    """The disassembly root, if this machine has one."""
    candidate = os.path.join(REPO_ROOT, "original-src")
    if os.path.isfile(os.path.join(candidate, "src", "world",
                                   "train_script.asm")):
        return candidate
    return None


def _write_asm(tmpdir, rel_path, text):
    """Lay a fragment out the way the parser expects to find it."""
    path = os.path.join(tmpdir, "src", rel_path)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path


class SignedByteTest(unittest.TestCase):
    """The steering curves hold signed offsets, so the top half of the byte
    range reads as a negative step rather than a large positive one."""

    def test_zero_and_the_positive_half(self):
        self.assertEqual(ptd.as_signed(0), 0)
        self.assertEqual(ptd.as_signed(1), 1)
        self.assertEqual(ptd.as_signed(127), 127)

    def test_the_negative_half(self):
        self.assertEqual(ptd.as_signed(128), -128)
        self.assertEqual(ptd.as_signed(0xF4), -12)
        self.assertEqual(ptd.as_signed(255), -1)


class ReadLabeledTableTest(unittest.TestCase):
    """The body reader takes the values under a label and the address the
    source puts on its first data line."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def test_reads_values_and_address(self):
        _write_asm(self.tmp.name, "world/x.asm",
                   "sample:\n@1234:  .byte   $01,$02,$03\n")
        table = ptd.read_labeled_table(self.tmp.name, "world/x.asm", "sample",
                                       "byte", 3, "sample")
        self.assertEqual(table.values, [1, 2, 3])
        self.assertEqual(table.address, 0x1234)
        self.assertEqual(table.snes, 0xEE1234)
        self.assertEqual(table.width, 1)

    def test_skips_stacked_alias_labels(self):
        # Every table in this unit carries at least two labels; the address one
        # sits on the data line itself.
        _write_asm(self.tmp.name, "world/x.asm",
                   "_ee2692:\nsindat:\n@2692:  .byte   $04,$05\n")
        table = ptd.read_labeled_table(self.tmp.name, "world/x.asm", "_ee2692",
                                       "byte", 2, "sindat")
        self.assertEqual(table.values, [4, 5])
        self.assertEqual(table.address, 0x2692)

    def test_words_are_little_endian_values(self):
        _write_asm(self.tmp.name, "world/x.asm",
                   "sample:\n@0d5c:  .word   $9c7a,$9d73\n")
        table = ptd.read_labeled_table(self.tmp.name, "world/x.asm", "sample",
                                       "word", 2, "sample")
        self.assertEqual(table.values, [0x9C7A, 0x9D73])
        self.assertEqual(table.width, 2)

    def test_decimal_terms_parse(self):
        # StrafeAngleTbl writes its bearings in decimal.
        _write_asm(self.tmp.name, "world/x.asm",
                   "sample:\n@6d2f:  .word   0,270,90\n")
        table = ptd.read_labeled_table(self.tmp.name, "world/x.asm", "sample",
                                       "word", 3, "sample")
        self.assertEqual(table.values, [0, 270, 90])

    def test_body_ends_at_the_next_non_data_line(self):
        _write_asm(self.tmp.name, "world/x.asm",
                   "sample:\n@1000:  .byte   $01\n\nnext:\n"
                   "@1001:  .byte   $ff\n")
        table = ptd.read_labeled_table(self.tmp.name, "world/x.asm", "sample",
                                       "byte", 1, "sample")
        self.assertEqual(table.values, [1])

    def test_comments_are_stripped(self):
        _write_asm(self.tmp.name, "world/x.asm",
                   "; leading note\nsample:\n@1000:  .byte   $01,$02   ; two\n")
        table = ptd.read_labeled_table(self.tmp.name, "world/x.asm", "sample",
                                       "byte", 2, "sample")
        self.assertEqual(table.values, [1, 2])

    def test_missing_label_is_an_error(self):
        _write_asm(self.tmp.name, "world/x.asm", "other:\n@1000:  .byte $01\n")
        with self.assertRaises(ParseError):
            ptd.read_labeled_table(self.tmp.name, "world/x.asm", "sample",
                                   "byte", 1, "sample")

    def test_wrong_count_is_an_error(self):
        _write_asm(self.tmp.name, "world/x.asm",
                   "sample:\n@1000:  .byte   $01,$02\n")
        with self.assertRaises(ParseError):
            ptd.read_labeled_table(self.tmp.name, "world/x.asm", "sample",
                                   "byte", 3, "sample")

    def test_wrong_directive_is_an_error(self):
        _write_asm(self.tmp.name, "world/x.asm",
                   "sample:\n@1000:  .word   $0102\n")
        with self.assertRaises(ParseError):
            ptd.read_labeled_table(self.tmp.name, "world/x.asm", "sample",
                                   "byte", 1, "sample")

    def test_a_table_with_no_address_line_is_an_error(self):
        _write_asm(self.tmp.name, "world/x.asm", "sample:\n    .byte   $01\n")
        with self.assertRaises(ParseError):
            ptd.read_labeled_table(self.tmp.name, "world/x.asm", "sample",
                                   "byte", 1, "sample")

    def test_an_unparsable_term_is_an_error(self):
        _write_asm(self.tmp.name, "world/x.asm",
                   "sample:\n@1000:  .byte   SOME_SYMBOL\n")
        with self.assertRaises(ParseError):
            ptd.read_labeled_table(self.tmp.name, "world/x.asm", "sample",
                                   "byte", 1, "sample")

    def test_an_unexpected_line_under_the_label_is_an_error(self):
        _write_asm(self.tmp.name, "world/x.asm", "sample:\n    lda   $00\n")
        with self.assertRaises(ParseError):
            ptd.read_labeled_table(self.tmp.name, "world/x.asm", "sample",
                                   "byte", 1, "sample")


class _FakeTable(object):
    """Enough of a Table for the structural checks."""

    def __init__(self, values, name="fake"):
        self.values = values
        self.name = name
        self.path = "world/fake.asm"
        self.line = 1


class StructuralCheckTest(unittest.TestCase):
    """Each check is the parser's guard against a corpus change being absorbed
    silently rather than stopping the emit."""

    def test_a_curve_that_does_not_divide_into_items_is_an_error(self):
        with self.assertRaises(ParseError):
            ptd._assert_divides(_FakeTable([0] * 33), 32, "curve items")

    def test_a_curve_that_divides_reports_its_item_count(self):
        self.assertEqual(
            ptd._assert_divides(_FakeTable([0] * 96), 32, "curve items"), 3)

    def test_a_background_byte_past_the_arrangements_is_an_error(self):
        with self.assertRaises(ParseError):
            ptd.assert_background_bytes_name_a_variant(
                _FakeTable([0, 19, 20]), 20)

    def test_background_bytes_inside_the_arrangements_pass(self):
        ptd.assert_background_bytes_name_a_variant(_FakeTable([0, 19]), 20)

    def test_the_camera_curve_mirror_tolerates_only_the_known_pair(self):
        rows = [[i] for i in range(32)]
        for i in range(15):
            rows[30 - i] = list(rows[i])
        rows[23] = [99]                    # the corpus's own divergence
        ptd.assert_liftoff_mirror(rows, "world/fake.asm", 1)

        rows[22] = [98]                    # a second one stops the parse
        with self.assertRaises(ParseError):
            ptd.assert_liftoff_mirror(rows, "world/fake.asm", 1)

    def test_a_perfectly_mirrored_camera_curve_is_also_an_error(self):
        # The known divergence is contract; losing it means the source changed.
        rows = [[i] for i in range(32)]
        for i in range(15):
            rows[30 - i] = list(rows[i])
        with self.assertRaises(ParseError):
            ptd.assert_liftoff_mirror(rows, "world/fake.asm", 1)

    def test_circles_must_share_their_opening_radius_and_step(self):
        good = [[0, 0, 0x100, 0x200, 0x1000, 1],
                [0, 0, 0x100, 0x200, 0x0d00, 2]]
        self.assertEqual(
            ptd.assert_circle_constants(good, "world/fake.asm", 1),
            (0x100, 0x200))

        bad = [list(good[0]), [0, 0, 0x101, 0x200, 0x0d00, 2]]
        with self.assertRaises(ParseError):
            ptd.assert_circle_constants(bad, "world/fake.asm", 1)

    def test_a_strafe_value_that_is_not_a_bearing_is_an_error(self):
        ptd.assert_strafe_angles(_FakeTable([0, 359]))
        with self.assertRaises(ParseError):
            ptd.assert_strafe_angles(_FakeTable([0, 360]))


class RenderTest(unittest.TestCase):
    """Rows are self-labeling: every value carries its field name, and the
    row's identity is a field rather than its position in the file."""

    def _table(self, values, directive="byte"):
        return ptd.Table("sample", "world/fake.asm", "sample", directive,
                         values, 0x1000, 1)

    def test_a_signed_curve_renders_negative_steps(self):
        text = ptd.render_curve_inc(self._table([0, 0xF4]), "kSample",
                                    "// doc", signed=True)
        self.assertIn("{ .index = 0, .value =    0 },", text)
        self.assertIn("{ .index = 1, .value =  -12 },", text)

    def test_an_unsigned_curve_renders_magnitudes(self):
        text = ptd.render_curve_inc(self._table([0, 0xF4]), "kSample",
                                    "// doc", signed=False)
        self.assertIn("{ .index = 1, .value =  244 },", text)

    def test_every_row_carries_the_regeneration_command(self):
        text = ptd.render_curve_inc(self._table([0]), "kSample", "// doc",
                                    signed=False)
        self.assertIn("AUTO-GENERATED by tools/asm_parser/parse_train_data.py",
                      text)
        self.assertIn("DO NOT EDIT BY HAND", text)
        self.assertIn("--source-root original-src --repo-root .", text)

    def test_a_tile_row_names_all_four_stored_bytes(self):
        table = self._table([0x1C, 0x20, 0x00, 0x00] * ptd.TILES_PER_VARIANT)
        text = ptd.render_tiles_inc(table, 1)
        self.assertIn(".tile = { .x =  28, .y =  32, .tileIndexLow =   0, "
                      ".tileIndexHigh =   0 }", text)

    def test_a_camera_row_names_the_transform_terms(self):
        table = self._table([0x0E, 0x00, 0x12, 0x00, 0x01, 0x02, 0x03])
        text = ptd.render_liftoff_inc(table, [table.values])
        self.assertIn(".scaleDelta =    14", text)
        self.assertIn(".nearWeightDelta =    18", text)
        self.assertIn(".farWeightDelta =   1", text)
        self.assertIn(".horizonDelta =  2", text)
        self.assertIn(".pivotYDelta =  3", text)

    def test_the_unreached_table_renders_raw_words_and_names_nothing(self):
        text = ptd.render_unused_inc(self._table([0x9C7A], "word"))
        self.assertIn("{ .index =  0, .value = 0x9C7A },", text)
        self.assertIn("named nothing", text)

    def test_the_enum_headers_carry_the_corpus_wording(self):
        text = ptd.render_yaw_type_header(self._table([0]))
        self.assertIn("enum class TrainYawType : std::uint8_t {", text)
        self.assertIn("STRAIGHT", text)
        self.assertIn("// left turn", text)

        text = ptd.render_background_type_header(self._table([0]))
        self.assertIn("SPLIT_TRACK_FEWER_WALLS =  4,  "
                      "// split track w/ fewer walls", text)


@unittest.skipIf(_source_root() is None, "disassembly not present")
class EndToEndTest(unittest.TestCase):
    """The whole run against the real corpus, including the cartridge compare.
    These are the counts and quirks the port is built on."""

    @classmethod
    def setUpClass(cls):
        cls.source_root = _source_root()
        try:
            common.load_vanilla_rom(cls.source_root)
        except Exception as exc:                     # noqa: BLE001
            raise unittest.SkipTest(
                "vanilla cartridge not available: {}".format(exc))
        cls.res = ptd.load_and_resolve(cls.source_root)

    def test_curve_item_counts(self):
        self.assertEqual(self.res.pitch_items, 13)
        self.assertEqual(self.res.yaw_items, 3)
        self.assertEqual(self.res.background_items, 5)

    def test_the_item_counts_match_the_named_types(self):
        self.assertEqual(self.res.yaw_items, len(ptd.YAW_TYPES))
        self.assertEqual(self.res.background_items, len(ptd.BACKGROUND_TYPES))

    def test_tunnel_arrangements(self):
        self.assertEqual(self.res.variants, 20)
        self.assertEqual(len(self.res.tables["tiles"].values),
                         20 * ptd.VARIANT_BYTES)

    def test_the_first_wall_tile_of_arrangement_one(self):
        # world/train_script.asm:555 — arrangement 1 opens with a wall tile at
        # (28, 32) drawing graphics tile 0.
        tiles = self.res.tables["tiles"].values
        base = 1 * ptd.VARIANT_BYTES
        self.assertEqual(tiles[base:base + 4], [0x1C, 0x20, 0x00, 0x00])

    def test_the_camera_curve_is_thirty_two_frames(self):
        self.assertEqual(len(self.res.liftoff_rows), 32)
        self.assertEqual(len(self.res.liftoff_rows[0]), ptd.LIFTOFF_ROW_BYTES)

    def test_the_camera_curve_peaks_where_the_mirror_folds(self):
        peak = self.res.liftoff_rows[15]
        self.assertEqual(peak[0] | (peak[1] << 8), 0x0600)
        self.assertEqual(peak[2] | (peak[3] << 8), 0x0780)

    def test_the_camera_curves_one_non_mirroring_pair(self):
        low, high = ptd.LIFTOFF_ASYMMETRIC_PAIR
        self.assertNotEqual(self.res.liftoff_rows[low],
                            self.res.liftoff_rows[high])
        # They differ by exactly one unit, in two of the seven bytes.
        differences = [(a, b) for a, b in zip(self.res.liftoff_rows[low],
                                              self.res.liftoff_rows[high])
                       if a != b]
        self.assertEqual(len(differences), 2)
        for a, b in differences:
            self.assertEqual(b - a, 1)

    def test_ending_scene_circles(self):
        self.assertEqual(len(self.res.circle_rows), 9)
        self.assertEqual(self.res.circle_radius, 0x0100)
        self.assertEqual(self.res.circle_step, 0x0200)

    def test_figaro_castle_splits_into_six_byte_rows(self):
        self.assertEqual(len(self.res.figaro_rows), 6)
        # world/event.asm:2264 — the first row's six bytes.
        self.assertEqual(self.res.figaro_rows[0],
                         [0x6D, 0x87, 0x00, 0x00, 0x40, 0x00])

    def test_the_strafe_table_falls_short_of_the_mask_space(self):
        angles = self.res.tables["strafeAngles"].values
        self.assertEqual(len(angles), 11)
        self.assertEqual(self.res.strafe_gap, (11, 12, 13, 14, 15))
        # Up, right, down, left, read off the bitmask's own bit order.
        self.assertEqual(angles[8], 0)
        self.assertEqual(angles[1], 270)
        self.assertEqual(angles[4], 180)
        self.assertEqual(angles[2], 90)

    def test_the_unreached_table_is_forty_eight_words(self):
        self.assertEqual(len(self.res.tables["unusedCutscene"].values), 48)

    def test_a_source_value_that_disagrees_with_the_cartridge_is_an_error(self):
        table = self.res.tables["rotationAngles"]
        rom = common.load_vanilla_rom(self.source_root)
        broken = ptd.Table(table.name, table.path, table.label,
                           table.directive, list(table.values), table.address,
                           table.line)
        broken.values[0] ^= 0xFF
        with self.assertRaises(ParseError):
            ptd.assert_rom_table(rom, broken)

    def test_the_whole_run_emits_without_writing(self):
        ptd.run(self.source_root, REPO_ROOT, check_only=True)


if __name__ == "__main__":
    unittest.main()
