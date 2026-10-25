// SPDX-License-Identifier: MIT

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using Bytes = std::vector<unsigned char>;
using PollFunction = void(__cdecl *)();
static DWORD originalCalls;
static DWORD savedStack;
static DWORD fullscreenStateRva;

__declspec(naked) static DWORD InvokeWithAbi(PollFunction)
{
  __asm
  {
        push ebp
        push ebx
        push esi
        push edi
        mov eax, dword ptr [esp+20]
        mov savedStack, esp
        mov ebp, 13579bdfh
        mov ebx, 2468ace0h
        mov esi, 31415926h
        mov edi, 27182818h
        call eax
        xor edx, edx
        cmp esp, savedStack
        je StackOkay
        or edx, 1
        mov esp, savedStack
    StackOkay:
        cmp ebp, 13579bdfh
        je FrameOkay
        or edx, 2
    FrameOkay:
        cmp ebx, 2468ace0h
        je BaseOkay
        or edx, 4
    BaseOkay:
        cmp esi, 31415926h
        je SourceOkay
        or edx, 8
    SourceOkay:
        cmp edi, 27182818h
        je DestinationOkay
        or edx, 16
    DestinationOkay:
        mov eax, edx
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
  }
}

struct Check
{
  std::string name;
  bool passed;
};

static std::vector<Check> checks;

static void Expect(const std::string &name, bool passed)
{
  checks.push_back({name, passed});
  std::printf("%s %s\n", passed ? "PASS" : "FAIL", name.c_str());
}

static void Require(bool condition, const char *message)
{
  if (!condition)
  {
    throw std::runtime_error(message);
  }
}

#include "../../support/mappedImage.hpp"

struct InputImage : MappedImage
{
  explicit InputImage(const char *file) : MappedImage(file, true, 0x400000) {}

  DWORD &Global(DWORD rva) const
  {
    Require(rva + sizeof(DWORD) <= imageSize, "Fixture global outside candidate.");

    return *reinterpret_cast<DWORD *>(base + rva);
  }

  DWORD JumpTarget(DWORD rva) const
  {
    Require(rva + 5 < imageSize && base[rva] == 0xe9,
            "Candidate does not contain the expected tail jump.");
    const auto displacement = *reinterpret_cast<const int32_t *>(base + rva + 1);
    const int64_t target = static_cast<int64_t>(rva) + 5 + displacement;
    Require(target >= 0 && target < imageSize, "Tail jump target is outside the candidate.");

    return static_cast<DWORD>(target);
  }

  bool PayloadCode(DWORD rva) const
  {
    for (const auto &section : sections)
    {
      if (std::memcmp(section.Name, ".fgpft", 6) == 0 &&
          (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0 && rva >= section.VirtualAddress &&
          rva < section.VirtualAddress + section.Misc.VirtualSize)
      {
        return true;
      }
    }

    return false;
  }
};

static void TestPoll(InputImage &image,
                     DWORD entryRva,
                     const char *route,
                     DWORD unavailable,
                     DWORD enabled,
                     DWORD fullscreen)
{
  constexpr DWORD buttonsRva = 0xebcb0;
  constexpr DWORD rawFallbackRva = 0x13cb4c;
  constexpr DWORD enabledRva = 0x13e728;
  const std::string name = std::string(route) + "-raw" + std::to_string(unavailable) + "-enabled" +
                           std::to_string(enabled) + "-fullscreen" + std::to_string(fullscreen);
  image.Global(fullscreenStateRva + sizeof(DWORD)) = fullscreen;
  image.Global(rawFallbackRva) = unavailable;
  image.Global(enabledRva) = enabled;
  image.Global(buttonsRva) = 0xa5c33c5a;
  Bytes expected(image.base, image.base + image.imageSize);

  if (unavailable)
  {
    *reinterpret_cast<DWORD *>(expected.data() + buttonsRva) = 0;
    *reinterpret_cast<DWORD *>(expected.data() + enabledRva) = 0;
  }

  originalCalls = 0;
  const auto poll = reinterpret_cast<PollFunction>(image.base + entryRva);
  const DWORD abi = InvokeWithAbi(poll);
  Expect(name + "-x86-abi", abi == 0);
  Expect(name + "-original-call-count", originalCalls == (unavailable ? 0u : 1u));
  Expect(name + "-exact-game-memory-effects",
         std::memcmp(image.base, expected.data(), expected.size()) == 0);
}

int main(int argc, char **argv)
{
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

  if (argc != 5)
  {
    std::fprintf(stderr,
                 "Usage: inputHookTests.exe candidate.exe configRva stateRva report.json\n");

    return 2;
  }

  try
  {
    InputImage image(argv[1]);
    const DWORD configRva = std::stoul(argv[2], nullptr, 0);
    fullscreenStateRva = std::stoul(argv[3], nullptr, 0);
    constexpr DWORD firstHook = 0x1d2a;
    constexpr DWORD secondHook = 0x14e95;
    constexpr DWORD originalPoll = 0x14c70;
    const DWORD firstTarget = image.JumpTarget(firstHook);
    const DWORD secondTarget = image.JumpTarget(secondHook);
    Expect("both-actual-hooks-tail-jump-to-one-guard", firstTarget == secondTarget);
    Require(firstTarget == secondTarget, "Input hooks do not target the same guard.");
    Expect("guard-target-is-executable-payload", image.PayloadCode(firstTarget));
    Require(image.PayloadCode(firstTarget), "Input guard target is not payload code.");
    Expect("guard-target-is-not-original-poll-or-hooks",
           firstTarget != originalPoll && firstTarget != firstHook && firstTarget != secondHook);
    Expect("original-poll-remains-unhooked", image.base[originalPoll] != 0xe9);
    Require(image.Global(configRva) == 0x46534650 && image.Global(configRva + 4) == 1 &&
                image.Global(configRva + 8) == reinterpret_cast<DWORD>(image.base),
            "Candidate config differs from expected transplanted layout.");

    // Only this owned memory mapping is changed. The actual game routine
    // becomes an observable no-op, so valid-path tests never poll hardware.
    unsigned char stub[] = {0xff, 0x05, 0, 0, 0, 0, 0xc3};
    *reinterpret_cast<DWORD *>(stub + 2) = reinterpret_cast<DWORD>(&originalCalls);
    std::memcpy(image.base + originalPoll, stub, sizeof(stub));
    FlushInstructionCache(GetCurrentProcess(), image.base + originalPoll, sizeof(stub));

    const DWORD entries[] = {firstHook, secondHook, firstTarget};
    const char *names[] = {"general-thunk", "button-tail-jump", "guard-direct"};

    for (unsigned entry = 0; entry < 3; ++entry)
    {
      for (DWORD fullscreen : {0u, 1u})
      {
        for (DWORD enabled : {0u, 1u})
        {
          TestPoll(image, entries[entry], names[entry], 1, enabled, fullscreen);
          TestPoll(image, entries[entry], names[entry], 0, enabled, fullscreen);
        }

        TestPoll(image, entries[entry], names[entry], 0xffffffff, 1, fullscreen);
      }
    }

    image.Global(0x13cb4c) = 0;
    originalCalls = 0;
    bool repeatedAbi = true;

    for (unsigned index = 0; index < 256; ++index)
    {
      repeatedAbi =
          repeatedAbi &&
          InvokeWithAbi(reinterpret_cast<PollFunction>(image.base + entries[index % 2])) == 0;
    }

    Expect("repeated-hooks-forward-once-without-recursion", repeatedAbi && originalCalls == 256);

    size_t failed = 0;

    for (const auto &check : checks)
    {
      if (!check.passed)
      {
        ++failed;
      }
    }

    FILE *report = nullptr;
    fopen_s(&report, argv[4], "wb");
    Require(report != nullptr, "Cannot write fixture report.");
    std::fprintf(report,
                 "{\n  \"format\": \"Fury3InputGuardMappedCandidateFixture\",\n  \"passed\": %s,\n "
                 " \"checksPassed\": %zu,\n  \"checksTotal\": %zu,\n  \"gameEntryPointCalled\": "
                 "false,\n  \"gameProcessLaunched\": false,\n  \"joystickApiCalled\": false,\n  "
                 "\"originalRoutineStubbedOnlyInOwnedMemory\": true,\n  \"guardRva\": \"0x%x\",\n  "
                 "\"checks\": [\n",
                 failed ? "false" : "true", checks.size() - failed, checks.size(), firstTarget);

    for (size_t index = 0; index < checks.size(); ++index)
    {
      std::fprintf(report, "    {\"name\": \"%s\", \"passed\": %s}%s\n", checks[index].name.c_str(),
                   checks[index].passed ? "true" : "false", index + 1 < checks.size() ? "," : "");
    }

    std::fprintf(report, "  ],\n  \"limitations\": [\"No real joystick hardware exercised\", \"No "
                         "game startup, input interaction, cutscene or mission executed\"]\n}\n");
    std::fclose(report);
    std::printf("RESULT %zu/%zu input checks passed\n", checks.size() - failed, checks.size());

    return failed ? 1 : 0;
  }
  catch (const std::exception &error)
  {
    std::fprintf(stderr, "%s\n", error.what());

    return 2;
  }
}
