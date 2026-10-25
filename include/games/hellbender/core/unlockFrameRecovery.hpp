// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_UNLOCK_FRAME_RECOVERY_HPP
#define HAVGAMEPATCHER_UNLOCK_FRAME_RECOVERY_HPP

namespace UnlockFrame
{
  constexpr DWORD nativeCaller = readinessFlushReturn;
  constexpr DWORD nativeEpilogue = recoveryFailureNativeReturn;

  struct Scope
  {
    DWORD busy, expectedDrawLock, flushGeneration, flushInvalidations;
    DWORD flushReentries, flushStack, executeUnlockMethod, sceneBegin;
    DWORD sceneEnd, sceneFailures, sceneCreates, sceneCreated;
  } scope = {};

  static_assert(sizeof(Scope) == 48, "Private terminal Unlock ownership");

  static bool FlushCurrent()
  {
    const auto &s = flushOwnerState;
    return !s.busy && !s.incomplete && s.activeValid &&
           (s.active == 1 || (scope.busy && !scope.expectedDrawLock && s.active == 0)) &&
           s.generation == scope.flushGeneration && s.activeGeneration == scope.flushGeneration &&
           s.ownerThread == TextureLifecycle::ThreadId() && s.ownerBase == config.gameBase &&
           s.entryStack == scope.flushStack && s.nativeReturn == config.gameBase + nativeCaller &&
           s.invalidations == scope.flushInvalidations && s.reentries == scope.flushReentries &&
           s.nativeReturned == 1 && s.nativeBool == 0 && s.rawCalls == 1 &&
           s.rawResult == static_cast<DWORD>(DDERR_SURFACELOST) &&
           s.rawSurface == QuadFrameRecovery::ownerBackbuffer && s.entryDrawLock == 1 &&
           s.afterDrawLock == 1 && s.branchKind == 4 && s.rawOwned &&
           FlushOwnership::ConfigurationUnchanged();
  }

  static bool SceneCurrent()
  {
    return !bridgeState.sceneDepth && bridgeState.beginCalls == scope.sceneBegin &&
           bridgeState.endCalls == scope.sceneEnd && bridgeState.failures == scope.sceneFailures &&
           bridgeState.createAttempts == scope.sceneCreates &&
           bridgeState.createSuccesses == scope.sceneCreated &&
           bridgeState.lastStage == D3DBridge::end && bridgeState.lastResult == S_OK;
  }

  static bool StackCurrent(const DWORD *registers)
  {
    using namespace QuadFrameRecovery;
    const auto &f = quadFrameRecoveryState;
    DWORD slots[28];
    return registers[3] <= 0xfffffff3u && registers[3] + 12 == scope.flushStack &&
           registers[7] == 0 && registers[9] == scope.flushGeneration &&
           registers[10] == config.gameBase + readinessFlushEntry &&
           scope.flushStack <= 0xffffff97u && scope.flushStack + 0x68 == f.ownerSlot &&
           StackRead(scope.flushStack, slots, sizeof(slots)) &&
           slots[0] == config.gameBase + nativeCaller &&
           slots[3] == config.gameBase + recoveryFlushCleanup &&
           slots[4] == config.gameBase + recoveryCpuRowsCleanup &&
           slots[5] == config.gameBase + recoveryFrameAbortReturn && slots[26] == f.continuation &&
           slots[27] == f.outerReturn &&
           f.outerReturn == config.gameBase + recoveryOuterFrameReturn;
  }

  // A decline before the genuine execute Unlock leaves the native BOOL-zero
  // fatal path intact. Only this transaction's extra references are dropped.
  static DWORD Decline(DWORD error)
  {
    using namespace QuadFrameRecovery;
    Release();
    state.restoreBusy = 0;
    context.owned = false;
    quadFrameRecoveryState.busy = 0;
    ResetContext();
    scope.expectedDrawLock = 0;
    scope.busy = 0;
    __writefsdword(0x34, error);
    return 0;
  }
} // namespace UnlockFrame

extern "C" BOOL __cdecl UnlockFrameBusy()
{
  return UnlockFrame::scope.busy != 0;
}

extern "C" BOOL __cdecl UnlockFrameDrawLockCurrent()
{
  return UnlockFrame::scope.busy && UnlockFrame::scope.expectedDrawLock == 1 &&
         Read(GameRva::drawLock) == 1 && !Read(GameRva::textureLock) && !Read(GameRva::drawSession);
}

extern "C" BOOL __cdecl UnlockFrameContextCurrent()
{
  if (!UnlockFrame::scope.busy)
  {
    return TRUE;
  }
  const DWORD execute = QuadFrameRecovery::context.execute;
  return execute && UnlockFrame::FlushCurrent() && UnlockFrame::SceneCurrent() &&
         Read(GameRva::drawLock) == UnlockFrame::scope.expectedDrawLock &&
         (*reinterpret_cast<const DWORD **>(execute))[5] == UnlockFrame::scope.executeUnlockMethod;
}

extern "C" void __cdecl CompleteUnlockFrame()
{
  UnlockFrame::scope.expectedDrawLock = 0;
  UnlockFrame::scope.busy = 0;
}

extern "C" DWORD __cdecl TryAbortUnlockFrame(const DWORD *registers, DWORD token)
{
  using namespace QuadFrameRecovery;
  using namespace UnlockFrame;
  const DWORD error = __readfsdword(0x34);
  if (registers[7] != 0)
  {
    return 0;
  }
  if (scope.busy)
  {
    return 0;
  }
  if (!Configured())
  {
    return 0;
  }
  auto &f = quadFrameRecoveryState;
  const auto &flush = flushOwnerState;
  if (!token || !FlushOwnership::Owned(token) || flush.busy || flush.incomplete ||
      !flush.activeValid || !flush.nativeReturned || flush.nativeBool || flush.rawCalls != 1 ||
      flush.rawResult != static_cast<DWORD>(DDERR_SURFACELOST) || flush.branchKind != 4 ||
      !flush.rawOwned)
  {
    return 0;
  }
  if (f.busy || f.aborted || f.awaitingFrame || f.pendingPresent || f.attempts != f.successes ||
      f.confirmedFrames != f.successes)
  {
    return 0;
  }
  if (!NormalFrameOwnership::SameOwner() || !SameOwner() || !FlushOwnership::entry.configured ||
      !FlushOwnership::entry.sameOwner || !FlushOwnership::returned.configured ||
      !FlushOwnership::returned.sameOwner || LostSurfaceReadiness::busy || state.restoreBusy ||
      textureState.busy || loadState.busy || TextureTransfer::textureTransferStats.busy ||
      TextureTransfer::textureTransferStats.incompleteFlags ||
      TextureTransfer::textureTransferStats.activeGeneration || failedPresentationState.busy ||
      failedPresentationState.pending || worldRelockState.busy || FullscreenSetupBusy() ||
      PostRestoreRendererBusy() || Read(GameRva::drawLock) != 1 || Read(GameRva::textureLock) ||
      Read(GameRva::drawSession))
  {
    return 0;
  }
  scope.flushGeneration = token;
  scope.flushInvalidations = flush.invalidations;
  scope.flushReentries = flush.reentries;
  scope.flushStack = flush.entryStack;
  if (!FlushCurrent() || !StackCurrent(registers))
  {
    return 0;
  }
  if (!ownerDraw || !ownerPrimary || !ownerBackbuffer || ownerPrimary == ownerBackbuffer ||
      !ownerDevice || !ownerExecute || Read(GameRva::renderBpp) != 16 ||
      Read(recoveryNativeSurfaceWidth) != 640 || Read(recoveryNativeSurfaceHeight) != 480 ||
      Read(GameRva::instructionOffset) != 0x20020 || Read(capacityRva) < 0x20038 ||
      Read(capacityRva) > 0x40000 || !Read(allocationRva))
  {
    return 0;
  }
  if (ownerEpoch == 0xffffffffu || f.observations == 0xffffffffu)
  {
    return 0;
  }
  if (bridgeState.sceneDepth || bridgeState.beginCalls != bridgeState.endCalls ||
      bridgeState.lastStage != D3DBridge::end || bridgeState.lastResult != S_OK ||
      bridgeState.endCalls < f.endCallsBefore)
  {
    return 0;
  }
  const auto methods = *reinterpret_cast<const DWORD **>(ownerExecute);
  if (!methods || !methods[5])
  {
    return 0;
  }
  scope.executeUnlockMethod = methods[5];
  scope.sceneBegin = bridgeState.beginCalls;
  scope.sceneEnd = bridgeState.endCalls;
  scope.sceneFailures = bridgeState.failures;
  scope.sceneCreates = bridgeState.createAttempts;
  scope.sceneCreated = bridgeState.createSuccesses;
  f.busy = 1;
  ++f.observations;
  f.lastError = error;
  ResetContext();
  scope.expectedDrawLock = 1;
  scope.busy = 1;
  if (!CaptureContext())
  {
    return Decline(error);
  }
  context.owned = true;
  state.restoreBusy = 1;
  if (!context.Windows() || !Retain())
  {
    return Decline(error);
  }
  if (!SurfaceCall(context.primaryLost, context.primary, f.primaryLost) ||
      !SurfaceCall(context.backbufferLost, context.backbuffer, f.backbufferLost))
  {
    return Decline(error);
  }
  const DWORD lost = static_cast<DWORD>(DDERR_SURFACELOST);
  if (f.backbufferLost != lost || (f.primaryLost != S_OK && f.primaryLost != lost))
  {
    return Decline(error);
  }
  if (!context.Windows() || !StackCurrent(registers))
  {
    return Decline(error);
  }
  using ExecuteUnlock = HRESULT(WINAPI *)(void *);
  const HRESULT unlocked = reinterpret_cast<ExecuteUnlock>(scope.executeUnlockMethod)(
      reinterpret_cast<void *>(context.execute));
  // A genuine execute-buffer Unlock is the cleanup boundary. From here any
  // rejection is fatal, never a return to scene submission with altered debt.
  f.callerKind = 5;
  if (unlocked != S_OK || !context.Windows() || !StackCurrent(registers))
  {
    Fatal(unlocked == S_OK ? QuadFrameRecovery::Reject::changed
                           : QuadFrameRecovery::Reject::resources);
  }

  // No callbacks occur between this final validation and the wrapper's return
  // selection. The old pixel memory was proven lost. Cancel the game's stale
  // access ownership without claiming that the failed surface Unlock succeeded.
  scope.expectedDrawLock = 0;
  Write(GameRva::drawLock, 0);
  AdvanceTextureFrameEpoch();
  ++ownerEpoch;
  context.frameEpoch = ownerEpoch;
  f.frameEpoch = ownerEpoch;
  f.aborted = 1;
  f.returnStage = 0;
  f.failureStack = scope.flushStack + 4;
  f.primary = ownerPrimary;
  f.backbuffer = ownerBackbuffer;
  f.draw = ownerDraw;
  f.device = ownerDevice;
  f.executeBuffer = ownerExecute;
  f.executeAllocation = Read(allocationRva);
  f.executeCapacity = Read(capacityRva);
  f.actualCount = Read(actualCountRva);
  f.restoreBefore = state.restoreSuccesses;
  f.lifecycle = ownerLifecycle;
  f.frameSerial = ownerFrame;
  f.uploadSerial = context.upload;
  f.textureGeneration = context.transferGeneration;
  f.lastResult = lost;
  ++f.attempts;
  f.rejectReason = 0;
  StageAt(QuadFrameRecovery::Stage::unwind);
  const DWORD target = config.gameBase + nativeEpilogue;
  __writefsdword(0x34, error);
  return target;
}

#endif
