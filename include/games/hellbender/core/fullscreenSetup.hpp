// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_FULLSCREEN_SETUP_HPP
#define HAVGAMEPATCHER_FULLSCREEN_SETUP_HPP

namespace FullscreenSetup
{
  using Native = int(__cdecl *)();

  struct ScopeState
  {
    DWORD busy, activeToken, nextToken, activeThread;
  } scopeState = {};

  static bool NativeWorkActive()
  {
    return normalFrameState.depth || flushOwnerState.busy || flushOwnerState.active ||
           LostSurfaceReadiness::busy || flipProbeState.busy || bridgeState.sceneDepth ||
           state.restoreBusy || readinessState.busy || textureState.busy || loadState.busy ||
           SoftwareModeCompletion::activePresentDepth || SoftwareModeCompletion::activePaintDepth;
  }

  // The native presenter pumps input after rendering. Only its qualified
  // input call may cross this otherwise active presentation scope.
  static bool InFlight()
  {
    return NativeWorkActive() || (FailedPresentation::scopeDepth && !PresentationInputCurrent());
  }

  static bool Quiet()
  {
    return !InFlight() && !UnlockFrameBusy() && !PostRestoreRendererBusy() &&
           !readinessState.busy && !state.busy && !state.restoreBusy && !state.setupCleanupBusy &&
           !state.videoDepth && !textureState.busy && !loadState.busy && !bridgeState.sceneDepth &&
           !TextureTransfer::textureTransferStats.busy &&
           !TextureTransfer::textureTransferStats.activeGeneration && !movieRecoveryState.busy &&
           !preMovieRecoveryState.busy && !preMovieRecoveryState.scopeDepth &&
           !quadFrameRecoveryState.depth && !quadFrameRecoveryState.busy &&
           !quadFrameRecoveryState.armed && !quadFrameRecoveryState.aborted &&
           !quadFrameRecoveryState.pendingPresent && !quadFrameRecoveryState.awaitingFrame &&
           !failedPresentationState.busy && !failedPresentationState.pending &&
           !worldRelockState.busy && !Read(GameRva::drawLock) && !Read(GameRva::drawSession) &&
           !Read(GameRva::textureLock);
  }

  static bool Current(const HardwareCompletionOwner &owner, DWORD previousMode)
  {
    return HardwareModeCompletion::OwnerCurrent(owner) && !Read(GameRva::quit) &&
           state.transitionDepth == 1 && modeAdmissionState.transitionDepth == 1 &&
           Read(GameRva::mode) == previousMode && previousMode < 2 &&
           config.setupOriginal == owner.base + modeNativeFullscreenSetup && Quiet();
  }

  static int Finish(int result, DWORD error)
  {
    __writefsdword(0x34, error);
    return result;
  }
} // namespace FullscreenSetup

extern "C" void __cdecl FinishFullscreenSetupScope(DWORD scope)
{
  auto &s = FullscreenSetup::scopeState;
  if (scope && s.busy && (scope >> 1) == s.activeToken &&
      TextureLifecycle::ThreadId() == s.activeThread)
  {
    s.busy = s.activeToken = s.activeThread = 0;
  }
}

extern "C" BOOL __cdecl SuppressModeChange()
{
  return FullscreenSetup::scopeState.busy || FullscreenSetup::InFlight() || UnlockFrameBusy() ||
         PostRestoreRendererBusy() || modeAdmissionState.transitionDepth || state.transitionDepth;
}

extern "C" BOOL __cdecl FullscreenSetupBusy()
{
  return FullscreenSetup::scopeState.busy != 0;
}

extern "C" BOOL __cdecl PrepareFrameRecovery(const DWORD *registers)
{
  return !FullscreenSetupBusy() && !UnlockFrameBusy() && !PostRestoreRendererBusy() &&
         !FullscreenSetup::NativeWorkActive() && !FailedPresentation::scopeDepth &&
         PostRestoreBeforeFrame(registers);
}

extern "C" int __cdecl CallOwnedFullscreenSetup(DWORD caller,
                                                DWORD *scope,
                                                DWORD requestedMode,
                                                DWORD frameObject)
{
  using namespace FullscreenSetup;
  const DWORD incomingError = __readfsdword(0x34);
  *scope = 0;
  auto &s = scopeState;
  if (s.busy || !Configured())
  {
    return Finish(0, incomingError);
  }
  const HardwareCompletionOwner owner = HardwareModeCompletion::CaptureOwner();
  const DWORD previousMode = Read(GameRva::mode);
  const DWORD retainedRoot = Read(GameRva::drawObject);
  ModeAdmission::Owner windowOwner(owner.base, reinterpret_cast<void *>(owner.app));
  if (s.nextToken == 0x7fffffffu || caller != owner.base + modeNativeFullscreenReturn ||
      !retainedRoot || requestedMode != 2 || frameObject != owner.frameObject ||
      !owner.ownerGeneration || TextureLifecycle::Incomplete() || !Current(owner, previousMode))
  {
    return Finish(0, incomingError);
  }

  // The stack-local nonce belongs to this wrapper call. Bit zero authorizes
  // native failure cleanup only after native setup has actually started.
  s.activeToken = ++s.nextToken;
  s.activeThread = owner.thread;
  *scope = s.activeToken << 1;
  s.busy = 1;
  if (!windowOwner.Windows(reinterpret_cast<HWND>(owner.view)) || !Current(owner, previousMode) ||
      Read(GameRva::drawObject) != retainedRoot)
  {
    return Finish(0, incomingError);
  }
  *scope |= 1;
  __writefsdword(0x34, incomingError);
  const int setup = reinterpret_cast<Native>(owner.base + modeNativeFullscreenSetup)();
  const DWORD setupError = __readfsdword(0x34);
  if (!HardwareModeCompletion::OwnerCurrent(owner) || state.transitionDepth != 1 ||
      modeAdmissionState.transitionDepth != 1 || Read(GameRva::mode) != previousMode ||
      Read(GameRva::quit) || Read(GameRva::drawObject) != retainedRoot)
  {
    return Finish(0, setupError);
  }
  if (setup == 1)
  {
    StampPostRestoreRenderer(*scope, retainedRoot);
  }
  return Finish(setup, setupError);
}

#endif
