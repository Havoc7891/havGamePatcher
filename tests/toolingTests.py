#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Check shared recipe and PE helpers with synthetic data, without game files."""

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).absolute().parents[1] / "scripts/core"))
from peImage import NormalizePayload
from recipeData import apply_recipe, make_recipe


def MakePayload() -> bytes:
    """Create one PE32 section containing MSVC POGO timestamp metadata."""
    data = bytearray(0x400)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHI", data, 0x84, 0x14C, 1, 0x12345678)
    struct.pack_into("<H", data, 0x94, 224)
    struct.pack_into("<HBB", data, 0x98, 0x10B, 14, 44)
    struct.pack_into("<III", data, 0xB4, 0x900000, 0x1000, 0x200)
    struct.pack_into("<II", data, 0xD0, 0x2000, 0x200)
    struct.pack_into("<I", data, 0xF4, 16)
    struct.pack_into("<II", data, 0x128, 0x1000, 28)
    data[0x178:0x180] = b".rdata\0\0"
    struct.pack_into("<IIII", data, 0x180, 0x200, 0x1000, 0x200, 0x200)
    struct.pack_into("<I", data, 0x19C, 0x40000040)
    struct.pack_into("<I", data, 0x204, 0x12345678)
    struct.pack_into("<I", data, 0x20C, 13)
    data[0x240:] = bytes(range(224)) * 2
    return bytes(data)


class ToolingTests(unittest.TestCase):
    def testTimestampNormalization(self):
        source = MakePayload()
        expected = bytearray(source)
        struct.pack_into("<I", expected, 0x88, 0x87654321)
        struct.pack_into("<I", expected, 0x204, 0x87654321)

        self.assertEqual(NormalizePayload(source, 0x87654321), expected)
        self.assertEqual(NormalizePayload(expected, 0x87654321), expected)

    def testInvalidPayloadMetadata(self):
        source = MakePayload()
        cases = (
            ("linker version", 0x9B, b"\x2d"),
            ("mismatched timestamps", 0x88, b"\0\0\0\0"),
            ("debug type", 0x20C, struct.pack("<I", 12)),
            ("missing debug metadata", 0x128, bytes(8)),
            ("debug directory size", 0x12C, struct.pack("<I", 56)),
        )

        for label, offset, replacement in cases:
            with self.subTest(label=label):
                changed = bytearray(source)
                changed[offset : offset + len(replacement)] = replacement
                with self.assertRaises(ValueError):
                    NormalizePayload(changed, 1)

        with self.assertRaises(ValueError):
            NormalizePayload(source[:500], 1)

    def testRecipeReconstruction(self):
        source = b"abcdefgh"
        patched = b"aXYdeZghAPPENDED"
        spans, replacements, appended = make_recipe(source, patched)

        self.assertEqual(apply_recipe(source, spans, replacements, appended), patched)
        self.assertEqual(replacements, b"XYZ")
        self.assertEqual(appended, b"APPENDED")

    def testInvalidSpans(self):
        source = b"abcdefgh"
        replacements = b"XYZ"
        cases = (
            ("empty", [(0, 0, 0)]),
            ("overlapping", [(0, 0, 2), (1, 0, 1)]),
            ("out of source", [(len(source), 0, 1)]),
            ("out of replacement data", [(0, len(replacements), 1)]),
            ("negative replacement offset", [(0, -1, 1)]),
        )

        for label, spans in cases:
            with self.subTest(label=label):
                with self.assertRaises(ValueError):
                    apply_recipe(source, spans, replacements, b"APPENDED")


if __name__ == "__main__":
    unittest.main(verbosity=2)
