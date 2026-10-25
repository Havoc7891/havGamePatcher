// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_LOST_SURFACE_READINESS_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_LOST_SURFACE_READINESS_HPP

namespace LostSurfaceReadiness
{
  static DWORD busy = 0;

  struct PreLockOwner
  {
    DWORD attempts, failures, guardInvalidations, guardReentries, upload;
    DWORD begins, ends, sceneFailures, creates, created;
    DWORD cachePasses, cacheSuccesses, cacheFailures, pending, completed, loads;
    DWORD transferGeneration, transferLoads, transferInvalidations;
    DWORD ownerGeneration, transition, app, frameObject, viewObject, drawWindow;
    DWORD flushInvalidations, flushReentries;
    DWORD primaryTable, backbufferTable, primaryLost, primaryRestore, backbufferLost;

    void Capture()
    {
      attempts = state.restoreAttempts;
      failures = state.restoreFailures;
      guardInvalidations = surfaceRestoreGuardState.invalidations;
      guardReentries = surfaceRestoreGuardState.reentries;
      upload = TextureLifecycle::UploadSerial();
      begins = bridgeState.beginCalls;
      ends = bridgeState.endCalls;
      sceneFailures = bridgeState.failures;
      creates = bridgeState.createAttempts;
      created = bridgeState.createSuccesses;
      cachePasses = textureState.passes;
      cacheSuccesses = textureState.successes;
      cacheFailures = textureState.failures;
      pending = textureState.pendingEpoch;
      completed = textureState.completedEpoch;
      loads = loadState.calls;
      transferGeneration = TextureTransfer::textureTransferStats.generation;
      transferLoads = TextureTransfer::textureTransferStats.loads;
      transferInvalidations = TextureTransfer::textureTransferStats.invalidations;
      ownerGeneration = modeAdmissionState.ownerGeneration;
      transition = modeAdmissionState.transitionSerial;
      app = Read(GameRva::app);
      frameObject = app ? *reinterpret_cast<DWORD *>(app + 0x1c) : 0;
      viewObject = Read(GameRva::view);
      drawWindow = Read(GameRva::drawWindow);
      flushInvalidations = flushOwnerState.invalidations;
      flushReentries = flushOwnerState.reentries;
      primaryTable = *reinterpret_cast<DWORD *>(NormalFrameOwnership::ownerPrimary);
      backbufferTable = *reinterpret_cast<DWORD *>(NormalFrameOwnership::ownerBackbuffer);
      const auto primary = reinterpret_cast<const DWORD *>(primaryTable);
      const auto backbuffer = reinterpret_cast<const DWORD *>(backbufferTable);
      primaryLost = primary ? primary[24] : 0;
      primaryRestore = primary ? primary[27] : 0;
      backbufferLost = backbuffer ? backbuffer[24] : 0;
    }

    bool Current(DWORD restored) const
    {
      const auto &receipt = NormalFrameOwnership::restoreReceipt;
      if (!NormalFrameOwnership::SameOwnerAtRestore(restored) ||
          normalFrameState.generation != receipt.frameGeneration ||
          NormalFrameOwnership::ownerRestore != receipt.previousEpoch ||
          flushOwnerState.generation + 1 != receipt.flushGeneration || flushOwnerState.busy ||
          flushOwnerState.active || flushOwnerState.incomplete ||
          flushOwnerState.invalidations != flushInvalidations ||
          flushOwnerState.reentries != flushReentries || state.restoreBusy ||
          state.setupCleanupBusy || busy || Read(GameRva::drawLock) || Read(GameRva::textureLock) ||
          Read(GameRva::drawSession) != receipt.session || textureState.busy || loadState.busy ||
          TextureTransfer::textureTransferStats.busy ||
          TextureTransfer::textureTransferStats.incompleteFlags ||
          TextureTransfer::textureTransferStats.readyGeneration ||
          TextureTransfer::textureTransferStats.activeGeneration || quadFrameRecoveryState.busy ||
          quadFrameRecoveryState.aborted || quadFrameRecoveryState.pendingPresent ||
          quadFrameRecoveryState.awaitingFrame || worldRelockState.busy ||
          worldRelockState.incomplete || bridgeState.sceneDepth || begins != ends ||
          bridgeState.beginCalls != begins || bridgeState.endCalls != ends ||
          bridgeState.failures != sceneFailures ||
          NormalFrameOwnership::ownerFailures != sceneFailures ||
          bridgeState.createAttempts != creates || bridgeState.createSuccesses != created ||
          bridgeState.lastStage != D3DBridge::end || bridgeState.lastResult != S_OK ||
          state.restoreFailures != failures ||
          surfaceRestoreGuardState.invalidations != guardInvalidations ||
          surfaceRestoreGuardState.reentries != guardReentries ||
          TextureLifecycle::UploadSerial() != upload || textureState.passes != cachePasses ||
          textureState.successes != cacheSuccesses || textureState.failures != cacheFailures ||
          textureState.pendingEpoch != pending || textureState.completedEpoch != completed ||
          loadState.calls != loads ||
          TextureTransfer::textureTransferStats.generation != transferGeneration ||
          TextureTransfer::textureTransferStats.loads != transferLoads ||
          TextureTransfer::textureTransferStats.invalidations != transferInvalidations ||
          modeAdmissionState.ownerGeneration != ownerGeneration ||
          modeAdmissionState.transitionSerial != transition || !app || app != Read(GameRva::app) ||
          !frameObject || frameObject != *reinterpret_cast<DWORD *>(app + 0x1c) || !viewObject ||
          viewObject != Read(GameRva::view) || drawWindow != Read(GameRva::drawWindow) ||
          !primaryTable || !backbufferTable || !primaryLost || !primaryRestore || !backbufferLost ||
          primaryTable != *reinterpret_cast<DWORD *>(NormalFrameOwnership::ownerPrimary) ||
          backbufferTable != *reinterpret_cast<DWORD *>(NormalFrameOwnership::ownerBackbuffer))
      {
        return false;
      }
      const auto primary = reinterpret_cast<const DWORD *>(primaryTable);
      const auto backbuffer = reinterpret_cast<const DWORD *>(backbufferTable);
      return primary[24] == primaryLost && primary[27] == primaryRestore &&
             backbuffer[24] == backbufferLost;
    }
  };

  static PreLockOwner preLockOwner = {};

  static DWORD BeginPreLockRestore(void *surface, void *descriptor, DWORD entry)
  {
    auto &receipt = NormalFrameOwnership::restoreReceipt;
    const bool nested = receipt.phase == 1 || receipt.phase == 2;
    receipt.phase = 0;
    DWORD slots[5];
    if (nested || !normalFrameState.generation || !NormalFrameOwnership::SameOwner() ||
        !NormalFrameOwnership::ownerPrimary || !NormalFrameOwnership::ownerBackbuffer ||
        reinterpret_cast<DWORD>(surface) != NormalFrameOwnership::ownerBackbuffer ||
        reinterpret_cast<DWORD>(descriptor) != config.gameBase + recoveryLockDescriptor ||
        !NormalFrameOwnership::StackRead(entry, slots, sizeof(slots)) ||
        slots[0] != config.gameBase + recoveryLockCallerReturn ||
        slots[1] != reinterpret_cast<DWORD>(surface) ||
        slots[2] != reinterpret_cast<DWORD>(descriptor) ||
        (slots[4] != config.gameBase + recoveryLockOuterReturn &&
         slots[4] != config.gameBase + recoveryGeometryLockReturn) ||
        state.restoreSuccesses == 0xffffffffu || state.restoreAttempts == 0xffffffffu ||
        flushOwnerState.generation == 0xffffffffu)
    {
      return 0;
    }
    receipt.frameGeneration = normalFrameState.generation;
    receipt.previousEpoch = state.restoreSuccesses;
    receipt.restoredEpoch = state.restoreSuccesses + 1;
    receipt.flushGeneration = flushOwnerState.generation + 1;
    receipt.entryStack = entry;
    receipt.outerReturn = slots[4];
    receipt.session = slots[4] == config.gameBase + recoveryGeometryLockReturn ? 1 : 0;
    preLockOwner.Capture();
    if (!preLockOwner.Current(receipt.previousEpoch))
    {
      return 0;
    }
    receipt.phase = 1;
    return receipt.frameGeneration;
  }

  static void FinishPreLockRestore(DWORD generation)
  {
    auto &receipt = NormalFrameOwnership::restoreReceipt;
    if (!generation || receipt.phase != 1 || receipt.frameGeneration != generation)
    {
      return;
    }
    if (state.restoreAttempts == preLockOwner.attempts + 1 && state.lastRestoreResult == S_OK &&
        state.lastPrimaryIsLost == S_OK && state.lastBackbufferIsLost == S_OK &&
        preLockOwner.Current(receipt.restoredEpoch))
    {
      // The original Lock still runs as a tail call. Only its unchanged raw
      // result observer can complete this provisional receipt.
      receipt.phase = 2;
    }
    else
    {
      receipt.phase = 0;
    }
  }

  struct CleanupScope
  {
    DWORD token, thread, stack, remaining;
  } cleanupScope = {};

  static bool CleanupStack(DWORD stack, DWORD remaining)
  {
    DWORD words[6];
    return remaining >= 1 && remaining <= 3 &&
           NormalFrameOwnership::StackRead(stack, words, sizeof(words)) &&
           words[0] == config.gameBase + readinessFlushReturn && words[1] == remaining &&
           words[3] == config.gameBase + readinessCleanupEndReturn &&
           words[4] == config.gameBase + readinessCleanupDispatchReturn &&
           words[5] == config.gameBase + readinessCleanupLoopReturn;
  }

  struct CleanupOwner
  {
    DWORD base, lifecycle, frame, epoch, restored, attempts, failures;
    DWORD draw, primary, backbuffer, device, execute, app, frameObject, viewObject, drawWindow;
    DWORD frameWindow, viewWindow, movieWindow, movieHeader, renderBpp, preference, software;
    DWORD staging, stagingInterface, pending, completed;
    DWORD mission, content, ready, borrowed, stage, menu, ownerGeneration, transition;
    DWORD begins, ends, sceneFailures, creates, created, upload;
    DWORD cachePasses, cacheSuccesses, cacheFailures, loads;
    DWORD transferGeneration, transferLoads, transferInvalidations;
    DWORD guardInvalidations, guardReentries, flushInvalidations, flushReentries;
    DWORD drawTable, primaryTable, backbufferTable, deviceTable, executeTable;
    DWORD drawAdd, drawRelease, primaryAdd, primaryRelease, primaryLost, primaryRestore;
    DWORD backbufferAdd, backbufferRelease, backbufferLost, deviceQuery, deviceExecute;
    DWORD executeUnlock, executeSetData;

    static DWORD Table(DWORD object)
    {
      return object ? *reinterpret_cast<const DWORD *>(object) : 0;
    }

    static DWORD Method(DWORD table, DWORD slot)
    {
      return table ? reinterpret_cast<const DWORD *>(table)[slot] : 0;
    }

    void Capture()
    {
      base = config.gameBase;
      lifecycle = TextureLifecycle::Serial();
      frame = TextureLifecycle::FrameSerial();
      epoch = TextureLifecycle::FrameEpoch();
      restored = state.restoreSuccesses;
      attempts = state.restoreAttempts;
      failures = state.restoreFailures;
      draw = Read(GameRva::drawObject);
      primary = Read(GameRva::primary);
      backbuffer = Read(GameRva::backbuffer);
      device = Read(GameRva::legacyDevice);
      execute = Read(readinessExecuteBuffer);
      app = Read(GameRva::app);
      frameObject = app ? *reinterpret_cast<DWORD *>(app + 0x1c) : 0;
      viewObject = Read(GameRva::view);
      drawWindow = Read(GameRva::drawWindow);
      frameWindow = reinterpret_cast<DWORD>(FrameWindow());
      viewWindow = reinterpret_cast<DWORD>(ViewWindow());
      movieWindow = Read(GameRva::movieWindow);
      movieHeader = Read(GameRva::movieHeader);
      renderBpp = Read(GameRva::renderBpp);
      preference = Read(GameRva::hardwarePreference);
      software = Read(GameRva::software);
      staging = Read(GameRva::textureStagingSurface);
      stagingInterface = Read(GameRva::textureStagingInterface);
      pending = textureState.pendingEpoch;
      completed = textureState.completedEpoch;
      mission = state.depth;
      content = state.contentActive;
      ready = state.videoReady;
      borrowed = state.videoBorrowed;
      stage = Read(GameRva::gameStage);
      menu = Read(GameRva::mainMenuVisible);
      ownerGeneration = modeAdmissionState.ownerGeneration;
      transition = modeAdmissionState.transitionSerial;
      begins = bridgeState.beginCalls;
      ends = bridgeState.endCalls;
      sceneFailures = bridgeState.failures;
      creates = bridgeState.createAttempts;
      created = bridgeState.createSuccesses;
      upload = TextureLifecycle::UploadSerial();
      cachePasses = textureState.passes;
      cacheSuccesses = textureState.successes;
      cacheFailures = textureState.failures;
      loads = loadState.calls;
      transferGeneration = TextureTransfer::textureTransferStats.generation;
      transferLoads = TextureTransfer::textureTransferStats.loads;
      transferInvalidations = TextureTransfer::textureTransferStats.invalidations;
      guardInvalidations = surfaceRestoreGuardState.invalidations;
      guardReentries = surfaceRestoreGuardState.reentries;
      flushInvalidations = flushOwnerState.invalidations;
      flushReentries = flushOwnerState.reentries;
      drawTable = Table(draw);
      primaryTable = Table(primary);
      backbufferTable = Table(backbuffer);
      deviceTable = Table(device);
      executeTable = Table(execute);
      drawAdd = Method(drawTable, 1);
      drawRelease = Method(drawTable, 2);
      primaryAdd = Method(primaryTable, 1);
      primaryRelease = Method(primaryTable, 2);
      primaryLost = Method(primaryTable, 24);
      primaryRestore = Method(primaryTable, 27);
      backbufferAdd = Method(backbufferTable, 1);
      backbufferRelease = Method(backbufferTable, 2);
      backbufferLost = Method(backbufferTable, 24);
      deviceQuery = Method(deviceTable, 0);
      deviceExecute = Method(deviceTable, 8);
      executeUnlock = Method(executeTable, 5);
      executeSetData = Method(executeTable, 6);
    }

    bool Current(DWORD token) const
    {
      return token && cleanupScope.token == token && FlushOwnership::Owned(token) &&
             FlushOwnership::ConfigurationUnchanged() && !flushOwnerState.incomplete &&
             flushOwnerState.activeValid && cleanupScope.thread == TextureLifecycle::ThreadId() &&
             cleanupScope.stack == flushOwnerState.entryStack &&
             CleanupStack(cleanupScope.stack, cleanupScope.remaining) &&
             flushOwnerState.invalidations == flushInvalidations &&
             flushOwnerState.reentries == flushReentries && config.gameBase == base &&
             lifecycle == TextureLifecycle::Serial() && frame == TextureLifecycle::FrameSerial() &&
             epoch == TextureLifecycle::FrameEpoch() && !TextureLifecycle::Incomplete() &&
             !normalFrameState.depth && !normalFrameState.armed && !normalFrameState.incomplete &&
             !state.busy && !state.ended && state.videoDepth == 1 && !state.mainMenuDepth &&
             !state.transitionDepth && !state.setupCleanupBusy && !Read(GameRva::quit) &&
             Read(GameRva::mode) == 2 && Read(GameRva::directDraw) && Read(GameRva::hardware) &&
             Read(GameRva::fullscreenActive) && !Read(GameRva::drawSession) &&
             !Read(GameRva::textureLock) && !movieRecoveryState.busy &&
             !preMovieRecoveryState.busy && !preMovieRecoveryState.scopeDepth &&
             !worldRelockState.busy && !worldRelockState.incomplete && !textureState.busy &&
             !loadState.busy && !TextureTransfer::textureTransferStats.busy &&
             !TextureTransfer::textureTransferStats.activeGeneration &&
             !TextureTransfer::textureTransferStats.incompleteFlags &&
             state.restoreFailures == failures &&
             surfaceRestoreGuardState.invalidations == guardInvalidations &&
             surfaceRestoreGuardState.reentries == guardReentries && draw &&
             draw == Read(GameRva::drawObject) && primary && primary == Read(GameRva::primary) &&
             backbuffer && backbuffer != primary && backbuffer == Read(GameRva::backbuffer) &&
             device && device == Read(GameRva::legacyDevice) && execute &&
             execute == Read(readinessExecuteBuffer) && app && app == Read(GameRva::app) &&
             frameObject && frameObject == *reinterpret_cast<DWORD *>(app + 0x1c) && viewObject &&
             viewObject == Read(GameRva::view) && drawWindow &&
             drawWindow == Read(GameRva::drawWindow) && frameWindow &&
             frameWindow == reinterpret_cast<DWORD>(FrameWindow()) && viewWindow &&
             viewWindow == reinterpret_cast<DWORD>(ViewWindow()) &&
             movieWindow == Read(GameRva::movieWindow) &&
             movieHeader == Read(GameRva::movieHeader) && renderBpp == Read(GameRva::renderBpp) &&
             preference == Read(GameRva::hardwarePreference) &&
             software == Read(GameRva::software) &&
             staging == Read(GameRva::textureStagingSurface) &&
             stagingInterface == Read(GameRva::textureStagingInterface) &&
             pending == textureState.pendingEpoch && completed == textureState.completedEpoch &&
             mission == state.depth && content == state.contentActive &&
             ready == state.videoReady && borrowed == state.videoBorrowed &&
             stage == Read(GameRva::gameStage) && menu == Read(GameRva::mainMenuVisible) &&
             ownerGeneration == modeAdmissionState.ownerGeneration &&
             transition == modeAdmissionState.transitionSerial && bridgeState.sceneDepth == 0 &&
             begins == ends && begins == bridgeState.beginCalls && ends == bridgeState.endCalls &&
             sceneFailures == bridgeState.failures && creates == bridgeState.createAttempts &&
             created == bridgeState.createSuccesses && bridgeState.lastStage == D3DBridge::end &&
             bridgeState.lastResult == S_OK && upload == TextureLifecycle::UploadSerial() &&
             cachePasses == textureState.passes && cacheSuccesses == textureState.successes &&
             cacheFailures == textureState.failures && loads == loadState.calls &&
             transferGeneration == TextureTransfer::textureTransferStats.generation &&
             transferLoads == TextureTransfer::textureTransferStats.loads &&
             transferInvalidations == TextureTransfer::textureTransferStats.invalidations &&
             drawTable && drawTable == Table(draw) && primaryTable &&
             primaryTable == Table(primary) && backbufferTable &&
             backbufferTable == Table(backbuffer) && deviceTable && deviceTable == Table(device) &&
             executeTable && executeTable == Table(execute) && drawAdd &&
             drawAdd == Method(drawTable, 1) && drawRelease &&
             drawRelease == Method(drawTable, 2) && primaryAdd &&
             primaryAdd == Method(primaryTable, 1) && primaryRelease &&
             primaryRelease == Method(primaryTable, 2) && primaryLost &&
             primaryLost == Method(primaryTable, 24) && primaryRestore &&
             primaryRestore == Method(primaryTable, 27) && backbufferAdd &&
             backbufferAdd == Method(backbufferTable, 1) && backbufferRelease &&
             backbufferRelease == Method(backbufferTable, 2) && backbufferLost &&
             backbufferLost == Method(backbufferTable, 24) && deviceQuery &&
             deviceQuery == Method(deviceTable, 0) && deviceExecute &&
             deviceExecute == Method(deviceTable, 8) && executeUnlock &&
             executeUnlock == Method(executeTable, 5) && executeSetData &&
             executeSetData == Method(executeTable, 6);
    }
  } cleanupOwner = {};

  // Capture before the failing readiness call. Its temporary COM references
  // close before this separate recovery scope begins.
  struct Pins
  {
    DisplayReadiness::Context identity{DisplayReadiness::Phase::textureRecovery};
    SurfaceRestoreOwnership::Owner surface;
    DWORD token, frameSerial = TextureLifecycle::FrameSerial();
    DWORD frameGeneration = normalFrameState.generation;
    DWORD ownerSlot = normalFrameState.ownerSlot;
    DWORD flushInvalidations = flushOwnerState.invalidations;
    DWORD flushReentries = flushOwnerState.reentries;
    DWORD begin = bridgeState.beginCalls, end = bridgeState.endCalls;
    DWORD sceneFailures = bridgeState.failures, creations = bridgeState.createAttempts;
    DWORD created = bridgeState.createSuccesses;
    DWORD cachePasses = textureState.passes, loadCalls = loadState.calls;
    DWORD transferGeneration = TextureTransfer::textureTransferStats.generation;
    DWORD transferLoads = TextureTransfer::textureTransferStats.loads;
    DWORD transferInvalidations = TextureTransfer::textureTransferStats.invalidations;
    DWORD execute = Read(readinessExecuteBuffer);
    DWORD frameRestore = NormalFrameOwnership::ownerRestore;
    DWORD drawAdd = 0, drawRelease = 0, primaryAdd = 0, primaryRelease = 0;
    DWORD primaryLost = 0, primaryRestore = 0, backbufferAdd = 0, backbufferRelease = 0,
          backbufferLost = 0;
    bool frameEntryValid = false, cleanupEntryValid = false;
    NormalFrameOwnership::RestoreReceipt preLockReceipt = {};

    explicit Pins(DWORD value) : token(value)
    {
      if (identity.draw && identity.primary && identity.backbuffer)
      {
        identity.drawMethods = *reinterpret_cast<DWORD **>(identity.draw);
        identity.primaryMethods = *reinterpret_cast<DWORD **>(identity.primary);
        identity.backbufferMethods = *reinterpret_cast<DWORD **>(identity.backbuffer);
        if (identity.drawMethods && identity.primaryMethods && identity.backbufferMethods)
        {
          drawAdd = identity.drawMethods[1];
          drawRelease = identity.drawMethods[2];
          primaryAdd = identity.primaryMethods[1];
          primaryRelease = identity.primaryMethods[2];
          primaryLost = identity.primaryMethods[24];
          primaryRestore = identity.primaryMethods[27];
          backbufferAdd = identity.backbufferMethods[1];
          backbufferRelease = identity.backbufferMethods[2];
          backbufferLost = identity.backbufferMethods[24];
        }
      }
      const auto &entry = FlushOwnership::entry;
      const auto &native = FlushOwnership::returned;
      // The existing pre-unlock hook can restore this frame's surfaces before
      // readiness is reached. Accept only that exact one-step publication.
      const bool originalRestore =
          entry.restoreSuccesses != 0xffffffffu &&
          identity.restored == entry.restoreSuccesses + 1 && state.lastRestoreResult == S_OK &&
          state.lastPrimaryIsLost == S_OK && state.lastBackbufferIsLost == S_OK;
      const auto &receipt = NormalFrameOwnership::restoreReceipt;
      const bool recordedPreLock =
          !entry.sameOwner && receipt.phase == 3 && receipt.frameGeneration == frameGeneration &&
          receipt.previousEpoch == frameRestore && receipt.previousEpoch != 0xffffffffu &&
          receipt.restoredEpoch == receipt.previousEpoch + 1 &&
          receipt.restoredEpoch == entry.restoreSuccesses && receipt.flushGeneration == token;
      if (recordedPreLock)
      {
        preLockReceipt = receipt;
      }
      const bool cleanupRestore =
          cleanupOwner.restored != 0xffffffffu && identity.restored == cleanupOwner.restored + 1 &&
          cleanupOwner.attempts != 0xffffffffu &&
          state.restoreAttempts == cleanupOwner.attempts + 1 && state.lastRestoreResult == S_OK &&
          state.lastPrimaryIsLost == S_OK && state.lastBackbufferIsLost == S_OK;
      cleanupEntryValid = cleanupOwner.Current(token) && entry.configured &&
                          entry.restoreSuccesses == cleanupOwner.restored &&
                          native.restoreSuccesses == identity.restored &&
                          entry.backbuffer == identity.backbuffer &&
                          native.backbuffer == identity.backbuffer &&
                          ((identity.restored == cleanupOwner.restored &&
                            state.restoreAttempts == cleanupOwner.attempts) ||
                           cleanupRestore);
      frameEntryValid =
          entry.configured &&
          ((entry.sameOwner && entry.restoreSuccesses == frameRestore) || recordedPreLock) &&
          native.restoreSuccesses == identity.restored &&
          (identity.restored == entry.restoreSuccesses || originalRestore);
    }

    bool FrameOwnerCurrent() const
    {
      const auto &f = normalFrameState;
      return frameEntryValid && NormalFrameOwnership::Phase() && f.depth == 1 && f.armed &&
             !f.invalidated && f.generation == frameGeneration && f.ownerSlot == ownerSlot &&
             f.ownerThread == identity.thread && NormalFrameOwnership::ownerBase == identity.base &&
             NormalFrameOwnership::ownerGeneration == frameGeneration &&
             NormalFrameOwnership::ownerLifecycle == identity.lifecycle &&
             NormalFrameOwnership::ownerFrame == frameSerial &&
             NormalFrameOwnership::ownerEpoch == identity.frameEpoch &&
             NormalFrameOwnership::ownerRestore == frameRestore &&
             NormalFrameOwnership::ownerDraw == identity.draw &&
             NormalFrameOwnership::ownerPrimary == identity.primary &&
             NormalFrameOwnership::ownerBackbuffer == identity.backbuffer &&
             NormalFrameOwnership::ownerDevice == identity.device &&
             NormalFrameOwnership::ownerExecute == execute &&
             NormalFrameOwnership::ownerFailures == sceneFailures;
    }

    bool Current(bool restoring) const
    {
      const auto &flush = flushOwnerState;
      const auto &receipt = NormalFrameOwnership::restoreReceipt;
      return !quadFrameRecoveryState.busy && !quadFrameRecoveryState.aborted &&
             !quadFrameRecoveryState.pendingPresent && !quadFrameRecoveryState.awaitingFrame &&
             !quadFrameRecoveryState.guardIncomplete &&
             (FrameOwnerCurrent() ||
              (cleanupEntryValid && cleanupOwner.Current(token) && flush.branchKind == 2 &&
               flush.rawCalls == 1 && flush.rawOwned && flush.rawResult == S_OK &&
               flush.entryDrawLock == 1)) &&
             (!preLockReceipt.phase ||
              (receipt.phase == 3 && receipt.frameGeneration == preLockReceipt.frameGeneration &&
               receipt.previousEpoch == preLockReceipt.previousEpoch &&
               receipt.restoredEpoch == preLockReceipt.restoredEpoch &&
               receipt.flushGeneration == preLockReceipt.flushGeneration &&
               receipt.entryStack == preLockReceipt.entryStack &&
               receipt.outerReturn == preLockReceipt.outerReturn &&
               receipt.session == preLockReceipt.session)) &&
             identity.IdentityCurrent() && identity.restored &&
             identity.restored != identity.completed &&
             state.restoreBusy == static_cast<DWORD>(restoring) && !textureState.busy &&
             !loadState.busy && !TextureTransfer::textureTransferStats.busy &&
             NormalFrameOwnership::Unlocked() && !readinessState.busy &&
             TextureLifecycle::FrameSerial() == frameSerial &&
             Read(readinessExecuteBuffer) == execute && FlushOwnership::Owned(token) &&
             FlushOwnership::ConfigurationUnchanged() && !flush.busy && !flush.incomplete &&
             flush.activeValid && flush.nativeReturned && flush.nativeBool == 1 &&
             !flush.afterDrawLock && flush.nativeReturn == identity.base + readinessFlushReturn &&
             flush.branchKind >= 1 && flush.branchKind <= 3 &&
             flush.invalidations == flushInvalidations && flush.reentries == flushReentries &&
             bridgeState.sceneDepth == 0 && begin == end && bridgeState.beginCalls == begin &&
             bridgeState.endCalls == end && bridgeState.failures == sceneFailures &&
             bridgeState.createAttempts == creations && bridgeState.createSuccesses == created &&
             bridgeState.lastStage == D3DBridge::end && bridgeState.lastResult == S_OK &&
             textureState.passes == cachePasses && loadState.calls == loadCalls &&
             TextureTransfer::textureTransferStats.generation == transferGeneration &&
             TextureTransfer::textureTransferStats.loads == transferLoads &&
             TextureTransfer::textureTransferStats.invalidations == transferInvalidations &&
             identity.drawMethods && identity.primaryMethods && identity.backbufferMethods &&
             drawAdd && drawRelease && primaryAdd && primaryRelease && primaryLost &&
             primaryRestore && backbufferAdd && backbufferRelease && backbufferLost &&
             identity.drawMethods[1] == drawAdd && identity.drawMethods[2] == drawRelease &&
             identity.primaryMethods[1] == primaryAdd &&
             identity.primaryMethods[2] == primaryRelease &&
             identity.primaryMethods[24] == primaryLost &&
             identity.primaryMethods[27] == primaryRestore &&
             identity.backbufferMethods[1] == backbufferAdd &&
             identity.backbufferMethods[2] == backbufferRelease &&
             identity.backbufferMethods[24] == backbufferLost;
    }

    bool AcceptOwnRestore()
    {
      // Only the shared restore's exact single publication may be accepted.
      // Frame recovery's old ownerRestore remains unchanged and is now stale.
      if (identity.restored == 0xffffffffu || state.restoreSuccesses != identity.restored + 1)
      {
        return false;
      }
      ++identity.restored;
      ++surface.restoreEpoch;
      return Current(true) && surface.Current(5);
    }
  };

  static HRESULT BudgetCheck(const Pins &pins, DisplayReadiness::Budget &budget, bool restoring)
  {
    if (!pins.Current(restoring) || !budget.initialized || !budget.clock)
    {
      return E_UNEXPECTED;
    }
    const DWORD now = budget.clock();
    if (!pins.Current(restoring))
    {
      return E_UNEXPECTED;
    }
    return now - budget.started < DisplayReadiness::timeoutMilliseconds
               ? S_OK
               : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
  }

  struct References
  {
    void *objects[3] = {};
    DisplayReadiness::Reference releases[3] = {};
    DWORD count = 0;

    bool Acquire(DWORD address)
    {
      if (!address || count == 3)
      {
        return false;
      }
      auto object = reinterpret_cast<void *>(address);
      const auto methods = *reinterpret_cast<DWORD **>(object);
      if (!methods || !methods[1] || !methods[2])
      {
        return false;
      }
      const auto retain = reinterpret_cast<DisplayReadiness::Reference>(methods[1]);
      objects[count] = object;
      releases[count++] = reinterpret_cast<DisplayReadiness::Reference>(methods[2]);
      retain(object);
      return true;
    }

    HRESULT Close(Pins &pins, DisplayReadiness::Budget &budget)
    {
      HRESULT result = S_OK;
      while (count)
      {
        --count;
        releases[count](objects[count]);
        const HRESULT checked = BudgetCheck(pins, budget, true);
        if (checked != S_OK)
        {
          result = checked;
        }
      }
      return result;
    }
  };

  static HRESULT Restore(Pins &pins, DisplayReadiness::Budget &budget)
  {
    if (pins.identity.restored == 0xffffffffu)
    {
      return E_UNEXPECTED;
    }
    const HRESULT initial = BudgetCheck(pins, budget, false);
    if (initial != S_OK)
    {
      return initial;
    }

    struct RestoreScope
    {
      RestoreScope()
      {
        state.restoreBusy = 1;
      }

      ~RestoreScope()
      {
        state.restoreBusy = 0;
      }
    } scope;

    References references;
    HRESULT checked = S_OK;
    auto current = [&]
    {
      checked = BudgetCheck(pins, budget, true);
      return checked == S_OK && pins.surface.Current(5);
    };
    HRESULT result = E_UNEXPECTED;
    if (current() && references.Acquire(pins.identity.draw) && current() &&
        references.Acquire(pins.identity.primary) && current() &&
        references.Acquire(pins.identity.backbuffer) && current())
    {
      using SurfaceMethod = HRESULT(WINAPI *)(void *);
      const auto primaryMethods = pins.identity.primaryMethods;
      const auto backbufferMethods = pins.identity.backbufferMethods;
      if (primaryMethods[24] && primaryMethods[27] && backbufferMethods[24])
      {
        const SurfaceRestoreOwnership::Methods methods(primaryMethods, backbufferMethods);
        const HRESULT primary = reinterpret_cast<SurfaceMethod>(primaryMethods[24])(
            reinterpret_cast<void *>(pins.identity.primary));
        if (current() && (primary == S_OK || primary == DDERR_SURFACELOST))
        {
          const HRESULT backbuffer = reinterpret_cast<SurfaceMethod>(backbufferMethods[24])(
              reinterpret_cast<void *>(pins.identity.backbuffer));
          if (current() && (backbuffer == S_OK || backbuffer == DDERR_SURFACELOST))
          {
            // An independently recovered chain can be freshly probed without
            // another mutation. Only an observed lost chain is restored.
            result = S_OK;
            if (primary == DDERR_SURFACELOST || backbuffer == DDERR_SURFACELOST)
            {
              result = SurfaceRestoreOwnership::RestoreAndVerify(pins.surface, methods, current);
              if (result == S_OK && !pins.AcceptOwnRestore())
              {
                result = E_UNEXPECTED;
              }
            }
          }
          else if (checked == S_OK && backbuffer != S_OK)
          {
            result = backbuffer;
          }
        }
        else if (checked == S_OK && primary != S_OK)
        {
          result = primary;
        }
      }
    }
    if (checked != S_OK)
    {
      result = checked;
    }
    const HRESULT closed = references.Close(pins, budget);
    if (closed != S_OK)
    {
      result = closed;
    }
    if (result == S_OK)
    {
      result = BudgetCheck(pins, budget, true);
    }
    return result;
  }
} // namespace LostSurfaceReadiness

extern "C" void __cdecl BeginCleanupReadinessOwner(DWORD token)
{
  SurfaceRestoreOwnership::LastErrorScope error;
  using namespace LostSurfaceReadiness;
  cleanupScope.token = 0;
  DWORD words[6];
  if (!token || state.videoDepth != 1 || normalFrameState.depth || normalFrameState.armed ||
      state.restoreBusy || readinessState.busy || busy || !Configured() ||
      !FlushOwnership::Owned(token) || !flushOwnerState.activeValid ||
      flushOwnerState.entryDrawLock != 1 ||
      !NormalFrameOwnership::StackRead(flushOwnerState.entryStack, words, sizeof(words)) ||
      !CleanupStack(flushOwnerState.entryStack, words[1]))
  {
    return;
  }
  cleanupScope.thread = TextureLifecycle::ThreadId();
  cleanupScope.stack = flushOwnerState.entryStack;
  cleanupScope.remaining = words[1];
  cleanupOwner.Capture();
  cleanupScope.token = token;
  if (!cleanupOwner.Current(token))
  {
    cleanupScope.token = 0;
  }
}

extern "C" void __cdecl EndCleanupReadinessOwner(DWORD token)
{
  SurfaceRestoreOwnership::LastErrorScope error;
  auto &scope = LostSurfaceReadiness::cleanupScope;
  if (scope.token == token && scope.thread == TextureLifecycle::ThreadId())
  {
    scope.token = 0;
  }
}

extern "C" void __cdecl CompleteFrameRestoreLock(const DWORD *registers)
{
  using namespace LostSurfaceReadiness;
  auto &receipt = NormalFrameOwnership::restoreReceipt;
  if (receipt.phase != 2)
  {
    return;
  }
  SurfaceRestoreOwnership::LastErrorScope error;
  const DWORD stack = registers[3] <= 0xfffffff7u ? registers[3] + 8 : 0;
  DWORD slots[6];
  const bool valid =
      registers[7] == S_OK && stack && stack <= 0xfffffffbu && stack + 4 == receipt.entryStack &&
      NormalFrameOwnership::StackRead(stack, slots, sizeof(slots)) &&
      slots[1] == config.gameBase + recoveryLockCallerReturn &&
      slots[2] == NormalFrameOwnership::ownerBackbuffer &&
      slots[3] == config.gameBase + recoveryLockDescriptor && slots[5] == receipt.outerReturn &&
      state.restoreAttempts == preLockOwner.attempts + 1 &&
      preLockOwner.Current(receipt.restoredEpoch);
  receipt.phase = valid ? 3 : 0;
}

extern "C" bool __cdecl WaitForFlushReadiness(DWORD token)
{
  using namespace LostSurfaceReadiness;
  SurfaceRestoreOwnership::LastErrorScope error;
  if (busy || !Configured())
  {
    return false;
  }

  struct Scope
  {
    Scope()
    {
      busy = 1;
    }

    ~Scope()
    {
      busy = 0;
    }
  } scope;

  if (!token || !DisplayReadiness::Eligible(DisplayReadiness::Phase::textureRecovery) ||
      !FlushOwnership::Owned(token) || !FlushOwnership::ConfigurationUnchanged())
  {
    return DisplayReadiness::Wait(DisplayReadiness::Phase::textureRecovery);
  }
  Pins pins(token);
  const bool captured = pins.Current(false);
  DisplayReadiness::Budget budget;
  const bool ready = DisplayReadiness::Wait(DisplayReadiness::Phase::textureRecovery, &budget);
  if (ready)
  {
    return true;
  }
  if (!captured || readinessState.lastResult != static_cast<DWORD>(DDERR_SURFACELOST) ||
      (readinessState.lastStage != static_cast<DWORD>(DisplayReadiness::Stage::primaryLoss) &&
       readinessState.lastStage != static_cast<DWORD>(DisplayReadiness::Stage::backbufferLoss)) ||
      !pins.Current(false))
  {
    return false;
  }
  HRESULT result = BudgetCheck(pins, budget, false);
  if (result == S_OK)
  {
    result = Restore(pins, budget);
  }
  if (result == S_OK)
  {
    result = BudgetCheck(pins, budget, false);
  }
  if (result == S_OK)
  {
    const bool fresh = DisplayReadiness::Wait(DisplayReadiness::Phase::textureRecovery, &budget);
    result =
        fresh ? BudgetCheck(pins, budget, false) : static_cast<HRESULT>(readinessState.lastResult);
  }
  return result == S_OK;
}

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_LOST_SURFACE_READINESS_HPP
