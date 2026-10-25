#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Read and transform the strict x86 PE32 layout used by patch payloads.

These helpers have no game configuration or filesystem side effects. They accept
4096-byte section alignment, 512-byte file alignment and HIGHLOW relocations.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass


def Require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def Align(value: int, alignment: int) -> int:
    Require(value >= 0, "Negative alignment input")
    Require(alignment > 0 and alignment & (alignment - 1) == 0, "Invalid alignment")
    return (value + alignment - 1) & -alignment


def U16(data: bytes | bytearray, offset: int) -> int:
    Require(0 <= offset <= len(data) - 2, "Truncated WORD")
    return struct.unpack_from("<H", data, offset)[0]


def U32(data: bytes | bytearray, offset: int) -> int:
    Require(0 <= offset <= len(data) - 4, "Truncated DWORD")
    return struct.unpack_from("<I", data, offset)[0]


def Put32(data: bytearray, offset: int, value: int) -> None:
    Require(0 <= offset <= len(data) - 4, "Truncated DWORD destination")
    Require(0 <= value <= 0xFFFFFFFF, "DWORD overflow")
    struct.pack_into("<I", data, offset, value)


@dataclass(frozen=True)
class Section:
    name: str
    virtual_size: int
    rva: int
    raw_size: int
    raw_offset: int
    flags: int

    @property
    def mapped_size(self) -> int:
        return max(self.virtual_size, self.raw_size)

    def Describe(self) -> dict:
        return {
            "name": self.name,
            "rva": self.rva,
            "virtualSize": self.virtual_size,
            "rawOffset": self.raw_offset,
            "rawSize": self.raw_size,
            "characteristics": self.flags,
        }


class PE:
    def __init__(self, data: bytes | bytearray):
        self.data = bytes(data)
        Require(self.data[:2] == b"MZ", "Missing DOS signature")

        self.pe = U32(self.data, 0x3C)
        Require(self.data[self.pe : self.pe + 4] == b"PE\0\0", "Missing PE signature")
        Require(U16(self.data, self.pe + 4) == 0x14C, "Only x86 is supported")

        self.count = U16(self.data, self.pe + 6)
        self.optional = self.pe + 24
        self.optional_size = U16(self.data, self.pe + 20)
        Require(self.optional_size == 224, "Expected standard PE32 optional header")
        Require(U16(self.data, self.optional) == 0x10B, "Only PE32 is supported")
        Require(U32(self.data, self.optional + 92) == 16, "Expected 16 PE directories")

        self.base = U32(self.data, self.optional + 28)
        self.section_alignment = U32(self.data, self.optional + 32)
        self.file_alignment = U32(self.data, self.optional + 36)
        Require(self.section_alignment == 0x1000, "Expected 4096-byte section alignment")
        Require(self.file_alignment == 0x200, "Expected 512-byte file alignment")

        self.image_size = U32(self.data, self.optional + 56)
        self.header_size = U32(self.data, self.optional + 60)
        self.checksum_offset = self.optional + 64
        self.section_table = self.optional + self.optional_size
        Require(self.count > 0, "Image has no sections")
        Require(
            self.section_table + self.count * 40 <= self.header_size <= len(self.data),
            "Invalid section table or header size",
        )
        Require(self.header_size % self.file_alignment == 0, "Misaligned headers")
        Require(self.image_size % self.section_alignment == 0, "Misaligned image size")
        Require(self.base + self.image_size <= 0x100000000, "Image address overflow")

        self.sections = []

        for index in range(self.count):
            at = self.section_table + index * 40
            name = self.data[at : at + 8].split(b"\0", 1)[0].decode("ascii")
            virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", self.data, at + 8)
            section = Section(
                name, virtual_size, rva, raw_size, raw_offset, U32(self.data, at + 36)
            )
            Require(
                rva >= Align(self.header_size, self.section_alignment), "Section overlaps headers"
            )
            Require(rva % self.section_alignment == 0, "Misaligned section RVA")
            Require(rva + section.mapped_size <= self.image_size, "Section exceeds image")

            if raw_size:
                Require(
                    raw_offset >= self.header_size and raw_offset % self.file_alignment == 0,
                    "Invalid section raw offset",
                )
                Require(
                    raw_size % self.file_alignment == 0 and raw_offset + raw_size <= len(self.data),
                    "Invalid section raw size",
                )

            self.sections.append(section)

        for index, left in enumerate(self.sections):
            for right in self.sections[index + 1 :]:
                Require(
                    left.rva + left.mapped_size <= right.rva
                    or right.rva + right.mapped_size <= left.rva,
                    "Overlapping virtual sections",
                )

                if left.raw_size and right.raw_size:
                    Require(
                        left.raw_offset + left.raw_size <= right.raw_offset
                        or right.raw_offset + right.raw_size <= left.raw_offset,
                        "Overlapping raw sections",
                    )

    def Directory(self, index: int) -> tuple[int, int]:
        Require(0 <= index < 16, "Invalid PE directory index")
        return struct.unpack_from("<II", self.data, self.optional + 96 + index * 8)

    def SectionAt(self, rva: int, length: int = 1) -> Section:
        Require(rva >= 0 and length > 0, "Invalid RVA range")

        for section in self.sections:
            if section.rva <= rva and rva + length <= section.rva + section.mapped_size:
                return section

        raise ValueError(f"Unmapped RVA 0x{rva:x}, size {length}")

    def Offset(self, rva: int, length: int = 1) -> int:
        Require(rva >= 0 and length > 0, "Invalid RVA range")

        if 0 <= rva and rva + length <= self.header_size:
            return rva

        section = self.SectionAt(rva, length)
        Require(rva + length <= section.rva + section.raw_size, "RVA has no file-backed bytes")

        return section.raw_offset + rva - section.rva

    def CString(self, rva: int) -> str:
        section = self.SectionAt(rva)
        start = self.Offset(rva)
        end = self.data.find(b"\0", start, section.raw_offset + section.raw_size)
        Require(end != -1, "Unterminated PE string")

        return self.data[start:end].decode("ascii")

    def Exports(self) -> dict[str, int]:
        rva, size = self.Directory(0)
        Require(rva and size >= 40, "Image has no exports")

        at = self.Offset(rva, 40)
        functions, names, function_table, name_table, ordinal_table = struct.unpack_from(
            "<IIIII", self.data, at + 20
        )
        Require(0 < names <= functions <= 4096, "Invalid export count")

        ft = self.Offset(function_table, functions * 4)
        nt = self.Offset(name_table, names * 4)
        ot = self.Offset(ordinal_table, names * 2)
        result = {}

        for index in range(names):
            name = self.CString(U32(self.data, nt + index * 4))
            ordinal = U16(self.data, ot + index * 2)
            Require(ordinal < functions, "Invalid export ordinal")

            target = U32(self.data, ft + ordinal * 4)
            Require(not rva <= target < rva + size, "Forwarded exports are unsupported")
            Require(name not in result, "Duplicate export name")
            self.SectionAt(target)
            result[name] = target

        return result

    def Relocations(self) -> set[int]:
        rva, size = self.Directory(5)
        Require(rva != 0 and size != 0, "Missing relocation directory")

        start = self.Offset(rva, size)
        cursor, end = start, start + size
        result = set()

        while cursor < end:
            Require(cursor + 8 <= end, "Truncated relocation block")

            page, length = struct.unpack_from("<II", self.data, cursor)
            Require(
                page % 0x1000 == 0 and length >= 8 and length % 4 == 0 and cursor + length <= end,
                "Invalid relocation block",
            )

            for at in range(cursor + 8, cursor + length, 2):
                entry = U16(self.data, at)
                kind, within = entry >> 12, entry & 0xFFF
                Require(kind in (0, 3), f"Unsupported relocation type {kind}")

                if kind == 3:
                    target = page + within
                    self.SectionAt(target, 4)
                    Require(target not in result, "Duplicate HIGHLOW relocation")
                    result.add(target)

            cursor += length

        return result

    def Imports(self) -> dict[int, tuple[str, str]]:
        rva, size = self.Directory(1)
        Require(rva and size >= 20, "Missing import directory")

        start = self.Offset(rva, size)
        result = {}

        for at in range(start, start + size - 19, 20):
            original_thunk, timestamp, forwarder, name, first_thunk = struct.unpack_from(
                "<5I", self.data, at
            )
            if (original_thunk, timestamp, forwarder, name, first_thunk) == (0, 0, 0, 0, 0):
                return result

            dll = self.CString(name).lower()

            for index in range(4096):
                value = U32(self.data, self.Offset((original_thunk or first_thunk) + index * 4, 4))
                if not value:
                    break

                imported = f"#{value & 0xFFFF}" if value & 0x80000000 else self.CString(value + 2)
                slot = first_thunk + index * 4
                self.Offset(slot, 4)
                Require(slot not in result, "Duplicate import address slot")

                result[slot] = (dll, imported)
            else:
                raise ValueError("Unterminated import thunk table")

        raise ValueError("Unterminated import descriptor table")

    def Mapped(self) -> bytearray:
        mapped = bytearray(self.image_size)
        mapped[: self.header_size] = self.data[: self.header_size]

        for section in self.sections:
            if section.raw_size:
                mapped[section.rva : section.rva + section.raw_size] = self.data[
                    section.raw_offset : section.raw_offset + section.raw_size
                ]

        return mapped


def EncodeRelocations(relocations: set[int]) -> bytes:
    pages: dict[int, list[int]] = {}

    for rva in sorted(relocations):
        Require(0 <= rva <= 0xFFFFFFFC, "Invalid HIGHLOW relocation RVA")
        pages.setdefault(rva & ~0xFFF, []).append(0x3000 | (rva & 0xFFF))

    result = bytearray()

    for page, entries in pages.items():
        if len(entries) % 2:
            entries.append(0)

        result.extend(struct.pack("<II", page, 8 + len(entries) * 2))
        result.extend(struct.pack("<" + "H" * len(entries), *entries))

    return bytes(result)


def Checksum(data: bytes | bytearray, skip: int) -> int:
    result = 0

    for at in range(0, len(data), 2):
        if skip <= at < skip + 4:
            continue

        result += data[at] | ((data[at + 1] if at + 1 < len(data) else 0) << 8)
        result = (result & 0xFFFF) + (result >> 16)

    result = (result & 0xFFFF) + (result >> 16)

    return result + len(data)


def Branch(rva: int, destination: int, length: int, opcode: int) -> bytes:
    distance = destination - (rva + 5)
    Require(length >= 5 and -0x80000000 <= distance <= 0x7FFFFFFF, "Invalid rel32 branch")
    return bytes([opcode]) + struct.pack("<i", distance) + b"\x90" * (length - 5)


def NormalizePayload(data: bytes | bytearray, timestamp: int) -> bytes:
    """Normalize only COFF and POGO timestamps for pinned MSVC 14.44 payloads."""

    image = PE(data)
    Require(
        data[image.optional + 2 : image.optional + 4] == bytes((14, 44)),
        "Rebuild with the pinned MSVC 14.44 x86 toolchain",
    )

    debug_rva, debug_size = image.Directory(6)
    Require(debug_rva and debug_size == 28, "Expected one linker debug entry")

    debug_at = image.Offset(debug_rva, 28)
    Require(U32(data, debug_at + 12) == 13, "Expected POGO metadata")
    Require(
        data[debug_at + 4 : debug_at + 8] == data[image.pe + 8 : image.pe + 12],
        "Payload linker timestamps disagree",
    )

    result = bytearray(data)
    Put32(result, image.pe + 8, timestamp)
    Put32(result, debug_at + 4, timestamp)

    return bytes(result)
