#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Validate any frozen retail recipe without starting or installing a game."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import tempfile

import generateRecipe as generation
from generateRecipe import Recipe, number, sha, TIMESTAMP
from peImage import PE, NormalizePayload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--edition", choices=("English", "French", "German"), required=True)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--payload", type=Path, required=True)
    args = parser.parse_args()

    recipe = Recipe(args.edition)
    lock_bytes = recipe.lock_path.read_bytes()
    lock = json.loads(lock_bytes)
    checks: list[dict] = []

    def check(name: str, condition: bool) -> None:
        checks.append({"name": name, "passed": bool(condition)})
        if not condition:
            raise AssertionError(name)

    def rejected(name: str, operation, reason: str | None = None) -> None:
        try:
            operation()
        except (OSError, ValueError) as error:
            check(name, reason is None or reason.lower() in str(error).lower())
        else:
            check(name, False)

    paths = (
        recipe.profile_path,
        recipe.header_path,
        recipe.lock_path,
        recipe.output / "gameAddresses.hpp",
        recipe.output / "HELLBEND.fullscreen.EXE",
        recipe.output / "HELLBEND.payload.EXE",
        recipe.output / "recipeReport.json",
        recipe.output / "layoutPayload.normalized.dll",
    )
    preserved = {path: path.read_bytes() for path in paths}
    source = args.input.read_bytes()
    compiled_payload = args.payload.read_bytes()

    original, _ = recipe.canonical(source)
    payload = NormalizePayload(compiled_payload, TIMESTAMP)

    with tempfile.TemporaryDirectory(dir=recipe.output, prefix="validation-") as temporary:
        directory = Path(temporary)
        fixture = Recipe(args.edition)
        fixture.output = directory / "outputs"
        fixture.header_path = directory / "fullscreenPatchData.hpp"
        fixture.lock_path = directory / "recipeLock.json"
        fixture.profile_path = directory / "versionProfile.json"
        fixture.header_path.write_bytes(preserved[recipe.header_path])
        fixture.lock_path.write_bytes(lock_bytes)
        fixture.profile_path.write_bytes(recipe.profile_bytes)
        fixture.prepare()

        source_path = directory / "source.EXE"
        source_path.write_bytes(original)
        payload_path = directory / "layoutPayload.dll"
        payload_path.write_bytes(compiled_payload)

        generated = fixture.generate(source_path, payload_path, check=True)
        result = (fixture.output / "HELLBEND.fullscreen.EXE").read_bytes()
        intermediate = (fixture.output / "HELLBEND.payload.EXE").read_bytes()
        report = generated["transformations"]
        header = fixture.header_path.read_bytes()
        check(
            "generation reproduces frozen images and maintained header",
            result == preserved[recipe.output / "HELLBEND.fullscreen.EXE"]
            and sha(result) == lock["patchedSha256"]
            and len(result) == lock["patchedSize"]
            and sha(intermediate) == lock["intermediateSha256"]
            and header == preserved[recipe.header_path],
        )
        check(
            "both accepted inputs reconstruct the same output",
            generated["bothInputsProduceIdenticalOutput"]
            and generated["recipeReconstructsBothInputs"],
        )
        check(
            "relocation simulation at three load bases",
            report["payload"]["checks"]["relocationSimulationPassed"]
            and len(report["payload"]["checks"]["simulatedLoadBases"]) == 3,
        )

        clone_report = report["payload"]["rendererClone"]
        check(
            "renderer clone is populated before relocation allocation",
            clone_report["bytes"] == 460
            and len(clone_report["gameHighlowRvas"]) == 26
            and clone_report["ordinaryPayloadReferenceRvas"]
            and clone_report["populatedBeforeRelocationAllocation"]
            and report["payload"]["checks"]["rendererCloneGameAddressesRebaseCorrectly"],
        )

        before, after = PE(original), PE(result)
        check(
            "renderer clone remains executable and read-only",
            after.SectionAt(clone_report["rva"], 460).flags & 0xE0000000 == 0x60000000
            and set(clone_report["gameHighlowRvas"]).issubset(after.Relocations())
            and not set(clone_report["gameHighlowRvas"]).intersection(
                clone_report["ordinaryPayloadReferenceRvas"]
            ),
        )
        resources = next(section for section in before.sections if section.name == ".rsrc")
        check(
            "edition resources preserved byte for byte",
            original[resources.raw_offset : resources.raw_offset + resources.raw_size]
            == result[resources.raw_offset : resources.raw_offset + resources.raw_size],
        )
        check(
            "original entry point preserved",
            before.data[before.optional + 16 : before.optional + 20]
            == after.data[after.optional + 16 : after.optional + 20],
        )

        source_hook = next(
            hook for hook in recipe.profile["hooks"] if hook["name"] == "textureSourceHook"
        )
        source_rva = number(source_hook["rva"])
        source_resume = number(recipe.profile["configFields"]["textureSourceResume"])
        config_rva = report["payload"]["configRva"]
        check(
            "texture source hook retains exact five-byte native preimage",
            original[before.Offset(source_rva, 5) : before.Offset(source_rva, 5) + 5]
            == bytes.fromhex("8b44241c50"),
        )
        check(
            "texture source hook is an executable JMP to the exported capture stub",
            result[after.Offset(source_rva)] == 0xE9
            and source_rva
            + 5
            + struct.unpack_from("<i", result, after.Offset(source_rva + 1, 4))[0]
            == report["payload"]["exports"]["textureSourceHook"],
        )
        check(
            "source resume uses the declared config ABI and correct native address",
            lock["patchVersion"] == generation.payloadSchema.PATCH_VERSION
            and lock["payloadVersion"] == generation.payloadSchema.PAYLOAD_VERSION
            and report["payload"]["configSize"] == 104
            and source_resume == source_rva + 5
            and struct.unpack_from("<I", result, after.Offset(config_rva + 100, 4))[0]
            == after.base + source_resume,
        )
        check(
            "source resume is relocated and the native source hook has no surviving relocation",
            config_rva + 100 in after.Relocations()
            and not any(
                fixup < source_rva + 5 and fixup + 4 > source_rva for fixup in after.Relocations()
            ),
        )

        def changed_source_profile(field: str, value):
            changed = json.loads(recipe.profile_bytes)
            if field == "resume":
                changed["configFields"]["textureSourceResume"] = value
            else:
                hook = next(
                    hook for hook in changed["hooks"] if hook["name"] == "textureSourceHook"
                )
                hook[field] = value

            generation.payloadSchema.validate_profile(changed)

        rejected(
            "source resume inside displaced instructions rejected",
            lambda: changed_source_profile("resume", source_rva + 4),
            "resume",
        )
        rejected(
            "source hook wrong native preimage rejected",
            lambda: changed_source_profile("original", "8b44242050"),
            "preimage",
        )
        rejected(
            "source hook call instead of jump rejected",
            lambda: changed_source_profile("opcode", 0xE8),
            "opcode",
        )

        pe = PE(payload)
        text_section = next(section for section in pe.sections if section.name == ".text")
        code = payload[text_section.raw_offset : text_section.raw_offset + text_section.raw_size]
        exports = pe.Exports()
        check(
            "shell state and callbacks are included in the embedded contract",
            report["payload"]["shellStateSize"] == 96
            and all(
                name in report["payload"]["exports"]
                for name in ("shellState", "syncShellFullscreen", "clearShellFullscreen")
            ),
        )

        def export_entry(name: str) -> tuple[int, int]:
            directory = pe.Offset(pe.Directory(0)[0], 40)
            _, count, functions, names, ordinals = struct.unpack_from(
                "<IIIII", payload, directory + 20
            )

            for index in range(count):
                name_rva = struct.unpack_from("<I", payload, pe.Offset(names) + index * 4)[0]
                if pe.CString(name_rva) == name:
                    ordinal = struct.unpack_from("<H", payload, pe.Offset(ordinals) + index * 2)[0]
                    return pe.Offset(name_rva), pe.Offset(functions) + ordinal * 4

            raise AssertionError("Missing export fixture: " + name)

        for name in (
            "shellState",
            "syncShellFullscreen",
            "clearShellFullscreen",
            "textureSourceHook",
            "textureLifecycleState",
            "textureTransferStats",
            "textureTransferState",
            "captureTextureSource",
            "transferTexture",
            "guardedRendererImage",
            "ChildCreateDevice",
        ):
            missing = bytearray(payload)
            name_at, _ = export_entry(name)
            missing[name_at] = ord("X")
            rejected(
                name + " export is required",
                lambda missing=missing: recipe.embed.create_image(original, bytes(missing)),
                "Missing undecorated payload export",
            )

        nonzero_clone = bytearray(payload)
        nonzero_clone[pe.Offset(exports["guardedRendererImage"], 460)] = 0x90
        rejected(
            "compiled renderer clone must remain a zero placeholder",
            lambda: recipe.embed.create_image(original, bytes(nonzero_clone)),
            "zero executable read-only placeholder",
        )

        for name, target, reason in (
            ("nonzero shell state rejected", "config", "initially"),
            ("overlapping shell state rejected", "state", "overlap"),
        ):
            changed = bytearray(payload)
            _, function_at = export_entry("shellState")
            struct.pack_into("<I", changed, function_at, exports[target])
            rejected(
                name,
                lambda changed=changed: recipe.embed.create_image(original, bytes(changed)),
                reason,
            )

        noncode_callback = bytearray(payload)
        _, function_at = export_entry("syncShellFullscreen")
        struct.pack_into("<I", noncode_callback, function_at, exports["shellState"])
        rejected(
            "non-executable shell callback rejected",
            lambda: recipe.embed.create_image(original, bytes(noncode_callback)),
            "executable",
        )

        check(
            "texture ownership state retains initialization across load bases",
            report["payload"]["checks"]["textureOwnershipDataPreservedAcrossLoadBases"]
            and {
                name: region["size"]
                for name, region in report["payload"]["textureOwnershipData"].items()
            }
            == {
                "textureLifecycleState": 20,
                "textureTransferStats": 128,
                "textureTransferState": 8980,
            },
        )

        changed_stats = bytearray(payload)
        changed_stats[pe.Offset(exports["textureTransferStats"], 12)] ^= 1
        rejected(
            "modified texture transfer state initializer rejected",
            lambda: recipe.embed.create_image(original, bytes(changed_stats)),
            "initialization",
        )

        overlapping_transfer = bytearray(payload)
        _, transfer_at = export_entry("textureLifecycleState")
        struct.pack_into("<I", overlapping_transfer, transfer_at, exports["textureTransferState"])
        rejected(
            "overlapping texture ownership regions rejected",
            lambda: recipe.embed.create_image(original, bytes(overlapping_transfer)),
            "overlap",
        )

        mission_stack = number(recipe.profile["payloadLayout"]["missionStack"])
        mission = b"\x81\xec" + struct.pack("<I", mission_stack)
        mission += b"\xff\x25" + struct.pack("<I", pe.base + exports["config"] + 12)
        check("compiled mission trampoline uses edition stack allocation", code.count(mission) == 1)

        rejected(
            "modified retail input rejected",
            lambda: recipe.canonical(original[:-1] + bytes((original[-1] ^ 1,))),
        )
        rejected(
            "incomplete embedded image rejected as source", lambda: recipe.canonical(intermediate)
        )
        rejected(
            "complete patched image rejected as generator source", lambda: recipe.canonical(result)
        )

        alternatives = [
            Recipe(edition)
            for edition in ("English", "French", "German")
            if edition != args.edition
        ]

        for alternate in alternatives:
            rejected(
                f"{args.edition} retail input rejected by {alternate.edition} profile",
                lambda alternate=alternate: alternate.canonical(original),
            )

        def corrupt_payload(offset: int) -> bytes:
            changed = bytearray(payload)
            changed[offset] ^= 1
            return bytes(changed)

        stack_alternate = next(
            alternate
            for alternate in alternatives
            if number(alternate.profile["payloadLayout"]["missionStack"]) != mission_stack
        )
        wrong_stack = bytearray(payload)
        mission_at = text_section.raw_offset + code.index(mission)
        struct.pack_into(
            "<I",
            wrong_stack,
            mission_at + 2,
            number(stack_alternate.profile["payloadLayout"]["missionStack"]),
        )
        rejected(
            f"{stack_alternate.edition} mission stack rejected for {args.edition}",
            lambda: recipe.verify_compiled_profile(bytes(wrong_stack)),
            "mission trampoline",
        )

        bridge_alternate = next(
            alternate
            for alternate in alternatives
            if number(alternate.profile["gameRvas"]["deviceIid"])
            != number(recipe.profile["gameRvas"]["deviceIid"])
        )
        wrong_bridge = bytearray(payload)
        bridge_at = pe.Offset(exports["createDeviceHook"], 12)
        struct.pack_into(
            "<I",
            wrong_bridge,
            bridge_at + 8,
            number(bridge_alternate.profile["gameRvas"]["deviceIid"]),
        )
        rejected(
            f"{bridge_alternate.edition} device address rejected for {args.edition}",
            lambda: recipe.verify_compiled_profile(bytes(wrong_bridge)),
            "native addresses",
        )

        fixture_paths = (
            fixture.profile_path,
            fixture.header_path,
            fixture.lock_path,
            fixture.output / "gameAddresses.hpp",
            fixture.output / "HELLBEND.fullscreen.EXE",
            fixture.output / "HELLBEND.payload.EXE",
            fixture.output / "recipeReport.json",
            fixture.output / "layoutPayload.normalized.dll",
        )
        fixture_preserved = {path: path.read_bytes() for path in fixture_paths}

        bad_payload = directory / "modifiedPayload.dll"
        bad_payload.write_bytes(corrupt_payload(text_section.raw_offset + 32))
        rejected(
            "modified payload rejected by frozen hash",
            lambda: fixture.generate(source_path, bad_payload, check=True),
            "frozen",
        )
        rejected(
            "input/output alias rejected",
            lambda: fixture.generate(fixture.output / "HELLBEND.fullscreen.EXE", payload_path),
            "alias",
        )
        rejected(
            "normalized payload output alias rejected",
            lambda: fixture.generate(source_path, fixture.output / "layoutPayload.normalized.dll"),
            "alias",
        )

        original_header_path = fixture.header_path
        fixture.header_path = source_path
        rejected(
            "same path output rejected",
            lambda: fixture.generate(source_path, payload_path),
            "alias",
        )

        hard_link = directory / "hardLinkedSource.EXE"
        hard_link.hardlink_to(source_path)
        fixture.header_path = hard_link
        rejected(
            "hard-linked output rejected",
            lambda: fixture.generate(source_path, payload_path),
            "alias",
        )

        symbolic_link = directory / "symbolicSource.EXE"

        try:
            symbolic_link.symlink_to(source_path)
        except OSError:
            checks.append(
                {
                    "name": "symbolic output alias",
                    "passed": True,
                    "note": "Resolved-path checks remain active. Creating symlinks is unavailable on this host.",
                }
            )
        else:
            fixture.header_path = symbolic_link
            rejected(
                "symbolic output rejected",
                lambda: fixture.generate(source_path, payload_path),
                "alias",
            )

        fixture.header_path = original_header_path
        check(
            "rejected generation leaves temporary recipes and outputs unchanged",
            all(path.read_bytes() == data for path, data in fixture_preserved.items()),
        )

        address_path = fixture.output / "gameAddresses.hpp"
        address_path.write_bytes(bridge_alternate.addresses())
        rejected(
            "another edition compile-time address header rejected",
            lambda: fixture.generate(source_path, payload_path, check=True),
            "addresses",
        )
        address_path.write_bytes(fixture_preserved[address_path])

        fixture.profile_path.write_bytes(alternatives[0].profile_bytes)
        rejected(
            "profile replaced after loading rejected",
            lambda: fixture.generate(source_path, payload_path, check=True),
            "profile",
        )
        fixture.profile_path.write_bytes(recipe.profile_bytes)

        for key, value, reason, name in (
            ("edition", alternatives[0].edition, "edition", "another edition lock rejected"),
            (
                "profileSha256",
                sha(alternatives[0].profile_bytes),
                "profile",
                "another edition profile hash rejected",
            ),
            (
                "patchVersion",
                lock["patchVersion"] + 1,
                "version",
                "unsupported patch version rejected",
            ),
            (
                "payloadVersion",
                lock["payloadVersion"] + 1,
                "version",
                "unsupported payload version rejected",
            ),
        ):
            changed_lock = {**lock, key: value}
            fixture.lock_path.write_text(json.dumps(changed_lock), encoding="utf-8")
            rejected(name, lambda: fixture.generate(source_path, payload_path, check=True), reason)
            fixture.lock_path.write_bytes(lock_bytes)

        check(
            "profile and address rejection leaves outputs unchanged",
            all(path.read_bytes() == data for path, data in fixture_preserved.items()),
        )
        check(
            "temporary source and payload files preserved",
            source_path.read_bytes() == original and payload_path.read_bytes() == compiled_payload,
        )

    check(
        "maintained profiles recipes and outputs unchanged",
        all(path.read_bytes() == data for path, data in preserved.items()),
    )
    check(
        "input game and compiled payload files preserved",
        args.input.read_bytes() == source and args.payload.read_bytes() == compiled_payload,
    )

    source_paths = (*generation.SOURCES, "scripts/games/hellbender/validateRecipe.py")
    validation = {
        "format": "HellbenderRetailRecipeValidation",
        "edition": args.edition,
        "passed": len(checks),
        "failed": 0,
        "checks": checks,
        "inputSha256": sha(source),
        "profileSha256": sha(recipe.profile_bytes),
        "payloadSha256": sha(payload),
        "patchedSha256": sha(result),
        "headerSha256": sha(header),
        "sourceHashes": {name: sha((generation.ROOT / name).read_bytes()) for name in source_paths},
        "gameStarted": False,
        "gameInstalled": False,
        "gameplayValidated": False,
    }
    (recipe.output / "recipeValidation.json").write_text(
        json.dumps(validation, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps({"edition": args.edition, "passed": len(checks), "failed": 0}))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
