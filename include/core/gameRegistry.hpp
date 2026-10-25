// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMEREGISTRY_HPP
#define HAVGAMEPATCHER_GAMEREGISTRY_HPP

#include "core/patchDefinition.hpp"

namespace havGamePatcher
{
  std::span<const GameDefinition *const> Games();
  const GameDefinition *FindGame(std::string_view id);
  const FullscreenRecipe *FindFullscreenRecipe(std::string_view gameId, std::string_view edition);
} // namespace havGamePatcher

#endif // HAVGAMEPATCHER_GAMEREGISTRY_HPP
