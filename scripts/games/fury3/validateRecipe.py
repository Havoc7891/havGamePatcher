#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Validate the frozen Fury3 recipe without starting or installing a game."""

from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import tempfile

from generateRecipe import Recipe, Number, Sha, peImage, TIMESTAMP


def Main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--edition", choices=("English",), default="English")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--payload", type=Path, required=True)
    args = parser.parse_args()

    recipe = Recipe(args.edition)
    lockBytes = recipe.lock_path.read_bytes()
    lock = json.loads(lockBytes)
    checks: list[dict] = []

    def Check(name: str, condition: bool) -> None:
        checks.append({"name": name, "passed": bool(condition)})
        if not condition:
            raise AssertionError(name)

    def Rejected(name: str, operation, reason: str | None = None) -> None:
        try:
            operation()
        except (OSError, ValueError) as error:
            Check(name, reason is None or reason.lower() in str(error).lower())
        else:
            Check(name, False)

    paths = (
        recipe.profile_path,
        recipe.header_path,
        recipe.lock_path,
        recipe.output / "preparedProfile.json",
        recipe.output / "FURY3.fullscreen.EXE",
        recipe.output / "recipeReport.json",
    )
    preserved = {path: path.read_bytes() for path in paths}
    source = args.input.read_bytes()
    compiledPayload = args.payload.read_bytes()

    original, _ = recipe.Canonical(source)
    payload = peImage.NormalizePayload(compiledPayload, TIMESTAMP)

    with tempfile.TemporaryDirectory(dir=recipe.output, prefix="validation-") as temporary:
        directory = Path(temporary)
        fixture = Recipe(args.edition)
        fixture.output = directory / "outputs"
        fixture.header_path = directory / "fullscreenPatchData.hpp"
        fixture.lock_path = directory / "recipeLock.json"
        fixture.profile_path = directory / "versionProfile.json"
        fixture.header_path.write_bytes(preserved[recipe.header_path])
        fixture.lock_path.write_bytes(lockBytes)
        fixture.profile_path.write_bytes(recipe.profile_bytes)
        fixture.Prepare()

        sourcePath = directory / "source.EXE"
        sourcePath.write_bytes(original)
        payloadPath = directory / "fullscreenPayload.dll"
        payloadPath.write_bytes(compiledPayload)

        generation = fixture.Generate(sourcePath, payloadPath, check=True)
        result = (fixture.output / "FURY3.fullscreen.EXE").read_bytes()
        header = fixture.header_path.read_bytes()
        Check(
            "generation reproduces the frozen image and maintained header",
            result == preserved[recipe.output / "FURY3.fullscreen.EXE"]
            and Sha(result) == lock["patchedSha256"]
            and len(result) == lock["patchedSize"]
            and header == preserved[recipe.header_path],
        )
        Check(
            "both accepted inputs reconstruct the same output",
            generation["bothInputsProduceIdenticalOutput"]
            and generation["recipeReconstructsBothInputs"],
        )

        before, after = peImage.PE(original), peImage.PE(result)
        resources = next(section for section in before.sections if section.name == ".rsrc")
        resourceRange = slice(resources.raw_offset, resources.raw_offset + resources.raw_size)
        Check(
            "original resources remain byte-identical",
            original[resourceRange] == result[resourceRange],
        )
        Check(
            "original entry point is preserved",
            peImage.U32(original, before.optional + 16) == peImage.U32(result, after.optional + 16),
        )
        Check("original imports are preserved", before.Imports() == after.Imports())
        Check(
            "retired hook relocations are absent",
            not {Number(rva) for rva in recipe.profile["retiredGameRelocations"]}
            & after.Relocations(),
        )
        Check(
            "PE checksum is correct",
            peImage.U32(result, after.checksum_offset)
            == peImage.Checksum(result, after.checksum_offset),
        )

        Rejected(
            "modified retail input rejected",
            lambda: recipe.Canonical(original[:-1] + bytes((original[-1] ^ 1,))),
        )
        Rejected("truncated retail input rejected", lambda: recipe.Canonical(original[:-1]))
        Rejected(
            "already patched image rejected as generator input", lambda: recipe.Canonical(result)
        )

        pe = peImage.PE(payload)
        exports = pe.Exports()
        text = next(section for section in pe.sections if section.name == ".text")

        def CorruptPayload(offset: int) -> bytes:
            changed = bytearray(payload)
            changed[offset] ^= 1
            return bytes(changed)

        Rejected(
            "nonzero initial state rejected",
            lambda: recipe.Build(original, CorruptPayload(pe.Offset(exports["state"]))),
        )
        Rejected(
            "invalid config ABI rejected",
            lambda: recipe.Build(original, CorruptPayload(pe.Offset(exports["config"]))),
        )

        fixturePaths = (
            fixture.profile_path,
            fixture.header_path,
            fixture.lock_path,
            fixture.output / "preparedProfile.json",
            fixture.output / "FURY3.fullscreen.EXE",
            fixture.output / "recipeReport.json",
        )
        fixturePreserved = {path: path.read_bytes() for path in fixturePaths}

        badPayload = directory / "modifiedPayload.dll"
        badPayload.write_bytes(CorruptPayload(text.raw_offset + 32))
        Rejected(
            "modified payload rejected by frozen identity",
            lambda: fixture.Generate(sourcePath, badPayload, check=True),
        )
        Rejected(
            "input/output alias rejected",
            lambda: fixture.Generate(fixture.output / "FURY3.fullscreen.EXE", payloadPath),
            "alias",
        )

        originalHeaderPath = fixture.header_path
        fixture.header_path = sourcePath
        Rejected(
            "same path output rejected", lambda: fixture.Generate(sourcePath, payloadPath), "alias"
        )

        hardLink = directory / "hardLinkedSource.EXE"
        hardLink.hardlink_to(sourcePath)
        fixture.header_path = hardLink
        Rejected(
            "hard-linked output rejected",
            lambda: fixture.Generate(sourcePath, payloadPath),
            "alias",
        )

        symbolicLink = directory / "symbolicSource.EXE"

        try:
            symbolicLink.symlink_to(sourcePath)
        except OSError:
            checks.append(
                {
                    "name": "symbolic output alias",
                    "passed": True,
                    "note": "This host cannot create symlinks. Resolved-path checks remain active.",
                }
            )
        else:
            fixture.header_path = symbolicLink
            Rejected(
                "symbolic output rejected",
                lambda: fixture.Generate(sourcePath, payloadPath),
                "alias",
            )

        fixture.header_path = originalHeaderPath
        Check(
            "failed generation preserves temporary outputs",
            all(path.read_bytes() == data for path, data in fixturePreserved.items()),
        )

        preparedPath = fixture.output / "preparedProfile.json"
        preparedPath.write_bytes(b"{}\n")
        Rejected(
            "stale prepared profile rejected",
            lambda: fixture.Generate(sourcePath, payloadPath, check=True),
        )
        preparedPath.write_bytes(fixturePreserved[preparedPath])
        fixture.profile_path.write_bytes(b"{}\n")
        Rejected(
            "profile replaced after loading rejected",
            lambda: fixture.Generate(sourcePath, payloadPath, check=True),
        )
        fixture.profile_path.write_bytes(recipe.profile_bytes)

        for key, value, name in (
            ("edition", "French", "another edition lock"),
            ("profileSha256", "0" * 64, "different profile hash"),
            ("payloadSha256", "0" * 64, "different payload hash"),
            ("patchedSha256", "0" * 64, "different output hash"),
            ("patchedSize", lock["patchedSize"] + 1, "different output size"),
            ("patchVersion", lock["patchVersion"] + 1, "unsupported patch version"),
            ("payloadVersion", lock["payloadVersion"] + 1, "unsupported payload version"),
        ):
            fixture.lock_path.write_text(json.dumps({**lock, key: value}), encoding="utf-8")
            Rejected(
                name + " rejected", lambda: fixture.Generate(sourcePath, payloadPath, check=True)
            )
            fixture.lock_path.write_bytes(lockBytes)

        for field, value in (("language", "French"), ("gameId", "hellbender"), ("version", 99)):
            changed = copy.deepcopy(recipe.profile)
            changed[field] = value
            fixture.profile = changed
            Rejected(
                "unsupported profile " + field + " rejected",
                lambda: fixture.Build(original, payload),
            )

        for field in (
            "originalPrepareMainMenu",
            "originalLeaveMainMenu",
            "movieOpen",
            "movieWindow",
            "originalResizeMovie",
        ):
            changed = copy.deepcopy(recipe.profile)
            changed["gameRvas"][field] = "0x1000"
            fixture.profile = changed
            Rejected("incorrect " + field + " rejected", lambda: fixture.Build(original, payload))

        for hookName in ("PrepareMainMenu", "LeaveMainMenu", "ResizeMovie"):
            changed = copy.deepcopy(recipe.profile)
            next(hook for hook in changed["hooks"] if hook["name"] == hookName)["kind"] = "call"
            fixture.profile = changed
            Rejected(
                "incorrect " + hookName + " hook ABI rejected",
                lambda: fixture.Build(original, payload),
            )

        fixture.profile = recipe.profile
        Check(
            "profile and lock failures preserve temporary outputs",
            all(path.read_bytes() == data for path, data in fixturePreserved.items()),
        )
        Check(
            "temporary source and payload remain unchanged",
            sourcePath.read_bytes() == original and payloadPath.read_bytes() == compiledPayload,
        )

    Check(
        "maintained profile lock header and generated outputs remain unchanged",
        all(path.read_bytes() == data for path, data in preserved.items()),
    )
    Check(
        "input and compiled payload files remain unchanged",
        args.input.read_bytes() == source and args.payload.read_bytes() == compiledPayload,
    )

    validation = {
        "format": "Fury3RetailRecipeValidation",
        "edition": args.edition,
        "passed": len(checks),
        "failed": 0,
        "checks": checks,
        "inputSha256": Sha(source),
        "profileSha256": Sha(recipe.profile_bytes),
        "payloadSha256": Sha(payload),
        "patchedSha256": Sha(result),
        "headerSha256": Sha(header),
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
    raise SystemExit(Main())
