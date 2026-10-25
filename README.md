# havGamePatcher

<img src="resources/havGamePatcher.png" width="96" height="96" alt="havGamePatcher icon">

A Windows application for applying compatibility patches to classic PC games.

## Table of Contents

- [Requirements](#requirements)
- [Features](#features)
- [How to run](#how-to-run)
- [Supported games](#supported-games)
- [Applying updated game patches](#applying-updated-game-patches)
- [Building from source](#building-from-source)
- [Packaging](#packaging)
- [Development](#development)
- [Troubleshooting](#troubleshooting)
- [Uninstall](#uninstall)
- [Contributing](#contributing)
- [License](#license)

## Requirements

- Windows. The x64 build requires 64-bit Windows
- An installed copy of a supported retail game with its data files
- Write access to the game folder

The patcher does not require installation. Python, wxWidgets and a compiler do not need to be installed to use a release build.

## Features

- Recognizes supported game executables
- Shows available fixes and detects patches that are already applied
- Provides patch options for each supported game
- Accepts files through Browse or drag and drop
- Creates patched copies while preserving the original executables
- Asks before replacing an existing patched copy

## How to run

1. Extract the release ZIP and run `havGamePatcher.exe`.
2. Close the game, click **Browse...** and select its original executable.
3. Review the detected game and patch status, then select the optional fixes you want.
4. Click **Create patched copy**. If the output already exists, choose **Replace** or **Cancel**.
5. Run the new executable from the game folder, or update your game shortcut to point to it.

You can also drop one executable onto the patcher window, `havGamePatcher.exe` or its shortcut in Windows Explorer.

Patched copies are saved in the same folder with `.patched` added to the filename. For example, a supported executable named `Game.exe` produces `Game.patched.exe`. Keep the patched copy in the game folder so it can use the existing data files.

For packaged builds, see the [Releases page](https://github.com/Havoc7891/havGamePatcher/releases). Offline instructions are provided in the [plain-text user guide](README.txt).

## Supported games

The following retail editions are currently supported. Trial and demo editions are not supported.

- **Fury³** — English retail edition
  - **Joystick compatibility:** Applied by default
  - **Fullscreen compatibility (experimental):** Optional and unchecked by default. Includes the joystick fix. F4 toggles borderless fullscreen, stretching the picture to fill the monitor. Cutscenes fill the window in both modes and follow resizing. Leaving fullscreen restores the previous window placement. Also prevents the legacy joystick-port crash when device detection fails

  The fullscreen patch requires an exact supported executable. A modified file may still qualify for the joystick fix. Modified fullscreen patches are rejected. Use the original executable when creating a new patched copy.

  See the [patch documentation](patches/fury3/README.md) for implementation details.

- **Hellbender** — English, French and German retail editions
  - **Joystick compatibility:** Applied by default
  - **Fullscreen compatibility (experimental):** Optional and unchecked by default. Includes the joystick fix and corrections for textures and shading in hardware rendering, including terrain, sky, weapon and radar graphics. Keeps the original windowed → hardware fullscreen → software fullscreen → windowed F4 cycle and mouse-click behavior

  The fullscreen patch requires an exact supported executable. A modified file may still qualify for the joystick fix. Modified fullscreen patches are rejected. Use the original executable when creating a new patched copy.

  Fullscreen behavior can vary with graphics drivers and display configurations. See the [patch documentation](patches/hellbender/README.md) for implementation details.

### Original executable hashes

For reference, these SHA-256 hashes identify the supported original, unpatched retail executables.

| Game       | Edition | Executable     | SHA-256                                                            |
| ---------- | ------- | -------------- | ------------------------------------------------------------------ |
| Fury³      | English | `FURY3.EXE`    | `288ce23095c8e9092bdb665b561f13f58c5b7f4b7c3cdb03f7059ab4149e8438` |
| Hellbender | English | `HELLBEND.EXE` | `0044f711594744450040cefe7fee14a1e0f2c1136434fe54a680e99ea9424a5b` |
| Hellbender | French  | `HELLBEND.EXE` | `497950d07723a887f801cec5ff4403c269b749345b0ddf6b314d6104993d8b92` |
| Hellbender | German  | `HELLBEND.EXE` | `0e31a60398565723179c19a914c81b62f25b82d921c2fd57cf945f8c90c606d5` |

## Applying updated game patches

New versions of havGamePatcher may include new or improved patches for supported games. To apply these changes to a game you have already patched:

1. Open the newer version of havGamePatcher.
2. Select the game's original, unpatched executable.
3. Choose the patches you want and click **Create patched copy**.
4. Confirm replacement of the existing `.patched` executable.

Keep the original executable for future patch updates.

To return to the original game, launch the original executable and restore any shortcut you changed. You can delete the generated `.patched` executable when it is no longer needed.

## Building from source

Use native Windows tools, including the Windows version of VS Code when debugging.

- CMake 4.0 or newer and Git on `PATH`
- Visual Studio 2022 or Build Tools 2022 with **Desktop development with C++** and a Windows SDK, or a recent MinGW-w64 toolchain

The project uses C++23. CMake fetches wxWidgets 3.3.4 during the first configure, which requires an internet connection. The patcher links wxWidgets core and base statically, with unused features disabled. Game files and Python are not needed to build the application from the included patch data.

### Visual Studio / MSVC

From the project directory:

```powershell
cmake --preset msvc
cmake --build --preset msvc-release --parallel
```

The executable is written to `out/build/msvc/Release/havGamePatcher.exe`. Use `msvc-debug` as the build preset for a Debug build.

In Visual Studio 2022, use **File > Open > Folder**, select the `msvc` configure preset and the desired build preset, and choose `havGamePatcher.exe` as the startup item. You can also open the generated `out/build/msvc/havGamePatcher.sln`.

### MinGW-w64

Put `gcc`, `g++` and `mingw32-make` on `PATH`, then run:

```powershell
cmake --preset mingw-release
cmake --build --preset mingw-release --parallel
```

The executable is written to `out/build/mingw-release/havGamePatcher.exe`. Use `mingw-debug` in both commands for a Debug build. Debugging also requires `gdb.exe` on `PATH`.

The batch helper accepts the same build preset names:

```powershell
.\scripts\compile.bat msvc-release
```

With no argument, it builds `mingw-release`. Build outputs remain separate under `out/build/`. Use the ignored `CMakeUserPresets.json` for local paths and options.

### VS Code

1. Open the project folder in the Windows version of VS Code and install the recommended **C/C++** and **CMake Tools** extensions.
2. Select **havGamePatcher (MSVC Debug)** or **havGamePatcher (MinGW Debug)** in Run and Debug.
3. Set a breakpoint and press **F5**. The selected configuration configures and builds its Debug target before launching.

**Ctrl+Shift+B** builds MSVC Debug. MinGW Debug is available through **Tasks: Run Build Task**.

For IntelliSense, run **CMake: Select Configure Preset**, choose `msvc` or `mingw-debug`, then run **CMake: Configure**. Restart VS Code after changing `PATH`.

## Packaging

Create a Windows x64 release ZIP from the project directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/createRelease.ps1
```

The script configures and builds the MSVC Release preset, reads the version from `include/havGamePatcherInfo.hpp`, and checks it against the executable and user guide. It stages only the release files and verifies the archive contents.

The outputs are written to `out/releases/`:

```text
havGamePatcher-0.1.0-win-x64.zip
havGamePatcher-0.1.0-win-x64.zip.sha256
```

The version in these filenames follows the application version. The ZIP contains:

- `havGamePatcher.exe`
- `README.txt`
- `LICENSE.txt`
- `thirdPartyNotices.md`
- `licenses/` with the third-party license texts

The checksum file records the ZIP's SHA-256 hash. Game executables, build intermediates and diagnostic files are not included. This packaging command uses MSVC. MinGW remains available for local builds.

## Development

| Directory    | Contents                                                       |
| ------------ | -------------------------------------------------------------- |
| `src/`       | Application UI, shared patch engine and game modules           |
| `include/`   | Application headers, game definitions and generated patch data |
| `resources/` | Icons, Windows resources and application manifest              |
| `scripts/`   | Build helpers, icon generation and patch tooling               |
| `patches/`   | Game documentation, edition profiles and recipe locks          |
| `tests/`     | Automated tests                                                |
| `out/`       | Generated builds, patch executables, test reports and releases |

Each game supplies its supported editions and patches through the shared registry. Code shared by editions lives under that game's `core/` directories. Generic executable validation and file handling live in `src/core/` and `include/core/`.

To rebuild or validate embedded patches, follow the [Fury³ recipe documentation](patches/fury3/README.md) or [Hellbender recipe documentation](patches/hellbender/README.md). Those workflows require the corresponding retail executable and additional tools described there. The [test guide](tests/README.md) explains which suites to run for each kind of change.

Use PascalCase for C++ function and method names, with braces around conditional bodies. Preserve names required by external interfaces. The repository includes clang-format settings for C++ and Black settings for the Python scripts and tests:

```powershell
clang-format -i src/havGamePatcher.cpp src/core/havGamePatcherCore.cpp include/core/havGamePatcherCore.hpp
python -m black .
```

For PowerShell scripts, use **Format Document** in VS Code with the Microsoft PowerShell extension and two-space indentation. Bulk formatting uses its bundled PSScriptAnalyzer with the same settings. The default `Invoke-Formatter` settings differ from the editor settings, so do not use them unchanged.

The release package's user instructions are maintained in [`README.txt`](README.txt). Keep its version aligned with the application metadata before packaging.

## Troubleshooting

- **Executable not recognized:** check that you selected the original executable of a supported retail edition
- **A patch is unavailable:** available fixes depend on the game, edition and executable version. Check the game's entry above
- **Patch already applied:** use the existing patched executable. To create an updated copy, select the original executable in the newer patcher
- **Cannot save the patched copy:** close the game, make sure the destination file is not in use and check that your account can write to the game folder
- **Game starts without the selected fixes:** check that your shortcut points to the `.patched` executable and that the intended options were selected when creating it

## Uninstall

- Close havGamePatcher and delete its executable and accompanying release files
- Patched game copies remain usable without the patcher
- To remove a game's patches, use its original executable and delete the generated `.patched` copy

## Contributing

Suggestions for additional games and fixes are welcome through [issues](https://github.com/Havoc7891/havGamePatcher/issues).

For bug reports, include the patcher version, game and edition, selected fixes, exact error text and steps to reproduce. For graphics problems, include the display mode and graphics hardware.

To maintain a consistent design and quality for this project, changes are implemented by the maintainer rather than via direct pull requests.

## License

Copyright © 2026 René Nicolaus

Released under the [MIT License](LICENSE).

Third-party components retain their own licenses. See [thirdPartyNotices.md](thirdPartyNotices.md) and the license texts in [`licenses/`](licenses/).
