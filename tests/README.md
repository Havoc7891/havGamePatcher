# Tests

The tests exercise the patcher and game-specific recipes. They are separate from the application build and are not included in release packages. They do not install or start a game.

Run the checks affected by a change:

| Change                                                        | Checks                                                            |
| ------------------------------------------------------------- | ----------------------------------------------------------------- |
| Executable detection, patch application or output replacement | `scripts/runCoreTests.ps1`                                        |
| Shared Python PE or recipe helpers                            | `python tests/toolingTests.py` and both game recipe validators    |
| A game's profile, payload or generated patch data             | Its recipe validator and native fixtures                          |
| Release packaging                                             | `scripts/runPackagingTests.ps1`, then `scripts/createRelease.ps1` |
| README, icon or ordinary UI layout                            | Relevant build or visual check                                    |

Before a release with regenerated patches, run all maintained automated suites. Repeat the manual checks for each affected edition when runtime behavior changes. Large assertion totals include repeated scenarios and do not measure gameplay coverage.

## Core integration

Run `scripts/runCoreTests.ps1` on Windows with MSVC or MinGW. Its required inputs are the supported retail, joystick-only and current fullscreen executables for each edition. The recipe generators produce the current fullscreen outputs. The runner reads inputs without modifying them and creates isolated copies under `out/test/`.

The suite covers detection, patch construction, already-patched recognition, malformed and modified inputs, ambiguous matches, file aliases and safe output replacement.

## Game recipes and native fixtures

Follow the [Fury³ recipe instructions](../patches/fury3/README.md) and [Hellbender recipe instructions](../patches/hellbender/README.md) to prepare the required artifacts.

```powershell
python tests/toolingTests.py
powershell -ExecutionPolicy Bypass -File scripts/games/fury3/runPayloadTests.ps1
powershell -ExecutionPolicy Bypass -File scripts/games/hellbender/runTextureTests.ps1
powershell -ExecutionPolicy Bypass -File scripts/games/hellbender/runShellTests.ps1
powershell -ExecutionPolicy Bypass -File scripts/games/hellbender/runShadingTests.ps1
powershell -ExecutionPolicy Bypass -File scripts/games/hellbender/runReadinessTests.ps1
.\scripts\games\hellbender\runMovieRecoveryTests.ps1 -Edition English,French,German
powershell -ExecutionPolicy Bypass -File scripts/games/hellbender/runRecoveryTests.ps1 -EnglishInput "<English-retail-executable>" -FrenchInput "<French-retail-executable>" -GermanInput "<German-retail-executable>"
```

The recovery runner generates six bounded native-fixture headers per edition from the exact retail inputs. It verifies retail identities, instruction spans and relocations before compiling four suites for fullscreen setup, renderer recovery, terminal frame and movie-cleanup recovery, and mode completion. Generated retail bytes stay under `out/test/` and are not committed. See [the fixture boundaries](games/hellbender/recoveryTests.md).

Hellbender's generic texture transfer and recovery scenarios run once using English. Generated hooks, shading and fullscreen Shell behavior are checked for each edition. Fixtures simulate native interfaces and execute generated code in owned test processes.

Fury³ movie playback checks use Windows multimedia services and are available with `runPayloadTests.ps1 -IncludeMovie`. The optional `runShellTests.ps1 -ProbeShell` checks the host's real Shell interface separately from the simulated fullscreen behavior tests.

## Packaging

```powershell
powershell -ExecutionPolicy Bypass -File scripts/runPackagingTests.ps1
```

These tests use isolated directories under `out/test/`. They check that invalid release inputs fail before a build starts and that a failed archive/checksum replacement restores the previous files. They do not publish or modify the real release package.

## Gameplay

Check each supported edition in the game when changing runtime behavior. Native fixtures do not establish correct rendering, input or timing on real hardware.

Generated fixtures, builds and test reports are written under ignored `out/` directories. Maintained tests do not depend on temporary investigation files.
