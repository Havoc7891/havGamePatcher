==============
havGamePatcher

VERSION 0.1.0
==============

Copyright (c) 2026 René Nicolaus

Platform: Windows (x64)
Website: https://havoc.de
Source Code: https://github.com/Havoc7891/havGamePatcher
Email: havgamepatcher@havoc.de

========
Contents
========
1. What it does
2. Features
3. Requirements
4. How to run
5. Supported games
6. Applying updated game patches
7. Troubleshooting
8. Uninstall
9. Changelog
10. License

===============
1. What it does
===============

havGamePatcher applies compatibility patches to classic PC games.
It identifies supported executables, shows the available fixes and
creates patched copies while preserving the original files.

Support and available patches depend on the game and edition.

===========
2. Features
===========

- Recognizes supported game executables
- Shows available fixes and patches that are already applied
- Provides patch options for each supported game
- Accepts files through Browse or drag and drop
- Creates patched copies while preserving the originals
- Asks before replacing an existing patched copy

===============
3. Requirements
===============

- Windows. The x64 build requires 64-bit Windows
- An installed copy of a supported retail game with its data files
- Write access to the game folder

The patcher does not require installation.

=============
4. How to run
=============

1) Extract the release ZIP and run havGamePatcher.exe.
2) Close the game, click Browse... and select its original executable.
3) Review the detected game and patch status, then select the optional
   fixes you want.
4) Click Create patched copy. If the output already exists, choose
   Replace or Cancel.
5) Run the new executable from the game folder, or update your game
   shortcut to point to it.

You can also drop one executable onto the patcher window,
havGamePatcher.exe or its shortcut in Windows Explorer.

Patched copies are saved in the same folder with .patched added to the
filename. For example, a supported executable named Game.exe produces
Game.patched.exe. Keep the copy in the game folder so it can use the
existing data files.

==================
5. Supported games
==================

The following retail editions are currently supported. Trial and demo
editions are not supported.

- Fury³: English retail edition

  - Joystick compatibility
    Applied by default

  - Fullscreen compatibility (experimental)
    Optional and unchecked by default. Includes the joystick fix.
    F4 toggles borderless fullscreen and stretches the picture to fill
    the monitor. Cutscenes fill the window in both modes and follow
    resizing. Leaving fullscreen restores the previous window placement.
    Also prevents the legacy joystick-port crash when detection fails.

  The fullscreen patch requires an exact supported executable. A
  modified file may still qualify for the joystick fix. Modified
  fullscreen patches are rejected. Use the original executable when
  creating a new patched copy.

  These patches need no wrapper DLLs. The original and patched
  executables share the game's settings.

- Hellbender: English, French and German retail editions

  - Joystick compatibility
    Applied by default

  - Fullscreen compatibility (experimental)
    Optional and unchecked by default. Includes the joystick fix and
    corrections for textures and shading in hardware rendering,
    including terrain, sky, weapon and radar graphics.
    Keeps the original three-mode F4 cycle:
    windowed -> hardware fullscreen -> software fullscreen -> windowed.
    The original mouse-click behavior is preserved.

  The fullscreen patch requires an exact supported executable. A
  modified file may still qualify for the joystick fix. Modified
  fullscreen patches are rejected. Use the original executable when
  creating a new patched copy.

  Fullscreen behavior can vary with graphics drivers and display
  configurations.

  These patches need no wrapper DLLs. The original and patched
  executables share the game's settings.

Original executable hashes
--------------------------

For reference, these SHA-256 hashes identify the supported original,
unpatched retail executables.

Fury³ (English) - FURY3.EXE
  288ce23095c8e9092bdb665b561f13f58c5b7f4b7c3cdb03f7059ab4149e8438

Hellbender (English) - HELLBEND.EXE
  0044f711594744450040cefe7fee14a1e0f2c1136434fe54a680e99ea9424a5b

Hellbender (French) - HELLBEND.EXE
  497950d07723a887f801cec5ff4403c269b749345b0ddf6b314d6104993d8b92

Hellbender (German) - HELLBEND.EXE
  0e31a60398565723179c19a914c81b62f25b82d921c2fd57cf945f8c90c606d5

================================
6. Applying updated game patches
================================

New versions of havGamePatcher may include new or improved patches
for supported games. To apply these changes to a game you have
already patched:

1) Open the newer version of havGamePatcher.
2) Select the game's original, unpatched executable.
3) Choose the patches you want and click Create patched copy.
4) Confirm replacement of the existing .patched executable.

Keep the original executable for future patch updates.

To return to the original game, launch the original executable and
restore any shortcut you changed. You can delete the generated .patched
executable when it is no longer needed.

==================
7. Troubleshooting
==================

- Executable not recognized
  Check that you selected the original executable of a supported
  retail edition.

- A patch is unavailable
  Available fixes depend on the game, edition and executable version.
  Check the game's entry in section 5.

- Patch already applied
  Use the existing patched executable. To create an updated copy,
  select the original executable in the newer patcher.

- Cannot save the patched copy
  Close the game, make sure the destination file is not in use and
  check that your account can write to the game folder.

- Game starts without the selected fixes
  Check that your shortcut points to the .patched executable and that
  the intended options were selected when creating it.

============
8. Uninstall
============

- Close havGamePatcher and delete its executable and accompanying
  release files
- Patched game copies remain usable without the patcher
- To remove a game's patches, use its original executable and delete
  the generated .patched copy

============
9. Changelog
============

Version 0.1.0 - 2026-10-25
- First release.

===========
10. License
===========

havGamePatcher is released under the MIT License. See LICENSE.txt.
Third-party components retain their own licenses. See
thirdPartyNotices.md and the license texts in the licenses folder.

===========
End of file
===========
