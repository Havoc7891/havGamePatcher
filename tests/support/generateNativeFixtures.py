#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Generate native recovery-test headers from a supported, unmodified retail image."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "scripts/core"))

from peImage import PE, Require
from recipeData import FormatCpp


def Hash(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def ReadSpan(image: PE, span: dict) -> bytes:
    start = image.Offset(span["rva"], span["size"])
    data = image.data[start : start + span["size"]]
    Require(Hash(data) == span["sha256"], "Native fixture span does not match its reviewed bytes")
    return data


def ByteArray(name: str, data: bytes) -> str:
    rows = [
        ", ".join(f"0x{value:02x}" for value in data[i : i + 16]) for i in range(0, len(data), 16)
    ]
    return f"static const BYTE {name}[] = {{\n" + ",\n".join(rows) + "\n};\n"


def DwordArray(name: str, values: list[int]) -> str:
    # MSVC needs storage even for an empty relocation list. Its explicit count remains zero.
    return f"static const DWORD {name}[] = {{" + ", ".join(hex(v) for v in values or [0]) + "};\n"


def Constants(values: dict, namespace: str | None = None) -> str:
    body = "\n".join(f"constexpr DWORD {name} = 0x{value:x};" for name, value in values.items())
    return f"namespace {namespace} {{\n{body}\n}}\n" if namespace else body + "\n"


def CloneTemplate(image: PE, spec: dict) -> bytes:
    template = bytearray(b"".join(ReadSpan(image, span) for span in spec["segments"]))
    Require(len(template) == 460, "Unexpected guarded renderer size")
    # Replace the original COM call with the cdecl bridge. Omit the CPU-cache fallback.
    template[15:25] = bytes.fromhex("50e80000000083c40890")
    template[433:435] = bytes.fromhex("7508")
    Require(Hash(template) == spec["sha256"], "Guarded renderer template differs")
    for offset, target in spec["absolute"]:
        Require(
            struct.unpack_from("<I", template, offset)[0] == image.base + target,
            "Guarded renderer absolute operand differs",
        )
    return bytes(template)


def SpanGroup(image: PE, spec: dict, kind: str) -> str:
    groups = {
        "preLock": ("PreLockNative", ["lockCaller", "surfaceLock", "begin"]),
        "movieCleanup": (
            "MovieCleanupNative",
            [
                "cleanupLoop",
                "beginDispatcher",
                "clearScreen",
                "clearDispatcher",
                "clearPrimitive",
                "quadEmitter",
            ],
        ),
    }
    namespace, names = groups[kind]
    spans = spec["spans"]
    Require(
        [span["name"] for span in spans] == [name + "Bytes" for name in names],
        "Native fixture group must contain exactly its reviewed spans",
    )
    parts = [
        f"namespace {namespace} {{",
        "struct Span { DWORD rva, size; const BYTE* data; const DWORD* relocations; DWORD count; };",
    ]
    rows = []
    for span in spans:
        parts.append(ByteArray(span["name"], ReadSpan(image, span)))
        parts.append(DwordArray(span["relocationName"], span["relocations"]))
        fields = (
            f"0x{span['rva']:x}, {span['size']}, {span['name']}, "
            f"{span['relocationName']}, {len(span['relocations'])}"
        )
        rows.append("{" + fields + "}")
    groupRows = rows[:2] if kind == "preLock" else rows
    parts.append("static const Span spans[] = {" + ",\n".join(groupRows) + "};")
    if kind == "preLock":
        parts.append("static const Span begin = " + rows[2] + ";")
    parts.append(Constants(spec["constants"]))
    parts.append("}")
    return "\n\n".join(parts)


def BuildHeader(image: PE, spec: dict, filename: str, edition: str) -> bytes:
    guard = f"HAVGAMEPATCHER_TEST_{edition.upper()}_{filename.replace('.', '_').upper()}"
    parts = [
        "// Generated from a verified retail image. Do not edit.",
        f"#ifndef {guard}",
        f"#define {guard}",
        "#include <windows.h>",
    ]
    kind = spec["kind"]
    spans = spec["spans"]
    prefix = spec.get("prefix", "Native")

    if kind in ("group", "frame"):
        fields = "DWORD rva, size; const BYTE* bytes;"
        if kind == "frame":
            fields += " const DWORD* relocations; DWORD count;"
        parts.append(f"struct {prefix}Span {{ {fields} }};")

    for span in spans:
        if "rvaName" in span:
            parts.append(f"constexpr DWORD {span['rvaName']} = 0x{span['rva']:x};")
        parts.append(ByteArray(span["name"], ReadSpan(image, span)))
        if "relocationName" in span:
            parts.append(DwordArray(span["relocationName"], span["relocations"]))

    if kind in ("group", "frame"):
        rows = []
        for span in spans:
            fields = f"0x{span['rva']:x}, {span['size']}, {span['name']}"
            if kind == "frame":
                fields += f", {span['relocationName']}, {len(span['relocations'])}"
            rows.append("{" + fields + "}")
        parts.append(f"static const {prefix}Span {prefix}Spans[] = {{" + ",\n".join(rows) + "};")
        if kind == "group":
            relocations = sorted(
                span["rva"] + offset for span in spans for offset in span["relocations"]
            )
            parts.append(DwordArray(prefix + "Relocations", relocations))

    if "clone" in spec:
        parts.append(ByteArray("RendererCloneTemplate", CloneTemplate(image, spec["clone"])))
        parts.append("struct CloneAbsolute { DWORD offset, target; };")
        rows = [f"{{{offset}, 0x{target:x}}}" for offset, target in spec["clone"]["absolute"]]
        parts.append(
            "static const CloneAbsolute RendererCloneAbsolute[] = {" + ",\n".join(rows) + "};"
        )

    if "constants" in spec:
        parts.append(Constants(spec["constants"], spec.get("namespace")))
    for group in ("preLock", "movieCleanup"):
        if group in spec:
            Require(kind == "frame", "Recovery span groups belong to the frame fixture header")
            parts.append(SpanGroup(image, spec[group], group))
    parts.append("#endif")
    return ("\n\n".join(parts) + "\n").encode("utf-8")


def ValidateSpans(image: PE, headers: dict) -> None:
    relocations = image.Relocations()
    for header in headers.values():
        spans = header["spans"] + [
            span
            for group in ("preLock", "movieCleanup")
            for span in header.get(group, {}).get("spans", [])
        ]
        for span in spans:
            data = ReadSpan(image, span)
            start, end = span["rva"], span["rva"] + span["size"]
            expected = [start + offset for offset in span["relocations"]]
            actual = sorted(rva for rva in relocations if start <= rva < end)
            Require(
                actual == expected and all(rva + 4 <= end for rva in actual),
                "Native fixture relocation inventory differs",
            )
            for offset, target in span["calls"]:
                Require(
                    0 <= offset <= len(data) - 5 and data[offset] == 0xE8,
                    "Native fixture call opcode differs",
                )
                actualTarget = start + offset + 5 + struct.unpack_from("<i", data, offset + 1)[0]
                Require(actualTarget == target, "Native fixture call target differs")


def Main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--edition", choices=("English", "French", "German"), required=True)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifestPath = Path(__file__).with_name("nativeFixtureManifest.json")
    manifest = json.loads(manifestPath.read_text(encoding="utf-8"))
    Require(
        manifest["format"] == "HellbenderNativeFixtureManifest" and manifest["version"] == 1,
        "Unsupported native fixture manifest",
    )
    spec = manifest["editions"][args.edition]
    Require(
        set(spec["headers"])
        == {
            "setupNative.hpp",
            "stampNative.hpp",
            "rendererNative.hpp",
            "presentationNative.hpp",
            "inputNative.hpp",
            "frameNative.hpp",
        },
        "Native fixture manifest must name exactly the six supported headers",
    )
    profilePath = PROJECT_ROOT / "patches/hellbender" / args.edition.lower() / "versionProfile.json"
    profileData = profilePath.read_bytes()
    profile = json.loads(profileData)
    Require(Hash(profileData) == spec["profileSha256"], "The reviewed retail profile changed")
    inputData = args.input.read_bytes()
    Require(
        Hash(inputData) == profile["originalSha256"] == spec["retailSha256"],
        "Tests require the unmodified retail executable for the selected edition",
    )
    Require(len(inputData) == profile["originalSize"], "Retail image size differs")
    image = PE(inputData)
    Require(image.base == int(profile["preferredBase"], 0), "Retail image base differs")
    ValidateSpans(image, spec["headers"])

    output = args.output.resolve()
    Require(
        output.is_relative_to((PROJECT_ROOT / "out/test").resolve()),
        "Generated native headers must remain under this project's out/test directory",
    )
    Require(not args.input.resolve().is_relative_to(output), "Output directory contains the input")
    generated = {
        name: FormatCpp(BuildHeader(image, header, name, args.edition), name)
        for name, header in spec["headers"].items()
    }
    # Finish all validation and formatting before replacing any prior fixture output.
    output.mkdir(parents=True, exist_ok=True)
    for name, data in generated.items():
        (output / name).write_bytes(data)
    sources = [
        Path(__file__),
        manifestPath,
        profilePath,
        PROJECT_ROOT / "scripts/core/peImage.py",
        PROJECT_ROOT / "scripts/core/recipeData.py",
        PROJECT_ROOT / ".clang-format",
    ]
    receipt = {
        "format": "HellbenderNativeFixtureInputs",
        "version": 1,
        "edition": args.edition,
        "retailSha256": Hash(inputData),
        "sourceHashes": {
            path.relative_to(PROJECT_ROOT).as_posix(): Hash(path.read_bytes()) for path in sources
        },
        "headerHashes": {name: Hash(data) for name, data in generated.items()},
    }
    (output / "fixtureInputs.json").write_text(
        json.dumps(receipt, indent=2) + "\n", encoding="utf-8"
    )
    print(
        json.dumps(
            {"edition": args.edition, "headers": len(generated), "retailSha256": Hash(inputData)}
        )
    )


if __name__ == "__main__":
    Main()
