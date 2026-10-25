#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Build exact English, French and German recipes from the shared fullscreen payload.

Profiles contain independently checked native addresses and hook preimages.
The first reviewed build is frozen explicitly. Subsequent builds must reproduce
its payload, complete executable and generated header. Game inputs are read only.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).absolute().parents[3]
CORE = Path(__file__).absolute().parent / "core"
sys.path.insert(0, str(ROOT / "scripts/core"))
sys.path.insert(0, str(CORE))
import payloadSchema
import recipeData
from peImage import PE, NormalizePayload

EDITIONS = ("English", "French", "German")
TIMESTAMP = 0x6AD37ECB
SOURCES = (
    ".clang-format",
    "src/games/hellbender/core/layoutPayload.cpp",
    "src/games/hellbender/core/layoutPayload.def",
    "include/games/hellbender/core/layoutPayload.hpp",
    "include/games/hellbender/core/displayReadiness.hpp",
    "include/games/hellbender/core/flipProbe.hpp",
    "include/games/hellbender/core/movieRecovery.hpp",
    "include/games/hellbender/core/textureRecovery.hpp",
    "include/games/hellbender/core/textureLoadRecovery.hpp",
    "include/games/hellbender/core/textureLifecycle.hpp",
    "include/games/hellbender/core/textureUploadOwnership.hpp",
    "include/games/hellbender/core/textureTransfer.hpp",
    "include/games/hellbender/core/focusRecovery.hpp",
    "include/games/hellbender/core/menuShell.hpp",
    "include/games/hellbender/core/d3dBridge.hpp",
    "scripts/games/hellbender/core/embedPayload.py",
    "scripts/games/hellbender/core/appendShading.py",
    "scripts/games/hellbender/core/payloadSchema.py",
    "scripts/games/hellbender/core/rendererClone.py",
    "scripts/core/peImage.py",
    "scripts/core/recipeData.py",
    "scripts/games/hellbender/generateRecipe.py",
    "scripts/games/hellbender/buildPayload.cmd",
    "include/games/hellbender/core/emptyCacheDeferral.hpp",
    "include/games/hellbender/core/failedPresentationRecovery.hpp",
    "include/games/hellbender/core/failedPresentationTypes.hpp",
    "include/games/hellbender/core/flushOwnership.hpp",
    "include/games/hellbender/core/frameFailureHooks.hpp",
    "include/games/hellbender/core/frameNativeAddresses.hpp",
    "include/games/hellbender/core/hardwareModeCompletion.hpp",
    "include/games/hellbender/core/hardwareModeCompletionTypes.hpp",
    "include/games/hellbender/core/lostSurfaceReadiness.hpp",
    "include/games/hellbender/core/menuModeCompletion.hpp",
    "include/games/hellbender/core/menuModeCompletionTypes.hpp",
    "include/games/hellbender/core/menuNativeAddresses.hpp",
    "include/games/hellbender/core/modeAdmission.hpp",
    "include/games/hellbender/core/modeAdmissionTypes.hpp",
    "include/games/hellbender/core/modeNativeAddresses.hpp",
    "include/games/hellbender/core/fullscreenSetup.hpp",
    "include/games/hellbender/core/presentationInput.hpp",
    "include/games/hellbender/core/unlockFrameRecovery.hpp",
    "include/games/hellbender/core/postRestoreRenderer.hpp",
    "include/games/hellbender/core/normalFrameOwnership.hpp",
    "include/games/hellbender/core/preMovieRecovery.hpp",
    "include/games/hellbender/core/presentationNativeAddresses.hpp",
    "include/games/hellbender/core/quadFrameRecovery.hpp",
    "include/games/hellbender/core/readinessNativeAddresses.hpp",
    "include/games/hellbender/core/softwareModeCompletion.hpp",
    "include/games/hellbender/core/softwareModeCompletionTypes.hpp",
    "include/games/hellbender/core/surfaceRestoreOwnership.hpp",
    "include/games/hellbender/core/worldRelockRecovery.hpp",
    "include/games/hellbender/core/worldRelockTypes.hpp",
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)

    return module


class Recipe:
    def __init__(self, edition):
        require(edition in EDITIONS, "Unsupported edition")

        self.edition = edition
        self.slug = edition.lower()
        self.directory = ROOT / "patches/hellbender" / self.slug
        self.output = ROOT / "out/patches/hellbender" / self.slug
        self.profile_path = self.directory / "versionProfile.json"
        self.profile_bytes = self.profile_path.read_bytes()
        self.profile = json.loads(self.profile_bytes)
        p = self.profile
        require(
            p["format"] == "HellbenderRetailVersionProfile" and p["version"] == 1,
            "Unsupported version profile format",
        )
        require(p["language"] == edition, "Profile edition mismatch")

        self.lock_path = self.directory / "recipeLock.json"
        self.header_path = ROOT / "include/games/hellbender" / self.slug / "fullscreenPatchData.hpp"

        self.embed = load_module("embed" + edition, CORE / "embedPayload.py")
        self.shading = load_module("shading" + edition, CORE / "appendShading.py")
        self.embed.configure(p)

    def addresses(self):
        values = self.profile["gameRvas"]
        require(
            set(values) == set(payloadSchema.GAME_RVA_NAMES),
            "Native address inventory differs from the shared payload schema",
        )
        require(
            all(0 <= number(value) <= 0xFFFFFFFF for value in values.values()),
            "Native addresses must fit in a DWORD",
        )

        guard = f"HAVGAMEPATCHER_HELLBENDER_{self.edition.upper()}_GAME_ADDRESSES_HPP"
        lines = [
            f"#ifndef {guard}",
            f"#define {guard}",
            "",
            "#define HAV_GAME_ADDRESSES_DEFINED",
            "#ifndef WIN32_LEAN_AND_MEAN",
            "#define WIN32_LEAN_AND_MEAN",
            "#endif",
            "#include <windows.h>",
            "",
            "namespace GameRva {",
        ]
        lines += [
            f"    constexpr DWORD {name} = 0x{number(values[name]):x};"
            for name in payloadSchema.GAME_RVA_NAMES
        ]
        lines += [
            "}",
            "",
            f'enum {{ missionStackBytes = 0x{number(self.profile["payloadLayout"]["missionStack"]):x} }};',
            "",
            f"#endif // {guard}",
            "",
        ]

        return recipeData.FormatCpp("\n".join(lines).encode("ascii"), "gameAddresses.hpp")

    def prepare(self):
        addresses = self.addresses()
        self.output.mkdir(parents=True, exist_ok=True)
        (self.output / "gameAddresses.hpp").write_bytes(addresses)

    def canonical(self, source):
        p, e = self.profile, self.embed
        require(
            sha(source) in e.SOURCE_HASHES and len(source) == number(p["originalSize"]),
            "Input is not the exact supported " + self.edition + " retail executable",
        )

        original = bytearray(source)
        original[e.JOYSTICK_OFFSET : e.JOYSTICK_OFFSET + 10] = e.JOYSTICK_ORIGINAL
        require(sha(original) == p["originalSha256"], "Original normalization mismatch")

        joystick = bytearray(original)
        joystick[e.JOYSTICK_OFFSET : e.JOYSTICK_OFFSET + 10] = e.JOYSTICK_PATCHED
        require(sha(joystick) == p["joystickSha256"], "Joystick normalization mismatch")

        return bytes(original), bytes(joystick)

    def verify_compiled_profile(self, payload):
        pe = PE(payload)
        exports = pe.Exports()
        section = next(s for s in pe.sections if s.name == ".text")
        code = payload[section.raw_offset : section.raw_offset + section.raw_size]
        mission = b"\x81\xec" + struct.pack(
            "<I", number(self.profile["payloadLayout"]["missionStack"])
        )
        mission += b"\xff\x25" + struct.pack("<I", pe.base + exports["config"] + 12)
        require(
            code.count(mission) == 1,
            "Compiled mission trampoline does not match edition stack layout",
        )

        hook_at = pe.Offset(exports["createDeviceHook"], 37)
        hook = payload[hook_at : hook_at + 37]
        config_address = struct.pack("<I", pe.base + exports["config"] + 8)
        load_base = b"\x8b\x15" + config_address
        device = (
            load_base
            + b"\x81\xc2"
            + struct.pack("<I", number(self.profile["gameRvas"]["deviceIid"]))
        )
        resume = (
            load_base
            + b"\x81\xc2"
            + struct.pack("<I", number(self.profile["gameRvas"]["deviceCreateResume"]))
        )
        require(
            hook.startswith(device) and resume in hook,
            "Compiled Direct3D bridge does not use this edition native addresses",
        )

    def build(self, source, payload, lock=None):
        intermediate, embedding = self.embed.create_image(source, payload)
        self.shading.configure(
            self.profile,
            parent_sha256=sha(intermediate),
            parent_size=len(intermediate),
            config_rva=embedding["configRva"],
            section_rva=PE(intermediate).image_size,
            output_sha256=lock["patchedSha256"] if lock else None,
        )

        final, shading, _ = self.shading.build(intermediate)

        if lock:
            require(
                sha(intermediate) == lock["intermediateSha256"],
                "Intermediate differs from frozen recipe",
            )
            require(
                sha(final) == lock["patchedSha256"] and len(final) == lock["patchedSize"],
                "Final image differs from frozen recipe",
            )

        return final, intermediate, {"payload": embedding, "shading": shading}

    def make_header(self, original, final):
        spans, replacements, appended = recipeData.make_recipe(original, final)

        for source in self.canonical(original):
            require(
                recipeData.apply_recipe(source, spans, replacements, appended) == final,
                "Native recipe does not reconstruct the exact result",
            )

        header = recipeData.make_header(
            len(original),
            spans,
            replacements,
            appended,
            namespace="havGamePatcher::hellbender" + self.edition,
            provenance=(
                "Generated by scripts/games/hellbender/generateRecipe.py from maintained payload sources "
                f"and an identified {self.edition} retail image."
            ),
            patch_version=payloadSchema.PATCH_VERSION,
            payload_version=payloadSchema.PAYLOAD_VERSION,
            source_hashes={
                "originalSha256": self.profile["originalSha256"],
                "joystickSha256": self.profile["joystickSha256"],
            },
            patched_sha256=sha(final),
            patched_size=len(final),
        )

        return (
            recipeData.FormatCpp(header.encode("ascii"), self.header_path.name),
            spans,
            replacements,
            appended,
        )

    def generate(self, input_path, payload_path, freeze=False, check=False):
        targets = (
            self.header_path,
            self.lock_path,
            self.output / "HELLBEND.fullscreen.EXE",
            self.output / "HELLBEND.payload.EXE",
            self.output / "recipeReport.json",
            self.output / "layoutPayload.normalized.dll",
        )

        for src in (input_path, payload_path):
            for dst in targets:
                require(
                    src.resolve() != dst.resolve() and not (dst.exists() and src.samefile(dst)),
                    "Output aliases an input file",
                )

        require(
            self.profile_path.read_bytes() == self.profile_bytes,
            "Version profile changed after loading",
        )

        original, joystick = self.canonical(input_path.read_bytes())
        require(
            (self.output / "gameAddresses.hpp").read_bytes() == self.addresses(),
            "Generated compile-time addresses differ from the reviewed profile",
        )

        payload = NormalizePayload(payload_path.read_bytes(), TIMESTAMP)
        self.verify_compiled_profile(payload)
        lock = json.loads(self.lock_path.read_text()) if self.lock_path.exists() else None
        require(
            lock is not None or freeze,
            "Recipe is not frozen. First reviewed build requires --freeze",
        )

        if lock:
            require(lock["edition"] == self.edition, "Recipe lock edition mismatch")
            require(
                lock["patchVersion"] == payloadSchema.PATCH_VERSION
                and lock["payloadVersion"] == payloadSchema.PAYLOAD_VERSION,
                "Unsupported recipe or payload version in lock",
            )
            require(
                lock["profileSha256"] == sha(self.profile_bytes),
                "Reviewed profile differs from lock",
            )
            require(lock["payloadSha256"] == sha(payload), "Payload differs from frozen build")

        final, intermediate, transformations = self.build(original, payload, lock)
        other, _, _ = self.build(joystick, payload, lock)
        require(other == final, "Original and joystick inputs produce different results")

        header, spans, replacements, appended = self.make_header(original, final)

        if check:
            require(
                self.header_path.read_bytes() == header,
                "Generated header differs from maintained recipe",
            )

        self.output.mkdir(parents=True, exist_ok=True)

        if not lock:
            lock = {
                "edition": self.edition,
                "patchVersion": payloadSchema.PATCH_VERSION,
                "payloadVersion": payloadSchema.PAYLOAD_VERSION,
                "profileSha256": sha(self.profile_bytes),
                "payloadSha256": sha(payload),
                "intermediateSha256": sha(intermediate),
                "patchedSha256": sha(final),
                "patchedSize": len(final),
            }
            self.lock_path.write_text(json.dumps(lock, indent=2) + "\n")

        if not check:
            self.header_path.parent.mkdir(parents=True, exist_ok=True)
            self.header_path.write_bytes(header)

        (self.output / "HELLBEND.fullscreen.EXE").write_bytes(final)
        (self.output / "HELLBEND.payload.EXE").write_bytes(intermediate)
        (self.output / "layoutPayload.normalized.dll").write_bytes(payload)

        report = {
            **lock,
            "originalSha256": self.profile["originalSha256"],
            "joystickSha256": self.profile["joystickSha256"],
            "originalSize": len(original),
            "spanCount": len(spans),
            "replacementBytes": len(replacements),
            "appendedBytes": len(appended),
            "headerSha256": sha(header),
            "bothInputsProduceIdenticalOutput": True,
            "recipeReconstructsBothInputs": True,
            "checkedInHeaderVerified": check,
            "transformations": transformations,
            "gameInstalled": False,
            "gameStarted": False,
            "sourceHashes": {name: sha((ROOT / name).read_bytes()) for name in SOURCES},
            "generatorSha256": sha(Path(__file__).read_bytes()),
        }
        (self.output / "recipeReport.json").write_text(json.dumps(report, indent=2) + "\n")

        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--edition", required=True, choices=EDITIONS)
    parser.add_argument(
        "--prepare", action="store_true", help="Write native addresses for payload compilation"
    )
    parser.add_argument("--input", type=Path)
    parser.add_argument("--payload", type=Path)
    parser.add_argument("--freeze", action="store_true", help="Freeze the first reviewed build")
    parser.add_argument(
        "--check", action="store_true", help="Require the maintained recipe to match"
    )
    args = parser.parse_args()

    try:
        recipe = Recipe(args.edition)
        if args.prepare:
            require(
                not args.freeze and not args.check and not args.input and not args.payload,
                "--prepare only generates compile-time addresses",
            )
            recipe.prepare()
            print("Prepared " + args.edition + " payload addresses")
        else:
            require(
                args.input and args.payload and not (args.freeze and args.check),
                "Specify input and payload, with at most one of --freeze and --check",
            )
            report = recipe.generate(args.input, args.payload, args.freeze, args.check)
            print(
                json.dumps(
                    {
                        key: report[key]
                        for key in ("edition", "patchedSha256", "patchedSize", "headerSha256")
                    }
                )
            )

        return 0
    except (OSError, ValueError, KeyError) as error:
        print("Error: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
