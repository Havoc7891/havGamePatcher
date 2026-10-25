// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_FAILED_PRESENTATION_RECOVERY_HPP
#define HAVGAMEPATCHER_FAILED_PRESENTATION_RECOVERY_HPP

extern "C"
{
  FailedPresentationState failedPresentationState = {};
}
static DWORD failedPresentationNativeTarget = 0;
static DWORD failedPresentationReplayBase = 0;

namespace FailedPresentation
{
  using Preservation = HardwareModeCompletion::Preservation;
  using Reference = DisplayReadiness::Reference;
  using Surface = SurfaceRestoreOwnership::SurfaceMethod;
  constexpr DWORD nativeFailure = presentationNativeFailure,
                  nativeFailureResume = presentationNativeFailureResume,
                  nativeFailureMessage = presentationNativeFailureMessage;

  struct Frame
  {
    HardwareCompletionOwner owner;
    DWORD serial, upload, begin, end, failures, creates, created, quadGeneration, quadSlot;
    DWORD cachePasses, loadCalls, transferGeneration, transferLoads, transferInvalidations;
  } frame = {}, pendingFrame = {};

  static DWORD scopeDepth = 0, scopeToken = 0, flipToken = 0, flipOrdinal = 0;
  static DWORD flipStack = 0, flipError = 0, flipResult = 0;
  static DWORD outerSlot = 0, nativeSlot = 0, nativeContinuation = 0;
  static bool incomplete = false, flipValid = false, failureArmed = false;

  static void Count(DWORD &value)
  {
    if (value != 0xffffffffu)
    {
      ++value;
    }
  }

  static bool Phase(bool inputDispatch = false)
  {
    return QuadFrameRecovery::Phase() && QuadFrameRecovery::Unlocked() &&
           !quadFrameRecoveryState.depth && !quadFrameRecoveryState.armed &&
           !quadFrameRecoveryState.busy && !quadFrameRecoveryState.aborted &&
           !quadFrameRecoveryState.pendingPresent && !quadFrameRecoveryState.awaitingFrame &&
           !modeAdmissionState.transitionDepth &&
           (!modeAdmissionState.inputDepth ||
            (inputDispatch && modeAdmissionState.inputDepth == 1)) &&
           !textureState.busy && !loadState.busy && !TextureTransfer::textureTransferStats.busy &&
           !TextureTransfer::textureTransferStats.incompleteFlags && !bridgeState.sceneDepth &&
           bridgeState.beginCalls == bridgeState.endCalls &&
           bridgeState.lastStage == D3DBridge::end && bridgeState.lastResult == S_OK;
  }

  static bool FrameCurrent(bool requireCache = true, bool inputDispatch = false)
  {
    const auto &f = quadFrameRecoveryState;
    return !incomplete && Phase(inputDispatch) &&
           HardwareModeCompletion::ResourcesCurrent(frame.owner) &&
           frame.serial == TextureLifecycle::FrameSerial() &&
           frame.upload == TextureLifecycle::UploadSerial() &&
           frame.begin == bridgeState.beginCalls && frame.end == bridgeState.endCalls &&
           frame.failures == bridgeState.failures && frame.creates == bridgeState.createAttempts &&
           frame.created == bridgeState.createSuccesses && f.frameReturned &&
           f.generation == frame.quadGeneration && f.ownerSlot == frame.quadSlot &&
           f.ownerThread == frame.owner.thread && frame.cachePasses == textureState.passes &&
           frame.loadCalls == loadState.calls &&
           frame.transferGeneration == TextureTransfer::textureTransferStats.generation &&
           frame.transferLoads == TextureTransfer::textureTransferStats.loads &&
           frame.transferInvalidations == TextureTransfer::textureTransferStats.invalidations &&
           (!requireCache || (textureState.pendingEpoch == state.restoreSuccesses &&
                              textureState.completedEpoch == state.restoreSuccesses &&
                              textureState.lastResult == S_OK));
  }

  static bool PendingOwner()
  {
    return HardwareModeCompletion::ResourcesCurrent(pendingFrame.owner) &&
           TextureLifecycle::ThreadId() == pendingFrame.owner.thread;
  }

  static bool ScopeCurrent()
  {
    DWORD outer = 0, native = 0;
    return scopeDepth == 1 && nativeSlot && outerSlot > nativeSlot &&
           QuadFrameRecovery::StackRead(outerSlot, &outer, 4) &&
           outer == frame.owner.base + presentationGameReturn &&
           QuadFrameRecovery::StackRead(nativeSlot, &native, 4) && native == nativeContinuation;
  }

  struct Pins
  {
    DisplayReadiness::Context identity{DisplayReadiness::Phase::textureRecovery};
    SurfaceRestoreOwnership::Owner surface;
    DWORD token = flipToken, ordinal = flipOrdinal, slot = flipStack;
    DWORD drawAdd = 0, drawRelease = 0, primaryAdd = 0, primaryRelease = 0;
    DWORD primaryLost = 0, primaryRestore = 0, backbufferAdd = 0, backbufferRelease = 0,
          backbufferLost = 0;

    Pins()
    {
      if (identity.draw && identity.primary && identity.backbuffer)
      {
        identity.drawMethods = *reinterpret_cast<DWORD **>(identity.draw);
        identity.primaryMethods = *reinterpret_cast<DWORD **>(identity.primary);
        identity.backbufferMethods = *reinterpret_cast<DWORD **>(identity.backbuffer);
        if (identity.drawMethods && identity.primaryMethods && identity.backbufferMethods)
        {
          drawAdd = identity.drawMethods[1];
          drawRelease = identity.drawMethods[2];
          primaryAdd = identity.primaryMethods[1];
          primaryRelease = identity.primaryMethods[2];
          primaryLost = identity.primaryMethods[24];
          primaryRestore = identity.primaryMethods[27];
          backbufferAdd = identity.backbufferMethods[1];
          backbufferRelease = identity.backbufferMethods[2];
          backbufferLost = identity.backbufferMethods[24];
        }
      }
    }

    bool Current(bool restoring) const
    {
      return failedPresentationState.busy == 1 && !failedPresentationState.pending &&
             ScopeCurrent() && token && token == scopeToken && flipToken == token &&
             ordinal == flipOrdinal && ordinal == flipProbeState.calls && !flipProbeState.busy &&
             slot == flipStack && flipResult == static_cast<DWORD>(DDERR_SURFACELOST) &&
             state.restoreBusy == static_cast<DWORD>(restoring) && FrameCurrent(false) &&
             identity.IdentityCurrent() && drawAdd && drawRelease && primaryAdd && primaryRelease &&
             primaryLost && primaryRestore && backbufferAdd && backbufferRelease &&
             backbufferLost && identity.drawMethods && identity.primaryMethods &&
             identity.backbufferMethods && identity.drawMethods[1] == drawAdd &&
             identity.drawMethods[2] == drawRelease && identity.primaryMethods[1] == primaryAdd &&
             identity.primaryMethods[2] == primaryRelease &&
             identity.primaryMethods[24] == primaryLost &&
             identity.primaryMethods[27] == primaryRestore &&
             identity.backbufferMethods[1] == backbufferAdd &&
             identity.backbufferMethods[2] == backbufferRelease &&
             identity.backbufferMethods[24] == backbufferLost;
    }

    bool AcceptRestore()
    {
      if (identity.restored == 0xffffffffu || state.restoreSuccesses != identity.restored + 1)
      {
        return false;
      }
      ++identity.restored;
      ++surface.restoreEpoch;
      ++frame.owner.restoreEpoch;
      return Current(true) && surface.Current(5);
    }
  };

  static HRESULT Check(Pins &pins, DisplayReadiness::Budget &budget, bool restoring)
  {
    if (!pins.Current(restoring) || !budget.initialized || !budget.clock)
    {
      return E_UNEXPECTED;
    }
    const DWORD now = budget.clock();
    if (!pins.Current(restoring))
    {
      return E_UNEXPECTED;
    }
    return now - budget.started < DisplayReadiness::timeoutMilliseconds
               ? S_OK
               : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
  }

  struct References
  {
    void *objects[3] = {};
    Reference drops[3] = {};
    DWORD count = 0;

    bool Acquire(DWORD address)
    {
      if (!address || count == 3)
      {
        return false;
      }
      auto object = reinterpret_cast<void *>(address);
      const auto methods = *reinterpret_cast<DWORD **>(object);
      if (!methods || !methods[1] || !methods[2])
      {
        return false;
      }
      const auto add = reinterpret_cast<Reference>(methods[1]);
      objects[count] = object;
      drops[count++] = reinterpret_cast<Reference>(methods[2]);
      add(object);
      return true;
    }

    HRESULT Close(Pins &pins, DisplayReadiness::Budget &budget)
    {
      HRESULT result = S_OK;
      while (count)
      {
        --count;
        drops[count](objects[count]);
        const HRESULT checked = Check(pins, budget, true);
        if (checked != S_OK)
        {
          result = checked;
        }
      }
      return result;
    }
  };

  static HRESULT Restore(Pins &pins, DisplayReadiness::Budget &budget)
  {
    if (pins.identity.restored == 0xffffffffu || Check(pins, budget, false) != S_OK)
    {
      return E_UNEXPECTED;
    }

    struct Scope
    {
      Scope()
      {
        state.restoreBusy = 1;
      }

      ~Scope()
      {
        state.restoreBusy = 0;
      }
    } scope;

    References references;
    HRESULT checked = S_OK;
    auto current = [&]
    {
      checked = Check(pins, budget, true);
      return checked == S_OK && pins.surface.Current(5);
    };
    HRESULT result = E_UNEXPECTED;
    if (current() && references.Acquire(pins.identity.draw) && current() &&
        references.Acquire(pins.identity.primary) && current() &&
        references.Acquire(pins.identity.backbuffer) && current())
    {
      const SurfaceRestoreOwnership::Methods methods(pins.identity.primaryMethods,
                                                     pins.identity.backbufferMethods);
      const HRESULT primary = methods.primaryLost(reinterpret_cast<void *>(pins.identity.primary));
      if (current() && (primary == S_OK || primary == DDERR_SURFACELOST))
      {
        const HRESULT back =
            methods.backbufferLost(reinterpret_cast<void *>(pins.identity.backbuffer));
        if (current() && (back == S_OK || back == DDERR_SURFACELOST) &&
            (primary == DDERR_SURFACELOST || back == DDERR_SURFACELOST))
        {
          result = SurfaceRestoreOwnership::RestoreAndVerify(pins.surface, methods, current);
          failedPresentationState.restoreResult = result;
          if (result == S_OK && !pins.AcceptRestore())
          {
            result = E_UNEXPECTED;
          }
        }
      }
    }
    if (checked != S_OK)
    {
      result = checked;
    }
    const HRESULT closed = references.Close(pins, budget);
    if (closed != S_OK)
    {
      result = closed;
    }
    if (result == S_OK)
    {
      result = Check(pins, budget, true);
    }
    return result;
  }

  static bool Reload(Pins &pins, DisplayReadiness::Budget &budget)
  {
    if (Check(pins, budget, false) != S_OK)
    {
      return false;
    }
    const DWORD pass = textureState.passes, success = textureState.successes,
                failure = textureState.failures, reloaded = textureState.texturesReloaded;
    const DWORD captures = TextureTransfer::textureTransferStats.captures,
                staged = TextureTransfer::textureTransferStats.staged,
                admitted = TextureTransfer::textureTransferStats.admitted,
                copied = TextureTransfer::textureTransferStats.copied,
                skipped = TextureTransfer::textureTransferStats.skipped,
                failed = TextureTransfer::textureTransferStats.failed,
                nested = TextureTransfer::textureTransferStats.nestedEntries,
                ready = TextureTransfer::textureTransferStats.readyGeneration;
    const bool recovered = RecoverLostTextures() != 0;
    failedPresentationState.cacheResult = textureState.lastResult;
    const DWORD reloads = textureState.texturesReloaded - reloaded;
    using QuadFrameRecovery::Delta;
    if (!recovered || textureState.texturesReloaded < reloaded || reloads > 1024 ||
        !Delta(pass, textureState.passes, 1) || !Delta(success, textureState.successes, 1) ||
        textureState.failures != failure || textureState.lastResult != S_OK ||
        textureState.pendingEpoch != state.restoreSuccesses ||
        textureState.completedEpoch != state.restoreSuccesses ||
        !Delta(frame.upload, TextureLifecycle::UploadSerial(), reloads) ||
        // The cache route admits directly and does not enter ordinary-load counters.
        frame.loadCalls != loadState.calls ||
        !Delta(captures, TextureTransfer::textureTransferStats.captures, reloads) ||
        !Delta(frame.transferLoads, TextureTransfer::textureTransferStats.loads, reloads) ||
        !Delta(frame.transferGeneration, TextureTransfer::textureTransferStats.generation,
               reloads * 2) ||
        !Delta(frame.transferInvalidations, TextureTransfer::textureTransferStats.invalidations,
               (ready && reloads) ? 1 : 0) ||
        !Delta(staged, TextureTransfer::textureTransferStats.staged, reloads) ||
        !Delta(admitted, TextureTransfer::textureTransferStats.admitted, reloads) ||
        !Delta(copied, TextureTransfer::textureTransferStats.copied, reloads) ||
        TextureTransfer::textureTransferStats.skipped != skipped ||
        TextureTransfer::textureTransferStats.failed != failed ||
        TextureTransfer::textureTransferStats.nestedEntries != nested ||
        TextureTransfer::textureTransferStats.activeGeneration ||
        TextureTransfer::textureTransferStats.readyGeneration != (reloads ? 0 : ready))
    {
      return false;
    }
    frame.upload = pins.identity.upload = TextureLifecycle::UploadSerial();
    frame.cachePasses = textureState.passes;
    frame.loadCalls = loadState.calls;
    frame.transferGeneration = TextureTransfer::textureTransferStats.generation;
    frame.transferLoads = TextureTransfer::textureTransferStats.loads;
    frame.transferInvalidations = TextureTransfer::textureTransferStats.invalidations;
    pins.identity.pending = textureState.pendingEpoch;
    pins.identity.completed = textureState.completedEpoch;
    return FrameCurrent() && Check(pins, budget, false) == S_OK;
  }
} // namespace FailedPresentation

extern "C" void __cdecl FailedPresentationFinishFrame()
{
  using namespace FailedPresentation;
  Preservation preserve;
  auto &s = failedPresentationState;
  s.frameReady = 0;
  flipValid = failureArmed = false;
  if (s.busy || scopeDepth || incomplete || !QuadFrameRecovery::SameOwner() ||
      !QuadFrameRecovery::Unlocked() || !quadFrameRecoveryState.frameReturned ||
      !HardwareModeCompletion::Quiet() || bridgeState.sceneDepth ||
      bridgeState.beginCalls != bridgeState.endCalls || bridgeState.lastStage != D3DBridge::end ||
      bridgeState.lastResult != S_OK)
  {
    return;
  }
  if (s.pending && !PendingOwner())
  {
    s.pending = 0;
  }
  if (s.generation == 0xffffffffu)
  {
    incomplete = true;
    return;
  }
  ++s.generation;
  frame.owner = HardwareModeCompletion::CaptureOwner();
  frame.serial = TextureLifecycle::FrameSerial();
  frame.upload = TextureLifecycle::UploadSerial();
  frame.begin = bridgeState.beginCalls;
  frame.end = bridgeState.endCalls;
  frame.failures = bridgeState.failures;
  frame.creates = bridgeState.createAttempts;
  frame.created = bridgeState.createSuccesses;
  frame.quadGeneration = quadFrameRecoveryState.generation;
  frame.quadSlot = quadFrameRecoveryState.ownerSlot;
  frame.cachePasses = textureState.passes;
  frame.loadCalls = loadState.calls;
  frame.transferGeneration = TextureTransfer::textureTransferStats.generation;
  frame.transferLoads = TextureTransfer::textureTransferStats.loads;
  frame.transferInvalidations = TextureTransfer::textureTransferStats.invalidations;
  if (s.pending && frame.serial <= pendingFrame.serial)
  {
    return;
  }
  s.frameReady = 1;
}

extern "C" DWORD __cdecl BeginFailedPresentationScope(const DWORD *outerReturn)
{
  using namespace FailedPresentation;
  Preservation preserve;
  flipValid = failureArmed = false;
  if (scopeDepth == 0xffffffffu)
  {
    incomplete = true;
    return 0;
  }
  ++scopeDepth;
  DWORD outer = 0;
  if (scopeDepth != 1 || failedPresentationState.busy || !failedPresentationState.frameReady ||
      !FrameCurrent() ||
      !QuadFrameRecovery::StackRead(reinterpret_cast<DWORD>(outerReturn), &outer, 4) ||
      outer != frame.owner.base + presentationGameReturn)
  {
    failedPresentationState.frameReady = 0;
    return 0;
  }
  scopeToken = failedPresentationState.generation;
  outerSlot = reinterpret_cast<DWORD>(outerReturn);
  nativeSlot = nativeContinuation = 0;
  return scopeToken;
}

extern "C" void __cdecl PrepareFailedPresentationNativeCall(const DWORD *registers,
                                                            DWORD continuation)
{
  using namespace FailedPresentation;
  Preservation preserve;
  failedPresentationNativeTarget = config.gameBase + recoveryHardwarePresentOriginal;
  failedPresentationReplayBase = config.gameBase;
  if (scopeDepth != 1 || !scopeToken || !failedPresentationState.frameReady || !FrameCurrent())
  {
    return;
  }
  // The shim restores its entry ESP before CALL. The next return occupies the
  // same slot as PUSHFD, whose address PUSHAD captured in registers[3].
  nativeSlot = registers[3];
  nativeContinuation = continuation;
}

extern "C" void __cdecl RecordFailedPresentationBase(DWORD base)
{
  // The outer observer publishes after all callback/recovery activity. The
  // following native failure hook snapshots this value before its own callbacks.
  failedPresentationReplayBase = base;
}

extern "C" __declspec(naked) int __cdecl CallFailedPresentationOriginal()
{
  __asm {
    pushfd
    pushad
    mov ebx,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    cld
    lea eax,Returned
    push eax
    push ebx
    call PrepareFailedPresentationNativeCall
    add esp,8
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    call dword ptr [failedPresentationNativeTarget]
  Returned:
    ret
  }
}

extern "C" void __cdecl EndFailedPresentationScope(DWORD token)
{
  using namespace FailedPresentation;
  Preservation preserve;
  if (token && token != scopeToken)
  {
    incomplete = true;
  }
  failedPresentationState.frameReady = 0;
  flipValid = failureArmed = false;
  if (scopeDepth)
  {
    --scopeDepth;
  }
  if (!scopeDepth)
  {
    FinishPresentationInput();
  }
}

extern "C" BOOL __cdecl FailedPresentationSuppress(void *app, DWORD token)
{
  using namespace FailedPresentation;
  Preservation preserve;
  auto &s = failedPresentationState;
  if (!s.pending && !s.busy)
  {
    return FALSE;
  }
  const auto &owner = s.busy ? frame.owner : pendingFrame.owner;
  if (reinterpret_cast<DWORD>(app) != owner.app || token != owner.ownerGeneration)
  {
    return FALSE;
  }
  if (!s.busy && (!HardwareModeCompletion::Gameplay() || !PendingOwner()))
  {
    s.pending = 0;
    return FALSE;
  }
  return TRUE;
}

extern "C" void __cdecl CaptureFailedPresentationFlip(DWORD surface,
                                                      DWORD target,
                                                      DWORD flags,
                                                      DWORD caller,
                                                      DWORD depth,
                                                      DWORD ordinal,
                                                      const DWORD *nativeStack)
{
  using namespace FailedPresentation;
  Preservation preserve;
  flipValid = failureArmed = false;
  DWORD chain[28];
  if (scopeDepth != 1 || depth != 1 || failedPresentationState.busy ||
      !failedPresentationState.frameReady || !scopeToken || !FrameCurrent() ||
      surface != frame.owner.primary || target || flags != 1 ||
      caller != frame.owner.base + recoveryGenericFlipCaller || !nativeStack ||
      !QuadFrameRecovery::StackRead(reinterpret_cast<DWORD>(nativeStack), chain, sizeof(chain)) ||
      chain[0] != caller || chain[25] != frame.owner.base + presentationAppReturn ||
      chain[26] != frame.owner.base + presentationWrapperReturn || !ScopeCurrent() ||
      reinterpret_cast<DWORD>(nativeStack) > 0xffffff93u ||
      reinterpret_cast<DWORD>(nativeStack) + 27 * 4 != nativeSlot ||
      chain[27] != nativeContinuation)
  {
    failedPresentationState.frameReady = 0;
    return;
  }
  flipToken = scopeToken;
  flipOrdinal = ordinal;
  flipStack = reinterpret_cast<DWORD>(nativeStack);
  flipValid = true;
  failedPresentationState.frameReady = 0;
}

extern "C" void __cdecl CompleteFailedPresentationFlip(HRESULT result, DWORD error)
{
  using namespace FailedPresentation;
  Preservation preserve;
  if (!flipValid || !ScopeCurrent() || flipToken != scopeToken ||
      flipProbeState.calls != flipOrdinal || flipProbeState.busy != 1 || !FrameCurrent())
  {
    flipValid = false;
    return;
  }
  flipResult = result;
  flipError = error;
  failureArmed = result == DDERR_SURFACELOST && !failedPresentationState.pending;
  if (result == S_OK && failedPresentationState.pending && PendingOwner() &&
      frame.serial > pendingFrame.serial)
  {
    ModeAdmission::Owner owner(frame.owner.base, reinterpret_cast<void *>(frame.owner.app));
    DWORD now = 0;
    if (ModeAdmission::Tick(owner, frame.owner.ownerGeneration, now) && ScopeCurrent() &&
        FrameCurrent() && flipProbeState.calls == flipOrdinal && flipProbeState.busy == 1)
    {
      modeAdmissionState.completedAt = now;
      modeAdmissionState.clockValid = 1;
      failedPresentationState.pending = 0;
      Count(failedPresentationState.confirmed);
    }
  }
}

extern "C" BOOL __cdecl TryRecoverFailedPresentation(const DWORD *registers)
{
  using namespace FailedPresentation;
  Preservation preserve;
  auto &s = failedPresentationState;
  DWORD caller = 0;
  // The native return follows saved EBX and the two private hook words.
  const DWORD native = registers[3] <= 0xffffffefu ? registers[3] + 16 : 0;
  const bool valid = failureArmed && flipValid && !s.pending && !s.busy &&
                     registers[7] == static_cast<DWORD>(DDERR_SURFACELOST) && native == flipStack &&
                     QuadFrameRecovery::StackRead(native, &caller, 4) &&
                     caller == frame.owner.base + recoveryGenericFlipCaller &&
                     flipResult == registers[7] && !flipProbeState.busy &&
                     flipProbeState.calls == flipOrdinal && FrameCurrent();
  failureArmed = false;
  if (!valid)
  {
    Count(s.rejected);
    return FALSE;
  }
  s.busy = 1;

  struct Busy
  {
    ~Busy()
    {
      failedPresentationState.busy = 0;
    }
  } busy;

  s.originalResult = flipResult;
  s.lastResult = s.restoreResult = s.cacheResult = static_cast<DWORD>(E_UNEXPECTED);
  s.lastStage = 1;
  Pins pins;
  if (!pins.Current(false))
  {
    Count(s.rejected);
    return FALSE;
  }
  Count(s.attempts);
  DisplayReadiness::Budget budget;
  const bool ready = DisplayReadiness::Wait(DisplayReadiness::Phase::textureRecovery, &budget);
  if (ready || !pins.Current(false) ||
      readinessState.lastResult != static_cast<DWORD>(DDERR_SURFACELOST) ||
      (readinessState.lastStage != static_cast<DWORD>(DisplayReadiness::Stage::primaryLoss) &&
       readinessState.lastStage != static_cast<DWORD>(DisplayReadiness::Stage::backbufferLoss)))
  {
    Count(s.failures);
    return FALSE;
  }
  s.lastStage = 2;
  HRESULT result = Restore(pins, budget);
  if (result == S_OK && Check(pins, budget, false) == S_OK)
  {
    s.lastStage = 3;
    const bool fresh = DisplayReadiness::Wait(DisplayReadiness::Phase::textureRecovery, &budget);
    result = fresh ? Check(pins, budget, false) : E_UNEXPECTED;
  }
  if (result == S_OK)
  {
    s.lastStage = 4;
    result = Reload(pins, budget) ? S_OK : E_UNEXPECTED;
  }
  s.lastResult = result;
  if (result != S_OK)
  {
    Count(s.failures);
    return FALSE;
  }
  pendingFrame = frame;
  s.pending = 1;
  s.lastStage = 5;
  Count(s.successes);
  Count(s.skipped);
  return TRUE;
}

extern "C" __declspec(naked) void FailedPresentationHook()
{
  __asm {
    push dword ptr [failedPresentationReplayBase]
    push 0
    pushfd
    pushad
    mov ebx,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    cld
    push ebx
    call TryRecoverFailedPresentation
    add esp,4
    test eax,eax
    lea eax,Unhandled
    jz Route
    lea eax,Recovered
  Route:
    mov [ebx+36],eax
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    ret
  Recovered:
    lea esp,[esp+4]
    pop ebx
    ret
  Unhandled:
    push eax
    mov eax,[esp+4]
    lea eax,[eax+presentationNativeFailureMessage]
    mov [esp+4],eax
    lea eax,[eax+presentationNativeFailureResume-presentationNativeFailureMessage]
    xchg eax,[esp]
    ret
  }
}

#endif
