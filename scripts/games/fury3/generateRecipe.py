#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Reproduce the reviewed Fury3 fullscreen image from maintained sources.

Only the exact English retail and joystick-only images are accepted. No game is
installed or started. Freezing records a reviewed build, not gameplay acceptance.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).absolute().parents[3]
CORE = Path(__file__).absolute().parent / "core"
sys.path.insert(0, str(ROOT / "scripts/core"))
sys.path.insert(0, str(CORE))
import peImage
import recipeData
import embedPayload
from payloadSchema import (
    PATCH_VERSION,
    PAYLOAD_VERSION,
    PAYLOAD_SHA256,
    OUTPUT_SHA256,
    OUTPUT_SIZE,
    TIMESTAMP,
    Number,
    ValidateProfile,
)
from peImage import Require

SOURCES = (
    ".clang-format",
    "src/games/fury3/core/fullscreenPayload.cpp",
    "src/games/fury3/core/fullscreenPayload.def",
    "include/games/fury3/core/fullscreenPayload.hpp",
    "scripts/games/fury3/core/embedPayload.py",
    "scripts/games/fury3/core/payloadSchema.py",
    "scripts/games/fury3/generateRecipe.py",
    "scripts/games/fury3/buildPayload.cmd",
    "scripts/core/peImage.py",
    "scripts/core/recipeData.py",
)


def Sha(data):
    return hashlib.sha256(data).hexdigest()


class Recipe:
    def __init__(self, edition="English"):
        Require(edition == "English", "Unsupported Fury3 edition")

        self.edition = edition
        self.directory = ROOT / "patches/fury3/english"
        self.output = ROOT / "out/patches/fury3/english"
        self.profile_path = self.directory / "versionProfile.json"
        self.profile_bytes = self.profile_path.read_bytes()
        self.profile = json.loads(self.profile_bytes)
        ValidateProfile(self.profile)

        self.lock_path = self.directory / "recipeLock.json"
        self.header_path = ROOT / "include/games/fury3/english/fullscreenPatchData.hpp"

    def PreparedProfile(self):
        ValidateProfile(self.profile)
        value = {
            "format": "Fury3PreparedProfile",
            "edition": self.edition,
            "profileSha256": Sha(self.profile_bytes),
            "patchVersion": PATCH_VERSION,
            "payloadVersion": PAYLOAD_VERSION,
            "configSize": 24,
            "stateSize": 132,
            "toolchain": "MSVC 14.44 x86",
        }
        return (json.dumps(value, indent=2) + "\n").encode("ascii")

    def Prepare(self):
        self.output.mkdir(parents=True, exist_ok=True)
        (self.output / "preparedProfile.json").write_bytes(self.PreparedProfile())

    def Canonical(self, source):
        ValidateProfile(self.profile)

        profile = self.profile
        Require(
            len(source) == Number(profile["originalSize"])
            and Sha(source) in {profile["originalSha256"], profile["joystickSha256"]},
            "Input is not the exact supported English Fury3 retail executable",
        )

        original = bytearray(source)
        at = Number(profile["joystick"]["fileOffset"])
        original[at : at + 10] = bytes.fromhex(profile["joystick"]["original"])
        Require(Sha(original) == profile["originalSha256"], "Original normalization mismatch")

        joystick = bytearray(original)
        joystick[at : at + 10] = bytes.fromhex(profile["joystick"]["patched"])
        Require(Sha(joystick) == profile["joystickSha256"], "Joystick normalization mismatch")

        return bytes(original), bytes(joystick)

    def VerifyCompiledProfile(self, payload):
        ValidateProfile(self.profile)

        image = peImage.PE(payload)
        code_section = next(section for section in image.sections if section.name == ".text")
        code = payload[code_section.raw_offset : code_section.raw_offset + code_section.raw_size]
        main = struct.pack("<I", Number(self.profile["gameRvas"]["mainWindow"]))
        legacy = struct.pack("<I", Number(self.profile["gameRvas"]["legacyFullscreen"]))
        command = struct.pack("<I", Number(self.profile["fullscreenCommand"]))

        for probe, count in (
            (b"\x8b\x80" + main, 3),
            (b"\x8b\x81" + main, 1),
            (b"\x8b\xb0" + main, 1),
            (b"\x83\xb8" + legacy + b"\x00", 1),
            (b"\x81\x7d\x0c" + command, 1),
        ):
            Require(
                code.count(probe) == count,
                "Compiled native constants differ from the reviewed profile",
            )

        for name, prefix in (
            ("joystickRawFallback", b"\x39\x88"),
            ("joystickButtons", b"\x89\x88"),
            ("joystickEnabled", b"\x89\x88"),
        ):
            probe = prefix + struct.pack("<I", Number(self.profile["gameRvas"][name]))
            Require(
                code.count(probe) == 1,
                "Compiled joystick constants differ from the reviewed profile",
            )

        for name in ("originalJoystickPoll", "originalPrepareMainMenu", "originalLeaveMainMenu"):
            probe = (
                b"\x05" + struct.pack("<I", Number(self.profile["gameRvas"][name])) + b"\xff\xe0"
            )
            Require(
                code.count(probe) == 1,
                "Compiled original function forwarding differs from the reviewed profile",
            )

        for name, prefix, suffix in (
            ("movieOpen", b"\x39\x98", b""),
            ("movieOpen", b"\x83\xb8", b"\x00"),
            ("movieWindow", b"\x39\xb0", b""),
            ("movieWindow", b"\x8b\x98", b""),
            ("originalResizeMovie", b"\x05", b"\xff\xd0"),
        ):
            probe = prefix + struct.pack("<I", Number(self.profile["gameRvas"][name])) + suffix
            Require(
                code.count(probe) == 1,
                "Compiled movie sizing constants differ from the reviewed profile",
            )

        for commandText in (b"put mov destination\0", b"where mov source\0"):
            Require(
                code.count(commandText) == 1,
                "Compiled movie commands differ from the reviewed payload",
            )

        exports = image.Exports()
        Require(
            set(exports)
            == {
                "config",
                "state",
                "ToggleFullscreen",
                "StretchFrame",
                "UpdateMenuCheck",
                "PollJoystick",
                "PrepareMainMenu",
                "LeaveMainMenu",
                "ResizeMovie",
            },
            "Unexpected payload exports",
        )
        Require(
            struct.unpack_from("<6I", payload, image.Offset(exports["config"], 24))
            == (0x46534650, 1, 0, 0, 0, 0),
            "Unexpected compiled config ABI",
        )
        Require(
            not any(image.Mapped()[exports["state"] : exports["state"] + 132]),
            "Payload state is not initially zero",
        )

    def ValidateLock(self, lock, payload):
        Require(
            lock["edition"] == self.edition
            and lock["patchVersion"] == PATCH_VERSION
            and lock["payloadVersion"] == PAYLOAD_VERSION,
            "Unsupported recipe lock edition or version",
        )
        Require(
            lock["profileSha256"] == Sha(self.profile_bytes), "Reviewed profile differs from lock"
        )
        Require(
            lock["payloadSha256"] == Sha(payload) == PAYLOAD_SHA256,
            "Payload differs from frozen build",
        )
        Require(
            lock["patchedSha256"] == OUTPUT_SHA256 and lock["patchedSize"] == OUTPUT_SIZE,
            "Output identity differs from reviewed build",
        )

    def Build(self, source, payload, lock=None):
        self.Canonical(source)
        self.VerifyCompiledProfile(payload)
        Require(Sha(payload) == PAYLOAD_SHA256, "Payload differs from reviewed build")

        if lock is not None:
            self.ValidateLock(lock, payload)

        final, report = embedPayload.Build(source, payload, self.profile)
        Require(
            Sha(final) == OUTPUT_SHA256 and len(final) == OUTPUT_SIZE,
            "Generated output differs from the reviewed build",
        )

        return final, report

    def MakeHeader(self, original, final):
        spans, replacements, appended = recipeData.make_recipe(original, final)

        for source in self.Canonical(original):
            Require(
                recipeData.apply_recipe(source, spans, replacements, appended) == final,
                "Native recipe does not reconstruct the exact result",
            )

        header = recipeData.make_header(
            len(original),
            spans,
            replacements,
            appended,
            namespace="havGamePatcher::fury3English",
            provenance="Generated by scripts/games/fury3/generateRecipe.py from maintained payload sources and an identified English retail image.",
            patch_version=PATCH_VERSION,
            payload_version=PAYLOAD_VERSION,
            source_hashes={
                "originalSha256": self.profile["originalSha256"],
                "joystickSha256": self.profile["joystickSha256"],
            },
            patched_sha256=Sha(final),
            patched_size=len(final),
        )

        return (
            recipeData.FormatCpp(header.encode("ascii"), self.header_path.name),
            spans,
            replacements,
            appended,
        )

    def Generate(self, input_path, payload_path, freeze=False, check=False):
        input_path, payload_path = Path(input_path), Path(payload_path)
        Require(not (freeze and check), "Use --freeze or --check, not both")

        targets = (
            self.header_path,
            self.lock_path,
            self.profile_path,
            self.output / "preparedProfile.json",
            self.output / "FURY3.fullscreen.EXE",
            self.output / "recipeReport.json",
        )

        for source in (input_path, payload_path):
            for target in targets:
                Require(
                    source.resolve() != target.resolve()
                    and not (target.exists() and source.samefile(target)),
                    "Output aliases an input file",
                )

        Require(
            self.profile_path.read_bytes() == self.profile_bytes,
            "Version profile changed after loading",
        )
        Require(
            (self.output / "preparedProfile.json").read_bytes() == self.PreparedProfile(),
            "Prepared profile differs from the reviewed profile. Run --prepare again",
        )

        original, joystick = self.Canonical(input_path.read_bytes())
        payload = peImage.NormalizePayload(payload_path.read_bytes(), TIMESTAMP)
        lock = json.loads(self.lock_path.read_text()) if self.lock_path.exists() else None
        Require(
            lock is not None or freeze,
            "Recipe is not frozen. First reviewed build requires --freeze",
        )

        if lock is not None:
            self.ValidateLock(lock, payload)

        final, transformations = self.Build(original, payload, lock)
        other, _ = self.Build(joystick, payload, lock)
        Require(other == final, "Retail and joystick-only inputs produce different results")

        header, spans, replacements, appended = self.MakeHeader(original, final)

        if check:
            Require(
                self.header_path.read_bytes() == header,
                "Generated header differs from maintained recipe",
            )

        if lock is None:
            lock = {
                "edition": self.edition,
                "patchVersion": PATCH_VERSION,
                "payloadVersion": PAYLOAD_VERSION,
                "profileSha256": Sha(self.profile_bytes),
                "payloadSha256": Sha(payload),
                "patchedSha256": Sha(final),
                "patchedSize": len(final),
            }

        report = {
            **lock,
            "originalSha256": self.profile["originalSha256"],
            "joystickSha256": self.profile["joystickSha256"],
            "originalSize": len(original),
            "spanCount": len(spans),
            "replacementBytes": len(replacements),
            "appendedBytes": len(appended),
            "headerSha256": Sha(header),
            "bothInputsProduceIdenticalOutput": True,
            "recipeReconstructsBothInputs": True,
            "checkedInHeaderVerified": check,
            "transformations": transformations,
            "gameInstalled": False,
            "gameStarted": False,
            "sourceHashes": {name: Sha((ROOT / name).read_bytes()) for name in SOURCES},
        }
        self.output.mkdir(parents=True, exist_ok=True)

        if not self.lock_path.exists():
            self.lock_path.parent.mkdir(parents=True, exist_ok=True)
            self.lock_path.write_text(json.dumps(lock, indent=2) + "\n")

        if not check:
            self.header_path.parent.mkdir(parents=True, exist_ok=True)
            self.header_path.write_bytes(header)

        (self.output / "FURY3.fullscreen.EXE").write_bytes(final)
        (self.output / "recipeReport.json").write_text(json.dumps(report, indent=2) + "\n")

        return report


def Main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--edition", choices=("English",), default="English")
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--input", type=Path)
    parser.add_argument("--payload", type=Path)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--freeze", action="store_true")
    action.add_argument("--check", action="store_true")
    args = parser.parse_args()
    recipe = Recipe(args.edition)

    if args.prepare:
        Require(
            not args.input and not args.payload and not args.freeze and not args.check,
            "--prepare cannot be combined with generation options",
        )
        recipe.Prepare()
        print("Prepared English Fury3 profile. No game was installed or started.")

        return

    Require(args.input is not None and args.payload is not None, "Specify --input and --payload")

    report = recipe.Generate(args.input, args.payload, args.freeze, args.check)
    print(
        json.dumps(
            {
                key: report[key]
                for key in ("edition", "patchedSha256", "patchedSize", "checkedInHeaderVerified")
            }
        )
    )


if __name__ == "__main__":
    try:
        Main()
    except (OSError, ValueError, KeyError) as error:
        # Keep installation paths out of diagnostics and shareable command output.
        detail = (
            str(error)
            if not isinstance(error, OSError)
            else "A required file could not be read or written"
        )
        print("Generation failed: " + detail, file=sys.stderr)
        sys.exit(1)
