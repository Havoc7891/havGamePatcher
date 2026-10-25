# Fury³ fullscreen recipe

The English retail edition has an embedded fullscreen recipe that accepts either the original executable or its joystick-only patched copy. The patcher verifies the exact input and complete output hashes. No game files or Python installation are needed when building or using the patcher itself.

The [version profile](english/versionProfile.json) records the supported executable and hook locations. The [recipe lock](english/recipeLock.json) pins the compiled payload and output. Payload sources live under [`src/games/fury3/core`](../../src/games/fury3/core), headers under [`include/games/fury3/core`](../../include/games/fury3/core), and generation tools under [`scripts/games/fury3`](../../scripts/games/fury3). The generated application data is in [`include/games/fury3/english`](../../include/games/fury3/english).

The fullscreen patch replaces the obsolete DisplayDib fullscreen path with borderless GDI presentation. Fullscreen stretches the menu and gameplay picture to fill the monitor. The native menu bar remains visible on the main menu and is hidden during loading, cutscenes and gameplay. F4 restores the previous window placement when leaving fullscreen, including the normal windowed menu behavior.

The recipe also prevents direct joystick-port access after Windows device detection fails. The normal Windows joystick polling path remains intact. The unavailable-device guard belongs to the fullscreen recipe. Leaving fullscreen unchecked retains the older joystick-only patch.

Cutscenes stretch to fill the available client area in both fullscreen and windowed mode, including window resizing. The full source image, audio and existing playback controls are preserved. These changes are included when the patcher's **Fullscreen compatibility** option is selected, including their effect on windowed cutscenes.

## Rebuild the recipe

Use Python 3.10 or later, clang-format and Visual Studio 2022 or Build Tools with MSVC 14.44 x86 tools. Generated C++ headers are automatically formatted with the project's `.clang-format`, including when comparing them with `--check`. The generator uses `CLANG_FORMAT` if set, then searches `PATH` and installed Visual Studio tools on Windows. Set `CLANG_FORMAT` to the formatter executable if it is installed elsewhere. Formatting failures leave existing generated files unchanged.

From the project directory:

```powershell
python scripts/games/fury3/generateRecipe.py --edition English --prepare
.\scripts\games\fury3\buildPayload.cmd English
python scripts/games/fury3/generateRecipe.py --edition English --input "<path-to-FURY3.EXE>" --payload out/patches/fury3/english/fullscreenPayload.dll --check
python scripts/games/fury3/validateRecipe.py --edition English --input "<path-to-FURY3.EXE>" --payload out/patches/fury3/english/fullscreenPayload.dll
```

Replace `<path-to-FURY3.EXE>` with your English retail executable. Inputs are read only. These commands do not install, launch or attach to a game.

`--check` verifies the generated header and frozen output. Omit it only to regenerate the header. Both accepted inputs must produce identical bytes. `--freeze` records the first reviewed build and does not bypass an existing lock. The generator normalizes only COFF and POGO timestamps and verifies the compiled address constants, imported APIs, transplanted sections and merged relocations.

## Validation

Run the maintained native fixtures after preparing the recipe:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/games/fury3/runPayloadTests.ps1
```

These fixtures cover fullscreen menu/rendering behavior and guarded input using the compiled patch in owned test processes. Add `-IncludeMovie` to check video playback with Windows multimedia services and a generated silent video. They do not launch the game. Reports are written under `out/test/`.

If device detection fails, disabled joystick input may be saved as `joystickActive=0` when the game exits.
