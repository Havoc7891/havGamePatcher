// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_QUAD_FRAME_RECOVERY_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_QUAD_FRAME_RECOVERY_HPP

struct QuadFrameRecoveryState
{
  DWORD busy, depth, armed, generation, ownerThread, ownerSlot, continuation, outerReturn;
  DWORD callerKind, returnStage, aborted, frameReturned, awaitingFrame, pendingPresent,
      invalidations, reentries;
  DWORD frames, normalFrames, attempts, successes, failures, skippedPresents, rejected,
      presentExpired;
  DWORD lastStage, lastResult, endSceneResult, primaryLost, backbufferLost, restoreResult,
      primaryAfter, backbufferAfter;
  DWORD restoreBefore, restoreAfter, lifecycle, uploadSerial, frameSerial, frameEpoch,
      textureGeneration, observations;
  DWORD primary, backbuffer, draw, device, executeBuffer, executeAllocation, executeCapacity,
      actualCount;
  DWORD failureStack, quadReturns, hudReturns, frameReturns, texturePassesBefore,
      texturePassesAfter, resetCalls, resetResult;
  DWORD lastError, rejectReason, guardIncomplete, qualifiedPresents, endCallsBefore, endCallsAfter,
      confirmedFrames, reserved;
};

static_assert(sizeof(QuadFrameRecoveryState) == 256, "Quad frame recovery ABI");
extern "C"
{
  QuadFrameRecoveryState quadFrameRecoveryState = {};
}

namespace QuadFrameRecovery
{
  constexpr DWORD executeBufferRva = recoveryExecuteBuffer,
                  allocationRva = recoveryExecuteAllocation;
  constexpr DWORD instructionBeginRva = recoveryExecuteInstructionBegin,
                  instructionEndRva = recoveryExecuteInstructionEnd;
  constexpr DWORD copyRecordRva = recoveryExecuteCopyRecord,
                  vertexPointerRva = recoveryExecuteVertexPointer,
                  actualCountRva = recoveryExecuteActualCount;
  constexpr DWORD capacityRva = recoveryExecuteCapacity, activeRva = recoveryNativeActive,
                  pausedRva = recoveryNativePausedAliasLegacyVideo;
  enum class Stage : DWORD
  {
    idle,
    frame,
    classify,
    query,
    unwind,
    restore,
    cache,
    reset,
    release,
    present,
    confirmed,
    fatal
  };
  enum class Reject : DWORD
  {
    none,
    configuration,
    phase,
    ownership,
    result,
    stack,
    resources,
    unavailable,
    notLost,
    changed,
    awaitingFrame,
    bookkeeping,
    cache,
    reset,
    incomplete
  };
  static DWORD ownerBase = 0, ownerGeneration = 0, ownerLifecycle = 0, ownerFrame = 0,
               ownerEpoch = 0;
  static DWORD ownerRestore = 0, ownerDraw = 0, ownerPrimary = 0, ownerBackbuffer = 0,
               ownerDevice = 0, ownerExecute = 0;
  static DWORD ownerFailures = 0, presentBase = 0, presentLifecycle = 0, presentEpoch = 0;
  static bool terminalScenePinned = false;
  static DWORD terminalSceneBegin = 0, terminalSceneEnd = 0, terminalSceneFailures = 0;

  static bool TerminalSceneCurrent()
  {
    return !terminalScenePinned ||
           (!bridgeState.sceneDepth && bridgeState.beginCalls == terminalSceneBegin &&
            bridgeState.endCalls == terminalSceneEnd &&
            bridgeState.failures == terminalSceneFailures &&
            bridgeState.lastStage == D3DBridge::end &&
            bridgeState.lastResult == static_cast<DWORD>(DDERR_SURFACELOST));
  }

  static void StageAt(Stage value)
  {
    quadFrameRecoveryState.lastStage = static_cast<DWORD>(value);
  }

  static DWORD Advance(DWORD &value)
  {
    if (value == 0xffffffffu)
    {
      quadFrameRecoveryState.guardIncomplete = 1;
      return 0;
    }
    return ++value;
  }

  static bool StackRead(DWORD address, DWORD *out, DWORD bytes)
  {
    const DWORD low = __readfsdword(8), high = __readfsdword(4);
    if ((address & 3) || (bytes & 3) || low >= high || address < low || address >= high ||
        bytes > high - address)
    {
      return false;
    }
    const auto in = reinterpret_cast<const volatile DWORD *>(address);
    for (DWORD i = 0; i < bytes / 4; ++i)
    {
      out[i] = in[i];
    }
    return true;
  }

  static bool Phase()
  {
    return Configured() && !TextureLifecycle::Incomplete() &&
           !quadFrameRecoveryState.guardIncomplete && state.depth == 1 && !state.ended &&
           !state.busy && !state.videoDepth && !state.mainMenuDepth && !state.transitionDepth &&
           !state.setupCleanupBusy && !Read(GameRva::quit) && !Read(GameRva::mainMenuVisible) &&
           !Read(pausedRva) && Read(activeRva) && Read(GameRva::mode) == 2 &&
           Read(GameRva::directDraw) && Read(GameRva::hardware) &&
           Read(GameRva::fullscreenActive) && !readinessState.busy && !movieRecoveryState.busy &&
           !preMovieRecoveryState.busy && !preMovieRecoveryState.scopeDepth;
  }

  static bool Unlocked()
  {
    return !Read(GameRva::drawLock) && !Read(GameRva::textureLock) && !Read(GameRva::drawSession);
  }

  static bool SameOwner()
  {
    auto &f = quadFrameRecoveryState;
    return Phase() && f.depth == 1 && f.armed && f.generation == ownerGeneration &&
           ownerBase == config.gameBase && f.ownerThread == TextureLifecycle::ThreadId() &&
           ownerLifecycle == TextureLifecycle::Serial() &&
           ownerFrame == TextureLifecycle::FrameSerial() &&
           ownerEpoch == TextureLifecycle::FrameEpoch() && ownerRestore == state.restoreSuccesses &&
           ownerDraw == Read(GameRva::drawObject) && ownerPrimary == Read(GameRva::primary) &&
           ownerBackbuffer == Read(GameRva::backbuffer) &&
           ownerDevice == Read(GameRva::legacyDevice) && ownerExecute == Read(executeBufferRva);
  }

  static DWORD RejectFailure(Reject reason)
  {
    quadFrameRecoveryState.rejectReason = static_cast<DWORD>(reason);
    ++quadFrameRecoveryState.rejected;
    return 0;
  }

  __declspec(noreturn) static void Fatal(Reject reason);
  using Reference = MovieRecovery::Reference;
  using Surface = MovieRecovery::Surface;
  using Lock = MovieRecovery::Lock;
  using Unlock = MovieRecovery::Unlock;
  using Flip = MovieRecovery::Flip;

  struct Context
  {
    bool captured, owned, invalidated;
    DWORD base, magic, version, observation, generation, thread, lifecycle, upload, frameSerial,
        frameEpoch;
    DWORD transferGeneration, readyGeneration, activeGeneration, transferIncomplete;
    DWORD transferNested, transferInvalidations, capacity, instructionOffset, vertexLimit;
    DWORD restoreEpoch, pendingEpoch, completedEpoch, passes;
    DWORD draw, primary, backbuffer, device, drawWindow, app, viewObject, frameObject;
    HWND frame, view;
    DWORD mode, hardware, directDraw, fullscreen, renderBpp, software, preference;
    DWORD mission, ended, content, stage, videoReady, videoBorrowed;
    DWORD tables[5], execute;
    Reference adds[5], drops[5];
    Surface restore, primaryLost, backbufferLost;
    DWORD executeLock;

    bool Current()
    {
      bool valid =
          captured && !invalidated && TerminalSceneCurrent() && config.gameBase == base &&
          config.magic == magic && config.version == version && SameOwner() &&
          (Unlocked() || UnlockFrameDrawLockCurrent()) && !loadState.busy && !textureState.busy &&
          !readinessState.busy && state.restoreBusy == static_cast<DWORD>(owned) &&
          !TextureTransfer::textureTransferStats.busy &&
          !TextureTransfer::textureTransferStats.incompleteFlags &&
          movieRecoveryState.observations == observation && !movieRecoveryState.reserved &&
          quadFrameRecoveryState.observations == generation && quadFrameRecoveryState.busy &&
          TextureTransfer::textureTransferStats.nestedEntries == transferNested &&
          TextureTransfer::textureTransferStats.invalidations == transferInvalidations &&
          Read(capacityRva) == capacity && Read(GameRva::instructionOffset) == instructionOffset &&
          Read(GameRva::vertexLimit) == vertexLimit &&
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
          tables[2] == *reinterpret_cast<DWORD *>(backbuffer) &&
          execute == Read(executeBufferRva) && tables[3] == *reinterpret_cast<DWORD *>(device) &&
          tables[4] == *reinterpret_cast<DWORD *>(execute);
      for (DWORD index = 0; valid && index < 5; ++index)
      {
        const auto methods = reinterpret_cast<const DWORD *>(tables[index]);
        valid = methods[1] == reinterpret_cast<DWORD>(adds[index]) &&
                methods[2] == reinterpret_cast<DWORD>(drops[index]);
      }
      if (valid)
      {
        const auto first = reinterpret_cast<const DWORD *>(tables[1]);
        const auto second = reinterpret_cast<const DWORD *>(tables[2]);
        valid = first[0x6c / 4] == reinterpret_cast<DWORD>(restore) &&
                first[0x60 / 4] == reinterpret_cast<DWORD>(primaryLost) &&
                second[0x60 / 4] == reinterpret_cast<DWORD>(backbufferLost) &&
                reinterpret_cast<const DWORD *>(tables[4])[4] == executeLock &&
                UnlockFrameContextCurrent();
      }
      if (!valid && !invalidated)
      {
        invalidated = true;
        ++quadFrameRecoveryState.invalidations;
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

  static Context context = {};
  static DWORD retained = 0;

  static void ResetContext()
  {
    terminalScenePinned = false;
    auto bytes = reinterpret_cast<volatile BYTE *>(&context);
    for (DWORD i = 0; i < sizeof(context); ++i)
    {
      bytes[i] = 0;
    }
    retained = 0;
  }

  static bool CaptureContext()
  {
    context.base = config.gameBase;
    context.magic = config.magic;
    context.version = config.version;
    context.observation = movieRecoveryState.observations;
    context.generation = quadFrameRecoveryState.observations;
    context.transferGeneration = TextureTransfer::textureTransferStats.generation;
    context.transferNested = TextureTransfer::textureTransferStats.nestedEntries;
    context.transferInvalidations = TextureTransfer::textureTransferStats.invalidations;
    context.capacity = Read(capacityRva);
    context.instructionOffset = Read(GameRva::instructionOffset);
    context.vertexLimit = Read(GameRva::vertexLimit);
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
    context.execute = Read(executeBufferRva);
    context.drawWindow = Read(GameRva::drawWindow);
    context.app = Read(GameRva::app);
    context.viewObject = Read(GameRva::view);
    if (!context.draw || !context.primary || !context.backbuffer ||
        context.primary == context.backbuffer || !context.device || !context.execute ||
        !context.app || !context.viewObject)
    {
      return false;
    }
    context.frameObject = *reinterpret_cast<DWORD *>(context.app + 0x1c);
    context.frame = FrameWindow();
    context.view = ViewWindow();
    if (!context.frameObject || !context.frame || !context.view)
    {
      return false;
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
    const DWORD objects[5] = {context.draw, context.primary, context.backbuffer, context.device,
                              context.execute};
    for (DWORD index = 0; index < 5; ++index)
    {
      const auto methods = *reinterpret_cast<DWORD **>(objects[index]);
      if (!methods || !methods[1] || !methods[2])
      {
        return false;
      }
      context.tables[index] = reinterpret_cast<DWORD>(methods);
      context.adds[index] = reinterpret_cast<Reference>(methods[1]);
      context.drops[index] = reinterpret_cast<Reference>(methods[2]);
    }
    const auto primary = reinterpret_cast<const DWORD *>(context.tables[1]);
    const auto backbuffer = reinterpret_cast<const DWORD *>(context.tables[2]);
    if (!primary[0x6c / 4] || !primary[0x60 / 4] || !backbuffer[0x60 / 4] ||
        !reinterpret_cast<const DWORD *>(context.tables[4])[4])
    {
      return false;
    }
    context.restore = reinterpret_cast<Surface>(primary[0x6c / 4]);
    context.primaryLost = reinterpret_cast<Surface>(primary[0x60 / 4]);
    context.backbufferLost = reinterpret_cast<Surface>(backbuffer[0x60 / 4]);
    context.executeLock = reinterpret_cast<const DWORD *>(context.tables[4])[4];
    context.captured = true;
    return context.Current();
  }

  static DWORD Object(DWORD index)
  {
    const DWORD values[5] = {context.draw, context.primary, context.backbuffer, context.device,
                             context.execute};
    return values[index];
  }

  static bool Release()
  {
    bool valid = true;
    while (retained)
    {
      const DWORD index = --retained;
      context.drops[index](reinterpret_cast<void *>(Object(index)));
      valid = context.Windows() && valid;
    }
    return valid;
  }

  __declspec(noreturn) static void Fatal(Reject reason)
  {
    auto &f = quadFrameRecoveryState;
    const DWORD error = f.lastError;
    const DWORD fatalBase = ownerBase;
    const DWORD fatalMessage =
        f.callerKind == 5
            ? recoveryUnlockFatalMessage
            : (f.callerKind == 3 ? relockNativeFatalMessage : recoveryEndSceneFatalMessage);
    f.rejectReason = static_cast<DWORD>(reason);
    ++f.failures;
    f.guardIncomplete = 1;
    StageAt(Stage::fatal);
    // Drop only references acquired by this transaction. Do not resume drawing.
    while (retained)
    {
      const DWORD index = --retained;
      context.drops[index](reinterpret_cast<void *>(Object(index)));
    }
    if (context.owned)
    {
      state.restoreBusy = 0;
      context.owned = false;
    }
    __writefsdword(0x34, error);
    reinterpret_cast<void(__cdecl *)(const char *)>(fatalBase + GameRva::fatalError)(
        reinterpret_cast<const char *>(fatalBase + fatalMessage));
    __assume(0);
  }

  static bool Retain()
  {
    for (DWORD i = 0; i < 5; ++i)
    {
      ++retained;
      context.adds[i](reinterpret_cast<void *>(Object(i)));
      if (!context.Windows())
      {
        return false;
      }
    }
    return true;
  }

  static bool SurfaceCall(Surface method, DWORD object, DWORD &result)
  {
    if (!context.Windows())
    {
      return false;
    }
    result = static_cast<DWORD>(method(reinterpret_cast<void *>(object)));
    return context.Windows();
  }
} // namespace QuadFrameRecovery

extern "C" void __cdecl BeginRecoveryFrame(const DWORD *registers, DWORD continuation)
{
  using namespace QuadFrameRecovery;
  auto &f = quadFrameRecoveryState;
  const DWORD error = __readfsdword(0x34);
  if (f.busy || f.aborted)
  {
    Fatal(Reject::ownership);
  }
  Advance(f.generation);
  Advance(f.depth);
  ++f.frames;
  if (f.depth != 1)
  {
    f.armed = 0;
    ++f.reentries;
    __writefsdword(0x34, error);
    return;
  }
  if (f.pendingPresent)
  {
    f.pendingPresent = 0;
    ++f.presentExpired;
  }
  f.armed = 0;
  f.frameReturned = 0;
  DWORD caller = 0;
  const DWORD entry = registers[3] + 4;
  if (!Phase() || state.restoreBusy || textureState.busy || loadState.busy ||
      TextureTransfer::textureTransferStats.busy ||
      TextureTransfer::textureTransferStats.incompleteFlags || entry < 4 ||
      !StackRead(entry, &caller, 4) || caller != config.gameBase + recoveryOuterFrameReturn)
  {
    __writefsdword(0x34, error);
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
  ownerExecute = Read(executeBufferRva);
  ownerFailures = bridgeState.failures;
  f.ownerThread = TextureLifecycle::ThreadId();
  f.ownerSlot = entry - 4;
  f.continuation = continuation;
  f.outerReturn = caller;
  f.endCallsBefore = bridgeState.endCalls;
  f.returnStage = 0;
  f.callerKind = 0;
  f.armed = 1;
  StageAt(Stage::frame);
  __writefsdword(0x34, error);
}

extern "C" DWORD __cdecl TryAbortQuadFrame(const DWORD *registers)
{
  using namespace QuadFrameRecovery;
  auto &f = quadFrameRecoveryState;
  const DWORD error = __readfsdword(0x34);
  Advance(f.observations);
  if (f.busy || f.aborted)
  {
    ++f.reentries;
    return RejectFailure(Reject::ownership);
  }
  f.endSceneResult = registers[7];
  f.lastResult = registers[7];
  f.lastError = error;
  const DWORD stack = registers[3] + 4;
  DWORD slots[47];
  const bool readable = Configured() && StackRead(stack, slots, sizeof(slots));
  auto decline = [&](Reject why) -> DWORD
  {
    RejectFailure(why);
    __writefsdword(0x34, error);
    return 0;
  };
  StageAt(Stage::classify);
  if (!Configured())
  {
    return decline(Reject::configuration);
  }
  if (f.awaitingFrame || f.attempts != f.successes || f.confirmedFrames != f.successes)
  {
    return decline(Reject::awaitingFrame);
  }
  if (!SameOwner() || !Unlocked() || state.restoreBusy || textureState.busy || loadState.busy ||
      TextureTransfer::textureTransferStats.busy ||
      TextureTransfer::textureTransferStats.incompleteFlags)
  {
    return decline(Reject::ownership);
  }
  if (f.endSceneResult != static_cast<DWORD>(DDERR_SURFACELOST) || bridgeState.lastStage != 8 ||
      bridgeState.lastResult != f.endSceneResult)
  {
    return decline(Reject::result);
  }
  const bool cleanup = readable && slots[2] == config.gameBase + recoveryFlushCleanup &&
                       slots[3] == config.gameBase + recoveryCpuRowsCleanup;
  const bool quad = cleanup && stack <= 0xffffff4bu && stack + 0xb4 == f.ownerSlot &&
                    slots[4] == config.gameBase + recoveryQuadGate &&
                    slots[23] == config.gameBase + recoveryHudGate &&
                    slots[24] == config.gameBase + recoveryFrameGate &&
                    slots[45] == f.continuation && slots[46] == f.outerReturn;
  const bool world = cleanup && stack <= 0xffffff9bu && stack + 0x64 == f.ownerSlot &&
                     slots[4] == config.gameBase + recoveryWorldGate &&
                     slots[25] == f.continuation && slots[26] == f.outerReturn;
  // The terminal native flush has the same owned frame layout as the
  // earlier world flush, but returns directly to ADD ESP,50h / RET.
  const bool terminal = cleanup && stack <= 0xffffff9bu && stack + 0x64 == f.ownerSlot &&
                        slots[4] == config.gameBase + recoveryFrameAbortReturn &&
                        slots[25] == f.continuation && slots[26] == f.outerReturn;
  if (!quad && !world && !terminal)
  {
    return decline(Reject::stack);
  }
  f.failureStack = stack;
  f.primary = ownerPrimary;
  f.backbuffer = ownerBackbuffer;
  f.draw = ownerDraw;
  f.device = ownerDevice;
  f.executeBuffer = ownerExecute;
  f.executeAllocation = Read(allocationRva);
  f.executeCapacity = Read(capacityRva);
  f.actualCount = Read(actualCountRva);
  f.restoreBefore = state.restoreSuccesses;
  f.lifecycle = ownerLifecycle;
  f.frameSerial = ownerFrame;
  if (!ownerDraw || !ownerPrimary || !ownerBackbuffer || ownerPrimary == ownerBackbuffer ||
      !ownerDevice || !ownerExecute || Read(GameRva::renderBpp) != 16 ||
      Read(recoveryNativeSurfaceWidth) != 640 || Read(recoveryNativeSurfaceHeight) != 480 ||
      Read(GameRva::instructionOffset) != 0x20020 || f.executeCapacity < 0x20038 ||
      f.executeCapacity > 0x40000)
  {
    return decline(Reject::resources);
  }
  if (ownerEpoch == 0xffffffffu)
  {
    return decline(Reject::incomplete);
  }
  // A terminal flush must retain its actual failed scene through all
  // callback-capable ownership/recovery work, including native cleanup.
  if (terminal && (bridgeState.sceneDepth || bridgeState.beginCalls != bridgeState.endCalls ||
                   bridgeState.endCalls <= f.endCallsBefore))
  {
    return decline(Reject::bookkeeping);
  }
  f.busy = 1;
  ResetContext();
  if (terminal)
  {
    terminalSceneBegin = bridgeState.beginCalls;
    terminalSceneEnd = bridgeState.endCalls;
    terminalSceneFailures = bridgeState.failures;
    terminalScenePinned = true;
  }
  const DWORD before = ownerEpoch;
  AdvanceTextureFrameEpoch();
  ownerEpoch = TextureLifecycle::FrameEpoch();
  f.frameEpoch = ownerEpoch;
  if (ownerEpoch != before + 1 || !CaptureContext())
  {
    f.busy = 0;
    return decline(Reject::changed);
  }
  context.owned = true;
  state.restoreBusy = 1;
  f.uploadSerial = context.upload;
  f.textureGeneration = context.transferGeneration;
  StageAt(Stage::query);
  bool valid = context.Windows() && Retain() &&
               SurfaceCall(context.primaryLost, context.primary, f.primaryLost) &&
               SurfaceCall(context.backbufferLost, context.backbuffer, f.backbufferLost);
  const DWORD lost = static_cast<DWORD>(DDERR_SURFACELOST);
  const bool chainLost = (f.primaryLost == lost || f.backbufferLost == lost) &&
                         (f.primaryLost == S_OK || f.primaryLost == lost) &&
                         (f.backbufferLost == S_OK || f.backbufferLost == lost);
  if (!valid || !chainLost)
  {
    const bool released = Release();
    state.restoreBusy = 0;
    context.owned = false;
    f.busy = 0;
    return decline(valid && released ? Reject::notLost : Reject::changed);
  }
  f.aborted = 1;
  f.callerKind = terminal ? 4 : (world ? 2 : 1);
  f.returnStage = 0;
  ++f.attempts;
  f.rejectReason = 0;
  StageAt(Stage::unwind);
  __writefsdword(0x34, error);
  return 1;
}

namespace QuadFrameRecovery
{
  static bool ReturnOwned(const DWORD *registers,
                          DWORD delta,
                          DWORD stage,
                          DWORD *slots,
                          DWORD callerKind = 1,
                          DWORD adjustment = 4)
  {
    auto &f = quadFrameRecoveryState;
    const DWORD stack = registers[3] + adjustment;
    return f.aborted && f.callerKind == callerKind && f.returnStage == stage && !f.frameReturned &&
           stack <= 0xffffffffu - delta - 8 && stack + delta == f.ownerSlot &&
           StackRead(stack, slots, delta + 8) && slots[delta / 4] == f.continuation &&
           slots[delta / 4 + 1] == f.outerReturn && context.Current();
  }
} // namespace QuadFrameRecovery

extern "C" DWORD __cdecl SelectWorldFrameReturn(const DWORD *registers)
{
  using namespace QuadFrameRecovery;
  auto &f = quadFrameRecoveryState;
  if (!f.aborted)
  {
    return config.gameBase + recoveryWorldNormalResume;
  }
  const DWORD error = __readfsdword(0x34);
  DWORD slots[22];
  if (!Configured() || config.gameBase != ownerBase || config.magic != context.magic ||
      config.version != context.version || !SameOwner())
  {
    Fatal(Reject::ownership);
  }
  // Native flush and CPU-row cleanup have completed before this world-only gate.
  if (Read(GameRva::renderBpp) != 8 || context.renderBpp != 16)
  {
    Fatal(Reject::bookkeeping);
  }
  context.renderBpp = 8;
  if (!ReturnOwned(registers, 0x50, 0, slots, 2))
  {
    Fatal(Reject::bookkeeping);
  }
  // Abandon this frame's remaining updates and drawing without replaying the suffix.
  f.returnStage = 4;
  ++f.frameReturns;
  __writefsdword(0x34, error);
  return ownerBase + recoveryFrameAbortReturn;
}

extern "C" DWORD __cdecl SelectQuadFlushReturn(const DWORD *registers)
{
  using namespace QuadFrameRecovery;
  auto &f = quadFrameRecoveryState;
  if (!f.aborted)
  {
    return config.gameBase + recoveryQuadNormalResume;
  }
  const DWORD error = __readfsdword(0x34);
  DWORD slots[42];
  if (!Configured() || config.gameBase != ownerBase || config.magic != context.magic ||
      config.version != context.version || !SameOwner())
  {
    Fatal(Reject::ownership);
  }
  // The preserved native cleanup has now deliberately restored 8-bit CPU rows.
  if (Read(GameRva::renderBpp) != 8 || context.renderBpp != 16)
  {
    Fatal(Reject::bookkeeping);
  }
  context.renderBpp = 8;
  if (!ReturnOwned(registers, 0xa0, 0, slots) || slots[18] != ownerBase + recoveryHudGate ||
      slots[19] != ownerBase + recoveryFrameGate)
  {
    Fatal(Reject::bookkeeping);
  }
  f.returnStage = 1;
  ++f.quadReturns;
  __writefsdword(0x34, error);
  return ownerBase + recoveryQuadAbortReturn;
}

extern "C" DWORD __cdecl SelectQuadHudReturn(const DWORD *registers)
{
  using namespace QuadFrameRecovery;
  auto &f = quadFrameRecoveryState;
  if (!f.aborted)
  {
    return config.gameBase + recoveryHudNormalResume;
  }
  const DWORD error = __readfsdword(0x34);
  DWORD slots[23];
  if (!ReturnOwned(registers, 0x54, 1, slots) || slots[0] != ownerBase + recoveryFrameGate)
  {
    Fatal(Reject::bookkeeping);
  }
  f.returnStage = 2;
  ++f.hudReturns;
  __writefsdword(0x34, error);
  return ownerBase + recoveryHudAbortReturn;
}

extern "C" DWORD __cdecl SelectQuadFrameReturn(const DWORD *registers)
{
  using namespace QuadFrameRecovery;
  auto &f = quadFrameRecoveryState;
  if (!f.aborted)
  {
    return 0;
  }
  const DWORD error = __readfsdword(0x34);
  DWORD slots[22];
  if (!ReturnOwned(registers, 0x50, 2, slots))
  {
    Fatal(Reject::bookkeeping);
  }
  f.returnStage = 3;
  ++f.frameReturns;
  __writefsdword(0x34, error);
  return ownerBase + recoveryFrameAbortReturn;
}

extern "C" void __cdecl CallQuadReset();

namespace QuadFrameRecovery
{
  static DWORD resetActive = 0, resetStage = 0, resetSlot = 0, resetContinuation = 0,
               resetOuterReturn = 0;

  static bool Writable(DWORD address, DWORD size)
  {
    if (!address || !size || address > 0xffffffffu - size || !context.Windows())
    {
      return false;
    }
    const auto module = Call(GetModuleHandleA, getModuleHandleA)("kernel32.dll");
    if (!context.Windows() || !module)
    {
      return false;
    }
    using Query = SIZE_T(WINAPI *)(const void *, PMEMORY_BASIC_INFORMATION, SIZE_T);
    const auto query =
        reinterpret_cast<Query>(Call(GetProcAddress, getProcAddress)(module, "VirtualQuery"));
    if (!context.Windows() || !query)
    {
      return false;
    }
    DWORD cursor = address;
    const DWORD end = address + size;
    for (DWORD regions = 0; cursor < end && regions < 32; ++regions)
    {
      MEMORY_BASIC_INFORMATION info;
      auto bytes = reinterpret_cast<volatile BYTE *>(&info);
      for (DWORD i = 0; i < sizeof(info); ++i)
      {
        bytes[i] = 0;
      }
      const SIZE_T count = query(reinterpret_cast<const void *>(cursor), &info, sizeof(info));
      if (!context.Windows() || count != sizeof(info) || info.State != MEM_COMMIT ||
          (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
          !(info.Protect &
            (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
      {
        return false;
      }
      const DWORD start = reinterpret_cast<DWORD>(info.BaseAddress);
      if (!info.RegionSize || start > cursor || info.RegionSize > 0xffffffffu - start ||
          start + info.RegionSize <= cursor)
      {
        return false;
      }
      cursor = start + static_cast<DWORD>(info.RegionSize);
    }
    return cursor >= end;
  }
} // namespace QuadFrameRecovery

extern "C" void __cdecl BeginQuadReset(const DWORD *registers, DWORD continuation)
{
  using namespace QuadFrameRecovery;
  const DWORD error = __readfsdword(0x34);
  const DWORD stack = registers[3] + 4;
  if (resetActive || !context.Current() || !StackRead(stack, &resetOuterReturn, 4) || stack < 4)
  {
    Fatal(Reject::reset);
  }
  resetActive = 1;
  resetStage = 0;
  resetSlot = stack - 4;
  resetContinuation = continuation;
  __writefsdword(0x34, error);
}

extern "C" void __cdecl CheckQuadResetReturn(const DWORD *registers)
{
  using namespace QuadFrameRecovery;
  if (!resetActive)
  {
    return;
  }
  const DWORD error = __readfsdword(0x34);
  const DWORD stack = registers[3] + 4;
  DWORD slots[10];
  if (resetStage || !context.Current() || !quadFrameRecoveryState.aborted || stack > 0xffffffd7u ||
      stack + 0x20 != resetSlot || !StackRead(stack, slots, sizeof(slots)) ||
      slots[8] != resetContinuation || slots[9] != resetOuterReturn)
  {
    Fatal(Reject::reset);
  }
  resetStage = 1;
  const DWORD allocation = slots[7];
  if (slots[3] != 20 || context.instructionOffset != 0x20020 || context.capacity < 0x20060 ||
      context.capacity > 0x40000 || (allocation & 3) || !Writable(allocation, context.capacity) ||
      !context.Current())
  {
    Fatal(Reject::reset);
  }
  DWORD current[10];
  if (!StackRead(stack, current, sizeof(current)))
  {
    Fatal(Reject::reset);
  }
  for (DWORD i = 0; i < 10; ++i)
  {
    if (current[i] != slots[i])
    {
      Fatal(Reject::reset);
    }
  }
  __writefsdword(0x34, error);
}

extern "C" void __cdecl FinishQuadReset(const DWORD *registers)
{
  using namespace QuadFrameRecovery;
  const DWORD error = __readfsdword(0x34);
  const DWORD stack = registers[3] + 4;
  DWORD caller = 0;
  if (!resetActive || resetStage != 1 || stack != resetSlot + 4 || !StackRead(stack, &caller, 4) ||
      caller != resetOuterReturn || !context.Current())
  {
    Fatal(Reject::reset);
  }
  resetActive = 0;
  __writefsdword(0x34, error);
}

namespace QuadFrameRecovery
{
  static bool Delta(DWORD before, DWORD after, DWORD amount)
  {
    return before <= 0xffffffffu - amount && after == before + amount;
  }

  static bool Repair()
  {
    auto &f = quadFrameRecoveryState;
    if (!context.Windows() || ownerRestore == 0xffffffffu)
    {
      return false;
    }
    StageAt(Stage::restore);
    ++state.restoreAttempts;
    const bool restored = SurfaceCall(context.restore, context.primary, f.restoreResult);
    state.lastRestoreResult = f.restoreResult;
    if (!restored || f.restoreResult != S_OK)
    {
      ++state.restoreFailures;
      return false;
    }
    if (!SurfaceCall(context.primaryLost, context.primary, f.primaryAfter) ||
        !SurfaceCall(context.backbufferLost, context.backbuffer, f.backbufferAfter) ||
        f.primaryAfter != S_OK || f.backbufferAfter != S_OK)
    {
      ++state.restoreFailures;
      return false;
    }
    state.lastPrimaryIsLost = f.primaryAfter;
    state.lastBackbufferIsLost = f.backbufferAfter;
    ++state.restoreSuccesses;
    ownerRestore = state.restoreSuccesses;
    context.restoreEpoch = ownerRestore;
    f.restoreAfter = ownerRestore;
    Write(GameRva::cockpitFullRedrawCount, 8);
    context.owned = false;
    state.restoreBusy = 0;
    if (!context.Windows() || !DisplayReadiness::Wait(DisplayReadiness::Phase::textureRecovery) ||
        !context.Windows())
    {
      return false;
    }
    StageAt(Stage::cache);
    const DWORD pass = textureState.passes, success = textureState.successes,
                failure = textureState.failures, reloaded = textureState.texturesReloaded;
    const DWORD upload = context.upload, gen = context.transferGeneration,
                ready = context.readyGeneration;
    const DWORD captures = TextureTransfer::textureTransferStats.captures,
                loads = TextureTransfer::textureTransferStats.loads;
    const DWORD nested = TextureTransfer::textureTransferStats.nestedEntries,
                invalid = TextureTransfer::textureTransferStats.invalidations;
    const DWORD staged = TextureTransfer::textureTransferStats.staged,
                admitted = TextureTransfer::textureTransferStats.admitted,
                copied = TextureTransfer::textureTransferStats.copied,
                skipped = TextureTransfer::textureTransferStats.skipped,
                failed = TextureTransfer::textureTransferStats.failed;
    f.texturePassesBefore = pass;
    if (!RecoverLostTextures())
    {
      return false;
    }
    const DWORD reloads = textureState.texturesReloaded - reloaded;
    const DWORD captured = TextureTransfer::textureTransferStats.captures - captures;
    if (!SameOwner() || !Unlocked() || !Delta(pass, textureState.passes, 1) ||
        !Delta(success, textureState.successes, 1) || textureState.failures != failure ||
        textureState.texturesReloaded < reloaded || reloads > 1024 ||
        !Delta(upload, TextureLifecycle::UploadSerial(), reloads) ||
        textureState.completedEpoch != ownerRestore || textureState.pendingEpoch != ownerRestore ||
        textureState.lastResult != S_OK || textureState.busy || loadState.busy ||
        state.restoreBusy || TextureTransfer::textureTransferStats.busy ||
        TextureTransfer::textureTransferStats.incompleteFlags ||
        TextureTransfer::textureTransferStats.nestedEntries != nested ||
        TextureTransfer::textureTransferStats.captures < captures || captured != reloads ||
        !Delta(loads, TextureTransfer::textureTransferStats.loads, reloads) ||
        TextureTransfer::textureTransferStats.activeGeneration != 0 ||
        TextureTransfer::textureTransferStats.readyGeneration != (reloads ? 0 : ready) ||
        !Delta(staged, TextureTransfer::textureTransferStats.staged, reloads) ||
        !Delta(admitted, TextureTransfer::textureTransferStats.admitted, reloads) ||
        !Delta(copied, TextureTransfer::textureTransferStats.copied, reloads) ||
        TextureTransfer::textureTransferStats.skipped != skipped ||
        TextureTransfer::textureTransferStats.failed != failed ||
        !Delta(gen, TextureTransfer::textureTransferStats.generation, captured + reloads) ||
        !Delta(invalid, TextureTransfer::textureTransferStats.invalidations,
               (ready && captured) ? 1 : 0))
    {
      return false;
    }
    // Only the independently checked cache pass may advance upload ownership.
    context.upload = TextureLifecycle::UploadSerial();
    context.transferGeneration = TextureTransfer::textureTransferStats.generation;
    context.transferInvalidations = TextureTransfer::textureTransferStats.invalidations;
    context.readyGeneration = TextureTransfer::textureTransferStats.readyGeneration;
    context.activeGeneration = TextureTransfer::textureTransferStats.activeGeneration;
    context.pendingEpoch = textureState.pendingEpoch;
    context.completedEpoch = textureState.completedEpoch;
    context.passes = textureState.passes;
    f.uploadSerial = context.upload;
    f.textureGeneration = context.transferGeneration;
    f.texturePassesAfter = context.passes;
    if (!context.Windows())
    {
      return false;
    }
    StageAt(Stage::reset);
    ++f.resetCalls;
    CallQuadReset();
    if (!context.Windows())
    {
      return false;
    }
    const DWORD allocation = Read(allocationRva), begin = Read(instructionBeginRva);
    const bool reset = context.Windows() && allocation && allocation <= 0xffffffffu - 0x20060 &&
                       begin == allocation + 0x20020 && Read(instructionEndRva) == begin + 64 &&
                       Read(copyRecordRva) == begin + 4 && Read(vertexPointerRva) == allocation &&
                       !Read(actualCountRva) && Read(executeBufferRva) == context.execute &&
                       state.restoreSuccesses == ownerRestore &&
                       textureState.completedEpoch == ownerRestore;
    f.resetResult = reset ? S_OK : static_cast<DWORD>(E_UNEXPECTED);
    return reset;
  }
} // namespace QuadFrameRecovery

extern "C" void __cdecl MarkFrameReturned(const DWORD *registers)
{
  using namespace QuadFrameRecovery;
  auto &f = quadFrameRecoveryState;
  DWORD caller = 0;
  const DWORD stack = registers[3] + 4;
  if (!f.armed)
  {
    return;
  }
  const bool valid = f.depth == 1 && stack == f.ownerSlot + 4 && StackRead(stack, &caller, 4) &&
                     caller == f.outerReturn && SameOwner();
  if (!valid)
  {
    if (f.aborted)
    {
      Fatal(Reject::ownership);
    }
    f.armed = 0;
    return;
  }
  if (f.aborted && (f.callerKind == 4 || f.callerKind == 5))
  {
    // No injected jump is needed for the terminal flush. Native flush cleanup
    // and CPU row rebuilding have returned through the exact frame epilogue.
    // Accept that transition only at this owned wrapper return.
    if (f.returnStage != 0 || Read(GameRva::renderBpp) != 8 || context.renderBpp != 16 ||
        !Unlocked())
    {
      Fatal(Reject::bookkeeping);
    }
    context.renderBpp = 8;
    if (!context.Current())
    {
      Fatal(Reject::ownership);
    }
    f.returnStage = f.callerKind == 5 ? 7 : 6;
    ++f.frameReturns;
  }
  f.frameReturned = 1;
}

extern "C" void __cdecl FinishRecoveryFrame()
{
  using namespace QuadFrameRecovery;
  auto &f = quadFrameRecoveryState;
  const DWORD error = __readfsdword(0x34);
  if (f.aborted)
  {
    const bool completedRoute =
        (f.callerKind == 1 && f.returnStage == 3) || (f.callerKind == 2 && f.returnStage == 4) ||
        (f.callerKind == 3 && f.returnStage == 5) || (f.callerKind == 4 && f.returnStage == 6) ||
        (f.callerKind == 5 && f.returnStage == 7);
    if (!f.frameReturned || !completedRoute || !context.Windows() || context.renderBpp != 8)
    {
      Fatal(Reject::bookkeeping);
    }
    const bool repaired = Repair();
    StageAt(Stage::release);
    const bool released = Release();
    if (!repaired || !released || !context.Windows())
    {
      Fatal(repaired ? Reject::changed : Reject::cache);
    }
    terminalScenePinned = false;
    CompleteUnlockFrame();
    ++f.successes;
    f.aborted = 0;
    f.busy = 0;
    f.awaitingFrame = 1;
    f.pendingPresent = 1;
    presentBase = ownerBase;
    presentLifecycle = ownerLifecycle;
    presentEpoch = ownerEpoch;
    f.lastResult = S_OK;
    StageAt(Stage::present);
  }
  else if (f.armed && f.frameReturned && SameOwner() && Unlocked() && Read(GameRva::renderBpp) == 8)
  {
    ++f.normalFrames;
    if (f.awaitingFrame && bridgeState.endCalls > f.endCallsBefore &&
        bridgeState.failures == ownerFailures && bridgeState.lastStage == 8 &&
        bridgeState.lastResult == S_OK)
    {
      f.awaitingFrame = 0;
      ++f.confirmedFrames;
      f.endCallsAfter = bridgeState.endCalls;
      StageAt(Stage::confirmed);
    }
    HardwareCompletionFinishFrame();
    FailedPresentationFinishFrame();
  }
  f.armed = 0;
  if (f.depth)
  {
    --f.depth;
  }
  __writefsdword(0x34, error);
}

extern "C" int __cdecl PresentRecoveryFrame()
{
  using namespace QuadFrameRecovery;
  auto &f = quadFrameRecoveryState;
  const DWORD error = __readfsdword(0x34);
  if (f.aborted || f.busy)
  {
    Fatal(Reject::ownership);
  }
  if (f.pendingPresent)
  {
    const bool valid =
        Phase() && !f.depth && !f.busy && !f.aborted && f.awaitingFrame &&
        presentBase == config.gameBase && presentLifecycle == TextureLifecycle::Serial() &&
        presentEpoch == TextureLifecycle::FrameEpoch() &&
        f.ownerThread == TextureLifecycle::ThreadId() && ownerPrimary == Read(GameRva::primary) &&
        ownerBackbuffer == Read(GameRva::backbuffer) &&
        ownerDevice == Read(GameRva::legacyDevice) && ownerExecute == Read(executeBufferRva) &&
        ownerRestore == state.restoreSuccesses;
    f.pendingPresent = 0;
    if (!valid)
    {
      ++f.presentExpired;
      Fatal(Reject::ownership);
    }
    ++f.skippedPresents;
    ++f.qualifiedPresents;
    __writefsdword(0x34, error);
    reinterpret_cast<void(__cdecl *)()>(presentBase + recoveryMessagePumpOriginal)();
    return 0;
  }
  __writefsdword(0x34, error);
  const DWORD presentationToken =
      BeginFailedPresentationScope(reinterpret_cast<const DWORD *>(_AddressOfReturnAddress()));
  const int result = CallFailedPresentationOriginal();
  EndFailedPresentationScope(presentationToken);
  return result;
}

extern "C" __declspec(naked) void __cdecl FrameHook()
{
  __asm {
    pushfd
    pushad
    cld
    mov ebp,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    push ebp
    call PrepareFrameRecovery
    add esp,4
    test eax,eax
    jz RecoveryBusy
    call AdvanceTextureFrame
    push offset Returned
    push ebp
    call BeginOwnedFrame
    add esp,8
    push offset Returned
    push ebp
    call BeginRecoveryFrame
    add esp,8
    call HardwareCompletionBeginFrame
    push ebp
    call SoftwareCompletionBeginFrame
    add esp,4
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    call dword ptr [config+16]
  Returned:
    pushfd
    pushad
    cld
    mov ebp,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    push ebp
    call MarkFrameReturned
    add esp,4
    push ebp
    call MarkOwnedFrameReturned
    add esp,4
    push ebp
    call SoftwareCompletionFinishFrame
    add esp,4
    call FinishRecoveryFrame
    push ebp
    call EndOwnedFrame
    add esp,4
    call PostRestoreAfterFrame
    call AfterFrame
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    ret
  RecoveryBusy:
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    ret
  }
}

// Per-invocation stack slots retain branch targets across callbacks.
extern "C" __declspec(naked) void WorldFrameReturnHook()
{
  // Both paths replay the removed read-only MOV without changing flags.
  // An accepted abort still owns the validated image and native context.
  __asm {
    push 0
    pushfd
    pushad
    add dword ptr [esp+12],4
    cld
    mov ebp,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    push ebp
    call SelectWorldFrameReturn
    add esp,4
    mov dword ptr [ebp+36],eax
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    mov eax,dword ptr [config+8]
    mov eax,dword ptr [eax+recoveryWorldModeFlag]
    ret
  }
}

extern "C" __declspec(naked) void QuadFlushReturnHook()
{
  __asm {
    push 0
    pushfd
    pushad
    add dword ptr [esp+12],4
    cld
    mov ebp,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    push ebp
    call SelectQuadFlushReturn
    add esp,4
    mov dword ptr [ebp+36],eax
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    push eax
    mov eax,dword ptr [config+8]
    cmp dword ptr [eax+recoveryQuadOptionalRelockConditionAliasDirectDraw],esi
    pop eax
    ret
  }
}

extern "C" __declspec(naked) void QuadHudReturnHook()
{
  __asm {
    push 0
    pushfd
    pushad
    add dword ptr [esp+12],4
    cld
    mov ebp,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    push ebp
    call SelectQuadHudReturn
    add esp,4
    mov dword ptr [ebp+36],eax
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    mov eax,dword ptr [config+8]
    mov eax,dword ptr [eax+recoveryHudNativeLoadedValue]
    ret
  }
}

extern "C" __declspec(naked) void QuadFrameReturnHook()
{
  __asm {
    push 0
    push 0
    pushfd
    pushad
    add dword ptr [esp+12],8
    cld
    mov ebp,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    push ebp
    call SelectQuadFrameReturn
    add esp,4
    test eax,eax
    jnz Aborted
    mov eax,dword ptr [config+8]
    lea ecx,[eax+recoveryFrameNormalCallee]
    mov dword ptr [ebp+36],ecx
    lea ecx,[eax+recoveryFrameNormalContinuation]
    mov dword ptr [ebp+40],ecx
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    ret
  Aborted:
    mov dword ptr [ebp+40],eax
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    lea esp,[esp+4]
    ret
  }
}

extern "C" __declspec(naked) void __cdecl CallQuadReset()
{
  __asm {
    pushfd
    pushad
    cld
    mov ebp,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    push offset Returned
    push ebp
    call BeginQuadReset
    add esp,8
    mov eax,dword ptr [config+8]
    add eax,recoveryExecuteResetEntry
    mov dword ptr [esp+512],eax
    fxrstor [esp]
    mov eax,dword ptr [esp+512]
    mov esp,ebp
    mov dword ptr [esp-4],eax
    popad
    popfd
    call dword ptr [esp-40]
  Returned:
    pushfd
    pushad
    cld
    mov ebp,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    push ebp
    call FinishQuadReset
    add esp,4
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    ret
  }
}

extern "C" __declspec(naked) void QuadResetReturnHook()
{
  using namespace QuadFrameRecovery;
  __asm {
    push 0
    pushfd
    pushad
    add dword ptr [esp+12],4
    cld
    mov ebp,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    mov eax,dword ptr [config+8]
    cmp resetActive,0
    je BaseReady
    mov eax,ownerBase
  BaseReady:
    mov dword ptr [esp+512],eax
    push ebp
    call CheckQuadResetReturn
    add esp,4
    mov eax,dword ptr [esp+512]
    lea ecx,[eax+recoveryQuadResetResume]
    mov dword ptr [ebp+36],ecx
    mov ecx,dword ptr [eax+recoveryExecuteInstructionOffset]
    mov dword ptr [ebp+4],ecx
    mov ecx,dword ptr [ebp+68]
    mov dword ptr [ebp+20],ecx
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    ret
  }
}
#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_QUAD_FRAME_RECOVERY_HPP
