// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_DISPLAY_READINESS_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_DISPLAY_READINESS_HPP

#include <ddraw.h>

struct DisplayReadinessState
{
  DWORD busy, calls, successes, failures, timeouts, invalidations, polls, sleeps;
  DWORD lastPhase, lastStage, lastResult, lastElapsed;
  DWORD lastCooperative, lastPrimaryLost, lastBackbufferLost, lastFlip;
};

static_assert(sizeof(DisplayReadinessState) == 64, "Display readiness ABI");

extern "C"
{
  DisplayReadinessState readinessState = {};
}

namespace DisplayReadiness
{
  enum class Phase : DWORD
  {
    startup = 1,
    textureRecovery = 2
  };

  enum class Stage : DWORD
  {
    entry = 1,
    resolveModule,
    resolveClock,
    resolveSleep,
    retainDraw,
    retainPrimary,
    retainBackbuffer,
    queryDraw4,
    windows,
    cooperative,
    displayMode,
    primaryLoss,
    backbufferLoss,
    primaryDescription,
    backbufferDescription,
    flip,
    clock,
    sleep,
    release,
    complete
  };

  static constexpr DWORD timeoutMilliseconds = 5000, pollMilliseconds = 10;
  static const GUID draw4Id = {
      0x9c59509a, 0x39bd, 0x11d1, {0x8c, 0x4a, 0x00, 0xc0, 0x4f, 0xd9, 0x30, 0xc5}};

  using Clock = DWORD(WINAPI *)();
  using Pause = void(WINAPI *)(DWORD);
  using Reference = ULONG(WINAPI *)(void *);

  struct Budget
  {
    Clock clock = nullptr;
    DWORD started = 0;
    bool initialized = false;
  };

  struct NativeError
  {
    decltype(&::GetLastError) get = Call(GetLastError, getLastError);
    decltype(&::SetLastError) set = Call(SetLastError, setLastError);
    DWORD value = get();

    ~NativeError()
    {
      // A rejected callback may have changed config.gameBase. Retain this API
      // target instead of resolving it again while leaving that rejected call.
      set(value);
    }
  };

  static void SetStage(Stage stage)
  {
    readinessState.lastStage = static_cast<DWORD>(stage);
  }

  static bool Eligible(Phase phase)
  {
    if (!Configured() || TextureLifecycle::Incomplete() || Read(GameRva::quit) || state.busy ||
        state.restoreBusy || state.setupCleanupBusy || textureState.busy || loadState.busy ||
        TextureTransfer::textureTransferStats.busy || Read(GameRva::drawLock) ||
        Read(GameRva::textureLock))
    {
      return false;
    }

    if (phase == Phase::startup)
    {
      // Native setup has not published final mode flags or shown its frame yet.
      return true;
    }

    return phase == Phase::textureRecovery && !state.transitionDepth && Read(GameRva::mode) == 2 &&
           Read(GameRva::directDraw) && Read(GameRva::fullscreenActive) && Read(GameRva::hardware);
  }

  struct Context
  {
    Phase phase;
    DWORD base = config.gameBase, magic = config.magic, version = config.version;
    DWORD lifecycle = TextureLifecycle::Serial(), upload = TextureLifecycle::UploadSerial();
    DWORD frameEpoch = TextureLifecycle::FrameEpoch(), thread = TextureLifecycle::ThreadId();
    DWORD draw = Read(GameRva::drawObject), primary = Read(GameRva::primary);
    DWORD backbuffer = Read(GameRva::backbuffer), drawWindow = Read(GameRva::drawWindow);
    DWORD app = Read(GameRva::app), viewObject = Read(GameRva::view);
    DWORD frameObject = app ? *reinterpret_cast<DWORD *>(app + 0x1c) : 0;
    HWND frame = FrameWindow(), view = ViewWindow();
    DWORD mode = Read(GameRva::mode), directDraw = Read(GameRva::directDraw);
    DWORD hardware = Read(GameRva::hardware), fullscreen = Read(GameRva::fullscreenActive);
    DWORD renderBpp = Read(GameRva::renderBpp), session = Read(GameRva::drawSession);
    DWORD preference = Read(GameRva::hardwarePreference), software = Read(GameRva::software);
    DWORD device = Read(GameRva::legacyDevice),
          stagingSurface = Read(GameRva::textureStagingSurface);
    DWORD stagingInterface = Read(GameRva::textureStagingInterface);
    DWORD stage = Read(GameRva::gameStage), menuVisible = Read(GameRva::mainMenuVisible);
    DWORD mission = state.depth, ended = state.ended, content = state.contentActive;
    DWORD video = state.videoDepth, menu = state.mainMenuDepth, transition = state.transitionDepth;
    DWORD restored = state.restoreSuccesses, pending = textureState.pendingEpoch;
    DWORD completed = textureState.completedEpoch;
    DWORD *drawMethods = nullptr, *primaryMethods = nullptr, *backbufferMethods = nullptr;
    bool invalidated = false;

    explicit Context(Phase value) : phase(value) {}

    // Pure identity pins are also used by the separately owned flush restore.
    // The normal readiness context still requires its original eligibility and busy state.
    bool IdentityCurrent() const
    {
      return config.gameBase == base && config.magic == magic && config.version == version &&
             lifecycle == TextureLifecycle::Serial() &&
             upload == TextureLifecycle::UploadSerial() &&
             frameEpoch == TextureLifecycle::FrameEpoch() &&
             thread == TextureLifecycle::ThreadId() && draw && draw == Read(GameRva::drawObject) &&
             primary && primary == Read(GameRva::primary) && backbuffer && backbuffer != primary &&
             backbuffer == Read(GameRva::backbuffer) && drawWindow == Read(GameRva::drawWindow) &&
             app && app == Read(GameRva::app) && viewObject && viewObject == Read(GameRva::view) &&
             frameObject && frameObject == *reinterpret_cast<DWORD *>(app + 0x1c) && frame &&
             frame == FrameWindow() && view && view == ViewWindow() &&
             mode == Read(GameRva::mode) && directDraw == Read(GameRva::directDraw) &&
             hardware == Read(GameRva::hardware) && fullscreen == Read(GameRva::fullscreenActive) &&
             renderBpp == Read(GameRva::renderBpp) && session == Read(GameRva::drawSession) &&
             preference == Read(GameRva::hardwarePreference) &&
             software == Read(GameRva::software) && device == Read(GameRva::legacyDevice) &&
             stagingSurface == Read(GameRva::textureStagingSurface) &&
             stagingInterface == Read(GameRva::textureStagingInterface) &&
             stage == Read(GameRva::gameStage) && menuVisible == Read(GameRva::mainMenuVisible) &&
             mission == state.depth && ended == state.ended && content == state.contentActive &&
             video == state.videoDepth && menu == state.mainMenuDepth &&
             transition == state.transitionDepth && restored == state.restoreSuccesses &&
             pending == textureState.pendingEpoch && completed == textureState.completedEpoch &&
             (!drawMethods || drawMethods == *reinterpret_cast<DWORD **>(draw)) &&
             (!primaryMethods || primaryMethods == *reinterpret_cast<DWORD **>(primary)) &&
             (!backbufferMethods || backbufferMethods == *reinterpret_cast<DWORD **>(backbuffer));
    }

    bool Current()
    {
      const bool current = !invalidated && config.gameBase == base && config.magic == magic &&
                           config.version == version && Eligible(phase) && readinessState.busy &&
                           IdentityCurrent();
      if (!current && !invalidated)
      {
        invalidated = true;
        ++readinessState.invalidations;
        readinessState.lastResult = static_cast<DWORD>(E_UNEXPECTED);
      }

      return current;
    }

    bool Windows()
    {
      SetStage(Stage::windows);
      if (!Current())
      {
        return false;
      }

      BOOL valid = Call(IsWindow, isWindow)(frame);
      if (!Current() || !valid)
      {
        return false;
      }

      valid = Call(IsWindow, isWindow)(view);
      if (!Current() || !valid)
      {
        return false;
      }

      valid = Call(IsChild, isChild)(frame, view);
      if (!Current() || !valid)
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

  struct References
  {
    Context &context;
    void *objects[4] = {};
    Reference releases[4] = {};
    DWORD count = 0;

    explicit References(Context &value) : context(value) {}

    bool Adopt(void *object)
    {
      if (!object || count == 4)
      {
        return false;
      }

      const auto methods = *reinterpret_cast<DWORD **>(object);
      if (!methods || !methods[2])
      {
        return false;
      }

      objects[count] = object;
      releases[count] = reinterpret_cast<Reference>(methods[2]);
      ++count;
      return true;
    }

    bool Acquire(DWORD object)
    {
      if (!context.Current())
      {
        return false;
      }

      const auto methods = *reinterpret_cast<DWORD **>(object);
      if (!methods || !methods[1] || !Adopt(reinterpret_cast<void *>(object)))
      {
        return false;
      }

      // Capture release ownership before AddRef can dispatch a nested callback.
      reinterpret_cast<Reference>(methods[1])(reinterpret_cast<void *>(object));
      return context.Current();
    }

    bool Close()
    {
      bool current = context.Current();
      while (count)
      {
        --count;
        releases[count](objects[count]);
        current = context.Current() && current;
      }

      return current;
    }
  };

  static bool Transient(HRESULT result)
  {
    return result == DDERR_WASSTILLDRAWING || result == DDERR_NOEXCLUSIVEMODE ||
           result == DDERR_WRONGMODE || result == DDERR_SURFACEBUSY;
  }

  static bool DescriptionValid(const DDSURFACEDESC &value)
  {
    constexpr DWORD required = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    if (value.dwSize != sizeof(value) || (value.dwFlags & required) != required || !value.dwWidth ||
        !value.dwHeight || value.ddpfPixelFormat.dwSize != sizeof(DDPIXELFORMAT))
    {
      return false;
    }

    constexpr DWORD indexed = DDPF_PALETTEINDEXED1 | DDPF_PALETTEINDEXED2 | DDPF_PALETTEINDEXED4 |
                              DDPF_PALETTEINDEXEDTO8 | DDPF_PALETTEINDEXED8;
    const auto &format = value.ddpfPixelFormat;
    if (format.dwFlags & indexed)
    {
      return (format.dwFlags & indexed) == DDPF_PALETTEINDEXED8 && format.dwRGBBitCount == 8;
    }

    return (format.dwFlags & DDPF_RGB) && format.dwRGBBitCount;
  }

  static bool SameFormat(const DDSURFACEDESC &left, const DDSURFACEDESC &right)
  {
    const auto &first = left.ddpfPixelFormat;
    const auto &second = right.ddpfPixelFormat;
    if (left.dwWidth != right.dwWidth || left.dwHeight != right.dwHeight ||
        first.dwRGBBitCount != second.dwRGBBitCount)
    {
      return false;
    }

    const bool firstIndexed = (first.dwFlags & DDPF_PALETTEINDEXED8) != 0;
    const bool secondIndexed = (second.dwFlags & DDPF_PALETTEINDEXED8) != 0;
    if (firstIndexed || secondIndexed)
    {
      // Palette entries define the colors. The unused RGB masks can differ,
      // and some drivers additionally report DDPF_RGB for indexed surfaces.
      return firstIndexed && secondIndexed && first.dwRGBBitCount == 8;
    }

    return (first.dwFlags & DDPF_RGB) && (second.dwFlags & DDPF_RGB) &&
           first.dwRBitMask == second.dwRBitMask && first.dwGBitMask == second.dwGBitMask &&
           first.dwBBitMask == second.dwBBitMask;
  }

  // Volatile stores prevent a CRT memset dependency in the transplanted payload.
  static void ClearDescription(DDSURFACEDESC &value)
  {
    auto bytes = reinterpret_cast<volatile BYTE *>(&value);
    for (DWORD index = 0; index < sizeof(value); ++index)
    {
      bytes[index] = 0;
    }

    value.dwSize = sizeof(value);
  }

  static HRESULT Probe(Context &context, IDirectDraw4 *draw4)
  {
    if (!context.Windows())
    {
      return E_UNEXPECTED;
    }

    ++readinessState.polls;
    SetStage(Stage::cooperative);
    HRESULT result = draw4->TestCooperativeLevel();
    readinessState.lastCooperative = result;
    if (!context.Current() || result != S_OK)
    {
      return context.invalidated ? E_UNEXPECTED : result;
    }

    auto draw = reinterpret_cast<IDirectDraw *>(context.draw);
    auto primary = reinterpret_cast<IDirectDrawSurface *>(context.primary);
    auto backbuffer = reinterpret_cast<IDirectDrawSurface *>(context.backbuffer);
    DDSURFACEDESC display, primaryDescription, backbufferDescription;
    ClearDescription(display);
    ClearDescription(primaryDescription);
    ClearDescription(backbufferDescription);

    SetStage(Stage::displayMode);
    result = draw->GetDisplayMode(&display);
    if (!context.Current() || result != S_OK)
    {
      return context.invalidated ? E_UNEXPECTED : result;
    }

    SetStage(Stage::primaryLoss);
    result = primary->IsLost();
    readinessState.lastPrimaryLost = result;
    if (!context.Current() || result != S_OK)
    {
      return context.invalidated ? E_UNEXPECTED : result;
    }

    SetStage(Stage::backbufferLoss);
    result = backbuffer->IsLost();
    readinessState.lastBackbufferLost = result;
    if (!context.Current() || result != S_OK)
    {
      return context.invalidated ? E_UNEXPECTED : result;
    }

    SetStage(Stage::primaryDescription);
    result = primary->GetSurfaceDesc(&primaryDescription);
    if (!context.Current() || result != S_OK)
    {
      return context.invalidated ? E_UNEXPECTED : result;
    }

    SetStage(Stage::backbufferDescription);
    result = backbuffer->GetSurfaceDesc(&backbufferDescription);
    if (!context.Current() || result != S_OK)
    {
      return context.invalidated ? E_UNEXPECTED : result;
    }

    if (!DescriptionValid(display) || !DescriptionValid(primaryDescription) ||
        !DescriptionValid(backbufferDescription))
    {
      return DDERR_INVALIDPARAMS;
    }

    if (!SameFormat(display, primaryDescription) ||
        !SameFormat(primaryDescription, backbufferDescription))
    {
      return DDERR_WRONGMODE;
    }

    SetStage(Stage::flip);
    result = primary->GetFlipStatus(DDGFS_CANFLIP);
    readinessState.lastFlip = result;
    if (!context.Current())
    {
      return E_UNEXPECTED;
    }

    if (result == S_OK && !context.Windows())
    {
      return E_UNEXPECTED;
    }

    SetStage(Stage::flip);
    return result;
  }

  static HRESULT Run(Context &context, References &references, Budget *budget = nullptr)
  {
    if (!context.Windows())
    {
      return E_UNEXPECTED;
    }

    SetStage(Stage::resolveModule);
    const HMODULE kernel = Call(GetModuleHandleA, getModuleHandleA)("kernel32.dll");
    if (!context.Current() || !kernel)
    {
      return E_UNEXPECTED;
    }

    SetStage(Stage::resolveClock);
    const auto clock =
        reinterpret_cast<Clock>(Call(GetProcAddress, getProcAddress)(kernel, "GetTickCount"));
    if (!context.Current() || !clock)
    {
      return E_UNEXPECTED;
    }

    SetStage(Stage::resolveSleep);
    const auto pause =
        reinterpret_cast<Pause>(Call(GetProcAddress, getProcAddress)(kernel, "Sleep"));
    if (!context.Current() || !pause)
    {
      return E_UNEXPECTED;
    }

    SetStage(Stage::clock);
    const DWORD observed = clock();
    if (!context.Current())
    {
      return E_UNEXPECTED;
    }
    if (budget)
    {
      if (!budget->initialized)
      {
        budget->clock = clock;
        budget->started = observed;
        budget->initialized = true;
      }
      else if (budget->clock != clock)
      {
        return E_UNEXPECTED;
      }
    }
    const DWORD started = budget ? budget->started : observed;
    if (budget && observed - started >= timeoutMilliseconds)
    {
      readinessState.lastElapsed = observed - started;
      ++readinessState.timeouts;
      return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }

    context.drawMethods = *reinterpret_cast<DWORD **>(context.draw);
    context.primaryMethods = *reinterpret_cast<DWORD **>(context.primary);
    context.backbufferMethods = *reinterpret_cast<DWORD **>(context.backbuffer);
    if (!context.drawMethods || !context.drawMethods[0] || !context.drawMethods[12] ||
        !context.primaryMethods || !context.primaryMethods[18] || !context.primaryMethods[22] ||
        !context.primaryMethods[24] || !context.backbufferMethods ||
        !context.backbufferMethods[22] || !context.backbufferMethods[24])
    {
      return E_UNEXPECTED;
    }

    SetStage(Stage::retainDraw);
    if (!references.Acquire(context.draw))
    {
      return E_UNEXPECTED;
    }

    SetStage(Stage::retainPrimary);
    if (!references.Acquire(context.primary))
    {
      return E_UNEXPECTED;
    }

    SetStage(Stage::retainBackbuffer);
    if (!references.Acquire(context.backbuffer))
    {
      return E_UNEXPECTED;
    }

    SetStage(Stage::queryDraw4);
    IDirectDraw4 *draw4 = nullptr;
    const HRESULT queried = reinterpret_cast<IDirectDraw *>(context.draw)
                                ->QueryInterface(draw4Id, reinterpret_cast<void **>(&draw4));
    // QueryInterface transfers a reference even if a nested callback invalidates
    // the game context. Adopt it before checking that context again.
    const bool adopted = draw4 && references.Adopt(draw4);
    if (!context.Current() || queried != S_OK || !adopted)
    {
      return context.invalidated || queried == S_OK ? E_UNEXPECTED : queried;
    }

    const auto draw4Methods = *reinterpret_cast<DWORD **>(draw4);
    if (!draw4Methods || !draw4Methods[26])
    {
      return E_UNEXPECTED;
    }

    for (DWORD attempt = 0; attempt <= timeoutMilliseconds / pollMilliseconds; ++attempt)
    {
      if (*reinterpret_cast<DWORD **>(draw4) != draw4Methods)
      {
        return E_UNEXPECTED;
      }

      const HRESULT result = Probe(context, draw4);
      readinessState.lastResult = result;
      if (!context.Current())
      {
        return E_UNEXPECTED;
      }

      const DWORD probeStage = readinessState.lastStage;
      SetStage(Stage::clock);
      const DWORD now = clock();
      if (!context.Current())
      {
        return E_UNEXPECTED;
      }

      readinessState.lastElapsed = now - started;
      readinessState.lastStage = probeStage;
      if (result != S_OK && !Transient(result))
      {
        return result;
      }

      if (readinessState.lastElapsed >= timeoutMilliseconds ||
          attempt == timeoutMilliseconds / pollMilliseconds)
      {
        ++readinessState.timeouts;
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
      }

      if (result == S_OK)
      {
        return result;
      }

      SetStage(Stage::sleep);
      ++readinessState.sleeps;
      pause(pollMilliseconds);
      if (!context.Current())
      {
        return E_UNEXPECTED;
      }
    }

    return E_UNEXPECTED;
  }

  static bool Wait(Phase phase, Budget *budget = nullptr)
  {
    ++readinessState.calls;
    if (readinessState.busy || !Eligible(phase))
    {
      ++readinessState.failures;
      return false;
    }

    readinessState.busy = 1;
    readinessState.lastPhase = static_cast<DWORD>(phase);
    SetStage(Stage::entry);
    readinessState.lastResult = static_cast<DWORD>(E_UNEXPECTED);
    readinessState.lastElapsed = 0;
    readinessState.lastCooperative = readinessState.lastPrimaryLost =
        static_cast<DWORD>(E_UNEXPECTED);
    readinessState.lastBackbufferLost = readinessState.lastFlip = static_cast<DWORD>(E_UNEXPECTED);

    Context context(phase);
    NativeError error;
    References references(context);
    HRESULT result = Run(context, references, budget);
    if (!references.Close())
    {
      result = E_UNEXPECTED;
    }

    if (result == S_OK && !context.Windows())
    {
      result = E_UNEXPECTED;
    }

    // A shared recovery budget includes reference release and final window
    // validation. Native COM calls cannot be preempted by this deadline.
    if (result == S_OK && budget)
    {
      const DWORD now = budget->clock();
      if (!context.Current())
      {
        result = E_UNEXPECTED;
      }
      else
      {
        readinessState.lastElapsed = now - budget->started;
        if (readinessState.lastElapsed >= timeoutMilliseconds)
        {
          ++readinessState.timeouts;
          result = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
      }
    }

    readinessState.lastResult = result;
    if (result == S_OK)
    {
      ++readinessState.successes;
      SetStage(Stage::complete);
    }
    else
    {
      ++readinessState.failures;
    }

    readinessState.busy = 0;
    return result == S_OK;
  }
} // namespace DisplayReadiness

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_DISPLAY_READINESS_HPP
