// SPDX-License-Identifier: MIT

// Runs retail resource teardown and caller publication bytes in a private image.
// Full native fullscreen setup is a controlled boundary that preserves its BOOL and LastError.

#include "support/ownedReadinessFixture.hpp"
#include "setupNative.hpp"
#include "stampNative.hpp"

static int SetupResult = 1;
static DWORD SetupCalls = 0, RecreateCalls = 0, RestoreCalls = 0, NormalCalls = 0;
static DWORD FrameCalls = 0;
static std::function<void()> AtSetup, AtRestore, AtRelease;
static Object LateDependent;

static void Invoke(std::function<void()> &callback)
{
  auto call = callback;
  callback = {};
  if (call)
  {
    call();
  }
}

static ULONG WINAPI NativeRelease(void *pointer)
{
  Object &object = *static_cast<Object *>(pointer);
  Check(object.references > 0, "Native release has an owned reference");
  --object.references;
  ++object.releases;
  Invoke(AtRelease);
  return object.references;
}

static HRESULT WINAPI NativeRestore(void *)
{
  Check(FullscreenSetup::scopeState.busy == 1, "Restore callback remains inside root fence");
  ++RestoreCalls;
  Invoke(AtRestore);
  return S_OK;
}

static HRESULT WINAPI NativeNormal(void *, HWND window, DWORD flags)
{
  Check(window == Active->frame && flags == 8, "Native normal cooperative contract");
  ++NormalCalls;
  return S_OK;
}

static int __cdecl ForbiddenRootRecreation()
{
  ++RecreateCalls;
  return 0;
}

static int __cdecl NativeSetup()
{
  Check(FullscreenSetup::scopeState.busy == 1, "Setup remains inside root fence");
  Check(RecreateCalls == 0, "Owned setup must not recreate the DirectDraw root");
  Check(Read(GameRva::drawObject) == reinterpret_cast<DWORD>(&Active->draw),
        "Original setup receives the same retained root");
  ResourceReleaseHook();
  Check(Read(GameRva::drawObject) != 0, "Setup retains the original live root");
  ++SetupCalls;
  Invoke(AtSetup);
  SetLastError(0x24681357);
  return SetupResult;
}

static void __cdecl NativeFrame()
{
  ++FrameCalls;
}

static void Jump(BYTE *address, void *target)
{
  address[0] = 0xe9;
  *reinterpret_cast<DWORD *>(address + 1) =
      reinterpret_cast<DWORD>(target) - reinterpret_cast<DWORD>(address + 5);
}

static void WriteCall(BYTE *address, void *target)
{
  Jump(address, target);
  address[0] = 0xe8;
}

template <size_t N, size_t R>
static void
NativeSpan(Fixture &fixture, DWORD rva, const BYTE (&bytes)[N], const DWORD (&relocations)[R])
{
  std::memcpy(fixture.image + rva, bytes, N);
  for (DWORD offset : relocations)
  {
    *reinterpret_cast<DWORD *>(fixture.image + rva + offset) += config.gameBase - 0x400000;
  }
}

static void Prepare(Fixture &fixture)
{
  DWORD protection = 0;
  Check(VirtualProtect(fixture.image, 0x600000, PAGE_EXECUTE_READWRITE, &protection) != FALSE,
        "Native fixture executable allocation");
  FullscreenSetup::scopeState = {};
  UnlockFrame::scope = {};
  PostRestoreRenderer::scope = {};
  PostRestoreRenderer::baseline = {};
  normalFrameState = {};
  flushOwnerState = {};
  modeAdmissionState = {};
  quadFrameRecoveryState = {};
  failedPresentationState = {};
  worldRelockState = {};
  bridgeState = {};
  movieRecoveryState = {};
  preMovieRecoveryState = {};
  hardwareModeCompletionState = {};
  softwareModeCompletionState = {};
  SetupResult = 1;
  SetupCalls = RecreateCalls = RestoreCalls = NormalCalls = FrameCalls = 0;
  AtSetup = AtRestore = AtRelease = {};
  fixture.methods[2] = reinterpret_cast<DWORD>(&NativeRelease);
  fixture.methods[19] = reinterpret_cast<DWORD>(&NativeRestore);
  fixture.methods[20] = reinterpret_cast<DWORD>(&NativeNormal);
  fixture.draw4.references = 0;
  Write(GameRva::mode, 0);
  Write(GameRva::hardwarePreference, 1);
  Write(GameRva::directDraw, 0);
  Write(GameRva::fullscreenActive, 0);
  state.transitionDepth = 1;
  modeAdmissionState.imageBase = config.gameBase;
  modeAdmissionState.app = Read(GameRva::app);
  modeAdmissionState.frameObject = reinterpret_cast<DWORD>(fixture.image + 0x381000);
  modeAdmissionState.frame = reinterpret_cast<DWORD>(fixture.frame);
  modeAdmissionState.thread = GetCurrentThreadId();
  modeAdmissionState.ownerGeneration = 1;
  modeAdmissionState.transitionDepth = 1;
  modeAdmissionState.transitionSerial = 3;
  config.setupOriginal = config.gameBase + modeNativeFullscreenSetup;
  config.releaseResourcesOriginal = config.gameBase + resourceTeardownRva;
  config.frameOriginal = reinterpret_cast<DWORD>(&NativeFrame);
  NativeSpan(fixture, resourceTeardownRva, resourceTeardownBytes, resourceTeardownRelocations);
  NativeSpan(fixture, textureTeardownRva, textureTeardownBytes, textureTeardownRelocations);
  Jump(fixture.image + rootRecreationRva, &ForbiddenRootRecreation);
  Jump(fixture.image + modeNativeFullscreenSetup, &NativeSetup);
  WriteCall(fixture.image + modeNativeFullscreenReturn - 5, &FullscreenSetupHook);
  fixture.image[modeNativeFullscreenReturn] = 0xc3;
  Check(FlushInstructionCache(GetCurrentProcess(), fixture.image, 0x600000) != FALSE,
        "Native instruction cache synchronized");
}

static int RunSetup()
{
  DWORD native = config.gameBase + modeNativeFullscreenReturn - 5;
  DWORD object = modeAdmissionState.frameObject;
  int result = 0;
  __asm {
    push ebx
    push esi
    mov ebx,object
    mov esi,2
    call native
    mov result,eax
    pop esi
    pop ebx
  }
  return result;
}

static void NestedModeAndFrame()
{
  const DWORD serial = modeAdmissionState.transitionSerial;
  const DWORD depth = state.transitionDepth;
  reinterpret_cast<void(__thiscall *)(void *, int)>(&ModeHook)(
      reinterpret_cast<void *>(Read(GameRva::app)), 0);
  FrameHook();
  Check(modeAdmissionState.transitionSerial == serial && state.transitionDepth == depth,
        "Nested mode does not enter native transition");
  Check(FrameCalls == 0, "Nested frame is suppressed before native renderer");
}

static void RunCases()
{
  Case("retained native root and original setup", [](Fixture &f)
  {
    Prepare(f);
    Check(RunSetup() == 1, "Genuine successful setup result is preserved");
    Check(RecreateCalls == 0 && SetupCalls == 1,
          "Only the original fullscreen setup boundary runs");
    Check(f.draw.references == 1 && f.draw.releases == 0 && f.draw4.references == 0 &&
              Read(GameRva::drawObject) == reinterpret_cast<DWORD>(&f.draw),
          "Native root ownership is unchanged");
    Check(f.primary.references == 0 && f.backbuffer.references == 0,
          "The original setup resource teardown releases old surfaces");
    Check(!RestoreCalls && !NormalCalls,
          "Successful mode entry adds no display restoration or normal cooperative transition");
    Check(!FullscreenSetup::scopeState.busy &&
              PostRestoreRenderer::baseline.base == config.gameBase,
          "Owned setup stamps before closing its scope");
    Check(GetLastError() == 0x24681357, "Native setup LastError is preserved");
  });
  Case("software fullscreen entry", [](Fixture &f)
  {
    Prepare(f);
    Write(GameRva::mode, 1);
    Check(RunSetup() == 1 && Read(GameRva::mode) == 1 && !RecreateCalls,
          "Original software-to-hardware entry retains the root");
  });
  Case("native setup failure", [](Fixture &f)
  {
    Prepare(f);
    SetupResult = 0;
    Check(RunSetup() == 0 && SetupCalls == 1 && state.setupFailureCount == 1,
          "Native failure uses the existing cleanup path");
    Check(!FullscreenSetup::scopeState.busy && !PostRestoreRenderer::baseline.base &&
              !RecreateCalls && f.draw.references == 1,
          "Failed setup does not fabricate a baseline or discard the retained root");
  });
  Case("nested mode and frame during resource teardown", [](Fixture &f)
  {
    Prepare(f);
    AtRelease = NestedModeAndFrame;
    Check(RunSetup() == 1, "Original teardown stays inside the owned fence");
  });
  Case("nested mode and frame during setup", [](Fixture &f)
  {
    Prepare(f);
    AtSetup = NestedModeAndFrame;
    Check(RunSetup() == 1, "Setup callback reentry is suppressed");
  });
  Case("nested mode and frame during failure cleanup", [](Fixture &f)
  {
    Prepare(f);
    SetupResult = 0;
    AtSetup = []
    {
      AtRestore = NestedModeAndFrame;
    };
    Check(RunSetup() == 0 && !FullscreenSetup::scopeState.busy,
          "Existing failure cleanup remains fenced");
  });
  Case("direct nested setup cannot clean outer scope", [](Fixture &f)
  {
    Prepare(f);
    AtSetup = []
    {
      const DWORD restores = RestoreCalls;
      Check(RunSetup() == 0, "Nested setup is rejected");
      Check(FullscreenSetup::scopeState.busy == 1 && RestoreCalls == restores &&
                state.setupFailureCount == 0,
            "Rejected nested wrapper preserves outer ownership");
    };
    Check(RunSetup() == 1 && SetupCalls == 1, "Outer setup remains owned");
  });
  Case("window validation callbacks are fenced", [](Fixture &f)
  {
    Prepare(f);
    f.On(Operation::Window, NestedModeAndFrame);
    Check(RunSetup() == 1, "Fence starts before callback-capable window validation");
  });
  Case("root changes during window validation", [](Fixture &f)
  {
    Prepare(f);
    f.On(Operation::Window, [] { Write(GameRva::drawObject, 0); });
    Check(RunSetup() == 0 && !SetupCalls && !RecreateCalls && !state.setupFailureCount &&
              !FullscreenSetup::scopeState.busy,
          "Changed root is rejected before native setup or cleanup");
  });
  Case("owner changes during setup", [](Fixture &f)
  {
    Prepare(f);
    AtSetup = []
    {
      ++modeAdmissionState.transitionSerial;
    };
    Check(RunSetup() == 0 && SetupCalls == 1 && !PostRestoreRenderer::baseline.base,
          "Changed owner cannot publish setup success");
  });
  Case("root changes during setup", [](Fixture &f)
  {
    Prepare(f);
    AtSetup = []
    {
      Write(GameRva::drawObject, 0);
    };
    Check(RunSetup() == 0 && !PostRestoreRenderer::baseline.base,
          "Replaced root cannot publish setup success");
  });
  Case("missing native root", [](Fixture &f)
  {
    Prepare(f);
    Write(GameRva::drawObject, 0);
    Check(RunSetup() == 0 && !SetupCalls && !RecreateCalls && !state.setupFailureCount,
          "No root is rejected before any native resource mutation");
  });
  Case("original setup owns old dependent resources", [](Fixture &f)
  {
    Prepare(f);
    LateDependent = {};
    LateDependent.table = f.methods;
    Write(modeNativePalette, reinterpret_cast<DWORD>(&LateDependent));
    Check(RunSetup() == 1 && LateDependent.references == 0 && !Read(modeNativePalette),
          "Old palette is released by original setup, not a preemptive root reset");
  });
  for (DWORD invalid = 0; invalid < 8; ++invalid)
  {
    Case("admission guard " + std::to_string(invalid), [invalid](Fixture &f)
    {
      Prepare(f);
      if (invalid == 0)
      {
        readinessState.busy = 1;
      }
      if (invalid == 1)
      {
        quadFrameRecoveryState.depth = 1;
      }
      if (invalid == 2)
      {
        Write(GameRva::mode, 2);
      }
      if (invalid == 3)
      {
        modeAdmissionState.transitionDepth = 2;
      }
      if (invalid == 4)
      {
        normalFrameState.depth = 1;
      }
      if (invalid == 5)
      {
        flushOwnerState.active = 1;
      }
      if (invalid == 6)
      {
        UnlockFrame::scope.busy = 1;
      }
      if (invalid == 7)
      {
        PostRestoreRenderer::scope.busy = 1;
      }
      Check(RunSetup() == 0 && !RecreateCalls && !SetupCalls,
            "Unsafe admission performs no native resource mutation");
      Check(!state.setupFailureCount && !FullscreenSetup::scopeState.busy,
            "Unowned rejected wrapper skips native cleanup");
    });
  }
}

namespace StampOrder
{
  static DWORD Callbacks = 0;
  static bool RestoreDuringUnblock = false;

  static void __fastcall Unblock(void *frame, void *, DWORD argument)
  {
    Check(reinterpret_cast<DWORD>(frame) == config.gameBase + 0x381000 && argument == 0,
          "Native unblock ABI changed");
    Check(!Read(GameRva::directDraw) && !Read(GameRva::fullscreenActive),
          "Native flags were published before the real unblock callback");
    Check(PostRestoreRenderer::baseline.valid && PostRestoreRenderer::baseline.restored == 7,
          "Successful setup did not stamp before the native callback");
    ++Callbacks;
    if (RestoreDuringUnblock)
    {
      ++state.restoreSuccesses;
    }
  }

  static void Seed(Fixture &fixture)
  {
    PostRestoreRenderer::scope = {};
    PostRestoreRenderer::baseline = {};
    FullscreenSetup::scopeState = {};
    bridgeState = {};
    bridgeState.createSuccesses = 1;
    Write(GameRva::mode, 0);
    Write(GameRva::directDraw, 0);
    Write(GameRva::fullscreenActive, 0);
    Write(GameRva::hardware, 1);
    Write(GameRva::legacyDevice, config.gameBase + 0x390000);
    Write(recoveryExecuteBuffer, config.gameBase + 0x391000);
    state.restoreSuccesses = 7;
    FullscreenSetup::scopeState.busy = 1;
    FullscreenSetup::scopeState.activeToken = 11;
    FullscreenSetup::scopeState.activeThread = TextureLifecycle::ThreadId();
    *reinterpret_cast<DWORD *>(fixture.image + 0x381000) = config.gameBase + 0x3e0000;
    *reinterpret_cast<DWORD *>(fixture.image + 0x3e0114) = reinterpret_cast<DWORD>(&Unblock);
    DWORD old = 0;
    Check(VirtualProtect(fixture.image, 0x600000, PAGE_EXECUTE_READWRITE, &old) != FALSE,
          "Native fixture permissions failed");
    std::memcpy(fixture.image + modeNativeFullscreenReturn, StampNative, sizeof(StampNative));
    for (DWORD offset : StampRelocations)
    {
      auto value = reinterpret_cast<DWORD *>(fixture.image + modeNativeFullscreenReturn + offset);
      *value = *value - 0x400000 + config.gameBase;
    }
    fixture.image[modeNativeFullscreenReturn + sizeof(StampNative)] = 0xc3;
    Check(FlushInstructionCache(GetCurrentProcess(), fixture.image + modeNativeFullscreenReturn,
                                sizeof(StampNative) + 1) != FALSE,
          "Native stamp fixture cache flush failed");
    Callbacks = 0;
  }

  static void NativeCaller()
  {
    const DWORD frame = config.gameBase + 0x381000,
                entry = config.gameBase + modeNativeFullscreenReturn;
    DWORD before = 0, after = 0;
    __asm {
    push ebx
    push edi
    mov ebx,frame
    mov eax,1
    mov before,esp
    call entry
    mov after,esp
    pop edi
    pop ebx
    }
    Check(before == after, "Native callback changed caller stack depth");
  }

  static void RunCases()
  {
    for (bool restore : {false, true})
    {
      Fixture fixture;
      Seed(fixture);
      RestoreDuringUnblock = restore;
      StampPostRestoreRenderer(23, Read(GameRva::drawObject));
      Check(PostRestoreRenderer::baseline.valid && !PostRestoreRenderer::scope.awaitingFrame,
            "Pre-publication hardware setup stamp was rejected");
      Check(!Read(GameRva::directDraw) && !Read(GameRva::fullscreenActive),
            "Payload fabricated native fullscreen flags");
      NativeCaller();
      Check(Callbacks == 1 && Read(GameRva::directDraw) == 1 &&
                Read(GameRva::fullscreenActive) == 1,
            "Native successful caller did not publish flags exactly once");
      Check(PostRestoreRenderer::baseline.restored == 7 &&
                state.restoreSuccesses == (restore ? 8u : 7u),
            "Intervening restore was silently adopted into the initial baseline");
      Check(Read(GameRva::mode) == 0,
            "Bounded prefix unexpectedly performed the later native mode publication");
      Cases.emplace_back(restore ? "restore-after-stamp-before-flags"
                                 : "normal-native-stamp-order");
    }
    for (DWORD rejected : {0u, 1u, 2u, 3u, 4u})
    {
      Fixture fixture;
      Seed(fixture);
      DWORD token = 23, root = Read(GameRva::drawObject);
      if (rejected == 0)
      {
        FullscreenSetup::scopeState.busy = 0;
      }
      if (rejected == 1)
      {
        token = 22;
      }
      if (rejected == 2)
      {
        token = 25;
      }
      if (rejected == 3)
      {
        ++FullscreenSetup::scopeState.activeThread;
      }
      if (rejected == 4)
      {
        root = 0;
      }
      StampPostRestoreRenderer(token, root);
      Check(!PostRestoreRenderer::baseline.valid, "Unowned setup established a renderer baseline");
      Cases.emplace_back("reject-unowned-setup-stamp");
    }
  }
} // namespace StampOrder

int main()
{
  try
  {
    for (DWORD base : {0x20000000u, 0x30000000u})
    {
      RequestedBase = base;
      RunCases();
      StampOrder::RunCases();
    }
    std::printf("{\"passed\":true,\"checks\":%u,\"executions\":%u,\"mappedBases\":2}\n", Checks,
                static_cast<unsigned>(Cases.size()));
    return 0;
  }
  catch (const std::exception &error)
  {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
