#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Append the shared crosshair, radar and fog policy using a retail profile.

All code is generated here without diagnostic files or runtime Python support.
A configured output identity is checked against the edition's frozen recipe.
"""

import hashlib
import struct

from payloadSchema import PAYLOAD_VERSION, digest, number, validate_profile
from peImage import Align, Checksum, PE, Require, Section, U32

SECTION_NAME = b".hgcsh"
_CONFIGURED = False


def configure(profile, *, parent_sha256, parent_size, config_rva, section_rva, output_sha256):
    """Bind native addresses and exact parent identity before generating any stubs."""

    global _CONFIGURED, PARENT_SHA256, PARENT_SIZE, OUTPUT_SHA256, OUTPUT_SIZE
    global CONFIG_RVA, SECTION_RVA, CLEAR_STUB_RVA, WEAPON_STUB_RVA
    global NEEDLE_STUB_RVA, BACKGROUND_STUB_RVA, NEEDLE_RETURN_CHAIN, BACKGROUND_RETURN_CHAIN
    global HOOK_RVA, EMIT_RVA, SKIP_RVA, FOG_HOOK_RVA, FOG_RESUME_RVA
    global ORIGINAL, FOG_ORIGINAL, FOG_RESUME_ORIGINAL, CLEAR_RETURN_RVAS, WEAPON_RETURN_CHAIN
    global PARENT_SECTION_COUNT, REPORT_FORMAT

    _CONFIGURED = False
    edition = validate_profile(profile)

    p = profile["shading"]
    parent_hash = digest(parent_sha256)
    output_hash = None if output_sha256 is None else digest(output_sha256)
    size, config, section = number(parent_size), number(config_rva), number(section_rva)
    Require(size > 0 and size % 512 == 0 and size <= 0xFFFFFFFF - 1024, "Invalid parent size")
    Require(
        section % 0x1000 == 0 and section >= number(profile["imageSize"]),
        "Invalid shading section RVA",
    )
    Require(
        number(profile["imageSize"]) <= config and config + 104 <= section,
        "Config must lie in embedded payload",
    )

    # These stack positions and flags are the supported native polygon ABI.
    Require(
        {name: number(value) for name, value in p["polygonStack"].items()}
        == {"return": 0x30, "source": 0x34, "count": 0x38, "flags": 0x3C},
        "Unexpected polygon stack layout",
    )
    Require(
        number(p["sourceVertexSize"]) == 0x24
        and tuple(number(value) for value in p["sourceFogOffsets"]) == (0x20, 0x44, 0x68, 0x8C),
        "Unexpected source vertex layout",
    )
    Require(
        (
            number(p["clearCount"]),
            number(p["clearFlags"]),
            number(p["weaponCount"]),
            number(p["weaponFlags"]),
        )
        == (4, 0x10, 4, 0x23),
        "Unexpected native clear or weapon polygon ABI",
    )

    addresses = {
        key: number(p[key])
        for key in ("hookRva", "emitRva", "skipRva", "fogHookRva", "fogResumeRva")
    }
    Require(
        all(0 < value < number(profile["imageSize"]) for value in addresses.values()),
        "Shading address lies outside retail image",
    )

    original, fog = bytes.fromhex(p["original"]), bytes.fromhex(p["fogOriginal"])
    resume = bytes.fromhex(p["fogCacheEntryOriginal"])
    Require(
        len(original) == 6 and len(fog) == 8 and len(resume) == 10,
        "Unexpected shading preimage lengths",
    )

    clear_returns = tuple(number(value) for value in p["clearReturnRvas"])
    chain = tuple(
        (number(item["stackOffset"]), number(item["returnRva"])) for item in p["weaponReturnChain"]
    )
    Require(
        len(clear_returns) == 2 and clear_returns[1] > clear_returns[0],
        "Expected two ordered clear callers",
    )
    Require(
        tuple(offset for offset, _ in chain) == (0x30, 0x40, 0x88, 0x94, 0xB0),
        "Unexpected weapon caller stack layout",
    )
    Require(
        all(
            0 < value < number(profile["imageSize"])
            for value in (*clear_returns, *(rva for _, rva in chain))
        ),
        "Shading caller lies outside retail image",
    )

    needle_chain = tuple(
        (number(item["stackOffset"]), number(item["returnRva"])) for item in p["needleReturnChain"]
    )
    background_chain = tuple(
        (number(item["stackOffset"]), number(item["returnRva"]))
        for item in p["radarBackgroundReturnChain"]
    )
    Require(
        (number(p["needleCount"]), number(p["needleFlags"])) == (4, 0x10),
        "Unexpected radar needle polygon ABI",
    )
    Require(
        (
            number(p["radarBackgroundMinCount"]),
            number(p["radarBackgroundMaxCount"]),
            number(p["radarBackgroundFlags"]),
        )
        == (3, 8, 0x10),
        "Unexpected clipped radar background polygon ABI",
    )
    Require(
        tuple(offset for offset, _ in needle_chain) == (0x30, 0x40, 0x88, 0x94, 0xB8, 0xC0),
        "Unexpected radar needle caller stack layout",
    )
    Require(
        tuple(offset for offset, _ in background_chain) == (0x30, 0x40, 0x88, 0x94, 0xB8),
        "Unexpected radar background caller stack layout",
    )
    Require(
        all(
            0 < rva < number(profile["imageSize"]) for _, rva in (*needle_chain, *background_chain)
        ),
        "Radar caller lies outside retail image",
    )

    PARENT_SHA256, PARENT_SIZE, OUTPUT_SHA256, OUTPUT_SIZE = (
        parent_hash,
        size,
        output_hash,
        size + 1024,
    )
    CONFIG_RVA, SECTION_RVA = config, section
    CLEAR_STUB_RVA, WEAPON_STUB_RVA = section + 0x40, section + 0x80
    NEEDLE_STUB_RVA, BACKGROUND_STUB_RVA = section + 0x140, section + 0x210
    HOOK_RVA, EMIT_RVA, SKIP_RVA = addresses["hookRva"], addresses["emitRva"], addresses["skipRva"]
    FOG_HOOK_RVA, FOG_RESUME_RVA = addresses["fogHookRva"], addresses["fogResumeRva"]
    ORIGINAL, FOG_ORIGINAL, FOG_RESUME_ORIGINAL = original, fog, resume
    CLEAR_RETURN_RVAS, WEAPON_RETURN_CHAIN = clear_returns, chain
    NEEDLE_RETURN_CHAIN, BACKGROUND_RETURN_CHAIN = needle_chain, background_chain
    PARENT_SECTION_COUNT = number(profile["sectionCount"]) + 3
    REPORT_FORMAT = "Hellbender" + edition + "MaintainedShading"
    _CONFIGURED = True


def require_configured():
    Require(_CONFIGURED, "Configure an explicit retail profile before using the shading helper")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def ShadingImage(data):
    pe = PE(data)
    Require(pe.base == 0x400000, "Unexpected preferred image base")
    Require(pe.count < 96, "Invalid section count")

    return pe


def SectionOffset(pe, rva, size=1):
    # Shading hooks and configuration must belong to a file-backed section.
    pe.SectionAt(rva, size)
    return pe.Offset(rva, size)


def validate_layout(source):
    require_configured()
    Require(
        len(source) == PARENT_SIZE and sha(source) == PARENT_SHA256,
        "Input is not the exact frozen unshaded payload image",
    )

    pe = ShadingImage(source)
    Require(
        pe.count == PARENT_SECTION_COUNT and pe.image_size == SECTION_RVA,
        "Unexpected payload section layout",
    )
    Require(
        U32(source, pe.checksum_offset) == Checksum(source, pe.checksum_offset),
        "Parent checksum mismatch",
    )
    Require(
        source[SectionOffset(pe, HOOK_RVA, 6) : SectionOffset(pe, HOOK_RVA, 6) + 6] == ORIGINAL,
        "Original shading sequence mismatch",
    )
    Require(
        source[SectionOffset(pe, CONFIG_RVA, 12) : SectionOffset(pe, CONFIG_RVA, 12) + 12]
        == struct.pack("<III", 0x48474C50, PAYLOAD_VERSION, 0x400000),
        "Expected unchanged payload config",
    )
    Require(
        source[SectionOffset(pe, FOG_HOOK_RVA, 8) : SectionOffset(pe, FOG_HOOK_RVA, 8) + 8]
        == FOG_ORIGINAL,
        "Original fog decision mismatch",
    )
    Require(
        source[
            SectionOffset(pe, FOG_RESUME_RVA, len(FOG_RESUME_ORIGINAL)) : SectionOffset(
                pe, FOG_RESUME_RVA, len(FOG_RESUME_ORIGINAL)
            )
            + len(FOG_RESUME_ORIGINAL)
        ]
        == FOG_RESUME_ORIGINAL,
        "Native fog cache entry mismatch",
    )
    Require(
        source[pe.optional + 96 + 4 * 8 : pe.optional + 96 + 5 * 8] == b"\0" * 8,
        "Signed images are not supported",
    )

    header = pe.section_table + pe.count * 40
    Require(header + 40 <= pe.header_size, "No section-header room")
    Require(source[header : header + 40] == b"\0" * 40, "New section-header room is occupied")
    Require(
        max(s.raw_offset + s.raw_size for s in pe.sections) == len(source),
        "Unexpected parent overlay or trailing data",
    )
    Require(
        not any(s.name == SECTION_NAME.decode() for s in pe.sections),
        "Shading section already present",
    )

    return pe


def stub_bytes(stub_rva):
    require_configured()

    # Only EBP is intentionally changed. ESP and all other registers stay intact.
    data = bytearray.fromhex("f7dd f744243c05000000")
    branches = []

    def skip(op):
        data.extend([op, 0])
        branches.append(len(data) - 1)

    skip(0x75)
    data.extend(bytes.fromhex("837c243803"))
    skip(0x75)

    for offset in (0x20, 0x44, 0x68):
        data.extend(bytes([0x83, 0x79, offset, 0]))
        skip(0x7F)

    data.extend(bytes.fromhex("31ed"))
    compare = len(data)
    data.extend(bytes.fromhex("3be8"))

    for at in branches:
        distance = compare - (at + 1)
        Require(0 <= distance <= 127, "Internal short branch overflow")
        data[at] = distance

    for opcode, target in ((b"\x0f\x84", SKIP_RVA), (b"\xe9", EMIT_RVA)):
        distance = target - (stub_rva + len(data) + len(opcode) + 4)
        Require(-(2**31) <= distance < 2**31, "Relative continuation overflow")
        data.extend(opcode + struct.pack("<i", distance))

    return bytes(data)


def clear_stub_bytes():
    require_configured()

    # Default to fog1, except the two exact native four-vertex clear calls.
    code = bytearray.fromhex("b801000000")
    branches, labels = [], {}

    def branch(opcode, label):
        code.extend((opcode, 0))
        branches.append((len(code) - 1, label))

    code.extend(bytes.fromhex("837c243804"))
    branch(0x75, "resume")
    code.extend(bytes.fromhex("837c243c10"))
    branch(0x75, "resume")

    code.extend(bytes.fromhex("51e800000000"))
    pc_rva = CLEAR_STUB_RVA + len(code)
    code.extend(bytes.fromhex("59"))
    code.extend(bytes.fromhex("81e9") + struct.pack("<I", pc_rva - CLEAR_RETURN_RVAS[0]))

    # Saved ECX shifts the original return from ESP+30 to ESP+34.
    code.extend(bytes.fromhex("394c2434"))
    branch(0x74, "clear")
    code.extend(
        bytes.fromhex("81c1") + struct.pack("<I", CLEAR_RETURN_RVAS[1] - CLEAR_RETURN_RVAS[0])
    )
    code.extend(bytes.fromhex("394c2434"))
    branch(0x75, "restore")

    labels["clear"] = len(code)
    code.extend(bytes.fromhex("31c0"))

    labels["restore"] = len(code)
    code.extend(bytes.fromhex("59"))

    labels["resume"] = len(code)
    code.extend(b"\xe9" + struct.pack("<i", FOG_RESUME_RVA - CLEAR_STUB_RVA - len(code) - 5))

    for at, label in branches:
        distance = labels[label] - at - 1
        Require(0 <= distance <= 127, "Clear stub branch overflow")
        code[at] = distance

    return bytes(code)


def weapon_stub_bytes():
    require_configured()

    # Check the native caller chain from shallowest to deepest before reading
    # the four initialized fog fields. Every mismatch uses the clear policy.
    code, branches, labels = bytearray(), [], {}

    def mismatch(label):
        code.extend(bytes.fromhex("0f85") + bytes(4))
        branches.append((len(code) - 4, label))

    def jump(rva):
        code.extend(b"\xe9" + struct.pack("<i", rva - WEAPON_STUB_RVA - len(code) - 5))

    code.extend(bytes.fromhex("837c243804"))
    mismatch("fallback")
    code.extend(bytes.fromhex("837c243c23"))
    mismatch("fallback")

    code.extend(bytes.fromhex("51e800000000"))
    pc_rva = WEAPON_STUB_RVA + len(code)
    code.extend(bytes.fromhex("5981e9") + struct.pack("<I", pc_rva))
    previous = 0

    for offset, rva in WEAPON_RETURN_CHAIN:
        code.extend(bytes.fromhex("81c1") + struct.pack("<i", rva - previous))
        offset += 4  # ECX remains saved on the stack.
        if offset < 0x80:
            code.extend(bytes.fromhex("394c24") + bytes([offset]))
        else:
            code.extend(bytes.fromhex("398c24") + struct.pack("<I", offset))

        mismatch("restoreFallback")
        previous = rva

    code.extend(bytes.fromhex("8b442438"))

    for offset in (0x20, 0x44, 0x68, 0x8C):
        if offset < 0x80:
            code.extend(bytes((0x83, 0x78, offset, 0)))
        else:
            code.extend(bytes.fromhex("83b8") + struct.pack("<I", offset) + b"\0")

        mismatch("restoreFallback")

    code.extend(bytes.fromhex("5931c0"))
    jump(FOG_RESUME_RVA)

    labels["restoreFallback"] = len(code)
    code.extend(b"\x59")

    labels["fallback"] = len(code)
    jump(CLEAR_STUB_RVA)

    for at, label in branches:
        struct.pack_into("<i", code, at, labels[label] - at - 4)

    return bytes(code)


def radar_stub_bytes(stub_rva, fallback_rva, return_chain, *, clipped):
    require_configured()

    code, branches, labels = bytearray(), [], {}

    def branch(opcode, label):
        code.extend((0x0F, opcode))
        branches.append((len(code), label))
        code.extend(bytes(4))

    def jump(target):
        code.extend(b"\xe9" + struct.pack("<i", target - stub_rva - len(code) - 5))

    # The needle is always a quad. Clipping the convex background quad against
    # four planes can produce three through eight vertices. Unsigned bounds
    # exclude negative counts before inspecting any source vertex.
    if clipped:
        code.extend(bytes.fromhex("837c243803"))
        branch(0x82, "fallback")
        code.extend(bytes.fromhex("837c243808"))
        branch(0x87, "fallback")
    else:
        code.extend(bytes.fromhex("837c243804"))
        branch(0x85, "fallback")

    code.extend(bytes.fromhex("837c243c10"))
    branch(0x85, "fallback")

    # Save EDX and derive each native return from the current instruction
    # pointer. Exact caller checks precede source reads and require no new
    # relocation entries. The saved register shifts stack positions by four.
    code.extend(bytes.fromhex("52e800000000"))
    pc_rva = stub_rva + len(code)
    code.extend(bytes.fromhex("5a81ea") + struct.pack("<I", pc_rva - return_chain[0][1]))
    previous = return_chain[0][1]

    for offset, rva in return_chain:
        delta = rva - previous
        if delta:
            code.extend(bytes.fromhex("81c2") + struct.pack("<I", delta & 0xFFFFFFFF))

        offset += 4
        if offset <= 0x7F:
            code.extend(bytes((0x39, 0x54, 0x24, offset)))
        else:
            code.extend(bytes.fromhex("399424") + struct.pack("<I", offset))

        branch(0x85, "restoreFallback")
        previous = rva

    if clipped:
        # Preserve ECX while examining exactly count initialized fog fields.
        # Two saved registers move the native count argument from +38 to +40.
        code.extend(bytes.fromhex("518b54244083c120"))
        labels["fogLoop"] = len(code)
        code.extend(bytes.fromhex("833900"))
        branch(0x85, "restoreSourceFallback")
        code.extend(bytes.fromhex("83c1244a"))
        branch(0x85, "fogLoop")
        code.extend(b"\x59")
    else:
        for offset in (0x20, 0x44, 0x68, 0x8C):
            if offset <= 0x7F:
                code.extend(bytes((0x83, 0x79, offset, 0)))
            else:
                code.extend(bytes.fromhex("83b9") + struct.pack("<I", offset) + b"\0")

            branch(0x85, "restoreFallback")

    # Both native shapes use one constant diffuse color. Gouraud preserves
    # that color and uses the existing native shade cache and command emitter.
    code.extend(bytes.fromhex("5a31ed3be8"))
    branch(0x84, "skip")
    jump(EMIT_RVA)

    labels["skip"] = len(code)
    jump(SKIP_RVA)

    if clipped:
        labels["restoreSourceFallback"] = len(code)
        code.extend(b"\x59")

    labels["restoreFallback"] = len(code)
    code.extend(b"\x5a")

    labels["fallback"] = len(code)
    jump(fallback_rva)

    for at, label in branches:
        struct.pack_into("<i", code, at, labels[label] - at - 4)

    return bytes(code)


def needle_stub_bytes():
    return radar_stub_bytes(NEEDLE_STUB_RVA, SECTION_RVA, NEEDLE_RETURN_CHAIN, clipped=False)


def background_stub_bytes():
    return radar_stub_bytes(
        BACKGROUND_STUB_RVA, NEEDLE_STUB_RVA, BACKGROUND_RETURN_CHAIN, clipped=True
    )


def section_bytes():
    require_configured()

    crosshair = stub_bytes(SECTION_RVA)
    clear = clear_stub_bytes()
    weapon = weapon_stub_bytes()
    needle = needle_stub_bytes()
    background = background_stub_bytes()
    Require(
        (len(crosshair), len(clear), len(weapon), len(needle), len(background))
        == (52, 58, 185, 207, 185),
        "Unexpected validated stub lengths",
    )

    code = bytearray(0x210 + len(background))
    code[: len(crosshair)] = crosshair
    code[0x40 : 0x40 + len(clear)] = clear
    code[0x80 : 0x80 + len(weapon)] = weapon
    code[0x140 : 0x140 + len(needle)] = needle
    code[0x210:] = background

    return bytes(code)


def build(source):
    pe = validate_layout(source)
    rva = Align(max(s.rva + s.mapped_size for s in pe.sections), pe.section_alignment)
    Require(rva == SECTION_RVA, "Unexpected shading section RVA")

    raw = Align(len(source), pe.file_alignment)
    code = section_bytes()
    raw_size = Align(len(code), pe.file_alignment)

    result = bytearray(source) + bytearray(raw + raw_size - len(source))
    result[raw : raw + len(code)] = code

    header = pe.section_table + pe.count * 40
    struct.pack_into(
        "<8sIIIIIIHHI",
        result,
        header,
        SECTION_NAME,
        len(code),
        rva,
        raw_size,
        raw,
        0,
        0,
        0,
        0,
        0x60000020,
    )
    struct.pack_into("<H", result, pe.pe + 6, pe.count + 1)
    struct.pack_into("<I", result, pe.optional + 4, U32(source, pe.optional + 4) + raw_size)
    struct.pack_into("<I", result, pe.optional + 56, Align(rva + len(code), pe.section_alignment))

    hook = SectionOffset(pe, HOOK_RVA, 6)
    result[hook : hook + 6] = (
        b"\xe9" + struct.pack("<i", BACKGROUND_STUB_RVA - (HOOK_RVA + 5)) + b"\x90"
    )

    fog_hook = SectionOffset(pe, FOG_HOOK_RVA, 8)
    result[fog_hook : fog_hook + 8] = (
        b"\xe9" + struct.pack("<i", WEAPON_STUB_RVA - FOG_HOOK_RVA - 5) + b"\x90" * 3
    )

    struct.pack_into("<I", result, pe.checksum_offset, Checksum(result, pe.checksum_offset))

    allowed = [
        (pe.pe + 6, 2),
        (pe.optional + 4, 4),
        (pe.optional + 56, 4),
        (pe.checksum_offset, 4),
        (header, 40),
        (hook, 6),
        (fog_hook, 8),
    ]
    verify(source, bytes(result), allowed, rva, raw, code)

    report = {
        "format": REPORT_FORMAT,
        "parentPayloadVersion": PAYLOAD_VERSION,
        "sourceSha256": sha(source),
        "outputSha256": sha(result),
        "sourceSize": len(source),
        "outputSize": len(result),
        "section": {
            "name": SECTION_NAME.decode(),
            "rva": rva,
            "rawOffset": raw,
            "rawSize": raw_size,
            "virtualSize": len(code),
            "characteristics": 0x60000020,
        },
        "hooks": [
            {
                "rva": HOOK_RVA,
                "original": ORIGINAL.hex(),
                "replacement": bytes(result[hook : hook + 6]).hex(),
                "targetRva": BACKGROUND_STUB_RVA,
            },
            {
                "rva": FOG_HOOK_RVA,
                "original": FOG_ORIGINAL.hex(),
                "replacement": bytes(result[fog_hook : fog_hook + 8]).hex(),
                "targetRva": WEAPON_STUB_RVA,
            },
        ],
        "stubs": [
            {"name": name, "rva": at, "size": len(data), "sha256": sha(data), "hex": data.hex()}
            for name, at, data in (
                ("crosshair", SECTION_RVA, stub_bytes(SECTION_RVA)),
                ("clearFog", CLEAR_STUB_RVA, clear_stub_bytes()),
                ("weaponFog", WEAPON_STUB_RVA, weapon_stub_bytes()),
                ("radarNeedle", NEEDLE_STUB_RVA, needle_stub_bytes()),
                ("radarBackground", BACKGROUND_STUB_RVA, background_stub_bytes()),
            )
        ],
        "sectionSha256": sha(code),
        "behavior": [
            "Use native Gouraud shading for untextured three-vertex polygons with no positive source fog.",
            "Use native Gouraud shading for the exact radar needle quad and clipped dark-background caller chains with zero fog.",
            "Keep fog enabled except the two exact native clear calls and the exact four-vertex weapon-icon call chain with zero source fog.",
            "Use the original native shading and fog caches and command emitters.",
        ],
        "weaponReturnChain": [
            {"stackOffset": offset, "returnRva": at} for offset, at in WEAPON_RETURN_CHAIN
        ],
        "needleReturnChain": [
            {"stackOffset": offset, "returnRva": at} for offset, at in NEEDLE_RETURN_CHAIN
        ],
        "radarBackgroundReturnChain": [
            {"stackOffset": offset, "returnRva": at} for offset, at in BACKGROUND_RETURN_CHAIN
        ],
        "preservation": {
            "allOldSectionsAndPayloadBytesUnchangedExceptListedChanges": True,
            "configAndStateUnchanged": True,
            "importsAndRelocationsUnchanged": True,
            "absoluteReferencesAdded": False,
            "newRelocationsRequired": False,
            "newMutableState": False,
            "stackUnchanged": True,
        },
        "allowedParentByteChanges": [{"offset": o, "size": n} for o, n in allowed],
        "limits": [
            "A supplied frozen output identity is verified. Static checks do not establish gameplay correctness.",
            "Clipped weapon shapes with a different vertex count retain the general fog policy.",
            "This builder does not install or execute the game.",
        ],
    }

    return bytes(result), report, code


def verify(source, result, allowed, rva, raw, code):
    pe = validate_layout(source)
    out = ShadingImage(result)
    Require(
        rva == SECTION_RVA and raw == PARENT_SIZE and code == section_bytes(),
        "Unexpected generated section inputs",
    )
    Require(
        out.count == pe.count + 1 and out.sections[:-1] == pe.sections,
        "Existing section metadata changed",
    )

    expected_section = Section(
        SECTION_NAME.decode(), len(code), rva, Align(len(code), pe.file_alignment), raw, 0x60000020
    )
    Require(out.sections[-1] == expected_section, "Stub section metadata differs")
    Require(
        len(result) == raw + expected_section.raw_size, "Unexpected candidate overlay or truncation"
    )
    Require(
        result[raw + len(code) :] == bytes(expected_section.raw_size - len(code)),
        "Stub padding is not zero",
    )
    Require(result[raw : raw + len(code)] == section_bytes(), "Stub contents differ")

    hook = SectionOffset(pe, HOOK_RVA, 6)
    Require(
        result[hook : hook + 6]
        == b"\xe9" + struct.pack("<i", BACKGROUND_STUB_RVA - (HOOK_RVA + 5)) + b"\x90",
        "Hook transfer differs",
    )

    fog_hook = SectionOffset(pe, FOG_HOOK_RVA, 8)
    Require(
        result[fog_hook : fog_hook + 8]
        == b"\xe9" + struct.pack("<i", WEAPON_STUB_RVA - FOG_HOOK_RVA - 5) + b"\x90" * 3,
        "Fog hook transfer differs",
    )

    Require(
        U32(result, out.optional + 4) == U32(source, pe.optional + 4) + expected_section.raw_size,
        "Incorrect SizeOfCode",
    )
    Require(out.image_size == Align(rva + len(code), pe.section_alignment), "Incorrect SizeOfImage")
    Require(
        U32(result, out.checksum_offset) == Checksum(result, out.checksum_offset),
        "Candidate checksum mismatch",
    )

    for i, (old, new) in enumerate(zip(source, result)):
        Require(
            old == new or any(at <= i < at + n for at, n in allowed),
            "Unexpected parent byte change at0x%x" % i,
        )

    Require(
        result[out.optional + 96 : out.optional + 224]
        == source[pe.optional + 96 : pe.optional + 224],
        "PE directories changed",
    )
    Require(
        result[SectionOffset(out, CONFIG_RVA, 104) : SectionOffset(out, CONFIG_RVA, 104) + 104]
        == source[SectionOffset(pe, CONFIG_RVA, 104) : SectionOffset(pe, CONFIG_RVA, 104) + 104],
        "Payload config changed",
    )
    Require(
        len(result) == OUTPUT_SIZE and (OUTPUT_SHA256 is None or sha(result) == OUTPUT_SHA256),
        "Output differs from the frozen candidate",
    )
