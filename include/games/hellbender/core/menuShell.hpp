// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MENU_SHELL_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MENU_SHELL_HPP

// Notify the shell about the native fullscreen frame without changing focus,
// placement or display mode. COM interfaces are local to each notification.
extern "C"
{
  ShellState shellState = {};
}

struct ShellSnapshot
{
  DWORD app, frameObject, thread;
  HWND frame, view;
};

enum class ShellStage : DWORD
{
  none,
  kernelModule,
  loader,
  systemLibrary,
  comExports,
  initialize,
  create,
  interfaceMethods,
  hrInit,
  changedState,
  mark,
  compensate
};

static void ShellFailure(ShellStage stage, HRESULT result)
{
  ++shellState.failures;
  shellState.lastStage = static_cast<DWORD>(stage);
  shellState.lastResult = static_cast<DWORD>(result);
}

static void InvalidateShellFullscreen()
{
  ++shellState.epoch;
  ++shellState.invalidations;
}

static bool ShellFrameAlive(HWND frame, DWORD thread)
{
  return frame && thread && Call(IsWindow, isWindow)(frame) &&
         thread == Call(GetCurrentThreadId, getCurrentThreadId)() &&
         Call(GetWindowThreadProcessId, getWindowThreadProcessId)(frame, nullptr) == thread;
}

static bool ShellCapture(ShellSnapshot &snapshot)
{
  snapshot.app = Read(GameRva::app);
  if (!snapshot.app)
  {
    return false;
  }

  snapshot.frameObject = *reinterpret_cast<DWORD *>(snapshot.app + 0x1c);
  if (!snapshot.frameObject)
  {
    return false;
  }

  snapshot.frame = *reinterpret_cast<HWND *>(snapshot.frameObject + 0x1c);
  snapshot.view = ViewWindow();
  snapshot.thread = Call(GetCurrentThreadId, getCurrentThreadId)();
  return Owned(snapshot.frame, snapshot.view);
}

static bool ShellSameWindows(const ShellSnapshot &snapshot)
{
  ShellSnapshot now = {};
  return ShellCapture(now) && now.app == snapshot.app && now.frameObject == snapshot.frameObject &&
         now.thread == snapshot.thread && now.frame == snapshot.frame && now.view == snapshot.view;
}

static bool ShellFullscreen(const ShellSnapshot &snapshot)
{
  if (!ShellSameWindows(snapshot) || state.transitionDepth || state.mainMenuDepth || state.busy ||
      Read(GameRva::quit))
  {
    return false;
  }

  const DWORD mode = Read(GameRva::mode);
  if ((mode != 1 && mode != 2) ||
      !*reinterpret_cast<volatile DWORD *>(snapshot.frameObject + 0xc4) ||
      *reinterpret_cast<volatile DWORD *>(snapshot.frameObject + 0xe4) ||
      !Call(IsWindowVisible, isWindowVisible)(snapshot.frame) ||
      !Call(IsWindowVisible, isWindowVisible)(snapshot.view) ||
      !Call(IsWindowEnabled, isWindowEnabled)(snapshot.frame) ||
      !Call(IsWindowEnabled, isWindowEnabled)(snapshot.view) ||
      Call(IsIconic, isIconic)(snapshot.frame))
  {
    return false;
  }

  RECT area = {};
  const int width = Call(GetSystemMetrics, getSystemMetrics)(SM_CXSCREEN);
  const int height = Call(GetSystemMetrics, getSystemMetrics)(SM_CYSCREEN);
  return width > 0 && height > 0 && Outer(snapshot.frame, area) && area.left <= 0 &&
         area.top <= 0 && area.right >= width && area.bottom >= height;
}

static bool ShellCurrent(const ShellSnapshot &snapshot, DWORD epoch)
{
  return Configured() && shellState.epoch == epoch && ShellFullscreen(snapshot) &&
         shellState.epoch == epoch;
}

static void ShellForget()
{
  shellState.marked = 0;
  shellState.dirty = 0;
  shellState.markedFrame = nullptr;
  shellState.markedView = nullptr;
  shellState.markedThread = 0;
  shellState.markedApp = 0;
  shellState.markedFrameObject = 0;
}

static void ShellRemember(const ShellSnapshot &snapshot)
{
  shellState.marked = 1;
  shellState.markedFrame = snapshot.frame;
  shellState.markedView = snapshot.view;
  shellState.markedThread = snapshot.thread;
  shellState.markedApp = snapshot.app;
  shellState.markedFrameObject = snapshot.frameObject;
}

static ShellSnapshot ShellRemembered()
{
  return {shellState.markedApp, shellState.markedFrameObject, shellState.markedThread,
          shellState.markedFrame, shellState.markedView};
}

using ShellInitialize = HRESULT(WINAPI *)(void *, DWORD);
using ShellCreate = HRESULT(WINAPI *)(const GUID &, void *, DWORD, const GUID &, void **);
using ShellUninitialize = void(WINAPI *)();
using ShellRelease = ULONG(WINAPI *)(void *);
using ShellHrInit = HRESULT(WINAPI *)(void *);
using ShellMark = HRESULT(WINAPI *)(void *, HWND, BOOL);

struct ShellSession
{
  HMODULE module = nullptr;
  void *taskbar = nullptr;
  ShellUninitialize uninitialize = nullptr;
  ShellRelease release = nullptr;
  decltype(&::FreeLibrary) freeLibrary = nullptr;
  bool initialized = false;

  ~ShellSession()
  {
    if (taskbar && release)
    {
      release(taskbar);
    }

    if (initialized && uninitialize)
    {
      uninitialize();
    }

    if (module && freeLibrary)
    {
      freeLibrary(module);
    }
  }
};

// This call owns one complete COM lifetime. Its destructor completes before
// the caller performs the final epoch check, including cleanup-time reentry.
static bool ShellIssue(const ShellSnapshot &snapshot, BOOL fullscreen, DWORD epoch)
{
  auto getModule = Api<decltype(&::GetModuleHandleA)>(GameRva::getModuleHandleA);
  auto getProc = Api<decltype(&::GetProcAddress)>(GameRva::getProcAddress);
  auto freeLibrary = Api<decltype(&::FreeLibrary)>(GameRva::freeLibrary);

  if (!getModule || !getProc || !freeLibrary)
  {
    ShellFailure(ShellStage::loader, E_NOINTERFACE);
    return false;
  }

  const HMODULE kernel = getModule("kernel32.dll");
  if (!kernel)
  {
    ShellFailure(ShellStage::kernelModule, E_FAIL);
    return false;
  }

  auto load = reinterpret_cast<decltype(&::LoadLibraryExA)>(getProc(kernel, "LoadLibraryExA"));
  if (!load)
  {
    ShellFailure(ShellStage::loader, E_NOINTERFACE);
    return false;
  }

  ShellSession session;
  session.freeLibrary = freeLibrary;
  session.module = load("ole32.dll", nullptr, 0x00000800); // LOAD_LIBRARY_SEARCH_SYSTEM32
  if (!session.module)
  {
    ShellFailure(ShellStage::systemLibrary, E_FAIL);
    return false;
  }

  auto initialize = reinterpret_cast<ShellInitialize>(getProc(session.module, "CoInitializeEx"));
  auto create = reinterpret_cast<ShellCreate>(getProc(session.module, "CoCreateInstance"));
  session.uninitialize =
      reinterpret_cast<ShellUninitialize>(getProc(session.module, "CoUninitialize"));

  if (!initialize || !create || !session.uninitialize)
  {
    ShellFailure(ShellStage::comExports, E_NOINTERFACE);
    return false;
  }

  const HRESULT initialized = initialize(nullptr, 2); // COINIT_APARTMENTTHREADED
  session.initialized = initialized == S_OK || initialized == S_FALSE;
  if (!session.initialized && initialized != static_cast<HRESULT>(0x80010106L))
  {
    ShellFailure(ShellStage::initialize, initialized);
    return false;
  }

  if (fullscreen && !ShellCurrent(snapshot, epoch))
  {
    ++shellState.skipped;
    shellState.lastStage = static_cast<DWORD>(ShellStage::changedState);
    return false;
  }

  const GUID clsid = {0x56fdf344, 0xfd6d, 0x11d0, {0x95, 0x8a, 0x00, 0x60, 0x97, 0xc9, 0xa0, 0x90}};
  const GUID iid = {0x602d4995, 0xb13a, 0x429b, {0xa6, 0x6e, 0x19, 0x35, 0xe4, 0x4f, 0x43, 0x17}};
  const HRESULT created = create(clsid, nullptr, 1, iid, &session.taskbar); // CLSCTX_INPROC_SERVER
  if (created < 0 || !session.taskbar)
  {
    ShellFailure(ShellStage::create, created < 0 ? created : E_POINTER);
    return false;
  }

  auto table = *reinterpret_cast<void ***>(session.taskbar);
  if (!table || !table[2])
  {
    ShellFailure(ShellStage::interfaceMethods, E_POINTER);
    return false;
  }

  session.release = reinterpret_cast<ShellRelease>(table[2]);
  if (!table[3] || !table[8])
  {
    ShellFailure(ShellStage::interfaceMethods, E_NOINTERFACE);
    return false;
  }

  if (fullscreen && !ShellCurrent(snapshot, epoch))
  {
    ++shellState.skipped;
    shellState.lastStage = static_cast<DWORD>(ShellStage::changedState);
    return false;
  }

  const HRESULT ready = reinterpret_cast<ShellHrInit>(table[3])(session.taskbar);
  if (ready < 0)
  {
    ShellFailure(ShellStage::hrInit, ready);
    return false;
  }

  if ((fullscreen && !ShellCurrent(snapshot, epoch)) ||
      !ShellFrameAlive(snapshot.frame, snapshot.thread))
  {
    ++shellState.skipped;
    shellState.lastStage = static_cast<DWORD>(ShellStage::changedState);
    return false;
  }

  if (fullscreen)
  {
    ++shellState.trueAttempts;
  }
  else
  {
    ++shellState.falseAttempts;
  }

  const HRESULT result =
      reinterpret_cast<ShellMark>(table[8])(session.taskbar, snapshot.frame, fullscreen);
  shellState.lastStage = static_cast<DWORD>(ShellStage::mark);
  shellState.lastResult = static_cast<DWORD>(result);

  if (result < 0)
  {
    ++shellState.failures;
    return false;
  }

  if (fullscreen)
  {
    ++shellState.trueSuccesses;
  }
  else
  {
    ++shellState.falseSuccesses;
  }

  return true;
}

static bool ShellClearRemembered()
{
  if (!shellState.marked)
  {
    return true;
  }

  const ShellSnapshot target = ShellRemembered();
  if (!ShellFrameAlive(target.frame, target.thread))
  {
    ShellForget();
    return true;
  }

  shellState.dirty = 1;
  if (!ShellIssue(target, FALSE, shellState.epoch))
  {
    return false;
  }

  ShellForget();
  return true;
}

extern "C" void __cdecl ClearShellFullscreen()
{
  if (!Configured())
  {
    return;
  }

  ++shellState.clearCalls;
  ++shellState.epoch;
  if (shellState.busy)
  {
    ++shellState.reentries;
    return;
  }

  shellState.busy = 1;
  ShellClearRemembered();
  shellState.busy = 0;
}

static void SynchronizeShellFullscreen(bool reassert)
{
  if (!Configured())
  {
    return;
  }

  ++shellState.syncCalls;
  if (reassert)
  {
    ++shellState.reassertCalls;
  }

  if (shellState.busy)
  {
    ++shellState.epoch;
    ++shellState.reentries;
    return;
  }

  ShellSnapshot target = {};
  if (!ShellCapture(target))
  {
    ++shellState.skipped;
    return;
  }

  // A disabled modal frame is temporarily ineligible, not a windowed frame.
  // Clear only for a known completed departure from native fullscreen.
  if (!state.transitionDepth && !state.mainMenuDepth && !state.busy &&
      (Read(GameRva::quit) || Read(GameRva::mode) == 0 ||
       !*reinterpret_cast<volatile DWORD *>(target.frameObject + 0xc4)))
  {
    ClearShellFullscreen();
    return;
  }

  if (!ShellFullscreen(target))
  {
    ++shellState.skipped;
    return;
  }

  if (!reassert && shellState.marked && !shellState.dirty &&
      shellState.markedFrame == target.frame && shellState.markedView == target.view &&
      shellState.markedThread == target.thread && shellState.markedApp == target.app &&
      shellState.markedFrameObject == target.frameObject)
  {
    ++shellState.deduplicated;
    return;
  }

  shellState.busy = 1;
  const DWORD epoch = ++shellState.epoch;
  if (shellState.marked && (shellState.dirty || shellState.markedFrame != target.frame ||
                            shellState.markedThread != target.thread))
  {
    if (!ShellClearRemembered())
    {
      shellState.busy = 0;
      return;
    }
  }

  if (ShellCurrent(target, epoch))
  {
    if (ShellIssue(target, TRUE, epoch))
    {
      ShellRemember(target);
      shellState.dirty = 0;
    }
  }

  // This runs after Release, CoUninitialize and FreeLibrary. A nested complete
  // mode cycle cannot hide behind the same final handles or fullscreen flags.
  if (!ShellCurrent(target, epoch) && shellState.marked)
  {
    ++shellState.compensations;
    shellState.lastStage = static_cast<DWORD>(ShellStage::compensate);
    ShellClearRemembered();
  }

  shellState.busy = 0;
}

extern "C" void __cdecl SyncShellFullscreen()
{
  SynchronizeShellFullscreen(false);
}

static void ReassertShellFullscreen()
{
  SynchronizeShellFullscreen(true);
}

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MENU_SHELL_HPP
