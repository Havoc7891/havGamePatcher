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
#include <bcrypt.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace havGamePatcher
{
  namespace
  {
    std::vector<std::size_t> FindAll(std::span<const Byte> bytes, std::span<const Byte> pattern)
    {
      std::vector<std::size_t> matches;

      if (pattern.empty())
      {
        return matches;
      }

      auto it = bytes.begin();

      while ((it = std::search(it, bytes.end(), pattern.begin(), pattern.end())) != bytes.end())
      {
        matches.push_back(static_cast<std::size_t>(it - bytes.begin()));
        ++it;
      }

      return matches;
    }

    std::array<Byte, 32> Sha256(std::span<const Byte> bytes)
    {
      struct Hash
      {
        BCRYPT_ALG_HANDLE algorithm{};
        BCRYPT_HASH_HANDLE handle{};

        ~Hash()
        {
          if (handle)
          {
            BCryptDestroyHash(handle);
          }

          if (algorithm)
          {
            BCryptCloseAlgorithmProvider(algorithm, 0);
          }
        }
      } hash;

      std::array<Byte, 32> digest{};

      if (bytes.size() > std::numeric_limits<ULONG>::max() ||
          BCryptOpenAlgorithmProvider(&hash.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
          BCryptCreateHash(hash.algorithm, &hash.handle, nullptr, 0, nullptr, 0, 0) < 0 ||
          BCryptHashData(hash.handle, const_cast<Byte *>(bytes.data()),
                         static_cast<ULONG>(bytes.size()), 0) < 0 ||
          BCryptFinishHash(hash.handle, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
      {
        throw std::runtime_error("Cannot verify the executable's SHA-256 fingerprint.");
      }

      return digest;
    }

    bool HasEmbeddedPatch(std::span<const Byte> bytes)
    {
      const auto requireRange = [&](std::size_t offset, std::size_t size)
      {
        if (offset > bytes.size() || size > bytes.size() - offset)
        {
          throw std::runtime_error("Truncated or invalid Windows executable.");
        }
      };

      const auto word = [&](std::size_t offset) -> std::uint16_t
      {
        requireRange(offset, 2);
        return bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
      };

      const auto dword = [&](std::size_t offset) -> std::uint32_t
      {
        requireRange(offset, 4);
        return bytes[offset] | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
               (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
               (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
      };

      const std::size_t pe = dword(0x3C);
      requireRange(pe, 24);

      if (pe < 0x40 || dword(pe) != 0x00004550 || word(pe + 4) != 0x14C)
      {
        throw std::runtime_error("Unsupported Windows executable format.");
      }

      const auto count = word(pe + 6);
      const auto optionalSize = word(pe + 20);
      const auto optional = pe + 24;
      requireRange(optional, optionalSize);

      if (!count || count > 96 || optionalSize < 96 || word(optional) != 0x10B)
      {
        throw std::runtime_error("Invalid x86 executable headers.");
      }

      const auto table = optional + optionalSize;
      requireRange(table, static_cast<std::size_t>(count) * 40);

      for (std::size_t i = 0; i < count; ++i)
      {
        const auto section = table + i * 40;
        const auto rawSize = dword(section + 16);

        if (rawSize)
        {
          requireRange(dword(section + 20), rawSize);
        }

        const std::string_view name(reinterpret_cast<const char *>(bytes.data() + section), 8);

        for (const auto *game : Games())
        {
          for (const auto &marker : game->embeddedPatchSections)
          {
            if (marker.match == SectionMatch::prefix ? name.starts_with(marker.name)
                                                     : name == marker.name)
            {
              return true;
            }
          }
        }
      }

      return false;
    }

    void ApplyFullscreen(std::vector<Byte> &bytes, const FullscreenRecipe &recipe)
    {
      if (bytes.size() != recipe.originalSize ||
          recipe.patchedSize != recipe.originalSize + recipe.appendBytes.size())
      {
        throw std::runtime_error("Fullscreen patch size verification failed.");
      }

      for (const auto &span : recipe.spans)
      {
        if (span.offset > bytes.size() || span.size > bytes.size() - span.offset ||
            span.dataOffset > recipe.patchBytes.size() ||
            span.size > recipe.patchBytes.size() - span.dataOffset)
        {
          throw std::runtime_error("Invalid embedded fullscreen patch data.");
        }

        std::copy_n(recipe.patchBytes.begin() + span.dataOffset, span.size,
                    bytes.begin() + span.offset);
      }

      bytes.insert(bytes.end(), recipe.appendBytes.begin(), recipe.appendBytes.end());

      if (Sha256(bytes) != recipe.patchedSha256)
      {
        throw std::runtime_error(
            "Fullscreen patch output verification failed. No file was created.");
      }
    }

    void
    WriteOutput(const fs::path &output, std::span<const Byte> bytes, ExistingOutput existingOutput)
    {
      struct Output
      {
        HANDLE handle{INVALID_HANDLE_VALUE};
        bool complete{};

        ~Output()
        {
          if (handle == INVALID_HANDLE_VALUE)
          {
            return;
          }

          if (!complete)
          {
            FILE_DISPOSITION_INFO disposition{TRUE};
            SetFileInformationByHandle(handle, FileDispositionInfo, &disposition,
                                       sizeof(disposition));
          }

          CloseHandle(handle);
        }
      } file;

      // Keep the completed temporary file open until the rename commits it.
      // No write or truncate operation ever targets the existing executable.
      for (int attempt = 0; attempt < 16; ++attempt)
      {
        std::array<Byte, 16> random{};

        if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        {
          throw std::runtime_error("Cannot create a temporary output filename.");
        }

        constexpr auto digits = L"0123456789abcdef";
        std::wstring name = L".havGamePatcher-";

        for (const auto value : random)
        {
          name += digits[value >> 4];
          name += digits[value & 0xF];
        }

        const auto temporary = output.parent_path() / (name + L".tmp");
        file.handle = CreateFileW(temporary.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.handle != INVALID_HANDLE_VALUE)
        {
          break;
        }

        const auto error = GetLastError();

        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
        {
          throw std::runtime_error("Cannot create a temporary executable in the game folder.");
        }
      }

      if (file.handle == INVALID_HANDLE_VALUE)
      {
        throw std::runtime_error("Cannot create a unique temporary output file.");
      }

      while (!bytes.empty())
      {
        const auto count = static_cast<DWORD>(
            std::min<std::size_t>(bytes.size(), std::numeric_limits<DWORD>::max()));
        DWORD written{};

        if (!WriteFile(file.handle, bytes.data(), count, &written, nullptr) || !written)
        {
          throw std::runtime_error("Failed to write output executable.");
        }

        bytes = bytes.subspan(written);
      }

      if (!FlushFileBuffers(file.handle))
      {
        throw std::runtime_error("Failed to save output executable.");
      }

      const auto filename = fs::absolute(output).wstring();
      const auto nameBytes = filename.size() * sizeof(wchar_t);
      const auto infoBytes = sizeof(FILE_RENAME_INFO) + nameBytes;

      if (infoBytes > std::numeric_limits<DWORD>::max())
      {
        throw std::runtime_error("Output filename is too long.");
      }

      std::vector<Byte> storage(infoBytes);
      auto *rename = reinterpret_cast<FILE_RENAME_INFO *>(storage.data());
      rename->ReplaceIfExists = existingOutput == ExistingOutput::replace;
      rename->FileNameLength = static_cast<DWORD>(nameBytes);

      std::copy(filename.begin(), filename.end(), rename->FileName);

      if (!SetFileInformationByHandle(file.handle, FileRenameInfo, rename,
                                      static_cast<DWORD>(storage.size())))
      {
        const auto error = GetLastError();

        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
        {
          throw std::runtime_error("Output already exists. Replacement was not confirmed.");
        }

        if (existingOutput == ExistingOutput::replace)
        {
          throw std::runtime_error("Cannot replace the patched executable. Close the game and "
                                   "check that the file is writable. The existing file was kept.");
        }

        throw std::runtime_error("Cannot save the patched executable.");
      }

      file.complete = true;
    }
  } // namespace

  std::string_view Inspection::GameId() const
  {
    return game ? game->id : std::string_view{};
  }

  std::string_view Inspection::GameName() const
  {
    const auto *recipe = FindFullscreenRecipe(GameId(), edition);
    return recipe ? recipe->name : patch ? patch->name : game ? game->name : std::string_view{};
  }

  std::string_view Inspection::FullscreenDescription() const
  {
    return game ? game->fullscreenDescription : std::string_view{};
  }

  bool Inspection::CanApply(bool includeFullscreen) const
  {
    if (!game || !patch || (includeFullscreen && !fullscreenSupported))
    {
      return false;
    }

    return !joystickPatched || (includeFullscreen && !fullscreenPatched);
  }

  Inspection Inspect(const fs::path &input)
  {
    std::ifstream in(input, std::ios::binary);

    if (!in)
    {
      throw std::runtime_error("Cannot open executable.");
    }

    Inspection result;
    result.bytes.assign(std::istreambuf_iterator<char>(in), {});

    if (in.bad())
    {
      throw std::runtime_error("Cannot read executable.");
    }

    if (result.bytes.size() < 2 || result.bytes[0] != 'M' || result.bytes[1] != 'Z')
    {
      throw std::runtime_error("Not a Windows MZ executable.");
    }

    const bool embeddedPatch = HasEmbeddedPatch(result.bytes);

    for (const auto *game : Games())
    {
      for (const auto &patch : game->joystickPatches)
      {
        const auto originals = FindAll(result.bytes, patch.original);
        const auto replacements = FindAll(result.bytes, patch.replacement);

        if (originals.empty() && replacements.empty())
        {
          continue;
        }

        if (result.patch || originals.size() + replacements.size() != 1)
        {
          throw std::runtime_error("Ambiguous patch signature. No changes made.");
        }

        result.game = game;
        result.patch = &patch;
        result.joystickPatched = originals.empty();
        result.offset = result.joystickPatched ? replacements.front() : originals.front();
      }
    }

    if (!result.game || !result.patch)
    {
      throw std::runtime_error("Unsupported executable or unknown version.");
    }

    // An edition fingerprint is meaningful only within its identified game
    const auto digest = Sha256(result.bytes);

    for (const auto &recipe : result.game->fullscreenRecipes)
    {
      if (digest != recipe.originalSha256 && digest != recipe.joystickSha256 &&
          digest != recipe.patchedSha256)
      {
        continue;
      }

      if (result.fullscreenSupported)
      {
        throw std::runtime_error("Ambiguous fullscreen recipe. No changes made.");
      }

      result.edition = recipe.edition;
      result.fullscreenPatched = digest == recipe.patchedSha256;
      result.fullscreenSupported = true;
    }

    if (embeddedPatch && !result.fullscreenPatched)
    {
      throw std::runtime_error(
          "Older, diagnostic or modified patch build. Select the original executable "
          "or a copy with only the joystick fix.");
    }

    return result;
  }

  fs::path OutputPath(const fs::path &input)
  {
    return input.parent_path() /
           (input.stem().wstring() + L".patched" + input.extension().wstring());
  }

  fs::path Apply(const fs::path &input, bool includeFullscreen, ExistingOutput existingOutput)
  {
    auto inspection = Inspect(input);

    if (includeFullscreen && !inspection.fullscreenSupported)
    {
      throw std::runtime_error("The fullscreen fix is not available for this executable.");
    }

    if (!inspection.CanApply(includeFullscreen))
    {
      throw std::runtime_error("The selected fixes are already applied.");
    }

    if (includeFullscreen)
    {
      const auto *recipe = FindFullscreenRecipe(inspection.GameId(), inspection.edition);

      if (!recipe)
      {
        throw std::runtime_error("The fullscreen fix is not available for this executable.");
      }

      ApplyFullscreen(inspection.bytes, *recipe);
    }
    else
    {
      std::copy(inspection.patch->replacement.begin(), inspection.patch->replacement.end(),
                inspection.bytes.begin() + inspection.offset);
    }

    const auto output = OutputPath(input);

    if (fs::exists(output))
    {
      if (!fs::is_regular_file(output))
      {
        throw std::runtime_error("The output path is not a regular file.");
      }

      if (existingOutput == ExistingOutput::reject)
      {
        throw std::runtime_error("Output already exists. Replacement was not confirmed.");
      }

      if (fs::equivalent(input, output))
      {
        throw std::runtime_error(
            "The output points to the original executable. It cannot be replaced.");
      }
    }

    WriteOutput(output, inspection.bytes, existingOutput);

    return output;
  }
} // namespace havGamePatcher
