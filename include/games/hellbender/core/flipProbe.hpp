// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_FLIP_PROBE_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_FLIP_PROBE_HPP

// Telemetry records the actual original Flip. Targeted movie recovery keeps
// its own outcome and preserves the original failure evidence.
struct FlipProbeSnapshot
{
  DWORD result, caller, surface, target, flags;
  DWORD mode, directDraw, hardware, fullscreen;
  DWORD transitionDepth, videoDepth, mainMenuVisible, drawLock, drawSession;
  DWORD lifecycleSerial, restoreSuccesses;
};

struct FlipProbeState
{
  DWORD calls, failures, busy, nested;
  FlipProbeSnapshot last, lastFailure;
};

static_assert(sizeof(FlipProbeSnapshot) == 64, "Flip observation snapshot ABI");
static_assert(sizeof(FlipProbeState) == 144, "Flip observation ABI");

extern "C"
{
  FlipProbeState flipProbeState = {};
}

namespace FlipProbe
{
  static void Store(FlipProbeSnapshot &destination, const FlipProbeSnapshot &source)
  {
    const auto from = reinterpret_cast<const DWORD *>(&source);
    auto to = reinterpret_cast<volatile DWORD *>(&destination);
    for (DWORD index = 0; index < sizeof(source) / sizeof(DWORD); ++index)
    {
      to[index] = from[index];
    }
  }
} // namespace FlipProbe

extern "C" HRESULT WINAPI ObserveFlipWithStack(
    void *surface, void *target, DWORD flags, DWORD caller, const DWORD *nativeStack)
{
  using Flip = HRESULT(WINAPI *)(void *, void *, DWORD);
  const auto methods = *reinterpret_cast<DWORD **>(surface);
  const auto flip = reinterpret_cast<Flip>(methods[0x2c / sizeof(DWORD)]);
  const DWORD nativeBase = config.gameBase;
  if (!Configured())
  {
    if (flipProbeState.busy || movieRecoveryState.busy)
    {
      MovieRecovery::ObserveUnconfiguredNested();
    }

    if (preMovieRecoveryState.scopeDepth || preMovieRecoveryState.busy)
    {
      PreMovieRecovery::ObserveUnconfiguredNested();
    }
    const HRESULT result = flip(surface, target, flags);
    RecordFailedPresentationBase(nativeBase);
    return result;
  }

  // Keep each in-flight call's input state on its own stack. A nested Flip can
  // update the global observation without overwriting the outer call's inputs.
  const DWORD incomingError = __readfsdword(0x34);
  const DWORD observedBase = config.gameBase;
  PreMovieRecovery::Context black;
  PreMovieRecovery::Capture(black, surface, target, flags, caller, flipProbeState.busy);
  MovieRecovery::Context movie;
  MovieRecovery::Capture(movie, surface, target, flags, caller, flipProbeState.busy);
  FlipProbeSnapshot snapshot;
  snapshot.result = static_cast<DWORD>(E_PENDING);
  snapshot.caller = caller;
  snapshot.surface = reinterpret_cast<DWORD>(surface);
  snapshot.target = reinterpret_cast<DWORD>(target);
  snapshot.flags = flags;
  snapshot.mode = Read(GameRva::mode);
  snapshot.directDraw = Read(GameRva::directDraw);
  snapshot.hardware = Read(GameRva::hardware);
  snapshot.fullscreen = Read(GameRva::fullscreenActive);
  snapshot.transitionDepth = state.transitionDepth;
  snapshot.videoDepth = state.videoDepth;
  snapshot.mainMenuVisible = Read(GameRva::mainMenuVisible);
  snapshot.drawLock = Read(GameRva::drawLock);
  snapshot.drawSession = Read(GameRva::drawSession);
  snapshot.lifecycleSerial = TextureLifecycle::Serial();
  snapshot.restoreSuccesses = state.restoreSuccesses;
  ++flipProbeState.calls;
  if (flipProbeState.busy++)
  {
    ++flipProbeState.nested;
  }

  const DWORD observedDepth = flipProbeState.busy;
  const DWORD observedCall = flipProbeState.calls;
  FlipProbe::Store(flipProbeState.last, snapshot);
  HardwareFlipCompletion completion;
  CaptureHardwareFlipCompletion(&completion, snapshot.surface, snapshot.target, flags, caller,
                                observedDepth, observedCall);
  CaptureFailedPresentationFlip(snapshot.surface, snapshot.target, flags, caller, observedDepth,
                                observedCall, nativeStack);
  const DWORD rendererToken = CapturePostRestoreFlip(snapshot.surface, snapshot.target, flags,
                                                     caller, observedDepth, observedCall);
  __writefsdword(0x34, incomingError);
  const HRESULT result = flip(surface, target, flags);
  const DWORD resultError = __readfsdword(0x34);
  CompletePostRestoreFlip(rendererToken, result);
  CompleteHardwareFlipCompletion(&completion, result);
  CompleteFailedPresentationFlip(result, resultError);
  snapshot.result = static_cast<DWORD>(result);
  FlipProbe::Store(flipProbeState.last, snapshot);
  if (result != S_OK)
  {
    ++flipProbeState.failures;
    FlipProbe::Store(flipProbeState.lastFailure, snapshot);
  }

  __writefsdword(0x34, resultError);
  const HRESULT blackResult = PreMovieRecovery::Recover(black, result, resultError, flip);
  const DWORD blackError = __readfsdword(0x34);
  const HRESULT finalResult = MovieRecovery::Recover(movie, blackResult, blackError, flip);
  const DWORD finalError = __readfsdword(0x34);
  --flipProbeState.busy;
  RecordFailedPresentationBase(observedBase);
  __writefsdword(0x34, finalError);
  return finalResult;
}

extern "C" HRESULT WINAPI ObserveFlip(void *surface, void *target, DWORD flags, DWORD caller)
{
  return ObserveFlipWithStack(surface, target, flags, caller, nullptr);
}

// The replacement CALL adds a return address before the existing three COM
// arguments. The game's caller is beyond those arguments and its saved EBX.
extern "C" __declspec(naked) HRESULT WINAPI FlipProbeHook(void *, void *, DWORD)
{
  __asm {
    pushfd
    cld
    lea eax, [esp + 24]
    push eax
    push dword ptr [esp + 28]
    push dword ptr [esp + 24]
    push dword ptr [esp + 24]
    push dword ptr [esp + 24]
    call ObserveFlipWithStack
    popfd
    test eax, eax
    ret 12
  }
}

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_FLIP_PROBE_HPP
