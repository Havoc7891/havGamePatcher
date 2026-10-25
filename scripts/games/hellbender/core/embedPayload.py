#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Embed the maintained Windows x86 payload into an exactly identified retail image.

This module preserves the validated payload ABI and original PE relocation checks.
It performs only in-memory transformations and never installs or executes a game.
"""

from __future__ import annotations

import hashlib
import struct

import rendererClone

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

from payloadSchema import (
    PAYLOAD_VERSION,
    CONFIG_FIELD_NAMES,
    HOOK_SCHEMA,
    FIXED_PATCH_NAMES,
    POINTER_HOOK_NAMES,
    RETIRED_RELOCATION_COUNT,
    IMPORT_IDENTITIES,
    number,
    validate_profile,
)

CONFIG_MAGIC = 0x48474C50
CONFIG_VERSION = PAYLOAD_VERSION
CONFIG_SIZE = 104
STATE_SIZE = 156
TEXTURE_STATE_SIZE = 4160
FOCUS_STATE_SIZE = 64
BRIDGE_STATE_SIZE = 32
LOAD_STATE_SIZE = 64
SHELL_STATE_SIZE = 96
READINESS_STATE_SIZE = 64
FLIP_PROBE_STATE_SIZE = 144
MOVIE_RECOVERY_STATE_SIZE = 128
REPORT_VERSION = 14

TEXTURE_OWNERSHIP_INITIALIZERS = {
    "textureLifecycleState": bytes(20),
    "textureTransferStats": struct.pack("<III", 0x48474354, 1, 128) + bytes(116),
    "textureTransferState": bytes(8980),
}

CALLBACK_EXPORTS = (
    "loadTextureWithRecovery",
    "captureTextureSource",
    "transferTexture",
    "advanceTextureLifecycle",
    "advanceTextureFrameEpoch",
    "advanceTextureFrame",
    "enterMission",
    "afterFrame",
    "beforeTeardown",
    "beforeTransition",
    "leaveMission",
    "beforeVideoOpen",
    "afterVideoCleanup",
    "beforeMainMenuShow",
    "afterMainMenuShow",
    "afterMainMenuHide",
    "afterTransition",
    "afterFullscreenSetup",
    "releaseBackbuffer",
    "afterModeAttempt",
    "prepareStartupFrames",
    "beforeSurfaceUnlock",
    "afterRendererInit",
    "prepareExecuteData",
    "prepareStateExecuteData",
    "finishFlushUnlock",
    "recoverLostTextures",
    "beginFocusTransition",
    "finishFocusTransition",
    "createDeviceBridge",
    "beginSceneBridge",
    "endSceneBridge",
    "syncShellFullscreen",
    "clearShellFullscreen",
    "prepareRenderer",
    "observeFlip",
)

IMAGE_SCN_EXECUTE = 0x20000000
IMAGE_SCN_READ = 0x40000000
IMAGE_SCN_WRITE = 0x80000000

_CONFIGURED = False


def configure(profile):
    """Select one complete retail profile before embedding or checking an image."""

    global _CONFIGURED, SOURCE_HASHES, GAME_BASE, GAME_IMAGE_SIZE, GAME_SECTION_COUNT
    global CONFIG_FIELDS, EXPECTED_IMPORTS, RETIRED_GAME_RELOCATIONS, HOOKS, FIXED_PATCHES
    global POINTER_HOOKS
    global JOYSTICK_OFFSET, JOYSTICK_ORIGINAL, JOYSTICK_PATCHED, F4_BRANCH_RVA, F4_WINDOWED_RVA
    global REPORT_FORMAT, EXPECTED_SOURCE_SIZE, RENDERER_PROFILE

    _CONFIGURED = False
    edition = validate_profile(profile)
    p = profile
    image_size = number(p["imageSize"])
    config = {name: number(p["configFields"][name]) for name in CONFIG_FIELD_NAMES}
    imports = {
        number(item["rva"]): (item["dll"].lower(), item["name"]) for item in p["expectedImports"]
    }
    Require(
        len(imports) == len(p["expectedImports"]) == len(IMPORT_IDENTITIES)
        and set(imports.values()) == set(IMPORT_IDENTITIES),
        "Missing or duplicated native imports",
    )
    Require(all(0 < rva <= image_size - 4 for rva in imports), "Native import lies outside image")

    for field, name in (
        ("getModuleHandleA", "GetModuleHandleA"),
        ("getProcAddress", "GetProcAddress"),
        ("freeLibrary", "FreeLibrary"),
    ):
        Require(
            imports.get(number(p["gameRvas"][field])) == ("kernel32.dll", name),
            "Shell loader address does not match its verified native import: " + field,
        )

    hooks = tuple(
        (h["name"], number(h["rva"]), bytes.fromhex(h["original"]), number(h["opcode"]))
        for h in p["hooks"]
    )
    Require(
        tuple((name, len(original), opcode) for name, _, original, opcode in hooks) == HOOK_SCHEMA,
        "Native hook inventory differs from payload ABI",
    )

    pointers = tuple(
        (item["name"], number(item["rva"]), number(item["originalTargetRva"]))
        for item in p["pointerHooks"]
    )
    Require(
        tuple(name for name, _, _ in pointers) == POINTER_HOOK_NAMES,
        "Native pointer hook inventory differs from payload ABI",
    )

    fixed = tuple(
        (h["name"], number(h["rva"]), bytes.fromhex(h["original"]), bytes.fromhex(h["replacement"]))
        for h in p["fixedPatches"]
    )
    Require(
        tuple(name for name, _, _, _ in fixed) == FIXED_PATCH_NAMES,
        "Native fixed patch inventory differs from payload ABI",
    )

    ranges = []

    for name, rva, original, _ in (*hooks, *fixed):
        Require(
            original and 0 < rva <= image_size - len(original),
            "Invalid native patch range: " + name,
        )
        Require(
            all(rva + len(original) <= start or end <= rva for start, end in ranges),
            "Overlapping native patches",
        )
        ranges.append((rva, rva + len(original)))

    for name, rva, target in pointers:
        Require(
            0 < rva <= image_size - 4 and 0 < target < image_size,
            "Invalid native pointer hook: " + name,
        )
        Require(
            all(rva + 4 <= start or end <= rva for start, end in ranges),
            "Overlapping native pointer hook: " + name,
        )
        ranges.append((rva, rva + 4))

    Require(
        all(len(before) == len(after) for _, _, before, after in fixed),
        "Fixed patch changes instruction length",
    )

    retired = {number(key): number(value) for key, value in p["retiredGameRelocations"].items()}
    Require(
        len(retired) == RETIRED_RELOCATION_COUNT
        and all(0 < rva <= image_size - 4 for rva in retired),
        "Retired native relocation inventory differs",
    )
    Require(
        not {rva for _, rva, _ in pointers}.intersection(retired),
        "Pointer hooks must retain their original relocations",
    )
    Require(
        all(
            any(start <= rva and rva + 4 <= start + len(before) for _, start, before, _ in hooks)
            for rva in retired
        ),
        "Retired relocation must lie inside a replaced native instruction",
    )

    joystick = p["joystick"]
    offset = number(joystick["fileOffset"])
    before, after = bytes.fromhex(joystick["original"]), bytes.fromhex(joystick["patched"])
    Require(
        len(before) == len(after) == 10
        and before != after
        and offset + 10 <= number(p["originalSize"]),
        "Invalid joystick patch",
    )

    f4_rva, f4 = next(
        (rva, after) for name, rva, _, after in fixed if name == "windowedAfterSoftwareFullscreen"
    )
    Require(f4[:2] == b"\x0f\x84" and len(f4) == 6, "Unexpected F4 branch")

    f4_target = f4_rva + 6 + struct.unpack_from("<i", f4, 2)[0]
    Require(0 < f4_target < image_size, "F4 target lies outside image")

    SOURCE_HASHES = {
        p["originalSha256"]: edition + " retail",
        p["joystickSha256"]: edition + " retail with joystick patch",
    }
    GAME_BASE, GAME_IMAGE_SIZE = number(p["preferredBase"]), image_size
    GAME_SECTION_COUNT, EXPECTED_SOURCE_SIZE = number(p["sectionCount"]), number(p["originalSize"])
    CONFIG_FIELDS, EXPECTED_IMPORTS = config, imports
    RETIRED_GAME_RELOCATIONS, HOOKS, FIXED_PATCHES = retired, hooks, fixed
    POINTER_HOOKS = pointers
    JOYSTICK_OFFSET, JOYSTICK_ORIGINAL, JOYSTICK_PATCHED = offset, before, after
    F4_BRANCH_RVA, F4_WINDOWED_RVA = f4_rva, f4_target
    REPORT_FORMAT = "Hellbender" + edition + "FullscreenPayload"
    RENDERER_PROFILE = profile
    _CONFIGURED = True


def require_configured():
    Require(_CONFIGURED, "Configure an explicit retail profile before using the payload helper")


def sha256(data: bytes | bytearray) -> str:
    return hashlib.sha256(data).hexdigest()


def create_image(source: bytes, payload_bytes: bytes) -> tuple[bytes, dict]:
    require_configured()

    source_hash = sha256(source)
    Require(
        source_hash in SOURCE_HASHES and len(source) == EXPECTED_SOURCE_SIZE,
        "Input is not an exactly supported retail executable for the configured edition",
    )

    game, payload = PE(source), PE(payload_bytes)
    Require(
        game.base == GAME_BASE
        and game.count == GAME_SECTION_COUNT
        and game.image_size == GAME_IMAGE_SIZE,
        "Unexpected retail image layout",
    )
    Require(payload.base == 0x900000, "Payload preferred base must be 0x900000")
    Require(U16(payload.data, payload.pe + 22) & 0x2000, "Payload must be a DLL")
    Require(U32(payload.data, payload.optional + 16) == 0, "Payload must have no entry point")

    for index, label in (
        (1, "imports"),
        (4, "certificate"),
        (9, "TLS"),
        (10, "load config"),
        (11, "bound imports"),
        (12, "IAT"),
        (13, "delay imports"),
        (14, "CLR"),
    ):
        Require(payload.Directory(index) == (0, 0), f"Payload must not contain {label}")

    Require(game.Directory(4) == (0, 0), "Signed game images are unsupported")

    game_imports = game.Imports()

    for rva, expected in EXPECTED_IMPORTS.items():
        Require(game_imports.get(rva) == expected, f"Unexpected game import at RVA 0x{rva:x}")

    exports = payload.Exports()

    required_exports = {
        "config",
        "state",
        "textureState",
        "focusState",
        "bridgeState",
        "loadState",
        "shellState",
        "readinessState",
        "flipProbeState",
        "movieRecoveryState",
        "softwareDrawHook",
        *TEXTURE_OWNERSHIP_INITIALIZERS,
        *rendererClone.CLONE_EXPORTS,
        *CALLBACK_EXPORTS,
        *(hook[0] for hook in HOOKS),
        *(hook[0] for hook in POINTER_HOOKS),
    }
    for name in required_exports:
        Require(name in exports, f"Missing undecorated payload export {name}")

    Require(
        set(exports) == required_exports and len(exports) == 99,
        "Lean payload must expose exactly the 99 operational symbols",
    )

    copied = [section for section in payload.sections if section.name != ".reloc"]
    Require(
        {section.name for section in copied} == {".text", ".data"} and len(copied) == 2,
        "Payload must contain only .text, .data and optional .reloc",
    )
    copied.sort(key=lambda section: section.rva)

    for section in copied:
        expected_access = IMAGE_SCN_READ | (
            IMAGE_SCN_EXECUTE if section.name == ".text" else IMAGE_SCN_WRITE
        )
        Require(
            section.flags & 0xE0000000 == expected_access, "Unexpected payload section permissions"
        )

    Require(
        payload.SectionAt(exports["config"], CONFIG_SIZE).name == ".data",
        "Config must be writable data",
    )

    Require(
        payload.SectionAt(exports["state"], STATE_SIZE).name == ".data",
        "State must be writable data",
    )
    Require(
        not any(payload.Mapped()[exports["state"] : exports["state"] + STATE_SIZE]),
        "Payload state must initially be all zero, including BSS",
    )

    Require(
        payload.SectionAt(exports["textureState"], TEXTURE_STATE_SIZE).name == ".data",
        "Texture recovery state must be writable data",
    )
    Require(
        not any(
            payload.Mapped()[exports["textureState"] : exports["textureState"] + TEXTURE_STATE_SIZE]
        ),
        "Texture recovery state must initially be all zero, including BSS",
    )

    Require(
        payload.SectionAt(exports["focusState"], FOCUS_STATE_SIZE).name == ".data",
        "Focus recovery state must be writable data",
    )
    Require(
        not any(payload.Mapped()[exports["focusState"] : exports["focusState"] + FOCUS_STATE_SIZE]),
        "Focus recovery state must initially be all zero, including BSS",
    )

    Require(
        payload.SectionAt(exports["bridgeState"], BRIDGE_STATE_SIZE).name == ".data",
        "Direct3D bridge state must be writable data",
    )
    Require(
        not any(
            payload.Mapped()[exports["bridgeState"] : exports["bridgeState"] + BRIDGE_STATE_SIZE]
        ),
        "Direct3D bridge state must initially be all zero, including BSS",
    )

    Require(
        payload.SectionAt(exports["loadState"], LOAD_STATE_SIZE).name == ".data",
        "Load state must be writable data",
    )
    Require(
        not any(payload.Mapped()[exports["loadState"] : exports["loadState"] + LOAD_STATE_SIZE]),
        "Load state must initially be zero",
    )

    Require(
        payload.SectionAt(exports["shellState"], SHELL_STATE_SIZE).name == ".data",
        "Shell state must be writable data",
    )
    Require(
        not any(payload.Mapped()[exports["shellState"] : exports["shellState"] + SHELL_STATE_SIZE]),
        "Shell state must initially be all zero, including BSS",
    )

    Require(
        payload.SectionAt(exports["readinessState"], READINESS_STATE_SIZE).name == ".data",
        "Readiness state must be writable data",
    )
    Require(
        not any(
            payload.Mapped()[
                exports["readinessState"] : exports["readinessState"] + READINESS_STATE_SIZE
            ]
        ),
        "Readiness state must initially be zero",
    )

    Require(
        payload.SectionAt(exports["flipProbeState"], FLIP_PROBE_STATE_SIZE).name == ".data",
        "Flip probe state must be writable data",
    )
    Require(
        not any(
            payload.Mapped()[
                exports["flipProbeState"] : exports["flipProbeState"] + FLIP_PROBE_STATE_SIZE
            ]
        ),
        "Flip probe state must initially be zero",
    )

    Require(
        payload.SectionAt(exports["movieRecoveryState"], MOVIE_RECOVERY_STATE_SIZE).name == ".data",
        "Movie recovery state must be writable data",
    )
    Require(
        not any(
            payload.Mapped()[
                exports["movieRecoveryState"] : exports["movieRecoveryState"]
                + MOVIE_RECOVERY_STATE_SIZE
            ]
        ),
        "Movie recovery state must initially be zero",
    )

    data_regions = [
        (exports["movieRecoveryState"], MOVIE_RECOVERY_STATE_SIZE),
        (exports["readinessState"], READINESS_STATE_SIZE),
        (exports["flipProbeState"], FLIP_PROBE_STATE_SIZE),
        (exports["loadState"], LOAD_STATE_SIZE),
        (exports["config"], CONFIG_SIZE),
        (exports["state"], STATE_SIZE),
        (exports["textureState"], TEXTURE_STATE_SIZE),
        (exports["focusState"], FOCUS_STATE_SIZE),
        (exports["bridgeState"], BRIDGE_STATE_SIZE),
        (exports["shellState"], SHELL_STATE_SIZE),
    ]

    for name, initial in TEXTURE_OWNERSHIP_INITIALIZERS.items():
        Require(
            payload.SectionAt(exports[name], len(initial)).name == ".data",
            f"{name} must be writable data",
        )
        Require(
            payload.Mapped()[exports[name] : exports[name] + len(initial)] == initial,
            f"{name} initialization differs from the ownership ABI",
        )
        data_regions.append((exports[name], len(initial)))

    for index, (start, size) in enumerate(data_regions):
        Require(
            all(
                start + size <= other or other + other_size <= start
                for other, other_size in data_regions[index + 1 :]
            ),
            "Payload data regions overlap",
        )

    for rva in exports.values():
        Require(
            payload.SectionAt(rva) in copied, "An export points outside the copied payload sections"
        )

    clone_rva = exports["guardedRendererImage"]
    clone_bytes = rendererClone.CLONE_BYTES
    Require(
        payload.SectionAt(clone_rva, clone_bytes).name == ".text"
        and payload.SectionAt(clone_rva, clone_bytes).flags & 0xE0000000 == 0x60000000
        and payload.data[
            payload.Offset(clone_rva, clone_bytes) : payload.Offset(clone_rva, clone_bytes)
            + clone_bytes
        ]
        == bytes(clone_bytes),
        "Renderer clone requires a zero executable read-only placeholder",
    )
    Require(
        all(
            name == "guardedRendererImage" or not clone_rva <= rva < clone_rva + clone_bytes
            for name, rva in exports.items()
        ),
        "Renderer clone placeholder overlaps another export",
    )

    config_at = payload.Offset(exports["config"], CONFIG_SIZE)
    Require(
        struct.unpack_from(f"<{CONFIG_SIZE // 4}I", payload.data, config_at)
        == (CONFIG_MAGIC, CONFIG_VERSION, *(0 for _ in CONFIG_FIELDS)),
        "Unexpected payload config ABI or nonzero address fields",
    )

    for name in (
        *CALLBACK_EXPORTS,
        "ChildCreateDevice",
        "softwareDrawHook",
        *(hook[0] for hook in HOOKS),
        *(hook[0] for hook in POINTER_HOOKS),
    ):
        Require(payload.SectionAt(exports[name]).name == ".text", "Hook must be executable code")

    original_relocations = game.Relocations()

    for name, rva, target in POINTER_HOOKS:
        Require(rva in original_relocations, "Native pointer hook lacks a relocation: " + name)
        Require(
            U32(source, game.Offset(rva, 4)) == game.base + target,
            "Native pointer hook signature differs: " + name,
        )
        Require(
            game.SectionAt(target).flags & IMAGE_SCN_EXECUTE,
            "Native pointer hook target is not executable: " + name,
        )

    for rva, expected in RETIRED_GAME_RELOCATIONS.items():
        Require(
            rva in original_relocations and U32(source, game.Offset(rva, 4)) == expected,
            "Unexpected original instruction relocation",
        )

    retained_game_relocations = original_relocations - RETIRED_GAME_RELOCATIONS.keys()
    payload_relocations = payload.Relocations()
    Require(payload_relocations, "Payload needs HIGHLOW relocations")
    Require(
        not any(clone_rva < at + 4 and at < clone_rva + clone_bytes for at in payload_relocations),
        "Renderer clone placeholder contains a compiled relocation",
    )
    clone_references = {
        at
        for at in payload_relocations
        if U32(payload.data, payload.Offset(at, 4)) == payload.base + clone_rva
    }
    Require(clone_references, "Payload has no ordinary relocated reference to its renderer clone")
    native_clone = rendererClone.ReadNative(RENDERER_PROFILE, game)
    Require(
        not any(
            rva < exports["movieRecoveryState"] + MOVIE_RECOVERY_STATE_SIZE
            and rva + 4 > exports["movieRecoveryState"]
            for rva in payload_relocations
        ),
        "A relocation overlaps the zero-initialized movie recovery state",
    )
    Require(
        not any(
            rva < exports["flipProbeState"] + FLIP_PROBE_STATE_SIZE
            and rva + 4 > exports["flipProbeState"]
            for rva in payload_relocations
        ),
        "A relocation overlaps the zero-initialized flip probe state",
    )
    Require(
        not any(
            rva < exports["readinessState"] + READINESS_STATE_SIZE
            and rva + 4 > exports["readinessState"]
            for rva in payload_relocations
        ),
        "A relocation overlaps the zero-initialized readiness state",
    )

    Require(
        not any(
            rva < exports["shellState"] + SHELL_STATE_SIZE and rva + 4 > exports["shellState"]
            for rva in payload_relocations
        ),
        "A relocation overlaps the zero-initialized shell state",
    )

    for name, initial in TEXTURE_OWNERSHIP_INITIALIZERS.items():
        Require(
            not any(
                rva < exports[name] + len(initial) and rva + 4 > exports[name]
                for rva in payload_relocations
            ),
            f"A relocation overlaps the initial {name} state",
        )

    for rva in payload_relocations:
        Require(
            payload.SectionAt(rva, 4) in copied, "Payload relocation targets an omitted section"
        )

        value = U32(payload.data, payload.Offset(rva, 4))
        Require(
            payload.base <= value < payload.base + payload.image_size,
            "Payload absolute pointer is not internal to its image",
        )
        Require(
            payload.SectionAt(value - payload.base) in copied,
            "Payload pointer refers to an omitted section",
        )

    for _, rva, original, _ in HOOKS:
        Require(
            source[
                game.Offset(rva, len(original)) : game.Offset(rva, len(original)) + len(original)
            ]
            == original,
            f"Hook signature mismatch at RVA 0x{rva:x}",
        )
        Require(
            not any(
                fixup < rva + len(original) and fixup + 4 > rva
                for fixup in retained_game_relocations
            ),
            "An unretired existing relocation overlaps a hook",
        )

    for name, rva, original, replacement in FIXED_PATCHES:
        Require(len(original) == len(replacement), f"Patch changes instruction length: {name}")

        at = game.Offset(rva, len(original))
        Require(source[at : at + len(original)] == original, f"Patch signature mismatch: {name}")
        Require(
            not any(
                fixup < rva + len(original) and fixup + 4 > rva for fixup in original_relocations
            ),
            f"An existing relocation overlaps patch: {name}",
        )

    Require(
        source[JOYSTICK_OFFSET : JOYSTICK_OFFSET + 10] in (JOYSTICK_ORIGINAL, JOYSTICK_PATCHED),
        "Joystick compatibility signature mismatch",
    )

    next_rva = Align(
        max(section.rva + section.mapped_size for section in game.sections), game.section_alignment
    )
    origin = next_rva - copied[0].rva
    delta = game.base + origin - payload.base
    image = bytearray(source)
    sections = []

    for index, section in enumerate(copied):
        raw_offset = Align(len(image), game.file_alignment)
        image.extend(b"\0" * (raw_offset - len(image)))
        content = payload.data[section.raw_offset : section.raw_offset + section.raw_size]
        image.extend(content)
        sections.append(
            Section(
                ".hglpt" if section.name == ".text" else ".hglpd",
                section.virtual_size,
                origin + section.rva,
                section.raw_size,
                raw_offset if section.raw_size else 0,
                section.flags,
            )
        )

    def embedded_offset(rva: int, length: int = 4) -> int:
        section = payload.SectionAt(rva, length)
        Require(
            section in copied and rva + length <= section.rva + section.raw_size,
            "Embedded location lacks file-backed data",
        )
        return sections[copied.index(section)].raw_offset + rva - section.rva

    for rva in payload_relocations:
        at = embedded_offset(rva)
        Put32(image, at, (U32(image, at) + delta) & 0xFFFFFFFF)

    config_rva = origin + exports["config"]
    config_fixups = {config_rva + offset for offset in range(8, CONFIG_SIZE, 4)}
    Require(
        not config_fixups.intersection({origin + rva for rva in payload_relocations}),
        "Config address slots already have payload relocations",
    )

    for index, value in enumerate(CONFIG_FIELDS.values()):
        Put32(image, embedded_offset(exports["config"]) + 8 + index * 4, game.base + value)

    clone_code = rendererClone.Construct(
        native_clone, game.base, origin + clone_rva, origin + exports["ChildCreateDevice"]
    )
    clone_at = embedded_offset(clone_rva, clone_bytes)
    image[clone_at : clone_at + clone_bytes] = clone_code
    clone_fixups = {
        origin + clone_rva + row["cloneOffset"] for row in native_clone["absoluteRelocations"]
    }
    Require(
        len(clone_fixups) == 26
        and not clone_fixups.intersection(
            retained_game_relocations | {origin + at for at in payload_relocations} | config_fixups
        ),
        "Renderer game-address fixups overlap an existing relocation category",
    )
    merged_relocations = (
        retained_game_relocations
        | {origin + rva for rva in payload_relocations}
        | config_fixups
        | clone_fixups
    )
    relocation_data = EncodeRelocations(merged_relocations)
    relocation_rva = Align(
        max(section.rva + section.mapped_size for section in sections), game.section_alignment
    )
    relocation_raw = Align(len(image), game.file_alignment)
    relocation_size = Align(len(relocation_data), game.file_alignment)
    image.extend(b"\0" * (relocation_raw - len(image)))
    image.extend(relocation_data)
    image.extend(b"\0" * (relocation_size - len(relocation_data)))

    sections.append(
        Section(
            ".hglpr",
            len(relocation_data),
            relocation_rva,
            relocation_size,
            relocation_raw,
            0x42000040,
        )
    )

    first_header = game.section_table + game.count * 40
    end_header = first_header + len(sections) * 40
    Require(end_header <= game.header_size, "Not enough section-header slack")
    Require(not any(source[first_header:end_header]), "Section-header slack is not empty")

    for index, section in enumerate(sections):
        at = first_header + index * 40
        struct.pack_into(
            "<8sIIIIIIHHI",
            image,
            at,
            section.name.encode("ascii"),
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

    for offset, section_flag in ((4, 0x20), (8, 0x40)):
        added = sum(section.raw_size for section in sections if section.flags & section_flag)
        Put32(image, game.optional + offset, U32(source, game.optional + offset) + added)

    added_uninitialized = sum(section.virtual_size for section in sections if section.flags & 0x80)
    Put32(image, game.optional + 12, U32(source, game.optional + 12) + added_uninitialized)
    Put32(
        image,
        game.optional + 56,
        Align(relocation_rva + len(relocation_data), game.section_alignment),
    )
    struct.pack_into("<II", image, game.optional + 96 + 5 * 8, relocation_rva, len(relocation_data))

    hook_report = []

    pointer_report = []

    for name, rva, target in POINTER_HOOKS:
        replacement = game.base + origin + exports[name]
        Put32(image, game.Offset(rva, 4), replacement)
        pointer_report.append(
            {
                "name": name,
                "rva": rva,
                "originalTargetRva": target,
                "targetRva": origin + exports[name],
                "relocationRetained": True,
            }
        )

    for name, rva, original, opcode in HOOKS:
        target = origin + exports[name]
        replacement = Branch(rva, target, len(original), opcode)
        at = game.Offset(rva, len(original))
        image[at : at + len(original)] = replacement
        hook_report.append(
            {
                "name": name,
                "rva": rva,
                "targetRva": target,
                "original": original.hex(),
                "replacement": replacement.hex(),
            }
        )

    image[JOYSTICK_OFFSET : JOYSTICK_OFFSET + 10] = JOYSTICK_PATCHED

    fixed_report = []

    for name, rva, original, replacement in FIXED_PATCHES:
        at = game.Offset(rva, len(original))
        image[at : at + len(original)] = replacement
        fixed_report.append(
            {"name": name, "rva": rva, "original": original.hex(), "replacement": replacement.hex()}
        )

    Put32(image, game.checksum_offset, Checksum(image, game.checksum_offset))

    result = bytes(image)
    checks = validate_image(
        game,
        payload,
        result,
        origin,
        sections,
        original_relocations,
        payload_relocations,
        merged_relocations,
        exports,
        native_clone,
    )

    report = {
        "format": REPORT_FORMAT,
        "version": REPORT_VERSION,
        "sourceKind": SOURCE_HASHES[source_hash],
        "sourceSha256": source_hash,
        "sourceSize": len(source),
        "payloadSha256": sha256(payload_bytes),
        "outputSha256": sha256(result),
        "outputSize": len(result),
        "payloadPreferredBase": payload.base,
        "payloadEmbeddingRvaOrigin": origin,
        "embeddedPayloadBase": game.base + origin,
        "sections": [section.Describe() for section in sections],
        "hooks": hook_report,
        "pointerHooks": pointer_report,
        "fixedPatches": fixed_report,
        "configRva": config_rva,
        "configSize": CONFIG_SIZE,
        "stateSize": STATE_SIZE,
        "textureStateSize": TEXTURE_STATE_SIZE,
        "focusStateSize": FOCUS_STATE_SIZE,
        "bridgeStateSize": BRIDGE_STATE_SIZE,
        "loadStateSize": LOAD_STATE_SIZE,
        "shellStateSize": SHELL_STATE_SIZE,
        "readinessStateSize": READINESS_STATE_SIZE,
        "flipProbeStateSize": FLIP_PROBE_STATE_SIZE,
        "movieRecoveryStateSize": MOVIE_RECOVERY_STATE_SIZE,
        "textureOwnershipData": {
            name: {
                "rva": origin + exports[name],
                "size": len(initial),
                "initialSha256": sha256(initial),
            }
            for name, initial in TEXTURE_OWNERSHIP_INITIALIZERS.items()
        },
        "config": {name: game.base + value for name, value in CONFIG_FIELDS.items()},
        "exports": {name: origin + rva for name, rva in exports.items()},
        "rendererClone": {
            "rva": origin + clone_rva,
            "bytes": clone_bytes,
            "sha256": sha256(clone_code),
            "helperRva": origin + exports["ChildCreateDevice"],
            "gameHighlowRvas": sorted(clone_fixups),
            "ordinaryPayloadReferenceRvas": sorted(origin + at for at in clone_references),
            "populatedBeforeRelocationAllocation": True,
        },
        "verifiedGameImports": [
            {"rva": rva, "dll": dll, "name": name} for rva, (dll, name) in EXPECTED_IMPORTS.items()
        ],
        "relocations": {
            "original": len(original_relocations),
            "retiredOriginal": sorted(RETIRED_GAME_RELOCATIONS),
            "retainedOriginal": len(retained_game_relocations),
            "payload": len(payload_relocations),
            "config": len(config_fixups),
            "rendererCloneGameAddresses": len(clone_fixups),
            "combined": len(merged_relocations),
        },
        "checks": checks,
    }

    return result, report


def validate_image(
    game: PE,
    payload: PE,
    result: bytes,
    origin: int,
    appended: list[Section],
    original_relocations: set[int],
    payload_relocations: set[int],
    merged_relocations: set[int],
    exports: dict[str, int],
    native_clone: dict,
) -> dict:
    require_configured()

    output = PE(result)
    Require(output.sections == game.sections + appended, "Section metadata changed unexpectedly")
    Require(output.Relocations() == merged_relocations, "Combined relocation table mismatch")

    retained_game_relocations = original_relocations - RETIRED_GAME_RELOCATIONS.keys()
    expected_clone_fixups = {
        origin + exports["guardedRendererImage"] + row["cloneOffset"]
        for row in native_clone["absoluteRelocations"]
    }
    Require(
        merged_relocations
        == retained_game_relocations
        | {origin + rva for rva in payload_relocations}
        | {origin + exports["config"] + offset for offset in range(8, CONFIG_SIZE, 4)}
        | expected_clone_fixups,
        "Relocation inventory differs from original, payload, config and clone categories",
    )
    Require(
        retained_game_relocations.issubset(output.Relocations()),
        "Unlisted original relocations were lost",
    )
    Require(
        not RETIRED_GAME_RELOCATIONS.keys() & output.Relocations(),
        "Replaced native instructions must not retain their original relocations",
    )
    Require(
        U32(result, output.checksum_offset) == Checksum(result, output.checksum_offset),
        "Checksum mismatch",
    )

    original_overlay = max(section.raw_offset + section.raw_size for section in game.sections)
    Require(
        result[original_overlay : len(game.data)] == game.data[original_overlay:],
        "Original overlay changed",
    )

    for index in range(16):
        if index != 5:
            Require(output.Directory(index) == game.Directory(index), "Original directory changed")

    Require(
        U32(result, output.optional + 16) == U32(game.data, game.optional + 16),
        "Entry point changed",
    )
    Require(
        result[JOYSTICK_OFFSET : JOYSTICK_OFFSET + 10] == JOYSTICK_PATCHED, "Missing joystick patch"
    )

    allowed = set(range(game.pe + 6, game.pe + 8))

    for at, size in (
        (game.optional + 4, 12),
        (game.optional + 56, 4),
        (game.checksum_offset, 4),
        (game.optional + 96 + 5 * 8, 8),
        (game.section_table + game.count * 40, len(appended) * 40),
        (JOYSTICK_OFFSET, 10),
    ):
        allowed.update(range(at, at + size))

    for _, rva, original, _ in HOOKS:
        allowed.update(range(game.Offset(rva), game.Offset(rva) + len(original)))

    for _, rva, _ in POINTER_HOOKS:
        allowed.update(range(game.Offset(rva, 4), game.Offset(rva, 4) + 4))

    for name, rva, original, replacement in FIXED_PATCHES:
        at = game.Offset(rva, len(original))
        allowed.update(range(at, at + len(original)))
        Require(result[at : at + len(original)] == replacement, f"Missing fixed patch: {name}")

    Require(
        all(
            before == after or index in allowed
            for index, (before, after) in enumerate(zip(game.data, result))
        ),
        "Unexpected change to original file bytes",
    )

    debug_rva, debug_size = game.Directory(6)
    if debug_rva:
        debug_offset = game.Offset(debug_rva, debug_size)
        Require(
            result[debug_offset : debug_offset + debug_size]
            == game.data[debug_offset : debug_offset + debug_size],
            "Debug raw-file pointers changed",
        )
        Require(debug_size % 28 == 0, "Invalid debug directory")

        for at in range(debug_offset, debug_offset + debug_size, 28):
            size, _, raw = struct.unpack_from("<III", game.data, at + 16)
            Require(
                raw + size <= len(game.data)
                and result[raw : raw + size] == game.data[raw : raw + size],
                "Debug data changed",
            )

    tested_bases = (GAME_BASE, 0x520000, 0x15000000)
    original_mapped = game.Mapped()

    for load_base in tested_bases:
        mapped = output.Mapped()
        load_delta = load_base - GAME_BASE

        for rva in merged_relocations:
            Put32(mapped, rva, (U32(mapped, rva) + load_delta) & 0xFFFFFFFF)

        for rva in retained_game_relocations:
            pointer = next((item for item in POINTER_HOOKS if item[1] == rva), None)
            if pointer is not None:
                Require(
                    U32(mapped, rva) == load_base + origin + exports[pointer[0]],
                    "Rebased native pointer hook target differs",
                )
                continue
            Require(
                U32(mapped, rva) == (U32(original_mapped, rva) + load_delta) & 0xFFFFFFFF,
                "Original relocation semantics changed",
            )

        for name, size in (
            ("state", STATE_SIZE),
            ("textureState", TEXTURE_STATE_SIZE),
            ("focusState", FOCUS_STATE_SIZE),
            ("bridgeState", BRIDGE_STATE_SIZE),
            ("loadState", LOAD_STATE_SIZE),
            ("shellState", SHELL_STATE_SIZE),
            ("readinessState", READINESS_STATE_SIZE),
            ("flipProbeState", FLIP_PROBE_STATE_SIZE),
            ("movieRecoveryState", MOVIE_RECOVERY_STATE_SIZE),
        ):
            Require(
                not any(mapped[origin + exports[name] : origin + exports[name] + size]),
                f"Transplanted {name} is not zero initialized",
            )

        for name, initial in TEXTURE_OWNERSHIP_INITIALIZERS.items():
            Require(
                mapped[origin + exports[name] : origin + exports[name] + len(initial)] == initial,
                f"Rebased {name} initialization differs from the ownership ABI",
            )

        for index, value in enumerate(CONFIG_FIELDS.values()):
            Require(
                U32(mapped, origin + exports["config"] + 8 + index * 4) == load_base + value,
                "Rebased config pointer mismatch",
            )

        for rva in payload_relocations:
            expected = (
                U32(payload.data, payload.Offset(rva, 4)) - payload.base + load_base + origin
            ) & 0xFFFFFFFF
            Require(U32(mapped, origin + rva) == expected, "Rebased payload pointer mismatch")

        clone_rva = origin + exports["guardedRendererImage"]
        expected_clone = rendererClone.Construct(
            native_clone, load_base, clone_rva, origin + exports["ChildCreateDevice"]
        )
        Require(
            mapped[clone_rva : clone_rva + rendererClone.CLONE_BYTES] == expected_clone,
            "Rebased renderer clone game addresses or relative calls differ",
        )
        expected_payload = payload.Mapped()
        for rva in payload_relocations:
            Put32(
                expected_payload,
                rva,
                (U32(expected_payload, rva) - payload.base + load_base + origin) & 0xFFFFFFFF,
            )
        for index, value in enumerate(CONFIG_FIELDS.values()):
            Put32(expected_payload, exports["config"] + 8 + index * 4, load_base + value)
        at = exports["guardedRendererImage"]
        expected_payload[at : at + rendererClone.CLONE_BYTES] = expected_clone
        for section in payload.sections:
            if section.name in (".text", ".data"):
                start, size = section.rva, section.mapped_size
                Require(
                    mapped[origin + start : origin + start + size]
                    == expected_payload[start : start + size],
                    "Payload bytes changed outside config, relocations or the exact renderer clone",
                )

        for name, rva, original, opcode in HOOKS:
            target = rva + 5 + struct.unpack_from("<i", mapped, rva + 1)[0]
            Require(
                mapped[rva] == opcode and target == origin + exports[name],
                "Hook branch target mismatch",
            )
            Require(
                mapped[rva + 5 : rva + len(original)] == b"\x90" * (len(original) - 5),
                "Hook padding mismatch",
            )

        for name, rva, original, replacement in FIXED_PATCHES:
            Require(
                mapped[rva : rva + len(replacement)] == replacement,
                f"Rebased fixed patch mismatch: {name}",
            )

        Require(
            F4_BRANCH_RVA + 6 + struct.unpack_from("<i", mapped, F4_BRANCH_RVA + 2)[0]
            == F4_WINDOWED_RVA,
            "F4 conditional branch target mismatch",
        )

    return {
        "originalBytesPreservedExceptListedChanges": True,
        "overlayAndDebugPointersPreserved": True,
        "originalDirectoriesPreservedExceptRelocations": True,
        "separateCodeAndWritableData": True,
        "joystickPatchPresent": True,
        "f4TogglePatchesPresent": True,
        "checksumValid": True,
        "simulatedLoadBases": list(tested_bases),
        "relocationSimulationPassed": True,
        "onlyDeclaredInstructionRelocationsRetired": True,
        "nativePointerHooksRebaseCorrectly": True,
        "publicD3DBridgeHooksPresent": True,
        "shellStateZeroAcrossLoadBases": True,
        "readinessStateZeroAcrossLoadBases": True,
        "flipProbeStateZeroAcrossLoadBases": True,
        "movieRecoveryStateZeroAcrossLoadBases": True,
        "shellCallbacksPresent": True,
        "textureOwnershipDataPreservedAcrossLoadBases": True,
        "rendererCloneGameAddressesRebaseCorrectly": True,
        "rendererClonePayloadReferencesRemainOrdinaryRelocations": True,
        "copiedPayloadBytesExactlyPreservedOutsideDeclaredTransformations": True,
    }
