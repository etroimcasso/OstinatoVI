#!/usr/bin/env python3
"""Unit tests for parse_monster_attacks.py.

Three layers per the parser-test discipline:
  1. pure helpers (arg expansion, renderer formatting);
  2. synthetic inputs (macro-body anchors, label anchors, arity, unrecognized
     lines);
  3. end-to-end against the real original-src monster_rage.asm /
     monster_sketch.asm / monster_control.asm + const.inc (skipped cleanly
     when the rip output is absent).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_monster_attacks as pma
from common import ParseError


_RAGE_PROLOGUE = (
    ".mac make_monster_rage attack2\n"
    "        .byte ATTACK::BATTLE, ATTACK::attack2\n"
    ".endmac\n"
    "MonsterRage:\n"
)

_SKETCH_PROLOGUE = (
    ".mac make_monster_sketch attack1, attack2\n"
    "        .byte ATTACK::attack1, ATTACK::attack2\n"
    ".endmac\n"
    "MonsterSketch:\n"
)

_CONTROL_PROLOGUE = (
    ".mac make_monster_control attack2, attack3, attack4\n"
    "        .byte ATTACK::BATTLE\n"
    "        .ifnblank attack2\n"
    "                .byte ATTACK::attack2\n"
    "        .else\n"
    "                .byte ATTACK::NONE\n"
    "        .endif\n"
    "        .ifnblank attack3\n"
    "                .byte ATTACK::attack3\n"
    "        .else\n"
    "                .byte ATTACK::NONE\n"
    "        .endif\n"
    "        .ifnblank attack4\n"
    "                .byte ATTACK::attack4\n"
    "        .else\n"
    "                .byte ATTACK::NONE\n"
    "        .endif\n"
    ".endmac\n"
    "MonsterControl:\n"
)


class FakeSymbols(object):
    """Duck-typed stand-in for pma.Symbols over a tiny ATTACK space."""

    _ATTACKS = {"BATTLE": 0xEE, "NONE": 0xFF, "SPECIAL": 0xEF, "SCAN": 0xA2,
                "FIRE": 0x00}

    def attack_value(self, member, path, lineno):
        val = self._ATTACKS.get(member)
        if val is None:
            raise ParseError(path, lineno,
                             "unknown ATTACK::{}".format(member))
        return val


def _parse_text(text, spec):
    fd, path = tempfile.mkstemp(suffix=".asm")
    with os.fdopen(fd, "w") as fh:
        fh.write(text)
    try:
        return pma.parse_table(path, spec, FakeSymbols())
    finally:
        os.remove(path)


class ExpansionTests(unittest.TestCase):

    def test_rage_expands_battle_into_slot_zero(self):
        self.assertEqual(pma.RAGE_SPEC.expand(["SPECIAL"]),
                         ["BATTLE", "SPECIAL"])

    def test_sketch_expands_args_in_order(self):
        self.assertEqual(pma.SKETCH_SPEC.expand(["FIRE", "SCAN"]),
                         ["FIRE", "SCAN"])

    def test_control_pads_blank_args_with_none(self):
        self.assertEqual(pma.CONTROL_SPEC.expand([]),
                         ["BATTLE", "NONE", "NONE", "NONE"])
        self.assertEqual(pma.CONTROL_SPEC.expand(["SCAN"]),
                         ["BATTLE", "SCAN", "NONE", "NONE"])
        self.assertEqual(pma.CONTROL_SPEC.expand(["SCAN", "FIRE", "SPECIAL"]),
                         ["BATTLE", "SCAN", "FIRE", "SPECIAL"])


class SyntheticGrammarTests(unittest.TestCase):

    def _assert_parse_error(self, text, spec, fragment):
        with self.assertRaises(ParseError) as ctx:
            _parse_text(text, spec)
        self.assertIn(fragment, str(ctx.exception))

    def test_wellformed_row_reaches_the_count_check(self):
        # One invocation per grammar parses through the whole file (macro
        # body anchored, label anchored); the corpus-count verification is
        # then the (expected) failure — proving the row itself was accepted.
        self._assert_parse_error(
            _RAGE_PROLOGUE + "        make_monster_rage SPECIAL\n",
            pma.RAGE_SPEC, "produced 1 records; expected 256")
        self._assert_parse_error(
            _SKETCH_PROLOGUE + "        make_monster_sketch FIRE, SCAN\n",
            pma.SKETCH_SPEC, "produced 1 records; expected 384")
        self._assert_parse_error(
            _CONTROL_PROLOGUE + "        make_monster_control\n",
            pma.CONTROL_SPEC, "produced 1 records; expected 384")

    def test_rage_macro_body_without_battle_slot_raises(self):
        text = (".mac make_monster_rage attack2\n"
                "        .byte ATTACK::attack2, ATTACK::attack2\n"
                ".endmac\n"
                "MonsterRage:\n"
                "        make_monster_rage SPECIAL\n")
        self._assert_parse_error(text, pma.RAGE_SPEC,
                                 "does not match the documented slot shape")

    def test_control_macro_body_reshaped_raises(self):
        # Drop the NONE padding branch — the shape anchor must trip.
        text = (".mac make_monster_control attack2, attack3, attack4\n"
                "        .byte ATTACK::BATTLE\n"
                "        .byte ATTACK::attack2\n"
                ".endmac\n"
                "MonsterControl:\n"
                "        make_monster_control SCAN\n")
        self._assert_parse_error(text, pma.CONTROL_SPEC,
                                 "does not match the documented slot shape")

    def test_missing_label_raises(self):
        text = (".mac make_monster_rage attack2\n"
                "        .byte ATTACK::BATTLE, ATTACK::attack2\n"
                ".endmac\n"
                "        make_monster_rage SPECIAL\n")
        self._assert_parse_error(text, pma.RAGE_SPEC,
                                 "before the MonsterRage label")

    def test_duplicate_label_raises(self):
        text = _RAGE_PROLOGUE + "MonsterRage:\n"
        self._assert_parse_error(text, pma.RAGE_SPEC,
                                 "duplicate MonsterRage label")

    def test_label_never_seen_raises(self):
        text = (".mac make_monster_rage attack2\n"
                "        .byte ATTACK::BATTLE, ATTACK::attack2\n"
                ".endmac\n")
        self._assert_parse_error(text, pma.RAGE_SPEC,
                                 "label not found")

    def test_wrong_arity_raises(self):
        self._assert_parse_error(
            _RAGE_PROLOGUE + "        make_monster_rage FIRE, SCAN\n",
            pma.RAGE_SPEC, "expects (1,) args")
        self._assert_parse_error(
            _SKETCH_PROLOGUE + "        make_monster_sketch FIRE\n",
            pma.SKETCH_SPEC, "expects (2,) args")
        self._assert_parse_error(
            _CONTROL_PROLOGUE +
            "        make_monster_control FIRE, SCAN, SPECIAL, SCAN\n",
            pma.CONTROL_SPEC, "args")

    def test_blank_middle_arg_raises(self):
        self._assert_parse_error(
            _CONTROL_PROLOGUE + "        make_monster_control FIRE,, SCAN\n",
            pma.CONTROL_SPEC, "args")

    def test_unknown_attack_raises(self):
        self._assert_parse_error(
            _RAGE_PROLOGUE + "        make_monster_rage BOGUS\n",
            pma.RAGE_SPEC, "unknown ATTACK::BOGUS")

    def test_unrecognized_line_raises(self):
        self._assert_parse_error(_RAGE_PROLOGUE + "        lda #$00\n",
                                 pma.RAGE_SPEC, "unrecognized line")


class RendererFormattingTests(unittest.TestCase):

    @staticmethod
    def _rage_record():
        rec = pma.Record(0, ["BATTLE", "SPECIAL"], [0xEE, 0xEF])
        rec.name = "GUARD"
        return rec

    @staticmethod
    def _control_record():
        rec = pma.Record(0, ["BATTLE", "SCAN", "NONE", "NONE"],
                         [0xEE, 0xA2, 0xFF, 0xFF])
        rec.name = "SOLDIER"
        return rec

    def test_rage_inc_row_carries_slot_comments(self):
        inc = pma.render_inc([self._rage_record()], pma.RAGE_SPEC)
        self.assertIn("MonsterRageEntry{  // [$000]", inc)
        self.assertIn(".id = MonsterId::GUARD,", inc)
        self.assertIn("AttackId::BATTLE,   // slot 0 (1/2, always BATTLE)",
                      inc)
        self.assertIn("AttackId::SPECIAL,  // slot 1 (1/2)", inc)

    def test_control_inc_row_renders_none_sentinels(self):
        inc = pma.render_inc([self._control_record()], pma.CONTROL_SPEC)
        self.assertIn("MonsterControlEntry{  // [$000]", inc)
        self.assertIn("AttackId::BATTLE,  // slot 0 (always BATTLE)", inc)
        self.assertIn("AttackId::NONE,    // slot 3", inc)

    def test_fixture_rows_render_raw_bytes_with_decimal_id(self):
        fixture = pma.render_fixture([self._rage_record()], pma.RAGE_SPEC)
        self.assertIn("std::array<ExpectedMonsterRageEntry, 1>", fixture)
        self.assertIn(".id =   0,  // $000 GUARD", fixture)
        self.assertIn(".record = { .slot0 = 0xEE, .slot1 = 0xEF } },",
                      fixture)
        control = pma.render_fixture([self._control_record()],
                                     pma.CONTROL_SPEC)
        self.assertIn(".slot0 = 0xEE, .slot1 = 0xA2, .slot2 = 0xFF, "
                      ".slot3 = 0xFF", control)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "battle",
                                   "monster_rage.asm")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src monster attack tables not present "
                     "(rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        battle = os.path.join(root, "src", "battle")
        cls.symbols = pma.Symbols(os.path.join(root, "include", "const.inc"))
        cls.rage = pma.parse_table(os.path.join(battle, "monster_rage.asm"),
                                   pma.RAGE_SPEC, cls.symbols)
        cls.sketch = pma.parse_table(
            os.path.join(battle, "monster_sketch.asm"), pma.SKETCH_SPEC,
            cls.symbols)
        cls.control = pma.parse_table(
            os.path.join(battle, "monster_control.asm"), pma.CONTROL_SPEC,
            cls.symbols)

    def test_corpus_shape(self):
        self.assertEqual(len(self.rage), 256)
        self.assertEqual(len(self.sketch), 384)
        self.assertEqual(len(self.control), 384)

    def test_battle_byte_is_ee(self):
        self.assertEqual(
            self.symbols.attack_value("BATTLE", "const.inc", 0), 0xEE)
        self.assertEqual(
            self.symbols.attack_value("NONE", "const.inc", 0), 0xFF)

    def test_rage_slot_zero_is_battle_on_every_record(self):
        for rec in self.rage:
            self.assertEqual(rec.attacks[0], "BATTLE")
            self.assertEqual(rec.bytes[0], 0xEE)

    def test_control_slot_zero_is_battle_on_every_record(self):
        for rec in self.control:
            self.assertEqual(rec.attacks[0], "BATTLE")
            self.assertEqual(rec.bytes[0], 0xEE)

    def test_first_records_hand_traced(self):
        # Hand-traced from the three files' first invocations (GUARD /
        # SOLDIER / TEMPLAR / NINJA rows).
        self.assertEqual(self.rage[0].attacks, ["BATTLE", "SPECIAL"])
        self.assertEqual(self.sketch[0].attacks, ["BATTLE", "BATTLE"])
        self.assertEqual(self.control[0].attacks,
                         ["BATTLE", "NONE", "NONE", "NONE"])
        self.assertEqual(self.control[3].attacks,
                         ["BATTLE", "FIRE_SKEAN", "WATER_EDGE", "BOLT_EDGE"])

    def test_last_records_hand_traced(self):
        # Rage 255 is PUGS (SPECIAL); control 383 is COLOSSEUM (SPECIAL).
        self.assertEqual(self.rage[255].name, "PUGS")
        self.assertEqual(self.rage[255].attacks, ["BATTLE", "SPECIAL"])
        self.assertEqual(self.control[383].attacks,
                         ["BATTLE", "SPECIAL", "NONE", "NONE"])


if __name__ == "__main__":
    unittest.main()
