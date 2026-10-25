// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_POST_RESTORE_RENDERER_HPP
#define HAVGAMEPATCHER_POST_RESTORE_RENDERER_HPP

extern "C"
{
  // Embedding fills the verified native bytes and their game-address fixups.
  // The ordinary read-only linker merge places this placeholder in .text.
  extern const BYTE guardedRendererImage[460] = {};
}

extern "C" HRESULT __cdecl ChildCreateDevice(void *surface, void **output)
{
  if (!Configured())
  {
    return E_INVALIDARG;
  }
  return CreateDeviceBridge(
      surface, reinterpret_cast<const GUID *>(config.gameBase + GameRva::deviceIid), output);
}

namespace PostRestoreRenderer
{
  constexpr DWORD paletteRva = modeNativePalette, viewportRva = modeNativeViewport;
  constexpr DWORD cpuAllocationsRva = rendererNativeCpuAllocations;
  using Native = int(__cdecl *)();
  using Reference = ULONG(WINAPI *)(void *);
  using Surface = HRESULT(WINAPI *)(void *);

  struct ScopeState
  {
    DWORD busy, setup, awaitingFrame;
  } scope = {};

  struct Baseline
  {
    DWORD base, thread, draw, primary, backbuffer, palette, device, execute, created, restored;
    bool valid;
  };

  static Baseline baseline = {};
  static DWORD nativeDispatches = 0, retained = 0;
  static bool setupReadinessActive = false, setupReadinessInvalidated = false;
  static DWORD confirmationFrame = 0, confirmationEpoch = 0, confirmationRestore = 0;
  static DWORD confirmationGeneration = 0;
  static DWORD confirmationBegin = 0, confirmationEnd = 0, confirmationFailures = 0;
  static DWORD confirmedBegin = 0, confirmedEnd = 0, confirmedUpload = 0;
  static HardwareCompletionOwner confirmationOwner = {};
  static DWORD flipToken = 0, nextFlipToken = 0, flipCall = 0, flipNested = 0;
  static DWORD flipPresentationToken = 0;
  static bool confirmationArmed = false;

  struct BankSlot
  {
    DWORD surface, texture, handle;
  };

  static BankSlot newSlots[1024] = {};
  static DWORD newCount = 0, newStaging = 0, newStagingTexture = 0;

  static bool ClosedScene()
  {
    return !bridgeState.sceneDepth && bridgeState.beginCalls == bridgeState.endCalls &&
           bridgeState.lastStage == D3DBridge::end && bridgeState.lastResult == S_OK;
  }

  static bool BaselineCurrent()
  {
    return baseline.valid && Configured() && config.gameBase == baseline.base &&
           TextureLifecycle::ThreadId() == baseline.thread &&
           Read(GameRva::drawObject) == baseline.draw &&
           Read(GameRva::primary) == baseline.primary &&
           Read(GameRva::backbuffer) == baseline.backbuffer &&
           Read(paletteRva) == baseline.palette && Read(GameRva::legacyDevice) == baseline.device &&
           Read(recoveryExecuteBuffer) == baseline.execute &&
           bridgeState.createSuccesses == baseline.created;
  }

  static bool Hardware()
  {
    return Configured() && !Read(GameRva::quit) && Read(GameRva::mode) == 2 &&
           Read(GameRva::hardware) && Read(GameRva::directDraw) && Read(GameRva::fullscreenActive);
  }

  static bool ConfirmationCurrent()
  {
    return BaselineCurrent() && HardwareModeCompletion::ResourcesCurrent(confirmationOwner) &&
           HardwareModeCompletion::Quiet() && Read(recoveryNativeActive) &&
           !Read(recoveryNativePausedAliasLegacyVideo) && !Read(GameRva::drawLock) &&
           !Read(GameRva::drawSession) && !Read(GameRva::textureLock) && !scope.busy &&
           !FullscreenSetupBusy() && !UnlockFrameBusy() && !quadFrameRecoveryState.depth &&
           !quadFrameRecoveryState.armed && !failedPresentationState.busy &&
           !worldRelockState.busy && !flushOwnerState.busy && !flushOwnerState.active &&
           !normalFrameState.depth && !LostSurfaceReadiness::busy && ClosedScene();
  }

  static bool PresentationCurrent(DWORD token, DWORD ordinal)
  {
    // CaptureFailedPresentationFlip has checked the complete native caller
    // chain. Keep that exact frame and scope alive across the original Flip.
    return token && FailedPresentation::flipValid && !FailedPresentation::failureArmed &&
           token == FailedPresentation::scopeToken && token == FailedPresentation::flipToken &&
           ordinal == FailedPresentation::flipOrdinal &&
           token == failedPresentationState.generation && !failedPresentationState.busy &&
           FailedPresentation::ScopeCurrent() && FailedPresentation::FrameCurrent();
  }

  static bool Quiet(bool restoring = false)
  {
    return Hardware() && !TextureLifecycle::Incomplete() && state.depth == 1 && !state.ended &&
           !state.busy && state.restoreBusy == static_cast<DWORD>(restoring) &&
           (!restoring || (scope.busy && scope.setup == 1 && setupReadinessActive &&
                           !setupReadinessInvalidated)) &&
           !state.setupCleanupBusy && !state.transitionDepth && !state.videoDepth &&
           !state.mainMenuDepth && !modeAdmissionState.transitionDepth &&
           !Read(GameRva::mainMenuVisible) && Read(recoveryNativeActive) &&
           !Read(recoveryNativePausedAliasLegacyVideo) && !Read(GameRva::drawLock) &&
           !Read(GameRva::drawSession) && !Read(GameRva::textureLock) &&
           Read(GameRva::renderBpp) == 8 && !readinessState.busy && !textureState.busy &&
           !loadState.busy && !TextureTransfer::textureTransferStats.busy &&
           !TextureTransfer::textureTransferStats.readyGeneration &&
           !TextureTransfer::textureTransferStats.activeGeneration &&
           !TextureTransfer::textureTransferStats.incompleteFlags && !movieRecoveryState.busy &&
           !preMovieRecoveryState.busy && !preMovieRecoveryState.scopeDepth &&
           !quadFrameRecoveryState.depth && !quadFrameRecoveryState.armed &&
           !quadFrameRecoveryState.busy && !quadFrameRecoveryState.aborted &&
           !quadFrameRecoveryState.pendingPresent && !quadFrameRecoveryState.awaitingFrame &&
           !failedPresentationState.busy && !failedPresentationState.pending &&
           !FailedPresentation::scopeDepth && !worldRelockState.busy && !FullscreenSetupBusy() &&
           !UnlockFrameBusy() && !flushOwnerState.busy && !flushOwnerState.active &&
           !flipProbeState.busy && !normalFrameState.depth && !normalFrameState.incomplete &&
           !LostSurfaceReadiness::busy && !flushOwnerState.incomplete;
  }

  struct Owner
  {
    DWORD base, thread, lifecycle, epoch, serial, upload, restore, pending, completed;
    DWORD draw, primary, backbuffer, palette, app, viewObject, frameObject, drawWindow;
    HWND frame, view;
    DWORD mission, content, gameStage, preference, quality, software, creates, created, begins,
        ends, failures;
    DWORD cpu[12], objects[4], tables[4];
    Reference adds[4], drops[4];
    DWORD child[4];

    void Capture()
    {
      base = config.gameBase;
      thread = TextureLifecycle::ThreadId();
      lifecycle = TextureLifecycle::Serial();
      epoch = TextureLifecycle::FrameEpoch();
      serial = TextureLifecycle::FrameSerial();
      upload = TextureLifecycle::UploadSerial();
      restore = state.restoreSuccesses;
      pending = textureState.pendingEpoch;
      completed = textureState.completedEpoch;
      draw = Read(GameRva::drawObject);
      primary = Read(GameRva::primary);
      backbuffer = Read(GameRva::backbuffer);
      palette = Read(paletteRva);
      app = Read(GameRva::app);
      viewObject = Read(GameRva::view);
      frameObject = app ? *reinterpret_cast<DWORD *>(app + 0x1c) : 0;
      frame = FrameWindow();
      view = ViewWindow();
      drawWindow = Read(GameRva::drawWindow);
      mission = state.depth;
      content = state.contentActive;
      gameStage = Read(GameRva::gameStage);
      preference = Read(GameRva::hardwarePreference);
      quality = Read(rendererNativeQuality);
      software = Read(GameRva::software);
      creates = bridgeState.createAttempts;
      created = bridgeState.createSuccesses;
      begins = bridgeState.beginCalls;
      ends = bridgeState.endCalls;
      failures = bridgeState.failures;
      objects[0] = draw;
      objects[1] = primary;
      objects[2] = backbuffer;
      objects[3] = palette;
      child[0] = Read(recoveryExecuteBuffer);
      child[1] = Read(viewportRva);
      child[2] = Read(GameRva::legacyDevice);
      child[3] = Read(GameRva::d3dRoot);
      for (DWORD i = 0; i < 12; ++i)
      {
        cpu[i] = Read(cpuAllocationsRva + i * 4);
      }
      for (DWORD i = 0; i < 4; ++i)
      {
        tables[i] = objects[i] ? *reinterpret_cast<DWORD *>(objects[i]) : 0;
        const auto table = reinterpret_cast<const DWORD *>(tables[i]);
        adds[i] = table ? reinterpret_cast<Reference>(table[1]) : nullptr;
        drops[i] = table ? reinterpret_cast<Reference>(table[2]) : nullptr;
      }
    }

    bool
    Current(bool checkChildren = true, bool restoring = false, bool restorePublished = false) const
    {
      bool valid =
          Configured() && base == config.gameBase && thread == TextureLifecycle::ThreadId() &&
          Quiet(restoring) && scope.busy && lifecycle == TextureLifecycle::Serial() &&
          epoch == TextureLifecycle::FrameEpoch() && serial == TextureLifecycle::FrameSerial() &&
          upload == TextureLifecycle::UploadSerial() &&
          (!restorePublished || (restoring && setupReadinessActive && !setupReadinessInvalidated &&
                                 restore != 0xffffffffu)) &&
          restore + static_cast<DWORD>(restorePublished) == state.restoreSuccesses &&
          pending == textureState.pendingEpoch && completed == textureState.completedEpoch &&
          draw == Read(GameRva::drawObject) && primary == Read(GameRva::primary) &&
          backbuffer == Read(GameRva::backbuffer) && palette == Read(paletteRva) && app &&
          app == Read(GameRva::app) && viewObject && viewObject == Read(GameRva::view) &&
          frameObject && frameObject == *reinterpret_cast<DWORD *>(app + 0x1c) && frame &&
          frame == FrameWindow() && view && view == ViewWindow() &&
          drawWindow == Read(GameRva::drawWindow) && mission == state.depth &&
          content == state.contentActive && gameStage == Read(GameRva::gameStage) &&
          preference == Read(GameRva::hardwarePreference) &&
          quality == Read(rendererNativeQuality) && software == Read(GameRva::software);
      for (DWORD i = 0; valid && i < 12; ++i)
      {
        valid = cpu[i] == Read(cpuAllocationsRva + i * 4);
      }
      for (DWORD i = 0; valid && i < 4; ++i)
      {
        valid = !objects[i] || (tables[i] && adds[i] && drops[i] &&
                                tables[i] == *reinterpret_cast<DWORD *>(objects[i]));
      }
      return valid &&
             (!checkChildren ||
              (child[0] == Read(recoveryExecuteBuffer) && child[1] == Read(viewportRva) &&
               child[2] == Read(GameRva::legacyDevice) && child[3] == Read(GameRva::d3dRoot) &&
               creates == bridgeState.createAttempts && created == bridgeState.createSuccesses &&
               begins == bridgeState.beginCalls && ends == bridgeState.endCalls &&
               failures == bridgeState.failures && ClosedScene()));
    }

    bool Windows() const
    {
      if (!Current())
      {
        return false;
      }
      const BOOL first = Call(IsWindow, isWindow)(frame);
      if (!Current() || !first)
      {
        return false;
      }
      const BOOL second = Call(IsWindow, isWindow)(view);
      if (!Current() || !second)
      {
        return false;
      }
      const BOOL related = Call(IsChild, isChild)(frame, view);
      if (!Current() || !related)
      {
        return false;
      }
      const DWORD frameThread =
          Call(GetWindowThreadProcessId, getWindowThreadProcessId)(frame, nullptr);
      if (!Current() || frameThread != thread)
      {
        return false;
      }
      const DWORD viewThread =
          Call(GetWindowThreadProcessId, getWindowThreadProcessId)(view, nullptr);
      return Current() && viewThread == thread;
    }
  };

  static Owner context = {};

  struct SetupReadinessOwner
  {
    SurfaceRestoreOwnership::Owner surface;
    DWORD root = Read(GameRva::d3dRoot);
    DWORD generation = modeAdmissionState.ownerGeneration;
    DWORD transition = modeAdmissionState.transitionSerial;
    DWORD frameGeneration = normalFrameState.generation;
    DWORD quadGeneration = quadFrameRecoveryState.generation;
    DWORD flushGeneration = flushOwnerState.generation;
    DWORD flushInvalidations = flushOwnerState.invalidations;
    DWORD flushReentries = flushOwnerState.reentries;
    DWORD cachePasses = textureState.passes, cacheSuccesses = textureState.successes;
    DWORD cacheFailures = textureState.failures, loadCalls = loadState.calls;
    DWORD transferGeneration = TextureTransfer::textureTransferStats.generation;
    DWORD transferLoads = TextureTransfer::textureTransferStats.loads;
    DWORD transferInvalidations = TextureTransfer::textureTransferStats.invalidations;
    DWORD primaryLost = 0, primaryRestore = 0, backbufferLost = 0;

    SetupReadinessOwner()
    {
      if (context.tables[1] && context.tables[2])
      {
        const auto primary = reinterpret_cast<const DWORD *>(context.tables[1]);
        const auto backbuffer = reinterpret_cast<const DWORD *>(context.tables[2]);
        primaryLost = primary[24];
        primaryRestore = primary[27];
        backbufferLost = backbuffer[24];
      }
    }

    bool Current(bool restoring = false, bool restorePublished = false) const
    {
      if (!setupReadinessActive || setupReadinessInvalidated || scope.setup != 1 ||
          nativeDispatches || retained != 4 ||
          !context.Current(false, restoring, restorePublished) || !root ||
          root != Read(GameRva::d3dRoot) || Read(recoveryExecuteBuffer) || Read(viewportRva) ||
          Read(GameRva::legacyDevice) || context.child[0] || context.child[1] || context.child[2] ||
          context.child[3] || Read(recoveryExecuteAllocation) ||
          Read(recoveryExecuteInstructionBegin) || Read(recoveryExecuteInstructionEnd) ||
          Read(recoveryExecuteCopyRecord) || Read(recoveryExecuteVertexPointer) ||
          bridgeState.createAttempts != context.creates ||
          bridgeState.createSuccesses != context.created ||
          bridgeState.beginCalls != context.begins || bridgeState.endCalls != context.ends ||
          bridgeState.failures != context.failures || !ClosedScene() ||
          generation != modeAdmissionState.ownerGeneration ||
          transition != modeAdmissionState.transitionSerial ||
          frameGeneration != normalFrameState.generation ||
          quadGeneration != quadFrameRecoveryState.generation ||
          flushGeneration != flushOwnerState.generation ||
          flushInvalidations != flushOwnerState.invalidations ||
          flushReentries != flushOwnerState.reentries || cachePasses != textureState.passes ||
          cacheSuccesses != textureState.successes || cacheFailures != textureState.failures ||
          loadCalls != loadState.calls ||
          transferGeneration != TextureTransfer::textureTransferStats.generation ||
          transferLoads != TextureTransfer::textureTransferStats.loads ||
          transferInvalidations != TextureTransfer::textureTransferStats.invalidations ||
          !primaryLost || !primaryRestore || !backbufferLost)
      {
        return false;
      }
      const auto primary = reinterpret_cast<const DWORD *>(context.tables[1]);
      const auto backbuffer = reinterpret_cast<const DWORD *>(context.tables[2]);
      return primary[24] == primaryLost && primary[27] == primaryRestore &&
             backbuffer[24] == backbufferLost;
    }

    bool BudgetCurrent(DisplayReadiness::Budget &budget, bool restoring = false) const
    {
      if (!Current(restoring) || !budget.initialized || !budget.clock)
      {
        return false;
      }
      const DWORD now = budget.clock();
      return Current(restoring) && now - budget.started < DisplayReadiness::timeoutMilliseconds;
    }

    bool Restore(DisplayReadiness::Budget &budget)
    {
      if (context.restore == 0xffffffffu || !BudgetCurrent(budget))
      {
        return false;
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
      } restoring;

      auto current = [&]
      {
        return BudgetCurrent(budget, true) && surface.Current(5);
      };
      if (!current())
      {
        return false;
      }
      const HRESULT first =
          reinterpret_cast<Surface>(primaryLost)(reinterpret_cast<void *>(context.primary));
      if (!current() || (first != S_OK && first != DDERR_SURFACELOST))
      {
        return false;
      }
      const HRESULT second =
          reinterpret_cast<Surface>(backbufferLost)(reinterpret_cast<void *>(context.backbuffer));
      if (!current() || (second != S_OK && second != DDERR_SURFACELOST))
      {
        return false;
      }
      if (first == S_OK && second == S_OK)
      {
        return true;
      }
      const SurfaceRestoreOwnership::Methods methods(reinterpret_cast<DWORD *>(context.tables[1]),
                                                     reinterpret_cast<DWORD *>(context.tables[2]));
      const HRESULT restored = SurfaceRestoreOwnership::RestoreAndVerify(surface, methods, current);
      // Validate every other witness before adopting the only allowed mutation.
      // The old named texture bank is gone, so the later empty-bank completion
      // consumes this epoch without claiming that old textures were uploaded.
      if (restored != S_OK || !Current(true, true) || state.lastRestoreResult != S_OK ||
          state.lastPrimaryIsLost != S_OK || state.lastBackbufferIsLost != S_OK)
      {
        return false;
      }
      ++context.restore;
      ++surface.restoreEpoch;
      return current();
    }
  };

  static bool PrepareChildRenderer()
  {
    if (setupReadinessActive)
    {
      setupReadinessInvalidated = true;
      return false;
    }

    struct ReadinessScope
    {
      ReadinessScope()
      {
        setupReadinessActive = true;
        setupReadinessInvalidated = false;
      }

      ~ReadinessScope()
      {
        setupReadinessActive = false;
      }
    } active;

    SurfaceRestoreOwnership::LastErrorScope error;
    if (scope.setup != 1 || nativeDispatches || retained != 4 || !context.Current(false))
    {
      return false;
    }
    SetupReadinessOwner owner;
    if (!owner.Current())
    {
      return false;
    }
    DisplayReadiness::Budget budget;
    if (DisplayReadiness::Wait(DisplayReadiness::Phase::startup, &budget))
    {
      return owner.BudgetCurrent(budget);
    }
    if (!owner.Current() || readinessState.lastResult != static_cast<DWORD>(DDERR_SURFACELOST) ||
        (readinessState.lastStage != static_cast<DWORD>(DisplayReadiness::Stage::primaryLoss) &&
         readinessState.lastStage != static_cast<DWORD>(DisplayReadiness::Stage::backbufferLoss)) ||
        !owner.Restore(budget) || !owner.BudgetCurrent(budget))
    {
      return false;
    }
    return DisplayReadiness::Wait(DisplayReadiness::Phase::startup, &budget) &&
           owner.BudgetCurrent(budget);
  }

  static bool EmptyExecute()
  {
    const DWORD allocation = Read(recoveryExecuteAllocation),
                offset = Read(GameRva::instructionOffset);
    return Read(recoveryExecuteBuffer) && allocation && offset >= ExecutePadding::vertexBytes &&
           offset <= 0xffffffbfu && allocation <= 0xffffffffu - offset - 64 &&
           Read(GameRva::executeCapacity) >= offset + 64 && !Read(recoveryExecuteActualCount) &&
           Read(recoveryExecuteInstructionBegin) == allocation + offset &&
           Read(recoveryExecuteInstructionEnd) == allocation + offset + 64 &&
           Read(recoveryExecuteCopyRecord) == allocation + offset + 4 &&
           Read(recoveryExecuteVertexPointer) == allocation;
  }

  static bool DropReferences()
  {
    bool valid = true;
    while (retained)
    {
      const DWORD index = --retained;
      if (context.objects[index])
      {
        context.drops[index](reinterpret_cast<void *>(context.objects[index]));
        valid = context.Current() && valid;
      }
    }
    return valid;
  }

  __declspec(noreturn) static void Fatal()
  {
    const DWORD error = __readfsdword(0x34), base = context.base;
    // Only acquired references are ours after an invalidated owner. The game
    // owns normal fatal cleanup, so no half-rebuilt renderer can resume.
    DropReferences();
    __writefsdword(0x34, error);
    reinterpret_cast<void(__cdecl *)(const char *)>(base + GameRva::fatalError)(
        "Cannot rebuild hardware renderer");
    __assume(0);
  }

  static bool NewBank()
  {
    const DWORD count = Read(GameRva::textureCount);
    if (count < 96 || count > 1024 || Read(GameRva::textureDimension) != 64 ||
        !Read(GameRva::textureStagingSurface) || !Read(GameRva::textureStagingInterface) ||
        Read(GameRva::textureSelectedSlot) != 0xffffffffu || !EmptyExecute())
    {
      return false;
    }
    for (DWORD i = 0; i < 1024; ++i)
    {
      if (*reinterpret_cast<const BYTE *>(context.base + GameRva::textureGpuNames + i * 20))
      {
        return false;
      }
      if (i < count &&
          (!Read(GameRva::textureSurfaceTable + i * 4) ||
           !Read(GameRva::textureInterfaceTable + i * 4) || !Read(GameRva::textureHandles + i * 4)))
      {
        return false;
      }
    }
    return true;
  }

  static bool HealthySurface(DWORD object, DWORD rva)
  {
    if (!context.Current() || !object || Read(rva) != object)
    {
      return false;
    }
    const auto table = *reinterpret_cast<const DWORD *const *>(object);
    if (!table || !table[0x60 / 4])
    {
      return false;
    }
    const auto query = reinterpret_cast<Surface>(table[0x60 / 4]);
    const HRESULT result = query(reinterpret_cast<void *>(object));
    return context.Current() && Read(rva) == object && result == S_OK;
  }

  static bool BankIdentitiesCurrent()
  {
    if (!context.Current() || !NewBank() || newCount != Read(GameRva::textureCount) ||
        newStaging != Read(GameRva::textureStagingSurface) ||
        newStagingTexture != Read(GameRva::textureStagingInterface))
    {
      return false;
    }
    for (DWORD i = 0; i < newCount; ++i)
    {
      if (newSlots[i].surface != Read(GameRva::textureSurfaceTable + i * 4) ||
          newSlots[i].texture != Read(GameRva::textureInterfaceTable + i * 4) ||
          newSlots[i].handle != Read(GameRva::textureHandles + i * 4))
      {
        return false;
      }
    }
    return true;
  }

  static bool ValidateNewBank()
  {
    if (!context.Current() || !NewBank())
    {
      return false;
    }
    newCount = Read(GameRva::textureCount);
    newStaging = Read(GameRva::textureStagingSurface);
    newStagingTexture = Read(GameRva::textureStagingInterface);
    for (DWORD i = 0; i < newCount; ++i)
    {
      newSlots[i] = {Read(GameRva::textureSurfaceTable + i * 4),
                     Read(GameRva::textureInterfaceTable + i * 4),
                     Read(GameRva::textureHandles + i * 4)};
    }
    if (!HealthySurface(context.primary, GameRva::primary) ||
        !HealthySurface(context.backbuffer, GameRva::backbuffer) ||
        !HealthySurface(newStaging, GameRva::textureStagingSurface))
    {
      return false;
    }
    for (DWORD i = 0; i < newCount; ++i)
    {
      if (!HealthySurface(newSlots[i].surface, GameRva::textureSurfaceTable + i * 4))
      {
        return false;
      }
    }
    return BankIdentitiesCurrent();
  }

  static void Rebuild()
  {
    context.Capture();
    scope.busy = 1;
    scope.setup = 0;
    nativeDispatches = retained = 0;
    if (!context.Windows() || !EmptyExecute())
    {
      scope.busy = 0;
      return;
    }
    for (DWORD i = 0; i < 4; ++i)
    {
      retained = i + 1;
      if (context.objects[i])
      {
        context.adds[i](reinterpret_cast<void *>(context.objects[i]));
        if (!context.Current())
        {
          Fatal();
        }
      }
    }
    const DWORD oldExecute = context.child[0];
    const auto executeMethods = *reinterpret_cast<const DWORD *const *>(oldExecute);
    if (!executeMethods || !executeMethods[5] || !executeMethods[2])
    {
      Fatal();
    }
    const HRESULT unlocked =
        reinterpret_cast<Surface>(executeMethods[5])(reinterpret_cast<void *>(oldExecute));
    if (unlocked != S_OK || !context.Current())
    {
      Fatal();
    }
    reinterpret_cast<void(__cdecl *)()>(context.base + rendererNativeTextureTeardown)();
    if (!context.Current())
    {
      Fatal();
    }
    constexpr DWORD childRvas[] = {recoveryExecuteBuffer, viewportRva, GameRva::legacyDevice,
                                   GameRva::d3dRoot};
    for (DWORD i = 0; i < 4; ++i)
    {
      const DWORD object = context.child[i];
      if (!object)
      {
        Fatal();
      }
      const auto table = *reinterpret_cast<const DWORD *const *>(object);
      if (!table || !table[2])
      {
        Fatal();
      }
      reinterpret_cast<Reference>(table[2])(reinterpret_cast<void *>(object));
      if (!context.Current())
      {
        Fatal();
      }
      Write(childRvas[i], 0);
      context.child[i] = 0;
    }
    // Native reset publishes new lock-owned pointers. The old allocation is
    // unusable once its execute buffer has been released.
    Write(recoveryExecuteAllocation, 0);
    Write(recoveryExecuteInstructionBegin, 0);
    Write(recoveryExecuteInstructionEnd, 0);
    Write(recoveryExecuteCopyRecord, 0);
    Write(recoveryExecuteVertexPointer, 0);
    scope.setup = 1;
    const int result = reinterpret_cast<Native>(context.base + rendererNativeChildSetup)();
    scope.setup = 0;
    if (result != 1 || nativeDispatches != 1 || !context.Current(false) ||
        bridgeState.createAttempts != context.creates + 1 ||
        bridgeState.createSuccesses != context.created + 1 ||
        bridgeState.beginCalls != context.begins + 1 || bridgeState.endCalls != context.ends + 1 ||
        bridgeState.failures != context.failures || !ClosedScene())
    {
      Fatal();
    }
    for (DWORD i = 0; i < 4; ++i)
    {
      context.child[i] = Read(childRvas[i]);
      if (!context.child[i])
      {
        Fatal();
      }
    }
    ++context.creates;
    ++context.created;
    ++context.begins;
    ++context.ends;
    if (!context.Windows() || !ValidateNewBank())
    {
      Fatal();
    }
    if (!DropReferences() || !BankIdentitiesCurrent())
    {
      Fatal();
    }
    // The old named bank is gone. Native material lookup fills the new unnamed
    // bank from the unchanged CPU cache during the next genuine frame.
    textureState.pendingEpoch = textureState.completedEpoch = context.restore;
    AdvanceTextureFrameEpoch();
    baseline.device = context.child[2];
    baseline.execute = context.child[0];
    baseline.created = context.created;
    baseline.restored = context.restore;
    confirmationArmed = false;
    flipToken = 0;
    scope.awaitingFrame = 1;
    scope.busy = 0;
  }
} // namespace PostRestoreRenderer

extern "C" int __cdecl PrepareRenderer()
{
  if (PostRestoreRenderer::scope.busy)
  {
    return PostRestoreRenderer::PrepareChildRenderer();
  }
  return !Configured() || DisplayReadiness::Wait(DisplayReadiness::Phase::startup);
}

extern "C" BOOL __cdecl PostRestoreRendererBusy()
{
  return PostRestoreRenderer::scope.busy != 0;
}

extern "C" void __cdecl StampPostRestoreRenderer(DWORD token, DWORD root)
{
  using namespace PostRestoreRenderer;
  const auto &owned = FullscreenSetup::scopeState;
  // Called only after the real full setup succeeds in the owned mode-two root
  // transaction. The native caller publishes fullscreen flags after our return.
  if (!Configured() || scope.busy || !owned.busy || !(token & 1) ||
      (token >> 1) != owned.activeToken || owned.activeThread != TextureLifecycle::ThreadId() ||
      !root || root != Read(GameRva::drawObject) || !Read(GameRva::hardware))
  {
    return;
  }
  baseline = {config.gameBase,
              TextureLifecycle::ThreadId(),
              Read(GameRva::drawObject),
              Read(GameRva::primary),
              Read(GameRva::backbuffer),
              Read(paletteRva),
              Read(GameRva::legacyDevice),
              Read(recoveryExecuteBuffer),
              bridgeState.createSuccesses,
              state.restoreSuccesses,
              true};
  baseline.valid = baseline.draw && baseline.primary && baseline.backbuffer && baseline.device &&
                   baseline.execute && baseline.primary != baseline.backbuffer;
  confirmationArmed = false;
  flipToken = 0;
  flipPresentationToken = 0;
  scope.awaitingFrame = 0;
}

extern "C" int __cdecl CallRendererInitForScope()
{
  using namespace PostRestoreRenderer;
  if (!scope.busy)
  {
    return reinterpret_cast<Native>(config.rendererInitOriginal)();
  }
  if (!scope.setup || nativeDispatches || context.lifecycle == 0xffffffffu ||
      TextureLifecycle::Serial() != context.lifecycle + 1)
  {
    return 0;
  }
  ++context.lifecycle;
  if (!context.Current(false) || context.child[0] || context.child[1] || context.child[2] ||
      !Read(GameRva::d3dRoot) || bridgeState.createAttempts != context.creates ||
      bridgeState.createSuccesses != context.created || bridgeState.beginCalls != context.begins ||
      bridgeState.endCalls != context.ends || bridgeState.failures != context.failures ||
      !ClosedScene() || *reinterpret_cast<const volatile BYTE *>(guardedRendererImage) != 0x83)
  {
    return 0;
  }
  ++nativeDispatches;
  return reinterpret_cast<Native>(reinterpret_cast<DWORD>(guardedRendererImage))();
}

extern "C" DWORD __cdecl PostRestoreBeforeFrame(const DWORD *registers)
{
  using namespace PostRestoreRenderer;
  if (scope.busy)
  {
    return 0;
  }
  if (!Hardware() || !BaselineCurrent())
  {
    confirmationArmed = false;
    flipToken = 0;
    flipPresentationToken = 0;
    scope.awaitingFrame = 0;
    return 1;
  }
  if (!Quiet() || !ClosedScene())
  {
    confirmationArmed = false;
    return 1;
  }
  DWORD caller = 0;
  if (!registers || !QuadFrameRecovery::StackRead(registers[3] + 4, &caller, 4) ||
      caller != config.gameBase + recoveryOuterFrameReturn)
  {
    return 1;
  }
  const DWORD error = __readfsdword(0x34);
  if (!scope.awaitingFrame && state.restoreSuccesses > baseline.restored && EmptyExecute() &&
      TextureLifecycle::Serial() < 0xffffffffu && TextureLifecycle::FrameEpoch() < 0xffffffffu &&
      bridgeState.createAttempts < 0xffffffffu && bridgeState.createSuccesses < 0xffffffffu &&
      bridgeState.beginCalls < 0xffffffffu && bridgeState.endCalls < 0xffffffffu)
  {
    Rebuild();
  }
  if (scope.awaitingFrame && normalFrameState.generation < 0xffffffffu)
  {
    // A later loss invalidates only the pending confirmation. Existing loss
    // recovery must finish before another real frame and Flip can confirm.
    confirmationGeneration = normalFrameState.generation + 1;
    confirmationFrame = TextureLifecycle::FrameSerial() + 1;
    confirmationEpoch = TextureLifecycle::FrameEpoch();
    confirmationRestore = state.restoreSuccesses;
    confirmationBegin = bridgeState.beginCalls;
    confirmationEnd = bridgeState.endCalls;
    confirmationFailures = bridgeState.failures;
    confirmationOwner = HardwareModeCompletion::CaptureOwner();
    confirmationArmed = true;
    flipToken = 0;
    flipPresentationToken = 0;
    scope.awaitingFrame = 1;
  }
  __writefsdword(0x34, error);
  return 1;
}

extern "C" void __cdecl PostRestoreAfterFrame()
{
  using namespace PostRestoreRenderer;
  if (!scope.awaitingFrame || !confirmationArmed)
  {
    return;
  }
  confirmationArmed = false;
  const auto &frame = normalFrameState;
  if (!ConfirmationCurrent() || !Quiet() || frame.depth || frame.armed || !frame.frameReturned ||
      frame.invalidated || frame.incomplete || frame.generation != confirmationGeneration ||
      confirmationFrame != TextureLifecycle::FrameSerial() ||
      confirmationEpoch != TextureLifecycle::FrameEpoch() ||
      confirmationRestore != state.restoreSuccesses ||
      bridgeState.failures != confirmationFailures || bridgeState.beginCalls <= confirmationBegin ||
      bridgeState.endCalls <= confirmationEnd || !quadFrameRecoveryState.frameReturned ||
      quadFrameRecoveryState.awaitingFrame || quadFrameRecoveryState.pendingPresent)
  {
    return;
  }
  confirmedBegin = bridgeState.beginCalls;
  confirmedEnd = bridgeState.endCalls;
  confirmedUpload = TextureLifecycle::UploadSerial();
  scope.awaitingFrame = 2;
}

extern "C" DWORD __cdecl CapturePostRestoreFlip(
    DWORD surface, DWORD target, DWORD flags, DWORD caller, DWORD depth, DWORD call)
{
  using namespace PostRestoreRenderer;
  if (PostRestoreRenderer::scope.awaitingFrame != 2 || !ConfirmationCurrent() ||
      !PresentationCurrent(FailedPresentation::scopeToken, call) ||
      PostRestoreRenderer::scope.busy || surface != baseline.primary || target || flags != 1 ||
      caller != config.gameBase + recoveryGenericFlipCaller || depth != 1 ||
      normalFrameState.generation != confirmationGeneration ||
      confirmationFrame != TextureLifecycle::FrameSerial() ||
      confirmationEpoch != TextureLifecycle::FrameEpoch() ||
      confirmationRestore != state.restoreSuccesses || !ClosedScene() ||
      confirmedBegin != bridgeState.beginCalls || confirmedEnd != bridgeState.endCalls ||
      confirmedUpload != TextureLifecycle::UploadSerial() ||
      bridgeState.failures != confirmationFailures || nextFlipToken == 0xffffffffu)
  {
    return 0;
  }
  flipCall = call;
  flipNested = flipProbeState.nested;
  flipPresentationToken = FailedPresentation::scopeToken;
  flipToken = ++nextFlipToken;
  return flipToken;
}

extern "C" void __cdecl CompletePostRestoreFlip(DWORD token, HRESULT result)
{
  using namespace PostRestoreRenderer;
  if (!token || token != flipToken)
  {
    return;
  }
  const DWORD presentationToken = flipPresentationToken;
  flipToken = 0;
  flipPresentationToken = 0;
  const bool valid =
      result == S_OK && ConfirmationCurrent() && PresentationCurrent(presentationToken, flipCall) &&
      flipProbeState.busy == 1 && flipProbeState.calls == flipCall &&
      flipProbeState.nested == flipNested && PostRestoreRenderer::scope.awaitingFrame == 2 &&
      normalFrameState.generation == confirmationGeneration &&
      confirmationFrame == TextureLifecycle::FrameSerial() &&
      confirmationEpoch == TextureLifecycle::FrameEpoch() &&
      confirmationRestore == state.restoreSuccesses && confirmedBegin == bridgeState.beginCalls &&
      confirmedEnd == bridgeState.endCalls && confirmedUpload == TextureLifecycle::UploadSerial() &&
      bridgeState.failures == confirmationFailures;
  if (valid)
  {
    PostRestoreRenderer::scope.awaitingFrame = 0;
  }
  else
  {
    PostRestoreRenderer::scope.awaitingFrame = 1;
  }
}

#endif
