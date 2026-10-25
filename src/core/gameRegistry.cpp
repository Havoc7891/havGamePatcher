// SPDX-License-Identifier: MIT

#include "core/gameRegistry.hpp"
#include "games/hellbender/patches.hpp"
#include "games/fury3/patches.hpp"

#include <algorithm>

namespace havGamePatcher
{
  std::span<const GameDefinition *const> Games()
  {
    static const std::array games{&hellbender::Definition(), &fury3::Definition()};
    return games;
  }

  const GameDefinition *FindGame(std::string_view id)
  {
    const auto games = Games();
    const auto found =
        std::find_if(games.begin(), games.end(), [id](const auto *game) { return game->id == id; });
    return found == games.end() ? nullptr : *found;
  }

  const FullscreenRecipe *FindFullscreenRecipe(std::string_view gameId, std::string_view edition)
  {
    const auto *game = FindGame(gameId);

    if (!game)
    {
      return nullptr;
    }

    const auto recipes = game->fullscreenRecipes;
    const auto found = std::find_if(recipes.begin(), recipes.end(), [edition](const auto &recipe)
    { return recipe.edition == edition; });

    return found == recipes.end() ? nullptr : &*found;
  }
} // namespace havGamePatcher
