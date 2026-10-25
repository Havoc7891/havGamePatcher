# SPDX-License-Identifier: MIT

"""Language-neutral symbols and ABI inventories for the shared fullscreen payload."""

from __future__ import annotations

import re

from rendererClone import ValidateProfile as ValidateRendererCloneProfile

PATCH_VERSION = 1
PAYLOAD_VERSION = 1

CONFIG_FIELD_NAMES = (
    "gameBase",
    "missionResume",
    "frameOriginal",
    "teardownOriginal",
    "modeResume",
    "videoOpenOriginal",
    "videoCleanupOriginal",
    "mainMenuShowResume",
    "mainMenuHideResume",
    "setupOriginal",
    "releaseResourcesOriginal",
    "unlockOriginal",
    "warmupResume",
    "warmupSuccess",
    "warmupFailure",
    "lockOriginal",
    "clearOriginal",
    "clearPolygonOriginal",
    "unlockResume",
    "rendererInitOriginal",
    "executeDataResume",
    "stateInstructionsResume",
    "stateExecuteDataResume",
    "textureSourceResume",
)

GAME_RVA_NAMES = (
    "mode",
    "directDraw",
    "hardware",
    "width",
    "height",
    "app",
    "view",
    "legacyVideo",
    "fullscreenActive",
    "movieWindow",
    "movieFlipReturn",
    "movieHeader",
    "movieBits",
    "moviePalette",
    "movieDescription",
    "mainMenuVisible",
    "quit",
    "gameStage",
    "software",
    "renderBpp",
    "hardwarePreference",
    "drawObject",
    "legacyDevice",
    "primary",
    "backbuffer",
    "vertexLimit",
    "instructionOffset",
    "executeCapacity",
    "drawLock",
    "drawSession",
    "drawWindow",
    "cockpitFullRedrawCount",
    "getLastError",
    "setLastError",
    "getCurrentThreadId",
    "getWindowThreadProcessId",
    "clientToScreen",
    "setMenu",
    "isChild",
    "isWindow",
    "getSystemMetrics",
    "getParent",
    "getMenu",
    "getWindowRect",
    "getClientRect",
    "setWindowPos",
    "showCursor",
    "getFocus",
    "isWindowVisible",
    "getForegroundWindow",
    "isWindowEnabled",
    "setFocus",
    "isIconic",
    "textureCount",
    "textureHandles",
    "textureSurfaceTable",
    "textureInterfaceTable",
    "textureGpuNames",
    "textureCpuCache",
    "textureSelectedSlot",
    "textureDimension",
    "textureSourceBase",
    "textureSourceX",
    "textureSourceY",
    "textureScratch",
    "textureStagingSurface",
    "textureStagingInterface",
    "textureLodSizes",
    "textureLock",
    "uploadGuard0",
    "uploadGuard1",
    "uploadGuard2",
    "uploadIndex0",
    "uploadIndex1",
    "uploadIndex2",
    "uploadTexture",
    "surfaceLock",
    "fatalError",
    "d3dRoot",
    "deviceIid",
    "deviceCreateResume",
    "getModuleHandleA",
    "getProcAddress",
    "freeLibrary",
)

HOOK_SCHEMA = (
    ("textureLoadHook", 5, 232),
    ("missionHook", 6, 233),
    ("frameHook", 5, 232),
    ("teardownHook", 5, 232),
    ("modeHook", 6, 233),
    ("videoOpenHook", 5, 232),
    ("videoCleanupHook", 5, 232),
    ("mainMenuShowHook", 6, 233),
    ("mainMenuHideHook", 9, 233),
    ("fullscreenSetupHook", 5, 232),
    ("resourceReleaseHook", 5, 232),
    ("resourceReleaseHook", 5, 232),
    ("resourceReleaseHook", 5, 232),
    ("resourceReleaseHook", 5, 232),
    ("startupFramesHook", 6, 233),
    ("clearPolygonHook", 5, 232),
    ("clearPolygonHook", 5, 232),
    ("surfaceUnlockHook", 5, 233),
    ("rendererInitHook", 5, 232),
    ("executeDataHook", 5, 233),
    ("stateInstructionsHook", 9, 233),
    ("stateExecuteDataHook", 5, 233),
    ("flushUnlockHook", 5, 232),
    ("createDeviceHook", 7, 233),
    ("beginSceneBridge", 5, 232),
    ("beginSceneBridge", 5, 232),
    ("endSceneBridge", 5, 232),
    ("endSceneBridge", 5, 232),
    ("backbufferLockHook", 5, 232),
    ("textureSourceHook", 5, 233),
    ("flipProbeHook", 5, 232),
    ("ownedUnlockResultHook", 5, 233),
    ("observeSoftwareBitBlt", 6, 232),
    ("observeSoftwareStretchBlt", 6, 232),
    ("menuBitmapHook", 5, 232),
    ("observeMenuStretchBlt", 6, 232),
    ("endSceneFailureHook", 5, 233),
    ("preMoviePresentHook", 5, 232),
    ("quadFlushReturnHook", 6, 233),
    ("quadHudReturnHook", 5, 233),
    ("quadFrameReturnHook", 5, 233),
    ("presentRecoveryFrame", 5, 232),
    ("quadResetReturnHook", 10, 233),
    ("worldFrameReturnHook", 5, 233),
    ("rawSurfaceLockResultHook", 5, 233),
    ("relockDecisionHook", 9, 233),
    ("relockFrameReturnHook", 5, 233),
    ("failedPresentationHook", 5, 233),
)

FIXED_PATCH_NAMES = (
    "preserveF4Mode",
    "windowedAfterSoftwareFullscreen",
    "stateBufferCapacity",
    "stateBufferZeroCount",
)

POINTER_HOOK_NAMES = (
    "modeAdmissionPreTranslate",
    "softwarePresentHook",
    "menuDrawHook",
    "menuQueueHook",
    "menuResumeHook",
    "menuNewGameHook",
    "menuMultiplayerHook",
)

RETIRED_RELOCATION_COUNT = 12

IMPORT_IDENTITIES = (
    ("kernel32.dll", "GetLastError"),
    ("kernel32.dll", "SetLastError"),
    ("kernel32.dll", "GetCurrentThreadId"),
    ("user32.dll", "GetWindowThreadProcessId"),
    ("user32.dll", "ClientToScreen"),
    ("user32.dll", "SetMenu"),
    ("user32.dll", "IsChild"),
    ("user32.dll", "IsWindow"),
    ("user32.dll", "GetSystemMetrics"),
    ("user32.dll", "GetParent"),
    ("user32.dll", "GetMenu"),
    ("user32.dll", "GetWindowRect"),
    ("user32.dll", "GetClientRect"),
    ("user32.dll", "SetWindowPos"),
    ("user32.dll", "ShowCursor"),
    ("user32.dll", "GetFocus"),
    ("user32.dll", "IsWindowVisible"),
    ("user32.dll", "GetForegroundWindow"),
    ("user32.dll", "IsWindowEnabled"),
    ("user32.dll", "SetFocus"),
    ("user32.dll", "IsIconic"),
    ("kernel32.dll", "GetModuleHandleA"),
    ("kernel32.dll", "GetProcAddress"),
    ("kernel32.dll", "FreeLibrary"),
    ("gdi32.dll", "BitBlt"),
    ("gdi32.dll", "StretchBlt"),
    ("user32.dll", "GetKeyState"),
    ("user32.dll", "InvalidateRect"),
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def number(value):
    require(
        isinstance(value, (int, str)) and not isinstance(value, bool),
        "Profile integer must be an integer or numeric string",
    )

    result = int(value, 0) if isinstance(value, str) else value
    require(0 <= result <= 0xFFFFFFFF, "Profile integer is outside DWORD range")

    return result


def digest(value):
    require(
        isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None,
        "Expected a lowercase SHA-256 digest",
    )
    return value


def validate_profile(profile):
    require(isinstance(profile, dict), "Version profile must be an object")
    require(
        profile["format"] == "HellbenderRetailVersionProfile" and profile["version"] == 1,
        "Unsupported version profile format",
    )
    require(profile["language"] in ("English", "French", "German"), "Unsupported profile edition")

    require(
        digest(profile["originalSha256"]) != digest(profile["joystickSha256"]),
        "Original and joystick identities must differ",
    )

    require(number(profile["preferredBase"]) == 0x400000, "Unexpected retail image base")
    require(number(profile["sectionCount"]) == 6, "Unexpected retail section count")
    require(0 < number(profile["originalSize"]) <= 0xFFFFFFFF - 512, "Invalid original size")
    require(
        number(profile["sourceSize"]) == number(profile["originalSize"]), "Source sizes disagree"
    )

    image_size = number(profile["imageSize"])
    require(image_size > 0 and image_size % 0x1000 == 0, "Invalid retail image size")
    require(
        number(profile["preferredBase"]) + image_size <= 0x100000000,
        "Retail image address overflow",
    )

    require(
        set(profile["configFields"]) == set(CONFIG_FIELD_NAMES),
        "Config field inventory differs from payload ABI",
    )
    require(
        set(profile["gameRvas"]) == set(GAME_RVA_NAMES),
        "Native address inventory differs from payload ABI",
    )
    require(number(profile["configFields"]["gameBase"]) == 0, "Config gameBase must use a zero RVA")

    for name, value in profile["configFields"].items():
        require(
            number(value) < image_size and (name == "gameBase" or number(value) > 0),
            "Config field lies outside retail image: " + name,
        )

    for name, value in profile["gameRvas"].items():
        require(0 < number(value) < image_size, "Native address lies outside retail image: " + name)

    require(number(profile["payloadLayout"]["missionStack"]) > 0, "Invalid mission stack size")

    source_hooks = [hook for hook in profile["hooks"] if hook["name"] == "textureSourceHook"]
    require(len(source_hooks) == 1, "Expected one texture source hook")

    source_hook = source_hooks[0]
    require(
        source_hook["original"] == "8b44241c50" and number(source_hook["opcode"]) == 0xE9,
        "Unexpected texture source hook preimage or opcode",
    )
    require(
        number(profile["configFields"]["textureSourceResume"]) == number(source_hook["rva"]) + 5,
        "Texture source resume must follow the displaced native instructions",
    )

    ValidateRendererCloneProfile(profile)
    return profile["language"]
