#!/usr/bin/env python3
"""Tests for scripts/diversity: python3 scripts/diversity/test_diversity.py"""

import random
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
import descriptors as DS  # noqa: E402
import screen as S  # noqa: E402
import wordbank as W  # noqa: E402


def req(cond, resp, timing="Always", ctype="trigger"):
    return {"condition": cond, "condition-type": ctype, "response": resp,
            "timing": {"type": timing}}


ORIG = {"assumptions": [],
        "guarantees": [req("true", "(a & b) -> c"), req("true", "a -> d"),
                       req("x", "!(c & d)", ctype="continual")]}


class Descriptors(unittest.TestCase):
    def test_printer_parentheses_are_no_change(self):
        self.assertEqual(DS.canonical("(a & b) -> c"),
                         DS.canonical("((a) & (b)) -> (c)"))
        self.assertNotEqual(DS.canonical("a & (b -> c)"),
                            DS.canonical("(a & b) -> c"))
        self.assertEqual(DS.canonical("a -> b -> c"),
                         DS.canonical("a -> (b -> c)"))

    def test_unchanged_repair_has_the_empty_cell(self):
        self.assertEqual(DS.cell(ORIG, ORIG), frozenset())

    def test_kinds(self):
        rep = {"assumptions": [req("true", "!x")],
               "guarantees": [req("true", "((a) & (b)) -> (c)", "Eventually"),
                              req("x", "!((c) & (d))", ctype="continual")]}
        self.assertEqual(DS.cell(ORIG, rep), frozenset({
            ("A+0", "assumption"), ("G0", "timing"), ("G1", "removed")}))
        rep2 = {"assumptions": [],
                "guarantees": [ORIG["guarantees"][0],
                               req("true", "a -> (d | c)", "Eventually"),
                               ORIG["guarantees"][2]]}
        self.assertEqual(DS.cell(ORIG, rep2),
                         frozenset({("G1", "timing+propositional")}))


class Statistics(unittest.TestCase):
    def test_hodges_lehmann(self):
        est, lo, hi = S.hodges_lehmann([1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0])
        self.assertEqual(est, 4.0)
        self.assertLessEqual(lo, est)
        self.assertGreaterEqual(hi, est)
        self.assertEqual(S.hodges_lehmann([1.0, 2.0])[1:], (None, None))

    def test_spearman(self):
        self.assertAlmostEqual(S.spearman([1, 2, 3, 4], [10, 20, 30, 40]), 1.0)
        self.assertAlmostEqual(S.spearman([1, 2, 3, 4], [4, 3, 2, 1]), -1.0)
        self.assertIsNone(S.spearman([1, 1, 1], [1, 2, 3]))


class WordBank(unittest.TestCase):
    def test_uniform_words_respect_the_bound(self):
        rng = random.Random(0)
        for _ in range(500):
            word = W.uniform_word(["p", "q"], rng, 8, 8).split()
            loop, n = int(word[0]), int(word[1])
            self.assertTrue(0 <= loop <= 8 and 1 <= n - loop <= 8)
            for item in word[2:]:
                self.assertLess(int(item.split("=")[1], 16), 1 << n)

    def test_signals_of(self):
        self.assertEqual(W.signals_of(["0 1 b=0 a=1", "1 2 c=3 a=0"]),
                         ["a", "b", "c"])


if __name__ == "__main__":
    unittest.main()
