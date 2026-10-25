// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_MODE_COMPLETION_FIXTURE_HPP
#define HAVGAMEPATCHER_MODE_COMPLETION_FIXTURE_HPP
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <vector>

// Compile the actual isolated payload, including its native x86 adapters.
// Only the game image, system callbacks and driver are owned fixture objects.
#include "../../../../src/games/hellbender/core/layoutPayload.cpp"

static unsigned Checks = 0, Cases = 0;
static DWORD Now = 1000, NativeCalls = 0, FlipCalls = 0, PumpCalls = 0;
static HRESULT FlipResult = S_OK;
static bool SceneSuccess = true, ClockAvailable = true;
static std::function<void()> DuringFlip, DuringClock, DuringFrame;
static BYTE App[512], Frame[512], View[512];
static DWORD FlipMethods[40], Primary[2], Backbuffer[2];
static BYTE *Game;
static DWORD PaintDcObject[4], GdiCalls = 0;
static BOOL GdiResult = 37;
static bool StretchPaint = false, SkipPaint = false;
static DWORD PaintObject = 0, GdiDestination = 0x4040, GdiSource = 0x5050;
static std::function<void()> DuringGdi, DuringPresent, DuringPaint;
static DWORD GdiCallerOffset = 0;
static DWORD GdiOperation = SRCCOPY;
static DWORD PreparedGdiSite = 0;
static bool UsePreparedGdiSite = false;
static constexpr DWORD errorIn = 0x12344321, errorNative = 0x87655678;

static void Check(bool condition, const char *message)
{
  ++Checks;
  if (!condition)
  {
    throw std::runtime_error(message);
  }
}

static DWORD &Word(DWORD rva)
{
  return *reinterpret_cast<DWORD *>(Game + rva);
}

static void Callback(std::function<void()> &action)
{
  if (action)
  {
    auto once = std::move(action);
    action = {};
    once();
  }
}

static DWORD WINAPI Clock()
{
  SetLastError(0xbad0cafe);
  Callback(DuringClock);
  return Now;
}

static HMODULE WINAPI Module(LPCSTR)
{
  return reinterpret_cast<HMODULE>(1);
}

static FARPROC WINAPI Proc(HMODULE, LPCSTR name)
{
  return ClockAvailable && std::strcmp(name, "GetTickCount") == 0
             ? reinterpret_cast<FARPROC>(&Clock)
             : nullptr;
}

static BOOL WINAPI Window(HWND)
{
  return TRUE;
}

static BOOL WINAPI Child(HWND, HWND)
{
  return TRUE;
}

static DWORD WINAPI WindowThread(HWND, DWORD *)
{
  return GetCurrentThreadId();
}

static SHORT WINAPI Key(int)
{
  return 0;
}

static __declspec(naked) BOOL NativeTranslate()
{
  __asm {
    inc NativeCalls
    mov eax,errorNative
    mov fs:[34h],eax
    mov eax,12345678h
    ret 4
  }
}

static void __cdecl NativeFrame()
{
  ++bridgeState.endCalls;
  bridgeState.lastStage = D3DBridge::end;
  bridgeState.lastResult = SceneSuccess ? S_OK : E_FAIL;
  if (!SceneSuccess)
  {
    ++bridgeState.failures;
  }
  Callback(DuringFrame);
  SetLastError(errorNative);
}

static HRESULT WINAPI NativeFlip(void *, void *, DWORD)
{
  ++FlipCalls;
  Callback(DuringFlip);
  SetLastError(errorNative);
  return FlipResult;
}

static void __cdecl NativePump()
{
  ++PumpCalls;
}

static int __cdecl NativePresent()
{
  // The pinned retail 0x26290 route discards the presentation result. Its
  // zero return is deliberately not used as evidence by the completion gate.
  ObserveFlip(Primary, nullptr, 1, config.gameBase + modeNativeFlipReturn);
  NativePump();
  return 0;
}

static void Branch(DWORD rva, DWORD target, BYTE opcode)
{
  Game[rva] = opcode;
  Word(rva + 1) = target - reinterpret_cast<DWORD>(Game + rva + 5);
  FlushInstructionCache(GetCurrentProcess(), Game + rva, 6);
}

static void Reset()
{
  ++Cases;
  std::memset(Game, 0, 0x500000);
  std::memset(&state, 0, sizeof(state));
  std::memset(&modeAdmissionState, 0, sizeof(modeAdmissionState));
  std::memset(&hardwareModeCompletionState, 0, sizeof(hardwareModeCompletionState));
  std::memset(&softwareModeCompletionState, 0, sizeof(softwareModeCompletionState));
  std::memset(&textureLifecycleState, 0, sizeof(textureLifecycleState));
  std::memset(&textureState, 0, sizeof(textureState));
  std::memset(&loadState, 0, sizeof(loadState));
  std::memset(&TextureTransfer::textureTransferStats, 0,
              sizeof(TextureTransfer::textureTransferStats));
  std::memset(&bridgeState, 0, sizeof(bridgeState));
  std::memset(&normalFrameState, 0, sizeof(normalFrameState));
  std::memset(&quadFrameRecoveryState, 0, sizeof(quadFrameRecoveryState));
  std::memset(&preMovieRecoveryState, 0, sizeof(preMovieRecoveryState));
  std::memset(&flipProbeState, 0, sizeof(flipProbeState));
  std::memset(&movieRecoveryState, 0, sizeof(movieRecoveryState));
  std::memset(&readinessState, 0, sizeof(readinessState));
  std::memset(App, 0, sizeof(App));
  std::memset(Frame, 0, sizeof(Frame));
  std::memset(View, 0, sizeof(View));
  config.magic = 0x48474c50;
  config.version = 1;
  config.gameBase = reinterpret_cast<DWORD>(Game);
  config.frameOriginal = reinterpret_cast<DWORD>(&NativeFrame);
  *reinterpret_cast<DWORD *>(App + 0x1c) = reinterpret_cast<DWORD>(Frame);
  *reinterpret_cast<DWORD *>(Frame + 0x1c) = 0x1010;
  *reinterpret_cast<DWORD *>(View + 0x1c) = 0x2020;
  Word(GameRva::app) = reinterpret_cast<DWORD>(App);
  Word(GameRva::view) = reinterpret_cast<DWORD>(View);
  Word(GameRva::getLastError) = reinterpret_cast<DWORD>(&GetLastError);
  Word(GameRva::setLastError) = reinterpret_cast<DWORD>(&SetLastError);
  Word(GameRva::getModuleHandleA) = reinterpret_cast<DWORD>(&Module);
  Word(GameRva::getProcAddress) = reinterpret_cast<DWORD>(&Proc);
  Word(GameRva::isWindow) = reinterpret_cast<DWORD>(&Window);
  Word(GameRva::isChild) = reinterpret_cast<DWORD>(&Child);
  Word(GameRva::getWindowThreadProcessId) = reinterpret_cast<DWORD>(&WindowThread);
  Word(modeNativeGetKeyState) = reinterpret_cast<DWORD>(&Key);
  Word(readinessNativeActive) = 1;
  Word(GameRva::renderBpp) = 8;
  Word(GameRva::drawObject) = 0x111000;
  Word(GameRva::legacyDevice) = 0x222000;
  Word(readinessExecuteBuffer) = 0x333000;
  Primary[0] = Backbuffer[0] = reinterpret_cast<DWORD>(FlipMethods);
  FlipMethods[11] = reinterpret_cast<DWORD>(&NativeFlip);
  Word(GameRva::primary) = reinterpret_cast<DWORD>(Primary);
  Word(GameRva::backbuffer) = reinterpret_cast<DWORD>(Backbuffer);
  state.depth = 1;
  state.blocked = 1; // Existing layout helper performs no unrelated window calls.
  textureLifecycleState.serial = textureLifecycleState.frameEpoch = 1;
  Branch(modeNativePreTranslate, reinterpret_cast<DWORD>(&NativeTranslate), 0xe9);
  Branch(readinessOuterFrameReturn - 5, reinterpret_cast<DWORD>(&FrameHook), 0xe8);
  Game[readinessOuterFrameReturn] = 0xc3;
  Now = 1000;
  NativeCalls = FlipCalls = PumpCalls = 0;
  FlipResult = S_OK;
  SceneSuccess = ClockAvailable = true;
  DuringFlip = DuringClock = DuringFrame = {};
  DuringGdi = DuringPresent = DuringPaint = {};
  GdiCalls = 0;
  GdiResult = 37;
  StretchPaint = SkipPaint = false;
  GdiDestination = 0x4040;
  GdiSource = 0x5050;
  GdiCallerOffset = 0;
  GdiOperation = SRCCOPY;
  PreparedGdiSite = 0;
  UsePreparedGdiSite = false;
  PaintObject = reinterpret_cast<DWORD>(View);
  PaintDcObject[1] = GdiDestination;
  *reinterpret_cast<DWORD *>(View) = config.gameBase + SoftwareModeCompletion::viewVtable;
}

static void EnterHardware()
{
  const DWORD token = BeginModeAdmission(App);
  Check(token != 0, "Owned transition token");
  Word(GameRva::mode) = 2;
  Word(GameRva::hardware) = Word(GameRva::directDraw) = Word(GameRva::fullscreenActive) = 1;
  Now = 1100;
  EndModeAdmission(App, token);
  Check(hardwareModeCompletionState.pending == 1, "Actual hardware transition arms completion");
}

static MSG Message(DWORD time)
{
  MSG result = {};
  result.hwnd = reinterpret_cast<HWND>(0x1010);
  result.message = WM_KEYDOWN;
  result.wParam = VK_F4;
  result.lParam = 0x003e0001;
  result.time = time;
  return result;
}

static void Input(bool consumed, DWORD time)
{
  MSG message = Message(time);
  const MSG saved = message;
  const DWORD calls = NativeCalls;
  using Translate = BOOL(__thiscall *)(void *, MSG *);
  SetLastError(errorIn);
  const BOOL value = reinterpret_cast<Translate>(&ModeAdmissionPreTranslate)(App, &message);
  Check(value == (consumed ? TRUE : 0x12345678), "Exact native or consumed return");
  Check(NativeCalls == calls + (consumed ? 0 : 1), "Native input count");
  Check(GetLastError() == (consumed ? errorIn : errorNative), "Input LastError");
  Check(std::memcmp(&message, &saved, sizeof(message)) == 0, "Input MSG remains unchanged");
}

static void FrameOnce()
{
  SetLastError(errorIn);
  reinterpret_cast<void(__cdecl *)()>(Game + readinessOuterFrameReturn - 5)();
  Check(GetLastError() == errorNative, "Frame adapter preserves native LastError");
}

static HRESULT FlipOnce()
{
  SetLastError(errorIn);
  const HRESULT value = ObserveFlip(Primary, nullptr, 1, config.gameBase + modeNativeFlipReturn);
  Check(GetLastError() == errorNative, "Flip preserves raw native LastError");
  return value;
}

static BOOL WINAPI NativeBitBlt(HDC, int, int, int, int, HDC, int, int, DWORD)
{
  ++GdiCalls;
  Callback(DuringGdi);
  SetLastError(errorNative);
  return GdiResult;
}

static BOOL WINAPI NativeStretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD)
{
  ++GdiCalls;
  Callback(DuringGdi);
  SetLastError(errorNative);
  return GdiResult;
}

static BOOL NativeGdiSite()
{
  const DWORD callsite =
      (StretchPaint ? (modeNativeStretchBltReturn - 5) : (modeNativeBitBltReturn - 5)) +
      GdiCallerOffset;
  const DWORD nativeWidth = Word(SoftwareModeCompletion::lowResolutionRva) ? 320 : 640;
  const DWORD nativeHeight = Word(SoftwareModeCompletion::lowResolutionRva) ? 200 : 480;
  std::vector<DWORD> arguments = {
      GdiDestination, 0, 0, StretchPaint ? 1280u : nativeWidth, StretchPaint ? 960u : nativeHeight,
      GdiSource,      0, 0};
  if (StretchPaint)
  {
    arguments.push_back(nativeWidth);
    arguments.push_back(nativeHeight);
  }
  arguments.push_back(GdiOperation);
  const DWORD begin = callsite - static_cast<DWORD>(arguments.size() * 5);
  DWORD cursor = begin;
  for (auto it = arguments.rbegin(); it != arguments.rend(); ++it)
  {
    Game[cursor] = 0x68;
    Word(cursor + 1) = *it;
    cursor += 5;
  }
  Branch(callsite,
         reinterpret_cast<DWORD>(StretchPaint ? reinterpret_cast<void *>(&ObserveSoftwareStretchBlt)
                                              : reinterpret_cast<void *>(&ObserveSoftwareBitBlt)),
         0xe8);
  Game[callsite + 5] = 0x90;
  Game[callsite + 6] = 0xc3;
  FlushInstructionCache(GetCurrentProcess(), Game + begin, callsite - begin + 7);
  PreparedGdiSite = reinterpret_cast<DWORD>(Game + begin);
  return reinterpret_cast<BOOL(__cdecl *)()>(PreparedGdiSite)();
}

static void __fastcall NativeSoftwareDraw(void *, void *, void *)
{
  Callback(DuringPaint);
  const BOOL result =
      UsePreparedGdiSite ? reinterpret_cast<BOOL(__cdecl *)()>(PreparedGdiSite)() : NativeGdiSite();
  Check(result == GdiResult, "Native GDI BOOL passes through exactly");
  Check(GetLastError() == errorNative, "Native GDI LastError passes through exactly");
}

static void __fastcall NativeSoftwarePresent(void *, void *)
{
  Callback(DuringPresent);
  if (!SkipPaint)
  {
    reinterpret_cast<void(__thiscall *)(void *, void *)>(&SoftwareDrawHook)(
        reinterpret_cast<void *>(PaintObject), PaintDcObject);
  }
}

static void SetupSoftware()
{
  Word(SoftwareModeCompletion::sourceDcRva) = GdiSource;
  Word(SoftwareModeCompletion::bitmapRva) = 0x6060;
  Word(SoftwareModeCompletion::bitsRva) = 0x7070;
  Word(SoftwareModeCompletion::renderBitsRva) = 0x7070;
  Word(GameRva::width) = 640;
  Word(GameRva::height) = 480;
  Word(SoftwareModeCompletion::bitBltImport) = reinterpret_cast<DWORD>(&NativeBitBlt);
  Word(SoftwareModeCompletion::stretchBltImport) = reinterpret_cast<DWORD>(&NativeStretchBlt);
  Branch(SoftwareModeCompletion::presentEntry, reinterpret_cast<DWORD>(&NativeSoftwarePresent),
         0xe9);
  Branch(SoftwareModeCompletion::drawEntry, reinterpret_cast<DWORD>(&NativeSoftwareDraw), 0xe9);
}

static void EnterSoftware(DWORD from, DWORD to)
{
  SetupSoftware();
  Word(GameRva::mode) = from;
  const DWORD token = BeginModeAdmission(App);
  Check(token != 0, "Software transition retains owned admission token");
  Word(GameRva::mode) = to;
  Word(GameRva::hardware) = Word(GameRva::directDraw) = Word(GameRva::fullscreenActive) = 0;
  Now += 100;
  EndModeAdmission(App, token);
  Check(softwareModeCompletionState.pending == 1, "Software or windowed transition arms barrier");
}

static void SoftwarePresent()
{
  SetLastError(errorIn);
  reinterpret_cast<void(__thiscall *)(void *)>(&SoftwarePresentHook)(App);
  if (!SkipPaint)
  {
    Check(GetLastError() == errorNative, "Native paint scope preserves LastError");
  }
}

#endif
