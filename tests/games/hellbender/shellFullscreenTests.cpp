// SPDX-License-Identifier: MIT

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shobjidl.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include "layoutPayload.hpp"

using Bytes = std::vector<unsigned char>;

static void Require(bool value, const char *message)
{
  if (!value)
  {
    throw std::runtime_error(message);
  }
}

#include "../../support/mappedImage.hpp"

static unsigned Checks = 0;

static void Pass(const std::string &name)
{
  ++Checks;
  std::printf("PASS %s\n", name.c_str());
  std::fflush(stdout);
}

using Callback = void(__cdecl *)();
static_assert(sizeof(ShellState) == 96, "Shell state ABI mismatch.");

struct Payload
{
  HMODULE module = nullptr;
  BYTE *image = nullptr;
  DWORD origin = 0;
  PayloadConfig *config = nullptr;
  PayloadState *state = nullptr;
  FocusState *focus = nullptr;
  DWORD *texture = nullptr;
  ShellState *shell = nullptr;
  Callback sync = nullptr, clear = nullptr, beforeMain = nullptr, afterMain = nullptr;
  Callback beforeMode = nullptr, afterMode = nullptr, mainHook = nullptr;
  void *modeHook = nullptr;

  template <class T> T Export(const char *name)
  {
    auto address = GetProcAddress(module, name);
    Require(address != nullptr, "Payload export missing.");

    if (image)
    {
      address = reinterpret_cast<FARPROC>(
          image + origin + (reinterpret_cast<DWORD>(address) - reinterpret_cast<DWORD>(module)));
    }

    return reinterpret_cast<T>(address);
  }

  Payload(HMODULE loaded, BYTE *mapped = nullptr, DWORD offset = 0)
      : module(loaded), image(mapped), origin(offset)
  {
    config = Export<PayloadConfig *>("config");
    state = Export<PayloadState *>("state");
    focus = Export<FocusState *>("focusState");
    texture = Export<DWORD *>("textureState");
    shell = Export<ShellState *>("shellState");
    sync = Export<Callback>("syncShellFullscreen");
    clear = Export<Callback>("clearShellFullscreen");
    beforeMain = Export<Callback>("beforeMainMenuShow");
    afterMain = Export<Callback>("afterMainMenuShow");
    beforeMode = Export<Callback>("beforeTransition");
    afterMode = Export<Callback>("afterTransition");
    mainHook = Export<Callback>("mainMenuShowHook");
    modeHook = Export<void *>("modeHook");
    Require(config->magic == 0x48474c50 && config->version == 1, "Payload ABI mismatch.");

    if (image)
    {
      Require(config->gameBase == reinterpret_cast<DWORD>(image),
              "Embedded gameBase did not relocate.");
    }
  }
};

enum class Stage
{
  None,
  Load,
  Initialize,
  Create,
  HrInit,
  Mark,
  Release,
  Uninitialize,
  Free
};
enum class Change
{
  None,
  ReenterSync,
  BeginTransition,
  BecomeWindowed,
  ReplaceView,
  DestroyFrame
};

struct Fixture;
static Fixture *Current = nullptr;
static HMODULE WINAPI FakeModule(LPCSTR);
static FARPROC WINAPI FakeProcedure(HMODULE, LPCSTR);
static HMODULE WINAPI FakeLoad(LPCSTR, HANDLE, DWORD);
static BOOL WINAPI FakeFree(HMODULE);
static HRESULT WINAPI FakeInitialize(LPVOID, DWORD);
static void WINAPI FakeUninitialize();
static HRESULT WINAPI FakeCreate(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *);
static HRESULT WINAPI FakeHrInit(void *);
static ULONG WINAPI FakeRelease(void *);
static HRESULT WINAPI FakeMark(void *, HWND, BOOL);
static int WINAPI FakeMetrics(int);
static BOOL WINAPI FakeVisible(HWND);
static BOOL WINAPI FakeEnabled(HWND);
static BOOL WINAPI FakeIconic(HWND);
static HWND WINAPI FakeForeground();
static HWND WINAPI FakeFocus();
static HWND WINAPI ForbiddenFocus(HWND);
static BOOL WINAPI ForbiddenPosition(HWND, HWND, int, int, int, int, UINT);
static BOOL WINAPI ForbiddenMenu(HWND, HMENU);
static DWORD WINAPI WrongThread(HWND, LPDWORD);
static void __cdecl OriginalMain();
static void __stdcall OriginalMode(int);

struct Fixture
{
  Payload &payload;
  BYTE *game = nullptr;
  bool ownsGame = false;
  HWND frame = nullptr, view = nullptr, alternate = nullptr;
  HMENU menu = nullptr;
  void *methods[9]{};
  void **object = methods;
  HRESULT initializeResult = S_OK, createResult = S_OK, hrResult = S_OK, markResult = S_OK;
  bool moduleMissing = false, loadMissing = false, loadFailure = false;
  bool initMissing = false, createMissing = false, uninitMissing = false;
  bool visible = true, enabled = true, iconic = false;
  HWND foreground = nullptr;
  Stage changeAt = Stage::None;
  Change change = Change::None;
  bool changed = false;
  unsigned loads = 0, frees = 0, initializes = 0, creates = 0, hrs = 0, releases = 0,
           uninitializes = 0;
  unsigned mutations = 0, originalMainCalls = 0, originalModeCalls = 0;
  std::vector<BOOL> markers;

  DWORD &Word(DWORD rva)
  {
    return *reinterpret_cast<DWORD *>(game + rva);
  }

  template <class T> void Api(DWORD rva, T value)
  {
    Word(rva) = reinterpret_cast<DWORD>(value);
  }

  explicit Fixture(Payload &value) : payload(value)
  {
    if (payload.image)
    {
      game = payload.image;
    }
    else
    {
      game = static_cast<BYTE *>(
          VirtualAlloc(nullptr, 0x500000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
      ownsGame = true;
    }

    Require(game != nullptr, "Synthetic game allocation failed.");
    frame =
        CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Owned hidden shell marker fixture", WS_POPUP,
                        0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    view = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 640, 480, frame, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
    alternate = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 640, 480, frame, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    menu = CreateMenu();
    Require(frame && view && alternate && menu && AppendMenuW(menu, MF_STRING, 1, L"Fixture"),
            "Owned window creation failed.");
    Require(SetMenu(frame, menu) != FALSE, "Owned menu assignment failed.");
    methods[2] = reinterpret_cast<void *>(&FakeRelease);
    methods[3] = reinterpret_cast<void *>(&FakeHrInit);
    methods[8] = reinterpret_cast<void *>(&FakeMark);
    Reset();
  }

  ~Fixture()
  {
    if (frame && IsWindow(frame))
    {
      SetMenu(frame, nullptr);
      DestroyWindow(frame);
    }

    if (menu)
    {
      DestroyMenu(menu);
    }

    if (ownsGame && game)
    {
      VirtualFree(game, 0, MEM_RELEASE);
    }

    Current = nullptr;
  }

  void Reset();

  void At(Stage value)
  {
    SetLastError(0x12345678);

    if (changed || value != changeAt)
    {
      return;
    }

    changed = true;

    switch (change)
    {
    case Change::ReenterSync:
      payload.sync();
      break;

    case Change::BeginTransition:
      payload.beforeMode();
      break;

    case Change::BecomeWindowed:
      Word(0x200100 + 0xc4) = 0;
      Word(GameRva::mode) = 0;
      payload.sync();
      break;

    case Change::ReplaceView:
      Word(0x200200 + 0x1c) = reinterpret_cast<DWORD>(alternate);
      payload.sync();
      break;

    case Change::DestroyFrame:
      DestroyWindow(frame);
      payload.sync();
      break;

    default:
      break;
    }
  }

  void CheckBalance()
  {
    Require(loads == frees, "Loaded module reference leaked.");
    Require(releases == (SUCCEEDED(createResult) ? creates : 0), "COM object reference leaked.");
    Require(uninitializes == (SUCCEEDED(initializeResult) ? initializes : 0),
            "COM initialization balance mismatch.");
    Require(mutations == 0, "Shell helper attempted a window mutation.");
  }
};

static HMODULE WINAPI FakeModule(LPCSTR name)
{
  Require(!strcmp(name, "kernel32.dll"), "Unexpected module name.");
  return Current->moduleMissing ? nullptr : reinterpret_cast<HMODULE>(0x11110000);
}

static DWORD WINAPI FakeTick()
{
  return 1000;
}

static FARPROC WINAPI FakeProcedure(HMODULE module, LPCSTR name)
{
  if (module == reinterpret_cast<HMODULE>(0x11110000) && !strcmp(name, "LoadLibraryExA"))
  {
    return Current->loadMissing ? nullptr : reinterpret_cast<FARPROC>(&FakeLoad);
  }

  if (module == reinterpret_cast<HMODULE>(0x11110000) && !strcmp(name, "GetTickCount"))
  {
    return reinterpret_cast<FARPROC>(&FakeTick);
  }

  Require(module == reinterpret_cast<HMODULE>(0x22220000), "Unexpected COM module.");

  if (!strcmp(name, "CoInitializeEx"))
  {
    return Current->initMissing ? nullptr : reinterpret_cast<FARPROC>(&FakeInitialize);
  }

  if (!strcmp(name, "CoCreateInstance"))
  {
    return Current->createMissing ? nullptr : reinterpret_cast<FARPROC>(&FakeCreate);
  }

  if (!strcmp(name, "CoUninitialize"))
  {
    return Current->uninitMissing ? nullptr : reinterpret_cast<FARPROC>(&FakeUninitialize);
  }

  throw std::runtime_error("Unexpected dynamic API.");
}

static HMODULE WINAPI FakeLoad(LPCSTR name, HANDLE file, DWORD flags)
{
  Require(!strcmp(name, "ole32.dll") && !file && flags == LOAD_LIBRARY_SEARCH_SYSTEM32,
          "Unsafe COM module search.");
  Current->At(Stage::Load);

  if (Current->loadFailure)
  {
    return nullptr;
  }

  ++Current->loads;
  return reinterpret_cast<HMODULE>(0x22220000);
}

static BOOL WINAPI FakeFree(HMODULE module)
{
  Require(module == reinterpret_cast<HMODULE>(0x22220000), "Wrong module released.");
  ++Current->frees;
  Current->At(Stage::Free);

  return TRUE;
}

static HRESULT WINAPI FakeInitialize(LPVOID reserved, DWORD flags)
{
  Require(!reserved && flags == COINIT_APARTMENTTHREADED, "Unexpected COM initialization request.");
  ++Current->initializes;
  Current->At(Stage::Initialize);

  return Current->initializeResult;
}

static void WINAPI FakeUninitialize()
{
  ++Current->uninitializes;
  Current->At(Stage::Uninitialize);
}

static HRESULT WINAPI
FakeCreate(REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID iid, LPVOID *output)
{
  Require(IsEqualGUID(clsid, __uuidof(TaskbarList)) && IsEqualGUID(iid, __uuidof(ITaskbarList2)) &&
              !outer && context == CLSCTX_INPROC_SERVER,
          "Unexpected taskbar COM identity.");
  ++Current->creates;
  Current->At(Stage::Create);
  *output = SUCCEEDED(Current->createResult) ? &Current->object : nullptr;

  return Current->createResult;
}

static HRESULT WINAPI FakeHrInit(void *object)
{
  Require(object == &Current->object, "Unexpected taskbar object.");
  ++Current->hrs;
  Current->At(Stage::HrInit);

  return Current->hrResult;
}

static ULONG WINAPI FakeRelease(void *object)
{
  Require(object == &Current->object, "Wrong COM object released.");
  ++Current->releases;
  Current->At(Stage::Release);

  return 0;
}

static HRESULT WINAPI FakeMark(void *object, HWND window, BOOL fullscreen)
{
  Require(object == &Current->object && window == Current->frame, "Wrong taskbar marker target.");
  Current->markers.push_back(fullscreen);
  Current->At(Stage::Mark);

  return Current->markResult;
}

static int WINAPI FakeMetrics(int index)
{
  if (index == SM_CXSCREEN)
  {
    return 640;
  }

  if (index == SM_CYSCREEN)
  {
    return 480;
  }

  return GetSystemMetrics(index);
}

static BOOL WINAPI FakeVisible(HWND)
{
  return Current->visible;
}

static BOOL WINAPI FakeEnabled(HWND)
{
  return Current->enabled;
}

static BOOL WINAPI FakeIconic(HWND)
{
  return Current->iconic;
}

static HWND WINAPI FakeForeground()
{
  return Current->foreground;
}

static HWND WINAPI FakeFocus()
{
  return Current->view;
}

static HWND WINAPI ForbiddenFocus(HWND)
{
  ++Current->mutations;
  return nullptr;
}

static BOOL WINAPI ForbiddenPosition(HWND, HWND, int, int, int, int, UINT)
{
  ++Current->mutations;
  return FALSE;
}

static BOOL WINAPI ForbiddenMenu(HWND, HMENU)
{
  ++Current->mutations;
  return FALSE;
}

static void __cdecl OriginalMain()
{
  ++Current->originalMainCalls;
  Current->Word(GameRva::mainMenuVisible) = 1;
}

static void __stdcall OriginalMode(int mode)
{
  ++Current->originalModeCalls;
  Current->Word(GameRva::mode) = static_cast<DWORD>(mode);
  Current->Word(0x200100 + 0xc4) = mode != 0;
}

static DWORD WINAPI WrongThread(HWND, LPDWORD process)
{
  if (process)
  {
    *process = GetCurrentProcessId();
  }

  return GetCurrentThreadId() + 1;
}

void Fixture::Reset()
{
  Current = this;
  ZeroMemory(payload.state, sizeof(PayloadState));
  ZeroMemory(payload.focus, sizeof(FocusState));
  ZeroMemory(payload.texture, 4160);
  ZeroMemory(payload.shell, sizeof(ShellState));
  ZeroMemory(game + 0x200000, 0x400);
  payload.config->gameBase = reinterpret_cast<DWORD>(game);
  payload.config->mainMenuShowResume = reinterpret_cast<DWORD>(&OriginalMain);
  payload.config->modeResume = reinterpret_cast<DWORD>(&OriginalMode);
  Word(GameRva::app) = reinterpret_cast<DWORD>(game + 0x200000);
  Word(0x200000 + 0x1c) = reinterpret_cast<DWORD>(game + 0x200100);
  Word(0x200100 + 0x1c) = reinterpret_cast<DWORD>(frame);
  Word(0x200100 + 0xc4) = 1;
  Word(0x200100 + 0xe4) = 0;
  Word(GameRva::view) = reinterpret_cast<DWORD>(game + 0x200200);
  Word(0x200200 + 0x1c) = reinterpret_cast<DWORD>(view);
  Word(GameRva::mode) = 1;
  Word(GameRva::mainMenuVisible) = 1;
  Word(GameRva::quit) = Word(GameRva::gameStage) = Word(GameRva::legacyVideo) = 0;
  Word(GameRva::directDraw) = Word(GameRva::hardware) = Word(GameRva::fullscreenActive) = 0;
  Word(GameRva::width) = 640;
  Word(GameRva::height) = 480;
  foreground = frame;
  Api(GameRva::getLastError, &GetLastError);
  Api(GameRva::setLastError, &SetLastError);
  Api(GameRva::getCurrentThreadId, &GetCurrentThreadId);
  Api(GameRva::getWindowThreadProcessId, &GetWindowThreadProcessId);
  Api(GameRva::clientToScreen, &ClientToScreen);
  Api(GameRva::getClientRect, &GetClientRect);
  Api(GameRva::getWindowRect, &GetWindowRect);
  Api(GameRva::getParent, &GetParent);
  Api(GameRva::getMenu, &GetMenu);
  Api(GameRva::isWindow, &IsWindow);
  Api(GameRva::isChild, &IsChild);
  Api(GameRva::getSystemMetrics, &FakeMetrics);
  Api(GameRva::isWindowVisible, &FakeVisible);
  Api(GameRva::isWindowEnabled, &FakeEnabled);
  Api(GameRva::isIconic, &FakeIconic);
  Api(GameRva::getForegroundWindow, &FakeForeground);
  Api(GameRva::getFocus, &FakeFocus);
  Api(GameRva::setFocus, &ForbiddenFocus);
  Api(GameRva::setWindowPos, &ForbiddenPosition);
  Api(GameRva::setMenu, &ForbiddenMenu);
  Api(GameRva::getModuleHandleA, &FakeModule);
  Api(GameRva::getProcAddress, &FakeProcedure);
  Api(GameRva::freeLibrary, &FakeFree);
}

static void ExpectMarkers(const Fixture &fixture, std::initializer_list<BOOL> expected)
{
  Require(fixture.markers.size() == expected.size(), "Unexpected fullscreen marker call count.");
  size_t index = 0;

  for (BOOL value : expected)
  {
    Require(fixture.markers[index++] == value, "Unexpected fullscreen marker value.");
  }
}

static void Case(Payload &payload,
                 const std::string &context,
                 const std::string &name,
                 const std::function<void(Fixture &)> &test)
{
  Fixture fixture(payload);
  RECT before{}, after{};
  Require(GetWindowRect(fixture.frame, &before) != FALSE, "Cannot read owned window geometry.");
  const auto style = GetWindowLongW(fixture.frame, GWL_STYLE);
  const auto extended = GetWindowLongW(fixture.frame, GWL_EXSTYLE);
  test(fixture);
  fixture.CheckBalance();
  Require(payload.shell->busy == 0, "Shell helper remained busy.");
  Require(GetForegroundWindow() != fixture.frame, "Owned hidden frame became foreground.");

  if (IsWindow(fixture.frame))
  {
    Require(GetWindowRect(fixture.frame, &after) != FALSE && EqualRect(&before, &after) &&
                GetWindowLongW(fixture.frame, GWL_STYLE) == style &&
                GetWindowLongW(fixture.frame, GWL_EXSTYLE) == extended &&
                GetMenu(fixture.frame) == fixture.menu,
            "Shell helper changed owned geometry, styles or menu.");
  }

  Pass(context + "/" + name);
}

static void RunCases(Payload &payload, const std::string &context)
{
  Case(payload, context, "software-fullscreen-mark-and-deduplicate", [&](Fixture &f)
  {
    payload.sync();
    payload.sync();
    ExpectMarkers(f, {TRUE});
    Require(payload.shell->marked && payload.shell->deduplicated == 1,
            "Identical marker was not deduplicated.");
  });
  Case(payload, context, "hardware-fullscreen", [&](Fixture &f)
  {
    f.Word(GameRva::mode) = 2;
    f.Word(GameRva::directDraw) = f.Word(GameRva::hardware) = f.Word(GameRva::fullscreenActive) = 1;
    payload.sync();
    ExpectMarkers(f, {TRUE});
  });
  Case(payload, context, "windowed-clears-previous-marker", [&](Fixture &f)
  {
    payload.sync();
    f.Word(GameRva::mode) = 0;
    f.Word(0x200100 + 0xc4) = 0;
    payload.sync();
    payload.sync();
    ExpectMarkers(f, {TRUE, FALSE});
    Require(!payload.shell->marked, "Windowed marker retained.");
  });
  Case(payload, context, "disabled-modal-frame-retains-valid-marker", [&](Fixture &f)
  {
    payload.sync();
    f.enabled = false;
    payload.sync();
    ExpectMarkers(f, {TRUE});
    Require(payload.shell->marked, "Disabled modal frame unnecessarily cleared its marker.");
  });
  Case(payload, context, "foreign-foreground-not-activated", [&](Fixture &f)
  {
    f.foreground = nullptr;
    payload.sync();
    ExpectMarkers(f, {TRUE});
    // MarkFullscreenWindow is shell metadata. It may describe an inactive window without activating
    // it.
  });
  const std::vector<std::pair<std::string, std::function<void(Fixture &)>>> guards = {
      {"windowed",
       [](Fixture &f)
  {
    f.Word(GameRva::mode) = 0;
  }},
      {"invalid-mode",
       [](Fixture &f)
  {
    f.Word(GameRva::mode) = 3;
  }},
      {"native-fullscreen-off",
       [](Fixture &f)
  {
    f.Word(0x200100 + 0xc4) = 0;
  }},
      {"activation-suppressed",
       [](Fixture &f)
  {
    f.Word(0x200100 + 0xe4) = 1;
  }},
      {"hidden",
       [](Fixture &f)
  {
    f.visible = false;
  }},
      {"disabled",
       [](Fixture &f)
  {
    f.enabled = false;
  }},
      {"minimized",
       [](Fixture &f)
  {
    f.iconic = true;
  }},
      {"quit",
       [](Fixture &f)
  {
    f.Word(GameRva::quit) = 1;
  }},
      {"transition",
       [](Fixture &f)
  {
    f.payload.state->transitionDepth = 1;
  }},
      {"main-menu-constructing",
       [](Fixture &f)
  {
    f.payload.state->mainMenuDepth = 1;
  }},
      {"layout-busy",
       [](Fixture &f)
  {
    f.payload.state->busy = 1;
  }},
      {"missing-app",
       [](Fixture &f)
  {
    f.Word(GameRva::app) = 0;
  }},
      {"missing-frame-object",
       [](Fixture &f)
  {
    f.Word(0x200000 + 0x1c) = 0;
  }},
      {"view-is-frame",
       [](Fixture &f)
  {
    f.Word(0x200200 + 0x1c) = reinterpret_cast<DWORD>(f.frame);
  }},
      {"wrong-thread", [](Fixture &f)
  {
    f.Api(GameRva::getWindowThreadProcessId, &WrongThread);
  }}};

  for (const auto &guard : guards)
  {
    Case(payload, context, "guard-" + guard.first, [&](Fixture &f)
    {
      guard.second(f);
      payload.sync();
      ExpectMarkers(f, {});
      Require(f.loads == 0, "Ineligible frame opened COM.");
    });
  }

  Case(payload, context, "insufficient-display-coverage", [&](Fixture &f)
  {
    Require(SetWindowPos(f.frame, nullptr, 0, 0, 639, 480, SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
            "Fixture resize failed.");
    payload.sync();
    ExpectMarkers(f, {});
    Require(SetWindowPos(f.frame, nullptr, 0, 0, 640, 480, SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
            "Fixture size restore failed.");
  });

  for (HRESULT result : {S_OK, S_FALSE, RPC_E_CHANGED_MODE, E_FAIL})
  {
    Case(payload, context, "initialize-" + std::to_string(static_cast<DWORD>(result)),
         [&](Fixture &f)
    {
      f.initializeResult = result;
      payload.sync();

      if (result == E_FAIL)
      {
        ExpectMarkers(f, {});
        Require(f.creates == 0, "Create followed failed initialization.");
      }
      else
      {
        ExpectMarkers(f, {TRUE});
      }
    });
  }

  for (int failure = 0; failure < 11; ++failure)
  {
    Case(payload, context, "API-failure-" + std::to_string(failure), [&](Fixture &f)
    {
      switch (failure)
      {
      case 0:
        f.moduleMissing = true;
        break;

      case 1:
        f.loadMissing = true;
        break;

      case 2:
        f.loadFailure = true;
        break;

      case 3:
        f.initMissing = true;
        break;

      case 4:
        f.createMissing = true;
        break;

      case 5:
        f.uninitMissing = true;
        break;

      case 6:
        f.createResult = E_FAIL;
        break;

      case 7:
        f.hrResult = E_FAIL;
        break;

      case 8:
        f.Word(GameRva::getModuleHandleA) = 0;
        break;

      case 9:
        f.Word(GameRva::getProcAddress) = 0;
        break;

      case 10:
        f.Word(GameRva::freeLibrary) = 0;
        break;
      }

      payload.sync();
      ExpectMarkers(f, {});
      Require(!payload.shell->marked && payload.shell->failures > 0,
              "Failed API operation was not recorded.");
    });
  }

  Case(payload, context, "failed-mark-not-remembered", [&](Fixture &f)
  {
    f.markResult = E_FAIL;
    payload.sync();
    ExpectMarkers(f, {TRUE});
    Require(!payload.shell->marked, "Failed TRUE was remembered as successful.");
  });
  Case(payload, context, "failed-clear-retained-for-bounded-retry", [&](Fixture &f)
  {
    payload.sync();
    f.markResult = E_FAIL;
    payload.clear();
    Require(payload.shell->marked && payload.shell->dirty, "Failed FALSE was forgotten.");
    f.markResult = S_OK;
    payload.clear();
    ExpectMarkers(f, {TRUE, FALSE, FALSE});
    Require(!payload.shell->marked, "Successful retry did not clear marker state.");
  });
  Case(payload, context, "actual-main-menu-wrapper-reasserts-and-preserves-last-error",
       [&](Fixture &f)
  {
    SetLastError(0x76543210);
    payload.mainHook();
    Require(GetLastError() == 0x76543210, "Main-menu boundary changed LastError.");
    payload.mainHook();
    ExpectMarkers(f, {TRUE, TRUE});
    Require(f.originalMainCalls == 2 && payload.state->mainMenuDepth == 0,
            "Main-menu original call or nesting failed.");
  });
  Case(payload, context, "actual-mode-wrapper-clears-and-reasserts", [&](Fixture &f)
  {
    payload.sync();
    using Mode = void(__thiscall *)(void *, int);
    const auto mode = reinterpret_cast<Mode>(payload.modeHook);
    SetLastError(0x76543210);
    mode(f.game + 0x200000, 0);
    Require(GetLastError() == 0x76543210, "Mode boundary changed LastError.");
    mode(f.game + 0x200000, 1);
    ExpectMarkers(f, {TRUE, FALSE, TRUE});
    Require(f.originalModeCalls == 2 && payload.state->transitionDepth == 0,
            "Mode original call or nesting failed.");
  });
  Case(payload, context, "nested-mode-boundaries-only-outer-sync", [&](Fixture &f)
  {
    payload.sync();
    payload.beforeMode();
    payload.beforeMode();
    payload.afterMode();
    ExpectMarkers(f, {TRUE, FALSE});
    payload.afterMode();
    ExpectMarkers(f, {TRUE, FALSE, TRUE});
  });

  for (Stage stage : {Stage::Load, Stage::Initialize, Stage::Create, Stage::HrInit, Stage::Mark,
                      Stage::Release, Stage::Uninitialize, Stage::Free})
  {
    for (Change change : {Change::ReenterSync, Change::BeginTransition, Change::BecomeWindowed,
                          Change::ReplaceView})
    {
      Case(payload, context,
           "reentry-stage-" + std::to_string(static_cast<int>(stage)) + "-change-" +
               std::to_string(static_cast<int>(change)),
           [&](Fixture &f)
      {
        f.changeAt = stage;
        f.change = change;
        payload.sync();
        Require(f.changed && !payload.shell->marked,
                "Stale TRUE marker survived callback state change.");
        if (stage < Stage::Mark)
        {
          ExpectMarkers(f, {});
        }
        else
        {
          ExpectMarkers(f, {TRUE, FALSE});
          Require(payload.shell->compensations == 1, "Stale marker did not get one compensation.");
        }

        Require(f.loads <= 2, "Reentry caused an unbounded COM retry.");
      });
    }
  }

  Case(payload, context, "destroyed-frame-during-TRUE-not-reused", [&](Fixture &f)
  {
    f.changeAt = Stage::Mark;
    f.change = Change::DestroyFrame;
    payload.sync();
    ExpectMarkers(f, {TRUE});
    Require(!payload.shell->marked, "Destroyed HWND retained as marked.");
  });
}

static void ProbeAvailability()
{
  auto kernel = GetModuleHandleA("kernel32.dll");
  Require(kernel != nullptr, "Kernel module unavailable.");
  auto load = reinterpret_cast<decltype(&LoadLibraryExA)>(GetProcAddress(kernel, "LoadLibraryExA"));
  Require(load != nullptr, "System module loader unavailable.");
  auto module = load("ole32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  Require(module != nullptr, "System COM module unavailable.");
  auto initialize =
      reinterpret_cast<decltype(&CoInitializeEx)>(GetProcAddress(module, "CoInitializeEx"));
  auto create =
      reinterpret_cast<decltype(&CoCreateInstance)>(GetProcAddress(module, "CoCreateInstance"));
  auto uninitialize =
      reinterpret_cast<decltype(&CoUninitialize)>(GetProcAddress(module, "CoUninitialize"));
  Require(initialize && create && uninitialize, "COM entry points unavailable.");
  const auto initialized = initialize(nullptr, COINIT_APARTMENTTHREADED);
  Require(SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE,
          "COM initialization unavailable.");
  ITaskbarList2 *taskbar = nullptr;
  const auto created = create(__uuidof(TaskbarList), nullptr, CLSCTX_INPROC_SERVER,
                              __uuidof(ITaskbarList2), reinterpret_cast<void **>(&taskbar));
  HRESULT ready = E_FAIL;

  if (SUCCEEDED(created) && taskbar)
  {
    ready = taskbar->HrInit();
    taskbar->Release();
  }

  if (SUCCEEDED(initialized))
  {
    uninitialize();
  }

  FreeLibrary(module);
  Require(SUCCEEDED(created) && SUCCEEDED(ready), "Taskbar COM interface is unavailable.");
  Pass("actual x86 taskbar COM availability, no MarkFullscreenWindow call");
}

int main(int argc, char **argv)
{
  const bool probeOnly = argc == 2 && std::strcmp(argv[1], "--probe-shell") == 0;

  if (!probeOnly && argc != 5)
  {
    return 2;
  }

  try
  {
    if (probeOnly)
    {
      ProbeAvailability();
      return 0;
    }

    HMODULE module = LoadLibraryA(argv[1]);
    Require(module != nullptr, "Maintained payload DLL could not be loaded.");
    {
      Payload dll(module);
      RunCases(dll, "DLL");
      MappedImage image(argv[2], true, 0x400000);
      const DWORD origin = std::stoul(argv[3], nullptr, 0);
      Payload embedded(module, image.base, origin);
      RunCases(embedded, "embedded-rebased20000000");
    }

    FreeLibrary(module);
    std::ofstream report(argv[4]);
    report << "{\n  \"passed\": true,\n  \"failed\": 0,\n  \"checks\": " << Checks
           << ",\n  \"gameLaunched\": false,\n  \"gameAttached\": false,\n  "
              "\"realShellMarkerCalls\": 0\n}\n";
    report.close();
    Require(static_cast<bool>(report), "Fixture report could not be saved.");
    std::printf("RESULT %u native checks passed\n", Checks);

    return 0;
  }
  catch (const std::exception &error)
  {
    std::printf("FAIL after %u checks: %s\n", Checks, error.what());
    return 1;
  }
}
