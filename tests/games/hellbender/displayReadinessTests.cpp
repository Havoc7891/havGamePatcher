// SPDX-License-Identifier: MIT

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <ddraw.h>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#include "layoutPayload.hpp"

extern "C"
{
  PayloadConfig config = {};
  PayloadState state = {};
}

template <class T> static T Api(DWORD rva)
{
  return *reinterpret_cast<T *>(config.gameBase + rva);
}

#define Call(name, rva) Api<decltype(&::name)>(GameRva::rva)

static DWORD Read(DWORD rva)
{
  return *reinterpret_cast<volatile DWORD *>(config.gameBase + rva);
}

static void Write(DWORD rva, DWORD value)
{
  *reinterpret_cast<volatile DWORD *>(config.gameBase + rva) = value;
}

static bool Configured()
{
  return config.magic == 0x48474c50 && config.version == 1 && config.gameBase != 0;
}

struct ErrorScope
{
  DWORD value = Call(GetLastError, getLastError)();

  ~ErrorScope()
  {
    Call(SetLastError, setLastError)(value);
  }
};

#include "textureLifecycle.hpp"
#include "textureRecovery.hpp"
#include "textureLoadRecovery.hpp"
#include "textureTransfer.hpp"

static HWND FrameWindow()
{
  const DWORD app = Read(GameRva::app);
  if (!app)
  {
    return nullptr;
  }

  const DWORD frame = *reinterpret_cast<DWORD *>(app + 0x1c);
  return frame ? *reinterpret_cast<HWND *>(frame + 0x1c) : nullptr;
}

static HWND ViewWindow()
{
  const DWORD view = Read(GameRva::view);
  return view ? *reinterpret_cast<HWND *>(view + 0x1c) : nullptr;
}

#include "displayReadiness.hpp"

static unsigned Checks = 0;
static std::vector<std::string> Cases;

static void Check(bool value, const char *message)
{
  if (!value)
  {
    throw std::runtime_error(message);
  }

  ++Checks;
}

enum class Operation
{
  Query,
  Add,
  Release,
  Cooperative,
  DisplayMode,
  PrimaryLost,
  BackbufferLost,
  Description,
  Flip,
  Tick,
  Sleep,
  Window
};

struct Object
{
  DWORD *table = nullptr;
  ULONG references = 1, additions = 0, releases = 0;
};

struct Fixture;
static Fixture *Active = nullptr;
static HRESULT WINAPI FakeQuery(void *, REFIID, void **);
static ULONG WINAPI FakeAdd(void *);
static ULONG WINAPI FakeRelease(void *);
static HRESULT WINAPI FakeCooperative(void *);
static HRESULT WINAPI FakeLost(void *);
static HRESULT WINAPI FakeFlip(void *, DWORD);
static HRESULT WINAPI FakeDisplayMode(void *, DDSURFACEDESC *);
static HRESULT WINAPI FakeDescription(void *, DDSURFACEDESC *);
static DWORD WINAPI FakeTick();
static void WINAPI FakeSleep(DWORD);
static HMODULE WINAPI FakeModule(LPCSTR);
static FARPROC WINAPI FakeProcedure(HMODULE, LPCSTR);
static BOOL WINAPI FakeWindow(HWND);
static BOOL WINAPI FakeChild(HWND, HWND);
static DWORD WINAPI FakeWindowThread(HWND, LPDWORD);
static BOOL WINAPI FakeVisible(HWND);
static BOOL WINAPI FakeIconic(HWND);
static HWND WINAPI FakeForeground();

struct PixelFormat
{
  DWORD flags = DDPF_RGB, bits = 16, red = 0xf800, green = 0x07e0, blue = 0x001f;
};

struct Fixture
{
  BYTE *image = nullptr;
  DWORD methods[40] = {};
  Object draw, draw4, primary, backbuffer;
  DWORD now = 100, sleeps = 0, slept = 0, cooperativeCalls = 0, flipCalls = 0;
  DWORD pendingUntil = 0, clockStep = 0;
  DWORD displayWidth = 640, surfaceWidth = 640;
  PixelFormat displayFormat, primaryFormat, backbufferFormat;
  bool stallClock = false, invalidDescription = false;
  bool visible = true, iconic = false, windowExists = true, child = true;
  bool moduleAvailable = true, tickAvailable = true, sleepAvailable = true;
  HRESULT queryResult = S_OK, cooperativeResult = S_OK, primaryResult = S_OK;
  HRESULT backbufferResult = S_OK, flipResult = S_OK;
  Operation pendingOperation = Operation::Cooperative;
  HRESULT pendingResult = DDERR_WASSTILLDRAWING;
  Operation callbackOperation = Operation::Cooperative;
  std::function<void()> callback;
  unsigned callbacks = 0;
  HWND frame = reinterpret_cast<HWND>(0x1111), view = reinterpret_cast<HWND>(0x2222);
  HWND foreground = frame;

  Fixture()
  {
    Active = this;
    image = static_cast<BYTE *>(
        VirtualAlloc(nullptr, 0x600000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(image != nullptr, "Owned fixture allocation failed");
    config = {};
    config.magic = 0x48474c50;
    config.version = 1;
    config.gameBase = reinterpret_cast<DWORD>(image);
    state = {};
    textureState = {};
    loadState = {};
    TextureTransfer::textureTransferStats = {};
    textureLifecycleState = {};
    readinessState = {};
    methods[0] = reinterpret_cast<DWORD>(&FakeQuery);
    methods[1] = reinterpret_cast<DWORD>(&FakeAdd);
    methods[2] = reinterpret_cast<DWORD>(&FakeRelease);
    methods[12] = reinterpret_cast<DWORD>(&FakeDisplayMode);
    methods[18] = reinterpret_cast<DWORD>(&FakeFlip);
    methods[22] = reinterpret_cast<DWORD>(&FakeDescription);
    methods[24] = reinterpret_cast<DWORD>(&FakeLost);
    methods[26] = reinterpret_cast<DWORD>(&FakeCooperative);
    draw.table = draw4.table = primary.table = backbuffer.table = methods;
    Write(GameRva::getLastError, reinterpret_cast<DWORD>(&::GetLastError));
    Write(GameRva::setLastError, reinterpret_cast<DWORD>(&::SetLastError));
    Write(GameRva::getCurrentThreadId, reinterpret_cast<DWORD>(&::GetCurrentThreadId));
    Write(GameRva::getModuleHandleA, reinterpret_cast<DWORD>(&FakeModule));
    Write(GameRva::getProcAddress, reinterpret_cast<DWORD>(&FakeProcedure));
    Write(GameRva::isWindow, reinterpret_cast<DWORD>(&FakeWindow));
    Write(GameRva::isChild, reinterpret_cast<DWORD>(&FakeChild));
    Write(GameRva::getWindowThreadProcessId, reinterpret_cast<DWORD>(&FakeWindowThread));
    Write(GameRva::isWindowVisible, reinterpret_cast<DWORD>(&FakeVisible));
    Write(GameRva::isWindowEnabled, reinterpret_cast<DWORD>(&FakeVisible));
    Write(GameRva::isIconic, reinterpret_cast<DWORD>(&FakeIconic));
    Write(GameRva::getForegroundWindow, reinterpret_cast<DWORD>(&FakeForeground));
    Write(GameRva::mode, 2);
    Write(GameRva::directDraw, 1);
    Write(GameRva::hardware, 1);
    Write(GameRva::fullscreenActive, 1);
    Write(GameRva::renderBpp, 16);
    Write(GameRva::drawObject, reinterpret_cast<DWORD>(&draw));
    Write(GameRva::primary, reinterpret_cast<DWORD>(&primary));
    Write(GameRva::backbuffer, reinterpret_cast<DWORD>(&backbuffer));
    Write(GameRva::drawWindow, reinterpret_cast<DWORD>(frame));
    Write(GameRva::gameStage, 1);
    Write(GameRva::app, reinterpret_cast<DWORD>(image + 0x380000));
    Write(GameRva::view, reinterpret_cast<DWORD>(image + 0x382000));
    *reinterpret_cast<DWORD *>(image + 0x38001c) = reinterpret_cast<DWORD>(image + 0x381000);
    *reinterpret_cast<HWND *>(image + 0x38101c) = frame;
    *reinterpret_cast<HWND *>(image + 0x38201c) = view;
    state.depth = state.contentActive = 1;
    SetLastError(0x12345678);
  }

  ~Fixture()
  {
    config = {};
    VirtualFree(image, 0, MEM_RELEASE);
    Active = nullptr;
  }

  void On(Operation operation, std::function<void()> action)
  {
    callbackOperation = operation;
    callback = action;
  }

  void Notify(Operation operation)
  {
    SetLastError(0x87654321);
    if (operation == callbackOperation && callback)
    {
      auto action = callback;
      callback = {};
      ++callbacks;
      action();
    }
  }

  HRESULT Probe(Operation operation, HRESULT result)
  {
    Notify(operation);
    if (operation == pendingOperation && pendingUntil && now < pendingUntil)
    {
      return pendingResult;
    }

    return result;
  }

  bool Wait(DisplayReadiness::Phase phase = DisplayReadiness::Phase::startup)
  {
    SetLastError(0x12345678);
    const bool result = DisplayReadiness::Wait(phase);
    Check(GetLastError() == 0x12345678, "Readiness changed the native LastError");
    Check(readinessState.busy == 0, "Readiness retained its reentry guard");
    for (Object *object : {&draw, &draw4, &primary, &backbuffer})
    {
      Check(object->references == 1 && object->additions == object->releases,
            "Readiness leaked or over-released a retained COM reference");
    }

    return result;
  }
};

static HRESULT WINAPI FakeQuery(void *, REFIID iid, void **output)
{
  const GUID directDraw4 = {
      0x9c59509a, 0x39bd, 0x11d1, {0x8c, 0x4a, 0x00, 0xc0, 0x4f, 0xd9, 0x30, 0xc5}};
  Check(std::memcmp(&iid, &directDraw4, sizeof(GUID)) == 0, "Unexpected interface requested");
  *output = nullptr;
  if (SUCCEEDED(Active->queryResult))
  {
    *output = &Active->draw4;
    ++Active->draw4.references;
    ++Active->draw4.additions;
  }

  Active->Notify(Operation::Query);
  return Active->queryResult;
}

static ULONG WINAPI FakeAdd(void *pointer)
{
  auto object = static_cast<Object *>(pointer);
  ++object->additions;
  ++object->references;
  Active->Notify(Operation::Add);
  return object->references;
}

static ULONG WINAPI FakeRelease(void *pointer)
{
  auto object = static_cast<Object *>(pointer);
  Check(object->references > 1, "Released a COM reference not owned by readiness");
  ++object->releases;
  --object->references;
  Active->Notify(Operation::Release);
  return object->references;
}

static HRESULT WINAPI FakeCooperative(void *)
{
  ++Active->cooperativeCalls;
  return Active->Probe(Operation::Cooperative, Active->cooperativeResult);
}

static HRESULT WINAPI FakeLost(void *pointer)
{
  const bool primary = pointer == &Active->primary;
  return Active->Probe(primary ? Operation::PrimaryLost : Operation::BackbufferLost,
                       primary ? Active->primaryResult : Active->backbufferResult);
}

static HRESULT WINAPI FakeFlip(void *, DWORD flags)
{
  Check(flags == DDGFS_CANFLIP || flags == DDGFS_ISFLIPDONE, "Invalid flip status flags");
  ++Active->flipCalls;
  return Active->Probe(Operation::Flip, Active->flipResult);
}

static void FillDescription(DDSURFACEDESC *value, DWORD width, const PixelFormat &format)
{
  Check(value->dwSize == sizeof(*value), "Surface descriptor was not initialized");
  value->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
  value->dwWidth = width;
  value->dwHeight = 480;
  value->ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
  value->ddpfPixelFormat.dwFlags = format.flags;
  value->ddpfPixelFormat.dwRGBBitCount = format.bits;
  value->ddpfPixelFormat.dwRBitMask = format.red;
  value->ddpfPixelFormat.dwGBitMask = format.green;
  value->ddpfPixelFormat.dwBBitMask = format.blue;
  if (Active->invalidDescription)
  {
    value->dwFlags = 0;
  }
}

static HRESULT WINAPI FakeDisplayMode(void *, DDSURFACEDESC *value)
{
  FillDescription(value, Active->displayWidth, Active->displayFormat);
  return Active->Probe(Operation::DisplayMode, S_OK);
}

static HRESULT WINAPI FakeDescription(void *surface, DDSURFACEDESC *value)
{
  FillDescription(value, Active->surfaceWidth,
                  surface == &Active->primary ? Active->primaryFormat : Active->backbufferFormat);
  return Active->Probe(Operation::Description, S_OK);
}

static DWORD WINAPI FakeTick()
{
  Active->Notify(Operation::Tick);
  Active->now += Active->clockStep;
  return Active->now;
}

static void WINAPI FakeSleep(DWORD duration)
{
  Check(duration > 0 && duration <= 100, "Readiness used a blocking unbounded sleep");
  ++Active->sleeps;
  Active->slept += duration;
  if (!Active->stallClock)
  {
    Active->now += duration;
  }
  Active->Notify(Operation::Sleep);
}

static HMODULE WINAPI FakeModule(LPCSTR name)
{
  Check(_stricmp(name, "kernel32.dll") == 0, "Unexpected runtime module requested");
  return Active->moduleAvailable ? reinterpret_cast<HMODULE>(0x1000) : nullptr;
}

static FARPROC WINAPI FakeProcedure(HMODULE, LPCSTR name)
{
  if (std::strcmp(name, "GetTickCount") == 0 && Active->tickAvailable)
  {
    return reinterpret_cast<FARPROC>(&FakeTick);
  }

  if (std::strcmp(name, "Sleep") == 0 && Active->sleepAvailable)
  {
    return reinterpret_cast<FARPROC>(&FakeSleep);
  }

  return nullptr;
}

static BOOL WINAPI FakeWindow(HWND window)
{
  Active->Notify(Operation::Window);
  return Active->windowExists && (window == Active->frame || window == Active->view);
}

static BOOL WINAPI FakeChild(HWND frame, HWND view)
{
  return Active->child && frame == Active->frame && view == Active->view;
}

static DWORD WINAPI FakeWindowThread(HWND, LPDWORD)
{
  return GetCurrentThreadId();
}

static BOOL WINAPI FakeVisible(HWND)
{
  return Active->visible;
}

static BOOL WINAPI FakeIconic(HWND)
{
  return Active->iconic;
}

static HWND WINAPI FakeForeground()
{
  return Active->foreground;
}

template <class F> static void Case(const std::string &name, F run)
{
  try
  {
    Fixture fixture;
    run(fixture);
    Cases.push_back(name);
  }
  catch (const std::exception &error)
  {
    std::fprintf(stderr, "%s: %s\n", name.c_str(), error.what());
    throw;
  }
}

int main()
{
  try
  {
    for (auto phase : {DisplayReadiness::Phase::startup, DisplayReadiness::Phase::textureRecovery})
    {
      const std::string prefix =
          phase == DisplayReadiness::Phase::startup ? "startup-" : "texture-";
      Case(prefix + "immediately-ready", [&](Fixture &fixture)
      {
        Check(fixture.Wait(phase), "Healthy display was rejected");
        Check(fixture.sleeps == 0, "Already ready display incurred a fixed delay");
        Check(fixture.cooperativeCalls && fixture.flipCalls,
              "Display and flip readiness not checked");
      });
    }

    Case("startup-hidden-transition-frame", [](Fixture &fixture)
    {
      fixture.visible = false;
      state.transitionDepth = 1;
      Write(GameRva::mode, 0);
      Write(GameRva::hardware, 0);
      Write(GameRva::fullscreenActive, 0);
      Check(fixture.Wait(), "Transient hidden setup frame was rejected");
    });

    for (Operation operation : {Operation::Cooperative, Operation::Flip})
    {
      Case("transient-ready-" + std::to_string(static_cast<int>(operation)), [&](Fixture &fixture)
      {
        fixture.pendingOperation = operation;
        fixture.pendingUntil = 150;
        Check(fixture.Wait(), "Transient busy result did not recover");
        Check(fixture.sleeps && fixture.now >= 150, "Wait returned while display was still busy");
      });
    }

    Case("permanent-error", [](Fixture &fixture)
    {
      fixture.cooperativeResult = E_FAIL;
      Check(!fixture.Wait(), "Permanent DirectDraw failure was hidden");
      Check(fixture.sleeps == 0, "Permanent failure was retried");
    });

    Case("timeout", [](Fixture &fixture)
    {
      fixture.cooperativeResult = DDERR_WASSTILLDRAWING;
      Check(!fixture.Wait(), "Never-ready display passed");
      Check(fixture.slept >= 5000 && fixture.slept <= 5100, "Deadline was not bounded");
      Check(readinessState.timeouts == 1, "Timeout was not identified");
    });

    Case("ready-probe-arrives-after-deadline", [](Fixture &fixture)
    {
      fixture.On(Operation::Flip, [&] { fixture.now += 5001; });
      Check(!fixture.Wait(), "A successful but late probe bypassed the deadline");
      Check(readinessState.timeouts == 1 && fixture.sleeps == 0,
            "Slow synchronous probe was not reported as a timeout");
    });

    Case("query-interface-consumes-deadline", [](Fixture &fixture)
    {
      fixture.On(Operation::Query, [&] { fixture.now += 5001; });
      Check(!fixture.Wait(), "Interface query time was excluded from the deadline");
      Check(readinessState.timeouts == 1, "Slow interface query was not bounded");
    });

    for (HRESULT transient : {DDERR_NOEXCLUSIVEMODE, DDERR_WRONGMODE, DDERR_SURFACEBUSY})
    {
      Case("transient-mode-status-" + std::to_string(static_cast<DWORD>(transient)),
           [&](Fixture &fixture)
      {
        fixture.pendingUntil = 120;
        fixture.pendingResult = transient;
        Check(fixture.Wait(), "Temporary mode status did not recover");
        Check(fixture.sleeps > 0, "Temporary mode status was accepted before readiness");
      });
    }

    Case("clock-wrap-timeout", [](Fixture &fixture)
    {
      fixture.now = 0xfffffff0u;
      fixture.cooperativeResult = DDERR_WASSTILLDRAWING;
      Check(!fixture.Wait(), "Never-ready display passed across tick wrap");
      Check(fixture.slept >= 5000 && fixture.slept <= 5100, "Tick wrap defeated deadline");
    });

    Case("stalled-clock-timeout", [](Fixture &fixture)
    {
      fixture.stallClock = true;
      fixture.cooperativeResult = DDERR_WASSTILLDRAWING;
      Check(!fixture.Wait(), "A stalled clock allowed an unbounded readiness wait");
      Check(fixture.slept >= 5000 && fixture.slept <= 5100, "Poll budget was not bounded");
    });

    Case("display-and-surface-dimensions-settle", [](Fixture &fixture)
    {
      fixture.displayWidth = 1920;
      fixture.On(Operation::Sleep, [&] { fixture.displayWidth = fixture.surfaceWidth; });
      Check(fixture.Wait(), "Display and surface convergence did not become ready");
      Check(fixture.sleeps > 0, "Readiness passed while display and surfaces disagreed");
    });

    Case("invalid-descriptor-rejected", [](Fixture &fixture)
    {
      fixture.invalidDescription = true;
      Check(!fixture.Wait(), "Malformed surface descriptor was accepted");
      Check(fixture.sleeps == 0, "Invalid descriptor was treated as transient");
    });

    Case("indexed-eight-ignores-unused-rgb-masks", [](Fixture &fixture)
    {
      fixture.displayFormat = {DDPF_PALETTEINDEXED8, 8, 0, 0, 0};
      fixture.primaryFormat = {DDPF_PALETTEINDEXED8, 8, 0xf800, 0x7e0, 0x1f};
      fixture.backbufferFormat = {DDPF_PALETTEINDEXED8, 8, 0xff0000, 0xff00, 0xff};
      Check(fixture.Wait(), "Equivalent palette-indexed surfaces were rejected by unused masks");
      Check(fixture.sleeps == 0, "Indexed mask differences caused an artificial settle delay");
    });

    Case("indexed-eight-allows-additional-rgb-flag", [](Fixture &fixture)
    {
      fixture.displayFormat = {DDPF_PALETTEINDEXED8 | DDPF_RGB, 8, 0, 0, 0};
      fixture.primaryFormat = {DDPF_PALETTEINDEXED8, 8, 0xff, 0, 0};
      fixture.backbufferFormat = {DDPF_PALETTEINDEXED8 | DDPF_RGB, 8, 0, 0xff, 0};
      Check(fixture.Wait(), "Additional RGB metadata changed the indexed format identity");
      Check(fixture.sleeps == 0, "Equivalent indexed flags caused an artificial settle delay");
    });

    Case("indexed-and-direct-eight-bit-formats-are-distinct", [](Fixture &fixture)
    {
      fixture.displayFormat = {DDPF_PALETTEINDEXED8 | DDPF_RGB, 8, 0xe0, 0x1c, 3};
      fixture.primaryFormat = fixture.backbufferFormat = {DDPF_RGB, 8, 0xe0, 0x1c, 3};
      fixture.On(Operation::Sleep, [&]
      { fixture.primaryFormat.flags = fixture.backbufferFormat.flags = DDPF_PALETTEINDEXED8; });
      Check(fixture.Wait(), "Indexed mode did not become ready after surfaces changed format");
      Check(fixture.sleeps > 0, "Matching masks confused direct RGB8 with palette indices");
    });

    Case("indexed-eight-rejects-wrong-depth", [](Fixture &fixture)
    {
      fixture.displayFormat.flags = fixture.primaryFormat.flags = fixture.backbufferFormat.flags =
          DDPF_PALETTEINDEXED8;
      Check(!fixture.Wait(), "Indexed-eight descriptor claiming sixteen bits was accepted");
      Check(fixture.sleeps == 0, "Invalid indexed depth was treated as temporary mode mismatch");
    });

    Case("other-indexed-formats-are-not-direct-rgb", [](Fixture &fixture)
    {
      fixture.displayFormat = fixture.primaryFormat =
          fixture.backbufferFormat = {DDPF_PALETTEINDEXED4 | DDPF_RGB, 4, 0, 0, 0};
      Check(!fixture.Wait(), "Unsupported indexed format was admitted through its RGB flag");
      Check(fixture.sleeps == 0, "Unsupported indexed format waited until timeout");
    });

    Case("direct-rgb-masks-still-identify-the-format", [](Fixture &fixture)
    {
      fixture.primaryFormat = {DDPF_RGB, 16, 0x7c00, 0x3e0, 0x1f};
      fixture.On(Operation::Sleep, [&] { fixture.primaryFormat = fixture.displayFormat; });
      Check(fixture.Wait(), "Direct RGB formats did not become ready when masks converged");
      Check(fixture.sleeps > 0, "Different direct RGB masks were treated as unused metadata");
    });

    for (bool primary : {false, true})
    {
      Case(primary ? "primary-lost-requires-recovery" : "backbuffer-lost-requires-recovery",
           [&](Fixture &fixture)
      {
        if (primary)
        {
          fixture.primaryResult = DDERR_SURFACELOST;
        }
        else
        {
          fixture.backbufferResult = DDERR_SURFACELOST;
        }
        Check(!fixture.Wait(), "Lost surface was reported ready");
        Check(fixture.sleeps == 0, "Readiness concealed a surface requiring native recovery");
      });
    }

    for (unsigned missing = 0; missing < 3; ++missing)
    {
      Case("missing-runtime-api-" + std::to_string(missing), [&](Fixture &fixture)
      {
        fixture.moduleAvailable = missing != 0;
        fixture.tickAvailable = missing != 1;
        fixture.sleepAvailable = missing != 2;
        Check(!fixture.Wait(), "Missing readiness API was ignored");
        Check(fixture.sleeps == 0, "Missing API did not fail immediately");
      });
    }

    Case("missing-cooperative-interface", [](Fixture &fixture)
    {
      fixture.queryResult = E_NOINTERFACE;
      Check(!fixture.Wait(), "Unsupported cooperative readiness probe was ignored");
      Check(fixture.sleeps == 0, "Unsupported interface was repeatedly queried");
    });

    for (DWORD address : {GameRva::drawLock, GameRva::textureLock})
    {
      Case("active-lock-" + std::to_string(address), [&](Fixture &fixture)
      {
        Write(address, 1);
        Check(!fixture.Wait(), "Readiness ran during an active renderer lock or session");
        Check(fixture.cooperativeCalls == 0, "COM probing started while the renderer was locked");
      });
    }

    for (Operation operation :
         {Operation::Query, Operation::Add, Operation::Cooperative, Operation::DisplayMode,
          Operation::PrimaryLost, Operation::BackbufferLost, Operation::Description,
          Operation::Flip, Operation::Release, Operation::Sleep})
    {
      const auto suffix = std::to_string(static_cast<int>(operation));
      Case("callback-resource-replaced-" + suffix, [&](Fixture &fixture)
      {
        if (operation == Operation::Sleep)
        {
          fixture.cooperativeResult = DDERR_WASSTILLDRAWING;
        }
        fixture.On(operation,
                   [&] { Write(GameRva::backbuffer, reinterpret_cast<DWORD>(&fixture.primary)); });
        Check(!fixture.Wait(), "Readiness accepted a resource replaced during a callback");
        Check(fixture.callbacks == 1, "Mutation callback was not exercised");
      });
      Case("callback-reentry-" + suffix, [&](Fixture &fixture)
      {
        if (operation == Operation::Sleep)
        {
          fixture.pendingUntil = 120;
        }
        fixture.On(operation, [&]
        {
          Check(!DisplayReadiness::Wait(DisplayReadiness::Phase::startup),
                "Nested readiness wait was admitted");
        });
        Check(fixture.Wait(), "Rejected nested wait damaged its outer wait");
        Check(fixture.callbacks == 1, "Reentry callback was not exercised");
      });
    }

    for (Operation operation : {Operation::Query, Operation::Add, Operation::Cooperative,
                                Operation::Flip, Operation::Release})
    {
      Case("callback-lifecycle-changed-" + std::to_string(static_cast<int>(operation)),
           [&](Fixture &fixture)
      {
        fixture.On(operation, [] { AdvanceTextureLifecycle(); });
        Check(!fixture.Wait(), "Readiness accepted a stale lifecycle after a callback");
      });
    }

    Case("quit-during-wait", [](Fixture &fixture)
    {
      fixture.cooperativeResult = DDERR_WASSTILLDRAWING;
      fixture.On(Operation::Sleep, [] { Write(GameRva::quit, 1); });
      Check(!fixture.Wait(), "Quit request did not abort the readiness wait");
      Check(fixture.slept <= 100, "Quit request waited until deadline");
    });

    for (Operation operation :
         {Operation::Query, Operation::Add, Operation::Cooperative, Operation::Release})
    {
      Case("configuration-invalidated-" + std::to_string(static_cast<int>(operation)),
           [&](Fixture &fixture)
      {
        fixture.On(operation, [] { config.gameBase = 0; });
        Check(!fixture.Wait(), "Callback configuration invalidation was accepted");
      });
    }

    Case("startup-background-frame-allowed", [](Fixture &fixture)
    {
      fixture.foreground = reinterpret_cast<HWND>(0x3333);
      Check(fixture.Wait(), "Resource readiness unnecessarily depended on foreground focus");
    });

    Case("texture-phase-rejects-active-transition", [](Fixture &fixture)
    {
      state.transitionDepth = 1;
      Check(!fixture.Wait(DisplayReadiness::Phase::textureRecovery),
            "Texture readiness was admitted while display transition was active");
      Check(fixture.cooperativeCalls == 0, "Invalid texture phase made COM calls");
    });

    for (DWORD mode : {0u, 1u})
    {
      Case("texture-phase-rejects-mode-" + std::to_string(mode), [&](Fixture &fixture)
      {
        Write(GameRva::mode, mode);
        Check(!fixture.Wait(DisplayReadiness::Phase::textureRecovery),
              "Texture readiness was admitted outside hardware fullscreen");
      });
    }

    for (DWORD *busy : {&state.busy, &state.restoreBusy, &state.setupCleanupBusy,
                        &textureState.busy, &loadState.busy})
    {
      Case("renderer-busy-" + std::to_string(Cases.size()), [&](Fixture &fixture)
      {
        *busy = 1;
        Check(!fixture.Wait(), "Readiness admitted a conflicting renderer operation");
        Check(fixture.cooperativeCalls == 0, "Busy renderer received extra COM calls");
      });
    }

    Case("source-transfer-busy", [](Fixture &fixture)
    {
      TextureTransfer::textureTransferStats.busy = 1;
      Check(!fixture.Wait(), "Readiness ran during converted texture transfer");
    });

    for (Operation operation : {Operation::Flip, Operation::Release})
    {
      Case("window-destroyed-with-stored-handle-" + std::to_string(static_cast<int>(operation)),
           [&](Fixture &fixture)
      {
        fixture.On(operation, [&] { fixture.windowExists = false; });
        Check(!fixture.Wait(),
              "Readiness accepted a destroyed window whose stored handle remained");
      });
    }

    Case("surface-alias-rejected", [](Fixture &fixture)
    {
      Write(GameRva::backbuffer, reinterpret_cast<DWORD>(&fixture.primary));
      Check(!fixture.Wait(), "Primary and backbuffer alias was admitted");
      Check(fixture.cooperativeCalls == 0, "Aliased surfaces reached readiness probing");
    });

    Case("view-no-longer-child", [](Fixture &fixture)
    {
      fixture.child = false;
      Check(!fixture.Wait(), "A detached game view was admitted");
      Check(fixture.cooperativeCalls == 0, "Detached view reached readiness probing");
    });

    std::printf("{\"passed\":true,\"checks\":%u,\"cases\":[", Checks);
    for (size_t index = 0; index < Cases.size(); ++index)
    {
      std::printf("%s\"%s\"", index ? "," : "", Cases[index].c_str());
    }
    std::printf("]}\n");
    return 0;
  }
  catch (...)
  {
    return 1;
  }
}
