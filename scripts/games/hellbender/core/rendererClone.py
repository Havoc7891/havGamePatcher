# SPDX-License-Identifier: MIT

"""Build the bounded native renderer clone using only verified retail bytes."""

from __future__ import annotations

import hashlib
import re
import struct

from peImage import Require, U32

CLONE_BYTES = 460
CLONE_EXPORTS = ("guardedRendererImage", "ChildCreateDevice")
SEGMENT_LAYOUT = ((0, 435, 0), (492, 8, 435), (500, 17, 443))
BRIDGE_REPLACEMENT = bytes.fromhex("50e80000000083c40890")


def Number(value):
    Require(isinstance(value, (int, str)) and not isinstance(value, bool), "Invalid clone integer")
    result = int(value, 0) if isinstance(value, str) else value
    Require(0 <= result <= 0xFFFFFFFF, "Clone integer is outside DWORD range")
    return result


def Digest(value):
    Require(isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value), "Invalid clone digest")
    return value


def ValidateProfile(profile):
    clone = profile["rendererClone"]
    entry = Number(clone["nativeEntryRva"])
    Require(
        clone["version"] == 1
        and clone["nativeBytes"] == 517
        and clone["cloneBytes"] == CLONE_BYTES
        and entry == Number(profile["configFields"]["rendererInitOriginal"]),
        "Renderer clone entry or size differs",
    )
    Require(
        [(Number(s["rva"]) - entry, s["bytes"], s["cloneOffset"]) for s in clone["segments"]]
        == list(SEGMENT_LAYOUT),
        "Renderer clone segment layout differs",
    )
    Digest(clone["nativeSha256"])
    Digest(clone["templateSha256"])
    for segment in clone["segments"]:
        Digest(segment["sha256"])
    fallback = clone["excludedFallback"]
    Require(
        Number(fallback["rva"]) == entry + 435 and fallback["bytes"] == 57,
        "Renderer fallback extent differs",
    )
    Digest(fallback["sha256"])
    bridge = bytes.fromhex(clone["bridgeOriginal"])
    Require(
        len(bridge) == 10
        and bridge[:3] == bytes.fromhex("8b1868")
        and bridge[7:] == bytes.fromhex("50ff13")
        and U32(bridge, 3)
        == Number(profile["preferredBase"]) + Number(profile["gameRvas"]["deviceIid"]),
        "Renderer device-IID bridge preimage differs",
    )
    absolute = clone["absoluteRelocations"]
    offsets = [Number(row["cloneOffset"]) for row in absolute]
    Require(
        len(absolute) == 26
        and offsets == sorted(set(offsets))
        and all(0 <= at <= CLONE_BYTES - 4 for at in offsets)
        and all(left + 4 <= right for left, right in zip(offsets, offsets[1:])),
        "Renderer clone absolute operand inventory differs",
    )
    for row in absolute:
        Require(
            0 < Number(row["targetRva"]) < Number(profile["imageSize"]),
            "Renderer clone target is outside the retail image",
        )
        Number(row["sourceRva"])
    calls = clone["relativeCalls"]
    Require([row["cloneOffset"] for row in calls] == [426, 443], "Renderer clone calls differ")
    Require(
        all(0 < Number(row["targetRva"]) < Number(profile["imageSize"]) for row in calls),
        "Renderer clone call is outside the retail image",
    )
    return clone


def ReadNative(profile, game):
    """Check the entire native body before copying its three retained segments."""
    clone = ValidateProfile(profile)
    entry = Number(clone["nativeEntryRva"])
    source = game.Mapped()
    body = source[entry : entry + clone["nativeBytes"]]
    Require(hashlib.sha256(body).hexdigest() == clone["nativeSha256"], "Native renderer changed")
    Require(game.SectionAt(entry, len(body)).flags & 0x20000000, "Native renderer is not code")
    relocations = game.Relocations()
    template = bytearray()
    observed = []
    for segment in clone["segments"]:
        at, size, offset = Number(segment["rva"]), segment["bytes"], segment["cloneOffset"]
        data = source[at : at + size]
        Require(
            len(template) == offset and hashlib.sha256(data).hexdigest() == segment["sha256"],
            "Native renderer segment changed",
        )
        template.extend(data)
        for fixup in sorted(relocations):
            if at <= fixup < at + size:
                Require(fixup + 4 <= at + size, "Native relocation crosses a clone segment")
                observed.append(
                    {
                        "sourceRva": fixup,
                        "cloneOffset": offset + fixup - at,
                        "targetRva": U32(source, fixup) - game.base,
                    }
                )
    Require(template[15:25].hex() == clone["bridgeOriginal"], "Native bridge bytes changed")
    displaced = [row for row in observed if 15 <= row["cloneOffset"] < 25]
    Require(
        displaced
        == [
            {
                "sourceRva": entry + 18,
                "cloneOffset": 18,
                "targetRva": Number(profile["gameRvas"]["deviceIid"]),
            }
        ],
        "Native bridge relocation differs",
    )
    observed = [row for row in observed if row not in displaced]
    expected = [
        {key: Number(value) for key, value in row.items()} for row in clone["absoluteRelocations"]
    ]
    Require(observed == expected, "Native clone relocation preimages differ")
    fallback = clone["excludedFallback"]
    at = Number(fallback["rva"])
    Require(
        hashlib.sha256(source[at : at + fallback["bytes"]]).hexdigest() == fallback["sha256"],
        "Native renderer fallback changed",
    )
    Require(
        template[433:435] == bytes.fromhex("7541")
        and template[435:443] == bytes.fromhex("33c05f5b83c438c3")
        and template[448:] == bytes.fromhex("5f83f8011bc05b4083c438c3"),
        "Renderer clone branch or cleanup differs",
    )
    for row in clone["relativeCalls"]:
        offset = row["cloneOffset"]
        native = entry + (offset if offset == 426 else 500)
        Require(
            source[native] == 0xE8
            and native + 5 + struct.unpack_from("<i", source, native + 1)[0]
            == Number(row["targetRva"]),
            "Native renderer external call target differs",
        )
    template[15:25] = BRIDGE_REPLACEMENT
    template[433:435] = bytes.fromhex("7508")
    Require(
        hashlib.sha256(template).hexdigest() == clone["templateSha256"],
        "Renderer clone template differs",
    )
    return {
        "template": bytes(template),
        "absoluteRelocations": expected,
        "relativeCalls": [
            {key: Number(value) for key, value in row.items()} for row in clone["relativeCalls"]
        ],
    }


def Construct(native, image_base, clone_rva, helper_rva):
    code = bytearray(native["template"])
    for row in native["absoluteRelocations"]:
        value = image_base + row["targetRva"]
        Require(0 <= value <= 0xFFFFFFFF, "Renderer clone absolute target overflows")
        struct.pack_into("<I", code, row["cloneOffset"], value)
    calls = [{"cloneOffset": 16, "targetRva": helper_rva}, *native["relativeCalls"]]
    for row in calls:
        at = row["cloneOffset"]
        distance = row["targetRva"] - (clone_rva + at + 5)
        Require(
            code[at] == 0xE8 and -0x80000000 <= distance <= 0x7FFFFFFF,
            "Renderer clone relative call does not fit",
        )
        struct.pack_into("<i", code, at + 1, distance)
    return bytes(code)
