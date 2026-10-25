// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_SOFTWARE_MODE_COMPLETION_HPP
#define HAVGAMEPATCHER_SOFTWARE_MODE_COMPLETION_HPP

#include "softwareModeCompletionTypes.hpp"

extern "C"
{
  SoftwareModeCompletionState softwareModeCompletionState = {};
}

namespace SoftwareModeCompletion
{
  constexpr DWORD presentEntry = modeNativePresent, drawEntry = modeNativeDraw,
                  viewVtable = modeNativeViewVtable;
  constexpr DWORD bitBltImport = modeNativeBitBlt, stretchBltImport = modeNativeStretchBlt;
  constexpr DWORD bitBltReturn = modeNativeBitBltReturn,
                  stretchBltReturn = modeNativeStretchBltReturn;
  constexpr DWORD sourceDcRva = modeNativeSourceDc, bitmapRva = modeNativeBitmap;
  constexpr DWORD bitsRva = modeNativeBits, renderBitsRva = modeNativeRenderBits,
                  lowResolutionRva = modeNativeLowResolution;
  using Preservation = HardwareModeCompletion::Preservation;

  static void Increment(DWORD &value)
  {
    if (value == 0xffffffffu)
    {
      softwareModeCompletionState.incomplete = 1;
    }
    else
    {
      ++value;
    }
  }

  static bool Phase()
  {
    return Configured() && state.depth == 1 && !state.ended && !state.videoDepth &&
           !state.mainMenuDepth && !Read(GameRva::mainMenuVisible) && !Read(GameRva::quit) &&
           Read(GameRva::mode) <= 1 && !Read(GameRva::hardware) && !Read(GameRva::directDraw) &&
           !Read(GameRva::fullscreenActive);
  }

  static bool Quiet()
  {
    return !modeAdmissionState.transitionDepth && !state.transitionDepth && !state.busy &&
           !state.setupCleanupBusy && !state.restoreBusy && !textureState.busy && !loadState.busy &&
           !readinessState.busy && !movieRecoveryState.busy && !preMovieRecoveryState.busy &&
           !quadFrameRecoveryState.aborted && !quadFrameRecoveryState.pendingPresent &&
           !TextureTransfer::textureTransferStats.busy && !TextureLifecycle::Incomplete() &&
           !normalFrameState.incomplete && bridgeState.sceneDepth == 0 &&
           !Read(readinessNativePaused) && Read(readinessNativeActive);
  }

  static bool OwnerCurrent(const SoftwareCompletionOwner &o)
  {
    if (!Configured() || config.gameBase != o.base || TextureLifecycle::ThreadId() != o.thread ||
        !o.app || !o.frameObject || !o.frame || !o.viewObject || !o.view ||
        Read(GameRva::app) != o.app || Read(GameRva::view) != o.viewObject ||
        modeAdmissionState.ownerGeneration != o.ownerGeneration ||
        modeAdmissionState.transitionSerial != o.transitionSerial ||
        modeAdmissionState.thread != o.thread || modeAdmissionState.app != o.app ||
        modeAdmissionState.frameObject != o.frameObject || modeAdmissionState.frame != o.frame)
    {
      return false;
    }
    return *reinterpret_cast<volatile DWORD *>(o.app + 0x1c) == o.frameObject &&
           *reinterpret_cast<volatile DWORD *>(o.frameObject + 0x1c) == o.frame &&
           *reinterpret_cast<volatile DWORD *>(o.viewObject + 0x1c) == o.view &&
           *reinterpret_cast<volatile DWORD *>(o.viewObject) == o.base + viewVtable;
  }

  static SoftwareCompletionOwner CaptureOwner()
  {
    SoftwareCompletionOwner o;
    o.base = config.gameBase;
    o.app = modeAdmissionState.app;
    o.frameObject = modeAdmissionState.frameObject;
    o.frame = modeAdmissionState.frame;
    o.viewObject = Read(GameRva::view);
    o.view = o.viewObject ? *reinterpret_cast<volatile DWORD *>(o.viewObject + 0x1c) : 0;
    o.thread = TextureLifecycle::ThreadId();
    o.ownerGeneration = modeAdmissionState.ownerGeneration;
    o.transitionSerial = modeAdmissionState.transitionSerial;
    o.mode = Read(GameRva::mode);
    o.lifecycle = TextureLifecycle::Serial();
    o.frameEpoch = TextureLifecycle::FrameEpoch();
    o.restoreEpoch = state.restoreSuccesses;
    o.frameSerial = TextureLifecycle::FrameSerial();
    o.sourceDc = Read(sourceDcRva);
    o.bitmap = Read(bitmapRva);
    o.bits = Read(bitsRva);
    o.renderBits = Read(renderBitsRva);
    o.width = Read(GameRva::width);
    o.height = Read(GameRva::height);
    o.bpp = Read(GameRva::renderBpp);
    o.lowResolution = Read(lowResolutionRva);
    o.sourceWidth = o.lowResolution ? 320 : o.width;
    o.sourceHeight = o.lowResolution ? 200 : o.height;
    return o;
  }

  static bool Current()
  {
    const auto &s = softwareModeCompletionState;
    const auto &o = s.owner;
    return s.pending && !s.incomplete && OwnerCurrent(o) && Phase() && Quiet() &&
           o.mode == Read(GameRva::mode) && o.lifecycle == TextureLifecycle::Serial() &&
           o.frameEpoch == TextureLifecycle::FrameEpoch() &&
           o.restoreEpoch == state.restoreSuccesses &&
           o.frameSerial == TextureLifecycle::FrameSerial() && o.sourceDc && o.bitmap && o.bits &&
           o.bits == o.renderBits && o.width && o.height && o.bpp == 8 &&
           o.sourceDc == Read(sourceDcRva) && o.bitmap == Read(bitmapRva) &&
           o.bits == Read(bitsRva) && o.renderBits == Read(renderBitsRva) &&
           o.width == Read(GameRva::width) && o.height == Read(GameRva::height) &&
           o.bpp == Read(GameRva::renderBpp) && o.lowResolution == Read(lowResolutionRva);
  }

  static void Invalidate()
  {
    auto &s = softwareModeCompletionState;
    if (s.frameStarted || s.frameReady || s.gdiSuccess)
    {
      Increment(s.invalidations);
    }
    s.frameStarted = s.frameReady = s.gdiSuccess = 0;
  }

  static void Cancel()
  {
    auto &s = softwareModeCompletionState;
    if (s.pending)
    {
      Increment(s.cancelled);
    }
    s.pending = 0;
    Invalidate();
  }

  struct ScopeCall
  {
    DWORD nativeEntry, tracked, kind, generation, frame, presentGeneration, paintGeneration, thread;
    DWORD view, dc, destination, calls, failures, reserved[3];
  };

  static_assert(sizeof(ScopeCall) == 64, "Native scoped wrapper stack reservation");
  static DWORD destinationDc = 0, paintDc = 0;
  static DWORD activePresentDepth = 0, activePaintDepth = 0;

  static bool SkipShutdownMenuPresent(void *application, const ScopeCall &call)
  {
    const DWORD app = reinterpret_cast<DWORD>(application);
    const DWORD slot = reinterpret_cast<DWORD>(&call);
    const auto &owner = modeAdmissionState;
    DWORD caller = 0;
    // The native demo handler may show its menu after a nested close has
    // cleared the frame. That exact caller ignores the presenter's result.
    return Configured() && app && !(app & 3) && app <= 0xffffffe0u && slot <= 0xffffffbfu &&
           owner.ownerGeneration && owner.imageBase == config.gameBase && owner.app == app &&
           owner.thread && owner.thread == TextureLifecycle::ThreadId() && owner.frameObject &&
           owner.frame && Read(GameRva::app) == app && Read(GameRva::quit) == 1 &&
           Read(GameRva::mainMenuVisible) == 1 && !Read(GameRva::gameStage) &&
           Read(GameRva::fullscreenActive) &&
           NormalFrameOwnership::StackRead(slot + sizeof(call), &caller, sizeof(caller)) &&
           caller == config.gameBase + modeNativeMenuPresentReturn &&
           !*reinterpret_cast<const volatile DWORD *>(app + 0x1c);
  }

  static void BeginScope(void *object, void *dc, DWORD kind, ScopeCall &call)
  {
    Preservation preserve;
    call.nativeEntry = config.gameBase + (kind ? drawEntry : presentEntry);
    call.tracked = 0;
    call.thread = TextureLifecycle::ThreadId();
    call.reserved[0] = 0;
    DWORD &activeDepth = kind ? activePaintDepth : activePresentDepth;
    if (activeDepth == 0xffffffffu)
    {
      softwareModeCompletionState.incomplete = 1;
      return;
    }
    ++activeDepth;
    call.reserved[0] = kind ? 2u : 1u;
    auto &s = softwareModeCompletionState;
    if (!s.pending || !Configured() || TextureLifecycle::ThreadId() != s.owner.thread)
    {
      return;
    }

    const bool owned = reinterpret_cast<DWORD>(object) == (kind ? s.owner.viewObject : s.owner.app);
    if (!owned || !OwnerCurrent(s.owner))
    {
      if (s.presentDepth)
      {
        Invalidate();
      }
      return;
    }
    if (kind && !s.presentDepth)
    {
      return;
    }
    call.tracked = 1;
    call.kind = kind;
    call.generation = s.generation;
    call.frame = s.owner.frameSerial;
    call.thread = s.owner.thread;
    call.calls = s.gdiCalls;
    call.failures = s.gdiFailures;
    if (kind)
    {
      Increment(s.paintDepth);
      Increment(s.paintGeneration);
      Increment(s.paintCalls);
      call.paintGeneration = s.paintGeneration;
      call.presentGeneration = s.presentGeneration;
      call.view = reinterpret_cast<DWORD>(object);
      call.dc = reinterpret_cast<DWORD>(dc);
      call.destination = dc ? reinterpret_cast<volatile DWORD *>(dc)[1] : 0;
      if (s.paintDepth != 1 || s.presentDepth != 1 || !call.destination || !s.frameReady ||
          !Current())
      {
        Increment(s.nested);
        Invalidate();
      }
      else
      {
        paintDc = call.dc;
        destinationDc = call.destination;
      }
    }
    else
    {
      Increment(s.presentDepth);
      Increment(s.presentGeneration);
      Increment(s.presentCalls);
      call.presentGeneration = s.presentGeneration;
      s.gdiSuccess = 0;
      if (s.presentDepth != 1 || s.paintDepth || !s.frameReady || !Current())
      {
        Increment(s.nested);
        Invalidate();
      }
    }
  }

  static bool SameCall(const ScopeCall &call)
  {
    const auto &s = softwareModeCompletionState;
    return call.generation == s.generation && call.frame == s.owner.frameSerial &&
           call.presentGeneration == s.presentGeneration && s.frameReady && Current() &&
           s.gdiCalls == call.calls + 1 && s.gdiFailures == call.failures && s.gdiSuccess == 1;
  }

  static void EndScope(const ScopeCall &call)
  {
    Preservation preserve;

    struct ActiveScope
    {
      const ScopeCall &call;

      ~ActiveScope()
      {
        if (call.thread == TextureLifecycle::ThreadId())
        {
          DWORD *depth = call.reserved[0] == 1   ? &activePresentDepth
                         : call.reserved[0] == 2 ? &activePaintDepth
                                                 : nullptr;
          if (depth && *depth && *depth != 0xffffffffu)
          {
            --*depth;
          }
        }
      }
    } active{call};

    auto &s = softwareModeCompletionState;
    if (!call.tracked || TextureLifecycle::ThreadId() != call.thread)
    {
      return;
    }
    bool candidate = false;
    {

      if (call.kind)
      {
        const bool valid = s.paintDepth == 1 && s.presentDepth == 1 &&
                           s.paintGeneration == call.paintGeneration && SameCall(call) &&
                           paintDc == call.dc && destinationDc == call.destination &&
                           *reinterpret_cast<volatile DWORD *>(call.dc + 4) == call.destination;
        if (!valid)
        {
          Invalidate();
        }
        if (s.paintDepth)
        {
          --s.paintDepth;
        }
        return;
      }
      candidate = s.presentDepth == 1 && !s.paintDepth && SameCall(call);
      if (s.presentDepth)
      {
        --s.presentDepth;
      }
      if (!candidate)
      {
        Invalidate();
        return;
      }
    }
    ModeAdmission::Owner owner(s.owner.base, reinterpret_cast<void *>(s.owner.app));
    DWORD now = 0;
    const bool clock = ModeAdmission::Tick(owner, s.owner.ownerGeneration, now);

    if (!clock || s.presentDepth || s.paintDepth || !SameCall(call))
    {
      if (!clock)
      {
        Increment(s.clockFailures);
      }
      Invalidate();
      return;
    }
    {

      modeAdmissionState.completedAt = now;
      modeAdmissionState.clockValid = 1;
    }
    s.completedAt = now;
    s.pending = s.frameReady = s.frameStarted = s.gdiSuccess = 0;
    Increment(s.completed);
    Increment(s.presentWitnesses);
  }

  struct GdiCall
  {
    DWORD valid, generation, frame, present, paint, calls, failures;
    DWORD dc, destination, source;
  };

  static GdiCall BeforeGdi(DWORD caller,
                           HDC destination,
                           HDC source,
                           int x,
                           int y,
                           int width,
                           int height,
                           int sourceX,
                           int sourceY,
                           int sourceWidth,
                           int sourceHeight,
                           DWORD operation,
                           bool stretch)
  {
    Preservation preserve;
    GdiCall call = {};
    auto &s = softwareModeCompletionState;
    if (!s.pending || !Configured() || TextureLifecycle::ThreadId() != s.owner.thread ||
        !s.presentDepth || !s.paintDepth)
    {
      return call;
    }

    Increment(s.gdiCalls);
    if (s.presentDepth != 1 || s.paintDepth != 1 || !s.frameReady || !Current() ||
        caller != s.owner.base + (stretch ? stretchBltReturn : bitBltReturn) ||
        reinterpret_cast<DWORD>(destination) != destinationDc || !paintDc ||
        *reinterpret_cast<volatile DWORD *>(paintDc + 4) != destinationDc ||
        reinterpret_cast<DWORD>(source) != s.owner.sourceDc || x || y || sourceX || sourceY ||
        width <= 0 || height <= 0 || sourceWidth != static_cast<int>(s.owner.sourceWidth) ||
        sourceHeight != static_cast<int>(s.owner.sourceHeight) || operation != SRCCOPY)
    {
      Invalidate();
      return call;
    }
    call.valid = 1;
    call.generation = s.generation;
    call.frame = s.owner.frameSerial;
    call.present = s.presentGeneration;
    call.paint = s.paintGeneration;
    call.calls = s.gdiCalls;
    call.failures = s.gdiFailures;
    call.dc = paintDc;
    call.destination = destinationDc;
    call.source = s.owner.sourceDc;
    return call;
  }

  static void AfterGdi(const GdiCall &call, BOOL result, DWORD error)
  {
    Preservation preserve;
    auto &s = softwareModeCompletionState;
    if (!call.valid || TextureLifecycle::ThreadId() != s.owner.thread)
    {
      return;
    }

    s.nativeResult = static_cast<DWORD>(result);
    s.nativeError = error;
    if (!result)
    {
      Increment(s.gdiFailures);
    }
    if (!result || !Current() || !s.frameReady || s.presentDepth != 1 || s.paintDepth != 1 ||
        call.generation != s.generation || call.frame != s.owner.frameSerial ||
        call.present != s.presentGeneration || call.paint != s.paintGeneration ||
        call.calls != s.gdiCalls || call.failures != s.gdiFailures || call.dc != paintDc ||
        call.destination != destinationDc || call.source != s.owner.sourceDc ||
        *reinterpret_cast<volatile DWORD *>(call.dc + 4) != call.destination)
    {
      Invalidate();
      return;
    }
    s.gdiSuccess = 1;
  }
} // namespace SoftwareModeCompletion

extern "C" void __cdecl SoftwareCompletionBeginTransition(void *app, DWORD token)
{
  using namespace SoftwareModeCompletion;
  Preservation preserve;
  if (!Configured() || !token || modeAdmissionState.transitionDepth != 1 ||
      modeAdmissionState.ownerGeneration != token ||
      modeAdmissionState.app != reinterpret_cast<DWORD>(app) ||
      modeAdmissionState.thread != TextureLifecycle::ThreadId())
  {
    return;
  }

  auto &s = softwareModeCompletionState;
  s.startMode = Read(GameRva::mode);
  s.startPending = s.pending && s.owner.ownerGeneration == token;
  s.transitionTracked = token;
  Invalidate();
}

extern "C" void __cdecl SoftwareCompletionEndTransition(void *app, DWORD token)
{
  using namespace SoftwareModeCompletion;
  Preservation preserve;
  auto &s = softwareModeCompletionState;
  if (!Configured() || !token || modeAdmissionState.transitionDepth ||
      s.transitionTracked != token || modeAdmissionState.ownerGeneration != token ||
      modeAdmissionState.app != reinterpret_cast<DWORD>(app) ||
      modeAdmissionState.thread != TextureLifecycle::ThreadId())
  {
    return;
  }

  s.transitionTracked = 0;
  if (!Phase() || (s.startMode == Read(GameRva::mode) && !s.startPending))
  {
    Cancel();
    return;
  }
  s.owner = CaptureOwner();
  if (!OwnerCurrent(s.owner))
  {
    Cancel();
    return;
  }
  Increment(s.generation);
  Increment(s.armed);
  s.pending = 1;
  s.completedAt = 0;
  Invalidate();
}

extern "C" BOOL __cdecl SoftwareCompletionSuppress(void *app, DWORD token)
{
  using namespace SoftwareModeCompletion;
  Preservation preserve;
  auto &s = softwareModeCompletionState;
  if (!s.pending || !Configured() || TextureLifecycle::ThreadId() != s.owner.thread)
  {
    return FALSE;
  }

  if (reinterpret_cast<DWORD>(app) != s.owner.app || token != s.owner.ownerGeneration ||
      !OwnerCurrent(s.owner) || !Phase())
  {
    Cancel();
    return FALSE;
  }
  Increment(s.suppressed);
  return TRUE;
}

extern "C" void __cdecl SoftwareCompletionBeginFrame(const DWORD *registers)
{
  using namespace SoftwareModeCompletion;
  Preservation preserve;
  auto &s = softwareModeCompletionState;
  if (!s.pending || !Configured() || TextureLifecycle::ThreadId() != s.owner.thread)
  {
    return;
  }

  Increment(s.frameDepth);
  Invalidate();
  if (!OwnerCurrent(s.owner) || !Phase())
  {
    Cancel();
    return;
  }
  s.owner = CaptureOwner();
  DWORD caller = 0;
  const DWORD slot = registers[3] + 4;
  if (s.frameDepth != 1 || s.presentDepth || s.paintDepth || !Current() ||
      !NormalFrameOwnership::StackRead(slot, &caller, 4) ||
      caller != s.owner.base + readinessOuterFrameReturn)
  {
    return;
  }
  s.frameSlot = slot;
  s.frameGeneration = s.generation;
  s.frameStarted = 1;
}

extern "C" void __cdecl SoftwareCompletionFinishFrame(const DWORD *registers)
{
  using namespace SoftwareModeCompletion;
  Preservation preserve;
  auto &s = softwareModeCompletionState;
  if (!s.frameDepth || TextureLifecycle::ThreadId() != s.owner.thread)
  {
    return;
  }

  DWORD caller = 0;
  const DWORD slot = registers[3] + 4;
  const bool valid = s.frameDepth == 1 && s.frameStarted && s.frameGeneration == s.generation &&
                     s.frameSlot == slot && !s.presentDepth && !s.paintDepth && Current() &&
                     NormalFrameOwnership::StackRead(slot, &caller, 4) &&
                     caller == s.owner.base + readinessOuterFrameReturn;
  --s.frameDepth;
  s.frameStarted = 0;
  if (!valid)
  {
    Invalidate();
    return;
  }
  s.frameReady = 1;
  Increment(s.frameWitnesses);
}

extern "C" BOOL __cdecl PrepareSoftwarePresent(void *app, SoftwareModeCompletion::ScopeCall *call)
{
  ModeAdmission::LastError preserve;
  if (SoftwareModeCompletion::SkipShutdownMenuPresent(app, *call))
  {
    return TRUE;
  }
  SoftwareModeCompletion::BeginScope(app, nullptr, 0, *call);
  return FALSE;
}

extern "C" void __cdecl PrepareSoftwareDraw(void *view,
                                            void *dc,
                                            SoftwareModeCompletion::ScopeCall *call)
{
  SoftwareModeCompletion::BeginScope(view, dc, 1, *call);
}

extern "C" void __cdecl CompleteSoftwareScope(const SoftwareModeCompletion::ScopeCall *call)
{
  SoftwareModeCompletion::EndScope(*call);
}

extern "C" BOOL WINAPI ObserveSoftwareBitBlt(HDC destination,
                                             int x,
                                             int y,
                                             int width,
                                             int height,
                                             HDC source,
                                             int sourceX,
                                             int sourceY,
                                             DWORD operation)
{
  using namespace SoftwareModeCompletion;
  const auto native = *reinterpret_cast<decltype(&::BitBlt) *>(config.gameBase + bitBltImport);
  const auto call = BeforeGdi(reinterpret_cast<DWORD>(_ReturnAddress()), destination, source, x, y,
                              width, height, sourceX, sourceY, width, height, operation, false);
  const BOOL result = native(destination, x, y, width, height, source, sourceX, sourceY, operation);
  const DWORD error = __readfsdword(0x34);
  AfterGdi(call, result, error);
  return result;
}

extern "C" BOOL WINAPI ObserveSoftwareStretchBlt(HDC destination,
                                                 int x,
                                                 int y,
                                                 int width,
                                                 int height,
                                                 HDC source,
                                                 int sourceX,
                                                 int sourceY,
                                                 int sourceWidth,
                                                 int sourceHeight,
                                                 DWORD operation)
{
  using namespace SoftwareModeCompletion;
  const auto native =
      *reinterpret_cast<decltype(&::StretchBlt) *>(config.gameBase + stretchBltImport);
  const auto call =
      BeforeGdi(reinterpret_cast<DWORD>(_ReturnAddress()), destination, source, x, y, width, height,
                sourceX, sourceY, sourceWidth, sourceHeight, operation, true);
  const BOOL result = native(destination, x, y, width, height, source, sourceX, sourceY,
                             sourceWidth, sourceHeight, operation);
  const DWORD error = __readfsdword(0x34);
  AfterGdi(call, result, error);
  return result;
}

extern "C" __declspec(naked) void SoftwarePresentHook()
{
  __asm {
    lea esp,[esp-64]
    pushfd
    pushad
    mov ebx,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    cld
    lea eax,[ebx+36]
    push eax
    push dword ptr [ebx+24]
    call PrepareSoftwarePresent
    add esp,8
    test eax,eax
    jnz Shutdown
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    call dword ptr [esp]
    pushfd
    pushad
    mov ebx,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    cld
    lea eax,[ebx+36]
    push eax
    call CompleteSoftwareScope
    add esp,4
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    lea esp,[esp+64]
    ret
  Shutdown:
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    lea esp,[esp+64]
    ret
  }
}

extern "C" __declspec(naked) void SoftwareDrawHook()
{
  __asm {
    lea esp,[esp-64]
    pushfd
    pushad
    mov ebx,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    cld
    lea eax,[ebx+36]
    push eax
    push dword ptr [ebx+104]
    push dword ptr [ebx+24]
    call PrepareSoftwareDraw
    add esp,12
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    push dword ptr [esp+68]
    call dword ptr [esp+4]
    pushfd
    pushad
    mov ebx,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    cld
    lea eax,[ebx+36]
    push eax
    call CompleteSoftwareScope
    add esp,4
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    lea esp,[esp+64]
    ret 4
  }
}

#endif // HAVGAMEPATCHER_SOFTWARE_MODE_COMPLETION_HPP
