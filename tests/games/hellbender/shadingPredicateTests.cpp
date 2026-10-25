// SPDX-License-Identifier: MIT

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using Bytes = std::vector<unsigned char>;

static void Require(bool value, const char *message)
{
  if (!value)
  {
    throw std::runtime_error(message);
  }
}

#include "../../support/mappedImage.hpp"

struct Registers
{
  DWORD eax, ebx, ecx, edx, esi, edi, ebp, esp;
};

static Registers input, actual;
static DWORD hostStack, entry, route, actualFlags;

__declspec(naked) static void Sink()
{
  __asm {
        mov actual.eax, eax
        mov actual.ebx, ebx
        mov actual.ecx, ecx
        mov actual.edx, edx
        mov actual.esi, esi
        mov actual.edi, edi
        mov actual.ebp, ebp
        mov actual.esp, esp
        pushfd
        pop actualFlags
        mov esp, hostStack
        popad
        popfd
        ret
  }
}

__declspec(naked) static void Emit()
{
  __asm { mov route, 1 }
  __asm
  {
    jmp Sink
  }
}

__declspec(naked) static void Skip()
{
  __asm { mov route, 2 }
  __asm
  {
    jmp Sink
  }
}

__declspec(naked) static void Fallback()
{
  __asm { mov route, 3 }
  __asm
  {
    jmp Sink
  }
}

__declspec(naked) static void Invoke()
{
  __asm {
        pushfd
        pushad
        mov hostStack, esp
        mov esp, input.esp
        mov eax, input.eax
        mov ebx, input.ebx
        mov ecx, input.ecx
        mov edx, input.edx
        mov esi, input.esi
        mov edi, input.edi
        mov ebp, input.ebp
        jmp entry
  }
}

struct ReturnSite
{
  DWORD stackOffset, rva;
};

struct Layout
{
  DWORD preferredBase, hook, emit, skip;
  DWORD sourceOffset, countOffset, flagsOffset, stride, fogOffset;
  DWORD needleCount, needleFlags, backgroundMinimum, backgroundMaximum, backgroundFlags;
  std::vector<ReturnSite> needleReturns, backgroundReturns;
};

static DWORD ReadNumber(std::istream &stream)
{
  std::string token;
  Require(static_cast<bool>(stream >> token), "Incomplete fixture layout.");
  size_t consumed = 0;
  const auto value = std::stoull(token, &consumed, 0);
  Require(consumed == token.size() && value <= std::numeric_limits<DWORD>::max(),
          "Invalid fixture layout number.");
  return static_cast<DWORD>(value);
}

static std::vector<ReturnSite> ReadReturns(std::istream &stream, DWORD expectedCount)
{
  Require(ReadNumber(stream) == expectedCount, "Unexpected return-chain length.");
  std::vector<ReturnSite> sites;

  for (DWORD index = 0; index < expectedCount; ++index)
  {
    const DWORD offset = ReadNumber(stream), rva = ReadNumber(stream);
    Require(offset >= 0x30 && offset <= 0xc0 && offset % 4 == 0 && rva > 0,
            "Invalid return-chain site.");
    sites.push_back({offset, rva});
  }

  return sites;
}

static Layout ReadLayout(const char *file)
{
  std::ifstream stream(file);
  Require(static_cast<bool>(stream), "Cannot read fixture layout.");
  Require(ReadNumber(stream) == 0x48475354 && ReadNumber(stream) == 1,
          "Unknown fixture layout format.");
  Layout value{};
  value.preferredBase = ReadNumber(stream);
  value.hook = ReadNumber(stream);
  value.emit = ReadNumber(stream);
  value.skip = ReadNumber(stream);
  value.sourceOffset = ReadNumber(stream);
  value.countOffset = ReadNumber(stream);
  value.flagsOffset = ReadNumber(stream);
  value.stride = ReadNumber(stream);
  value.fogOffset = ReadNumber(stream);
  value.needleCount = ReadNumber(stream);
  value.needleFlags = ReadNumber(stream);
  value.backgroundMinimum = ReadNumber(stream);
  value.backgroundMaximum = ReadNumber(stream);
  value.backgroundFlags = ReadNumber(stream);
  value.needleReturns = ReadReturns(stream, 6);
  value.backgroundReturns = ReadReturns(stream, 5);
  std::string extra;
  Require(!(stream >> extra), "Unexpected fixture layout fields.");
  Require(value.preferredBase == 0x400000 && value.sourceOffset == 0x34 &&
              value.countOffset == 0x38 && value.flagsOffset == 0x3c && value.stride == 36 &&
              value.fogOffset == 32 && value.needleCount == 4 && value.needleFlags == 0x10 &&
              value.backgroundMinimum == 3 && value.backgroundMaximum == 8 &&
              value.backgroundFlags == 0x10,
          "The reviewed predicate contract changed. Update the case matrix explicitly.");
  return value;
}

struct Case
{
  std::string name;
  DWORD count = 4, flags = 0x10, cache = 1, rawShade = 0xffffffff;
  int wrongReturn = -1;
  int fogIndex = -1;
  DWORD fog = 0;
  bool badSource = false;
  bool expectedMatch = true;
};

static std::vector<std::pair<std::string, bool>> checks;

static void Expect(const std::string &name, bool passed)
{
  checks.push_back({name, passed});
  std::printf("%s %s\n", passed ? "PASS" : "FAIL", name.c_str());
}

static void Redirect(BYTE *from, const void *target)
{
  from[0] = 0xe9;
  *reinterpret_cast<DWORD *>(from + 1) =
      reinterpret_cast<DWORD>(target) - reinterpret_cast<DWORD>(from) - 5;
}

static std::vector<Case> BackgroundCases()
{
  std::vector<Case> result;

  for (DWORD count = 3; count <= 8; ++count)
  {
    const std::string prefix = "count-" + std::to_string(count) + "/";

    for (DWORD cache : {0u, 1u})
    {
      for (DWORD rawShade : {0u, 1u, 0xffffffffu})
      {
        Case test{prefix + "match-cache-" + std::to_string(cache) + "-raw-" +
                  std::to_string(rawShade)};
        test.count = count;
        test.cache = cache;
        test.rawShade = rawShade;
        result.push_back(test);
      }
    }

    for (int index = 0; index < static_cast<int>(count); ++index)
    {
      for (DWORD fog : {1u, 0xffffffffu})
      {
        Case test{prefix + "fog-" + std::to_string(index) + "-" + std::to_string(fog)};
        test.count = count;
        test.fogIndex = index;
        test.fog = fog;
        test.expectedMatch = false;
        result.push_back(test);
      }
    }

    for (int index = 0; index < 5; ++index)
    {
      Case test{prefix + "return-" + std::to_string(index)};
      test.count = count;
      test.wrongReturn = index;
      test.expectedMatch = false;
      result.push_back(test);
      test.name += "-invalid-source";
      test.badSource = true;
      result.push_back(test);
    }

    for (DWORD flags : {0u, 1u, 0x11u, 0x14u, 0x23u, 0xffffffffu})
    {
      Case test{prefix + "flags-" + std::to_string(flags)};
      test.count = count;
      test.flags = flags;
      test.expectedMatch = false;
      result.push_back(test);
      test.name += "-invalid-source";
      test.badSource = true;
      result.push_back(test);
    }
  }

  for (DWORD count : {0u, 1u, 2u, 9u, 0x80000000u, 0xffffffffu})
  {
    Case test{"invalid-count-" + std::to_string(count)};
    test.count = count;
    test.expectedMatch = false;
    test.badSource = true;
    result.push_back(test);
  }

  return result;
}

static std::vector<Case> NeedleCases()
{
  std::vector<Case> cases;
  cases.push_back({"needle-cached-flat"});
  Case test{"needle-cached-gouraud"};
  test.cache = 0;
  cases.push_back(test);
  test = {"needle-raw-shade-zero"};
  test.rawShade = 0;
  cases.push_back(test);

  for (int i = 0; i < 6; ++i)
  {
    test = {"return-mismatch-" + std::to_string(i)};
    test.wrongReturn = i;
    test.expectedMatch = false;
    cases.push_back(test);
    test.name += "-invalid-source";
    test.badSource = true;
    cases.push_back(test);
  }

  for (int i = 0; i < 4; ++i)
  {
    for (DWORD fog : {1u, 0xffffffffu})
    {
      test = {"fog-mismatch-" + std::to_string(i) + "-" + std::to_string(fog)};
      test.fogIndex = i;
      test.fog = fog;
      test.expectedMatch = false;
      cases.push_back(test);
    }
  }

  for (DWORD count : {0u, 3u, 5u, 0xffffffffu})
  {
    test = {"count-mismatch-" + std::to_string(count)};
    test.count = count;
    test.expectedMatch = false;
    test.badSource = count != 3;
    cases.push_back(test);
  }

  for (DWORD flags : {0u, 1u, 0x11u, 0x14u, 0x23u, 0xffffffffu})
  {
    test = {"flags-mismatch-" + std::to_string(flags)};
    test.flags = flags;
    test.expectedMatch = false;
    test.badSource = true;
    cases.push_back(test);
  }

  for (DWORD cache : {0u, 1u})
  {
    test = {"legacy-crosshair-cache-" + std::to_string(cache)};
    test.count = 3;
    test.cache = cache;
    test.expectedMatch = false;
    cases.push_back(test);
    test.name += "-positive-fog";
    test.fogIndex = 1;
    test.fog = 1;
    cases.push_back(test);
    test.name += "-negative-fog";
    test.fog = 0xffffffff;
    cases.push_back(test);
  }

  return cases;
}

static void Run(const char *candidate, const Layout &layout, bool firstBase, bool background)
{
  MappedImage image(candidate, firstBase, layout.preferredBase);
  DWORD shading = 0;

  for (const auto &section : image.sections)
  {
    if (std::memcmp(section.Name, ".hgcsh\0\0", 8) == 0)
    {
      Require(shading == 0 && section.SizeOfRawData == 1024 &&
                  section.Characteristics == 0x60000020,
              "Unexpected maintained shading section.");
      shading = section.VirtualAddress;
    }
  }

  Require(shading != 0 && shading + 1024 <= image.imageSize, "Maintained shading section missing.");
  const DWORD fallback = shading + (background ? 0x140 : 0);
  const DWORD hook = layout.hook;
  Require(hook + 6 <= image.imageSize && layout.emit + 5 <= image.imageSize &&
              layout.skip + 5 <= image.imageSize,
          "Native continuation outside the image.");
  Require(image.base[hook] == 0xe9 && image.base[hook + 5] == 0x90,
          "Hook is not expected six-byte jump.");
  const DWORD dispatcher = hook + 5 + *reinterpret_cast<DWORD *>(image.base + hook + 1);
  Require(dispatcher == shading + 0x210, "Hook does not reach maintained background dispatcher.");
  entry = reinterpret_cast<DWORD>(image.base + hook);
  Redirect(image.base + layout.emit, reinterpret_cast<const void *>(&Emit));
  Redirect(image.base + layout.skip, reinterpret_cast<const void *>(&Skip));
  BYTE fallbackEntry[5];
  std::memcpy(fallbackEntry, image.base + fallback, sizeof(fallbackEntry));
  auto allocation =
      static_cast<BYTE *>(VirtualAlloc(nullptr, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  Require(allocation != nullptr, "Source allocation failed.");
  DWORD oldProtection;
  Require(VirtualProtect(allocation + 0x1000, 0x1000, PAGE_NOACCESS, &oldProtection) != FALSE,
          "Guard page setup failed.");
  alignas(16) BYTE stackStorage[1024];
  BYTE *stack = stackStorage + 128;
  const auto cases = background ? BackgroundCases() : NeedleCases();
  const auto &returns = background ? layout.backgroundReturns : layout.needleReturns;

  for (bool interceptFallback : {true, false})
  {
    if (interceptFallback)
    {
      Redirect(image.base + fallback, reinterpret_cast<const void *>(&Fallback));
    }
    else
    {
      std::memcpy(image.base + fallback, fallbackEntry, sizeof(fallbackEntry));
    }

    FlushInstructionCache(GetCurrentProcess(), image.base, image.imageSize);

    for (const auto &current : cases)
    {
      // The accepted legacy triangle policy legitimately reads source fog.
      // Only the new guard is exercised with an invalid triangle pointer.
      if (!interceptFallback && current.badSource && current.count == 3 && !(current.flags & 5))
      {
        continue;
      }

      const DWORD allocatedCount = current.count >= 3 && current.count <= 8 ? current.count : 8;
      const DWORD sourceSize = allocatedCount * layout.stride;
      BYTE *source = allocation + 0x1000 - sourceSize;
      std::memset(allocation, 0x69, 0x1000);
      std::memset(source, 0, sourceSize);
      std::memset(stackStorage, 0xa5, sizeof(stackStorage));

      if (current.fogIndex >= 0)
      {
        *reinterpret_cast<DWORD *>(source + layout.fogOffset + current.fogIndex * layout.stride) =
            current.fog;
      }

      *reinterpret_cast<DWORD *>(stack + layout.sourceOffset) =
          current.badSource ? 1 : reinterpret_cast<DWORD>(source);
      *reinterpret_cast<DWORD *>(stack + layout.countOffset) = current.count;
      *reinterpret_cast<DWORD *>(stack + layout.flagsOffset) = current.flags;

      for (size_t index = 0; index < returns.size(); ++index)
      {
        *reinterpret_cast<DWORD *>(stack + returns[index].stackOffset) =
            reinterpret_cast<DWORD>(image.base) + returns[index].rva +
            (static_cast<int>(index) == current.wrongReturn ? 1 : 0);
      }

      input = {current.cache,
               1,
               current.badSource ? 1 : reinterpret_cast<DWORD>(source),
               0x13579bdf,
               0x2468ace0,
               0x31415926,
               current.rawShade,
               reinterpret_cast<DWORD>(stack)};
      Registers expected = input;
      DWORD expectedRoute;

      if (current.expectedMatch)
      {
        expected.ebp = 0;
        expectedRoute = current.cache == 0 ? 2 : 1;
      }
      else if (interceptFallback)
      {
        expectedRoute = 3;
      }
      else
      {
        expected.ebp = 0 - current.rawShade;

        if (!(current.flags & 5) && current.count == 3 &&
            !(current.fogIndex >= 0 && current.fogIndex < 3 && static_cast<LONG>(current.fog) > 0))
        {
          expected.ebp = 0;
        }

        expectedRoute = expected.ebp == current.cache ? 2 : 1;
      }

      BYTE sourceBefore[0x1000], stackBefore[512];
      std::memcpy(sourceBefore, allocation, sizeof(sourceBefore));
      std::memcpy(stackBefore, stack, sizeof(stackBefore));
      Require(VirtualProtect(allocation, 0x1000, PAGE_READONLY, &oldProtection) != FALSE,
              "Read-only source setup failed.");
      actual = {};
      route = 0;
      actualFlags = 0;
      Invoke();
      Require(VirtualProtect(allocation, 0x1000, PAGE_READWRITE, &oldProtection) != FALSE,
              "Source protection restore failed.");
      const bool flagsValid =
          !current.expectedMatch || ((actualFlags & 0xc1) == (current.cache == 0 ? 0x40u : 0x81u));
      const std::string name = std::string(background ? "background/" : "needle/") +
                               (firstBase ? "rebased20000000/" : "rebased30000000/") +
                               (interceptFallback ? "predicate/" : "accepted-fallback/") +
                               current.name;
      Expect(name, route == expectedRoute && std::memcmp(&actual, &expected, sizeof(actual)) == 0 &&
                       flagsValid &&
                       std::memcmp(allocation, sourceBefore, sizeof(sourceBefore)) == 0 &&
                       std::memcmp(stack, stackBefore, sizeof(stackBefore)) == 0);
    }
  }

  VirtualFree(allocation, 0, MEM_RELEASE);
}

int main(int argc, char **argv)
{
  if (argc != 4)
  {
    return 2;
  }

  try
  {
    const auto layout = ReadLayout(argv[2]);

    for (bool background : {true, false})
    {
      Run(argv[1], layout, true, background);
      Run(argv[1], layout, false, background);
    }

    size_t failed = 0;

    for (const auto &check : checks)
    {
      if (!check.second)
      {
        ++failed;
      }
    }

    std::ofstream report(argv[3]);
    Require(static_cast<bool>(report), "Cannot create fixture report.");
    report << "{\n  \"passed\": " << (failed ? "false" : "true")
           << ",\n  \"checks\": " << checks.size() << ",\n  \"failed\": " << failed
           << ",\n  \"gameLaunched\": false,\n  \"gameAttached\": false,\n  "
              "\"candidateMappedOnly\": true,\n  \"sourceReadOnlyAndGuarded\": true\n}\n";
    report.close();
    Require(static_cast<bool>(report), "Cannot complete fixture report.");
    std::printf("RESULT %zu checks, %zu failed\n", checks.size(), failed);

    return failed ? 1 : 0;
  }
  catch (const std::exception &)
  {
    // File errors and parser exceptions can contain input paths
    std::printf("Fixture failed before completing its report.\n");

    return 2;
  }
}
