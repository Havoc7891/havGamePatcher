# SPDX-License-Identifier: MIT

"""Construct the exact Fury3 retail fullscreen image in memory."""

import hashlib
import struct

from peImage import (
    PE,
    Section,
    Align,
    Branch,
    Checksum,
    EncodeRelocations,
    Put32,
    Require,
    U16,
    U32,
)
from payloadSchema import Number, ValidateProfile


def Sha(data):
    return hashlib.sha256(data).hexdigest()


def Build(source, payloadBytes, profile):
    ValidateProfile(profile)

    HASHES = {profile["originalSha256"], profile["joystickSha256"]}
    IMPORTS = {Number(i["rva"]): (i["dll"].lower(), i["name"]) for i in profile["expectedImports"]}
    HOOKS = tuple(
        (h["name"], Number(h["rva"]), bytes.fromhex(h["original"]), h["kind"])
        for h in profile["hooks"]
    )
    RETIRED = {Number(k): Number(v) for k, v in profile["retiredGameRelocations"].items()}
    Require(len(source) == 645120 and Sha(source) in HASHES, "Unsupported exact Fury3 input")

    game, payload = PE(source), PE(payloadBytes)
    Require(
        game.base == Number(profile["preferredBase"])
        and game.image_size == Number(profile["imageSize"])
        and game.count == Number(profile["sectionCount"]),
        "Unexpected game layout",
    )
    Require(
        U32(source, game.pe + 8) == Number(profile["timestamp"])
        and U32(source, game.checksum_offset) == Number(profile["checksum"]),
        "Source header identity differs from profile",
    )
    Require(game.Directory(4) == (0, 0), "Signed inputs are not supported")

    Require(
        payload.base == 0x900000 and U16(payloadBytes, payload.pe + 22) & 0x2000,
        "Expected fixed-layout x86 payload DLL",
    )
    Require(U32(payloadBytes, payload.optional + 16) == 0, "Payload must have no entry point")

    for index in (1, 4, 9, 10, 11, 12, 13, 14):
        Require(payload.Directory(index) == (0, 0), "Unexpected payload runtime dependency")

    for rva, identity in IMPORTS.items():
        Require(game.Imports().get(rva) == identity, "Game import identity mismatch")

    exports = payload.Exports()
    Require(
        set(exports) == {"config", "state", *(h[0] for h in HOOKS)}, "Unexpected payload exports"
    )

    copied = sorted((s for s in payload.sections if s.name != ".reloc"), key=lambda s: s.rva)
    Require(
        len(copied) == 2 and {s.name for s in copied} == {".text", ".data"},
        "Unexpected payload section layout",
    )

    for section in copied:
        access = 0x60000000 if section.name == ".text" else 0xC0000000
        Require(section.flags & 0xE0000000 == access, "Unexpected payload section permissions")

    Require(payload.SectionAt(exports["config"], 24).name == ".data", "Config must be writable")

    Require(payload.SectionAt(exports["state"], 132).name == ".data", "State must be writable")
    Require(
        not any(payload.Mapped()[exports["state"] : exports["state"] + 132]),
        "Payload state must initially be zero",
    )
    Require(
        exports["config"] + 24 <= exports["state"] or exports["state"] + 132 <= exports["config"],
        "Config and state overlap",
    )
    Require(
        struct.unpack_from("<6I", payloadBytes, payload.Offset(exports["config"], 24))
        == (0x46534650, 1, 0, 0, 0, 0),
        "Unexpected payload config ABI",
    )

    for name, _, _, _ in HOOKS:
        Require(payload.SectionAt(exports[name]).name == ".text", "Hook must be executable")

    originalRelocs = game.Relocations()
    payloadRelocs = payload.Relocations()

    for rva, expected in RETIRED.items():
        Require(
            rva in originalRelocs and U32(source, game.Offset(rva, 4)) == expected,
            "Unexpected original hook relocation",
        )

    retained = originalRelocs - RETIRED.keys()

    for name, rva, before, kind in HOOKS:
        Require(
            source[game.Offset(rva) : game.Offset(rva) + len(before)] == before,
            "Unexpected hook bytes: " + name,
        )
        Require(
            not any(r < rva + len(before) and r + 4 > rva for r in retained),
            "Unretired relocation overlaps hook",
        )

    for rva in payloadRelocs:
        Require(payload.SectionAt(rva, 4) in copied, "Relocation outside payload sections")
        target = U32(payloadBytes, payload.Offset(rva, 4)) - payload.base
        Require(payload.SectionAt(target) in copied, "Payload absolute pointer leaves copied image")

    origin = Align(game.image_size, 0x1000) - copied[0].rva
    delta = game.base + origin - payload.base
    image = bytearray(source)
    sections = []

    for section in copied:
        raw = Align(len(image), 0x200)
        image.extend(bytes(raw - len(image)))
        image.extend(payloadBytes[section.raw_offset : section.raw_offset + section.raw_size])
        sections.append(
            Section(
                ".fgpft" if section.name == ".text" else ".fgpfd",
                section.virtual_size,
                origin + section.rva,
                section.raw_size,
                raw if section.raw_size else 0,
                section.flags,
            )
        )

    def EmbeddedOffset(rva, size=4):
        section = payload.SectionAt(rva, size)
        Require(
            section in copied and rva + size <= section.rva + section.raw_size,
            "Embedded edit is outside file-backed data",
        )
        return sections[copied.index(section)].raw_offset + rva - section.rva

    for rva in payloadRelocs:
        at = EmbeddedOffset(rva)
        Put32(image, at, (U32(image, at) + delta) & 0xFFFFFFFF)

    configAt = EmbeddedOffset(exports["config"], 24)
    struct.pack_into("<6I", image, configAt, 0x46534650, 1, game.base, 0x1453B4, 0x145398, 0x145324)
    addedRelocs = {origin + exports["config"] + 8}

    reports = []

    for name, rva, before, kind in HOOKS:
        target = origin + exports[name]

        if kind in ("call", "jmp"):
            after = Branch(rva, target, len(before), 0xE8 if kind == "call" else 0xE9)
        elif kind == "pointer":
            after = b"\xbf" + struct.pack("<I", game.base + target) + b"\x90"
            addedRelocs.add(rva + 1)
        else:
            raise ValueError("Unsupported hook kind")

        at = game.Offset(rva, len(before))
        image[at : at + len(before)] = after
        reports.append(
            dict(
                name=name,
                kind=kind,
                rva=rva,
                targetRva=target,
                original=before.hex(),
                replacement=after.hex(),
            )
        )

    # Preserve the existing joystick fix in both accepted input variants.
    joystickAt = Number(profile["joystick"]["fileOffset"])
    joystickBytes = bytes.fromhex(profile["joystick"]["patched"])
    image[joystickAt : joystickAt + len(joystickBytes)] = joystickBytes

    merged = retained | {origin + r for r in payloadRelocs} | addedRelocs
    relocData = EncodeRelocations(merged)
    relocRva = Align(max(s.rva + s.mapped_size for s in sections), 0x1000)
    relocRaw = Align(len(image), 0x200)
    relocSize = Align(len(relocData), 0x200)
    image.extend(bytes(relocRaw - len(image)))
    image.extend(relocData)
    image.extend(bytes(relocSize - len(relocData)))
    sections.append(Section(".fgpfr", len(relocData), relocRva, relocSize, relocRaw, 0x42000040))

    firstHeader = game.section_table + game.count * 40
    endHeader = firstHeader + len(sections) * 40
    Require(
        endHeader <= game.header_size and not any(source[firstHeader:endHeader]),
        "Insufficient empty PE section-header space",
    )

    for index, section in enumerate(sections):
        struct.pack_into(
            "<8sIIIIIIHHI",
            image,
            firstHeader + 40 * index,
            section.name.encode(),
            section.virtual_size,
            section.rva,
            section.raw_size,
            section.raw_offset,
            0,
            0,
            0,
            0,
            section.flags,
        )

    struct.pack_into("<H", image, game.pe + 6, game.count + len(sections))

    for offset, flag in ((4, 0x20), (8, 0x40)):
        Put32(
            image,
            game.optional + offset,
            U32(source, game.optional + offset)
            + sum(s.raw_size for s in sections if s.flags & flag),
        )

    Put32(
        image,
        game.optional + 12,
        U32(source, game.optional + 12) + sum(s.virtual_size for s in sections if s.flags & 0x80),
    )
    Put32(image, game.optional + 56, Align(relocRva + len(relocData), 0x1000))
    struct.pack_into("<II", image, game.optional + 96 + 5 * 8, relocRva, len(relocData))
    Put32(image, game.checksum_offset, Checksum(image, game.checksum_offset))

    output = PE(image)
    Require(output.sections == game.sections + sections, "Original section metadata changed")
    Require(output.Imports() == game.Imports(), "Original imports changed")
    Require(output.Relocations() == merged, "Relocation merge failed")
    Require(not RETIRED.keys() & merged, "Retired relocations remain")
    Require(
        U32(image, game.checksum_offset) == Checksum(image, game.checksum_offset),
        "Checksum mismatch",
    )

    for index in range(16):
        if index != 5:
            Require(
                output.Directory(index) == game.Directory(index), "Unexpected PE directory change"
            )

    # No unlisted bytes in the original file may change.
    allowed = set(range(firstHeader, endHeader))
    allowed.update(range(game.pe + 6, game.pe + 8))

    for offset, size in ((4, 12), (56, 4), (64, 4), (136, 8)):
        allowed.update(range(game.optional + offset, game.optional + offset + size))

    allowed.update((0xA9B9, 0xA9BA))

    for _, rva, before, _ in HOOKS:
        at = game.Offset(rva)
        allowed.update(range(at, at + len(before)))

    Require(
        all(a == b or i in allowed for i, (a, b) in enumerate(zip(source, image))),
        "An unlisted original byte changed",
    )

    # Verify payload code/data transplant, with only fixups and config rewritten.
    for old, new in zip(copied, sections):
        expected = bytearray(payloadBytes[old.raw_offset : old.raw_offset + old.raw_size])

        for rva in payloadRelocs:
            if old.rva <= rva < old.rva + old.raw_size:
                at = rva - old.rva
                Put32(expected, at, (U32(expected, at) + delta) & 0xFFFFFFFF)

        if old.rva <= exports["config"] < old.rva + old.raw_size:
            struct.pack_into(
                "<6I",
                expected,
                exports["config"] - old.rva,
                0x46534650,
                1,
                game.base,
                0x1453B4,
                0x145398,
                0x145324,
            )

        Require(
            image[new.raw_offset : new.raw_offset + new.raw_size] == expected,
            "Embedded payload bytes differ unexpectedly",
        )

    # Apply a synthetic nonpreferred base and confirm absolute hook/config pointers move together.
    rebased = output.Mapped()

    for rva in merged:
        Put32(rebased, rva, (U32(rebased, rva) + 0x100000) & 0xFFFFFFFF)

    Require(
        U32(rebased, origin + exports["config"] + 8) == game.base + 0x100000,
        "Config game-base relocation failed",
    )
    Require(
        U32(rebased, 0x4E5CB) == game.base + origin + exports["UpdateMenuCheck"] + 0x100000,
        "Menu pointer relocation failed",
    )

    for hook in reports:
        if hook["kind"] in ("call", "jmp"):
            displacement = struct.unpack_from("<i", rebased, hook["rva"] + 1)[0]
            Require(
                hook["rva"] + 5 + displacement == hook["targetRva"],
                "Relative hook target changed during synthetic rebase",
            )

    return bytes(image), dict(
        format="Fury3FullscreenPayload",
        version=1,
        sourceSha256=Sha(source),
        payloadSha256=Sha(payloadBytes),
        outputSha256=Sha(image),
        outputSize=len(image),
        hooks=reports,
        configRva=origin + exports["config"],
        exports={k: origin + v for k, v in exports.items()},
        sections=[s.Describe() for s in sections],
        relocations=dict(
            original=len(originalRelocs),
            retired=sorted(RETIRED),
            payload=len(payloadRelocs),
            total=len(merged),
        ),
        checks=[
            "Exact supported input",
            "Original imports preserved",
            "Only listed original bytes changed",
            "Payload transplant verified",
            "Merged relocations verified",
            "Synthetic rebase checked",
            "PE checksum verified",
        ],
        gameplayValidated=False,
    )
