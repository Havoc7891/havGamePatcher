// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_HARDWARE_MODE_COMPLETION_HPP
#define HAVGAMEPATCHER_HARDWARE_MODE_COMPLETION_HPP

#include "hardwareModeCompletionTypes.hpp"

extern "C"
{
  HardwareModeCompletionState hardwareModeCompletionState = {};
}

namespace HardwareModeCompletion
{
  // Completion is asynchronous. None of these observers renders, restores,
  // presents, sleeps, pumps messages or changes a native result.
  struct Preservation
  {
    __declspec(align(16)) BYTE floatingPoint[512];
    DWORD error = __readfsdword(0x34);
    DWORD reserved[3];

    Preservation()
    {
      BYTE *address = floatingPoint;
      __asm {
        mov eax,address
        fxsave [eax]
      }
    }

    ~Preservation()
    {
      BYTE *address = floatingPoint;
      __asm {
        mov eax,address
        fxrstor [eax]
      }
      __writefsdword(0x34, error);
    }
  };

  static void Increment(DWORD &value)
  {
    if (value != 0xffffffffu)
    {
      ++value;
    }
  }

  static bool Gameplay()
  {
    return Configured() && state.depth == 1 && !state.ended && !state.videoDepth &&
           !state.mainMenuDepth && !Read(GameRva::mainMenuVisible) && !Read(GameRva::quit) &&
           Read(GameRva::mode) == 2 && Read(GameRva::hardware) && Read(GameRva::directDraw) &&
           Read(GameRva::fullscreenActive);
  }

  static bool OwnerCurrent(const HardwareCompletionOwner &owner)
  {
    if (!Configured() || config.gameBase != owner.base ||
        TextureLifecycle::ThreadId() != owner.thread || !owner.app || !owner.frameObject ||
        !owner.frame || !owner.viewObject || !owner.view ||
        modeAdmissionState.ownerGeneration != owner.ownerGeneration ||
        modeAdmissionState.transitionSerial != owner.transitionSerial ||
        Read(GameRva::app) != owner.app || Read(GameRva::view) != owner.viewObject)
    {
      return false;
    }
    return *reinterpret_cast<volatile DWORD *>(owner.app + 0x1c) == owner.frameObject &&
           *reinterpret_cast<volatile DWORD *>(owner.frameObject + 0x1c) == owner.frame &&
           *reinterpret_cast<volatile DWORD *>(owner.viewObject + 0x1c) == owner.view &&
           modeAdmissionState.app == owner.app &&
           modeAdmissionState.frameObject == owner.frameObject &&
           modeAdmissionState.frame == owner.frame && modeAdmissionState.thread == owner.thread;
  }

  static HardwareCompletionOwner CaptureOwner()
  {
    HardwareCompletionOwner owner;
    owner.base = config.gameBase;
    owner.app = modeAdmissionState.app;
    owner.frameObject = modeAdmissionState.frameObject;
    owner.frame = modeAdmissionState.frame;
    owner.viewObject = Read(GameRva::view);
    owner.view =
        owner.viewObject ? *reinterpret_cast<volatile DWORD *>(owner.viewObject + 0x1c) : 0;
    owner.thread = TextureLifecycle::ThreadId();
    owner.ownerGeneration = modeAdmissionState.ownerGeneration;
    owner.transitionSerial = modeAdmissionState.transitionSerial;
    owner.lifecycle = TextureLifecycle::Serial();
    owner.frameEpoch = TextureLifecycle::FrameEpoch();
    owner.restoreEpoch = state.restoreSuccesses;
    owner.draw = Read(GameRva::drawObject);
    owner.primary = Read(GameRva::primary);
    owner.backbuffer = Read(GameRva::backbuffer);
    owner.device = Read(GameRva::legacyDevice);
    owner.execute = Read(readinessExecuteBuffer);
    return owner;
  }

  static bool ResourcesCurrent(const HardwareCompletionOwner &owner)
  {
    return OwnerCurrent(owner) && Gameplay() && !TextureLifecycle::Incomplete() &&
           owner.lifecycle == TextureLifecycle::Serial() &&
           owner.frameEpoch == TextureLifecycle::FrameEpoch() &&
           owner.restoreEpoch == state.restoreSuccesses && owner.draw && owner.primary &&
           owner.backbuffer && owner.primary != owner.backbuffer && owner.device && owner.execute &&
           owner.draw == Read(GameRva::drawObject) && owner.primary == Read(GameRva::primary) &&
           owner.backbuffer == Read(GameRva::backbuffer) &&
           owner.device == Read(GameRva::legacyDevice) &&
           owner.execute == Read(readinessExecuteBuffer);
  }

  static bool Quiet()
  {
    return !modeAdmissionState.transitionDepth && !state.transitionDepth && !state.busy &&
           !state.setupCleanupBusy && !state.restoreBusy && !textureState.busy && !loadState.busy &&
           !TextureTransfer::textureTransferStats.busy &&
           !TextureTransfer::textureTransferStats.incompleteFlags && !readinessState.busy &&
           !movieRecoveryState.busy && !preMovieRecoveryState.busy &&
           !preMovieRecoveryState.scopeDepth && !quadFrameRecoveryState.busy &&
           !quadFrameRecoveryState.aborted && !quadFrameRecoveryState.pendingPresent &&
           !quadFrameRecoveryState.awaitingFrame && !quadFrameRecoveryState.guardIncomplete &&
           !normalFrameState.incomplete && bridgeState.sceneDepth == 0 &&
           textureState.pendingEpoch == state.restoreSuccesses &&
           textureState.completedEpoch == state.restoreSuccesses &&
           (!state.restoreSuccesses || textureState.lastResult == S_OK);
  }

  static void Invalidate()
  {
    auto &s = hardwareModeCompletionState;
    if (s.frameStarted || s.frameReady)
    {
      Increment(s.invalidations);
    }
    s.frameStarted = s.frameReady = 0;
  }

  static void Cancel()
  {
    auto &s = hardwareModeCompletionState;
    if (s.pending)
    {
      Increment(s.cancelled);
    }
    s.pending = 0;
    Invalidate();
  }
} // namespace HardwareModeCompletion

extern "C" void __cdecl HardwareCompletionBeginTransition(void *app, DWORD token)
{
  using namespace HardwareModeCompletion;
  Preservation preserve;
  if (!Configured() || !token || modeAdmissionState.transitionDepth != 1 ||
      modeAdmissionState.ownerGeneration != token ||
      modeAdmissionState.app != reinterpret_cast<DWORD>(app) ||
      modeAdmissionState.thread != TextureLifecycle::ThreadId())
  {
    return;
  }

  auto &s = hardwareModeCompletionState;
  s.startMode = Read(GameRva::mode);
  s.startPending =
      s.pending && s.owner.ownerGeneration == token && s.owner.app == reinterpret_cast<DWORD>(app);
  s.transitionTracked = token;
  Invalidate();
}

extern "C" void __cdecl HardwareCompletionEndTransition(void *app, DWORD token)
{
  using namespace HardwareModeCompletion;
  Preservation preserve;
  auto &s = hardwareModeCompletionState;
  if (!Configured() || !token || modeAdmissionState.transitionDepth ||
      s.transitionTracked != token || modeAdmissionState.ownerGeneration != token ||
      modeAdmissionState.app != reinterpret_cast<DWORD>(app) ||
      modeAdmissionState.thread != TextureLifecycle::ThreadId())
  {
    return;
  }

  s.transitionTracked = 0;
  if (!Gameplay() || (s.startMode == Read(GameRva::mode) && !s.startPending))
  {
    Cancel();
    return;
  }
  const HardwareCompletionOwner owner = CaptureOwner();
  if (!OwnerCurrent(owner))
  {
    Cancel();
    return;
  }
  if (s.generation == 0xffffffffu)
  {
    s.incomplete = 1;
  }
  else
  {
    ++s.generation;
  }
  s.owner = owner;
  s.pending = 1;
  s.frameStarted = s.frameReady = 0;
  s.completedAt = 0;
  Increment(s.armed);
}

extern "C" BOOL __cdecl HardwareCompletionSuppress(void *app, DWORD token)
{
  using namespace HardwareModeCompletion;
  Preservation preserve;
  auto &s = hardwareModeCompletionState;
  if (!s.pending || !Configured() || modeAdmissionState.thread != TextureLifecycle::ThreadId())
  {
    return FALSE;
  }

  if (token != s.owner.ownerGeneration || reinterpret_cast<DWORD>(app) != s.owner.app ||
      !OwnerCurrent(s.owner) || !Gameplay())
  {
    Cancel();
    return FALSE;
  }
  Increment(s.suppressed);
  return TRUE;
}

extern "C" void __cdecl HardwareCompletionBeginFrame()
{
  using namespace HardwareModeCompletion;
  Preservation preserve;
  auto &s = hardwareModeCompletionState;
  if (!s.pending || !Configured() || TextureLifecycle::ThreadId() != s.owner.thread)
  {
    return;
  }

  Invalidate();
  if (!OwnerCurrent(s.owner) || !Gameplay())
  {
    Cancel();
    return;
  }
  const HardwareCompletionOwner fresh = CaptureOwner();
  if (s.incomplete || !ResourcesCurrent(fresh) || !Quiet() || !QuadFrameRecovery::Phase())
  {
    return;
  }
  // A restored resource set needs a fresh frame. Never reuse the prior witness.
  s.owner = fresh;
  s.frameSerial = TextureLifecycle::FrameSerial();
  s.endCalls = bridgeState.endCalls;
  s.bridgeFailures = bridgeState.failures;
  s.frameStarted = 1;
}

extern "C" void __cdecl HardwareCompletionFinishFrame()
{
  using namespace HardwareModeCompletion;
  Preservation preserve;
  auto &s = hardwareModeCompletionState;
  if (!s.pending || !s.frameStarted || !Configured() ||
      TextureLifecycle::ThreadId() != s.owner.thread)
  {
    return;
  }

  s.frameStarted = 0;
  if (!ResourcesCurrent(s.owner) || !Quiet() || !QuadFrameRecovery::SameOwner() ||
      !quadFrameRecoveryState.frameReturned || !QuadFrameRecovery::Unlocked() ||
      s.frameSerial != TextureLifecycle::FrameSerial() || bridgeState.endCalls <= s.endCalls ||
      bridgeState.failures != s.bridgeFailures || bridgeState.lastStage != D3DBridge::end ||
      bridgeState.lastResult != S_OK)
  {
    s.frameReady = 0;
    Increment(s.invalidations);
    return;
  }
  s.frameReady = 1;
  s.beginCalls = bridgeState.beginCalls;
  s.endCalls = bridgeState.endCalls;
  s.uploadSerial = TextureLifecycle::UploadSerial();
  Increment(s.frameWitnesses);
}

extern "C" void __cdecl CaptureHardwareFlipCompletion(HardwareFlipCompletion *call,
                                                      DWORD surface,
                                                      DWORD target,
                                                      DWORD flags,
                                                      DWORD caller,
                                                      DWORD depth,
                                                      DWORD ordinal)
{
  using namespace HardwareModeCompletion;
  Preservation preserve;
  call->valid = 0;
  auto &s = hardwareModeCompletionState;
  if (!s.pending || !Configured() || TextureLifecycle::ThreadId() != s.owner.thread)
  {
    return;
  }

  if (depth != 1)
  {
    Increment(s.nestedFlips);
    Invalidate();
    return;
  }
  if (!s.frameReady || s.incomplete || !ResourcesCurrent(s.owner) || !Quiet() ||
      quadFrameRecoveryState.depth || surface != s.owner.primary || target || flags != 1 ||
      caller != s.owner.base + modeNativeFlipReturn ||
      s.frameSerial != TextureLifecycle::FrameSerial() ||
      s.uploadSerial != TextureLifecycle::UploadSerial() || bridgeState.endCalls != s.endCalls ||
      bridgeState.failures != s.bridgeFailures || bridgeState.beginCalls != s.beginCalls ||
      bridgeState.lastStage != D3DBridge::end || bridgeState.lastResult != S_OK)
  {
    Invalidate();
    return;
  }
  call->generation = s.generation;
  call->frameSerial = s.frameSerial;
  call->uploadSerial = s.uploadSerial;
  call->endCalls = s.endCalls;
  call->beginCalls = s.beginCalls;
  call->bridgeFailures = s.bridgeFailures;
  call->probeCall = ordinal;
  call->probeDepth = depth;
  call->owner = s.owner;
  call->valid = 1;
}

extern "C" void __cdecl CompleteHardwareFlipCompletion(const HardwareFlipCompletion *call,
                                                       HRESULT result)
{
  using namespace HardwareModeCompletion;
  Preservation preserve;
  auto &s = hardwareModeCompletionState;
  if (!call->valid || !s.pending || !Configured() ||
      TextureLifecycle::ThreadId() != call->owner.thread)
  {
    return;
  }
  bool valid = s.frameReady && s.generation == call->generation && !s.incomplete &&
               ResourcesCurrent(call->owner) && Quiet() &&
               call->frameSerial == TextureLifecycle::FrameSerial() &&
               call->uploadSerial == TextureLifecycle::UploadSerial() &&
               call->endCalls == bridgeState.endCalls &&
               call->bridgeFailures == bridgeState.failures &&
               call->beginCalls == bridgeState.beginCalls &&
               bridgeState.lastStage == D3DBridge::end && bridgeState.lastResult == S_OK &&
               flipProbeState.calls == call->probeCall && flipProbeState.busy == call->probeDepth;
  if (result != S_OK || !valid)
  {

    if (result != S_OK)
    {
      Increment(s.failedFlips);
    }
    Invalidate();
    return;
  }
  // Existing mode admission resolves this clock during the transition. The
  // clock only timestamps a real completion, it is never a readiness test.
  ModeAdmission::Owner owner(call->owner.base, reinterpret_cast<void *>(call->owner.app));
  DWORD now = 0;
  const bool clock = ModeAdmission::Tick(owner, call->owner.ownerGeneration, now);
  valid = s.pending && s.frameReady && s.generation == call->generation &&
          ResourcesCurrent(call->owner) && Quiet() &&
          call->frameSerial == TextureLifecycle::FrameSerial() &&
          call->uploadSerial == TextureLifecycle::UploadSerial() &&
          call->endCalls == bridgeState.endCalls && call->bridgeFailures == bridgeState.failures &&
          call->beginCalls == bridgeState.beginCalls && bridgeState.lastStage == D3DBridge::end &&
          bridgeState.lastResult == S_OK && flipProbeState.calls == call->probeCall &&
          flipProbeState.busy == call->probeDepth;

  if (!clock || !valid)
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
  s.pending = s.frameReady = s.frameStarted = 0;
  Increment(s.completed);
  Increment(s.flipWitnesses);
}

#endif // HAVGAMEPATCHER_HARDWARE_MODE_COMPLETION_HPP
