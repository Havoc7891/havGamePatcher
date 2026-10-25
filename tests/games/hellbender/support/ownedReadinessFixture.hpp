// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_OWNED_READINESS_FIXTURE_HPP
#define HAVGAMEPATCHER_OWNED_READINESS_FIXTURE_HPP

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
#include "../../../../src/games/hellbender/core/layoutPayload.cpp"

static unsigned Checks = 0;
static DWORD RequestedBase = 0x20000000;
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
  Window,
  Restore,
  SurfaceUnlock,
  ExecuteUnlock
};

struct Object
{
  DWORD *table = nullptr;
  ULONG references = 1, additions = 0, releases = 0;
};

static std::function<void(Operation)> EachOperation;

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
    EachOperation = {};
    image = static_cast<BYTE *>(VirtualAlloc(reinterpret_cast<void *>(RequestedBase), 0x600000,
                                             MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(reinterpret_cast<DWORD>(image) == RequestedBase, "Owned fixture allocation failed");
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
    EachOperation = {};
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
    if (EachOperation)
    {
      EachOperation(operation);
    }
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

#endif
