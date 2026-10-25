// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MOVIE_RECOVERY_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MOVIE_RECOVERY_HPP

#include <ddraw.h>

struct MovieRecoveryState
{
  DWORD busy, observations, calls, attempts, successes, failures, reentrant, invalidations;
  DWORD copies, restoreCalls, restoreSuccesses, locks, unlocks, retries, retrySuccesses, releases;
  DWORD lastStage, lastResult, originalResult, retryResult, caller, source, primary, backbuffer;
  DWORD thread, lifecycle, frameEpoch, restoreEpoch, primaryLost, backbufferLost, lastError,
      reserved;
};

static_assert(sizeof(MovieRecoveryState) == 128, "Movie recovery state ABI");
extern "C"
{
  MovieRecoveryState movieRecoveryState = {};
}

namespace MovieRecovery
{
  using Flip = HRESULT(WINAPI *)(void *, void *, DWORD);
  using Reference = ULONG(WINAPI *)(void *);
  using Surface = HRESULT(WINAPI *)(void *);
  using Lock = HRESULT(WINAPI *)(void *, void *, DDSURFACEDESC *, DWORD, void *);
  using Unlock = HRESULT(WINAPI *)(void *, void *);
  using Query = SIZE_T(WINAPI *)(const void *, MEMORY_BASIC_INFORMATION *, SIZE_T);

  enum class Stage : DWORD
  {
    entry = 1,
    windows,
    retainDraw,
    retainPrimary,
    retainBackbuffer,
    source,
    resolveQuery,
    readable,
    restore,
    primaryLoss,
    backbufferLoss,
    lock,
    draw,
    unlock,
    retry,
    release,
    complete
  };

  // Independent bounded storage, never shared with texture-transfer staging.
  static BYTE savedPixels[320 * 240] = {};
  static WORD savedPalette[256] = {};

  // A callback can temporarily invalidate config and then restore it. Record
  // nested presentation even when the ordinary observer must pass through.
  static void ObserveUnconfiguredNested()
  {
    if (movieRecoveryState.observations == 0xffffffffu)
    {
      movieRecoveryState.reserved = 1;
    }
    else
    {
      ++movieRecoveryState.observations;
    }

    ++movieRecoveryState.reentrant;
  }

  static void SetStage(Stage value)
  {
    movieRecoveryState.lastStage = static_cast<DWORD>(value);
  }

  static bool Failure(HRESULT value)
  {
    movieRecoveryState.lastResult = static_cast<DWORD>(value);
    return false;
  }

  static bool Eligible(bool owned)
  {
    return Configured() && !TextureLifecycle::Incomplete() && !Read(GameRva::quit) && !state.busy &&
           state.restoreBusy == static_cast<DWORD>(owned) && !state.setupCleanupBusy &&
           !state.transitionDepth && !readinessState.busy && !textureState.busy &&
           !loadState.busy && !TextureTransfer::textureTransferStats.busy &&
           !Read(GameRva::drawLock) && !Read(GameRva::textureLock) && !Read(GameRva::drawSession) &&
           Read(GameRva::mode) == 2 && Read(GameRva::hardware) && Read(GameRva::directDraw) &&
           Read(GameRva::fullscreenActive) && Read(GameRva::width) == 640 &&
           Read(GameRva::height) == 480 && state.videoDepth == 1 && state.videoReady &&
           !state.mainMenuDepth && !Read(GameRva::mainMenuVisible);
  }

  static bool Format(const DDSURFACEDESC &value)
  {
    const auto &p = value.ddpfPixelFormat;
    return value.dwSize == sizeof(value) && value.dwWidth == 640 && value.dwHeight == 480 &&
           (value.dwFlags & (DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT)) ==
               (DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT) &&
           p.dwSize == sizeof(p) && p.dwFlags == DDPF_RGB && p.dwFourCC == 0 &&
           p.dwRGBBitCount == 16 && p.dwBBitMask == 0x1f && p.dwRGBAlphaBitMask == 0 &&
           ((p.dwRBitMask == 0xf800 && p.dwGBitMask == 0x7e0) ||
            (p.dwRBitMask == 0x7c00 && p.dwGBitMask == 0x3e0));
  }

  struct Context
  {
    bool captured = false, owned = false, invalidated = false, copied = false, retried = false;
    DWORD base, magic, version, observation, thread, lifecycle, upload, frameSerial, frameEpoch;
    DWORD restoreEpoch, pendingEpoch, completedEpoch, passes;
    DWORD draw, primary, backbuffer, device, drawWindow, app, viewObject, frameObject;
    HWND frame, view;
    DWORD mode, hardware, directDraw, fullscreen, renderBpp, software, preference;
    DWORD mission, ended, content, stage, videoReady, videoBorrowed, source, redMask, greenMask;
    DWORD header[10], tables[3];
    Reference adds[3], drops[3];
    Surface restore, primaryLost, backbufferLost;
    Lock lock;
    Unlock unlock;
    Flip nativeFlip;
    HRESULT retryResult = DDERR_SURFACELOST;
    DWORD retryError = 0;

    bool Current()
    {
      bool valid =
          captured && !invalidated && config.gameBase == base && config.magic == magic &&
          config.version == version && Eligible(owned) && !movieRecoveryState.reserved &&
          movieRecoveryState.observations == observation && (!owned || movieRecoveryState.busy) &&
          thread == TextureLifecycle::ThreadId() && lifecycle == TextureLifecycle::Serial() &&
          upload == TextureLifecycle::UploadSerial() &&
          frameSerial == TextureLifecycle::FrameSerial() &&
          frameEpoch == TextureLifecycle::FrameEpoch() && restoreEpoch == state.restoreSuccesses &&
          pendingEpoch == textureState.pendingEpoch &&
          completedEpoch == textureState.completedEpoch && passes == textureState.passes &&
          draw == Read(GameRva::drawObject) && primary == Read(GameRva::primary) &&
          backbuffer == Read(GameRva::backbuffer) && device == Read(GameRva::legacyDevice) &&
          drawWindow == Read(GameRva::drawWindow) && app == Read(GameRva::app) &&
          viewObject == Read(GameRva::view) &&
          frameObject == *reinterpret_cast<DWORD *>(app + 0x1c) && frame == FrameWindow() &&
          view == ViewWindow() && reinterpret_cast<DWORD>(view) == Read(GameRva::movieWindow) &&
          mode == Read(GameRva::mode) && hardware == Read(GameRva::hardware) &&
          directDraw == Read(GameRva::directDraw) &&
          fullscreen == Read(GameRva::fullscreenActive) && renderBpp == Read(GameRva::renderBpp) &&
          software == Read(GameRva::software) && preference == Read(GameRva::hardwarePreference) &&
          mission == state.depth && ended == state.ended && content == state.contentActive &&
          stage == Read(GameRva::gameStage) && videoReady == state.videoReady &&
          videoBorrowed == state.videoBorrowed && source == Read(GameRva::movieBits) &&
          tables[0] == *reinterpret_cast<DWORD *>(draw) &&
          tables[1] == *reinterpret_cast<DWORD *>(primary) &&
          tables[2] == *reinterpret_cast<DWORD *>(backbuffer);
      for (DWORD index = 0; valid && index < 3; ++index)
      {
        const auto methods = reinterpret_cast<const DWORD *>(tables[index]);
        valid = methods[1] == reinterpret_cast<DWORD>(adds[index]) &&
                methods[2] == reinterpret_cast<DWORD>(drops[index]);
      }
      if (valid)
      {
        const auto first = reinterpret_cast<const DWORD *>(tables[1]);
        const auto second = reinterpret_cast<const DWORD *>(tables[2]);
        valid = first[0x2c / 4] == reinterpret_cast<DWORD>(nativeFlip) &&
                first[0x6c / 4] == reinterpret_cast<DWORD>(restore) &&
                first[0x60 / 4] == reinterpret_cast<DWORD>(primaryLost) &&
                second[0x60 / 4] == reinterpret_cast<DWORD>(backbufferLost) &&
                second[0x64 / 4] == reinterpret_cast<DWORD>(lock) &&
                second[0x80 / 4] == reinterpret_cast<DWORD>(unlock);
      }
      for (DWORD index = 0; valid && index < 10; ++index)
      {
        valid = header[index] == Read(GameRva::movieHeader + index * 4);
      }
      if (valid)
      {
        const auto description =
            reinterpret_cast<const DDSURFACEDESC *>(base + GameRva::movieDescription);
        valid = Format(*description) && description->ddpfPixelFormat.dwRBitMask == redMask &&
                description->ddpfPixelFormat.dwGBitMask == greenMask;
      }
      const auto palette = reinterpret_cast<const volatile WORD *>(base + GameRva::moviePalette);
      for (DWORD index = 0; valid && copied && index < 256; ++index)
      {
        valid = palette[index] == savedPalette[index];
      }
      if (!valid && !invalidated)
      {
        invalidated = true;
        ++movieRecoveryState.invalidations;
      }
      return valid;
    }

    bool Windows()
    {
      if (!Current())
      {
        return false;
      }
      BOOL result = Call(IsWindow, isWindow)(frame);
      if (!Current() || !result)
      {
        return false;
      }
      result = Call(IsWindow, isWindow)(view);
      if (!Current() || !result)
      {
        return false;
      }
      result = Call(IsChild, isChild)(frame, view);
      if (!Current() || !result)
      {
        return false;
      }
      DWORD owner = Call(GetWindowThreadProcessId, getWindowThreadProcessId)(frame, nullptr);
      if (!Current() || owner != thread)
      {
        return false;
      }
      owner = Call(GetWindowThreadProcessId, getWindowThreadProcessId)(view, nullptr);
      return Current() && owner == thread;
    }
  };

  // No COM or window calls occur before the original native Flip.
  static void Capture(
      Context &context, void *surface, void *target, DWORD flags, DWORD caller, DWORD observerDepth)
  {
    if (movieRecoveryState.observations == 0xffffffffu)
    {
      movieRecoveryState.reserved = 1;
      return;
    }
    ++movieRecoveryState.observations;
    if (observerDepth || movieRecoveryState.busy)
    {
      ++movieRecoveryState.reentrant;
      return;
    }
    if (movieRecoveryState.reserved || !Eligible(false) || target || flags != 1 ||
        caller != config.gameBase + GameRva::movieFlipReturn)
    {
      return;
    }
    context.base = config.gameBase;
    context.magic = config.magic;
    context.version = config.version;
    context.observation = movieRecoveryState.observations;
    context.thread = TextureLifecycle::ThreadId();
    context.lifecycle = TextureLifecycle::Serial();
    context.upload = TextureLifecycle::UploadSerial();
    context.frameSerial = TextureLifecycle::FrameSerial();
    context.frameEpoch = TextureLifecycle::FrameEpoch();
    context.restoreEpoch = state.restoreSuccesses;
    context.pendingEpoch = textureState.pendingEpoch;
    context.completedEpoch = textureState.completedEpoch;
    context.passes = textureState.passes;
    context.draw = Read(GameRva::drawObject);
    context.primary = Read(GameRva::primary);
    context.backbuffer = Read(GameRva::backbuffer);
    context.device = Read(GameRva::legacyDevice);
    context.drawWindow = Read(GameRva::drawWindow);
    context.app = Read(GameRva::app);
    context.viewObject = Read(GameRva::view);
    if (!context.draw || !context.primary || !context.backbuffer ||
        context.primary == context.backbuffer ||
        reinterpret_cast<DWORD>(surface) != context.primary || !context.device || !context.app ||
        !context.viewObject)
    {
      return;
    }
    context.frameObject = *reinterpret_cast<DWORD *>(context.app + 0x1c);
    context.frame = FrameWindow();
    context.view = ViewWindow();
    if (!context.frameObject || !context.frame || !context.view ||
        Read(GameRva::movieWindow) != reinterpret_cast<DWORD>(context.view))
    {
      return;
    }
    context.mode = Read(GameRva::mode);
    context.hardware = Read(GameRva::hardware);
    context.directDraw = Read(GameRva::directDraw);
    context.fullscreen = Read(GameRva::fullscreenActive);
    context.renderBpp = Read(GameRva::renderBpp);
    context.software = Read(GameRva::software);
    context.preference = Read(GameRva::hardwarePreference);
    context.mission = state.depth;
    context.ended = state.ended;
    context.content = state.contentActive;
    context.stage = Read(GameRva::gameStage);
    context.videoReady = state.videoReady;
    context.videoBorrowed = state.videoBorrowed;
    context.source = Read(GameRva::movieBits);
    for (DWORD index = 0; index < 10; ++index)
    {
      context.header[index] = Read(GameRva::movieHeader + index * 4);
    }
    if (context.header[0] != 40 || context.header[1] != 320 ||
        context.header[2] != static_cast<DWORD>(-240) || context.header[3] != 0x00080001 ||
        context.header[4] != BI_RGB || !context.source ||
        context.source > 0xffffffffu - sizeof(savedPixels))
    {
      return;
    }
    const auto description =
        reinterpret_cast<const DDSURFACEDESC *>(context.base + GameRva::movieDescription);
    if (!Format(*description))
    {
      return;
    }
    context.redMask = description->ddpfPixelFormat.dwRBitMask;
    context.greenMask = description->ddpfPixelFormat.dwGBitMask;
    const DWORD objects[3] = {context.draw, context.primary, context.backbuffer};
    for (DWORD index = 0; index < 3; ++index)
    {
      const auto methods = *reinterpret_cast<DWORD **>(objects[index]);
      if (!methods || !methods[1] || !methods[2])
      {
        return;
      }
      context.tables[index] = reinterpret_cast<DWORD>(methods);
      context.adds[index] = reinterpret_cast<Reference>(methods[1]);
      context.drops[index] = reinterpret_cast<Reference>(methods[2]);
    }
    const auto primary = reinterpret_cast<const DWORD *>(context.tables[1]);
    const auto backbuffer = reinterpret_cast<const DWORD *>(context.tables[2]);
    if (!primary[0x6c / 4] || !primary[0x60 / 4] || !backbuffer[0x60 / 4] ||
        !backbuffer[0x64 / 4] || !backbuffer[0x80 / 4])
    {
      return;
    }
    context.restore = reinterpret_cast<Surface>(primary[0x6c / 4]);
    context.primaryLost = reinterpret_cast<Surface>(primary[0x60 / 4]);
    context.backbufferLost = reinterpret_cast<Surface>(backbuffer[0x60 / 4]);
    context.lock = reinterpret_cast<Lock>(backbuffer[0x64 / 4]);
    context.unlock = reinterpret_cast<Unlock>(backbuffer[0x80 / 4]);
    context.nativeFlip = reinterpret_cast<Flip>(primary[0x2c / 4]);
    context.captured = true;
  }

  static bool Readable(Context &context)
  {
    SetStage(Stage::resolveQuery);
    const auto module = Call(GetModuleHandleA, getModuleHandleA)("kernel32.dll");
    if (!context.Windows() || !module)
    {
      return Failure(E_UNEXPECTED);
    }
    const auto query =
        reinterpret_cast<Query>(Call(GetProcAddress, getProcAddress)(module, "VirtualQuery"));
    if (!context.Windows() || !query)
    {
      return Failure(E_UNEXPECTED);
    }
    SetStage(Stage::readable);
    DWORD cursor = context.source;
    const DWORD end = cursor + sizeof(savedPixels);
    for (DWORD regions = 0; cursor < end && regions < 32; ++regions)
    {
      MEMORY_BASIC_INFORMATION info;
      auto bytes = reinterpret_cast<volatile BYTE *>(&info);
      for (DWORD index = 0; index < sizeof(info); ++index)
      {
        bytes[index] = 0;
      }
      const auto count = query(reinterpret_cast<const void *>(cursor), &info, sizeof(info));
      if (!context.Windows() || count != sizeof(info) || info.State != MEM_COMMIT ||
          (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
          !(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
      {
        return Failure(E_UNEXPECTED);
      }
      const DWORD start = reinterpret_cast<DWORD>(info.BaseAddress);
      if (!info.RegionSize || start > cursor || info.RegionSize > 0xffffffffu - start ||
          start + info.RegionSize <= cursor)
      {
        return Failure(E_UNEXPECTED);
      }
      cursor = start + static_cast<DWORD>(info.RegionSize);
    }
    return cursor >= end ? true : Failure(E_UNEXPECTED);
  }

  static bool Run(Context &context, Flip flip)
  {
    if (!Readable(context))
    {
      return false;
    }
    SetStage(Stage::source);
    const auto pixels = reinterpret_cast<const volatile BYTE *>(context.source);
    for (DWORD index = 0; index < sizeof(savedPixels); ++index)
    {
      savedPixels[index] = pixels[index];
    }
    const auto palette =
        reinterpret_cast<const volatile WORD *>(context.base + GameRva::moviePalette);
    for (DWORD index = 0; index < 256; ++index)
    {
      savedPalette[index] = palette[index];
    }
    context.copied = true;
    ++movieRecoveryState.copies;
    if (!context.Windows() || context.restoreEpoch == 0xffffffffu)
    {
      return Failure(E_UNEXPECTED);
    }
    SetStage(Stage::restore);
    ++movieRecoveryState.restoreCalls;
    HRESULT result = context.restore(reinterpret_cast<void *>(context.primary));
    if (result != S_OK)
    {
      context.Current();
      return Failure(result);
    }
    if (!context.Windows())
    {
      return Failure(E_UNEXPECTED);
    }
    SetStage(Stage::primaryLoss);
    result = context.primaryLost(reinterpret_cast<void *>(context.primary));
    movieRecoveryState.primaryLost = result;
    if (result != S_OK)
    {
      context.Current();
      return Failure(result);
    }
    if (!context.Windows())
    {
      return Failure(E_UNEXPECTED);
    }
    SetStage(Stage::backbufferLoss);
    result = context.backbufferLost(reinterpret_cast<void *>(context.backbuffer));
    movieRecoveryState.backbufferLost = result;
    if (result != S_OK)
    {
      context.Current();
      return Failure(result);
    }
    if (!context.Windows())
    {
      return Failure(E_UNEXPECTED);
    }
    ++state.restoreSuccesses;
    context.restoreEpoch = state.restoreSuccesses;
    movieRecoveryState.restoreEpoch = context.restoreEpoch;
    ++movieRecoveryState.restoreSuccesses;
    Write(GameRva::cockpitFullRedrawCount, 8);
    DDSURFACEDESC description;
    DisplayReadiness::ClearDescription(description);
    SetStage(Stage::lock);
    ++movieRecoveryState.locks;
    result = context.lock(reinterpret_cast<void *>(context.backbuffer), nullptr, &description,
                          DDLOCK_WAIT, nullptr);
    if (result != S_OK)
    {
      context.Current();
      return Failure(result);
    }
    HRESULT drawn = E_UNEXPECTED;
    SetStage(Stage::draw);
    const DWORD bits = reinterpret_cast<DWORD>(description.lpSurface);
    const LONG pitch = description.lPitch;
    const bool valid = context.Windows() && Format(description) &&
                       (description.dwFlags & DDSD_PITCH) &&
                       description.ddpfPixelFormat.dwRBitMask == context.redMask &&
                       description.ddpfPixelFormat.dwGBitMask == context.greenMask &&
                       pitch >= 1280 && pitch <= 8192 && !(pitch & 1) && bits &&
                       bits <= 0xffffffffu - (479u * static_cast<DWORD>(pitch) + 1280u);
    if (valid)
    {
      for (DWORD y = 0; y < 240; ++y)
      {
        auto first = reinterpret_cast<volatile WORD *>(bits + y * 2 * pitch);
        auto second = reinterpret_cast<volatile WORD *>(bits + (y * 2 + 1) * pitch);
        for (DWORD x = 0; x < 320; ++x)
        {
          const WORD value = savedPalette[savedPixels[y * 320 + x]];
          first[x * 2] = value;
          first[x * 2 + 1] = value;
          second[x * 2] = value;
          second[x * 2 + 1] = value;
        }
      }
      drawn = S_OK;
    }
    // A successful Lock owns one Unlock obligation, including rejected callbacks.
    SetStage(Stage::unlock);
    ++movieRecoveryState.unlocks;
    result = context.unlock(reinterpret_cast<void *>(context.backbuffer), nullptr);
    if (result != S_OK)
    {
      context.Current();
      return Failure(result);
    }
    if (drawn != S_OK)
    {
      SetStage(Stage::draw);
      context.Current();
      return Failure(drawn);
    }
    if (!context.Windows())
    {
      return Failure(E_UNEXPECTED);
    }
    SetStage(Stage::retry);
    context.retried = true;
    ++movieRecoveryState.retries;
    context.retryResult = flip(reinterpret_cast<void *>(context.primary), nullptr, 1);
    context.retryError = __readfsdword(0x34);
    movieRecoveryState.retryResult = context.retryResult;
    if (context.retryResult != S_OK)
    {
      context.Current();
      return Failure(context.retryResult);
    }
    ++movieRecoveryState.retrySuccesses;
    return context.Windows() ? true : Failure(E_UNEXPECTED);
  }

  static HRESULT Recover(Context &context, HRESULT original, DWORD originalError, Flip flip)
  {
    if (original != DDERR_SURFACELOST || !context.captured)
    {
      return original;
    }
    ++movieRecoveryState.calls;
    if (movieRecoveryState.busy)
    {
      ++movieRecoveryState.reentrant;
      return original;
    }
    SetStage(Stage::entry);
    movieRecoveryState.originalResult = original;
    movieRecoveryState.retryResult = static_cast<DWORD>(E_PENDING);
    movieRecoveryState.caller = context.base + GameRva::movieFlipReturn;
    movieRecoveryState.source = context.source;
    movieRecoveryState.primary = context.primary;
    movieRecoveryState.backbuffer = context.backbuffer;
    movieRecoveryState.thread = context.thread;
    movieRecoveryState.lifecycle = context.lifecycle;
    movieRecoveryState.frameEpoch = context.frameEpoch;
    movieRecoveryState.restoreEpoch = context.restoreEpoch;
    movieRecoveryState.lastResult = static_cast<DWORD>(E_PENDING);
    movieRecoveryState.primaryLost = static_cast<DWORD>(E_PENDING);
    movieRecoveryState.backbufferLost = static_cast<DWORD>(E_PENDING);
    if (!context.Current() || flip != context.nativeFlip)
    {
      ++movieRecoveryState.failures;
      Failure(E_UNEXPECTED);
      movieRecoveryState.lastError = originalError;
      __writefsdword(0x34, originalError);
      return original;
    }
    movieRecoveryState.busy = 1;
    state.restoreBusy = 1;
    context.owned = true;
    ++movieRecoveryState.attempts;
    const DWORD objects[3] = {context.draw, context.primary, context.backbuffer};
    DWORD retained = 0;
    SetStage(Stage::windows);
    bool valid = context.Windows();
    for (DWORD index = 0; valid && index < 3; ++index)
    {
      SetStage(static_cast<Stage>(static_cast<DWORD>(Stage::retainDraw) + index));
      ++retained;
      context.adds[index](reinterpret_cast<void *>(objects[index]));
      valid = context.Windows();
    }
    if (!valid)
    {
      Failure(E_UNEXPECTED);
    }
    else
    {
      valid = Run(context, flip);
    }
    while (retained)
    {
      const DWORD index = --retained;
      context.drops[index](reinterpret_cast<void *>(objects[index]));
      ++movieRecoveryState.releases;
      const bool current = context.Windows();
      if (!current && valid)
      {
        SetStage(Stage::release);
        Failure(E_UNEXPECTED);
      }
      valid = valid && current;
    }
    if (valid)
    {
      ++movieRecoveryState.successes;
      SetStage(Stage::complete);
      movieRecoveryState.lastResult = S_OK;
    }
    else
    {
      ++movieRecoveryState.failures;
    }
    const HRESULT finalResult = context.retried ? context.retryResult : original;
    const DWORD finalError = context.retried ? context.retryError : originalError;
    movieRecoveryState.lastError = finalError;
    state.restoreBusy = 0;
    movieRecoveryState.busy = 0;
    __writefsdword(0x34, finalError);
    return finalResult;
  }
} // namespace MovieRecovery

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MOVIE_RECOVERY_HPP
