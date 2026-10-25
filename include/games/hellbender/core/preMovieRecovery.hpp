// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_PRE_MOVIE_RECOVERY_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_PRE_MOVIE_RECOVERY_HPP

#include <ddraw.h>

// Recovery is limited to the verified pre-movie CALL in each supported edition.
struct PreMovieRecoveryState
{
  DWORD busy, scopeDepth, generation, pending, scopeThread, observations, incomplete, entries;
  DWORD consumed, eligible, calls, attempts, successes, failures, reentrant, invalidations;
  DWORD restoreCalls, restoreSuccesses, locks, clears, unlocks, retries, retrySuccesses, releases;
  DWORD lastStage, lastResult, originalResult, retryResult, caller, primary, backbuffer, thread;
  DWORD lifecycle, frameEpoch, restoreEpoch, primaryLost, backbufferLost, lastError, scopeBase,
      reserved;
};

static_assert(sizeof(PreMovieRecoveryState) == 160, "Pre-movie recovery state ABI");
extern "C"
{
  PreMovieRecoveryState preMovieRecoveryState = {};
}

namespace PreMovieRecovery
{
  using Flip = MovieRecovery::Flip;
  using Reference = MovieRecovery::Reference;
  using Surface = MovieRecovery::Surface;
  using Lock = MovieRecovery::Lock;
  using Unlock = MovieRecovery::Unlock;
  constexpr DWORD PresentRva = recoveryPreMoviePresentOriginal;
  constexpr DWORD FlipReturnRva = recoveryGenericFlipCaller;
  enum class Stage : DWORD
  {
    entry = 1,
    windows,
    retainDraw,
    retainPrimary,
    retainBackbuffer,
    epoch,
    restore,
    primaryLoss,
    backbufferLoss,
    lock,
    clear,
    unlock,
    retry,
    release,
    complete
  };

  static DWORD Advance(DWORD &counter)
  {
    for (DWORD attempt = 0; attempt < 16; ++attempt)
    {
      const DWORD before = TextureLifecycle::ReadCounter(counter);
      if (before == 0xffffffffu)
      {
        break;
      }
      if (static_cast<DWORD>(_InterlockedCompareExchange(
              reinterpret_cast<volatile long *>(&counter), static_cast<long>(before + 1),
              static_cast<long>(before))) == before)
      {
        return before + 1;
      }
    }
    preMovieRecoveryState.incomplete = 1;
    preMovieRecoveryState.pending = 0;
    return 0;
  }

  static void ObserveUnconfiguredNested()
  {
    Advance(preMovieRecoveryState.observations);
    preMovieRecoveryState.pending = 0;
    ++preMovieRecoveryState.reentrant;
  }

  static void SetStage(Stage value)
  {
    preMovieRecoveryState.lastStage = static_cast<DWORD>(value);
  }

  static bool Failure(HRESULT value)
  {
    preMovieRecoveryState.lastResult = static_cast<DWORD>(value);
    return false;
  }

  static bool Eligible(bool owned)
  {
    return Configured() && !TextureLifecycle::Incomplete() && !Read(GameRva::quit) && !state.busy &&
           state.restoreBusy == static_cast<DWORD>(owned) && !state.setupCleanupBusy &&
           !state.transitionDepth && !readinessState.busy && !textureState.busy &&
           !loadState.busy && !TextureTransfer::textureTransferStats.busy &&
           !TextureTransfer::textureTransferStats.incompleteFlags && !Read(GameRva::drawLock) &&
           !Read(GameRva::textureLock) && !Read(GameRva::drawSession) && Read(GameRva::mode) == 2 &&
           Read(GameRva::hardware) && Read(GameRva::directDraw) &&
           Read(GameRva::fullscreenActive) && Read(GameRva::width) == 640 &&
           Read(GameRva::height) == 480 && !state.videoDepth && !state.mainMenuDepth &&
           !Read(GameRva::mainMenuVisible) && !movieRecoveryState.busy &&
           !preMovieRecoveryState.incomplete && preMovieRecoveryState.scopeDepth == 1 &&
           !preMovieRecoveryState.pending;
  }

  struct Context
  {
    bool captured = false, owned = false, invalidated = false, retried = false;
    DWORD base, magic, version, observation, generation, thread, lifecycle, upload, frameSerial,
        frameEpoch;
    DWORD transferGeneration, readyGeneration, activeGeneration, transferIncomplete;
    DWORD restoreEpoch, pendingEpoch, completedEpoch, passes;
    DWORD draw, primary, backbuffer, device, drawWindow, app, viewObject, frameObject;
    HWND frame, view;
    DWORD mode, hardware, directDraw, fullscreen, renderBpp, software, preference;
    DWORD mission, ended, content, stage, videoReady, videoBorrowed;
    DWORD tables[3];
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
          config.version == version && Eligible(owned) && !preMovieRecoveryState.incomplete &&
          preMovieRecoveryState.observations == observation &&
          preMovieRecoveryState.generation == generation &&
          preMovieRecoveryState.scopeThread == thread && preMovieRecoveryState.scopeBase == base &&
          (!owned || preMovieRecoveryState.busy) &&
          TextureTransfer::textureTransferStats.generation == transferGeneration &&
          TextureTransfer::textureTransferStats.readyGeneration == readyGeneration &&
          TextureTransfer::textureTransferStats.activeGeneration == activeGeneration &&
          TextureTransfer::textureTransferStats.incompleteFlags == transferIncomplete &&
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
          view == ViewWindow() && mode == Read(GameRva::mode) &&
          hardware == Read(GameRva::hardware) && directDraw == Read(GameRva::directDraw) &&
          fullscreen == Read(GameRva::fullscreenActive) && renderBpp == Read(GameRva::renderBpp) &&
          software == Read(GameRva::software) && preference == Read(GameRva::hardwarePreference) &&
          mission == state.depth && ended == state.ended && content == state.contentActive &&
          stage == Read(GameRva::gameStage) && videoReady == state.videoReady &&
          videoBorrowed == state.videoBorrowed && tables[0] == *reinterpret_cast<DWORD *>(draw) &&
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
      if (!valid && !invalidated)
      {
        invalidated = true;
        ++preMovieRecoveryState.invalidations;
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
    if (!Advance(preMovieRecoveryState.observations))
    {
      return;
    }
    if (observerDepth || preMovieRecoveryState.busy)
    {
      preMovieRecoveryState.pending = 0;
      ++preMovieRecoveryState.reentrant;
      return;
    }
    if (!preMovieRecoveryState.pending)
    {
      return;
    }
    // The first observed presentation consumes the token, even if ineligible.
    preMovieRecoveryState.pending = 0;
    ++preMovieRecoveryState.consumed;
    if (caller != config.gameBase + FlipReturnRva || !Eligible(false) || target || flags != 1)
    {
      return;
    }
    context.base = config.gameBase;
    context.magic = config.magic;
    context.version = config.version;
    context.observation = preMovieRecoveryState.observations;
    context.generation = preMovieRecoveryState.generation;
    context.transferGeneration = TextureTransfer::textureTransferStats.generation;
    context.readyGeneration = TextureTransfer::textureTransferStats.readyGeneration;
    context.activeGeneration = TextureTransfer::textureTransferStats.activeGeneration;
    context.transferIncomplete = TextureTransfer::textureTransferStats.incompleteFlags;
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
    if (!context.frameObject || !context.frame || !context.view)
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
    ++preMovieRecoveryState.eligible;
  }

  static bool Run(Context &context, Flip flip)
  {
    if (!context.Windows() || context.restoreEpoch == 0xffffffffu)
    {
      return Failure(E_UNEXPECTED);
    }
    SetStage(Stage::restore);
    ++preMovieRecoveryState.restoreCalls;
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
    preMovieRecoveryState.primaryLost = result;
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
    preMovieRecoveryState.backbufferLost = result;
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
    preMovieRecoveryState.restoreEpoch = context.restoreEpoch;
    ++preMovieRecoveryState.restoreSuccesses;
    Write(GameRva::cockpitFullRedrawCount, 8);
    DDSURFACEDESC description;
    DisplayReadiness::ClearDescription(description);
    SetStage(Stage::lock);
    ++preMovieRecoveryState.locks;
    result = context.lock(reinterpret_cast<void *>(context.backbuffer), nullptr, &description,
                          DDLOCK_WAIT, nullptr);
    if (result != S_OK)
    {
      context.Current();
      return Failure(result);
    }
    HRESULT drawn = E_UNEXPECTED;
    SetStage(Stage::clear);
    const DWORD bits = reinterpret_cast<DWORD>(description.lpSurface);
    const LONG pitch = description.lPitch;
    const bool valid = context.Windows() && MovieRecovery::Format(description) &&
                       (description.dwFlags & DDSD_PITCH) && pitch >= 1280 && pitch <= 8192 &&
                       !(pitch & 1) && bits &&
                       bits <= 0xffffffffu - (479u * static_cast<DWORD>(pitch) + 1280u);
    if (valid)
    {
      for (DWORD y = 0; y < 480; ++y)
      {
        auto row = reinterpret_cast<volatile WORD *>(bits + y * pitch);
        for (DWORD x = 0; x < 640; ++x)
        {
          row[x] = 0;
        }
      }
      ++preMovieRecoveryState.clears;
      drawn = S_OK;
    }
    // A successful Lock owns one Unlock obligation, including rejected callbacks.
    SetStage(Stage::unlock);
    ++preMovieRecoveryState.unlocks;
    result = context.unlock(reinterpret_cast<void *>(context.backbuffer), nullptr);
    if (result != S_OK)
    {
      context.Current();
      return Failure(result);
    }
    if (drawn != S_OK)
    {
      SetStage(Stage::clear);
      context.Current();
      return Failure(drawn);
    }
    if (!context.Windows())
    {
      return Failure(E_UNEXPECTED);
    }
    SetStage(Stage::retry);
    context.retried = true;
    ++preMovieRecoveryState.retries;
    context.retryResult = flip(reinterpret_cast<void *>(context.primary), nullptr, 1);
    context.retryError = __readfsdword(0x34);
    preMovieRecoveryState.retryResult = context.retryResult;
    if (context.retryResult != S_OK)
    {
      context.Current();
      return Failure(context.retryResult);
    }
    ++preMovieRecoveryState.retrySuccesses;
    return context.Windows() ? true : Failure(E_UNEXPECTED);
  }

  static HRESULT Recover(Context &context, HRESULT original, DWORD originalError, Flip flip)
  {
    if (original != DDERR_SURFACELOST || !context.captured)
    {
      return original;
    }
    ++preMovieRecoveryState.calls;
    if (preMovieRecoveryState.busy)
    {
      ++preMovieRecoveryState.reentrant;
      return original;
    }
    SetStage(Stage::entry);
    preMovieRecoveryState.originalResult = original;
    preMovieRecoveryState.retryResult = static_cast<DWORD>(E_PENDING);
    preMovieRecoveryState.caller = context.base + FlipReturnRva;
    preMovieRecoveryState.primary = context.primary;
    preMovieRecoveryState.backbuffer = context.backbuffer;
    preMovieRecoveryState.thread = context.thread;
    preMovieRecoveryState.lifecycle = context.lifecycle;
    preMovieRecoveryState.frameEpoch = context.frameEpoch;
    preMovieRecoveryState.restoreEpoch = context.restoreEpoch;
    preMovieRecoveryState.lastResult = static_cast<DWORD>(E_PENDING);
    preMovieRecoveryState.primaryLost = static_cast<DWORD>(E_PENDING);
    preMovieRecoveryState.backbufferLost = static_cast<DWORD>(E_PENDING);
    if (!context.Current() || flip != context.nativeFlip)
    {
      ++preMovieRecoveryState.failures;
      Failure(E_UNEXPECTED);
      preMovieRecoveryState.lastError = originalError;
      __writefsdword(0x34, originalError);
      return original;
    }
    if (_InterlockedCompareExchange(reinterpret_cast<volatile long *>(&preMovieRecoveryState.busy),
                                    1, 0))
    {
      ++preMovieRecoveryState.reentrant;
      __writefsdword(0x34, originalError);
      return original;
    }
    state.restoreBusy = 1;
    context.owned = true;
    ++preMovieRecoveryState.attempts;
    // Invalidate any staged upload before the first recovery callback. The
    // private context adopts only this deliberate epoch change.
    SetStage(Stage::epoch);
    const DWORD beforeEpoch = context.frameEpoch;
    AdvanceTextureFrameEpoch();
    context.frameEpoch = TextureLifecycle::FrameEpoch();
    preMovieRecoveryState.frameEpoch = context.frameEpoch;
    bool epochValid =
        beforeEpoch != 0xffffffffu && context.frameEpoch == beforeEpoch + 1 && context.Current();
    const DWORD objects[3] = {context.draw, context.primary, context.backbuffer};
    DWORD retained = 0;
    SetStage(Stage::windows);
    bool valid = epochValid && context.Windows();
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
      ++preMovieRecoveryState.releases;
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
      ++preMovieRecoveryState.successes;
      SetStage(Stage::complete);
      preMovieRecoveryState.lastResult = S_OK;
    }
    else
    {
      ++preMovieRecoveryState.failures;
    }
    const HRESULT finalResult = context.retried ? context.retryResult : original;
    const DWORD finalError = context.retried ? context.retryError : originalError;
    preMovieRecoveryState.lastError = finalError;
    state.restoreBusy = 0;
    preMovieRecoveryState.busy = 0;
    __writefsdword(0x34, finalError);
    return finalResult;
  }
} // namespace PreMovieRecovery

// Preserve complete native state around the scope bookkeeping. The original
// zero-argument presenter is called once, including when eligibility is absent.
extern "C" DWORD __cdecl EnterPreMovieScope()
{
  const DWORD error = __readfsdword(0x34);
  const DWORD address = config.gameBase + PreMovieRecovery::PresentRva;
  const DWORD depth = preMovieRecoveryState.scopeDepth;
  preMovieRecoveryState.pending = 0;
  PreMovieRecovery::Advance(preMovieRecoveryState.generation);
  PreMovieRecovery::Advance(preMovieRecoveryState.scopeDepth);
  ++preMovieRecoveryState.entries;
  if (!depth)
  {
    preMovieRecoveryState.scopeThread = TextureLifecycle::ThreadId();
    preMovieRecoveryState.scopeBase = config.gameBase;
    if (Configured() && !preMovieRecoveryState.busy && !preMovieRecoveryState.incomplete)
    {
      preMovieRecoveryState.pending = 1;
    }
  }
  else
  {
    ++preMovieRecoveryState.reentrant;
  }
  __writefsdword(0x34, error);
  return address;
}

extern "C" void __cdecl LeavePreMovieScope()
{
  const DWORD error = __readfsdword(0x34);
  preMovieRecoveryState.pending = 0;
  if (preMovieRecoveryState.scopeDepth)
  {
    --preMovieRecoveryState.scopeDepth;
  }
  else
  {
    preMovieRecoveryState.incomplete = 1;
  }
  __writefsdword(0x34, error);
}

extern "C" __declspec(naked) void __cdecl PreMoviePresentHook()
{
  __asm {
    push 0
    pushfd
    pushad
    cld
    mov ebp, esp
    sub esp, 528
    and esp, -16
    fxsave [esp]
    call EnterPreMovieScope
    mov [ebp + 36], eax
    fxrstor [esp]
    mov esp, ebp
    popad
    popfd
    call dword ptr [esp]
    pushfd
    pushad
    cld
    mov ebp, esp
    sub esp, 528
    and esp, -16
    fxsave [esp]
    call LeavePreMovieScope
    fxrstor [esp]
    mov esp, ebp
    popad
    popfd
    lea esp, [esp + 4]
    ret
  }
}

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_PRE_MOVIE_RECOVERY_HPP
