// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_LAYOUT_PAYLOAD_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_LAYOUT_PAYLOAD_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#ifndef HAV_GAME_ADDRESSES_DEFINED
#error The shared payload requires an explicitly supplied edition address header.
#endif

// Game addresses are selected for an exactly identified retail edition.
// The prototype builder installs config and adds relocations for its pointers.
struct PayloadConfig
{
  DWORD magic, version, gameBase;
  DWORD missionResume, frameOriginal, teardownOriginal, modeResume;
  DWORD videoOpenOriginal, videoCleanupOriginal;
  DWORD mainMenuShowResume, mainMenuHideResume;
  DWORD setupOriginal, releaseResourcesOriginal, unlockOriginal;
  DWORD warmupResume, warmupSuccess, warmupFailure, lockOriginal, clearOriginal;
  DWORD clearPolygonOriginal;
  DWORD unlockResume;
  DWORD rendererInitOriginal, executeDataResume, stateInstructionsResume, stateExecuteDataResume;
  DWORD textureSourceResume;
};

struct PayloadState
{
  // active records ownership of a software layout (1) or hardware menu (2)
  DWORD depth, busy, active, blocked, cancelled, ended;
  HWND frame, view;
  HMENU menu;
  RECT saved, last;

  DWORD appliedCount, restoredCount, failureCount;

  DWORD videoDepth, videoReady, videoBorrowed;
  DWORD mainMenuDepth, transitionDepth, contentActive;

  DWORD setupFailureCount, recoveryCount, setupCleanupBusy;
  DWORD restoreAttempts, restoreSuccesses, restoreFailures;
  DWORD lastRestoreResult, lastPrimaryIsLost, lastBackbufferIsLost, restoreBusy;

  DWORD layoutPadCount, executePadCount, lastActualVertexCount;
};

struct ShellState
{
  DWORD busy, epoch, marked, dirty;
  HWND markedFrame, markedView;
  DWORD markedThread, markedApp, markedFrameObject;
  DWORD syncCalls, clearCalls, reassertCalls, deduplicated, skipped;
  DWORD reentries, invalidations, trueAttempts, falseAttempts;
  DWORD trueSuccesses, falseSuccesses, compensations, failures;
  DWORD lastStage, lastResult;
};

struct FocusState
{
  DWORD armed, busy;
  HWND frame, view;
  DWORD initialMode, captures, attempts, successes, skips, failures;
  HWND beforeFocus, afterFocus, beforeForeground, afterForeground;
  DWORD lastSkipReason, reserved;
};

struct BridgeState
{
  DWORD createAttempts, createSuccesses, beginCalls, endCalls;
  DWORD failures, lastStage, lastResult, sceneDepth;
};

struct TextureLoadState
{
  DWORD busy, calls, lostResults, attempts, successes, failures, lastSlot, lastStage;
  DWORD initialResult, sourceResult, destinationResult, restoreResult, retryResult;
  DWORD reserved[3];
};

static_assert(sizeof(TextureLoadState) == 64, "Texture load state ABI");

enum class FocusSkip : DWORD
{
  none,
  phase,
  windows,
  foreground,
  initialFocus,
  unchangedMode,
  currentFocus,
  unavailable,
  reentry,
  focusFailed,
  stateChanged
};

static_assert(sizeof(void *) == 4, "The payload is x86 only");
static_assert(sizeof(PayloadConfig) == 104, "Builder ABI");
static_assert(sizeof(PayloadState) == 156, "Builder state ABI");
static_assert(sizeof(FocusState) == 64, "Builder focus state ABI");
static_assert(sizeof(ShellState) == 96, "Builder shell state ABI");
static_assert(sizeof(BridgeState) == 32, "Builder bridge state ABI");

// The .def aliases keep the frozen lowercase export names stable
extern "C"
{
  extern PayloadConfig config;
  extern PayloadState state;
  extern FocusState focusState;
  extern ShellState shellState;
  extern BridgeState bridgeState;
  extern TextureLoadState loadState;

  HRESULT WINAPI LoadTextureWithRecovery(void *destination, void *source);
  HRESULT WINAPI TextureLoadHook(void *destination, void *source);
  void WINAPI CaptureTextureSource(DWORD canonicalStack, DWORD convertedEnd);
  DWORD WINAPI TransferTexture(DWORD destination, DWORD source, DWORD canonicalStack);
  void TextureSourceHook();

  void __cdecl AdvanceTextureLifecycle();
  void __cdecl AdvanceTextureFrameEpoch();
  void __cdecl AdvanceTextureFrame();

  HRESULT WINAPI CreateDeviceBridge(void *surface, const GUID *selected, void **output);
  HRESULT WINAPI BeginSceneBridge(void *legacyDevice);
  HRESULT WINAPI EndSceneBridge(void *legacyDevice);
  void CreateDeviceHook();

  void __cdecl SyncShellFullscreen();
  void __cdecl ClearShellFullscreen();

  void __cdecl BeginFocusTransition();
  void __cdecl FinishFocusTransition();

  void __cdecl EnterMission();
  void __cdecl AfterFrame();
  void __cdecl BeforeTeardown();
  void __cdecl BeforeTransition();
  void __cdecl AfterTransition();
  void __cdecl LeaveMission();

  void __cdecl BeforeVideoOpen(HWND requestedView);
  void __cdecl AfterVideoCleanup();

  void __cdecl BeforeMainMenuShow();
  void __cdecl AfterMainMenuShow();
  void __cdecl AfterMainMenuHide();

  int __cdecl MissionHook(int argument);
  void __cdecl FrameHook();
  void __cdecl TeardownHook();

  // ModeHook preserves the original thiscall ABI and returns through RET 4
  void ModeHook();

  int __cdecl VideoOpenHook(HWND requestedView, const char *filename);
  void __cdecl VideoCleanupHook();

  void MainMenuShowHook();
  void MainMenuHideHook();

  void __cdecl AfterFullscreenSetup(int result);
  void __cdecl ReleaseBackbuffer();
  void __cdecl AfterModeAttempt(void *app, DWORD requestedMode, DWORD previousFailures);
  BOOL __cdecl SuppressModeChange();
  BOOL __cdecl PrepareFrameRecovery(const DWORD *registers);
  BOOL __cdecl FullscreenSetupBusy();
  void __cdecl BeginPresentationInput(void *app, const MSG *message, const void *call);
  void __cdecl EndPresentationInput(const void *call);
  void __cdecl FinishPresentationInput();
  BOOL __cdecl PresentationInputCurrent();
  BOOL __cdecl UnlockFrameBusy();
  BOOL __cdecl UnlockFrameDrawLockCurrent();
  BOOL __cdecl UnlockFrameContextCurrent();
  void __cdecl CompleteUnlockFrame();
  DWORD __cdecl TryAbortUnlockFrame(const DWORD *registers, DWORD token);
  BOOL __cdecl PostRestoreRendererBusy();
  void __cdecl StampPostRestoreRenderer(DWORD scope, DWORD root);
  DWORD __cdecl PostRestoreBeforeFrame(const DWORD *registers);
  void __cdecl PostRestoreAfterFrame();
  int __cdecl CallRendererInitForScope();
  DWORD __cdecl CapturePostRestoreFlip(
      DWORD surface, DWORD target, DWORD flags, DWORD caller, DWORD depth, DWORD call);
  void __cdecl CompletePostRestoreFlip(DWORD token, HRESULT result);
  void __cdecl FinishFullscreenSetupScope(DWORD scope);
  int __cdecl CallOwnedFullscreenSetup(DWORD caller,
                                       DWORD *scope,
                                       DWORD requestedMode,
                                       DWORD frameObject);
  int __cdecl FullscreenSetupHook();
  void __cdecl ResourceReleaseHook();

  int __cdecl PrepareStartupFrames();
  void StartupFramesHook();

  int __cdecl ClearPolygonHook(void *vertices, int count, DWORD flags);
  void __cdecl BeforeSurfaceUnlock(void *surface);
  int __cdecl BackbufferLockHook(void *surface, void *descriptor);
  int __cdecl SurfaceUnlockHook(void *surface);

  void __cdecl AfterRendererInit(int result);
  void __cdecl PrepareExecuteData(DWORD *data);
  void __cdecl PrepareStateExecuteData(DWORD *data);
  int __cdecl FinishFlushUnlock(int result);
  int __cdecl FlushUnlockHook();
  int __cdecl RendererInitHook();
  int __cdecl PrepareRenderer();
  void ExecuteDataHook();
  void StateInstructionsHook();
  void StateExecuteDataHook();
}

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_LAYOUT_PAYLOAD_HPP
