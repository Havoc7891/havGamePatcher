// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHERCORE_HPP
#define HAVGAMEPATCHERCORE_HPP

#include "core/patchDefinition.hpp"

#include <filesystem>
#include <vector>

namespace havGamePatcher
{
  namespace fs = std::filesystem;

  enum class ExistingOutput
  {
    reject,
    replace
  };

  struct Inspection
  {
    const GameDefinition *game{};
    const JoystickPatch *patch{};
    std::string_view edition;
    std::size_t offset{};
    bool joystickPatched{};
    bool fullscreenSupported{};
    bool fullscreenPatched{};
    std::vector<Byte> bytes;

    std::string_view GameId() const;
    std::string_view GameName() const;
    std::string_view FullscreenDescription() const;
    bool CanApply(bool includeFullscreen = false) const;
  };

  Inspection Inspect(const fs::path &input);
  fs::path OutputPath(const fs::path &input);

  // Revalidate the input and finish writing before publishing the output.
  // Replacing an existing output requires explicit caller approval.
  fs::path Apply(const fs::path &input,
                 bool includeFullscreen = false,
                 ExistingOutput existingOutput = ExistingOutput::reject);
} // namespace havGamePatcher

#endif // HAVGAMEPATCHERCORE_HPP
