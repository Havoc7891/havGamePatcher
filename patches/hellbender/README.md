# Hellbender fullscreen recipes

English is the primary edition. English, French and German share the patch implementation, with a separate profile and recipe lock for each exact retail executable. Original and joystick-only inputs must produce the same verified output. Building or using the patcher itself does not require game files or Python.

| Edition | Native profile                                     | Recipe lock                                |
| ------- | -------------------------------------------------- | ------------------------------------------ |
| English | [versionProfile.json](english/versionProfile.json) | [recipeLock.json](english/recipeLock.json) |
| French  | [versionProfile.json](french/versionProfile.json)  | [recipeLock.json](french/recipeLock.json)  |
| German  | [versionProfile.json](german/versionProfile.json)  | [recipeLock.json](german/recipeLock.json)  |

The patch preserves the original windowed, hardware-fullscreen and software-fullscreen cycle and the game's mouse behavior. It corrects fullscreen menu placement, texture transfers, selected shading paths and radar colors. Readiness checks and guarded recovery handle specific lost-surface failures at verified native boundaries. Recovery keeps object and frame ownership, respects bounded waits and retains actual native results. A successful readiness check does not guarantee that a later driver operation will succeed.

The shared implementation is in [src/games/hellbender/core](../../src/games/hellbender/core) and [include/games/hellbender/core](../../include/games/hellbender/core). Build and embedding tools are in [scripts/games/hellbender](../../scripts/games/hellbender). Profiles supply each edition's addresses, signatures, calling conventions and relocation data. No edition silently inherits another edition's addresses.

## Rebuild and verify

Use Python 3.10 or later, clang-format and Visual Studio 2022 with MSVC 14.44 x86 tools. Generated C++ headers use the project's `.clang-format`. Set `CLANG_FORMAT` if the formatter is outside the usual tool locations.

```powershell
python scripts/games/hellbender/generateRecipe.py --edition English --prepare
.\scripts\games\hellbender\buildPayload.cmd English
python scripts/games/hellbender/generateRecipe.py --edition English --input "<English-retail-executable>" --payload out/patches/hellbender/english/layoutPayload.dll --check
python scripts/games/hellbender/validateRecipe.py --edition English --input "<English-retail-executable>" --payload out/patches/hellbender/english/layoutPayload.dll
```

Repeat with `French` and `french`, then `German` and `german`, using the corresponding retail executable. Preparation generates the edition's address header. Outputs and reports remain under `out/patches/hellbender/<edition>` and inputs are read only.

Each lock pins the profile, normalized payload and reconstructed images. The build normalizes only the located COFF and POGO timestamps. `--check` requires the committed header and lock to match. Omitting it regenerates the header but does not replace a lock. `--freeze` creates a first lock when none exists, otherwise it validates and preserves the existing lock. A deliberate recipe update needs a separately reviewed new lock and generated header together.

## Validation

See [tests/README.md](../../tests/README.md) for the maintained runners and [recoveryTests.md](../../tests/games/hellbender/recoveryTests.md) for the native recovery fixtures and their boundaries. These suites exercise exact retail instruction spans with owned interfaces, including setup, terminal drawing failures, presentation, input dispatch, movie cleanup and normal shutdown. They do not start the game or prove real driver behavior.
