// SPDX-License-Identifier: MIT

#include "fullscreenPayload.hpp"

// This experimental image is relocated into the executable. It has no imports,
// entry point, CRT, heap allocations, worker threads or persistent settings.
extern "C"
{
  FullscreenConfig config = {0x46534650, 1};
  FullscreenState state = {};
}

struct ApiTable
{
  decltype(&::GetCurrentProcessId) getCurrentProcessId;
  decltype(&::GetCurrentThreadId) getCurrentThreadId;
  decltype(&::GetLastError) getLastError;
  decltype(&::SetLastError) setLastError;
  decltype(&::IsWindow) isWindow;
  decltype(&::GetWindowThreadProcessId) getWindowThreadProcessId;
  decltype(&::GetWindowLongA) getWindowLong;
  decltype(&::SetWindowLongA) setWindowLong;
  decltype(&::GetMenu) getMenu;
  decltype(&::SetMenu) setMenu;
  decltype(&::GetWindowPlacement) getWindowPlacement;
  decltype(&::SetWindowPlacement) setWindowPlacement;
  decltype(&::GetWindowRect) getWindowRect;
  decltype(&::GetClientRect) getClientRect;
  decltype(&::MonitorFromWindow) monitorFromWindow;
  decltype(&::GetMonitorInfoA) getMonitorInfo;
  decltype(&::SetWindowPos) setWindowPos;
  decltype(&::InvalidateRect) invalidateRect;
  decltype(&::DrawMenuBar) drawMenuBar;
  decltype(&::CheckMenuItem) checkMenuItem;
  decltype(&::PatBlt) patBlt;
  decltype(&::SetStretchBltMode) setStretchBltMode;
  decltype(&::GetParent) getParent;
  decltype(&::IsIconic) isIconic;
  decltype(&::MoveWindow) moveWindow;
  decltype(&::ScreenToClient) screenToClient;
};

using MovieCommand = DWORD(WINAPI *)(LPCSTR, LPSTR, UINT, HWND);

static ApiTable api = {};
static DWORD apiReady = 0;
static DWORD mainMenuActive = 0;
static DWORD movieResizeBusy = 0;
static MovieCommand movieCommand = nullptr;

static constexpr DWORD mainWindowRva = 0x1329cc;
static constexpr DWORD legacyFullscreenRva = 0x132464;
static constexpr DWORD joystickRawFallbackRva = 0x13cb4c;
static constexpr DWORD joystickButtonsRva = 0xebcb0;
static constexpr DWORD joystickEnabledRva = 0x13e728;
static constexpr DWORD originalJoystickPollRva = 0x14c70;
static constexpr DWORD originalPrepareMainMenuRva = 0x4fbf0;
static constexpr DWORD originalLeaveMainMenuRva = 0x4fc90;
static constexpr DWORD originalResizeMovieRva = 0x50050;
static constexpr DWORD movieOpenRva = 0x143048;
static constexpr DWORD movieWindowRva = 0x5fc7c;

static constexpr UINT fullscreenCommand = 0x9c45;
static constexpr UINT positionFlags =
    SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED;

template <typename T> static T Imported(DWORD rva)
{
  return *reinterpret_cast<T *>(config.gameBase + rva);
}

static bool Configured()
{
  return config.magic == 0x46534650 && config.version == 1 && config.gameBase != 0 &&
         config.getProcAddressIatRva != 0 && config.getModuleHandleAIatRva != 0 &&
         config.stretchBltIatRva != 0;
}

static bool Initialize()
{
  if (!Configured())
  {
    return false;
  }

  if (apiReady)
  {
    return true;
  }

  const auto getProcAddress = Imported<decltype(&::GetProcAddress)>(config.getProcAddressIatRva);
  const auto getModuleHandle =
      Imported<decltype(&::GetModuleHandleA)>(config.getModuleHandleAIatRva);

  if (!getProcAddress || !getModuleHandle)
  {
    return false;
  }

  HMODULE kernel = getModuleHandle("kernel32.dll");
  HMODULE user = getModuleHandle("user32.dll");
  HMODULE gdi = getModuleHandle("gdi32.dll");

  if (!kernel || !user || !gdi)
  {
    return false;
  }

#define Resolve(field, module, name)                                                               \
  api.field = reinterpret_cast<decltype(api.field)>(getProcAddress(module, name));                 \
  if (!api.field)                                                                                  \
  {                                                                                                \
    return false;                                                                                  \
  }

  Resolve(getCurrentProcessId, kernel, "GetCurrentProcessId")
  Resolve(getCurrentThreadId, kernel, "GetCurrentThreadId")
  Resolve(getLastError, kernel, "GetLastError")
  Resolve(setLastError, kernel, "SetLastError")

  Resolve(isWindow, user, "IsWindow")
  Resolve(getWindowThreadProcessId, user, "GetWindowThreadProcessId")
  Resolve(getWindowLong, user, "GetWindowLongA")
  Resolve(setWindowLong, user, "SetWindowLongA")
  Resolve(getMenu, user, "GetMenu")
  Resolve(setMenu, user, "SetMenu")
  Resolve(getWindowPlacement, user, "GetWindowPlacement")
  Resolve(setWindowPlacement, user, "SetWindowPlacement")
  Resolve(getWindowRect, user, "GetWindowRect")
  Resolve(getClientRect, user, "GetClientRect")
  Resolve(monitorFromWindow, user, "MonitorFromWindow")
  Resolve(getMonitorInfo, user, "GetMonitorInfoA")
  Resolve(setWindowPos, user, "SetWindowPos")
  Resolve(invalidateRect, user, "InvalidateRect")
  Resolve(drawMenuBar, user, "DrawMenuBar")
  Resolve(checkMenuItem, user, "CheckMenuItem")

  Resolve(patBlt, gdi, "PatBlt")
  Resolve(setStretchBltMode, gdi, "SetStretchBltMode")

  Resolve(getParent, user, "GetParent")
  Resolve(isIconic, user, "IsIconic")
  Resolve(moveWindow, user, "MoveWindow")
  Resolve(screenToClient, user, "ScreenToClient")

#undef Resolve

  apiReady = 1;
  return true;
}

static HWND MainWindow()
{
  return *reinterpret_cast<HWND volatile *>(config.gameBase + mainWindowRva);
}

static bool Owned(HWND window)
{
  DWORD process = 0;
  return window && api.isWindow(window) &&
         api.getWindowThreadProcessId(window, &process) == api.getCurrentThreadId() &&
         process == api.getCurrentProcessId();
}

static void Failure(DWORD stage, DWORD error = 0)
{
  ++state.failureCount;
  state.lastFailureStage = stage;
  state.lastError = error ? error : api.getLastError();

  if (!state.lastError)
  {
    state.lastError = ERROR_GEN_FAILURE;
  }
}

static bool ChangeStyle(int index, LONG value)
{
  api.setLastError(ERROR_SUCCESS);
  const LONG previous = api.setWindowLong(state.window, index, value);
  return previous != 0 || api.getLastError() == ERROR_SUCCESS;
}

static void SyncMenu()
{
  if (!state.active || state.busy || !apiReady)
  {
    return;
  }

  if (!Owned(state.window) || MainWindow() != state.window)
  {
    Failure(30, ERROR_INVALID_WINDOW_HANDLE);
    return;
  }

  const HMENU desiredMenu = mainMenuActive ? state.menu : nullptr;
  const HMENU previousMenu = api.getMenu(state.window);
  if (previousMenu == desiredMenu)
  {
    return;
  }

  state.busy = 1;
  state.stage = 30;
  bool changed = api.setMenu(state.window, desiredMenu) != FALSE;

  if (changed)
  {
    state.stage = 31;
    changed = api.setWindowPos(state.window, nullptr, 0, 0, 0, 0,
                               positionFlags | SWP_NOMOVE | SWP_NOSIZE) != FALSE;
  }

  if (changed)
  {
    state.stage = 32;
    changed = api.drawMenuBar(state.window) != FALSE && api.getMenu(state.window) == desiredMenu;
  }

  if (!changed)
  {
    Failure(state.stage);
    api.setMenu(state.window, previousMenu);
    api.setWindowPos(state.window, nullptr, 0, 0, 0, 0, positionFlags | SWP_NOMOVE | SWP_NOSIZE);
    api.drawMenuBar(state.window);
  }

  state.stage = 8;
  state.busy = 0;
}

static bool Restore()
{
  state.active = 0;
  if (!state.savedValid)
  {
    return true;
  }

  if (!Owned(state.window) || MainWindow() != state.window)
  {
    Failure(20, ERROR_INVALID_WINDOW_HANDLE);
    return false;
  }

  bool restored = true;
  state.stage = 21;
  if (!api.setMenu(state.window, state.menu))
  {
    Failure(state.stage);
    restored = false;
  }

  state.stage = 22;
  if (!ChangeStyle(GWL_STYLE, state.style))
  {
    Failure(state.stage);
    restored = false;
  }

  state.stage = 23;
  if (!ChangeStyle(GWL_EXSTYLE, state.exStyle))
  {
    Failure(state.stage);
    restored = false;
  }

  state.stage = 24;
  WINDOWPLACEMENT placement = state.placement;

  // GetWindowPlacement can report SW_SHOWNORMAL for a window that was never
  // shown. Its saved WS_VISIBLE bit is authoritative for hidden windows.
  if (!(state.style & WS_VISIBLE))
  {
    placement.showCmd = SW_HIDE;
  }

  if (!api.setWindowPlacement(state.window, &placement))
  {
    Failure(state.stage);
    restored = false;
  }

  state.stage = 25;
  if (!api.setWindowPos(state.window, nullptr, 0, 0, 0, 0, positionFlags | SWP_NOMOVE | SWP_NOSIZE))
  {
    Failure(state.stage);
    restored = false;
  }

  api.drawMenuBar(state.window);
  api.invalidateRect(state.window, nullptr, TRUE);
  if (restored)
  {
    state.savedValid = 0;
    state.stage = 0;
  }

  return restored;
}

static bool Enter(HWND window)
{
  ++state.enterAttempts;
  state.stage = 1;
  if (*reinterpret_cast<volatile DWORD *>(config.gameBase + legacyFullscreenRva) != 0)
  {
    Failure(state.stage, ERROR_INVALID_STATE);
    return false;
  }

  state.window = window;
  state.threadId = api.getCurrentThreadId();
  state.style = api.getWindowLong(window, GWL_STYLE);
  state.exStyle = api.getWindowLong(window, GWL_EXSTYLE);
  state.menu = api.getMenu(window);

  state.placement.length = sizeof(state.placement);
  state.stage = 2;

  MONITORINFO monitor = {};
  monitor.cbSize = sizeof(monitor);

  if (!api.getWindowPlacement(window, &state.placement) ||
      !api.getMonitorInfo(api.monitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
  {
    Failure(state.stage);
    return false;
  }

  const LONG width = monitor.rcMonitor.right - monitor.rcMonitor.left;
  const LONG height = monitor.rcMonitor.bottom - monitor.rcMonitor.top;
  if (width < 1 || height < 1 || width > 32768 || height > 32768)
  {
    Failure(state.stage, ERROR_INVALID_PARAMETER);
    return false;
  }

  state.monitor = monitor.rcMonitor;
  state.savedValid = 1;
  state.stage = 3;
  const HMENU desiredMenu = mainMenuActive ? state.menu : nullptr;
  bool entered = api.setMenu(window, desiredMenu) != FALSE;

  if (entered)
  {
    state.stage = 4;
    entered = ChangeStyle(
        GWL_STYLE, (state.style & ~(WS_OVERLAPPEDWINDOW | WS_MINIMIZE | WS_MAXIMIZE)) | WS_POPUP);
  }

  if (entered)
  {
    state.stage = 5;
    entered = ChangeStyle(GWL_EXSTYLE, state.exStyle & ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE |
                                                         WS_EX_STATICEDGE | WS_EX_WINDOWEDGE));
  }

  if (entered)
  {
    state.stage = 6;
    entered = api.setWindowPos(window, nullptr, monitor.rcMonitor.left, monitor.rcMonitor.top,
                               width, height, positionFlags) != FALSE;
  }

  if (entered)
  {
    state.stage = 7;
    RECT actual = {};
    entered = api.getWindowRect(window, &actual) && actual.left == monitor.rcMonitor.left &&
              actual.top == monitor.rcMonitor.top && actual.right == monitor.rcMonitor.right &&
              actual.bottom == monitor.rcMonitor.bottom && api.getMenu(window) == desiredMenu;
  }

  if (!entered)
  {
    Failure(state.stage);
    Restore();
    return false;
  }

  state.active = 1;
  state.stage = 8;
  ++state.enterSuccesses;
  api.invalidateRect(window, nullptr, TRUE);

  return true;
}

extern "C" void __cdecl ToggleFullscreen()
{
  if (state.busy || !Initialize())
  {
    return;
  }

  HWND window = MainWindow();
  if (!Owned(window))
  {
    Failure(1, ERROR_INVALID_WINDOW_HANDLE);
    return;
  }

  state.busy = 1;
  if (state.savedValid)
  {
    ++state.leaveAttempts;
    if (Restore())
    {
      ++state.leaveSuccesses;
    }
  }
  else
  {
    Enter(window);
  }

  state.busy = 0;
  SyncMenu();
}

extern "C" void __cdecl PrepareMainMenu()
{
  if (!Configured())
  {
    return;
  }

  mainMenuActive = 1;
  SyncMenu();
  reinterpret_cast<void(__cdecl *)()>(config.gameBase + originalPrepareMainMenuRva)();
}

extern "C" void __cdecl LeaveMainMenu()
{
  if (!Configured())
  {
    return;
  }

  mainMenuActive = 0;
  SyncMenu();
  reinterpret_cast<void(__cdecl *)()>(config.gameBase + originalLeaveMainMenuRva)();
}

static bool MovieWindowOwned(HWND parent, HWND child)
{
  DWORD process = 0;
  return child && api.isWindow(child) && api.getWindowThreadProcessId(child, &process) != 0 &&
         process == api.getCurrentProcessId() && api.getParent(child) == parent &&
         (api.getWindowLong(child, GWL_STYLE) & WS_CHILD) != 0;
}

static bool MovieSession(HWND parent, HWND child)
{
  return *reinterpret_cast<const DWORD *>(config.gameBase + movieOpenRva) != 0 &&
         *reinterpret_cast<const HWND *>(config.gameBase + movieWindowRva) == child &&
         MainWindow() == parent && Owned(parent) && MovieWindowOwned(parent, child);
}

static bool MovieSourceValid()
{
  char reply[128];
  reply[0] = '\0';
  const DWORD error = movieCommand("where mov source", reply, sizeof(reply), nullptr);
  reply[sizeof(reply) - 1] = '\0';

  if (error != 0)
  {
    return false;
  }

  // The original sizing routine assumes four space-separated numbers and does
  // not check MCI failures. Validate that format before using it as a fallback.
  const char *cursor = reply;

  for (unsigned int field = 0; field < 4; ++field)
  {
    unsigned int value = 0;
    unsigned int digits = 0;
    while (*cursor >= '0' && *cursor <= '9')
    {
      value = value * 10 + static_cast<unsigned int>(*cursor - '0');
      if (value > 32768)
      {
        return false;
      }

      ++cursor;
      ++digits;
    }

    if (!digits || (field >= 2 && value == 0))
    {
      return false;
    }

    if (field < 3)
    {
      if (*cursor != ' ')
      {
        return false;
      }

      ++cursor;
    }
  }

  return *cursor == '\0';
}

extern "C" void __cdecl ResizeMovie(HWND parent)
{
  if (movieResizeBusy || !Configured() ||
      *reinterpret_cast<const DWORD *>(config.gameBase + movieOpenRva) == 0 || !Initialize())
  {
    return;
  }

  const HWND child = *reinterpret_cast<const HWND *>(config.gameBase + movieWindowRva);
  if (!MovieSession(parent, child) || api.isIconic(parent))
  {
    return;
  }

  RECT client = {};
  RECT previous = {};
  if (!api.getClientRect(parent, &client) || !api.getWindowRect(child, &previous))
  {
    return;
  }

  const LONG width = client.right - client.left;
  const LONG height = client.bottom - client.top;
  if (width < 1 || height < 1 || width > 32768 || height > 32768)
  {
    return;
  }

  POINT origin = {previous.left, previous.top};
  if (!api.screenToClient(parent, &origin))
  {
    return;
  }

  if (!movieCommand)
  {
    const auto getProcAddress = Imported<decltype(&::GetProcAddress)>(config.getProcAddressIatRva);
    const auto getModuleHandle =
        Imported<decltype(&::GetModuleHandleA)>(config.getModuleHandleAIatRva);
    const HMODULE winmm = getModuleHandle("winmm.dll");

    if (!winmm)
    {
      return;
    }

    movieCommand = reinterpret_cast<MovieCommand>(getProcAddress(winmm, "mciSendStringA"));
    if (!movieCommand)
    {
      return;
    }
  }

  // WM_SIZE also arrives during fullscreen transitions, so this guard is
  // independent of the fullscreen transition's state.busy flag.
  movieResizeBusy = 1;
  const BOOL moved = api.moveWindow(child, 0, 0, width, height, TRUE);
  const DWORD moveError = moved ? 0 : api.getLastError();

  if (MovieSession(parent, child))
  {
    const DWORD commandError = moved ? movieCommand("put mov destination", nullptr, 0, nullptr) : 0;
    if (!moved || commandError)
    {
      Failure(moved ? 41 : 40, moved ? commandError : moveError);
      if (MovieSession(parent, child))
      {
        api.moveWindow(child, origin.x, origin.y, previous.right - previous.left,
                       previous.bottom - previous.top, TRUE);
        if (MovieSourceValid() && MovieSession(parent, child) && !api.isIconic(parent))
        {
          reinterpret_cast<void(__cdecl *)(HWND)>(config.gameBase + originalResizeMovieRva)(parent);
        }
      }
    }
  }

  movieResizeBusy = 0;
}

extern "C" void __cdecl PollJoystick()
{
  if (!Configured())
  {
    return;
  }

  const DWORD gameBase = config.gameBase;
  if (*reinterpret_cast<const DWORD *>(gameBase + joystickRawFallbackRva) != 0)
  {
    // The DOS game-port fallback uses privileged I/O after WinMM rejects the device
    *reinterpret_cast<DWORD *>(gameBase + joystickButtonsRva) = 0;
    *reinterpret_cast<DWORD *>(gameBase + joystickEnabledRva) = 0;
    return;
  }

  // Call the original function directly, bypassing its patched entry thunk.
  reinterpret_cast<void(__cdecl *)()>(gameBase + originalJoystickPollRva)();
}

extern "C" BOOL WINAPI StretchFrame(HDC destination,
                                    int x,
                                    int y,
                                    int width,
                                    int height,
                                    HDC source,
                                    int sourceX,
                                    int sourceY,
                                    int sourceWidth,
                                    int sourceHeight,
                                    DWORD operation)
{
  if (!Configured())
  {
    return FALSE;
  }

  const auto stretchBlt = Imported<decltype(&::StretchBlt)>(config.stretchBltIatRva);
  if (!stretchBlt)
  {
    return FALSE;
  }

  if (!state.active || state.busy || !apiReady || MainWindow() != state.window ||
      !Owned(state.window))
  {
    return stretchBlt(destination, x, y, width, height, source, sourceX, sourceY, sourceWidth,
                      sourceHeight, operation);
  }

  RECT client = {};
  if (!api.getClientRect(state.window, &client))
  {
    return stretchBlt(destination, x, y, width, height, source, sourceX, sourceY, sourceWidth,
                      sourceHeight, operation);
  }

  const LONG clientWidth = client.right - client.left;
  const LONG clientHeight = client.bottom - client.top;
  if (clientWidth < 1 || clientHeight < 1 || clientWidth > 32768 || clientHeight > 32768)
  {
    return stretchBlt(destination, x, y, width, height, source, sourceX, sourceY, sourceWidth,
                      sourceHeight, operation);
  }

  const int oldMode = api.setStretchBltMode(destination, COLORONCOLOR);
  const BOOL result = stretchBlt(destination, 0, 0, clientWidth, clientHeight, source, sourceX,
                                 sourceY, sourceWidth, sourceHeight, operation);
  const DWORD lastError = api.getLastError();

  if (oldMode)
  {
    api.setStretchBltMode(destination, oldMode);
  }

  api.setLastError(lastError);
  ++state.frames;
  return result;
}

extern "C" DWORD WINAPI UpdateMenuCheck(HMENU menu, UINT item, UINT flags)
{
  if (!Initialize())
  {
    return static_cast<DWORD>(-1);
  }

  if (item == fullscreenCommand && !(flags & MF_BYPOSITION))
  {
    flags = (flags & ~MF_CHECKED) | (state.active ? MF_CHECKED : MF_UNCHECKED);
  }

  return api.checkMenuItem(menu, item, flags);
}
