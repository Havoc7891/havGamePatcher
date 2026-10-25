# SPDX-License-Identifier: MIT

"""Reviewed Fury3 payload ABI and retail-profile validation."""

from peImage import Require

PATCH_VERSION = 1
PAYLOAD_VERSION = 1
PAYLOAD_SHA256 = "370be83da886e13bd63bfaad9d93d8f83d9ba5a9f87f68e273dc5979031d739d"
OUTPUT_SHA256 = "d57bc95a60b77be06d5902dcac46b5903ce41af7bf122531615c2d02123ee616"
OUTPUT_SIZE = 678912
TIMESTAMP = 0x6AD48B91
ORIGINAL_SHA256 = "288ce23095c8e9092bdb665b561f13f58c5b7f4b7c3cdb03f7059ab4149e8438"
JOYSTICK_SHA256 = "3548df0193842904220d90a9cb07732dce8f06e8fdfd9111da50bd4382d0bf5c"

CONFIG_NAMES = ("gameBase", "getProcAddressIatRva", "getModuleHandleAIatRva", "stretchBltIatRva")

HOOK_SCHEMA = (
    ("ToggleFullscreen", 5, "call"),
    ("StretchFrame", 6, "call"),
    ("UpdateMenuCheck", 6, "pointer"),
    ("PollJoystick", 5, "jmp"),
    ("PollJoystick", 5, "jmp"),
    ("PrepareMainMenu", 5, "jmp"),
    ("LeaveMainMenu", 5, "jmp"),
    ("ResizeMovie", 5, "jmp"),
)

GAME_RVAS = {
    "mainWindow": 0x1329CC,
    "legacyFullscreen": 0x132464,
    "joystickRawFallback": 0x13CB4C,
    "joystickButtons": 0xEBCB0,
    "joystickEnabled": 0x13E728,
    "originalJoystickPoll": 0x14C70,
    "originalPrepareMainMenu": 0x4FBF0,
    "originalLeaveMainMenu": 0x4FC90,
    "originalResizeMovie": 0x50050,
    "movieOpen": 0x143048,
    "movieWindow": 0x5FC7C,
}

IMPORT_IDENTITIES = {
    ("kernel32.dll", "GetProcAddress"),
    ("kernel32.dll", "GetModuleHandleA"),
    ("gdi32.dll", "StretchBlt"),
    ("user32.dll", "CheckMenuItem"),
}


def Number(value):
    Require(
        isinstance(value, (int, str)) and not isinstance(value, bool),
        "Expected an integer or hexadecimal string",
    )
    return int(value, 0) if isinstance(value, str) else value


def ValidateProfile(profile):
    Require(
        profile["format"] == "Fury3RetailVersionProfile" and profile["version"] == 1,
        "Unsupported Fury3 profile format",
    )
    Require(
        profile["gameId"] == "fury3" and profile["language"] == "English",
        "Unsupported Fury3 edition",
    )
    Require(profile["patchVersion"] == PATCH_VERSION, "Unsupported Fury3 recipe revision")
    Require(
        profile["originalSha256"] == ORIGINAL_SHA256
        and profile["joystickSha256"] == JOYSTICK_SHA256,
        "Profile does not identify the reviewed retail inputs",
    )
    Require(
        Number(profile["sourceSize"]) == Number(profile["originalSize"]) == 645120,
        "Unexpected retail file size",
    )
    Require(
        Number(profile["preferredBase"]) == 0x400000
        and Number(profile["imageSize"]) == 0x17C000
        and Number(profile["sectionCount"]) == 7,
        "Unexpected retail image layout",
    )

    layout = profile["payloadLayout"]
    Require(
        Number(layout["magic"]) == 0x46534650
        and Number(layout["version"]) == 1
        and Number(layout["configSize"]) == 24
        and Number(layout["stateSize"]) == 132
        and Number(layout["preferredBase"]) == 0x900000
        and layout["linkerVersion"] == [14, 44]
        and Number(layout["normalizedTimestamp"]) == TIMESTAMP,
        "Unexpected payload ABI or toolchain",
    )
    Require(
        profile["reviewedPayloadSha256"] == PAYLOAD_SHA256
        and profile["reviewedOutputSha256"] == OUTPUT_SHA256
        and Number(profile["reviewedOutputSize"]) == OUTPUT_SIZE,
        "Reviewed build identity changed",
    )
    Require(
        {name: Number(value) for name, value in profile["gameRvas"].items()} == GAME_RVAS,
        "Unexpected native address inventory",
    )
    Require(
        all(
            0 < Number(value) <= Number(profile["imageSize"]) - 4
            for value in profile["gameRvas"].values()
        ),
        "Native address lies outside the image",
    )
    Require(Number(profile["fullscreenCommand"]) == 0x9C45, "Unexpected fullscreen menu command")

    fields = profile["configFields"]
    Require(
        set(fields) == set(CONFIG_NAMES)
        and Number(fields["gameBase"]) == Number(profile["preferredBase"]),
        "Unexpected config fields",
    )

    imports = {
        Number(item["rva"]): (item["dll"].lower(), item["name"])
        for item in profile["expectedImports"]
    }
    Require(
        len(imports) == len(profile["expectedImports"]) == 4
        and set(imports.values()) == IMPORT_IDENTITIES,
        "Missing or duplicated native imports",
    )

    for name, identity in zip(
        CONFIG_NAMES[1:],
        (
            ("kernel32.dll", "GetProcAddress"),
            ("kernel32.dll", "GetModuleHandleA"),
            ("gdi32.dll", "StretchBlt"),
        ),
    ):
        Require(
            imports.get(Number(fields[name])) == identity,
            "Config import does not match its declared API",
        )

    hooks = profile["hooks"]
    Require(
        tuple((hook["name"], len(bytes.fromhex(hook["original"])), hook["kind"]) for hook in hooks)
        == HOOK_SCHEMA,
        "Hook inventory does not match the payload ABI",
    )

    ranges = []

    for hook in hooks:
        start = Number(hook["rva"])
        end = start + len(bytes.fromhex(hook["original"]))
        Require(0 < start < end <= Number(profile["imageSize"]), "Hook lies outside the image")
        Require(all(end <= left or start >= right for left, right in ranges), "Overlapping hooks")
        ranges.append((start, end))

    Require(
        len(profile["retiredGameRelocations"]) == 2, "Expected exactly two retired hook relocations"
    )

    joystick = profile["joystick"]
    Require(
        Number(joystick["fileOffset"]) == 0xA9B3
        and joystick["original"] == "c7056824530050030000"
        and joystick["patched"] == "c7056824530000040000",
        "Unexpected joystick preimage or replacement",
    )
