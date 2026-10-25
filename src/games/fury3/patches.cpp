// SPDX-License-Identifier: MIT

#include "games/fury3/patches.hpp"
#include "games/fury3/english/fullscreenPatchData.hpp"

namespace havGamePatcher::fury3
{
  namespace
  {
    constexpr auto englishSpans = ConvertPatchSpans(fury3English::patchSpans);

    constexpr std::array joystickPatches{
        JoystickPatch{"Fury\u00b3",
                      {0xC7, 0x05, 0x68, 0x24, 0x53, 0x00, 0x50, 0x03, 0x00, 0x00},
                      {0xC7, 0x05, 0x68, 0x24, 0x53, 0x00, 0x00, 0x04, 0x00, 0x00}}};

    constexpr std::array fullscreenRecipes{FullscreenRecipe{
        "english", "Fury\u00b3 (English)", fury3English::originalSize, fury3English::patchedSize,
        fury3English::originalSha256, fury3English::joystickSha256, fury3English::patchedSha256,
        englishSpans, fury3English::patchBytes, fury3English::appendBytes}};

    constexpr std::array embeddedPatchSections{EmbeddedPatchSection{".fgpf", SectionMatch::prefix}};

    constexpr GameDefinition game{
        "fury3",
        "Fury\u00b3",
        "English",
        joystickPatches,
        fullscreenRecipes,
        embeddedPatchSections,
        "Includes the joystick fix. F4 switches between windowed and borderless fullscreen, "
        "stretching the picture to fill the monitor. Cutscenes fill the window in both modes."};
  } // namespace

  const GameDefinition &Definition()
  {
    return game;
  }
} // namespace havGamePatcher::fury3
