// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_WORLD_RELOCK_RECOVERY_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_WORLD_RELOCK_RECOVERY_HPP

extern "C"
{
  WorldRelockState worldRelockState = {};
  volatile LONG worldRelockCommitStatus = 0;
}

namespace WorldRelock
{
  enum class Reason : DWORD
  {
    none,
    configuration,
    result,
    ownership,
    stack,
    scene,
    resources,
    budget,
    changed,
    notLost,
    token,
    incomplete
  };
  static DWORD tokenMagic = 0, tokenVersion = 0;

  static void Increment(DWORD &value)
  {
    if (value != 0xffffffffu)
    {
      ++value;
    }
    else
    {
      _InterlockedOr(reinterpret_cast<volatile LONG *>(&worldRelockState.incomplete), 1);
    }
  }

  static bool BeginWrite()
  {
    auto &s = worldRelockState;
    if (_InterlockedCompareExchange(&s.busy, 1, 0) != 0)
    {
      _InterlockedExchange(reinterpret_cast<volatile LONG *>(&s.tokenValid), 0);
      _InterlockedOr(reinterpret_cast<volatile LONG *>(&s.incomplete), 2);
      for (DWORD attempt = 0; attempt < 16; ++attempt)
      {
        const DWORD before = *reinterpret_cast<const volatile DWORD *>(&s.reentries);
        if (before == 0xffffffffu ||
            static_cast<DWORD>(_InterlockedCompareExchange(
                reinterpret_cast<volatile LONG *>(&s.reentries), static_cast<LONG>(before + 1),
                static_cast<LONG>(before))) == before)
        {
          break;
        }
      }
      return false;
    }
    if (s.sequence >= 0xfffffffeu || s.incomplete)
    {
      s.tokenValid = 0;
      s.incomplete |= 1;
      _InterlockedExchange(&s.busy, 0);
      return false;
    }
    ++s.sequence;
    return true;
  }

  static void EndWrite()
  {
    ++worldRelockState.sequence;
    _InterlockedExchange(&worldRelockState.busy, 0);
  }

  static void ClearToken()
  {
    if (worldRelockState.tokenValid)
    {
      Increment(worldRelockState.invalidations);
    }
    worldRelockState.tokenValid = 0;
  }

  static bool SceneComplete()
  {
    const auto &f = quadFrameRecoveryState;
    return bridgeState.sceneDepth == 0 && bridgeState.beginCalls == bridgeState.endCalls &&
           bridgeState.endCalls > f.endCallsBefore && bridgeState.lastStage == 8 &&
           bridgeState.lastResult == S_OK &&
           bridgeState.failures == QuadFrameRecovery::ownerFailures;
  }

  static bool TokenCurrent()
  {
    using namespace QuadFrameRecovery;
    const auto &s = worldRelockState;
    const auto &f = quadFrameRecoveryState;
    return !s.incomplete && s.tokenBase == config.gameBase && tokenMagic == config.magic &&
           tokenVersion == config.version && SameOwner() && Unlocked() &&
           s.tokenThread == TextureLifecycle::ThreadId() &&
           s.tokenFrameGeneration == f.generation &&
           s.tokenLifecycle == TextureLifecycle::Serial() &&
           s.tokenFrameSerial == TextureLifecycle::FrameSerial() &&
           s.tokenFrameEpoch == TextureLifecycle::FrameEpoch() &&
           s.tokenRestoreEpoch == state.restoreSuccesses && s.tokenOwnerSlot == f.ownerSlot &&
           s.tokenContinuation == f.continuation && s.tokenOuterReturn == f.outerReturn &&
           s.tokenSurface == Read(GameRva::backbuffer) &&
           s.tokenPrimary == Read(GameRva::primary) && s.tokenDraw == Read(GameRva::drawObject) &&
           s.tokenDevice == Read(GameRva::legacyDevice) &&
           s.tokenExecute == Read(executeBufferRva) && SceneComplete() &&
           s.tokenBeginCalls == bridgeState.beginCalls && s.tokenEndCalls == bridgeState.endCalls &&
           s.tokenBridgeFailures == bridgeState.failures;
  }

  static bool Stable(DWORD observation, DWORD generation)
  {
    const auto &s = worldRelockState;
    return !s.incomplete && s.observations == observation && s.tokenGeneration == generation &&
           s.tokenBeginCalls == bridgeState.beginCalls && s.tokenEndCalls == bridgeState.endCalls &&
           s.tokenBridgeFailures == bridgeState.failures && SceneComplete();
  }

} // namespace WorldRelock

// This observes every native Lock result before its Boolean conversion.
// It performs no COM calls and never changes the native result or descriptor.
extern "C" void __cdecl RecordWorldRelockResult(const DWORD *registers)
{
  using namespace WorldRelock;
  using namespace QuadFrameRecovery;
  const DWORD error = __readfsdword(0x34);
  CompleteFrameRestoreLock(registers);
  auto &s = worldRelockState;
  if (!BeginWrite())
  {
    __writefsdword(0x34, error);
    return;
  }
  ClearToken();
  Increment(s.observations);
  s.lastStage = 1;
  s.lastResult = registers[7];
  s.lastError = error;
  s.lastReason = static_cast<DWORD>(Reason::result);
  const DWORD stack = registers[3] <= 0xfffffff7u ? registers[3] + 8 : 0;
  DWORD slots[29];
  const auto &f = quadFrameRecoveryState;
  if (registers[7] == static_cast<DWORD>(DDERR_SURFACELOST) && Configured() && SameOwner() &&
      !f.busy && !f.aborted && Unlocked() && Read(GameRva::renderBpp) == 8 && SceneComplete() &&
      stack <= 0xffffff93u && StackRead(stack, slots, sizeof(slots)) &&
      slots[1] == config.gameBase + recoveryLockCallerReturn &&
      slots[5] == config.gameBase + recoveryLockOuterReturn &&
      slots[6] == config.gameBase + relockNativeFrameGate && slots[2] &&
      slots[2] == ownerBackbuffer && slots[3] == config.gameBase + recoveryLockDescriptor &&
      stack + 0x6c == f.ownerSlot && slots[27] == f.continuation && slots[28] == f.outerReturn)
  {
    s.tokenBase = config.gameBase;
    tokenMagic = config.magic;
    tokenVersion = config.version;
    s.tokenThread = TextureLifecycle::ThreadId();
    s.tokenStack = stack;
    s.tokenSurface = slots[2];
    s.tokenDescriptor = slots[3];
    s.tokenFrameGeneration = f.generation;
    s.tokenLifecycle = TextureLifecycle::Serial();
    s.tokenFrameSerial = TextureLifecycle::FrameSerial();
    s.tokenFrameEpoch = TextureLifecycle::FrameEpoch();
    s.tokenRestoreEpoch = state.restoreSuccesses;
    s.tokenOwnerSlot = f.ownerSlot;
    s.tokenContinuation = f.continuation;
    s.tokenOuterReturn = f.outerReturn;
    s.tokenPrimary = ownerPrimary;
    s.tokenDraw = ownerDraw;
    s.tokenDevice = ownerDevice;
    s.tokenExecute = ownerExecute;
    s.tokenBeginCalls = bridgeState.beginCalls;
    s.tokenEndCalls = bridgeState.endCalls;
    s.tokenBridgeFailures = bridgeState.failures;
    Increment(s.tokenGeneration);
    Increment(s.published);
    s.tokenValid = s.incomplete ? 0 : 1;
    s.lastReason = 0;
  }
  EndWrite();
  __writefsdword(0x34, error);
}

// A nonzero target is an original native branch. Zero selects only the explicit
// failed-return path, with EAX still the native false result.
extern "C" DWORD __cdecl SelectRelockDecision(const DWORD *registers)
{
  using namespace WorldRelock;
  using namespace QuadFrameRecovery;
  const DWORD error = __readfsdword(0x34);
  const DWORD base = config.gameBase;
  const DWORD normal = base + (registers[7] ? relockNativeSuccessResume : relockNativeFatalResume);
  auto &s = worldRelockState;
  auto &f = quadFrameRecoveryState;
  if (!BeginWrite())
  {
    __writefsdword(0x34, error);
    return normal;
  }
  const bool token = s.tokenValid != 0;
  s.tokenValid = 0;
  if (token)
  {
    Increment(s.consumed);
  }
  s.lastStage = 2;
  s.decisionStack = registers[3] <= 0xfffffffbu ? registers[3] + 4 : 0;
  auto decline = [&](Reason reason) -> DWORD
  {
    s.lastReason = static_cast<DWORD>(reason);
    Increment(s.rejected);
    EndWrite();
    __writefsdword(0x34, error);
    return normal;
  };
  if (registers[7])
  {
    s.lastReason = 0;
    EndWrite();
    __writefsdword(0x34, error);
    return normal;
  }
  if (!token || s.lastResult != static_cast<DWORD>(DDERR_SURFACELOST))
  {
    return decline(Reason::token);
  }
  if (!Configured() || !TokenCurrent() || f.busy || f.aborted || state.restoreBusy ||
      textureState.busy || loadState.busy || TextureTransfer::textureTransferStats.busy ||
      TextureTransfer::textureTransferStats.incompleteFlags)
  {
    return decline(Reason::ownership);
  }
  if (s.attempts || s.accepted || worldRelockCommitStatus || f.awaitingFrame ||
      f.attempts == 0xffffffffu || f.frameReturns == 0xffffffffu || f.attempts != f.successes ||
      f.confirmedFrames != f.successes)
  {
    return decline(Reason::budget);
  }
  DWORD slots[23];
  const DWORD stack = s.decisionStack;
  if (s.tokenStack > 0xffffffe7u || stack != s.tokenStack + 24 || stack > 0xffffffa3u ||
      stack + 0x54 != f.ownerSlot || !StackRead(stack, slots, sizeof(slots)) ||
      slots[0] != base + relockNativeFrameGate || slots[21] != f.continuation ||
      slots[22] != f.outerReturn)
  {
    return decline(Reason::stack);
  }
  if (!ownerDraw || !ownerPrimary || !ownerBackbuffer || ownerPrimary == ownerBackbuffer ||
      !ownerDevice || !ownerExecute || Read(GameRva::renderBpp) != 8 ||
      Read(recoveryNativeSurfaceWidth) != 640 || Read(recoveryNativeSurfaceHeight) != 480 ||
      Read(GameRva::instructionOffset) != 0x20020 || Read(capacityRva) < 0x20038 ||
      Read(capacityRva) > 0x40000 || ownerEpoch == 0xffffffffu)
  {
    return decline(Reason::resources);
  }
  const DWORD observation = s.observations, generation = s.tokenGeneration;
  Advance(f.observations);
  f.lastResult = s.lastResult;
  f.endSceneResult = bridgeState.lastResult;
  f.lastError = error;
  f.failureStack = s.tokenStack;
  f.primary = ownerPrimary;
  f.backbuffer = ownerBackbuffer;
  f.draw = ownerDraw;
  f.device = ownerDevice;
  f.executeBuffer = ownerExecute;
  f.executeAllocation = Read(allocationRva);
  f.executeCapacity = Read(capacityRva);
  f.actualCount = Read(actualCountRva);
  f.restoreBefore = ownerRestore;
  f.lifecycle = ownerLifecycle;
  f.frameSerial = ownerFrame;
  f.busy = 1;
  ResetContext();
  const DWORD before = ownerEpoch;
  AdvanceTextureFrameEpoch();
  ownerEpoch = TextureLifecycle::FrameEpoch();
  f.frameEpoch = ownerEpoch;
  if (ownerEpoch != before + 1 || !CaptureContext())
  {
    f.busy = 0;
    return decline(Reason::changed);
  }
  context.owned = true;
  state.restoreBusy = 1;
  f.uploadSerial = context.upload;
  f.textureGeneration = context.transferGeneration;
  StageAt(Stage::query);
  bool valid = context.Windows() && Stable(observation, generation) && Retain() &&
               Stable(observation, generation) &&
               SurfaceCall(context.primaryLost, context.primary, f.primaryLost) &&
               Stable(observation, generation) &&
               SurfaceCall(context.backbufferLost, context.backbuffer, f.backbufferLost) &&
               Stable(observation, generation);
  // Callbacks may reenter or alter a native return slot. Recheck the complete
  // still-owned suffix after the last callback, before committing any abort.
  DWORD after[23];
  valid = valid && StackRead(stack, after, sizeof(after));
  for (DWORD i = 0; valid && i < 23; ++i)
  {
    valid = after[i] == slots[i];
  }
  valid = valid && context.Current() && Stable(observation, generation);
  const DWORD lost = static_cast<DWORD>(DDERR_SURFACELOST);
  const bool chainLost = (f.primaryLost == lost || f.backbufferLost == lost) &&
                         (f.primaryLost == S_OK || f.primaryLost == lost) &&
                         (f.backbufferLost == S_OK || f.backbufferLost == lost);
  if (!valid || !chainLost)
  {
    const bool released = Release();
    state.restoreBusy = 0;
    context.owned = false;
    f.busy = 0;
    return decline(valid && released ? Reason::notLost : Reason::changed);
  }
  f.aborted = 1;
  f.callerKind = 3;
  f.returnStage = 0;
  ++f.attempts;
  f.rejectReason = 0;
  StageAt(Stage::unwind);
  Increment(s.attempts);
  Increment(s.accepted);
  s.lastStage = 3;
  s.lastReason = 0;
  _InterlockedExchange(&worldRelockCommitStatus, 1);
  _InterlockedExchange(&worldRelockCommitStatus, 2);
  EndWrite();
  __writefsdword(0x34, error);
  return 0;
}

extern "C" DWORD __cdecl SelectRelockFrameReturn(DWORD *registers)
{
  using namespace QuadFrameRecovery;
  const DWORD error = __readfsdword(0x34);
  const DWORD base = config.gameBase;
  auto &f = quadFrameRecoveryState;
  if (!f.aborted)
  {
    // Normal native MOV semantics, with no observer callbacks before the read.
    registers[7] = *reinterpret_cast<volatile DWORD *>(base + relockNativeFrameValue);
    __writefsdword(0x34, error);
    return base + relockNativeFrameResume;
  }
  DWORD slots[22];
  if (!Configured() || base != ownerBase || config.magic != context.magic ||
      config.version != context.version || !SameOwner())
  {
    Fatal(Reject::ownership);
  }
  // On an admitted abort, validate the pinned image before reading its operand.
  registers[7] = *reinterpret_cast<volatile DWORD *>(base + relockNativeFrameValue);
  if (context.renderBpp != 8 || Read(GameRva::renderBpp) != 8 ||
      !ReturnOwned(registers, 0x50, 0, slots, 3) || worldRelockState.attempts != 1 ||
      worldRelockCommitStatus != 2)
  {
    Fatal(Reject::bookkeeping);
  }
  if (!WorldRelock::BeginWrite())
  {
    Fatal(Reject::incomplete);
  }
  worldRelockState.gateStack = registers[3] + 4;
  worldRelockState.lastStage = 4;
  f.returnStage = 5;
  ++f.frameReturns;
  WorldRelock::EndWrite();
  __writefsdword(0x34, error);
  return ownerBase + recoveryFrameAbortReturn;
}

extern "C" __declspec(naked) void RelockDecisionHook()
{
  __asm {
    push 0
    push 0
    pushfd
    pushad
    add dword ptr [esp+12],8
    cld
    mov ebp,esp
    mov eax,dword ptr [config+8]
    add eax,relockNativeFatalMessage
    mov dword ptr [ebp+40],eax
    sub esp,528
    and esp,-16
    fxsave [esp]
    push ebp
    call SelectRelockDecision
    add esp,4
    test eax,eax
    jz Aborted
    mov dword ptr [ebp+36],eax
    cmp dword ptr [ebp+28],0
    jne Successful
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    test eax,eax
    ret
  Successful:
    mov dword ptr [ebp+40],eax
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    test eax,eax
    lea esp,[esp+4]
    ret
  Aborted:
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    test eax,eax
    lea esp,[esp+8]
    ret
  }
}

extern "C" __declspec(naked) void RelockFrameReturnHook()
{
  __asm {
    push 0
    pushfd
    pushad
    add dword ptr [esp+12],4
    cld
    mov ebp,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    push ebp
    call SelectRelockFrameReturn
    add esp,4
    mov dword ptr [ebp+36],eax
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    ret
  }
}

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_WORLD_RELOCK_RECOVERY_HPP
