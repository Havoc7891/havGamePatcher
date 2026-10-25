// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_EMPTY_CACHE_DEFERRAL_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_EMPTY_CACHE_DEFERRAL_HPP

struct EmptyCacheDeferralState
{
  DWORD probes, deferred, nonempty, rejected, lastOutcome, lastEpoch, lastCount, reserved;
};

static_assert(sizeof(EmptyCacheDeferralState) == 32, "Empty cache deferral ABI");

extern "C"
{
  EmptyCacheDeferralState emptyCacheDeferralState = {};
}

namespace EmptyCacheDeferral
{
  enum class Outcome : DWORD
  {
    Failed = 0,
    Accepted = 1,
    Deferred = 2
  };

  static void Count(DWORD &value)
  {
    if (value != 0xffffffffu)
    {
      ++value;
    }
  }

  struct Identity
  {
    DWORD surface, texture, handle;
    BYTE name[20];
  };

  // A separate workspace cannot leave probe identities in the strict cache pass.
  static Identity identities[1024] = {};

  struct PendingScope
  {
    DWORD previous = textureState.pendingEpoch;
    bool deferred = false;

    ~PendingScope()
    {
      if (!deferred)
      {
        textureState.pendingEpoch = previous;
      }
    }
  };

  static bool TryDefer()
  {
    using namespace TextureRecovery;
    if (!Configured() || textureState.busy || !state.restoreSuccesses ||
        state.restoreSuccesses == textureState.completedEpoch || state.restoreBusy ||
        state.transitionDepth || state.setupCleanupBusy || Read(GameRva::quit) ||
        Read(GameRva::mode) != 2 || !Read(GameRva::directDraw) ||
        !Read(GameRva::fullscreenActive) || !Read(GameRva::hardware))
    {
      return false;
    }

    auto &observation = emptyCacheDeferralState;
    Count(observation.probes);
    observation.lastEpoch = state.restoreSuccesses;
    observation.lastCount = 0;
    BusyScope busy;
    PendingScope pending;
    textureState.pendingEpoch = state.restoreSuccesses;
    Context context;
    const DWORD frameEpoch = TextureLifecycle::FrameEpoch();
    observation.lastCount = context.count;
    if (!context.count || context.count > 1024 || !context.staging || !context.stagingInterface ||
        !context.guard0 || !context.guard1 || !context.guard2 || context.index0 > 15 ||
        context.index1 > 15 || context.index2 > 15 || !context.Current() ||
        textureState.busy != 1 || frameEpoch != TextureLifecycle::FrameEpoch())
    {
      Count(observation.rejected);
      return false;
    }

    bool named = false;
    for (DWORD slot = 0; slot < context.count; ++slot)
    {
      Identity &saved = identities[slot];
      saved.surface = Read(surfaceTable + slot * 4);
      saved.texture = Read(textureTable + slot * 4);
      saved.handle = Read(handles + slot * 4);
      const auto name =
          reinterpret_cast<const volatile BYTE *>(context.base + gpuNames + slot * 20);
      for (DWORD index = 0; index < 20; ++index)
      {
        saved.name[index] = name[index];
      }
      if (!saved.surface || !saved.texture || !Method(saved.texture, 0x18))
      {
        Count(observation.rejected);
        return false;
      }
      named = named || saved.name[0] != 0;
    }

    // No API or COM callback occurs during these bounded synchronous scans.
    // They do not claim an atomic snapshot against an unrelated writing thread.
    if (!context.Current() || textureState.busy != 1 ||
        frameEpoch != TextureLifecycle::FrameEpoch())
    {
      Count(observation.rejected);
      return false;
    }
    for (DWORD slot = 0; slot < context.count; ++slot)
    {
      const Identity &saved = identities[slot];
      bool current = saved.surface == Read(surfaceTable + slot * 4) &&
                     saved.texture == Read(textureTable + slot * 4) &&
                     saved.handle == Read(handles + slot * 4);
      const auto name =
          reinterpret_cast<const volatile BYTE *>(context.base + gpuNames + slot * 20);
      for (DWORD index = 0; current && index < 20; ++index)
      {
        current = saved.name[index] == name[index];
      }
      if (!current)
      {
        Count(observation.rejected);
        return false;
      }
    }
    if (!context.Current() || textureState.busy != 1 ||
        frameEpoch != TextureLifecycle::FrameEpoch())
    {
      Count(observation.rejected);
      return false;
    }
    if (named)
    {
      Count(observation.nonempty);
      return false;
    }

    pending.deferred = true;
    Count(observation.deferred);
    return true;
  }
} // namespace EmptyCacheDeferral

extern "C" DWORD __cdecl RecoverTexturesForFlush()
{
  using EmptyCacheDeferral::Outcome;
  const Outcome outcome = EmptyCacheDeferral::TryDefer()
                              ? Outcome::Deferred
                              : (RecoverLostTextures() ? Outcome::Accepted : Outcome::Failed);
  emptyCacheDeferralState.lastOutcome = static_cast<DWORD>(outcome);
  return static_cast<DWORD>(outcome);
}

#endif
