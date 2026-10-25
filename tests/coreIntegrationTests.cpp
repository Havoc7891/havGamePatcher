// SPDX-License-Identifier: MIT

#include "core/havGamePatcherCore.hpp"
#include "core/gameRegistry.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
  using havGamePatcher::Byte;
  using havGamePatcher::ExistingOutput;
  namespace fs = std::filesystem;
  using Bytes = std::vector<Byte>;

  Bytes Read(const fs::path &path)
  {
    std::ifstream stream(path, std::ios::binary);

    if (!stream)
    {
      throw std::runtime_error("Cannot read test input.");
    }

    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  }

  void Write(const fs::path &path, const Bytes &bytes)
  {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);

    if (!stream)
    {
      throw std::runtime_error("Cannot create test fixture.");
    }

    stream.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    stream.close();

    if (!stream)
    {
      throw std::runtime_error("Cannot finish test fixture.");
    }
  }

  void Require(bool condition, const char *message)
  {
    if (!condition)
    {
      throw std::runtime_error(message);
    }
  }

  void RequireOnlyEntries(const fs::path &directory, std::initializer_list<fs::path> expected)
  {
    std::vector<fs::path> actual;

    for (const auto &entry : fs::directory_iterator(directory))
    {
      actual.push_back(entry.path());
    }

    std::vector<fs::path> wanted(expected);
    std::sort(actual.begin(), actual.end());
    std::sort(wanted.begin(), wanted.end());
    Require(actual == wanted, "Replacement left unexpected files or removed an expected entry.");
  }

  struct FileHandle
  {
    HANDLE value{INVALID_HANDLE_VALUE};

    ~FileHandle()
    {
      if (value != INVALID_HANDLE_VALUE)
      {
        CloseHandle(value);
      }
    }
  };

  struct FileAttributes
  {
    fs::path path;
    DWORD original;

    ~FileAttributes()
    {
      SetFileAttributesW(path.c_str(), original);
    }
  };

  template <class Operation> void Rejects(Operation operation)
  {
    try
    {
      operation();
    }
    catch (const std::exception &)
    {
      return;
    }

    throw std::runtime_error("Expected operation to reject input.");
  }

  std::string Quote(std::string_view value)
  {
    std::ostringstream result;
    result << '"';

    for (const char c : value)
    {
      if (c == '"' || c == '\\')
      {
        result << '\\' << c;
      }
      else if (c == '\n')
      {
        result << "\\n";
      }
      else if (c == '\r')
      {
        result << "\\r";
      }
      else if (c == '\t')
      {
        result << "\\t";
      }
      else
      {
        result << c;
      }
    }

    result << '"';
    return result.str();
  }

  struct Suite
  {
    fs::path directory;
    std::vector<std::string> results;
    std::size_t failures{};

    void Test(std::string name, const std::function<void(const fs::path &)> &operation)
    {
      const auto caseDirectory = directory / (std::to_string(results.size() + 1) + "-" + name);
      fs::create_directory(caseDirectory);

      try
      {
        operation(caseDirectory);
        results.push_back("{\"name\":" + Quote(name) + ",\"passed\":true}");
      }
      catch (const std::exception &error)
      {
        ++failures;
        results.push_back("{\"name\":" + Quote(name) +
                          ",\"passed\":false,\"error\":" + Quote(error.what()) + "}");
      }
    }

    void Report() const
    {
      std::cout << "{\"format\":\"ProductionCoreIntegrationTests\",\"cases\":" << results.size()
                << ",\"failures\":" << failures << ",\"gameProcessesStarted\":false,\"results\":[";
      for (std::size_t index = 0; index < results.size(); ++index)
      {
        std::cout << (index ? "," : "") << results[index];
      }

      std::cout << "]}\n";
    }
  };

  fs::path
  Fixture(const fs::path &directory, const Bytes &bytes, const wchar_t *name = L"HELLBEND.EXE")
  {
    const auto path = directory / name;
    Write(path, bytes);

    return path;
  }

  void RejectFullscreen(const fs::path &path)
  {
    const auto before = Read(path);
    Rejects([&] { havGamePatcher::Apply(path, true); });
    Require(Read(path) == before, "Rejected fullscreen request changed its input.");
    Require(!fs::exists(havGamePatcher::OutputPath(path)),
            "Rejected fullscreen request left an output file.");
  }

  void WithoutFullscreen(const fs::path &path)
  {
    try
    {
      const auto inspection = havGamePatcher::Inspect(path);
      Require(!inspection.fullscreenSupported, "Nonexact input gained fullscreen support.");
      Require(!inspection.fullscreenPatched, "Nonexact input was called fully patched.");
      Require(!inspection.CanApply(true), "Nonexact input permits the fullscreen patch.");
    }
    catch (const std::runtime_error &error)
    {
      // Unsupported files may be rejected by inspect. Our own assertions must still fail.
      const std::string_view message(error.what());

      if (message.starts_with("Nonexact"))
      {
        throw;
      }
    }

    RejectFullscreen(path);
  }

  bool DescendantOf(const fs::path &path, const fs::path &root)
  {
    // Compare directory identities because native Windows toolchains preserve
    // different spelling/casing when normalizing the same existing directory.
    auto ancestor = path.parent_path();

    while (!ancestor.empty())
    {
      std::error_code error;

      if (fs::equivalent(ancestor, root, error) && !error)
      {
        return true;
      }

      const auto parent = ancestor.parent_path();

      if (parent == ancestor)
      {
        break;
      }

      ancestor = parent;
    }

    return false;
  }

  struct EditionFixture
  {
    std::string_view slug, name;
    std::string_view gameId, edition;
    const wchar_t *executableName;
    std::array<fs::path, 3> inputs;
    std::array<Bytes, 3> bytes;
  };

  void TestEdition(Suite &suite, const EditionFixture &edition)
  {
    const auto &inputs = edition.inputs;
    const auto &bytes = edition.bytes;
    const auto &original = bytes[0];
    const auto &joystick = bytes[1];
    const auto &full = bytes[2];
    const auto test =
        [&](std::string_view name, const std::function<void(const fs::path &)> &operation)
    {
      suite.Test(std::string(edition.slug) + "-" + std::string(name), operation);
    };
    test("retail-status", [&](const fs::path &)
    {
      const auto item = havGamePatcher::Inspect(inputs[0]);
      Require(item.GameId() == edition.gameId && item.GameName() == edition.name &&
                  item.edition == edition.edition,
              "Retail edition identity is wrong.");
      Require(item.patch && !item.joystickPatched, "Retail joystick status is wrong.");
      Require(item.fullscreenSupported && !item.fullscreenPatched,
              "Retail fullscreen status is wrong.");
      Require(item.CanApply() && item.CanApply(true), "Retail patch choices are unavailable.");
    });
    test("joystick-only-status", [&](const fs::path &)
    {
      const auto item = havGamePatcher::Inspect(inputs[1]);
      Require(item.GameId() == edition.gameId && item.GameName() == edition.name &&
                  item.edition == edition.edition,
              "Joystick-only edition identity is wrong.");
      Require(item.joystickPatched && item.fullscreenSupported && !item.fullscreenPatched,
              "Joystick-only upgrade status is wrong.");
      Require(!item.CanApply() && item.CanApply(true), "Joystick-only upgrade choice is wrong.");
    });
    test("complete-status", [&](const fs::path &)
    {
      const auto item = havGamePatcher::Inspect(inputs[2]);
      Require(item.GameId() == edition.gameId && item.GameName() == edition.name &&
                  item.edition == edition.edition,
              "Patched edition identity is wrong.");
      Require(item.joystickPatched && item.fullscreenSupported && item.fullscreenPatched,
              "Frozen output status is wrong.");
      Require(!item.CanApply() && !item.CanApply(true), "Duplicate patch remains available.");
    });

    for (std::size_t index = 0; index < 2; ++index)
    {
      test(index ? "joystick-to-frozen-output" : "retail-to-frozen-output",
           [&, index](const fs::path &directory)
      {
        const auto input = Fixture(directory, bytes[index], edition.executableName);
        const auto patched = havGamePatcher::Apply(input, true);
        Require(patched == havGamePatcher::OutputPath(input), "Wrong output path.");
        Require(Read(patched) == full, "Output differs from the frozen edition candidate.");
        Require(Read(input) == bytes[index], "Fullscreen patch changed its input.");
        Require(havGamePatcher::Inspect(patched).fullscreenPatched,
                "New output was not recognized as fully patched.");
        RejectFullscreen(patched);
      });
    }

    for (std::size_t index = 0; index < 2; ++index)
    {
      test(index ? "replace-from-joystick" : "replace-upgrades-existing-joystick",
           [&, index](const fs::path &directory)
      {
        const auto input = Fixture(directory, bytes[index], edition.executableName);
        const auto patched = havGamePatcher::OutputPath(input);
        Write(patched, index ? Bytes{'O', 'L', 'D'} : joystick);
        Require(havGamePatcher::Apply(input, true, ExistingOutput::replace) == patched,
                "Replacement returned the wrong output path.");
        Require(Read(patched) == full, "Replacement differs from the frozen edition candidate.");
        Require(Read(input) == bytes[index], "Replacement changed its source executable.");
        RequireOnlyEntries(directory, {input, patched});
      });
    }

    test("default-remains-joystick-only", [&](const fs::path &directory)
    {
      const auto input = Fixture(directory, original, edition.executableName);
      Require(Read(havGamePatcher::Apply(input)) == joystick,
              "Default application changed more than the existing joystick fix.");
      Require(Read(input) == original, "Default patch changed its input.");
    });

    for (std::size_t index = 1; index < 3; ++index)
    {
      test(index == 1 ? "duplicate-joystick-rejected" : "duplicate-complete-rejected",
           [&, index](const fs::path &directory)
      {
        const auto input = Fixture(directory, bytes[index], edition.executableName);
        Rejects([&] { havGamePatcher::Apply(input); });
        Require(Read(input) == bytes[index], "Duplicate patch changed its input.");
        Require(!fs::exists(havGamePatcher::OutputPath(input)),
                "Duplicate patch created an output.");
      });
    }

    test("reinspect-before-writing", [&](const fs::path &directory)
    {
      const auto input = Fixture(directory, original, edition.executableName);
      Require(havGamePatcher::Inspect(input).fullscreenSupported,
              "Initial fixture is not recognized.");
      auto changed = original;
      changed.back() ^= 1;
      Write(input, changed);
      WithoutFullscreen(input);
    });
    test("modified-joystick-no-fullscreen", [&](const fs::path &directory)
    {
      auto changed = joystick;
      changed.back() ^= 0x40;
      WithoutFullscreen(Fixture(directory, changed, edition.executableName));
    });
    test("modified-complete-rejected", [&](const fs::path &directory)
    {
      auto changed = full;
      changed.back() ^= 0x40;
      const auto input = Fixture(directory, changed, edition.executableName);
      Rejects([&] { havGamePatcher::Inspect(input); });
      Rejects([&] { havGamePatcher::Apply(input); });
      RejectFullscreen(input);
      RequireOnlyEntries(directory, {input});
    });
    test("partial-fullscreen-rejected", [&](const fs::path &directory)
    {
      auto changed = full;
      const auto retail = havGamePatcher::Inspect(inputs[0]);
      std::copy(retail.patch->original.begin(), retail.patch->original.end(),
                changed.begin() + retail.offset);
      Require(changed != full, "Partial fixture did not remove the joystick fix.");
      const auto input = Fixture(directory, changed, edition.executableName);
      Rejects([&] { havGamePatcher::Inspect(input); });
      Rejects([&] { havGamePatcher::Apply(input); });
      RejectFullscreen(input);
    });
    test("truncated-with-signature-no-fullscreen", [&](const fs::path &directory)
    {
      const auto item = havGamePatcher::Inspect(inputs[0]);
      Require(item.patch, "Retail fixture lacks its joystick signature.");
      const auto end = item.offset + item.patch->original.size();
      Require(end < original.size(), "Truncation would not shorten the fixture.");
      WithoutFullscreen(Fixture(directory, Bytes(original.begin(), original.begin() + end),
                                edition.executableName));
    });
    test("ambiguous-signature-rejected", [&](const fs::path &directory)
    {
      auto changed = original;
      const auto item = havGamePatcher::Inspect(inputs[0]);
      changed.insert(changed.end(), item.patch->original.begin(), item.patch->original.end());
      const auto input = Fixture(directory, changed, edition.executableName);
      Rejects([&] { havGamePatcher::Inspect(input); });
      RejectFullscreen(input);
    });
  }
} // namespace

int wmain(int argc, wchar_t **argv)
{
  try
  {
    if (argc < 14)
    {
      throw std::runtime_error(
          "Usage: coreIntegrationTests german-retail german-joystick german-fullscreen "
          "french-retail french-joystick french-fullscreen english-retail english-joystick "
          "english-fullscreen fury3-retail fury3-joystick "
          "fury3-fullscreen workspace-output "
          "[joystick-only-retail-inputs...]");
    }

    std::array<fs::path, 12> inputs;
    std::array<Bytes, 12> bytes;

    for (std::size_t index = 0; index < inputs.size(); ++index)
    {
      inputs[index] = argv[index + 1];
      bytes[index] = Read(inputs[index]);
    }

    const auto output = fs::weakly_canonical(fs::path(argv[13]));
    const auto allowed = fs::weakly_canonical(fs::path(HAV_TEST_PROJECT_ROOT) / "out" / "test");
    Require(DescendantOf(output, allowed), "Test output must be below this project's out/test.");
    Require(!fs::exists(output), "Test output directory must be new.");
    fs::create_directories(output);
    Suite suite{output, {}, 0};
    const std::array editions{EditionFixture{"english",
                                             "Hellbender (English)",
                                             "hellbender",
                                             "english",
                                             L"HELLBEND.EXE",
                                             {inputs[6], inputs[7], inputs[8]},
                                             {bytes[6], bytes[7], bytes[8]}},
                              EditionFixture{"french",
                                             "Hellbender (French)",
                                             "hellbender",
                                             "french",
                                             L"HELLBEND.EXE",
                                             {inputs[3], inputs[4], inputs[5]},
                                             {bytes[3], bytes[4], bytes[5]}},
                              EditionFixture{"german",
                                             "Hellbender (German)",
                                             "hellbender",
                                             "german",
                                             L"HELLBEND.EXE",
                                             {inputs[0], inputs[1], inputs[2]},
                                             {bytes[0], bytes[1], bytes[2]}},
                              EditionFixture{"fury3-english",
                                             "Fury\u00b3 (English)",
                                             "fury3",
                                             "english",
                                             L"FURY3.EXE",
                                             {inputs[9], inputs[10], inputs[11]},
                                             {bytes[9], bytes[10], bytes[11]}}};
    suite.Test("game-registry-identities", [&](const fs::path &)
    {
      const auto games = havGamePatcher::Games();
      Require(!games.empty(), "No games are registered.");
      std::vector<std::string_view> identifiers;

      for (const auto *game : games)
      {
        Require(game && !game->id.empty() && !game->name.empty(), "Incomplete game metadata.");
        Require(havGamePatcher::FindGame(game->id) == game, "Game lookup returned another game.");
        Require(std::find(identifiers.begin(), identifiers.end(), game->id) == identifiers.end(),
                "Two games share an identity.");
        identifiers.push_back(game->id);
        Require(!game->joystickPatches.empty(), "Registered game has no joystick definitions.");
      }

      Require(havGamePatcher::FindGame("hellbender") && havGamePatcher::FindGame("fury3"),
              "An existing supported game is absent from the registry.");
      Require(!havGamePatcher::FindGame("unknown") && !havGamePatcher::FindGame(""),
              "Unknown game identity resolved to a registered game.");
    });
    suite.Test("fullscreen-recipes-are-scoped-to-game", [&](const fs::path &)
    {
      const auto *hellbender = havGamePatcher::FindGame("hellbender");
      const auto *fury3 = havGamePatcher::FindGame("fury3");
      Require(hellbender && fury3, "A required game is not registered.");
      Require(!hellbender->fullscreenRecipes.empty() &&
                  hellbender->fullscreenRecipes.front().edition == "english",
              "English is not the primary Hellbender edition.");
      Require(!fury3->fullscreenRecipes.empty() &&
                  fury3->fullscreenRecipes.front().edition == "english",
              "English is not the primary Fury3 edition.");
      for (const auto *game : {hellbender, fury3})
      {
        std::vector<std::string_view> editionsSeen;

        for (const auto &recipe : game->fullscreenRecipes)
        {
          Require(std::find(editionsSeen.begin(), editionsSeen.end(), recipe.edition) ==
                      editionsSeen.end(),
                  "Two fullscreen recipes share an edition within one game.");
          editionsSeen.push_back(recipe.edition);
          Require(havGamePatcher::FindFullscreenRecipe(game->id, recipe.edition) == &recipe,
                  "Game and edition did not select their exact recipe.");
          Require(!havGamePatcher::FindFullscreenRecipe("unknown", recipe.edition),
                  "An unknown game inherited another game's recipe.");
        }

        Require(!havGamePatcher::FindFullscreenRecipe(game->id, "unknown"),
                "An unknown edition inherited the primary recipe.");
      }

      const auto *hellbenderEnglish = havGamePatcher::FindFullscreenRecipe("hellbender", "english");
      const auto *fury3English = havGamePatcher::FindFullscreenRecipe("fury3", "english");
      Require(hellbenderEnglish && fury3English && hellbenderEnglish != fury3English &&
                  hellbenderEnglish->originalSha256 != fury3English->originalSha256 &&
                  hellbenderEnglish->patchedSha256 != fury3English->patchedSha256,
              "Games with the same edition name did not retain independent recipes.");
      Require(!havGamePatcher::FindFullscreenRecipe("fury3", "french") &&
                  !havGamePatcher::FindFullscreenRecipe("fury3", "german"),
              "A Hellbender-only edition leaked into Fury3.");
    });
    suite.Test("empty-inspection-has-no-game", [&](const fs::path &)
    {
      const havGamePatcher::Inspection item;
      Require(item.GameId().empty() && item.GameName().empty() &&
                  item.FullscreenDescription().empty(),
              "An empty inspection inherited a game identity or description.");
      Require(!item.CanApply() && !item.CanApply(true), "An empty inspection offers a patch.");
    });

    for (const auto &edition : editions)
    {
      TestEdition(suite, edition);
    }

    const auto &original = bytes[6];
    const auto &joystick = bytes[7];
    const auto &full = bytes[8];
    // Filesystem and malformed-header handling are shared across all game editions
    suite.Test("existing-output-preserved", [&](const fs::path &directory)
    {
      const auto input = Fixture(directory, original);
      const Bytes sentinel{'K', 'E', 'E', 'P'};
      const auto patched = havGamePatcher::OutputPath(input);
      Write(patched, sentinel);
      Rejects([&] { havGamePatcher::Apply(input, true); });
      Require(Read(patched) == sentinel, "Existing output was overwritten.");
      Require(Read(input) == original, "Existing-output rejection changed the input.");
    });
    suite.Test("output-directory-rejected", [&](const fs::path &directory)
    {
      const auto input = Fixture(directory, original);
      const auto patched = havGamePatcher::OutputPath(input);
      fs::create_directory(patched);
      Rejects([&] { havGamePatcher::Apply(input, true); });
      Require(fs::is_directory(patched) && fs::is_empty(patched),
              "Existing output directory was modified.");
      Require(Read(input) == original, "Directory-output rejection changed the input.");
    });
    suite.Test("unknown-and-truncated-rejected", [&](const fs::path &directory)
    {
      const std::array<Bytes, 5> invalid{Bytes{}, Bytes{'M'}, Bytes{'M', 'Z'}, Bytes{'N', 'O'},
                                         Bytes(original.begin(), original.begin() + 32)};
      for (std::size_t index = 0; index < invalid.size(); ++index)
      {
        const auto input = directory / ("unknown-" + std::to_string(index) + ".exe");
        Write(input, invalid[index]);
        Rejects([&] { havGamePatcher::Inspect(input); });
        RejectFullscreen(input);
      }
    });
    suite.Test("unicode-path", [&](const fs::path &directory)
    {
      const auto nested = directory / L"Ren\u00e9 Spiele";
      fs::create_directory(nested);
      const auto input = Fixture(nested, original, L"H\u00f6llen.EXE");
      Require(Read(havGamePatcher::Apply(input, true)) == full,
              "Unicode input path changed the patch result.");
      Require(Read(input) == original, "Unicode-path patch changed its input.");
    });
    suite.Test("replace-joystick-only", [&](const fs::path &directory)
    {
      const auto input = Fixture(directory, original);
      const auto patched = havGamePatcher::OutputPath(input);
      Write(patched, Bytes{'O', 'L', 'D'});
      Require(Read(havGamePatcher::Apply(input, false, ExistingOutput::replace)) == joystick,
              "Joystick-only replacement changed more than the joystick fix.");
      Require(Read(input) == original, "Joystick-only replacement changed its input.");
      RequireOnlyEntries(directory, {input, patched});
    });
    suite.Test("replace-absent-output", [&](const fs::path &directory)
    {
      const auto input = Fixture(directory, original);
      const auto patched = havGamePatcher::OutputPath(input);
      Require(!fs::exists(patched), "Replacement fixture unexpectedly has an output.");
      Require(Read(havGamePatcher::Apply(input, true, ExistingOutput::replace)) == full,
              "Replacement mode cannot create a previously absent output.");
      Require(Read(input) == original, "Replacement mode changed its input.");
      RequireOnlyEntries(directory, {input, patched});
    });
    suite.Test("replace-unicode-path", [&](const fs::path &directory)
    {
      const auto nested = directory / L"Ren\u00e9 Spiele";
      fs::create_directory(nested);
      const auto input = Fixture(nested, original, L"H\u00f6llen.EXE");
      const auto patched = havGamePatcher::OutputPath(input);
      Write(patched, joystick);
      Require(Read(havGamePatcher::Apply(input, true, ExistingOutput::replace)) == full,
              "Unicode replacement changed the patch result.");
      Require(Read(input) == original, "Unicode replacement changed its input.");
      RequireOnlyEntries(nested, {input, patched});
    });
    suite.Test("replace-locked-output-preserved", [&](const fs::path &directory)
    {
      const auto input = Fixture(directory, original);
      const auto patched = havGamePatcher::OutputPath(input);
      const Bytes sentinel{'K', 'E', 'E', 'P'};
      Write(patched, sentinel);
      {
        FileHandle lock{CreateFileW(patched.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        Require(lock.value != INVALID_HANDLE_VALUE, "Could not lock replacement fixture.");
        Rejects([&] { havGamePatcher::Apply(input, true, ExistingOutput::replace); });
      }

      Require(Read(patched) == sentinel, "Failed replacement changed the locked output.");
      Require(Read(input) == original, "Locked-output rejection changed the input.");
      RequireOnlyEntries(directory, {input, patched});
    });
    suite.Test("replace-read-only-output-preserved", [&](const fs::path &directory)
    {
      const auto input = Fixture(directory, original);
      const auto patched = havGamePatcher::OutputPath(input);
      const Bytes sentinel{'K', 'E', 'E', 'P'};
      Write(patched, sentinel);
      const auto attributes = GetFileAttributesW(patched.c_str());
      Require(attributes != INVALID_FILE_ATTRIBUTES, "Cannot read fixture attributes.");
      FileAttributes restore{patched, attributes};
      Require(SetFileAttributesW(patched.c_str(), attributes | FILE_ATTRIBUTE_READONLY) != 0,
              "Cannot make the replacement fixture read-only.");
      Require((GetFileAttributesW(patched.c_str()) & FILE_ATTRIBUTE_READONLY) != 0,
              "Fixture lacks the Windows read-only attribute.");
      Rejects([&] { havGamePatcher::Apply(input, true, ExistingOutput::replace); });
      Require(Read(patched) == sentinel, "Failed replacement changed the read-only output.");
      Require((GetFileAttributesW(patched.c_str()) & FILE_ATTRIBUTE_READONLY) != 0,
              "Replacement removed the output's read-only attribute.");
      Require(Read(input) == original, "Read-only-output rejection changed the input.");
      RequireOnlyEntries(directory, {input, patched});
    });
    suite.Test("replace-output-directory-preserved", [&](const fs::path &directory)
    {
      const auto input = Fixture(directory, original);
      const auto patched = havGamePatcher::OutputPath(input);
      fs::create_directory(patched);
      const auto child = patched / "keep.txt";
      const Bytes sentinel{'K', 'E', 'E', 'P'};
      Write(child, sentinel);
      Rejects([&] { havGamePatcher::Apply(input, true, ExistingOutput::replace); });
      Require(fs::is_directory(patched) && Read(child) == sentinel,
              "Replacement modified the existing output directory.");
      Require(Read(input) == original, "Directory-output rejection changed the input.");
      RequireOnlyEntries(patched, {child});
      RequireOnlyEntries(directory, {input, patched});
    });
    suite.Test("replace-invalid-input-preserves-output", [&](const fs::path &directory)
    {
      const Bytes invalid{'M', 'Z'};
      const auto input = Fixture(directory, invalid);
      const auto patched = havGamePatcher::OutputPath(input);
      const Bytes sentinel{'K', 'E', 'E', 'P'};
      Write(patched, sentinel);
      Rejects([&] { havGamePatcher::Apply(input, true, ExistingOutput::replace); });
      Require(Read(patched) == sentinel, "Invalid replacement input changed the old output.");
      Require(Read(input) == invalid, "Rejected replacement changed its invalid input.");
      RequireOnlyEntries(directory, {input, patched});
    });
    suite.Test("replace-input-hard-link-rejected", [&](const fs::path &directory)
    {
      const auto input = Fixture(directory, original);
      const auto patched = havGamePatcher::OutputPath(input);
      fs::create_hard_link(input, patched);
      Require(fs::equivalent(input, patched), "Output fixture does not alias the input.");
      Rejects([&] { havGamePatcher::Apply(input, true, ExistingOutput::replace); });
      Require(Read(input) == original && Read(patched) == original,
              "Replacement modified an input hard link.");
      Require(fs::equivalent(input, patched), "Replacement separated the existing hard links.");
      RequireOnlyEntries(directory, {input, patched});
    });
    const auto fury3Retail = havGamePatcher::Inspect(inputs[9]);
    suite.Test("fury3-nonexact-joystick-compatibility", [&](const fs::path &directory)
    {
      auto changed = bytes[9];
      changed.back() ^= 0x80;
      const auto input = Fixture(directory, changed, L"FURY3.EXE");
      const auto item = havGamePatcher::Inspect(input);
      Require(item.GameId() == "fury3" && item.GameName() == "Fury\u00b3" && item.edition.empty(),
              "Nonexact Fury3 inherited an exact edition identity.");
      Require(item.game == havGamePatcher::FindGame("fury3"),
              "Fury3 inspection does not use its registered game.");
      Require(!item.fullscreenSupported && !item.fullscreenPatched && !item.CanApply(true),
              "Nonexact Fury3 offers an unverified fullscreen patch.");
      Require(!item.FullscreenDescription().empty() &&
                  item.FullscreenDescription() !=
                      havGamePatcher::Inspect(inputs[6]).FullscreenDescription(),
              "Fury3 inherited Hellbender's fullscreen description.");
      RejectFullscreen(input);
      auto expected = changed;
      std::copy(item.patch->replacement.begin(), item.patch->replacement.end(),
                expected.begin() + item.offset);
      const auto patched = havGamePatcher::Apply(input);
      const auto result = havGamePatcher::Inspect(patched);
      Require(Read(patched) == expected, "Nonexact Fury3 changed beyond the joystick fix.");
      Require(result.GameId() == item.GameId() && result.GameName() == item.GameName() &&
                  result.edition.empty() && result.joystickPatched && !result.fullscreenSupported &&
                  !result.CanApply(true),
              "Joystick patch changed nonexact Fury3 identity or fullscreen support.");
      Require(Read(input) == changed, "Nonexact Fury3 test changed its source.");
    });
    suite.Test("cross-game-signature-ambiguity-rejected", [&](const fs::path &directory)
    {
      const auto hellbender = havGamePatcher::Inspect(inputs[6]);
      auto mixedFury3 = bytes[9];
      mixedFury3.insert(mixedFury3.end(), hellbender.patch->original.begin(),
                        hellbender.patch->original.end());
      auto mixedHellbender = bytes[6];
      mixedHellbender.insert(mixedHellbender.end(), fury3Retail.patch->original.begin(),
                             fury3Retail.patch->original.end());
      const std::array mixedInputs{mixedFury3, mixedHellbender};

      for (std::size_t index = 0; index < mixedInputs.size(); ++index)
      {
        const auto input = directory / ("mixed-game-" + std::to_string(index) + ".exe");
        Write(input, mixedInputs[index]);
        Rejects([&] { havGamePatcher::Inspect(input); });
        Rejects([&] { havGamePatcher::Apply(input); });
        RejectFullscreen(input);
        Require(Read(input) == mixedInputs[index], "Ambiguous game input was modified.");
        Require(!fs::exists(havGamePatcher::OutputPath(input)),
                "Ambiguous game input produced a joystick output.");
      }
    });

    for (int argument = 14; argument < argc; ++argument)
    {
      const fs::path source(argv[argument]);
      const auto sourceBytes = Read(source);
      suite.Test("other-retail-" + std::to_string(argument - 13),
                 [&, sourceBytes, source](const fs::path &directory)
      {
        const auto input = Fixture(directory, sourceBytes);
        const auto item = havGamePatcher::Inspect(input);
        Require(item.patch && !item.joystickPatched && !item.fullscreenSupported,
                "Other retail build recognition changed.");
        RejectFullscreen(input);
        auto expected = sourceBytes;
        std::copy(item.patch->replacement.begin(), item.patch->replacement.end(),
                  expected.begin() + item.offset);
        const auto patched = havGamePatcher::Apply(input);
        Require(Read(patched) == expected, "Other retail build changed beyond joystick fix.");
        Require(Read(input) == sourceBytes && Read(source) == sourceBytes,
                "Other retail input was modified.");
        Require(!havGamePatcher::Inspect(patched).CanApply(),
                "Other retail duplicate patch remains available.");
      });
    }

    suite.Test("all-reference-inputs-unchanged", [&](const fs::path &)
    {
      for (std::size_t index = 0; index < inputs.size(); ++index)
      {
        Require(Read(inputs[index]) == bytes[index], "A supplied reference input was changed.");
      }
    });
    suite.Report();

    return suite.failures ? 1 : 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
