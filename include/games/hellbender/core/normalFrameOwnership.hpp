// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_NORMAL_FRAME_OWNERSHIP_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_NORMAL_FRAME_OWNERSHIP_HPP

struct NormalFrameState
{
  DWORD depth, armed, invalidated, generation, ownerThread, ownerSlot;
  DWORD continuation, outerReturn, incomplete, frameReturned;
};

static NormalFrameState normalFrameState = {};

namespace NormalFrameOwnership
{
  struct RestoreReceipt
  {
    DWORD phase, frameGeneration, previousEpoch, restoredEpoch;
    DWORD flushGeneration, entryStack, outerReturn, session;
  } restoreReceipt = {};

  static void ClearRestoreReceipt()
  {
    restoreReceipt.phase = 0;
  }

  static DWORD ownerBase = 0, ownerGeneration = 0, ownerLifecycle = 0, ownerFrame = 0,
               ownerEpoch = 0, ownerRestore = 0, ownerDraw = 0, ownerPrimary = 0,
               ownerBackbuffer = 0, ownerDevice = 0, ownerExecute = 0, ownerFailures = 0;

  static bool StackRead(DWORD address, DWORD *output, DWORD bytes)
  {
    const DWORD low = __readfsdword(8), high = __readfsdword(4);
    if ((address & 3) || (bytes & 3) || low >= high || address < low || address >= high ||
        bytes > high - address)
    {
      return false;
    }
    const auto input = reinterpret_cast<const volatile DWORD *>(address);
    for (DWORD index = 0; index < bytes / 4; ++index)
    {
      output[index] = input[index];
    }
    return true;
  }

  static bool Advance(DWORD &value)
  {
    if (value == 0xffffffffu)
    {
      normalFrameState.incomplete = 1;
      normalFrameState.armed = 0;
      return false;
    }
    ++value;
    return true;
  }

  static bool Phase()
  {
    return Configured() && !TextureLifecycle::Incomplete() && !normalFrameState.incomplete &&
           state.depth == 1 && !state.ended && !state.busy && !state.videoDepth &&
           !state.mainMenuDepth && !state.transitionDepth && !state.setupCleanupBusy &&
           !Read(GameRva::quit) && !Read(GameRva::mainMenuVisible) &&
           !Read(readinessNativePaused) && Read(readinessNativeActive) &&
           Read(GameRva::mode) == 2 && Read(GameRva::directDraw) && Read(GameRva::hardware) &&
           Read(GameRva::fullscreenActive) && !readinessState.busy && !movieRecoveryState.busy &&
           !preMovieRecoveryState.busy && !preMovieRecoveryState.scopeDepth;
  }

  static bool Unlocked()
  {
    return !Read(GameRva::drawLock) && !Read(GameRva::textureLock) && !Read(GameRva::drawSession);
  }

  static bool SameOwnerAtRestore(DWORD restored)
  {
    const auto &f = normalFrameState;
    return Phase() && f.depth == 1 && f.armed && !f.invalidated &&
           f.generation == ownerGeneration && ownerBase == config.gameBase &&
           f.ownerThread == TextureLifecycle::ThreadId() &&
           ownerLifecycle == TextureLifecycle::Serial() &&
           ownerFrame == TextureLifecycle::FrameSerial() &&
           ownerEpoch == TextureLifecycle::FrameEpoch() && restored == state.restoreSuccesses &&
           ownerDraw == Read(GameRva::drawObject) && ownerPrimary == Read(GameRva::primary) &&
           ownerBackbuffer == Read(GameRva::backbuffer) &&
           ownerDevice == Read(GameRva::legacyDevice) &&
           ownerExecute == Read(readinessExecuteBuffer);
  }

  static bool SameOwner()
  {
    return SameOwnerAtRestore(ownerRestore);
  }
} // namespace NormalFrameOwnership

extern "C" void __cdecl CompleteFrameRestoreLock(const DWORD *registers);

extern "C" void __cdecl BeginOwnedFrame(const DWORD *registers, DWORD continuation)
{
  using namespace NormalFrameOwnership;
  SurfaceRestoreOwnership::LastErrorScope error;
  auto &f = normalFrameState;
  ClearRestoreReceipt();
  const bool generation = Advance(f.generation);
  const bool depth = Advance(f.depth);
  f.armed = 0;
  f.frameReturned = 0;
  if (!generation || !depth || f.depth != 1)
  {
    f.invalidated = 1;
    return;
  }
  f.invalidated = 0;
  DWORD caller = 0;
  const DWORD entry = registers[3] <= 0xfffffffbu ? registers[3] + 4 : 0;
  if (!Phase() || state.restoreBusy || textureState.busy || loadState.busy ||
      TextureTransfer::textureTransferStats.busy ||
      TextureTransfer::textureTransferStats.incompleteFlags || !entry ||
      !StackRead(entry, &caller, 4) || caller != config.gameBase + readinessOuterFrameReturn)
  {
    return;
  }
  ownerBase = config.gameBase;
  ownerGeneration = f.generation;
  ownerLifecycle = TextureLifecycle::Serial();
  ownerFrame = TextureLifecycle::FrameSerial();
  ownerEpoch = TextureLifecycle::FrameEpoch();
  ownerRestore = state.restoreSuccesses;
  ownerDraw = Read(GameRva::drawObject);
  ownerPrimary = Read(GameRva::primary);
  ownerBackbuffer = Read(GameRva::backbuffer);
  ownerDevice = Read(GameRva::legacyDevice);
  ownerExecute = Read(readinessExecuteBuffer);
  ownerFailures = bridgeState.failures;
  f.ownerThread = TextureLifecycle::ThreadId();
  f.ownerSlot = entry - 4;
  f.continuation = continuation;
  f.outerReturn = caller;
  f.armed = 1;
}

extern "C" void __cdecl MarkOwnedFrameReturned(const DWORD *registers)
{
  SurfaceRestoreOwnership::LastErrorScope error;
  auto &f = normalFrameState;
  const DWORD entry = registers[3] <= 0xfffffffbu ? registers[3] + 4 : 0;
  DWORD caller = 0;
  f.frameReturned = 0;
  if (!f.armed)
  {
    return;
  }
  if (f.depth != 1 || !entry || entry - 4 != f.ownerSlot ||
      !NormalFrameOwnership::StackRead(entry, &caller, 4) || caller != f.outerReturn ||
      !NormalFrameOwnership::SameOwner())
  {
    f.invalidated = 1;
    f.armed = 0;
    return;
  }
  f.frameReturned = 1;
}

extern "C" void __cdecl EndOwnedFrame(const DWORD *)
{
  NormalFrameOwnership::ClearRestoreReceipt();
  auto &f = normalFrameState;
  f.armed = 0;
  if (f.depth)
  {
    --f.depth;
  }
}

#endif
