// SPDX-License-Identifier: MIT

// Exercises completion and deferred-menu ownership with controlled frame, GDI and command
// callbacks. The real presentation and input-pump instruction chains are covered by
// rendererRecoveryTests.
#include "support/modeCompletionFixture.hpp"
#include "support/menuNativeFixture.hpp"
#include <cstddef>

static void ProgressCases()
{
  Reset();
  EnterHardware();
  Now = 1200;
  Input(true, 1199);
  Check(FlipOnce() == S_OK && hardwareModeCompletionState.pending,
        "Flip before frame cannot release");
  FrameOnce();
  Check(hardwareModeCompletionState.frameReady == 1,
        "Actual FrameHook return qualifies fresh scene");
  Input(true, 1199);
  Now = 1250;
  Check(FlipOnce() == S_OK && !hardwareModeCompletionState.pending,
        "Fresh frame and raw Flip release");
  Check(hardwareModeCompletionState.completed == 1 && modeAdmissionState.completedAt == 1250,
        "Completion updates stale message cutoff");
  Input(true, 1250);
  Input(false, 1251);
  Check(FlipCalls == 2, "No extra native presentation calls");

  Reset();
  EnterHardware();
  SceneSuccess = false;
  FrameOnce();
  Check(!hardwareModeCompletionState.frameReady, "Failed EndScene does not qualify");
  Check(FlipOnce() == S_OK && hardwareModeCompletionState.pending,
        "Success after failed frame cannot release");
  SceneSuccess = true;
  FrameOnce();
  FlipResult = E_FAIL;
  Check(FlipOnce() == E_FAIL && hardwareModeCompletionState.pending,
        "Raw Flip failure remains failure");
  FlipResult = S_OK;
  Check(FlipOnce() == S_OK && hardwareModeCompletionState.pending,
        "Retry without new frame cannot release");
  FrameOnce();
  Check(FlipOnce() == S_OK && !hardwareModeCompletionState.pending,
        "New healthy frame permits completion");
}

static void OrderingAndRecoveryCases()
{
  Reset();
  EnterHardware();
  FrameOnce();
  const DWORD frame = hardwareModeCompletionState.frameSerial;
  Now = 1200;
  Input(true, 1200);
  Check(hardwareModeCompletionState.frameReady && hardwareModeCompletionState.frameSerial == frame,
        "Frame witness survives intervening owned input before the next loop");
  Check(NativePresent() == 0 && FlipCalls == 1 && PumpCalls == 1,
        "Actual presenter wrapper enters native-equivalent generic Flip and message pump");
  Check(hardwareModeCompletionState.completed == 1,
        "Previous frame completes at next-loop presentation before another FrameHook");

  Reset();
  EnterHardware();
  Word(readinessNativePaused) = 1;
  FrameOnce();
  NativePresent();
  Check(hardwareModeCompletionState.pending && !hardwareModeCompletionState.frameReady,
        "Paused phase cannot qualify a successful frame");
  Word(readinessNativePaused) = 0;
  FrameOnce();
  NativePresent();
  Check(hardwareModeCompletionState.completed == 1,
        "Resuming the native phase permits new frame progress");
}

static void DirtyClockState()
{
  __asm {
    fninit
    fld1
    pxor xmm0,xmm0
  }
  SetLastError(0xabcdef12);
}

static void PreservationCases()
{
  Reset();
  EnterHardware();
  FrameOnce();
  __declspec(align(16)) BYTE before[512] = {}, after[512] = {};
  DuringClock = &DirtyClockState;
  __asm {
    fninit
    fldpi
    pcmpeqd xmm0,xmm0
    fxsave before
  }
  FlipOnce();
  __asm {
    fxsave after
    fninit
  }
  Check(std::memcmp(before, after, sizeof(before)) == 0,
        "Completion clock callback preserves x87, SSE and MXCSR state");
  Check(hardwareModeCompletionState.completed == 1,
        "State preservation does not prevent real completion");

  Reset();
  ClockAvailable = false;
  EnterHardware();
  FrameOnce();
  FlipOnce();
  Check(hardwareModeCompletionState.pending && !hardwareModeCompletionState.completed &&
            hardwareModeCompletionState.clockFailures == 1,
        "Missing completion timestamp cannot admit queued input");
  ClockAvailable = true;
  FrameOnce();
  FlipOnce();
  Check(!hardwareModeCompletionState.pending && hardwareModeCompletionState.completed == 1,
        "Restored clock permits a fresh completion without a fixed wait");
}

static void PhaseCases()
{
  const std::vector<std::function<void()>> leave = {[] { state.videoDepth = 1; },
                                                    [] { Word(GameRva::mainMenuVisible) = 1; },
                                                    [] { state.ended = 1; },
                                                    [] { state.depth = 0; },
                                                    [] { Word(GameRva::quit) = 1; },
                                                    [] { Word(GameRva::mode) = 1; },
                                                    []
  {
    Word(GameRva::hardware) = 0;
  }};
  for (const auto &mutation : leave)
  {
    Reset();
    EnterHardware();
    mutation();
    Now = 1200;
    Input(false, 1200);
    Check(!hardwareModeCompletionState.pending && !hardwareModeCompletionState.completed,
          "Leaving scope cancels without claiming ready or deadlocking input");
  }
  for (DWORD mode : {0u, 1u})
  {
    Reset();
    const DWORD token = BeginModeAdmission(App);
    Word(GameRva::mode) = mode;
    EndModeAdmission(App, token);
    Check(!hardwareModeCompletionState.pending, "Software and windowed paths remain native");
  }
  Reset();
  Word(GameRva::mainMenuVisible) = 1;
  const DWORD token = BeginModeAdmission(App);
  Word(GameRva::mode) = 2;
  Word(GameRva::hardware) = Word(GameRva::directDraw) = Word(GameRva::fullscreenActive) = 1;
  EndModeAdmission(App, token);
  Check(!hardwareModeCompletionState.pending, "Main menu cannot wait for gameplay frames");
  Reset();
  EnterHardware();
  MSG other = Message(1200);
  other.wParam = VK_ESCAPE;
  const DWORD calls = NativeCalls;
  reinterpret_cast<BOOL(__thiscall *)(void *, MSG *)>(&ModeAdmissionPreTranslate)(App, &other);
  Check(NativeCalls == calls + 1 && hardwareModeCompletionState.pending,
        "Other controls continue while pending");
}

static void SoftwareProgressCases()
{
  for (bool stretch : {false, true})
  {
    Reset();
    EnterSoftware(2, 1);
    StretchPaint = stretch;
    Now = 1200;
    SoftwarePresent();
    Check(softwareModeCompletionState.pending && !softwareModeCompletionState.completed,
          "Repaint of old pixels after mode setup cannot complete admission");
    Input(true, 1200);
    FrameOnce();
    Check(softwareModeCompletionState.frameReady == 1,
          "Fresh software FrameHook return is recorded");
    Input(true, 1201);
    Now = 1250;
    SoftwarePresent();
    Check(!softwareModeCompletionState.pending && softwareModeCompletionState.completed == 1,
          "Fresh frame followed by successful owned native GDI releases mode1");
    Input(true, 1250);
    Input(false, 1251);
    EnterSoftware(1, 0);
    Now = 1400;
    Input(true, 1401);
    FrameOnce();
    Input(true, 1402);
    SoftwarePresent();
    Check(softwareModeCompletionState.completed == 2,
          "Mode1 to windowed mode0 needs its own fresh frame and native GDI success");
    Input(false, 1401);
    const DWORD token = BeginModeAdmission(App);
    Word(GameRva::mode) = 2;
    Word(GameRva::hardware) = Word(GameRva::directDraw) = Word(GameRva::fullscreenActive) = 1;
    EndModeAdmission(App, token);
    Check(!softwareModeCompletionState.pending && hardwareModeCompletionState.pending,
          "Mode0 to hardware mode2 switches to the raw Flip barrier");
    Input(true, 1402);
    FrameOnce();
    FlipOnce();
    Check(hardwareModeCompletionState.completed == 1,
          "Cross-mode cycle retains the existing native hardware completion");
  }
  Reset();
  EnterSoftware(2, 1);
  FrameOnce();
  GdiResult = FALSE;
  SoftwarePresent();
  Check(softwareModeCompletionState.pending && softwareModeCompletionState.gdiFailures == 1,
        "False GDI return cannot become readiness");
  GdiResult = 37;
  SoftwarePresent();
  Check(softwareModeCompletionState.pending,
        "Retrying old pixels cannot reuse failed frame witness");
  FrameOnce();
  SoftwarePresent();
  Check(softwareModeCompletionState.completed == 1,
        "A new complete frame can recover after GDI failure");
}

static void LeanOwnershipCases()
{
  Reset();
  EnterHardware();
  DuringFrame = []
  {
    ++state.restoreSuccesses;
  };
  FrameOnce();
  Check(hardwareModeCompletionState.pending && !hardwareModeCompletionState.frameReady,
        "A restored frame cannot certify its old surface epoch");
  textureState.pendingEpoch = textureState.completedEpoch = state.restoreSuccesses;
  textureState.lastResult = S_OK;
  FrameOnce();
  Check(hardwareModeCompletionState.frameReady,
        "Next actual frame can use the independently completed cache epoch");
  FlipOnce();
  Check(!hardwareModeCompletionState.pending && hardwareModeCompletionState.completed == 1,
        "Fresh frame and real Flip complete after a restore without self-waiting");

  Reset();
  EnterHardware();
  FrameOnce();
  DuringFlip = []
  {
    bridgeState.sceneDepth = 1;
  };
  Check(FlipOnce() == S_OK && hardwareModeCompletionState.pending &&
            !hardwareModeCompletionState.completed,
        "An in-flight scene callback cannot complete a mode even with raw Flip success");

  Reset();
  using Translate = BOOL(__thiscall *)(void *, MSG *);
  auto translate = reinterpret_cast<Translate>(&ModeAdmissionPreTranslate);
  MSG message = Message(1200);
  message.lParam |= 1u << 30;
  SetLastError(errorIn);
  Check(translate(App, &message) == TRUE && NativeCalls == 0 &&
            modeAdmissionState.suppressedRepeat == 1 && GetLastError() == errorIn,
        "Held F4 repeats are consumed without a native mode attempt");
  message.message = WM_KEYUP;
  Check(translate(App, &message) == 0x12345678 && NativeCalls == 1,
        "F4 key release reaches the unchanged native input handler");
  message = Message(1300);
  message.lParam |= 1u << 29;
  Check(translate(App, &message) == 0x12345678 && NativeCalls == 2,
        "Alt-context F4 remains native");
  message = Message(1400);
  Check(translate(App, &message) == 0x12345678 && NativeCalls == 3,
        "Fresh released F4 is admitted after a held key");
}

static void CompletionInvalidationCases()
{
  for (bool hardware : {false, true})
  {
    for (unsigned mutation = 0; mutation != 4; ++mutation)
    {
      Reset();
      if (hardware)
      {
        EnterHardware();
      }
      else
      {
        EnterSoftware(2, 1);
      }
      FrameOnce();
      auto change = [mutation, hardware]
      {
        if (mutation == 0)
        {
          ++textureLifecycleState.frameEpoch;
        }
        if (mutation == 1)
        {
          ++state.restoreSuccesses;
        }
        if (mutation == 2)
        {
          Word(GameRva::view) = 0;
        }
        if (mutation == 3)
        {
          if (hardware)
          {
            ++bridgeState.beginCalls;
          }
          else
          {
            ++Word(SoftwareModeCompletion::bitmapRva);
          }
        }
      };
      if (hardware)
      {
        DuringFlip = change;
        Check(FlipOnce() == S_OK && !hardwareModeCompletionState.completed,
              "Changed native Flip owner/scene must not complete admission");
      }
      else
      {
        DuringGdi = change;
        SoftwarePresent();
        Check(!softwareModeCompletionState.completed,
              "Changed native GDI owner/epoch must not complete admission");
      }
      ++Cases;
    }
  }
}

static void HardwareRecoveryGuards()
{
  for (DWORD *flag : {&quadFrameRecoveryState.busy, &quadFrameRecoveryState.aborted,
                      &quadFrameRecoveryState.pendingPresent, &quadFrameRecoveryState.awaitingFrame,
                      &quadFrameRecoveryState.guardIncomplete})
  {
    Reset();
    EnterHardware();
    Check(HardwareModeCompletion::Quiet(), "Hardware quiet baseline is eligible");
    *flag = 1;
    Check(!HardwareModeCompletion::Quiet(), "Hardware completion admitted an active recovery");
    HardwareCompletionBeginFrame();
    Check(!hardwareModeCompletionState.frameStarted && hardwareModeCompletionState.pending,
          "Hardware recovery acquired a new completion frame");
    Check(FlipOnce() == S_OK && !hardwareModeCompletionState.completed,
          "Raw Flip cleared completion without a qualified frame");
  }
  Reset();
  EnterHardware();
  FrameOnce();
  Check(hardwareModeCompletionState.frameReady && quadFrameRecoveryState.normalFrames == 1,
        "Composed actual frame return must qualify hardware completion");
  Check(!quadFrameRecoveryState.depth && !normalFrameState.depth,
        "Both independent frame scopes must close");
  FlipOnce();
  Check(hardwareModeCompletionState.completed == 1,
        "Actual owned frame and native Flip complete the hardware barrier");
}

static void SoftwareAfterHardwareRecovery()
{
  for (bool stretch : {false, true})
  {
    Reset();
    quadFrameRecoveryState.awaitingFrame = 1;
    quadFrameRecoveryState.successes = quadFrameRecoveryState.attempts = 1;
    EnterSoftware(2, 1);
    StretchPaint = stretch;
    Check(SoftwareModeCompletion::Quiet(),
          "A stale hardware confirmation cannot block a valid software phase");
    FrameOnce();
    Check(softwareModeCompletionState.frameReady && quadFrameRecoveryState.awaitingFrame == 1,
          "Software owns its fresh frame while hardware confirmation stays unclaimed");
    Now = 1300;
    SoftwarePresent();
    Check(softwareModeCompletionState.completed == 1 && !softwareModeCompletionState.pending,
          "Fresh software frame and native GDI complete despite stale hardware awaitingFrame");
    Check(!quadFrameRecoveryState.confirmedFrames,
          "Software presentation must not fabricate hardware recovery confirmation");
    Input(false, 1301);
  }
  for (DWORD *flag : {&quadFrameRecoveryState.aborted, &quadFrameRecoveryState.pendingPresent})
  {
    Reset();
    EnterSoftware(2, 1);
    *flag = 1;
    Check(!SoftwareModeCompletion::Quiet(), "Original software recovery exclusion must remain");
  }
}

static BYTE MenuBitmap[0x74], MenuDocument[512], MenuReplacementBitmap[0x74],
    MenuReplacementDocument[512];
static BYTE *MenuActiveBitmap = MenuBitmap;
static RECT MenuClient = {0, 0, 1280, 960};
static DWORD MenuPresentAddress, MenuDrawAddress, MenuBitmapAddress, MenuRawGdiAddress;
static DWORD MenuQueueAddress, MenuResumeAddress, MenuNewGameAddress, MenuMultiplayerAddress;
static DWORD MenuPresentCalls, MenuDrawCalls, MenuBitmapCalls, MenuInvalidations;
static DWORD MenuQueueCalls, MenuResumeCalls, MenuNewGameCalls, MenuMultiplayerCalls;
static DWORD MenuLastCommand, MenuLastDocument;
static DWORD MenuSourceDc = 0x8080, MenuBitmapHandle = 0x9090;
static DWORD MenuSourceWidth = 640, MenuSourceHeight = 480;
static bool MenuSkipBitmap = false, MenuSkipGdi = false;
static BOOL MenuInvalidateResult = TRUE, MenuRectResult = TRUE;
static std::function<void()> DuringMenuPresent, DuringMenuDraw, DuringMenuBitmap;
static std::function<void()> DuringMenuInvalidate, DuringMenuRect, DuringMenuQueue,
    DuringMenuResume;
static DWORD MenuPreparedBitmapSite, MenuPreparedGdiSite;

static BOOL WINAPI MenuClientRect(HWND, LPRECT rect)
{
  Callback(DuringMenuRect);
  *rect = MenuClient;
  SetLastError(0xabcdef01);
  return MenuRectResult;
}

static BOOL WINAPI MenuInvalidate(HWND, const RECT *, BOOL)
{
  ++MenuInvalidations;
  Callback(DuringMenuInvalidate);
  SetLastError(0xabcdef02);
  return MenuInvalidateResult;
}

static DWORD
BuildNativeCall(DWORD callsite, DWORD target, const std::vector<DWORD> &arguments, DWORD object = 0)
{
  const DWORD begin = callsite - static_cast<DWORD>(arguments.size() * 5) - (object ? 5 : 0);
  DWORD cursor = begin;
  for (auto it = arguments.rbegin(); it != arguments.rend(); ++it)
  {
    Game[cursor] = 0x68;
    Word(cursor + 1) = *it;
    cursor += 5;
  }
  if (object)
  {
    Game[cursor] = 0xb9;
    Word(cursor + 1) = object;
    cursor += 5;
  }
  Check(cursor == callsite, "Fixture native callsite alignment");
  Branch(callsite, target, 0xe8);
  Game[callsite + 5] = 0x90;
  Game[callsite + 6] = 0xc3;
  FlushInstructionCache(GetCurrentProcess(), Game + begin, callsite - begin + 7);
  return reinterpret_cast<DWORD>(Game + begin);
}

static DWORD __fastcall NativeMenuBitmap(
    void *bitmap, void *, void *dc, RECT *rect, DWORD operation)
{
  ++MenuBitmapCalls;
  Check(bitmap == MenuActiveBitmap, "Native bitmap receives original this pointer");
  Callback(DuringMenuBitmap);
  if (!MenuSkipGdi)
  {
    const std::vector<DWORD> args = {static_cast<DWORD *>(dc)[1],
                                     static_cast<DWORD>(rect->left),
                                     static_cast<DWORD>(rect->top),
                                     static_cast<DWORD>(rect->right - rect->left),
                                     static_cast<DWORD>(rect->bottom - rect->top),
                                     MenuSourceDc,
                                     0,
                                     0,
                                     MenuSourceWidth,
                                     MenuSourceHeight,
                                     operation};
    MenuPreparedGdiSite = BuildNativeCall((menuNativeStretchCaller - 5), MenuRawGdiAddress, args);
    const BOOL result = reinterpret_cast<BOOL(__cdecl *)()>(MenuPreparedGdiSite)();
    Check(result == GdiResult, "Menu raw GDI BOOL is preserved exactly");
    Check(GetLastError() == errorNative, "Menu raw GDI LastError is preserved exactly");
  }
  SetLastError(errorNative);
  return 0x1234; // Native helper result is deliberately not raw GDI success.
}

static DWORD __fastcall NativeMenuDraw(void *, void *, void *dc)
{
  ++MenuDrawCalls;
  Callback(DuringMenuDraw);
  if (!MenuSkipBitmap)
  {
    const std::vector<DWORD> args = {reinterpret_cast<DWORD>(dc),
                                     reinterpret_cast<DWORD>(&MenuClient), SRCCOPY};
    MenuPreparedBitmapSite = BuildNativeCall((menuNativeBitmapCaller - 5), MenuBitmapAddress, args,
                                             reinterpret_cast<DWORD>(MenuActiveBitmap));
    Check(reinterpret_cast<DWORD(__cdecl *)()>(MenuPreparedBitmapSite)() == 0x1234,
          "Menu bitmap adapter preserves native helper return");
    Check(GetLastError() == errorNative, "Menu bitmap adapter preserves native LastError");
  }
  SetLastError(errorNative);
  return 0x1357;
}

static DWORD __fastcall NativeMenuPresent(void *, void *)
{
  ++MenuPresentCalls;
  Callback(DuringMenuPresent);
  if (!SkipPaint)
  {
    reinterpret_cast<void(__thiscall *)(void *, void *)>(MenuDrawAddress)(View, PaintDcObject);
  }
  SetLastError(errorNative);
  return 0x2468;
}

static DWORD __fastcall NativeMenuQueue(void *app, void *)
{
  ++MenuQueueCalls;
  MenuLastCommand = *reinterpret_cast<DWORD *>(static_cast<BYTE *>(app) + 0x128);
  Callback(DuringMenuQueue);
  *reinterpret_cast<DWORD *>(static_cast<BYTE *>(app) + 0x128) = 0;
  SetLastError(errorNative);
  return 0x3579;
}

static DWORD __fastcall NativeMenuResume(void *document, void *)
{
  ++MenuResumeCalls;
  MenuLastDocument = reinterpret_cast<DWORD>(document);
  Callback(DuringMenuResume);
  SetLastError(errorNative);
  return 0x468a;
}

static DWORD __fastcall NativeMenuNewGame(void *app, void *)
{
  ++MenuNewGameCalls;
  *reinterpret_cast<DWORD *>(static_cast<BYTE *>(app) + 0x128) = 0x8003;
  SetLastError(errorNative);
  return 0x579b;
}

static DWORD __fastcall NativeMenuMultiplayer(void *app, void *)
{
  ++MenuMultiplayerCalls;
  *reinterpret_cast<DWORD *>(static_cast<BYTE *>(app) + 0x128) = 0x8033;
  SetLastError(errorNative);
  return 0x68ac;
}

static BOOL WINAPI MenuNotIconic(HWND)
{
  return FALSE;
}

static BOOL WINAPI MenuChild(HWND parent, HWND child)
{
  return parent == reinterpret_cast<HWND>(0x1010) && child == reinterpret_cast<HWND>(0x2020);
}

static DWORD &QueuedCommand()
{
  return *reinterpret_cast<DWORD *>(App + 0x128);
}

static void MenuReset()
{
  Reset();
  std::memset(&menuModeCompletionState, 0, sizeof(menuModeCompletionState));
  std::memset(MenuBitmap, 0, sizeof(MenuBitmap));
  std::memset(MenuDocument, 0, sizeof(MenuDocument));
  std::memset(MenuReplacementBitmap, 0, sizeof(MenuReplacementBitmap));
  std::memset(MenuReplacementDocument, 0, sizeof(MenuReplacementDocument));
  MenuActiveBitmap = MenuBitmap;
  state.depth = 0;
  Word(GameRva::mainMenuVisible) = 1;
  Word(GameRva::isWindowVisible) = reinterpret_cast<DWORD>(&Window);
  Word(GameRva::isIconic) = reinterpret_cast<DWORD>(&MenuNotIconic);
  Word(GameRva::isChild) = reinterpret_cast<DWORD>(&MenuChild);
  Word(GameRva::getClientRect) = reinterpret_cast<DWORD>(&MenuClientRect);
  Word(MenuModeCompletion::invalidateImport) = reinterpret_cast<DWORD>(&MenuInvalidate);
  Word(MenuModeCompletion::stretchImport) = reinterpret_cast<DWORD>(&NativeStretchBlt);
  *reinterpret_cast<DWORD *>(App) = config.gameBase + fixtureAppVtable;
  *reinterpret_cast<DWORD *>(App + 0xec) = reinterpret_cast<DWORD>(MenuBitmap);
  *reinterpret_cast<DWORD *>(App + 0xf4) = reinterpret_cast<DWORD>(MenuDocument);
  *reinterpret_cast<DWORD *>(MenuBitmap) = config.gameBase + MenuModeCompletion::bitmapVtable;
  *reinterpret_cast<DWORD *>(MenuBitmap + 8) = 640;
  *reinterpret_cast<DWORD *>(MenuBitmap + 12) = 480;
  *reinterpret_cast<DWORD *>(MenuBitmap + 0x60) = MenuBitmapHandle;
  *reinterpret_cast<DWORD *>(MenuBitmap + 0x64) = 0x101010;
  Word(fixtureAppPresentSlot) = reinterpret_cast<DWORD>(&SoftwarePresentHook);
  Word(fixtureQueueSlot) = reinterpret_cast<DWORD>(&MenuQueueHook);
  MenuPresentAddress = reinterpret_cast<DWORD>(&SoftwarePresentHook);
  MenuDrawAddress = reinterpret_cast<DWORD>(&MenuDrawHook);
  MenuBitmapAddress = reinterpret_cast<DWORD>(&MenuBitmapHook);
  MenuRawGdiAddress = reinterpret_cast<DWORD>(&ObserveMenuStretchBlt);
  MenuQueueAddress = reinterpret_cast<DWORD>(&MenuQueueHook);
  MenuResumeAddress = reinterpret_cast<DWORD>(&MenuResumeHook);
  MenuNewGameAddress = reinterpret_cast<DWORD>(&MenuNewGameHook);
  MenuMultiplayerAddress = reinterpret_cast<DWORD>(&MenuMultiplayerHook);
  Branch(SoftwareModeCompletion::presentEntry, reinterpret_cast<DWORD>(&NativeMenuPresent), 0xe9);
  Branch(SoftwareModeCompletion::drawEntry, reinterpret_cast<DWORD>(&NativeMenuDraw), 0xe9);
  Branch(MenuModeCompletion::bitmapEntry, reinterpret_cast<DWORD>(&NativeMenuBitmap), 0xe9);
  Branch(MenuModeCompletion::queueEntry, reinterpret_cast<DWORD>(&NativeMenuQueue), 0xe9);
  Branch(MenuModeCompletion::resumeEntry, reinterpret_cast<DWORD>(&NativeMenuResume), 0xe9);
  Branch(fixtureNewGameEntry, reinterpret_cast<DWORD>(&NativeMenuNewGame), 0xe9);
  Branch(fixtureMultiplayerEntry, reinterpret_cast<DWORD>(&NativeMenuMultiplayer), 0xe9);
  // Preserve the exact native indirect app virtual-call return address.
  Game[(menuNativeQueueCaller - 13)] = 0xb9;
  Word((menuNativeQueueCaller - 12)) = reinterpret_cast<DWORD>(App);
  Game[(menuNativeQueueCaller - 8)] = 0x8b;
  Game[(menuNativeQueueCaller - 7)] = 0x01;
  const BYTE indirect[] = {0xff, 0x90, 0xa0, 0, 0, 0, 0xc3};
  std::memcpy(Game + (menuNativeQueueCaller - 6), indirect, sizeof(indirect));
  FlushInstructionCache(GetCurrentProcess(), Game + (menuNativeQueueCaller - 13), 14);
  MenuClient = {0, 0, 1280, 960};
  MenuSourceWidth = 640;
  MenuSourceHeight = 480;
  MenuPresentCalls = MenuDrawCalls = MenuBitmapCalls = MenuInvalidations = 0;
  MenuQueueCalls = MenuResumeCalls = MenuNewGameCalls = MenuMultiplayerCalls = 0;
  MenuLastCommand = MenuLastDocument = 0;
  MenuSkipBitmap = MenuSkipGdi = false;
  MenuInvalidateResult = MenuRectResult = TRUE;
  DuringMenuPresent = DuringMenuDraw = DuringMenuBitmap = {};
  DuringMenuInvalidate = DuringMenuRect = DuringMenuQueue = DuringMenuResume = {};
}

static void MenuArm()
{
  SetLastError(errorIn);
  MenuCompletionAfterShow();
  Check(GetLastError() == errorIn, "Menu arm preserves LastError");
  Check(menuModeCompletionState.pending == 1 && menuModeCompletionState.armed == 1,
        "Owned visible splash arms menu completion");
  Check(MenuPresentCalls == 0, "Arming does not repaint inside native setup");
}

static DWORD MenuQueueOnce()
{
  SetLastError(errorIn);
  return reinterpret_cast<DWORD(__cdecl *)()>(Game + (menuNativeQueueCaller - 13))();
}

static void MenuPaintOnce()
{
  SetLastError(errorIn);
  reinterpret_cast<void(__thiscall *)(void *, void *)>(MenuDrawAddress)(View, PaintDcObject);
  Check(GetLastError() == errorNative, "Ordinary menu paint preserves native LastError");
}

static void MenuProgressCases()
{
  MenuReset();
  Check(sizeof(MenuModeCompletionState) == 268 && sizeof(MenuCompletionOwner) == 88 &&
            offsetof(MenuModeCompletionState, owner) == 180 &&
            offsetof(MenuCompletionOwner, client) == 72 &&
            offsetof(MenuModeCompletionState, rearmPending) == 176,
        "Menu state and owner ABI offsets");
  MenuArm();
  QueuedCommand() = 0x8003;
  Now = 1200;
  Check(MenuQueueOnce() == 0x3579, "Completed queue preserves native consumer result");
  Check(GetLastError() == errorNative, "Completed queue preserves native consumer LastError");
  Check(!menuModeCompletionState.pending && menuModeCompletionState.completed == 1 &&
            MenuPresentCalls == 1 && GdiCalls == 1 && MenuQueueCalls == 1,
        "One selected native presentation completes the menu before queued launch");
  Check(MenuLastCommand == 0x8003 && QueuedCommand() == 0 && modeAdmissionState.completedAt == 1200,
        "Exact native pending command and completion cutoff are preserved");
  Check(MenuInvalidations == 0, "Successful GDI does not request a retry");

  MenuReset();
  const DWORD token = BeginModeAdmission(App);
  MenuPaintOnce();
  Check(!menuModeCompletionState.ready && !menuModeCompletionState.pending,
        "Paint inside initial mode transition does not establish final-mode completion");
  Word(GameRva::mode) = 1;
  EndModeAdmission(App, token);
  Check(menuModeCompletionState.pending && !menuModeCompletionState.ready,
        "Completed mode publication arms a fresh menu witness");
  QueuedCommand() = 0x8033;
  MenuQueueOnce();
  Check(MenuLastCommand == 0x8033 && menuModeCompletionState.completed == 1,
        "Multiplayer queue proceeds after final-mode presentation");

  MenuReset();
  MenuArm();
  MenuPaintOnce();
  Check(menuModeCompletionState.ready && menuModeCompletionState.pending &&
            !menuModeCompletionState.completed,
        "Ordinary paint records readiness without completing inside WM_PAINT");
  MenuQueueOnce();
  Check(!menuModeCompletionState.pending && MenuPresentCalls == 0 && MenuQueueCalls == 1,
        "Next exact native queue boundary consumes an ordinary paint witness");
}

static void MenuFailureLivenessCases()
{
  MenuReset();
  MenuArm();
  GdiResult = FALSE;
  QueuedCommand() = 0x8003;
  MenuQueueOnce();
  Check(GetLastError() == errorIn, "Held queue preserves incoming LastError");
  Check(menuModeCompletionState.pending && !menuModeCompletionState.ready &&
            menuModeCompletionState.gdiFailures == 1 && MenuPresentCalls == 1 &&
            MenuInvalidations == 1 && MenuQueueCalls == 0 && QueuedCommand() == 0x8003,
        "Failed repaint stays failed, retains launch and schedules one ordinary repaint");
  for (int i = 0; i != 4; ++i)
  {
    MenuQueueOnce();
  }
  Check(MenuPresentCalls == 1 && MenuInvalidations == 1 && MenuQueueCalls == 0 &&
            QueuedCommand() == 0x8003,
        "Repeated queue polling neither spins repaint nor loses the native command");
  GdiResult = 17;
  MenuPaintOnce();
  Check(menuModeCompletionState.pending && menuModeCompletionState.ready,
        "Ordinary paint after failure can record a fresh success");
  MenuQueueOnce();
  Check(menuModeCompletionState.completed == 1 && MenuQueueCalls == 1 &&
            MenuLastCommand == 0x8003 && MenuInvalidations == 1,
        "Deferred queue is released once after successful ordinary repaint");

  for (bool noBitmap : {false, true})
  {
    MenuReset();
    MenuArm();
    MenuSkipBitmap = noBitmap;
    MenuSkipGdi = !noBitmap;
    QueuedCommand() = 0x8003;
    MenuQueueOnce();
    Check(menuModeCompletionState.pending && MenuQueueCalls == 0 && MenuInvalidations == 0 &&
              menuModeCompletionState.gdiFailures == 0 && MenuPresentCalls == 1,
          "Missing GDI witness is not fabricated into success or an actual GDI failure");
    MenuQueueOnce();
    Check(MenuPresentCalls == 1, "No-witness queue remains bounded");
    MenuSkipBitmap = MenuSkipGdi = false;
    MenuPaintOnce();
    MenuQueueOnce();
    Check(menuModeCompletionState.completed == 1,
          "Ordinary real paint restores no-witness progress");
  }

  MenuReset();
  MenuArm();
  GdiResult = FALSE;
  MenuInvalidateResult = FALSE;
  QueuedCommand() = 0x8003;
  MenuQueueOnce();
  Check(menuModeCompletionState.retryPosted == 1 && !menuModeCompletionState.retryPostResult &&
            MenuInvalidations == 1 && menuModeCompletionState.pending,
        "Failed InvalidateRect is recorded without unbounded retry");
  MenuQueueOnce();
  Check(MenuInvalidations == 1, "Failed invalidation does not spin");
  GdiResult = TRUE;
  MenuPaintOnce();
  MenuQueueOnce();
  Check(menuModeCompletionState.completed == 1,
        "A later ordinary paint can recover even after invalidation failure");
}

static void MenuInput(MSG message, bool consumed)
{
  const MSG saved = message;
  const DWORD before = NativeCalls;
  SetLastError(errorIn);
  const BOOL result = reinterpret_cast<BOOL(__thiscall *)(void *, MSG *)>(
      &ModeAdmissionPreTranslate)(App, &message);
  Check(consumed ? result == TRUE : result == 0x12345678,
        "Menu input preserves native or consumed result");
  Check(NativeCalls == before + (consumed ? 0 : 1), "Only owned gated F4 is consumed");
  Check(std::memcmp(&message, &saved, sizeof(message)) == 0, "Menu input never rewrites MSG");
  Check(GetLastError() == (consumed ? errorIn : errorNative), "Menu input LastError contract");
}

static void MenuResumeAndCommandCases()
{
  MenuReset();
  MenuArm();
  GdiResult = FALSE;
  QueuedCommand() = 0x8033;
  SetLastError(errorIn);
  reinterpret_cast<void(__thiscall *)(void *)>(MenuResumeAddress)(MenuDocument);
  Check(GetLastError() == errorIn && MenuResumeCalls == 0 &&
            menuModeCompletionState.deferredResume == 1 && QueuedCommand() == 0x8033,
        "Direct Resume retains an explicit document without overwriting native queued command");
  MenuQueueOnce();
  Check(MenuResumeCalls == 0 && QueuedCommand() == 0x8033 && MenuQueueCalls == 0,
        "Resume and native queued launch remain deferred after failed repaint");
  GdiResult = TRUE;
  MenuPaintOnce();
  MenuQueueOnce();
  Check(MenuResumeCalls == 1 && MenuLastDocument == reinterpret_cast<DWORD>(MenuDocument) &&
            MenuQueueCalls == 1 && MenuLastCommand == 0x8033 &&
            !menuModeCompletionState.deferredResume,
        "Successful menu witness resumes exact document once and preserves queued native work");
  MenuQueueOnce();
  Check(MenuResumeCalls == 1, "Deferred Resume is not replayed on later queue polling");

  MenuReset();
  MenuArm();
  reinterpret_cast<void(__thiscall *)(void *)>(MenuResumeAddress)(MenuDocument);
  ++modeAdmissionState.ownerGeneration;
  MenuQueueOnce();
  Check(MenuResumeCalls == 0 && !menuModeCompletionState.deferredResume &&
            menuModeCompletionState.cancelledResume == 1,
        "Obsolete owner cancels stored Resume instead of calling old document");

  MenuReset();
  MenuArm();
  reinterpret_cast<void(__thiscall *)(void *)>(MenuResumeAddress)(MenuDocument);
  Word(GameRva::quit) = 1;
  QueuedCommand() = 0x8003;
  MenuQueueOnce();
  Check(!menuModeCompletionState.pending && !menuModeCompletionState.deferredResume &&
            MenuResumeCalls == 0 && MenuQueueCalls == 0 && QueuedCommand() == 0x8003,
        "Quit cancels deferred Resume and blocks queued launch without rewriting native slot");

  for (DWORD command : {0xe141u, 0xe140u, 0xe144u, 0x8042u, 0x8043u, 0x7777u})
  {
    MenuReset();
    MenuArm();
    GdiResult = FALSE;
    QueuedCommand() = command;
    Check(MenuQueueOnce() == 0x3579 && MenuQueueCalls == 1 && MenuLastCommand == command &&
              menuModeCompletionState.pending,
          "Exit, About and unrelated native queued commands are never blocked by menu readiness");
  }
  for (DWORD command : {0x8003u, 0x8033u, 0x8005u, 0x2710u, 0x2711u, 0x2712u, 0x2713u})
  {
    MenuReset();
    MenuArm();
    GdiResult = FALSE;
    QueuedCommand() = command;
    MenuQueueOnce();
    Check(MenuQueueCalls == 0 && QueuedCommand() == command &&
              menuModeCompletionState.launchDeferrals == 1,
          "Every reviewed launch queue value remains intact while owned menu is pending");
  }
  MenuReset();
  Word(GameRva::mainMenuVisible) = 0;
  *reinterpret_cast<DWORD *>(App + 0xec) = 0;
  MenuCompletionAfterShow();
  QueuedCommand() = 0x2711;
  MenuQueueOnce();
  Check(!menuModeCompletionState.pending && MenuQueueCalls == 1 && MenuLastCommand == 0x2711,
        "Startup without visible splash keeps its native launch route");
}

static void MenuEpochRearmCases()
{
  const std::vector<std::function<void()>> changes = {
      [] { ++textureLifecycleState.serial; },
      [] { ++textureLifecycleState.frameEpoch; },
      [] { *reinterpret_cast<DWORD *>(MenuBitmap + 0x60) += 4; },
      [] { *reinterpret_cast<DWORD *>(MenuBitmap + 0x64) += 4; },
      []
  {
    *reinterpret_cast<DWORD *>(MenuBitmap + 8) = 320;
    *reinterpret_cast<DWORD *>(MenuBitmap + 12) = 200;
    MenuSourceWidth = 320;
    MenuSourceHeight = 200;
  },
      []
  {
    std::memcpy(MenuReplacementBitmap, MenuBitmap, sizeof(MenuBitmap));
    *reinterpret_cast<DWORD *>(MenuReplacementBitmap + 0x60) += 4;
    *reinterpret_cast<DWORD *>(MenuReplacementBitmap + 0x64) += 4;
    MenuActiveBitmap = MenuReplacementBitmap;
    *reinterpret_cast<DWORD *>(App + 0xec) = reinterpret_cast<DWORD>(MenuActiveBitmap);
  }};
  for (const auto &change : changes)
  {
    MenuReset();
    MenuArm();
    GdiResult = FALSE;
    QueuedCommand() = 0x8003;
    MenuQueueOnce();
    const DWORD generation = menuModeCompletionState.generation;
    change();
    GdiResult = TRUE;
    MenuQueueOnce();
    Check(menuModeCompletionState.generation == generation + 1 &&
              menuModeCompletionState.completed == 1 && !menuModeCompletionState.pending &&
              MenuPresentCalls == 2 && GdiCalls == 2 && MenuQueueCalls == 1 &&
              MenuLastCommand == 0x8003,
          "Same UI resource or epoch change rearms one fresh actual GDI witness after failure");
    Check(menuModeCompletionState.owner.lifecycle == TextureLifecycle::Serial() &&
              menuModeCompletionState.owner.frameEpoch == TextureLifecycle::FrameEpoch() &&
              menuModeCompletionState.owner.bitmapObject ==
                  reinterpret_cast<DWORD>(MenuActiveBitmap),
          "Completed menu witness pins the replacement resource epoch and bitmap identity");
  }

  for (bool nullBitmap : {false, true})
  {
    MenuReset();
    MenuArm();
    GdiResult = FALSE;
    QueuedCommand() = 0x8003;
    MenuQueueOnce();
    if (nullBitmap)
    {
      *reinterpret_cast<DWORD *>(App + 0xec) = 0;
    }
    else
    {
      MenuRectResult = FALSE;
    }
    MenuQueueOnce();
    MenuQueueOnce();
    Check(menuModeCompletionState.pending && !menuModeCompletionState.completed &&
              MenuQueueCalls == 0 && QueuedCommand() == 0x8003 && MenuPresentCalls == 1 &&
              GdiCalls == 1,
          "Temporarily missing bitmap or failing client query cannot launch or spin presentation");
    *reinterpret_cast<DWORD *>(App + 0xec) = reinterpret_cast<DWORD>(MenuBitmap);
    MenuRectResult = TRUE;
    GdiResult = TRUE;
    MenuQueueOnce();
    Check(menuModeCompletionState.completed == 1 && MenuQueueCalls == 1 && MenuPresentCalls == 2 &&
              GdiCalls == 2 && MenuLastCommand == 0x8003,
          "Valid recapture after temporary failure requires fresh GDI even for the same restored "
          "identity");
  }

  MenuReset();
  MenuArm();
  GdiResult = FALSE;
  QueuedCommand() = 0x8003;
  MenuQueueOnce();
  *reinterpret_cast<DWORD *>(App + 0xec) = 0;
  MenuQueueOnce();
  Check(menuModeCompletionState.rearmPending == 1,
        "Failed recapture publishes explicit rearmPending");
  Now = 2000;
  MenuInput(Message(2001), true);
  reinterpret_cast<void(__thiscall *)(void *)>(MenuResumeAddress)(MenuDocument);
  MenuQueueOnce();
  Check(menuModeCompletionState.pending && menuModeCompletionState.rearmPending &&
            menuModeCompletionState.deferredResume && MenuResumeCalls == 0 && MenuQueueCalls == 0 &&
            QueuedCommand() == 0x8003 && MenuPresentCalls == 1,
        "Fresh F4 and direct Resume stay held during a resource gap without changing native queue");
  *reinterpret_cast<DWORD *>(App + 0xec) = reinterpret_cast<DWORD>(MenuBitmap);
  GdiResult = TRUE;
  MenuQueueOnce();
  Check(!menuModeCompletionState.rearmPending && menuModeCompletionState.completed == 1 &&
            MenuResumeCalls == 1 && MenuQueueCalls == 1 && MenuLastCommand == 0x8003 &&
            MenuPresentCalls == 2,
        "Valid recapture releases held Resume and native launch only after fresh GDI");

  for (bool ownerChanged : {false, true})
  {
    MenuReset();
    MenuArm();
    GdiResult = FALSE;
    QueuedCommand() = 0x8003;
    MenuQueueOnce();
    *reinterpret_cast<DWORD *>(App + 0xec) = 0;
    MenuQueueOnce();
    Check(menuModeCompletionState.rearmPending == 1,
          "Cancellation fixture begins with failed recapture");
    if (ownerChanged)
    {
      ++modeAdmissionState.ownerGeneration;
    }
    else
    {
      state.videoDepth = 1;
    }
    Now = 2000;
    MenuInput(Message(2001), false);
    Check(!menuModeCompletionState.pending && !menuModeCompletionState.rearmPending &&
              menuModeCompletionState.cancelled == 1 && !menuModeCompletionState.completed,
          "Explicit owner or phase cancellation clears pending rearm without claiming success");
  }

  MenuReset();
  MenuArm();
  MenuPaintOnce();
  const DWORD firstPaint = menuModeCompletionState.paintCalls;
  ++textureLifecycleState.serial;
  GdiResult = FALSE;
  QueuedCommand() = 0x8033;
  MenuQueueOnce();
  Check(menuModeCompletionState.pending && !menuModeCompletionState.completed &&
            MenuQueueCalls == 0 && QueuedCommand() == 0x8033 &&
            menuModeCompletionState.paintCalls == firstPaint + 1,
        "A ready old epoch is discarded and failed replacement paint cannot launch");
  GdiResult = TRUE;
  MenuPaintOnce();
  MenuQueueOnce();
  Check(menuModeCompletionState.completed == 1 && MenuLastCommand == 0x8033,
        "Fresh replacement epoch can complete through ordinary paint");

  MenuReset();
  MenuArm();
  reinterpret_cast<void(__thiscall *)(void *)>(MenuResumeAddress)(MenuDocument);
  QueuedCommand() = 0x8003;
  GdiResult = FALSE;
  MenuQueueOnce();
  *reinterpret_cast<DWORD *>(App + 0xf4) = reinterpret_cast<DWORD>(MenuReplacementDocument);
  MenuQueueOnce();
  Check(!menuModeCompletionState.deferredResume && menuModeCompletionState.cancelledResume == 1 &&
            MenuResumeCalls == 0 && MenuQueueCalls == 0 && QueuedCommand() == 0x8003 &&
            menuModeCompletionState.pending && MenuPresentCalls == 2,
        "Document replacement cancels stale private Resume and still gates native queued launch");
  GdiResult = TRUE;
  MenuPaintOnce();
  MenuQueueOnce();
  Check(MenuResumeCalls == 0 && MenuQueueCalls == 1 && MenuLastCommand == 0x8003 &&
            menuModeCompletionState.owner.document ==
                reinterpret_cast<DWORD>(MenuReplacementDocument),
        "Replacement document launches only after its fresh menu witness");
}

int main()
{
  try
  {
    for (DWORD base : {0x20000000u, 0x30000000u})
    {
      Game = static_cast<BYTE *>(VirtualAlloc(reinterpret_cast<void *>(base), 0x500000,
                                              MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
      Check(reinterpret_cast<DWORD>(Game) == base, "Owned renderer fixture allocation");
      LeanOwnershipCases();
      ProgressCases();
      OrderingAndRecoveryCases();
      PreservationCases();
      PhaseCases();
      SoftwareProgressCases();
      CompletionInvalidationCases();
      HardwareRecoveryGuards();
      SoftwareAfterHardwareRecovery();
      MenuProgressCases();
      MenuFailureLivenessCases();
      MenuResumeAndCommandCases();
      MenuEpochRearmCases();
      Check(VirtualFree(Game, 0, MEM_RELEASE) != FALSE, "Owned fixture release");
    }
    std::printf("{\"passed\":true,\"checks\":%u,\"mappedBases\":2,\"gameLaunched\":false}\n",
                Checks);
    return 0;
  }
  catch (const std::exception &error)
  {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
