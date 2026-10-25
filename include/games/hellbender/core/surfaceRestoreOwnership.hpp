// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_SURFACE_RESTORE_OWNERSHIP_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_SURFACE_RESTORE_OWNERSHIP_HPP

struct SurfaceRestoreGuardState
{
  DWORD invalidations, lastBoundary, lastReason, reentries;
  DWORD ownerThread, ownerBase, reserved[2];
};

static_assert(sizeof(SurfaceRestoreGuardState) == 32, "Surface restore guard state ABI");

extern "C"
{
  SurfaceRestoreGuardState surfaceRestoreGuardState = {};
}

namespace SurfaceRestoreOwnership
{
  static void Increment(DWORD &counter)
  {
    if (counter != 0xffffffffu)
    {
      ++counter;
    }
  }

  // Cleanup must not resolve a native import through config after a callback changes it.
  struct LastErrorScope
  {
    DWORD value = __readfsdword(0x34);

    ~LastErrorScope()
    {
      __writefsdword(0x34, value);
    }
  };

  struct Owner
  {
    DWORD base = config.gameBase, magic = config.magic, version = config.version;
    DWORD thread = TextureLifecycle::ThreadId(), lifecycle = TextureLifecycle::Serial();
    DWORD frameEpoch = TextureLifecycle::FrameEpoch(), restoreEpoch = state.restoreSuccesses;
    DWORD draw = Read(GameRva::drawObject), device = Read(GameRva::legacyDevice);
    DWORD primary = Read(GameRva::primary), backbuffer = Read(GameRva::backbuffer);
    DWORD mode = Read(GameRva::mode), hardware = Read(GameRva::hardware);
    DWORD directDraw = Read(GameRva::directDraw), fullscreen = Read(GameRva::fullscreenActive);

    Owner()
    {
      surfaceRestoreGuardState.ownerThread = thread;
      surfaceRestoreGuardState.ownerBase = base;
    }

    DWORD ChangedReason() const
    {
      if (config.gameBase != base || config.magic != magic || config.version != version)
      {
        return 1;
      }
      if (TextureLifecycle::ThreadId() != thread)
      {
        return 2;
      }
      if (TextureLifecycle::Serial() != lifecycle || TextureLifecycle::Incomplete())
      {
        return 3;
      }
      if (TextureLifecycle::FrameEpoch() != frameEpoch)
      {
        return 4;
      }
      if (state.restoreSuccesses != restoreEpoch)
      {
        return 5;
      }
      if (Read(GameRva::drawObject) != draw || Read(GameRva::legacyDevice) != device ||
          Read(GameRva::primary) != primary || Read(GameRva::backbuffer) != backbuffer)
      {
        return 6;
      }
      if (state.restoreBusy != 1 || state.transitionDepth || state.setupCleanupBusy ||
          Read(GameRva::quit) || Read(GameRva::mode) != mode ||
          Read(GameRva::hardware) != hardware || Read(GameRva::directDraw) != directDraw ||
          Read(GameRva::fullscreenActive) != fullscreen)
      {
        return 7;
      }
      return 0;
    }

    bool Current(DWORD boundary) const
    {
      const DWORD reason = ChangedReason();
      if (reason)
      {
        Increment(surfaceRestoreGuardState.invalidations);
        surfaceRestoreGuardState.lastBoundary = boundary;
        surfaceRestoreGuardState.lastReason = reason;
        return false;
      }
      return true;
    }
  };

  // Shared by the original pre-unlock recovery and the separately owned
  // readiness recovery. The caller retains any references it requires.
  using SurfaceMethod = HRESULT(WINAPI *)(void *);

  struct Methods
  {
    SurfaceMethod restore, primaryLost, backbufferLost;

    Methods(DWORD *primary, DWORD *backbuffer)
        : restore(reinterpret_cast<SurfaceMethod>(primary[0x6c / 4])),
          primaryLost(reinterpret_cast<SurfaceMethod>(primary[0x60 / 4])),
          backbufferLost(reinterpret_cast<SurfaceMethod>(backbuffer[0x60 / 4]))
    {
    }
  };

  template <class Check>
  static HRESULT RestoreAndVerify(const Owner &owner, const Methods &methods, Check check)
  {
    if (!owner.Current(2) || !check())
    {
      return E_UNEXPECTED;
    }
    ++state.restoreAttempts;
    state.lastRestoreResult = methods.restore(reinterpret_cast<void *>(owner.primary));
    if (!owner.Current(2) || !check())
    {
      return E_UNEXPECTED;
    }
    if (state.lastRestoreResult != S_OK)
    {
      ++state.restoreFailures;
      return state.lastRestoreResult;
    }
    state.lastPrimaryIsLost = methods.primaryLost(reinterpret_cast<void *>(owner.primary));
    if (!owner.Current(3) || !check())
    {
      return E_UNEXPECTED;
    }
    state.lastBackbufferIsLost = methods.backbufferLost(reinterpret_cast<void *>(owner.backbuffer));
    if (!owner.Current(4) || !check())
    {
      return E_UNEXPECTED;
    }
    if (state.lastPrimaryIsLost != S_OK || state.lastBackbufferIsLost != S_OK)
    {
      ++state.restoreFailures;
      return state.lastPrimaryIsLost != S_OK ? state.lastPrimaryIsLost : state.lastBackbufferIsLost;
    }
    Write(GameRva::cockpitFullRedrawCount, 8);
    ++state.restoreSuccesses;
    return S_OK;
  }
} // namespace SurfaceRestoreOwnership

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_SURFACE_RESTORE_OWNERSHIP_HPP
