// SPDX-License-Identifier: MIT

#include "games/hellbender/patches.hpp"
#include "games/hellbender/english/fullscreenPatchData.hpp"
#include "games/hellbender/french/fullscreenPatchData.hpp"
#include "games/hellbender/german/fullscreenPatchData.hpp"

namespace havGamePatcher::hellbender
{
  namespace
  {
    constexpr auto englishSpans = ConvertPatchSpans(hellbenderEnglish::patchSpans);
    constexpr auto frenchSpans = ConvertPatchSpans(hellbenderFrench::patchSpans);
    constexpr auto germanSpans = ConvertPatchSpans(hellbenderGerman::patchSpans);

    constexpr std::array joystickPatches{
        JoystickPatch{"Hellbender",
                      {0xC7, 0x05, 0xBC, 0x9A, 0x57, 0x00, 0x50, 0x03, 0x00, 0x00},
                      {0xC7, 0x05, 0xBC, 0x9A, 0x57, 0x00, 0x00, 0x04, 0x00, 0x00}},
        JoystickPatch{"Hellbender (French/German)",
                      {0xC7, 0x05, 0x7C, 0xA7, 0x53, 0x00, 0x50, 0x03, 0x00, 0x00},
                      {0xC7, 0x05, 0x7C, 0xA7, 0x53, 0x00, 0x00, 0x04, 0x00, 0x00}}};

    constexpr std::array fullscreenRecipes{
        FullscreenRecipe{"english", "Hellbender (English)", hellbenderEnglish::originalSize,
                         hellbenderEnglish::patchedSize, hellbenderEnglish::originalSha256,
                         hellbenderEnglish::joystickSha256, hellbenderEnglish::patchedSha256,
                         englishSpans, hellbenderEnglish::patchBytes,
                         hellbenderEnglish::appendBytes},
        FullscreenRecipe{"french", "Hellbender (French)", hellbenderFrench::originalSize,
                         hellbenderFrench::patchedSize, hellbenderFrench::originalSha256,
                         hellbenderFrench::joystickSha256, hellbenderFrench::patchedSha256,
                         frenchSpans, hellbenderFrench::patchBytes, hellbenderFrench::appendBytes},
        FullscreenRecipe{"german", "Hellbender (German)", hellbenderGerman::originalSize,
                         hellbenderGerman::patchedSize, hellbenderGerman::originalSha256,
                         hellbenderGerman::joystickSha256, hellbenderGerman::patchedSha256,
                         germanSpans, hellbenderGerman::patchBytes, hellbenderGerman::appendBytes}};

    constexpr std::array embeddedPatchSections{
        EmbeddedPatchSection{".hglp", SectionMatch::prefix},
        EmbeddedPatchSection{std::string_view(".hgcsh\0\0", 8), SectionMatch::exact}};

    constexpr GameDefinition game{
        "hellbender",
        "Hellbender",
        "English, French, German",
        joystickPatches,
        fullscreenRecipes,
        embeddedPatchSections,
        "Includes the joystick fix. Keeps the existing three-state F4 cycle."};
  } // namespace

  const GameDefinition &Definition()
  {
    return game;
  }
} // namespace havGamePatcher::hellbender
