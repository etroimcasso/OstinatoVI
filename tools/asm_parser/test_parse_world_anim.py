#!/usr/bin/env python3
"""Unit tests for parse_world_anim.py.

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
import parse_world_anim as pwa  # noqa: E402
from common import ParseError  # noqa: E402


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
SOURCE_ROOT = os.path.join(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))), "original-src")


def _source_root():
    """The disassembly root, if this machine has one."""
    for candidate in (os.path.join(REPO_ROOT, "original-src"), SOURCE_ROOT):
        if os.path.isfile(os.path.join(candidate, "src", "world",
                                       "world_anim.asm")):
            return candidate
    return None


class GroupHeadingTest(unittest.TestCase):
    """Only a heading naming the frame below it counts as that frame's."""

    def test_single_frame_heading(self):
        self.assertEqual(pwa._group_heading("$00: no animation", 0),
                         "no animation")

    def test_range_heading_names_its_first_frame(self):
        self.assertEqual(pwa._group_heading("$01-$12: airship", 1), "airship")

    def test_range_heading_does_not_name_a_later_frame(self):
        self.assertIsNone(pwa._group_heading("$01-$12: airship", 2))

    def test_range_missing_the_second_dollar_is_still_a_heading(self):
        # world_anim.asm writes one heading as `$5f-61: bird`.
        self.assertEqual(pwa._group_heading("$5f-61: bird", 0x5F), "bird")

    def test_a_heading_for_another_frame_is_not_taken(self):
        self.assertIsNone(pwa._group_heading("$03: vhoopppm", 0))

    def test_non_headings_are_ignored(self):
        self.assertIsNone(pwa._group_heading(None, 0))
        self.assertIsNone(pwa._group_heading("", 0))
        self.assertIsNone(pwa._group_heading("pointers to sprite data", 0))
        self.assertIsNone(pwa._group_heading(
            "-" * 40, 0))


class ByteValuesTest(unittest.TestCase):
    def test_hex_and_decimal_terms(self):
        self.assertEqual(pwa._byte_values("12", "x", 1), [12])
        self.assertEqual(pwa._byte_values("$f4,$f0,$40,$10", "x", 1),
                         [0xF4, 0xF0, 0x40, 0x10])

    def test_unparsable_term_is_an_error(self):
        with self.assertRaises(ParseError):
            pwa._byte_values("$f4,nonsense", "x", 1)

    def test_out_of_range_term_is_an_error(self):
        with self.assertRaises(ParseError):
            pwa._byte_values("$1ff", "x", 1)


class SyntheticSourceTest(unittest.TestCase):
    """The record walker against hand-made world_anim.asm fragments."""

    HEADER = (
        "; animation data format is 1 byte for the number of sprites\n"
        ";   $00: x position\n"
        ";   $03: vhoopppm\n"
        "\n"
        "WorldAnimSpritePtrs:\n"
        "@0100:\n"
        ".repeat $04, i\n"
        "        .addr   .ident(.sprintf(\"WorldAnimSprite_%02x\", i)) - "
        "WorldAnimSprites\n"
        ".endrep\n"
        "\n"
        "WorldAnimSprites:\n"
        "\n")

    def _write(self, body, directory):
        path = os.path.join(directory, "src", "world")
        os.makedirs(path)
        full = os.path.join(path, "world_anim.asm")
        with open(full, "w", encoding="utf-8") as fh:
            fh.write(self.HEADER + body)
        return full

    def _read(self, body):
        with tempfile.TemporaryDirectory() as directory:
            self._write(body, directory)
            return pwa.read_records(directory)

    def test_a_zero_length_label_shares_the_next_records_offset(self):
        body = ("; $00: no animation\n"
                "WorldAnimSprite_00:\n"
                "\n"
                "; $01-$03: thing\n"
                "WorldAnimSprite_01:\n"
                "@0108:  .byte   1\n"
                "        .byte   $01,$02,$03,$04\n"
                "\n"
                "WorldAnimSprite_02:\n"
                "@010d:  .byte   1\n"
                "        .byte   $05,$06,$07,$08\n"
                "\n"
                "WorldAnimSprite_03:\n"
                "@0112:  .byte   1\n"
                "        .byte   $09,$0a,$0b,$0c\n")
        _path, count, records, groups = self._read(body)
        self.assertEqual(count, 4)
        self.assertEqual(len(records), 3)
        self.assertEqual(records[0].frames, [0, 1])
        self.assertEqual(records[0].address, 0x0108)
        self.assertEqual(groups, {0: "no animation", 1: "thing"})

    def test_two_labels_on_one_record_are_kept_as_one_record(self):
        body = ("WorldAnimSprite_00:\n"
                "@0108:  .byte   1\n"
                "        .byte   $01,$02,$03,$04\n"
                "\n"
                "WorldAnimSprite_01:\n"
                "WorldAnimSprite_02:\n"
                "@010d:  .byte   1\n"
                "        .byte   $05,$06,$07,$08\n"
                "\n"
                "WorldAnimSprite_03:\n"
                "@0112:  .byte   1\n"
                "        .byte   $09,$0a,$0b,$0c\n")
        _path, _count, records, _groups = self._read(body)
        self.assertEqual(len(records), 3)
        self.assertEqual(records[1].frames, [1, 2])

    def test_the_format_documentation_is_not_read_as_a_heading(self):
        body = ("WorldAnimSprite_00:\n"
                "@0108:  .byte   0\n"
                "\n"
                "WorldAnimSprite_01:\n"
                "@0109:  .byte   0\n"
                "\n"
                "WorldAnimSprite_02:\n"
                "@010a:  .byte   0\n"
                "\n"
                "WorldAnimSprite_03:\n"
                "@010b:  .byte   0\n")
        _path, _count, _records, groups = self._read(body)
        self.assertEqual(groups, {})

    def test_a_record_without_an_address_annotation_is_an_error(self):
        body = ("WorldAnimSprite_00:\n"
                "        .byte   1\n"
                "        .byte   $01,$02,$03,$04\n")
        with self.assertRaises(ParseError) as caught:
            self._read(body)
        self.assertIn("@addr", str(caught.exception))

    def test_a_trailing_label_with_no_record_is_an_error(self):
        body = ("WorldAnimSprite_00:\n"
                "@0108:  .byte   1\n"
                "        .byte   $01,$02,$03,$04\n"
                "\n"
                "WorldAnimSprite_01:\n")
        with self.assertRaises(ParseError) as caught:
            self._read(body)
        self.assertIn("carry no record", str(caught.exception))

    def test_a_missing_repeat_directive_is_an_error(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "src", "world")
            os.makedirs(path)
            full = os.path.join(path, "world_anim.asm")
            with open(full, "w", encoding="utf-8") as fh:
                fh.write("WorldAnimSprites:\nWorldAnimSprite_00:\n"
                         "@0100:  .byte 0\n")
            with self.assertRaises(ParseError) as caught:
                pwa.read_records(directory)
            self.assertIn(".repeat", str(caught.exception))


class SyntheticSequenceTest(unittest.TestCase):
    """The frame-sequence reader against hand-made sprite.asm fragments."""

    def _read(self, body, frame_count=108):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "src", "world")
            os.makedirs(path)
            with open(os.path.join(path, "sprite.asm"), "w",
                      encoding="utf-8") as fh:
                fh.write(body)
            return pwa.read_frame_sequences(directory, frame_count)

    BODY = ("_ee4de3:\n"
            "@4de3:  .byte   $49,$4a,$49,$4b,$4c\n"
            "\n"
            "_ee5196:\n"
            "@5196:  .byte   $00,$00,$00,$00,$00,$00\n"
            "        .byte   $00,$56,$56,$56,$56,$56\n"
            "        .byte   $00,$57,$57,$56,$56,$56\n"
            "\n"
            "_ee51a8:\n"
            "@51a8:  .byte   $00,$59,$58,$57,$56,$56\n"
            "        .byte   $00\n"
            "\n"
            "_ee5350:\n"
            "@5350:  .byte   $5f,$60,$61,$60\n")

    def test_the_two_airship_labels_join_into_one_table(self):
        sequences = self._read(self.BODY)
        self.assertEqual(len(sequences), 3)
        airship = [s for s in sequences if s.label == "_ee5196"][0]
        self.assertEqual(len(airship.values), 25)
        self.assertEqual(airship.values[18], 0x00)
        self.assertEqual(airship.values[19], 0x59)

    def test_a_gap_between_the_two_airship_labels_is_an_error(self):
        body = self.BODY.replace("@51a8:", "@51b0:")
        with self.assertRaises(ParseError) as caught:
            self._read(body)
        self.assertIn("one run", str(caught.exception))

    def test_a_value_past_the_frame_space_is_an_error(self):
        body = self.BODY.replace("$5f,$60,$61,$60", "$5f,$60,$61,$ff")
        with self.assertRaises(ParseError) as caught:
            self._read(body)
        self.assertIn("only 108 frames exist", str(caught.exception))

    def test_a_missing_table_is_an_error(self):
        body = self.BODY.replace("_ee5350:", "_ee5351:")
        with self.assertRaises(ParseError) as caught:
            self._read(body)
        self.assertIn("_ee5350", str(caught.exception))


@unittest.skipIf(_source_root() is None, "no original-src checkout")
class EndToEndTest(unittest.TestCase):
    """The real corpus: counts, quirks, and the cartridge cross-check."""

    @classmethod
    def setUpClass(cls):
        cls.root = _source_root()
        cls.resolved = pwa.resolve(cls.root)

    def test_frame_count_and_record_count(self):
        self.assertEqual(self.resolved.frame_count, 108)
        # Two frames share a record with another, so there are two fewer
        # records than frames.
        self.assertEqual(len(self.resolved.records), 106)

    def test_region_extent(self):
        self.assertEqual(self.resolved.region_at, 0xEE573E)
        self.assertEqual(len(self.resolved.pointer_bytes), 216)
        self.assertEqual(len(self.resolved.block_bytes), 0x13D6)
        self.assertEqual(self.resolved.region_size, 5294)
        self.assertEqual(self.resolved.block_at, 0xEE5816)

    def test_the_blank_frame_and_the_first_airship_frame_share_an_offset(self):
        self.assertEqual(self.resolved.offsets[0], 0)
        self.assertEqual(self.resolved.offsets[1], 0)
        # The next six step by the airship record's own stride.
        for frame in range(2, 8):
            self.assertEqual(self.resolved.offsets[frame],
                             0x31 * (frame - 1))

    def test_the_aliased_esper_frames_share_a_record(self):
        self.assertEqual(self.resolved.offsets[0x5A],
                         self.resolved.offsets[0x5B])

    def test_the_surplus_row_records_are_the_eight_the_corpus_has(self):
        surplus = self.resolved.surplus_frames
        self.assertEqual([frame for frame, _d, _s in surplus],
                         list(pwa.SURPLUS_ROW_FRAMES))
        for frame, declared, stored in surplus:
            self.assertGreater(stored, declared)
            if frame <= 0x44:
                self.assertEqual((declared, stored), (2, 4))
            else:
                self.assertEqual((declared, stored), (4, 6))

    def test_every_group_heading_names_the_frame_it_opens(self):
        # The headings come from the corpus, so their count is the corpus's.
        self.assertEqual(len(self.resolved.groups), 18)
        self.assertEqual(self.resolved.groups[0], "no animation")
        self.assertEqual(self.resolved.groups[1], "airship")
        self.assertEqual(self.resolved.groups[0x5F], "bird")

    def test_the_frame_sequences_hold_what_the_corpus_holds(self):
        by_label = {s.label: s for s in self.resolved.sequences}
        self.assertEqual(by_label["_ee4de3"].values,
                         [0x49, 0x4A, 0x49, 0x4B, 0x4C])
        self.assertEqual(by_label["_ee5350"].values, [0x5F, 0x60, 0x61, 0x60])

        airship = by_label["_ee5196"].values
        self.assertEqual(len(airship), 43)
        # Whole rows of six plus the trailing entry the last row overruns onto,
        # every row opening on the blank frame.
        self.assertEqual(len(airship) % pwa.SMOKING_AIRSHIP_ROW_STRIDE, 1)
        for step in range(0, len(airship), pwa.SMOKING_AIRSHIP_ROW_STRIDE):
            self.assertEqual(airship[step], 0)
        self.assertEqual(airship[-1], 0)

    @unittest.skipIf(common.find_vanilla_rom(_source_root()) is None,
                     "no vanilla cartridge (set FF6_VANILLA_ROM)")
    def test_the_region_and_the_sequences_match_the_cartridge(self):
        note = pwa.assert_matches_rom(self.resolved, self.root)
        self.assertIsNotNone(note)
        self.assertIn("5294", note)


if __name__ == "__main__":
    unittest.main()
