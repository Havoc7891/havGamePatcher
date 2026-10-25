// SPDX-License-Identifier: MIT

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
#include "layoutPayload.hpp"

using Bytes = std::vector<unsigned char>;
static unsigned Checks = 0;
static std::vector<std::string> Cases;

static void Require(bool value, const char *message)
{
  if (!value)
  {
    throw std::runtime_error(message);
  }
}

static void Check(bool value, const char *message)
{
  Require(value, message);
  ++Checks;
}

#include "../../support/mappedImage.hpp"

struct MovieState
{
  DWORD busy, observations, calls, attempts, successes, failures, reentrant, invalidations;
  DWORD copies, restoreCalls, restoreSuccesses, locks, unlocks, retries, retrySuccesses, releases;
  DWORD lastStage, lastResult, originalResult, retryResult, caller, source, primary, backbuffer;
  DWORD thread, lifecycle, frameEpoch, restoreEpoch, primaryLost, backbufferLost, lastError,
      reserved;
};

static_assert(sizeof(MovieState) == 128, "Movie recovery diagnostic ABI");
using Observe = HRESULT(WINAPI *)(void *, void *, DWORD, DWORD);
static constexpr HRESULT Lost = static_cast<HRESULT>(0x887601c2);
static constexpr DWORD FirstError = 0x12345678, RetryError = 0x87654321;

struct Payload
{
  HMODULE module;
  MappedImage image;
  DWORD origin;
  PayloadConfig *config;
  PayloadState *state;
  MovieState *movie;
  Observe observe;

  template <class T> T Export(const char *name)
  {
    const auto address = GetProcAddress(module, name);
    Require(address != nullptr, "Diagnostic payload export missing");
    return reinterpret_cast<T>(image.base + origin + reinterpret_cast<DWORD>(address) -
                               reinterpret_cast<DWORD>(module));
  }

  Payload(HMODULE value, const char *candidate, DWORD offset, bool first)
      : module(value), image(candidate, first, 0x400000), origin(offset)
  {
    config = Export<PayloadConfig *>("config");
    state = Export<PayloadState *>("state");
    movie = Export<MovieState *>("movieRecoveryState");
    observe = Export<Observe>("observeFlip");
    Require(config->magic == 0x48474c50 && config->version == 1 &&
                config->gameBase == reinterpret_cast<DWORD>(image.base),
            "Mapped movie payload identity mismatch");
  }
};

enum class Stage
{
  FirstFlip,
  Retry,
  AddPrimary,
  AddBack,
  Restore,
  PrimaryQuery,
  BackQuery,
  Lock,
  Unlock,
  ReleasePrimary,
  ReleaseBack,
  MemoryQuery,
  AddDraw,
  ReleaseDraw,
  Windows,
  Resolve
};

enum class Mutation
{
  None,
  Reenter,
  ReplaceBack,
  Mode,
  Lifecycle,
  Upload,
  Frame,
  FrameEpoch,
  Palette,
  SourcePointer,
  SourceBytes,
  Config,
  Header,
  Format,
  Cache,
  UnconfiguredNested,
  Methods,
  Window
};

struct Fixture;
static Fixture *Active = nullptr;
static HRESULT WINAPI FakeFlip(void *, void *, DWORD);
static ULONG WINAPI FakeAdd(void *);
static ULONG WINAPI FakeRelease(void *);
static HRESULT WINAPI FakeRestore(void *);
static HRESULT WINAPI FakeLost(void *);
static HRESULT WINAPI FakeLock(void *, void *, DDSURFACEDESC *, DWORD, HANDLE);
static HRESULT WINAPI FakeUnlock(void *, void *);
static SIZE_T WINAPI FakeQuery(const void *, MEMORY_BASIC_INFORMATION *, SIZE_T);
static FARPROC WINAPI FakeProcedure(HMODULE, LPCSTR);
static BOOL WINAPI FakeWindow(HWND);
static BOOL WINAPI FakeChild(HWND, HWND);
static DWORD WINAPI FakeThread(HWND, DWORD *);

struct Object
{
  DWORD *methods;
  ULONG references = 1;
};

struct Fixture
{
  Payload &p;
  DWORD methods[33] = {};
  DWORD app[16] = {}, frame[16] = {}, view[16] = {};
  Object primary{methods}, backbuffer{methods}, other{methods};
  Bytes source, originalSource, destination;
  std::vector<WORD> originalPalette;
  DWORD width = 640, height = 480, bits = 16, pitch = 1312;
  DWORD red = 0xf800, green = 0x7e0, blue = 0x1f;
  DWORD caller = 0, expectedError = FirstError, suppliedBits = 0;
  HRESULT firstResult = Lost, retryResult = S_OK, restoreResult = S_OK;
  HRESULT primaryLost = S_OK, backLost = S_OK, lockResult = S_OK, unlockResult = S_OK;
  unsigned flips = 0, outerFlips = 0, restores = 0, queries = 0, locks = 0, unlocks = 0;
  unsigned additions = 0, releases = 0;
  bool overrideBits = false, missingQuery = false, nested = false, mutated = false;
  bool windowAlive = true;
  unsigned invalidMemory = 0;
  Stage mutateAt = Stage::Restore;
  Mutation mutation = Mutation::None;
  HRESULT nestedResult = S_OK;
  std::vector<Stage> events;

  DWORD &Word(DWORD rva)
  {
    return *reinterpret_cast<DWORD *>(p.image.base + rva);
  }

  explicit Fixture(Payload &payload) : p(payload)
  {
    Require(!Active, "Overlapping movie fixtures");
    Active = this;
    methods[1] = reinterpret_cast<DWORD>(&FakeAdd);
    methods[2] = reinterpret_cast<DWORD>(&FakeRelease);
    methods[11] = reinterpret_cast<DWORD>(&FakeFlip);
    methods[24] = reinterpret_cast<DWORD>(&FakeLost);
    methods[25] = reinterpret_cast<DWORD>(&FakeLock);
    methods[27] = reinterpret_cast<DWORD>(&FakeRestore);
    methods[32] = reinterpret_cast<DWORD>(&FakeUnlock);
    source.resize(320 * 240);
    destination.resize(64 + 8192 * 480 + 64);
    originalPalette.resize(256);
    Reset();
  }

  ~Fixture()
  {
    p.config->gameBase = reinterpret_cast<DWORD>(p.image.base);
    Active = nullptr;
  }

  void Reset()
  {
    p.config->magic = 0x48474c50;
    p.config->version = 1;
    p.config->gameBase = reinterpret_cast<DWORD>(p.image.base);
    *p.state = {};
    *p.movie = {};
    for (const auto item : {std::pair<const char *, size_t>{"textureState", 4160},
                            {"loadState", 64},
                            {"readinessState", 64},
                            {"textureTransferStats", 128},
                            {"textureLifecycleState", 20},
                            {"flipProbeState", 144}})
    {
      std::memset(p.Export<void *>(item.first), 0, item.second);
    }
    p.state->depth = p.state->contentActive = p.state->videoDepth = p.state->videoReady = 1;
    p.state->restoreSuccesses = 4;
    Word(GameRva::getLastError) = reinterpret_cast<DWORD>(&GetLastError);
    Word(GameRva::setLastError) = reinterpret_cast<DWORD>(&SetLastError);
    Word(GameRva::getModuleHandleA) = reinterpret_cast<DWORD>(&GetModuleHandleA);
    Word(GameRva::getProcAddress) = reinterpret_cast<DWORD>(&FakeProcedure);
    Word(GameRva::isWindow) = reinterpret_cast<DWORD>(&FakeWindow);
    Word(GameRva::isChild) = reinterpret_cast<DWORD>(&FakeChild);
    Word(GameRva::getWindowThreadProcessId) = reinterpret_cast<DWORD>(&FakeThread);
    Word(GameRva::mode) = 2;
    Word(GameRva::hardware) = Word(GameRva::directDraw) = Word(GameRva::fullscreenActive) = 1;
    Word(GameRva::hardwarePreference) = Word(GameRva::gameStage) = 1;
    Word(GameRva::software) = Word(GameRva::mainMenuVisible) = Word(GameRva::quit) = 0;
    Word(GameRva::drawLock) = Word(GameRva::textureLock) = Word(GameRva::drawSession) = 0;
    Word(GameRva::width) = 640;
    Word(GameRva::height) = 480;
    Word(GameRva::renderBpp) = 8;
    Word(GameRva::primary) = reinterpret_cast<DWORD>(&primary);
    Word(GameRva::backbuffer) = reinterpret_cast<DWORD>(&backbuffer);
    Word(GameRva::drawObject) = Word(GameRva::legacyDevice) = reinterpret_cast<DWORD>(&other);
    Word(GameRva::app) = reinterpret_cast<DWORD>(app);
    Word(GameRva::view) = reinterpret_cast<DWORD>(view);
    app[7] = reinterpret_cast<DWORD>(frame);
    frame[7] = Word(GameRva::drawWindow) = 0x1111;
    view[7] = Word(GameRva::movieWindow) = 0x2222;
    Word(GameRva::cockpitFullRedrawCount) = 3;
    primary.references = backbuffer.references = other.references = 1;
    firstResult = Lost;
    retryResult = restoreResult = primaryLost = backLost = lockResult = unlockResult = S_OK;
    expectedError = FirstError;
    flips = outerFlips = restores = queries = locks = unlocks = additions = releases = 0;
    overrideBits = missingQuery = nested = mutated = false;
    windowAlive = true;
    invalidMemory = 0;
    mutation = Mutation::None;
    events.clear();
    width = 640;
    height = 480;
    bits = 16;
    pitch = 1312;
    red = 0xf800;
    green = 0x7e0;
    blue = 0x1f;
    suppliedBits = 0;
    caller = reinterpret_cast<DWORD>(p.image.base) + GameRva::movieFlipReturn;
    std::fill(destination.begin(), destination.end(), static_cast<BYTE>(0xa5));
    for (DWORD y = 0; y < 240; ++y)
    {
      for (DWORD x = 0; x < 320; ++x)
      {
        source[y * 320 + x] = static_cast<BYTE>((x * 7 + y * 13) ^ (x >> 3));
      }
    }
    originalSource = source;
    for (DWORD index = 0; index < 256; ++index)
    {
      originalPalette[index] =
          static_cast<WORD>((index % 32) | (((index * 5) % 64) << 5) | (((index * 11) % 32) << 11));
    }
    std::memcpy(p.image.base + GameRva::moviePalette, originalPalette.data(), 512);
    auto header = reinterpret_cast<BITMAPINFOHEADER *>(p.image.base + GameRva::movieHeader);
    *header = {};
    header->biSize = sizeof(*header);
    header->biWidth = 320;
    header->biHeight = -240;
    header->biPlanes = 1;
    header->biBitCount = 8;
    Word(GameRva::movieBits) = reinterpret_cast<DWORD>(source.data());
    auto description = reinterpret_cast<DDSURFACEDESC *>(p.image.base + GameRva::movieDescription);
    *description = {};
    description->dwSize = sizeof(*description);
    Fill(*description);
  }

  void Fill(DDSURFACEDESC &value)
  {
    value.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_PITCH;
    value.dwWidth = width;
    value.dwHeight = height;
    value.lPitch = static_cast<LONG>(pitch);
    value.lpSurface = reinterpret_cast<void *>(
        overrideBits ? suppliedBits : reinterpret_cast<DWORD>(destination.data() + 64));
    value.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    value.ddpfPixelFormat.dwFlags = DDPF_RGB;
    value.ddpfPixelFormat.dwRGBBitCount = bits;
    value.ddpfPixelFormat.dwRBitMask = red;
    value.ddpfPixelFormat.dwGBitMask = green;
    value.ddpfPixelFormat.dwBBitMask = blue;
  }

  void At(Stage stage)
  {
    events.push_back(stage);
    if (mutation == Mutation::None || mutated || stage != mutateAt)
    {
      return;
    }
    mutated = true;
    DWORD *lifecycle = p.Export<DWORD *>("textureLifecycleState");
    switch (mutation)
    {
    case Mutation::Reenter:
      nested = true;
      nestedResult = p.observe(&primary, nullptr, 1, caller);
      nested = false;
      break;
    case Mutation::UnconfiguredNested:
    {
      const DWORD magic = p.config->magic;
      p.config->magic = 0;
      nested = true;
      nestedResult = p.observe(&primary, nullptr, 1, caller);
      nested = false;
      p.config->magic = magic;
      break;
    }
    case Mutation::Methods:
      methods[27] = reinterpret_cast<DWORD>(&FakeLost);
      break;
    case Mutation::Window:
      windowAlive = false;
      break;
    case Mutation::ReplaceBack:
      Word(GameRva::backbuffer) = reinterpret_cast<DWORD>(&other);
      break;
    case Mutation::Mode:
      Word(GameRva::mode) = 0;
      break;
    case Mutation::Lifecycle:
      ++lifecycle[0];
      break;
    case Mutation::Upload:
      ++lifecycle[1];
      break;
    case Mutation::Frame:
      ++lifecycle[2];
      break;
    case Mutation::FrameEpoch:
      ++lifecycle[3];
      break;
    case Mutation::Palette:
      Word(GameRva::moviePalette) ^= 0x001f0001;
      break;
    case Mutation::SourcePointer:
      Word(GameRva::movieBits) = reinterpret_cast<DWORD>(source.data() + 1);
      break;
    case Mutation::SourceBytes:
      source[0] ^= 0xff;
      source.back() ^= 0x7f;
      break;
    case Mutation::Config:
      p.config->gameBase = 0;
      break;
    case Mutation::Header:
      Word(GameRva::movieHeader + 4) = 319;
      break;
    case Mutation::Format:
      Word(GameRva::movieDescription + 22 * 4) ^= 0xf800;
      break;
    case Mutation::Cache:
      ++p.Export<DWORD *>("textureState")[1];
      break;
    default:
      break;
    }
  }

  HRESULT Run()
  {
    const DWORD beforeBusy = p.movie->busy, beforeRestore = p.state->restoreBusy;
    SetLastError(0xaabbccdd);
    const HRESULT result = p.observe(&primary, nullptr, 1, caller);
    const DWORD error = GetLastError();
    Check(error == expectedError, "Movie recovery changed the result-associated native LastError");
    Check(primary.references == 1 && backbuffer.references == 1 && other.references == 1 &&
              additions == releases,
          "Movie recovery leaked or over-released a COM surface");
    Check(p.movie->busy == beforeBusy && p.state->restoreBusy == beforeRestore,
          "Movie recovery left an ownership guard active");
    return result;
  }

  void Pixels()
  {
    bool correct = true, padding = true, guards = true;
    for (DWORD y = 0; y < 480; ++y)
    {
      const BYTE *row = destination.data() + 64 + y * pitch;
      for (DWORD x = 0; x < 640; ++x)
      {
        WORD pixel;
        std::memcpy(&pixel, row + x * 2, 2);
        correct &= pixel == originalPalette[originalSource[y / 2 * 320 + x / 2]];
      }
      for (DWORD x = 1280; x < pitch; ++x)
      {
        padding &= row[x] == 0xa5;
      }
    }
    for (DWORD index = 0; index < 64; ++index)
    {
      guards &= destination[index] == 0xa5 && destination[64 + pitch * 480 + index] == 0xa5;
    }
    Check(correct, "Recovered movie does not preserve every palette pixel and 2x2 expansion");
    Check(padding && guards, "Recovered movie touched destination padding or guard bytes");
  }
};

static HRESULT WINAPI FakeFlip(void *surface, void *target, DWORD flags)
{
  Check(surface == &Active->primary && !target && flags == 1, "Original Flip arguments changed");
  ++Active->flips;
  if (Active->nested)
  {
    SetLastError(0x10203040);
    return Lost;
  }
  const bool retry = Active->outerFlips++ != 0;
  Active->At(retry ? Stage::Retry : Stage::FirstFlip);
  SetLastError(retry ? RetryError : FirstError);
  return retry ? Active->retryResult : Active->firstResult;
}

static ULONG WINAPI FakeAdd(void *surface)
{
  auto object = static_cast<Object *>(surface);
  Check(object == &Active->primary || object == &Active->backbuffer || object == &Active->other,
        "Unexpected retained object");
  ++Active->additions;
  ++object->references;
  Active->At(object == &Active->primary      ? Stage::AddPrimary
             : object == &Active->backbuffer ? Stage::AddBack
                                             : Stage::AddDraw);
  SetLastError(0xdead0001);
  return object->references;
}

static ULONG WINAPI FakeRelease(void *surface)
{
  auto object = static_cast<Object *>(surface);
  Check(object->references == 2, "Surface release has no retained reference");
  Check(Active->p.movie->busy && Active->p.state->restoreBusy,
        "Recovery guards ended before Release");
  --object->references;
  ++Active->releases;
  Active->At(object == &Active->primary      ? Stage::ReleasePrimary
             : object == &Active->backbuffer ? Stage::ReleaseBack
                                             : Stage::ReleaseDraw);
  SetLastError(0xdead0002);
  return object->references;
}

static HRESULT WINAPI FakeRestore(void *surface)
{
  Check(surface == &Active->primary, "Recovery must restore the original primary");
  ++Active->restores;
  Active->At(Stage::Restore);
  SetLastError(0xdead0003);
  return Active->restoreResult;
}

static HRESULT WINAPI FakeLost(void *surface)
{
  ++Active->queries;
  const bool primary = surface == &Active->primary;
  Active->At(primary ? Stage::PrimaryQuery : Stage::BackQuery);
  SetLastError(0xdead0004);
  return primary ? Active->primaryLost : Active->backLost;
}

static HRESULT WINAPI
FakeLock(void *surface, void *rectangle, DDSURFACEDESC *value, DWORD flags, HANDLE event)
{
  Check(surface == &Active->backbuffer && !rectangle && flags == DDLOCK_WAIT && !event,
        "Movie redraw changed the native whole-surface lock contract");
  Check(value->dwSize == sizeof(*value), "Lock descriptor size is wrong");
  ++Active->locks;
  Active->At(Stage::Lock);
  if (Active->lockResult == S_OK)
  {
    Active->Fill(*value);
  }
  SetLastError(0xdead0005);
  return Active->lockResult;
}

static HRESULT WINAPI FakeUnlock(void *surface, void *bits)
{
  Check(surface == &Active->backbuffer && !bits,
        "Movie redraw must unlock its original whole surface");
  ++Active->unlocks;
  Active->At(Stage::Unlock);
  SetLastError(0xdead0006);
  return Active->unlockResult;
}

static SIZE_T WINAPI FakeQuery(const void *address, MEMORY_BASIC_INFORMATION *info, SIZE_T size)
{
  const SIZE_T result = VirtualQuery(address, info, size);
  Active->At(Stage::MemoryQuery);
  switch (Active->invalidMemory)
  {
  case 1:
    info->Protect = PAGE_NOACCESS;
    break;
  case 2:
    info->Protect |= PAGE_GUARD;
    break;
  case 3:
    info->State = MEM_RESERVE;
    break;
  case 4:
    info->RegionSize = 0;
    break;
  case 5:
    info->BaseAddress = reinterpret_cast<void *>(reinterpret_cast<DWORD>(address) + 1);
    break;
  case 6:
    return 0;
  }
  return result;
}

static FARPROC WINAPI FakeProcedure(HMODULE module, LPCSTR name)
{
  if (std::strcmp(name, "VirtualQuery") == 0)
  {
    Active->At(Stage::Resolve);
    return Active->missingQuery ? nullptr : reinterpret_cast<FARPROC>(&FakeQuery);
  }
  return GetProcAddress(module, name);
}

static BOOL WINAPI FakeWindow(HWND value)
{
  Active->At(Stage::Windows);
  return Active->windowAlive &&
         (value == reinterpret_cast<HWND>(0x1111) || value == reinterpret_cast<HWND>(0x2222));
}

static BOOL WINAPI FakeChild(HWND parent, HWND child)
{
  return parent == reinterpret_cast<HWND>(0x1111) && child == reinterpret_cast<HWND>(0x2222);
}

static DWORD WINAPI FakeThread(HWND value, DWORD *)
{
  return FakeWindow(value) ? GetCurrentThreadId() : 0;
}

template <class F> static void Case(Payload &payload, const std::string &name, F run)
{
  try
  {
    Fixture fixture(payload);
    run(fixture);
    Cases.push_back(name);
  }
  catch (const std::exception &error)
  {
    std::fprintf(stderr, "%s: %s\n", name.c_str(), error.what());
    throw;
  }
}

static void RunCases(Payload &payload)
{
  for (DWORD red : {0xf800u, 0x7c00u})
  {
    for (DWORD pitch : {1280u, 1312u, 8192u})
    {
      Case(payload, "pixels-" + std::to_string(red) + "-" + std::to_string(pitch), [&](Fixture &f)
      {
        f.red = red;
        f.green = red == 0xf800 ? 0x7e0 : 0x3e0;
        f.pitch = pitch;
        f.Fill(*reinterpret_cast<DDSURFACEDESC *>(f.p.image.base + GameRva::movieDescription));
        const Bytes original(f.p.image.base + GameRva::movieDescription,
                             f.p.image.base + GameRva::movieDescription + sizeof(DDSURFACEDESC));
        f.expectedError = RetryError;
        Check(f.Run() == S_OK && f.restores == 1 && f.locks == 1 && f.unlocks == 1 && f.flips == 2,
              "Movie recovery must restore, redraw, unlock and retry exactly once");
        Check(f.p.movie->successes == 1 && f.p.state->restoreSuccesses == 5,
              "Verified movie restoration did not publish its recovery epoch");
        Check(std::memcmp(original.data(), f.p.image.base + GameRva::movieDescription,
                          original.size()) == 0,
              "Recovery overwrote the game's native descriptor");
        f.Pixels();
        Check(f.source == f.originalSource, "Recovery modified the decoder's source pixels");
      });
    }
  }

  for (HRESULT result : {S_OK, S_FALSE, E_FAIL, static_cast<HRESULT>(0x8876021c)})
  {
    Case(payload, "unrelated-result-" + std::to_string(static_cast<DWORD>(result)), [&](Fixture &f)
    {
      f.firstResult = result;
      Check(f.Run() == result && f.flips == 1 && !f.restores && !f.locks && !f.additions,
            "Non-lost Flip acquired recovery resources or changed the result");
    });
  }

  for (unsigned failure = 0; failure < 5; ++failure)
  {
    Case(payload, "recovery-operation-failure-" + std::to_string(failure), [&](Fixture &f)
    {
      if (failure == 0)
      {
        f.restoreResult = E_FAIL;
      }
      if (failure == 1)
      {
        f.primaryLost = Lost;
      }
      if (failure == 2)
      {
        f.backLost = Lost;
      }
      if (failure == 3)
      {
        f.lockResult = E_FAIL;
      }
      if (failure == 4)
      {
        f.unlockResult = E_FAIL;
      }
      Check(f.Run() == Lost && f.flips == 1,
            "Failed reconstruction retried or hid original Flip loss");
      Check(f.unlocks == (f.locks && failure != 3 ? 1u : 0u), "Lock ownership was not balanced");
    });
  }

  for (HRESULT result : {S_FALSE, E_FAIL, Lost})
  {
    Case(payload, "retry-result-" + std::to_string(static_cast<DWORD>(result)), [&](Fixture &f)
    {
      f.retryResult = result;
      f.expectedError = RetryError;
      Check(f.Run() == result && f.flips == 2 && f.restores == 1,
            "Failed retry did not return its actual HRESULT exactly once");
      Check(f.p.movie->successes == 0, "Nonzero retry was reported as recovered");
    });
  }

  for (unsigned invalid = 0; invalid < 12; ++invalid)
  {
    Case(payload, "invalid-lock-description-" + std::to_string(invalid), [&](Fixture &f)
    {
      switch (invalid)
      {
      case 0:
        f.width = 639;
        break;
      case 1:
        f.height = 479;
        break;
      case 2:
        f.bits = 32;
        break;
      case 3:
        f.red = 0x7c00;
        break;
      case 4:
        f.green = 0x3e0;
        break;
      case 5:
        f.blue = 0xff;
        break;
      case 6:
        f.pitch = 1278;
        break;
      case 7:
        f.pitch = 8194;
        break;
      case 8:
        f.pitch = 1281;
        break;
      case 9:
        f.pitch = 0xffffff80;
        break;
      case 10:
        f.overrideBits = true;
        f.suppliedBits = 0;
        break;
      case 11:
        f.overrideBits = true;
        f.suppliedBits = 0xfffffff0;
        break;
      }
      Check(f.Run() == Lost && f.locks == 1 && f.unlocks == 1 && f.flips == 1,
            "Invalid destination descriptor was written or retried");
      Check(std::all_of(f.destination.begin(), f.destination.end(),
                        [](BYTE value) { return value == 0xa5; }),
            "Invalid destination descriptor modified pixels");
    });
  }

  for (Stage stage : {
         Stage::FirstFlip, Stage::AddDraw, Stage::AddPrimary, Stage::AddBack, Stage::MemoryQuery,
             Stage::Windows, Stage::Resolve
         , Stage::Restore, Stage::PrimaryQuery, Stage::BackQuery, Stage::Lock, Stage::Unlock,
             Stage::Retry, Stage::ReleaseBack, Stage::ReleasePrimary, Stage::ReleaseDraw
       })
  {
    for (Mutation mutation :
         {Mutation::Reenter, Mutation::ReplaceBack, Mutation::Lifecycle, Mutation::Upload,
          Mutation::Frame, Mutation::FrameEpoch, Mutation::SourcePointer, Mutation::Config,
          Mutation::Header, Mutation::Cache, Mutation::UnconfiguredNested, Mutation::Methods,
          Mutation::Window})
    {
      Case(payload,
           "callback-" + std::to_string(static_cast<int>(stage)) + "-" +
               std::to_string(static_cast<int>(mutation)),
           [&](Fixture &f)
      {
        f.mutateAt = stage;
        f.mutation = mutation;
        const bool afterRetry = stage == Stage::Retry || stage == Stage::ReleaseBack ||
                                stage == Stage::ReleasePrimary || stage == Stage::ReleaseDraw;
        f.expectedError = afterRetry ? RetryError : FirstError;
        const HRESULT result = f.Run();
        Check(f.mutated, "Requested callback mutation was not exercised");
        Check(result == (afterRetry ? S_OK : Lost),
              "Callback invalidation changed the actual native result");
        Check(f.outerFlips == (afterRetry ? 2u : 1u),
              "Invalidated recovery issued a new native retry");
        Check(f.p.movie->successes == 0,
              "Invalidated callback chain was reported as stable recovery");
      });
    }
  }

  for (unsigned invalid = 0; invalid < 28; ++invalid)
  {
    Case(payload, "ineligible-scope-" + std::to_string(invalid), [&](Fixture &f)
    {
      switch (invalid)
      {
      case 0:
        ++f.caller;
        break;
      case 1:
        f.Word(GameRva::mode) = 1;
        break;
      case 2:
        f.Word(GameRva::hardware) = 0;
        break;
      case 3:
        f.Word(GameRva::directDraw) = 0;
        break;
      case 4:
        f.Word(GameRva::fullscreenActive) = 0;
        break;
      case 5:
        f.Word(GameRva::width) = 800;
        break;
      case 6:
        f.Word(GameRva::height) = 600;
        break;
      case 7:
        f.p.state->videoDepth = 0;
        break;
      case 8:
        f.p.state->videoDepth = 2;
        break;
      case 9:
        f.p.state->videoReady = 0;
        break;
      case 10:
        f.p.state->mainMenuDepth = 1;
        break;
      case 11:
        f.Word(GameRva::mainMenuVisible) = 1;
        break;
      case 12:
        f.Word(GameRva::movieWindow) = 0x3333;
        break;
      case 13:
        f.Word(GameRva::drawLock) = 1;
        break;
      case 14:
        f.Word(GameRva::textureLock) = 1;
        break;
      case 15:
        f.Word(GameRva::drawSession) = 1;
        break;
      case 16:
        f.p.state->busy = 1;
        break;
      case 17:
        f.p.state->restoreBusy = 1;
        break;
      case 18:
        f.p.state->transitionDepth = 1;
        break;
      case 19:
        f.p.state->setupCleanupBusy = 1;
        break;
      case 20:
        f.Word(GameRva::quit) = 1;
        break;
      case 21:
        f.p.Export<DWORD *>("readinessState")[0] = 1;
        break;
      case 22:
        f.p.Export<DWORD *>("textureState")[0] = 1;
        break;
      case 23:
        f.p.Export<DWORD *>("loadState")[0] = 1;
        break;
      case 24:
        f.p.Export<DWORD *>("textureTransferStats")[3] = 1;
        break;
      case 25:
        f.p.Export<DWORD *>("textureLifecycleState")[4] = 1;
        break;
      case 26:
        f.p.movie->busy = 1;
        break;
      case 27:
        f.Word(GameRva::primary) = reinterpret_cast<DWORD>(&f.backbuffer);
        break;
      }
      Check(f.Run() == Lost && f.flips == 1 && !f.additions && !f.restores && !f.locks,
            "Ineligible native path attempted movie recovery");
    });
  }

  for (unsigned invalid = 0; invalid < 13; ++invalid)
  {
    Case(payload, "invalid-source-" + std::to_string(invalid), [&](Fixture &f)
    {
      auto header = reinterpret_cast<BITMAPINFOHEADER *>(f.p.image.base + GameRva::movieHeader);
      auto description =
          reinterpret_cast<DDSURFACEDESC *>(f.p.image.base + GameRva::movieDescription);
      switch (invalid)
      {
      case 0:
        header->biSize = 0;
        break;
      case 1:
        header->biWidth = 319;
        break;
      case 2:
        header->biHeight = 240;
        break;
      case 3:
        header->biPlanes = 2;
        break;
      case 4:
        header->biBitCount = 16;
        break;
      case 5:
        header->biCompression = BI_RLE8;
        break;
      case 6:
        f.Word(GameRva::movieBits) = 0;
        break;
      case 7:
        f.Word(GameRva::movieBits) = 0xfffffff0;
        break;
      case 8:
        description->ddpfPixelFormat.dwRGBBitCount = 32;
        break;
      case 9:
        description->ddpfPixelFormat.dwFlags |= DDPF_ALPHAPIXELS;
        break;
      case 10:
        description->ddpfPixelFormat.dwRBitMask = 0;
        break;
      case 11:
        description->dwSize = 0;
        break;
      case 12:
        description->dwFlags &= ~DDSD_PIXELFORMAT;
        break;
      }
      Check(f.Run() == Lost && f.flips == 1 && !f.additions && !f.restores && !f.locks,
            "Invalid native movie source acquired recovery resources");
    });
  }

  for (unsigned invalid = 0; invalid < 7; ++invalid)
  {
    Case(payload, "source-memory-" + std::to_string(invalid), [&](Fixture &f)
    {
      f.invalidMemory = invalid;
      f.missingQuery = invalid == 0;
      Check(f.Run() == Lost && f.flips == 1 && !f.restores && !f.locks,
            "Unreadable source memory reached restoration");
    });
  }

  for (Stage stage :
       {Stage::Restore, Stage::PrimaryQuery, Stage::BackQuery, Stage::Lock, Stage::Unlock,
        Stage::Retry, Stage::ReleaseBack, Stage::ReleasePrimary, Stage::ReleaseDraw})
  {
    Case(payload, "palette-after-copy-" + std::to_string(static_cast<int>(stage)), [&](Fixture &f)
    {
      f.mutateAt = stage;
      f.mutation = Mutation::Palette;
      const bool afterRetry = stage == Stage::Retry || stage == Stage::ReleaseBack ||
                              stage == Stage::ReleasePrimary || stage == Stage::ReleaseDraw;
      f.expectedError = afterRetry ? RetryError : FirstError;
      Check(f.Run() == (afterRetry ? S_OK : Lost) && f.mutated && !f.p.movie->successes,
            "Palette change after the saved frame was not detected");
      Check(f.outerFlips == (afterRetry ? 2u : 1u), "Palette invalidation issued a new retry");
    });
  }

  Case(payload, "nested-observation-overflow", [](Fixture &f)
  {
    f.p.movie->observations = 0xfffffffe;
    f.mutateAt = Stage::FirstFlip;
    f.mutation = Mutation::Reenter;
    Check(f.Run() == Lost && f.flips == 2 && !f.additions && !f.restores,
          "Saturated nested observation generation allowed outer restoration");
    Check(f.p.movie->reserved == 1, "Observation generation overflow was not latched");
  });

  Case(payload, "snapshot-pixels-before-restore", [](Fixture &f)
  {
    f.mutateAt = Stage::Restore;
    f.mutation = Mutation::SourceBytes;
    f.expectedError = RetryError;
    Check(f.Run() == S_OK, "Captured source pixels were not independent of decoder advances");
    f.Pixels();
  });
}

int main(int argc, char **argv)
{
  HMODULE module = nullptr;
  try
  {
    Require(argc == 4, "Expected payload DLL, inert candidate and embedding origin");
    module = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    Require(module != nullptr, "Cannot map diagnostic payload without executing its entry point");
    const DWORD origin = std::stoul(argv[3], nullptr, 0);
    for (bool first : {true, false})
    {
      Payload payload(module, argv[2], origin, first);
      RunCases(payload);
    }
    FreeLibrary(module);
    std::printf(
        "{\"passed\":true,\"checks\":%u,\"cases\":%zu,\"mappedBases\":2,\"gameLaunched\":false}\n",
        Checks, Cases.size());
    return 0;
  }
  catch (const std::exception &error)
  {
    std::fprintf(stderr, "FAIL after %u checks: %s\n", Checks, error.what());
    if (module)
    {
      FreeLibrary(module);
    }
    return 1;
  }
}
