// SPDX-License-Identifier: MIT

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using Bytes = std::vector<unsigned char>;
static unsigned Checks = 0;

static void Require(bool value, const char *message)
{
  if (!value)
  {
    throw std::runtime_error(message);
  }
}

static void Check(bool value, const char *message)
{
  Require(value, message);
  ++Checks;
}

#include "../../support/mappedImage.hpp"
#include "layoutPayload.hpp"
static HMODULE PayloadModule;
static DWORD PayloadOrigin, BridgeRva, EntryRva, CaptureRva, SourceRva, ResumeRva;
static PayloadConfig *CurrentConfig;

static DWORD ExportRva(const char *name)
{
  const auto address = GetProcAddress(PayloadModule, name);
  Require(address != nullptr, "Maintained payload export missing");

  return PayloadOrigin + reinterpret_cast<DWORD>(address) - reinterpret_cast<DWORD>(PayloadModule);
}

static void Bind(MappedImage &image)
{
  CurrentConfig = reinterpret_cast<PayloadConfig *>(image.base + ExportRva("config"));
  Require(CurrentConfig->version == 1 &&
              CurrentConfig->gameBase == reinterpret_cast<DWORD>(image.base),
          "Maintained payload configuration did not relocate");
  BridgeRva = ExportRva("transferTexture");
  EntryRva = ExportRva("textureLoadHook");
  CaptureRva = ExportRva("captureTextureSource");
  ResumeRva = CurrentConfig->textureSourceResume - reinterpret_cast<DWORD>(image.base);
  Require(ResumeRva >= 5 && ResumeRva < image.imageSize, "Source resume is outside the game image");
  SourceRva = ResumeRva - 5;
}

struct Registers
{
  DWORD edi, esi, ebp, savedEsp, ebx, edx, ecx, eax, flags;
};

static Registers Output;
static DWORD HostStack, InputStack, Target, Result, Flags, SeenDestination, SeenSource, SeenU,
    SeenFlags;
static DWORD SeenEnd, NativeEsi, OutputTop;
static DWORD NativeEbx = 0x44444444;
static __declspec(align(16)) BYTE Scratch[8192], FloatingBefore[512], FloatingAfter[512];

__declspec(naked) static void MockBridge()
{
  __asm {
    mov eax,[esp+4]
    mov SeenDestination,eax
    mov eax,[esp+8]
    mov SeenSource,eax
    mov eax,[esp+12]
    mov SeenU,eax
    pushfd
    pop eax
    mov SeenFlags,eax
    mov dword ptr fs:[34h], 55667788h
    mov ecx,11223344h
    mov edx,22334455h
    mov eax,Result
    ret 12
  }
}

__declspec(naked) static void CaptureExit()
{
  __asm {
    pushfd
    pushad
    fxsave FloatingAfter
    mov eax,[esp+36]
    mov OutputTop,eax
    mov esi,esp
    mov edi,offset Output
    mov ecx,9
    cld
    rep movsd
    mov esp,HostStack
    popad
    popfd
    ret
  }
}

__declspec(naked) static void Invoke()
{
  __asm {
    pushfd
    pushad
    mov HostStack,esp
    mov esp,InputStack
    pcmpeqd xmm0,xmm0
    fxsave FloatingBefore
    push Flags
    popfd
    mov edi,11111111h
    mov esi,NativeEsi
    mov ebp,33333333h
    mov ebx,NativeEbx
    mov eax,11221122h
    mov ecx,22332233h
    mov edx,33443344h
    jmp Target
  }
}

static DWORD &At(BYTE *memory, DWORD offset)
{
  return *reinterpret_cast<DWORD *>(memory + offset);
}

static void RunLoad(const char *candidate, bool first)
{
  MappedImage image(candidate, first, 0x400000);
  Bind(image);
  BYTE *bridge = image.base + BridgeRva;
  bridge[0] = 0xe9;
  At(bridge, 1) = reinterpret_cast<DWORD>(&MockBridge) - reinterpret_cast<DWORD>(bridge + 5);
  FlushInstructionCache(GetCurrentProcess(), image.base, image.imageSize);

  for (DWORD result : {0u, 1u, 0x80004005u, 0x887601c2u})
  {
    for (DWORD flags : {0x202u, 0x247u, 0x602u, 0x647u})
    {
      NativeEsi = 0x22222222;
      Result = result;
      Flags = flags;
      BYTE *u = Scratch + 4096;
      At(u - 12, 0) = reinterpret_cast<DWORD>(&CaptureExit);
      At(u - 12, 4) = 0x10203040;
      At(u - 12, 8) = 0x50607080;
      InputStack = reinterpret_cast<DWORD>(u - 12);
      Target = reinterpret_cast<DWORD>(image.base + EntryRva);
      SetLastError(0xaabbccdd);
      Invoke();
      Check(Output.eax == result, "Result is preserved");
      Check(Output.edi == 0x11111111 && Output.esi == 0x22222222 && Output.ebp == 0x33333333 &&
                Output.ebx == 0x44444444,
            "Nonvolatile registers preserved");
      Check(Output.savedEsp + 4 == reinterpret_cast<DWORD>(u), "Native ret8 stack cleanup");
      Check(SeenDestination == 0x10203040 && SeenSource == 0x50607080, "Original native arguments");
      Check(SeenU == reinterpret_cast<DWORD>(u), "Canonical U is caller post-argument stack");
      Check(!(SeenFlags & 0x400), "C++ bridge receives clear direction flag");
      Check((Output.flags & 0x400) == (flags & 0x400), "Incoming direction flag restored");
      Check(((Output.flags & 0x40) != 0) == (result == 0), "Native TEST zero flag");
      Check(((Output.flags & 0x80) != 0) == ((result & 0x80000000) != 0), "Native TEST sign flag");
      Check(!(Output.flags & 0x801), "Native TEST clears carry and overflow");
      unsigned parity = 0;

      for (unsigned bit = 0; bit < 8; ++bit)
      {
        parity ^= (result >> bit) & 1;
      }

      Check(((Output.flags & 4) != 0) == (parity == 0), "Native TEST parity flag");
      Check(GetLastError() == 0x55667788, "Post-bridge LastError preserved by thunk");
      Check(std::memcmp(FloatingBefore, FloatingAfter, sizeof(FloatingBefore)) == 0,
            "Thunk leaves x87 SSE and MXCSR unchanged");
    }
  }
}

__declspec(naked) static void MockCapture()
{
  __asm {
    mov eax,[esp+4]
    mov SeenU,eax
    mov eax,[esp+8]
    mov SeenEnd,eax
    pushfd
    pop eax
    mov SeenFlags,eax
    mov dword ptr fs:[34h],99887766h
    mov eax,11112222h
    mov ecx,22223333h
    mov edx,33334444h
    mov ebx,44445555h
    mov esi,55556666h
    mov edi,66667777h
    mov ebp,77778888h
    std
    ret 8
  }
}

static void Jump(BYTE *where, DWORD target)
{
  where[0] = 0xe9;
  At(where, 1) = target - reinterpret_cast<DWORD>(where + 5);
}

static Registers CaptureRun(BYTE *image, DWORD flags)
{
  BYTE *u = Scratch + 4096;
  At(u, 0x1c) = 0x12345678;
  InputStack = reinterpret_cast<DWORD>(u);
  NativeEsi = 0x34567890;
  Flags = flags;
  Target = reinterpret_cast<DWORD>(image + SourceRva);
  SetLastError(0xaabbccdd);
  Invoke();

  return Output;
}

static void RunCapture(const char *candidate, bool first)
{
  MappedImage image(candidate, first, 0x400000);
  Bind(image);
  BYTE original[5] = {0x8b, 0x44, 0x24, 0x1c, 0x50};
  BYTE patched[5];
  std::memcpy(patched, image.base + SourceRva, 5);
  Jump(image.base + CaptureRva, reinterpret_cast<DWORD>(&MockCapture));
  Jump(image.base + ResumeRva, reinterpret_cast<DWORD>(&CaptureExit));

  for (DWORD flags : {0x202u, 0x247u, 0x602u, 0x647u})
  {
    std::memcpy(image.base + SourceRva, original, 5);
    FlushInstructionCache(GetCurrentProcess(), image.base, image.imageSize);
    Registers expected = CaptureRun(image.base, flags);
    DWORD expectedTop = OutputTop;
    std::memcpy(image.base + SourceRva, patched, 5);
    FlushInstructionCache(GetCurrentProcess(), image.base, image.imageSize);
    Registers actual = CaptureRun(image.base, flags);
    Check(std::memcmp(&expected, &actual, sizeof(expected)) == 0,
          "Pre-Unlock replay preserves exact original regs/flags/ESP");
    Check(OutputTop == expectedTop && OutputTop == 0x12345678, "Pre-Unlock pushes chosen surface");
    Check(actual.eax == 0x12345678, "Pre-Unlock native MOV result");
    Check(actual.savedEsp + 4 == reinterpret_cast<DWORD>(Scratch + 4096) - 4,
          "Pre-Unlock native PUSH stack effect");
    Check(SeenU == reinterpret_cast<DWORD>(Scratch + 4096), "Capture receives native canonical U");
    Check(SeenEnd == NativeEsi, "Capture receives original ESI converted end");
    Check(!(SeenFlags & 0x400), "Capture C++ helper receives clear DF");
    Check((actual.flags & 0x400) == (flags & 0x400),
          "Capture restores incoming DF after clobbering helper");
    Check(GetLastError() == 0xaabbccdd, "Capture preserves LastError despite helper clobber");
    Check(std::memcmp(FloatingBefore, FloatingAfter, sizeof(FloatingBefore)) == 0,
          "Capture stub leaves x87 SSE MXCSR unchanged");
  }
}

// Run the actual maintained entry thunks with inert native callees
static Registers NativeRegisters;
static DWORD NativeLastError, NativeCalls, ReturnFlags;
static DWORD RendererReady, RendererChecks, RendererFlags;
static DWORD *Lifecycle;

__declspec(naked) static void MockPrepareRenderer()
{
  __asm {
    pushfd
    pop eax
    mov RendererFlags,eax
    inc RendererChecks
    mov eax,RendererReady
    mov ecx,0abcddcbah
    mov edx,0bcdeedcbh
    ret
  }
}

__declspec(naked) static void NativeCall()
{
  __asm {
    pushfd
    pushad
    mov esi,esp
    mov edi,offset NativeRegisters
    mov ecx,9
    cld
    rep movsd
    mov eax,fs:[34h]
    mov NativeLastError,eax
    inc NativeCalls
    popad
    popfd
    mov eax,Result
    mov ecx,0aabbccddh
    mov edx,0bbccddefh
    push ReturnFlags
    popfd
    ret
  }
}

__declspec(naked) static void NativeModeCall()
{
  __asm {
    call NativeCall
    ret 4
  }
}

static void StubReturn(BYTE *where)
{
  where[0] = 0xc3;
}

static void RunBoundaries(const char *candidate, bool first)
{
  MappedImage image(candidate, first, 0x400000);
  Bind(image);
  Lifecycle = reinterpret_cast<DWORD *>(image.base + ExportRva("textureLifecycleState"));
  auto *payloadState = reinterpret_cast<PayloadState *>(image.base + ExportRva("state"));
  At(image.base, GameRva::getLastError) = reinterpret_cast<DWORD>(&GetLastError);
  At(image.base, GameRva::setLastError) = reinterpret_cast<DWORD>(&SetLastError);
  StubReturn(image.base + ExportRva("afterFrame"));
  StubReturn(image.base + ExportRva("afterRendererInit"));
  StubReturn(image.base + ExportRva("afterFullscreenSetup"));
  Jump(image.base + ExportRva("prepareRenderer"), reinterpret_cast<DWORD>(&MockPrepareRenderer));
  RendererReady = 1;
  CurrentConfig->frameOriginal = reinterpret_cast<DWORD>(&NativeCall);
  CurrentConfig->rendererInitOriginal = reinterpret_cast<DWORD>(&NativeCall);
  CurrentConfig->setupOriginal = reinterpret_cast<DWORD>(&NativeCall);
  FlushInstructionCache(GetCurrentProcess(), image.base, image.imageSize);

  struct Entry
  {
    const char *name;
    unsigned counter;
  };

  for (const Entry entry : {Entry{"frameHook", 2}, Entry{"rendererInitHook", 0}})
  {
    for (DWORD flags : {0x202u, 0x247u, 0x602u, 0x647u})
    {
      for (bool configured : {true, false})
      {
        CurrentConfig->magic = configured ? 0x48474c50 : 0;
        const DWORD generation = Lifecycle[entry.counter];
        BYTE *u = Scratch + 4096;
        At(u - 4, 0) = reinterpret_cast<DWORD>(&CaptureExit);
        InputStack = reinterpret_cast<DWORD>(u - 4);
        Target = reinterpret_cast<DWORD>(image.base + ExportRva(entry.name));
        NativeEsi = 0x22222222;
        Flags = flags;
        ReturnFlags = flags ^ 0x45;
        Result = 0x887601c2;
        NativeCalls = 0;
        RendererChecks = 0;
        SetLastError(0x12344321);
        Invoke();
        Check(NativeCalls == 1, "Each wrapper invokes its native function once");
        if (std::strcmp(entry.name, "rendererInitHook") == 0)
        {
          Check(RendererChecks == 1 && !(RendererFlags & 0x400),
                "Renderer readiness gate runs once with clear DF before native initialization");
        }

        Check(Lifecycle[entry.counter] == generation + (configured ? 1u : 0u),
              "Configured entry advances its ownership generation");
        Check(NativeRegisters.eax == 0x11221122 && NativeRegisters.ecx == 0x22332233 &&
                  NativeRegisters.edx == 0x33443344,
              "New entry bookkeeping preserves native volatile arguments");
        Check(NativeRegisters.edi == 0x11111111 && NativeRegisters.esi == NativeEsi &&
                  NativeRegisters.ebx == 0x44444444 && NativeRegisters.ebp == 0x33333333,
              "New entry bookkeeping preserves native nonvolatile registers");
        // The renderer's native prologue replaces arithmetic flags immediately.
        // Its scoped C++ dispatcher must still preserve DF and native return flags.
        const DWORD incomingMask = std::strcmp(entry.name, "rendererInitHook") == 0 ? 0x400 : 0xcd5;
        Check((NativeRegisters.flags & incomingMask) == (flags & incomingMask),
              "Entry bookkeeping preserves the native flag contract");
        Check(NativeLastError == 0x12344321 && GetLastError() == 0x12344321,
              "Entry bookkeeping preserves LastError");
        Check(Output.eax == Result && Output.ecx == 0xaabbccdd && Output.edx == 0xbbccddef,
              "Native return registers survive exit bookkeeping");
        Check((Output.flags & 0xcd5) == (ReturnFlags & 0xcd5),
              "Native return flags survive exit bookkeeping");
        Check(Output.savedEsp + 4 == reinterpret_cast<DWORD>(u),
              "Entry wrapper preserves cdecl stack cleanup");
        Check(std::memcmp(FloatingBefore, FloatingAfter, sizeof(FloatingBefore)) == 0,
              "Entry bookkeeping leaves x87 SSE MXCSR unchanged");
      }
    }
  }

  CurrentConfig->magic = 0x48474c50;
  using Callback = void(__cdecl *)();

  struct Boundary
  {
    const char *name;
    bool lifecycle;
    bool video;
  };

  for (const Boundary boundary :
       {Boundary{"beforeTeardown", true, false}, Boundary{"beforeTransition", true, false},
        Boundary{"beforeMainMenuShow", false, false}, Boundary{"beforeVideoOpen", false, true},
        Boundary{"releaseBackbuffer", true, false}})
  {
    std::memset(payloadState, 0, sizeof(*payloadState));
    payloadState->busy = 1;
    payloadState->transitionDepth = 1;
    payloadState->videoDepth = 1;
    At(image.base, GameRva::backbuffer) = 0;
    At(image.base, GameRva::drawLock) = 0;
    const DWORD serial = Lifecycle[0], epoch = Lifecycle[3];
    SetLastError(0x55667788);

    if (boundary.video)
    {
      reinterpret_cast<void(__cdecl *)(HWND)>(image.base + ExportRva(boundary.name))(nullptr);
    }
    else
    {
      reinterpret_cast<Callback>(image.base + ExportRva(boundary.name))();
    }

    Check(Lifecycle[0] == serial + (boundary.lifecycle ? 1u : 0u),
          "Native boundary retains lifecycle coverage");
    Check(Lifecycle[3] == epoch + 1,
          "Native boundary invalidates frame ownership before changing state");
    Check(GetLastError() == 0x55667788, "Native boundary preserves LastError");
  }
}

static void RunReadinessRejection(const char *candidate, bool first)
{
  MappedImage image(candidate, first, 0x400000);
  Bind(image);
  Lifecycle = reinterpret_cast<DWORD *>(image.base + ExportRva("textureLifecycleState"));
  auto *payloadState = reinterpret_cast<PayloadState *>(image.base + ExportRva("state"));
  DWORD &readinessBusy = At(image.base, ExportRva("readinessState"));
  At(image.base, GameRva::getLastError) = reinterpret_cast<DWORD>(&GetLastError);
  At(image.base, GameRva::setLastError) = reinterpret_cast<DWORD>(&SetLastError);
  CurrentConfig->rendererInitOriginal = reinterpret_cast<DWORD>(&NativeCall);
  CurrentConfig->setupOriginal = reinterpret_cast<DWORD>(&NativeCall);
  CurrentConfig->modeResume = reinterpret_cast<DWORD>(&NativeModeCall);
  Jump(image.base + ExportRva("prepareRenderer"), reinterpret_cast<DWORD>(&MockPrepareRenderer));
  FlushInstructionCache(GetCurrentProcess(), image.base, image.imageSize);

  readinessBusy = 1;

  // Nonzero transition state ensures rejection occurs before either native
  // bookkeeping or a recovery attempt can alter an already active transition.
  payloadState->busy = 1;
  payloadState->transitionDepth = 3;
  payloadState->setupFailureCount = 9;
  const PayloadState expectedState = *payloadState;
  DWORD expectedLifecycle[5];
  std::memcpy(expectedLifecycle, Lifecycle, sizeof(expectedLifecycle));
  RendererReady = 0;

  struct Entry
  {
    const char *name;
    bool mode;
    bool renderer;
  };

  for (const Entry entry : {Entry{"rendererInitHook", false, true}})
  {
    for (DWORD flags : {0x202u, 0x247u, 0x602u, 0x647u})
    {
      BYTE *u = Scratch + 4096;
      BYTE *input = u - (entry.mode ? 8 : 4);
      At(input, 0) = reinterpret_cast<DWORD>(&CaptureExit);
      if (entry.mode)
      {
        At(input, 4) = 2;
      }

      InputStack = reinterpret_cast<DWORD>(input);
      Target = reinterpret_cast<DWORD>(image.base + ExportRva(entry.name));
      NativeEsi = 0x22222222;
      NativeCalls = 0;
      RendererChecks = 0;
      Flags = flags;
      SetLastError(0x12344321);
      Invoke();
      Check(NativeCalls == 0, "Rejected readiness entry does not invoke native initialization");
      Check(std::memcmp(payloadState, &expectedState, sizeof(expectedState)) == 0,
            "Rejected readiness entry leaves active transition state unchanged");
      Check(std::memcmp(Lifecycle, expectedLifecycle, sizeof(expectedLifecycle)) == 0,
            "Rejected readiness entry does not advance resource ownership");
      Check(readinessBusy == 1, "Nested request cannot release the outer readiness wait");
      Check(Output.eax == (entry.mode ? 0x11221122u : 0u) && Output.ecx == 0x22332233 &&
                Output.edx == 0x33443344,
            "Rejected entry preserves arguments and returns native failure where required");
      Check(Output.edi == 0x11111111 && Output.esi == NativeEsi && Output.ebx == 0x44444444 &&
                Output.ebp == 0x33333333,
            "Rejected readiness entry preserves nonvolatile registers");
      Check((Output.flags & 0xcd5) == (flags & 0xcd5),
            "Rejected readiness entry restores incoming flags and DF");
      Check(Output.savedEsp + 4 == reinterpret_cast<DWORD>(u),
            "Rejected entry preserves cdecl or thiscall stack cleanup");
      Check(GetLastError() == 0x12344321, "Rejected readiness entry preserves LastError");
      Check(std::memcmp(FloatingBefore, FloatingAfter, sizeof(FloatingBefore)) == 0,
            "Rejected readiness entry leaves x87 SSE MXCSR unchanged");
      if (entry.renderer)
      {
        Check(RendererChecks == 1 && !(RendererFlags & 0x400),
              "Renderer gate failure is checked with clear DF and skips native initialization");
      }
    }
  }
}

// Fullscreen setup now requires owned native caller provenance. Its real teardown,
// failure cleanup and busy-scope rejection are covered by fullscreenSetupTests.

struct ProbeData
{
  DWORD calls, failures, busy, nested;
  DWORD last[16], lastFailure[16];
};

static_assert(sizeof(ProbeData) == 144, "Flip observation ABI");
static DWORD FlipSurface, FlipTarget, FlipFlags, FlipIncomingError;

__declspec(naked) static void NativeFlip()
{
  __asm {
    pushfd
    pushad
    mov eax,[esp+40]
    mov FlipSurface,eax
    mov eax,[esp+44]
    mov FlipTarget,eax
    mov eax,[esp+48]
    mov FlipFlags,eax
    mov eax,fs:[34h]
    mov FlipIncomingError,eax
    inc NativeCalls
    popad
    popfd
    mov dword ptr fs:[34h],0f1f2f3f4h
    mov eax,Result
    ret 12
  }
}

using ObserveFlip = HRESULT(WINAPI *)(void *, void *, DWORD, DWORD);
static ObserveFlip NestedObserve;
static ProbeData *ActiveProbe;
static DWORD *NestedSurface;
static DWORD NestedNativeCalls;

static HRESULT WINAPI NestedFlip(void *surface, void *target, DWORD flags)
{
  ++NestedNativeCalls;
  Check(target == nullptr && flags == 1, "Nested observation changed native Flip arguments");
  if (surface != &NestedSurface)
  {
    Check(ActiveProbe->busy == 1, "Outer native Flip is not covered by observer depth");
    Check(NestedObserve(&NestedSurface, nullptr, 1, 0x10203040) == static_cast<HRESULT>(0x887601c2),
          "Nested observation changed the nested native HRESULT");
    Check(ActiveProbe->busy == 1, "Nested observation cleared the outer active depth");
    SetLastError(0xf1f2f3f4);
    return S_OK;
  }

  Check(ActiveProbe->busy == 2, "Nested native Flip is not covered by observer depth");
  SetLastError(0x55667788);
  return static_cast<HRESULT>(0x887601c2);
}

static void RunFlipObservation(const char *candidate, bool first)
{
  MappedImage image(candidate, first, 0x400000);
  Bind(image);
  auto *probe = reinterpret_cast<ProbeData *>(image.base + ExportRva("flipProbeState"));
  auto *payloadState = reinterpret_cast<PayloadState *>(image.base + ExportRva("state"));
  auto *lifecycle = reinterpret_cast<DWORD *>(image.base + ExportRva("textureLifecycleState"));
  At(image.base, GameRva::getLastError) = reinterpret_cast<DWORD>(&GetLastError);
  At(image.base, GameRva::setLastError) = reinterpret_cast<DWORD>(&SetLastError);
  At(image.base, GameRva::mode) = 2;
  At(image.base, GameRva::directDraw) = 3;
  At(image.base, GameRva::hardware) = 4;
  At(image.base, GameRva::fullscreenActive) = 5;
  At(image.base, GameRva::mainMenuVisible) = 8;
  At(image.base, GameRva::drawLock) = 9;
  At(image.base, GameRva::drawSession) = 10;
  payloadState->transitionDepth = 6;
  payloadState->videoDepth = 7;
  payloadState->restoreSuccesses = 12;
  lifecycle[0] = 11;
  DWORD methods[12] = {};
  methods[11] = reinterpret_cast<DWORD>(&NativeFlip);
  DWORD *surface = methods;
  NativeEbx = reinterpret_cast<DWORD>(methods);

  for (bool configured : {true, false})
  {
    CurrentConfig->magic = configured ? 0x48474c50 : 0;
    for (DWORD result : {0u, 1u, 0x80004005u, 0x887601c2u})
    {
      for (DWORD flags : {0x202u, 0x247u, 0x602u, 0x647u})
      {
        std::memset(probe, 0, sizeof(*probe));
        BYTE *u = Scratch + 4096;
        At(u - 16, 0) = reinterpret_cast<DWORD>(&CaptureExit);
        At(u - 16, 4) = reinterpret_cast<DWORD>(&surface);
        At(u - 16, 8) = 0x20304050;
        At(u - 16, 12) = 1;
        At(u, 0) = 0x31415926;
        At(u, 4) = 0x40506070;
        InputStack = reinterpret_cast<DWORD>(u - 16);
        Target = reinterpret_cast<DWORD>(image.base + ExportRva("flipProbeHook"));
        NativeEsi = 0x22222222;
        NativeCalls = 0;
        Result = result;
        Flags = flags;
        SetLastError(0x12344321);
        Invoke();
        Check(NativeCalls == 1, "Flip observation calls native Flip exactly once");
        Check(FlipSurface == reinterpret_cast<DWORD>(&surface) && FlipTarget == 0x20304050 &&
                  FlipFlags == 1,
              "Flip observation preserves the original COM arguments");
        Check(FlipIncomingError == 0x12344321 && GetLastError() == 0xf1f2f3f4,
              "Flip observation preserves incoming and native-return LastError");
        Check(Output.eax == result, "Flip observation preserves the native HRESULT");
        Check(Output.edi == 0x11111111 && Output.esi == NativeEsi && Output.ebp == 0x33333333 &&
                  Output.ebx == NativeEbx,
              "Flip observation preserves native nonvolatile registers");
        Check(Output.savedEsp + 4 == reinterpret_cast<DWORD>(u),
              "Flip observation preserves native stdcall RET12 cleanup");
        Check((Output.flags & 0x400) == (flags & 0x400), "Flip observation restores incoming DF");
        Check(((Output.flags & 0x40) != 0) == (result == 0), "Flip TEST preserves zero flag");
        Check(((Output.flags & 0x80) != 0) == ((result & 0x80000000) != 0),
              "Flip TEST preserves sign flag");
        Check(!(Output.flags & 0x801), "Flip TEST clears carry and overflow");
        unsigned parity = 0;
        for (unsigned bit = 0; bit < 8; ++bit)
        {
          parity ^= (result >> bit) & 1;
        }
        Check(((Output.flags & 4) != 0) == (parity == 0), "Flip TEST preserves parity flag");
        Check(probe->busy == 0, "Flip observation releases active depth");
        Check(probe->calls == (configured ? 1u : 0u), "Only configured calls are observed");
        if (configured)
        {
          Check(probe->last[0] == result && probe->last[1] == 0x40506070 &&
                    probe->last[2] == FlipSurface && probe->last[3] == FlipTarget &&
                    probe->last[4] == FlipFlags,
                "Flip observation records the native result, caller and arguments");
          for (DWORD index = 5; index < 16; ++index)
          {
            Check(probe->last[index] == index - 3,
                  "Flip observation records the actual native and payload mode state");
          }
          Check(probe->failures == (result ? 1u : 0u),
                "Flip failure count records nonzero results");
        }
      }
    }
  }

  CurrentConfig->magic = 0x48474c50;
  std::memset(probe, 0, sizeof(*probe));
  auto observe = reinterpret_cast<ObserveFlip>(image.base + ExportRva("observeFlip"));
  Result = 0x887601c2;
  Check(observe(&surface, nullptr, 1, 0xabcdef00) == static_cast<HRESULT>(Result),
        "Observed failure retains its exact HRESULT");
  DWORD failure[16];
  std::memcpy(failure, probe->lastFailure, sizeof(failure));
  Result = 0;
  Check(observe(&surface, nullptr, 1, 0xabcdef10) == S_OK,
        "Subsequent successful Flip remains successful");
  Check(probe->calls == 2 && probe->failures == 1 && probe->last[0] == 0,
        "Success updates current observation without erasing the failure count");
  Check(std::memcmp(failure, probe->lastFailure, sizeof(failure)) == 0,
        "A later success preserves the previous failed Flip snapshot");

  methods[11] = reinterpret_cast<DWORD>(&NestedFlip);
  NestedSurface = methods;
  NestedObserve = observe;
  ActiveProbe = probe;
  NestedNativeCalls = 0;
  std::memset(probe, 0, sizeof(*probe));
  Check(observe(&surface, nullptr, 1, 0xabcdef20) == S_OK,
        "Nested native callback does not alter the outer Flip result");
  Check(NestedNativeCalls == 2 && probe->calls == 2 && probe->nested == 1 && probe->busy == 0,
        "Nested observation accounts for both original calls and clears its depth");
  Check(probe->failures == 1 && probe->last[0] == 0 && probe->lastFailure[0] == 0x887601c2 &&
            probe->lastFailure[1] == 0x10203040,
        "Outer success preserves the independently captured nested failure");
  Check(GetLastError() == 0xf1f2f3f4, "Nested observation preserves the outer native LastError");
  NativeEbx = 0x44444444;
}

int main(int argc, char **argv)
{
  try
  {
    Require(argc == 4, "Expected normalized payload DLL, generated image and payload origin");
    PayloadOrigin = std::stoul(argv[3], nullptr, 0);
    PayloadModule = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    Require(PayloadModule != nullptr, "Windows rejected the normalized payload image");
    RunLoad(argv[2], true);
    RunLoad(argv[2], false);
    RunCapture(argv[2], true);
    RunCapture(argv[2], false);
    RunBoundaries(argv[2], true);
    RunBoundaries(argv[2], false);
    RunReadinessRejection(argv[2], true);
    RunReadinessRejection(argv[2], false);
    RunFlipObservation(argv[2], true);
    RunFlipObservation(argv[2], false);
    FreeLibrary(PayloadModule);
    std::printf("{\"passed\":true,\"checks\":%u,\"gameLaunched\":false,\"mappedBases\":2}\n",
                Checks);
    return 0;
  }
  catch (const std::exception &error)
  {
    std::printf("FAIL after %u checks: %s\n", Checks, error.what());
    return 1;
  }
}
