// SPDX-License-Identifier: MIT

// Runs the retail terminal Unlock and EndScene cleanup paths in a private image.
// Driver results and callbacks are supplied by owned COM fixtures, never a live game.
#include "support/ownedReadinessFixture.hpp"
#include "frameNative.hpp"
#include "presentationNative.hpp"

static Object Execute, StageSurface, StageTexture, Destination, Texture, SceneDevice;
static DWORD ExecuteMethods[40], DeviceMethods[40], SceneMethods[40], TextureMethods[40];
static HRESULT SceneResult;
static unsigned Restores, NativeLocks, CleanupCalls, FatalCalls;
static bool RawActive;
static std::function<void()> CleanupCallback, SceneEndCallback;
static DWORD GeometryTail;

struct FatalSignal
{
};

static void Jump(BYTE *at, DWORD target, BYTE opcode = 0xe9)
{
  at[0] = opcode;
  *reinterpret_cast<DWORD *>(at + 1) = target - reinterpret_cast<DWORD>(at) - 5;
}

static void __cdecl FakeFatal(const char *)
{
  ++FatalCalls;
  if (RawActive)
  {
    std::fprintf(stderr, "Raw native fatal stage%lu reject%lu kind%lu return%lu\n",
                 quadFrameRecoveryState.lastStage, quadFrameRecoveryState.rejectReason,
                 quadFrameRecoveryState.callerKind, quadFrameRecoveryState.returnStage);
    ExitProcess(97);
  }
  throw FatalSignal{};
}

static bool CatchFatal(const std::function<void()> &action)
{
  try
  {
    action();
  }
  catch (const FatalSignal &)
  {
    return true;
  }
  return false;
}

static HRESULT WINAPI NativeRestore(void *object)
{
  Check(object == &Active->primary, "Recovery restored a foreign surface");
  ++Restores;
  Active->primaryResult = Active->backbufferResult = S_OK;
  Active->Notify(Operation::Restore);
  return S_OK;
}

static HRESULT WINAPI NativeLock(void *object, DWORD *description)
{
  Check(object == &Execute && description[0] == 20, "Native reset Lock ABI changed");
  ++NativeLocks;
  description[3] = Read(recoveryExecuteCapacity);
  description[4] = Read(recoveryExecuteAllocation);
  return S_OK;
}

static HRESULT WINAPI TextureLost(void *)
{
  return S_OK;
}

static HRESULT WINAPI TextureDescription(void *, DWORD *out)
{
  Check(out[0] == 108, "Texture descriptor size changed");
  out[1] = 0x1007;
  out[2] = out[3] = 64;
  out[18] = 32;
  out[19] = 0x40;
  out[21] = 16;
  out[22] = 0xf800;
  out[23] = 0x7e0;
  out[24] = 0x1f;
  return S_OK;
}

static HRESULT WINAPI UncalledLoad(void *, void *)
{
  Check(false, "Unnamed cache unexpectedly uploaded a texture");
  return E_FAIL;
}

static HRESULT WINAPI SceneQuery(void *device, const GUID *id, void **out)
{
  Check(device == &Active->draw4 && std::memcmp(id, &D3DBridge::device2Id, sizeof(GUID)) == 0,
        "Actual bridge queried a different interface");
  FakeAdd(&SceneDevice);
  *out = &SceneDevice;
  return S_OK;
}

static HRESULT WINAPI SceneBegin(void *)
{
  return S_OK;
}

static HRESULT WINAPI SceneEnd(void *)
{
  if (SceneEndCallback)
  {
    SceneEndCallback();
  }
  return SceneResult;
}

static HRESULT __cdecl RawEnd()
{
  Check(BeginSceneBridge(&Active->draw4) == S_OK, "Actual BeginScene failed");
  return EndSceneBridge(&Active->draw4);
}

static __declspec(naked) void RawGeometry()
{
  __asm {
    push ebx
    push edi
    call RawEnd
    jmp dword ptr [GeometryTail]
  }
}

static int __cdecl NativeCleanup()
{
  ++CleanupCalls;
  if (CleanupCallback)
  {
    CleanupCallback();
  }
  return 1;
}

static void __cdecl EmptyPump() {}

static FARPROC WINAPI Procedure(HMODULE module, LPCSTR name)
{
  if (std::strcmp(name, "VirtualQuery") == 0)
  {
    return reinterpret_cast<FARPROC>(&::VirtualQuery);
  }
  return FakeProcedure(module, name);
}

static void Prepare(Fixture &f)
{
  quadFrameRecoveryState = {};
  bridgeState = {};
  movieRecoveryState = {};
  preMovieRecoveryState = {};
  modeAdmissionState = {};
  hardwareModeCompletionState = {};
  softwareModeCompletionState = {};
  failedPresentationState = {};
  FullscreenSetup::scopeState = {};
  UnlockFrame::scope = {};
  PostRestoreRenderer::scope = {};
  PostRestoreRenderer::baseline = {};
  normalFrameState = {};
  flushOwnerState = {};
  QuadFrameRecovery::ResetContext();
  QuadFrameRecovery::resetActive = 0;
  Restores = NativeLocks = CleanupCalls = FatalCalls = 0;
  CleanupCallback = {};
  SceneEndCallback = {};
  RawActive = false;
  SceneResult = DDERR_SURFACELOST;
  state.blocked = 1;
  Write(GameRva::hardwarePreference, 1);
  state.restoreSuccesses = 7;
  textureState.pendingEpoch = textureState.completedEpoch = 7;
  textureState.passes = textureState.successes = 3;
  textureLifecycleState.serial = 8;
  textureLifecycleState.frameEpoch = 9;
  textureLifecycleState.frameSerial = 10;
  textureLifecycleState.uploadSerial = 11;
  bridgeState.beginCalls = bridgeState.endCalls = 100;
  bridgeState.lastStage = D3DBridge::end;
  bridgeState.lastResult = S_OK;
  f.methods[27] = reinterpret_cast<DWORD>(&NativeRestore);
  std::memcpy(DeviceMethods, f.methods, sizeof(DeviceMethods));
  DeviceMethods[0] = reinterpret_cast<DWORD>(&SceneQuery);
  f.draw4.table = DeviceMethods;
  std::memcpy(SceneMethods, f.methods, sizeof(SceneMethods));
  SceneMethods[10] = reinterpret_cast<DWORD>(&SceneBegin);
  SceneMethods[11] = reinterpret_cast<DWORD>(&SceneEnd);
  SceneDevice = {};
  SceneDevice.table = SceneMethods;
  std::memcpy(ExecuteMethods, f.methods, sizeof(ExecuteMethods));
  ExecuteMethods[4] = reinterpret_cast<DWORD>(&NativeLock);
  Execute = {};
  Execute.table = ExecuteMethods;
  std::memcpy(TextureMethods, f.methods, sizeof(TextureMethods));
  TextureMethods[24] = reinterpret_cast<DWORD>(&TextureLost);
  TextureMethods[22] = reinterpret_cast<DWORD>(&TextureDescription);
  TextureMethods[6] = reinterpret_cast<DWORD>(&UncalledLoad);
  StageSurface = {};
  StageTexture = {};
  Destination = {};
  Texture = {};
  StageSurface.table = StageTexture.table = Destination.table = Texture.table = TextureMethods;
  Write(GameRva::getProcAddress, reinterpret_cast<DWORD>(&Procedure));
  Write(GameRva::legacyDevice, reinterpret_cast<DWORD>(&f.draw4));
  Write(recoveryExecuteBuffer, reinterpret_cast<DWORD>(&Execute));
  Write(recoveryNativeActive, 1);
  Write(recoveryNativeSurfaceWidth, 640);
  Write(recoveryNativeSurfaceHeight, 480);
  Write(GameRva::textureCount, 1);
  Write(GameRva::textureDimension, 64);
  Write(GameRva::textureStagingSurface, reinterpret_cast<DWORD>(&StageSurface));
  Write(GameRva::textureStagingInterface, reinterpret_cast<DWORD>(&StageTexture));
  Write(GameRva::textureSurfaceTable, reinterpret_cast<DWORD>(&Destination));
  Write(GameRva::textureInterfaceTable, reinterpret_cast<DWORD>(&Texture));
  Write(GameRva::uploadGuard0, 1);
  Write(GameRva::uploadGuard1, 1);
  Write(GameRva::uploadGuard2, 1);
  Write(GameRva::textureSelectedSlot, 0xffffffffu);
  Write(recoveryExecuteAllocation, reinterpret_cast<DWORD>(f.image + 0x410000));
  Write(recoveryExecuteCapacity, 0x30000);
  Write(GameRva::instructionOffset, 0x20020);
  Write(GameRva::vertexLimit, 0x800);
  Write(recoveryExecuteActualCount, 4);
  const DWORD allocation = Read(recoveryExecuteAllocation);
  Write(recoveryExecuteVertexPointer, allocation);
  Write(recoveryExecuteInstructionBegin, allocation + 0x20020);
  Write(recoveryExecuteInstructionEnd, allocation + 0x20060);
  Write(recoveryExecuteCopyRecord, allocation + 0x20024);
  Write(UnlockFixtureRva::rowWidth, 640);
  Write(UnlockFixtureRva::rowHeight, 480);
  Write(UnlockFixtureRva::cpuSource, reinterpret_cast<DWORD>(f.image + 0x450000));
  DWORD old;
  Check(VirtualProtect(f.image, 0x600000, PAGE_EXECUTE_READWRITE, &old) != 0,
        "Fixture code protection failed");
  for (const auto &span : NativeSpans)
  {
    std::memcpy(f.image + span.rva, span.bytes, span.size);
    for (DWORD i = 0; i < span.count; ++i)
    {
      *reinterpret_cast<DWORD *>(f.image + span.rva + span.relocations[i]) +=
          config.gameBase - 0x400000u;
    }
  }
  Jump(f.image + GameRva::fatalError, reinterpret_cast<DWORD>(&FakeFatal));
  Jump(f.image + readinessFlushEntry, reinterpret_cast<DWORD>(&NativeCleanup));
  Jump(f.image + UnlockFixtureRva::geometry, reinterpret_cast<DWORD>(&RawGeometry));
  Jump(f.image + UnlockFixtureRva::endSceneHook, reinterpret_cast<DWORD>(&EndSceneFailureHook));
  Jump(f.image + recoveryQuadResetResume - 10, reinterpret_cast<DWORD>(&QuadResetReturnHook));
  for (DWORD i = 5; i < 10; ++i)
  {
    f.image[recoveryQuadResetResume - 10 + i] = 0x90;
  }
  Jump(f.image + recoveryOuterFrameReturn - 5, reinterpret_cast<DWORD>(&FrameHook), 0xe8);
  f.image[recoveryOuterFrameReturn] = 0xc3;
  const BYTE frameProlog[] = {0x83, 0xec, 0x50};
  std::memcpy(f.image + 0x350000, frameProlog, 3);
  Jump(f.image + 0x350003, config.gameBase + recoveryFrameAbortReturn - 5);
  config.frameOriginal = config.gameBase + 0x350000;
  GeometryTail = config.gameBase + UnlockFixtureRva::geometryFailure;
  Jump(f.image + recoveryMessagePumpOriginal, reinterpret_cast<DWORD>(&EmptyPump));
  f.primaryResult = f.backbufferResult = DDERR_SURFACELOST;
  SetLastError(0x12345678);
}

struct Route
{
  DWORD words[64] = {}, registers[8] = {}, entry[8] = {};

  void Arm(DWORD kind = 4)
  {
    const DWORD owner = kind == 1 ? 45 : 25;
    words[owner] = 0x12345000;
    words[owner + 1] = config.gameBase + recoveryOuterFrameReturn;
    entry[3] = reinterpret_cast<DWORD>(words + owner + 1) - 4;
    BeginOwnedFrame(entry, words[owner]);
    BeginRecoveryFrame(entry, words[owner]);
    Check(QuadFrameRecovery::SameOwner(), "Actual frame owner did not arm");
    words[2] = config.gameBase + recoveryFlushCleanup;
    words[3] = config.gameBase + recoveryCpuRowsCleanup;
    words[4] = config.gameBase + (kind == 4   ? recoveryFrameAbortReturn
                                  : kind == 2 ? recoveryWorldGate
                                              : recoveryQuadGate);
    if (kind == 1)
    {
      words[23] = config.gameBase + recoveryHudGate;
      words[24] = config.gameBase + recoveryFrameGate;
    }
    registers[3] = reinterpret_cast<DWORD>(words) - 4;
    registers[7] = static_cast<DWORD>(RawEnd());
  }

  DWORD Abort()
  {
    return TryAbortQuadFrame(registers);
  }

  void Cleanup()
  {
    Write(GameRva::renderBpp, 8);
  }

  void Returned()
  {
    MarkFrameReturned(entry);
  }
};

static DWORD SurfaceUnlocks, ExecuteUnlocks;
static HRESULT SurfaceUnlockResult, ExecuteUnlockResult;

static HRESULT WINAPI ActualSurfaceUnlock(void *surface, void *rectangle)
{
  Check(surface == &Active->backbuffer && !rectangle, "Native surface Unlock ABI changed");
  ++SurfaceUnlocks;
  Active->Notify(Operation::SurfaceUnlock);
  return SurfaceUnlockResult;
}

static HRESULT WINAPI ActualExecuteUnlock(void *execute)
{
  Check(execute == &Execute, "Discard unlocked a foreign execute buffer");
  ++ExecuteUnlocks;
  Active->Notify(Operation::ExecuteUnlock);
  return ExecuteUnlockResult;
}

static void PrepareUnlock(Fixture &f)
{
  Prepare(f);
  SurfaceUnlocks = ExecuteUnlocks = 0;
  SurfaceUnlockResult = DDERR_SURFACELOST;
  ExecuteUnlockResult = SceneResult = S_OK;
  f.methods[32] = reinterpret_cast<DWORD>(&ActualSurfaceUnlock);
  ExecuteMethods[5] = reinterpret_cast<DWORD>(&ActualExecuteUnlock);
  for (const auto &span : NativeSpans)
  {
    if (span.rva != readinessFlushEntry && span.rva != UnlockFixtureRva::geometry)
    {
      continue;
    }
    std::memcpy(f.image + span.rva, span.bytes, span.size);
    for (DWORD i = 0; i < span.count; ++i)
    {
      *reinterpret_cast<DWORD *>(f.image + span.rva + span.relocations[i]) +=
          config.gameBase - 0x400000u;
    }
  }
  Jump(f.image + readinessFlushReturn - 5, reinterpret_cast<DWORD>(&FlushUnlockHook), 0xe8);
  Jump(f.image + readinessUnlockResume - 5, reinterpret_cast<DWORD>(&OwnedUnlockResultHook));
  Jump(f.image + UnlockFixtureRva::endSceneHook, reinterpret_cast<DWORD>(&EndSceneFailureHook));
  config.unlockOriginal = config.gameBase + readinessFlushEntry;
  Write(GameRva::drawLock, 1);
  Write(GameRva::drawSession, 0);
  Write(GameRva::textureLock, 0);
  Write(UnlockFixtureRva::drawFlag, 1);
  Check(FlushInstructionCache(GetCurrentProcess(), f.image, 0x600000) != FALSE,
        "Native fixture instruction cache failed");
}

static void CheckReferences(Fixture &f)
{
  for (Object *object : {&f.draw, &f.primary, &f.backbuffer, &f.draw4, &Execute})
  {
    Check(object->references == 1 && object->additions == object->releases,
          "Discard leaked a retained reference");
  }
}

static void RunNativeFrame(Fixture &f)
{
  RawActive = true;
  reinterpret_cast<void(__cdecl *)()>(f.image + recoveryOuterFrameReturn - 5)();
  RawActive = false;
}

struct UnlockRoute
{
  DWORD storage[128] = {}, entry[12] = {}, rawRegisters[12] = {}, frame[12] = {};
  DWORD *words = storage + 32;
  DWORD token = 0;

  void Arm()
  {
    const DWORD stack = reinterpret_cast<DWORD>(words);
    words[0] = config.gameBase + readinessFlushReturn;
    words[3] = config.gameBase + recoveryFlushCleanup;
    words[4] = config.gameBase + recoveryCpuRowsCleanup;
    words[5] = config.gameBase + recoveryFrameAbortReturn;
    words[26] = 0x12345000;
    words[27] = config.gameBase + recoveryOuterFrameReturn;
    frame[3] = reinterpret_cast<DWORD>(words + 27) - 4;
    BeginOwnedFrame(frame, words[26]);
    BeginRecoveryFrame(frame, words[26]);
    Check(NormalFrameOwnership::SameOwner() && QuadFrameRecovery::SameOwner(),
          "Terminal owner did not arm");
    entry[3] = stack - 12;
    entry[10] = config.gameBase + readinessFlushEntry;
    token = BeginOwnedFlush(entry, 0x12346000);
    Check(token != 0, "Actual private flush owner did not arm");
    entry[9] = token;
    DWORD *raw = words - 8;
    raw[1] = config.gameBase + readinessUnlockReturn;
    raw[2] = Read(GameRva::backbuffer);
    raw[5] = 0x12346000;
    raw[6] = token;
    raw[7] = config.gameBase + readinessFlushEntry;
    rawRegisters[3] = reinterpret_cast<DWORD>(raw) - 8;
    rawRegisters[7] = static_cast<DWORD>(DDERR_SURFACELOST);
    RecordOwnedUnlock(rawRegisters);
    AfterOwnedFlush(entry, token);
    Check(flushOwnerState.branchKind == 4 && flushOwnerState.activeValid &&
              flushOwnerState.rawOwned,
          "Private flush did not classify raw loss and native BOOL zero");
  }

  DWORD Abort(DWORD supplied = 0)
  {
    return TryAbortUnlockFrame(entry, supplied ? supplied : token);
  }
};

static void CasesForBase()
{
  {
    Fixture f;
    PrepareUnlock(f);
    RunNativeFrame(f);
    Check(!FatalCalls && SurfaceUnlocks == 1 && ExecuteUnlocks == 1 && !UnlockFrameBusy(),
          "Actual terminal cleanup failed");
    Check(flushOwnerState.rawResult == static_cast<DWORD>(DDERR_SURFACELOST) &&
              flushOwnerState.nativeBool == 0 && !flushOwnerState.active,
          "Native HRESULT or BOOL changed");
    Check(quadFrameRecoveryState.callerKind == 5 && quadFrameRecoveryState.returnStage == 7 &&
              quadFrameRecoveryState.successes == 1 && Restores == 1 && NativeLocks == 1,
          "Native cleanup and frame repair did not complete");
    Check(Read(GameRva::drawLock) == 0 && Read(GameRva::renderBpp) == 8 &&
              Read(UnlockFixtureRva::cpuDestination) == Read(UnlockFixtureRva::cpuSource),
          "Native CPU row cleanup failed");
    Check(bridgeState.beginCalls == 100 && bridgeState.endCalls == 100 &&
              bridgeState.lastResult == S_OK,
          "Discard submitted an extra scene");
    Check(quadFrameRecoveryState.pendingPresent && quadFrameRecoveryState.awaitingFrame &&
              PresentRecoveryFrame() == 0,
          "Discarded frame was presentable");
    CheckReferences(f);
    Check(!normalFrameState.depth && !normalFrameState.armed,
          "Frame wrapper retained ownership after return");
    SceneResult = S_OK;
    Write(GameRva::renderBpp, 16);
    AdvanceTextureFrame();
    Route fresh;
    fresh.Arm();
    fresh.Cleanup();
    fresh.Returned();
    FinishRecoveryFrame();
    Check(!quadFrameRecoveryState.awaitingFrame && quadFrameRecoveryState.confirmedFrames == 1,
          "Fresh completed scene/frame did not confirm repair");
    Cases.emplace_back("native-terminal-unlock-cleanup-CPU-rows-frame-reset-fresh-confirmation");
  }
  for (Operation point : {Operation::Window, Operation::ExecuteUnlock, Operation::Release})
  {
    Fixture f;
    PrepareUnlock(f);
    unsigned calls = 0;
    EachOperation = [&](Operation op)
    {
      if (op != point || !UnlockFrameBusy())
      {
        return;
      }
      DWORD transition = state.transitionDepth, generation = normalFrameState.generation;
      reinterpret_cast<void(__thiscall *)(void *, int)>(&ModeHook)(
          reinterpret_cast<void *>(Read(GameRva::app)), 0);
      FrameHook();
      Check(state.transitionDepth == transition && normalFrameState.generation == generation,
            "Nested mode/frame escaped discard fence");
      ++calls;
    };
    RunNativeFrame(f);
    Check(calls && !FatalCalls && !UnlockFrameBusy() && Restores == 1,
          "Reentrant discard did not finish");
    CheckReferences(f);
    Cases.emplace_back("native-unlock-reentry-" + std::to_string(static_cast<unsigned>(point)));
  }
  for (HRESULT result : {S_OK, DDERR_NOTLOCKED, E_FAIL, DDERR_SURFACELOST})
  {
    Fixture f;
    PrepareUnlock(f);
    SurfaceUnlockResult = result;
    int actual = FlushUnlockHook();
    bool accepted = result == S_OK || result == DDERR_NOTLOCKED;
    Check(actual == static_cast<int>(accepted) && SurfaceUnlocks == 1 && !ExecuteUnlocks &&
              !Restores,
          "Foreign ancestry changed actual native result");
    Check(Read(GameRva::drawLock) == static_cast<DWORD>(!accepted) &&
              flushOwnerState.rawResult == static_cast<DWORD>(result),
          "Foreign raw result/bookkeeping changed");
    Cases.emplace_back("native-foreign-unlock-result-" +
                       std::to_string(static_cast<DWORD>(result)));
  }
  for (unsigned fault = 0; fault < 9; ++fault)
  {
    Fixture f;
    PrepareUnlock(f);
    UnlockRoute r;
    r.Arm();
    DWORD token = r.token;
    switch (fault)
    {
    case 0:
      ++token;
      break;
    case 1:
      ++r.words[5];
      break;
    case 2:
      normalFrameState.invalidated = 1;
      break;
    case 3:
      ++textureLifecycleState.serial;
      break;
    case 4:
      ++flushOwnerState.rawCalls;
      break;
    case 5:
      flushOwnerState.rawSurface = Read(GameRva::primary);
      break;
    case 6:
      ++bridgeState.beginCalls;
      break;
    case 7:
      quadFrameRecoveryState.awaitingFrame = 1;
      break;
    case 8:
      flushOwnerState.rawResult = static_cast<DWORD>(E_FAIL);
      break;
    }
    Check(!r.Abort(token) && !ExecuteUnlocks && !Restores && !NativeLocks && !UnlockFrameBusy(),
          "Foreign owner or raw result entered discard");
    CheckReferences(f);
    Cases.emplace_back("unlock-qualified-owner-negative-" + std::to_string(fault));
  }
  for (unsigned fault = 0; fault < 3; ++fault)
  {
    Fixture f;
    PrepareUnlock(f);
    UnlockRoute r;
    r.Arm();
    if (fault == 0)
    {
      ExecuteUnlockResult = E_FAIL;
    }
    else
    {
      f.On(Operation::ExecuteUnlock, [&]
      {
        if (fault == 1)
        {
          ++textureLifecycleState.serial;
        }
        else
        {
          ++r.words[26];
        }
      });
    }
    Check(CatchFatal([&] { r.Abort(); }) && FatalCalls == 1 && ExecuteUnlocks == 1 && !Restores,
          "Post-cleanup failure resumed native submission");
    Check(Read(GameRva::drawLock) == 1 && !quadFrameRecoveryState.aborted,
          "Failed cleanup committed discard");
    CheckReferences(f);
    Cases.emplace_back("unlock-post-cleanup-failclosed-" + std::to_string(fault));
  }
  {
    Fixture f;
    PrepareUnlock(f);
    UnlockRoute r;
    r.Arm();
    f.On(Operation::Add, [&] { ++textureLifecycleState.serial; });
    Check(!r.Abort() && !ExecuteUnlocks && !Restores && Read(GameRva::drawLock) == 1 &&
              !UnlockFrameBusy(),
          "Pre-cleanup mutation altered native failure path");
    CheckReferences(f);
    Cases.emplace_back("unlock-pre-cleanup-mutation-declines");
  }
  {
    Fixture f;
    Prepare(f);
    RunNativeFrame(f);
    Check(!FatalCalls && quadFrameRecoveryState.callerKind == 4 &&
              quadFrameRecoveryState.returnStage == 6 && Restores == 1 && NativeLocks == 1 &&
              CleanupCalls == 1,
          "Existing native EndScene terminal route regressed");
    CheckReferences(f);
    Cases.emplace_back("native-final-EndScene-route-preserved");
  }
}

namespace FrameRestoreCases
{
  enum class Kind
  {
    Healthy,
    PriorRestoreHealthy,
    PriorRestoreLoss,
    LockFailure,
    Invalidated
  };
  static Kind kind;
  static bool normalBegin, afterFirstLock;
  static DWORD locks, scenes, presents, receiptAfterLock, lockError, entryRestore, entrySame;

  static HRESULT WINAPI
  Lock(void *object, void *rectangle, DWORD *description, DWORD flags, void *event)
  {
    Check(object == &Active->backbuffer && !rectangle && !event && flags == 1 &&
              description[0] == 108,
          "Native surface Lock contract changed");
    ++locks;
    lockError = GetLastError();
    description[4] = 1280;
    description[9] = reinterpret_cast<DWORD>(Active->image + 0x400000);
    SetLastError(0x1234abcd);
    return kind == Kind::LockFailure ? E_FAIL : S_OK;
  }

  static HRESULT WINAPI SetData(void *, DWORD *data)
  {
    Check(data[0] == 48, "Native execute data size");
    return S_OK;
  }

  static HRESULT WINAPI Execute(void *, void *, void *, DWORD)
  {
    return S_OK;
  }

  static HRESULT WINAPI Flip(void *, void *target, DWORD flags)
  {
    Check(!target && flags == 1, "Native Flip contract changed");
    ++presents;
    return S_OK;
  }

  static BOOL WINAPI WindowRect(HWND, RECT *r)
  {
    *r = {0, 0, 640, 480};
    return TRUE;
  }

  static int __cdecl AppTail()
  {
    return 1;
  }

  static void PreparePresentation(Fixture &f)
  {
    for (const auto &span : PresentSpans)
    {
      std::memcpy(f.image + span.rva, span.bytes, span.size);
    }
    for (DWORD r : PresentRelocations)
    {
      *reinterpret_cast<DWORD *>(f.image + r) += config.gameBase - 0x400000u;
    }
    Jump(f.image + PresentRva::flipHook, reinterpret_cast<DWORD>(&FlipProbeHook), 0xe8);
    Jump(f.image + PresentRva::outer, reinterpret_cast<DWORD>(&PresentRecoveryFrame), 0xe8);
    f.image[presentationGameReturn] = 0xc3;
    std::memcpy(f.image + PresentRva::postApp, "\x33\xc0\xc3", 3);
    Write(PresentRva::appGlobal, Read(GameRva::app));
    const DWORD table = config.gameBase + 0x383000;
    *reinterpret_cast<DWORD *>(Read(GameRva::app)) = table;
    *reinterpret_cast<DWORD *>(table + 0xc4) = reinterpret_cast<DWORD>(&SoftwarePresentHook);
    *reinterpret_cast<DWORD *>(table + 0x60) = reinterpret_cast<DWORD>(&AppTail);
    Write(PresentRva::windowRect, reinterpret_cast<DWORD>(&WindowRect));
    Write(PresentRva::height, 480);
    Write(recoveryNativePausedAliasLegacyVideo, 0);
    f.methods[11] = reinterpret_cast<DWORD>(&Flip);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  }

  static void Present(Fixture &f)
  {
    PreparePresentation(f);
    Check(reinterpret_cast<int(__cdecl *)()>(config.gameBase + PresentRva::outer)() == 0,
          "Native presentation return changed");
    Check(presents == 1 && !FailedPresentation::scopeDepth, "Native presentation did not retire");
  }

  static void __cdecl FirstPass()
  {
    Check(NormalFrameOwnership::SameOwner() && !NormalFrameOwnership::restoreReceipt.phase,
          "Fresh native frame ownership did not arm");
    Write(GameRva::drawLock, 0);
    Write(GameRva::drawSession, normalBegin ? 0 : 1);
    SetLastError(0x77112233);
    using Native = void(__cdecl *)();
    if (normalBegin)
    {
      if (kind != Kind::Healthy)
      {
        Active->backbufferResult = DDERR_SURFACELOST;
      }
      reinterpret_cast<Native>(config.gameBase + PreLockNative::begin.rva)();
    }
    else
    {
      reinterpret_cast<Native>(config.gameBase + UnlockFixtureRva::geometry)();
    }
    Check(lockError == (normalBegin ? 0x77112233u : 0x87654321u) && GetLastError() == 0x1234abcd,
          "Restore observation changed native LastError");
    Check(locks == 1 && Read(GameRva::drawLock) == 1 && Read(GameRva::drawSession) == 1,
          "Original native Lock did not finish exactly once");
    receiptAfterLock = NormalFrameOwnership::restoreReceipt.phase;
    Write(recoveryExecuteActualCount, 4);
    afterFirstLock = true;
  }

  static void Configure(Fixture &f, Kind selected)
  {
    PrepareUnlock(f);
    worldRelockState = {};
    kind = selected;
    afterFirstLock = false;
    locks = scenes = presents = receiptAfterLock = entryRestore = entrySame = 0;
    state.restoreAttempts = state.restoreFailures = 0;
    state.restoreSuccesses = textureState.pendingEpoch = textureState.completedEpoch = 9;
    SurfaceUnlockResult = S_OK;
    f.primaryResult = f.backbufferResult = S_OK;
    f.methods[25] = reinterpret_cast<DWORD>(&Lock);
    ExecuteMethods[6] = reinterpret_cast<DWORD>(&SetData);
    DeviceMethods[19] = reinterpret_cast<DWORD>(&BeginSceneBridge);
    DeviceMethods[20] = reinterpret_cast<DWORD>(&EndSceneBridge);
    DeviceMethods[8] = reinterpret_cast<DWORD>(&Execute);
    for (const auto &span : PreLockNative::spans)
    {
      std::memcpy(f.image + span.rva, span.data, span.size);
      for (DWORD i = 0; i < span.count; ++i)
      {
        *reinterpret_cast<DWORD *>(f.image + span.rva + span.relocations[i]) +=
            config.gameBase - 0x400000u;
      }
    }
    const auto &start = PreLockNative::begin;
    std::memcpy(f.image + start.rva, start.data, start.size);
    for (DWORD i = 0; i < start.count; ++i)
    {
      *reinterpret_cast<DWORD *>(f.image + start.rva + start.relocations[i]) +=
          config.gameBase - 0x400000u;
    }
    Jump(f.image + PreLockNative::hook, reinterpret_cast<DWORD>(&BackbufferLockHook), 0xe8);
    Jump(f.image + recoveryLockResultResume - 5,
         reinterpret_cast<DWORD>(&RawSurfaceLockResultHook));
    config.unlockResume = config.gameBase + readinessUnlockResume - 0x10;
    Jump(f.image + readinessUnlockResume - 0x15, reinterpret_cast<DWORD>(&SurfaceUnlockHook));
    Write(PreLockNative::height, 480);
    Jump(f.image + 0x350003, reinterpret_cast<DWORD>(&FirstPass), 0xe8);
    Jump(f.image + 0x350008, config.gameBase + recoveryFrameAbortReturn - 5);
    SceneEndCallback = []
    {
      ++scenes;
      if (!normalBegin && scenes == 1 && kind != Kind::Healthy)
      {
        Active->backbufferResult = DDERR_SURFACELOST;
      }
    };
    EachOperation = [](Operation operation)
    {
      if (operation == Operation::Restore && Restores == 1 && kind == Kind::Invalidated)
      {
        ++flushOwnerState.invalidations;
      }
      if (operation == Operation::SurfaceUnlock && afterFirstLock)
      {
        Check(!Read(GameRva::drawSession), "Native final cleanup did not clear the session");
        entryRestore = FlushOwnership::entry.restoreSuccesses;
        entrySame = FlushOwnership::entry.sameOwner;
        if (kind == Kind::PriorRestoreLoss || kind == Kind::Invalidated)
        {
          Active->primaryResult = DDERR_SURFACELOST;
        }
      }
    };
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  }

  static void Case(Kind selected, const char *name)
  {
    Fixture f;
    Configure(f, selected);
    const bool fatal = CatchFatal([&]
    { reinterpret_cast<void(__cdecl *)()>(f.image + recoveryOuterFrameReturn - 5)(); });
    const bool rejected = selected == Kind::LockFailure || selected == Kind::Invalidated;
    Check(fatal == rejected, "Frame restore receipt disposition changed");
    if (selected == Kind::PriorRestoreLoss)
    {
      Check(entryRestore == 10 && !entrySame && NormalFrameOwnership::ownerRestore == 9,
            "Native stale frame/flush epochs not reproduced");
      Check(receiptAfterLock == 3 && Restores == 2,
            "Readiness did not use the verified prior restore");
    }
    if (selected == Kind::PriorRestoreHealthy)
    {
      Check(Restores == 1 && receiptAfterLock == 3, "Healthy later flush repeated Restore");
    }
    if (selected == Kind::Healthy)
    {
      Check(!Restores && !receiptAfterLock, "Healthy Lock fabricated restore proof");
    }
    if (rejected)
    {
      Check(Restores == 1 && !NormalFrameOwnership::restoreReceipt.phase,
            "Failed or invalidated Lock retained proof");
    }
    else
    {
      Check(SurfaceUnlocks == 1 && scenes == (normalBegin ? 1u : 2u) && !normalFrameState.depth &&
                !NormalFrameOwnership::restoreReceipt.phase && Read(GameRva::renderBpp) == 8,
            "Native frame cleanup did not retire proof");
      CheckReferences(f);
      Present(f);
    }
    Cases.push_back(name);
  }

  static void Run()
  {
    for (bool begin : {true, false})
    {
      normalBegin = begin;
      Case(Kind::Healthy, "FrameRestoreHealthy");
      Case(Kind::PriorRestoreHealthy, "FrameRestoreStillReady");
      Case(Kind::PriorRestoreLoss, "FrameRestoreLostAgain");
      Case(Kind::LockFailure, "FrameRestoreLockFailure");
      Case(Kind::Invalidated, "FrameRestoreInvalidated");
    }
  }
} // namespace FrameRestoreCases

namespace MovieCleanupCases
{
  enum class Kind
  {
    Healthy,
    Lost,
    PriorRestoreLost,
    OwnerChanged,
    MethodChanged,
    WrongCaller,
    NonVideo,
    Deadline
  };
  static Kind kind;
  static DWORD locks, scenes, submissions, loopTarget, endDispatcher;
  static Object destinations[240], textures[240];

  static HRESULT WINAPI
  Lock(void *surface, void *rectangle, DWORD *description, DWORD flags, void *event)
  {
    Check(surface == &Active->backbuffer && !rectangle && !event && flags == 1 &&
              description[0] == 108,
          "Movie cleanup native surface Lock ABI changed");
    ++locks;
    description[4] = 1280;
    description[9] = reinterpret_cast<DWORD>(Active->image + 0x400000);
    if (locks == 1 && kind == Kind::PriorRestoreLost)
    {
      Active->backbufferResult = DDERR_SURFACELOST;
    }
    SetLastError(0x1234abcd);
    return S_OK;
  }

  static HRESULT WINAPI SetData(void *buffer, DWORD *data)
  {
    Check(buffer == &::Execute && data[0] == 48 && data[2] >= 4,
          "Movie cleanup did not submit its native black quad");
    return S_OK;
  }

  static HRESULT WINAPI AlternateSetData(void *buffer, DWORD *data)
  {
    return SetData(buffer, data);
  }

  static HRESULT WINAPI Submit(void *device, void *buffer, void *, DWORD flags)
  {
    Check(device == &Active->draw4 && buffer == &::Execute && flags == 2,
          "Movie cleanup native Execute ABI changed");
    ++submissions;
    return S_OK;
  }

  static __declspec(naked) void ForeignDispatcher()
  {
    __asm {
      call dword ptr [endDispatcher]
      ret
    }
  }

  static __declspec(naked) void Loop()
  {
    __asm {
      push ebx
      push esi
      push edi
      xor ebx, ebx
      call dword ptr [loopTarget]
      pop edi
      pop esi
      pop ebx
      ret
    }
  }

  template <class Span> static void Install(Fixture &f, const Span &span)
  {
    std::memcpy(f.image + span.rva, span.data, span.size);
    for (DWORD i = 0; i < span.count; ++i)
    {
      *reinterpret_cast<DWORD *>(f.image + span.rva + span.relocations[i]) +=
          config.gameBase - 0x400000u;
    }
  }

  static void Configure(Fixture &f, Kind selected)
  {
    PrepareUnlock(f);
    kind = selected;
    locks = scenes = submissions = FrameRestoreCases::presents = 0;
    worldRelockState = {};
    emptyCacheDeferralState = {};
    surfaceRestoreGuardState = {};
    LostSurfaceReadiness::cleanupScope = {};
    state.restoreAttempts = state.restoreFailures = 0;
    state.restoreSuccesses = 2;
    textureState.pendingEpoch = 1;
    textureState.completedEpoch = textureState.passes = textureState.successes =
        textureState.failures = 0;
    textureLifecycleState.frameSerial = textureLifecycleState.uploadSerial = 0;
    state.videoDepth = selected == Kind::NonVideo ? 0 : 1;
    state.videoReady = 1;
    state.frame = f.frame;
    state.view = f.view;
    SurfaceUnlockResult = S_OK;
    f.primaryResult = f.backbufferResult = S_OK;
    f.methods[25] = reinterpret_cast<DWORD>(&Lock);
    ExecuteMethods[6] = reinterpret_cast<DWORD>(&SetData);
    DeviceMethods[19] = reinterpret_cast<DWORD>(&BeginSceneBridge);
    DeviceMethods[20] = reinterpret_cast<DWORD>(&EndSceneBridge);
    DeviceMethods[8] = reinterpret_cast<DWORD>(&Submit);
    for (const auto &span : PreLockNative::spans)
    {
      Install(f, span);
    }
    Install(f, PreLockNative::begin);
    for (const auto &span : MovieCleanupNative::spans)
    {
      Install(f, span);
    }
    Jump(f.image + PreLockNative::hook, reinterpret_cast<DWORD>(&BackbufferLockHook), 0xe8);
    Jump(f.image + recoveryLockResultResume - 5,
         reinterpret_cast<DWORD>(&RawSurfaceLockResultHook));
    config.unlockResume = config.gameBase + readinessUnlockResume - 0x10;
    Jump(f.image + readinessUnlockResume - 0x15, reinterpret_cast<DWORD>(&SurfaceUnlockHook));
    config.clearPolygonOriginal = config.gameBase + MovieCleanupNative::quadEmitter;
    Jump(f.image + MovieCleanupNative::clearPolygonHook, reinterpret_cast<DWORD>(&ClearPolygonHook),
         0xe8);
    f.image[MovieCleanupNative::loopEnd] = 0xc3;
    loopTarget = config.gameBase + MovieCleanupNative::cleanupLoop;
    endDispatcher = config.gameBase + MovieCleanupNative::endDispatcher;
    if (selected == Kind::WrongCaller)
    {
      Jump(f.image + MovieCleanupNative::cleanupLoopReturn - 5,
           reinterpret_cast<DWORD>(&ForeignDispatcher), 0xe8);
    }
    Write(PreLockNative::height, 480);
    Write(GameRva::textureCount, 240);
    for (DWORD i = 0; i < 240; ++i)
    {
      destinations[i] = {};
      textures[i] = {};
      destinations[i].table = textures[i].table = TextureMethods;
      Write(GameRva::textureSurfaceTable + 4 * i, reinterpret_cast<DWORD>(&destinations[i]));
      Write(GameRva::textureInterfaceTable + 4 * i, reinterpret_cast<DWORD>(&textures[i]));
      Write(GameRva::textureHandles + 4 * i, i + 1);
    }
    Write(GameRva::drawLock, 0);
    Write(GameRva::drawSession, 0);
    FrameRestoreCases::PreparePresentation(f);
    SceneEndCallback = []
    {
      ++scenes;
    };
    EachOperation = [](Operation operation)
    {
      if (operation == Operation::SurfaceUnlock && SurfaceUnlocks == 1)
      {
        if (kind != Kind::Healthy)
        {
          Active->primaryResult = DDERR_SURFACELOST;
        }
        if (kind == Kind::MethodChanged)
        {
          ExecuteMethods[6] = reinterpret_cast<DWORD>(&AlternateSetData);
        }
      }
      if (operation == Operation::Restore)
      {
        if (kind == Kind::OwnerChanged)
        {
          ++textureLifecycleState.frameEpoch;
        }
        if (kind == Kind::Deadline)
        {
          Active->now += 5000;
        }
      }
    };
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  }

  static void Case(Kind selected, const char *name)
  {
    Fixture f;
    Configure(f, selected);
    const bool fatal = CatchFatal([] { Loop(); });
    const bool success =
        selected == Kind::Healthy || selected == Kind::Lost || selected == Kind::PriorRestoreLost;
    Check(fatal != success, "Movie cleanup readiness disposition changed");
    if (success)
    {
      const unsigned expected = selected == Kind::PriorRestoreLost ? 2u
                                : selected == Kind::Lost           ? 1u
                                                                   : 0u;
      Check(Restores == expected && state.restoreAttempts == expected,
            "Movie cleanup changed the bounded genuine Restore count");
      Check(locks == 3 && SurfaceUnlocks == 3 && ExecuteUnlocks == 3 && scenes == 3 &&
                submissions == 3 && FrameRestoreCases::presents == 3,
            "Movie cleanup did not finish its three native black-quad presentations");
      Check(!normalFrameState.depth && !normalFrameState.generation && state.videoDepth == 1 &&
                !LostSurfaceReadiness::cleanupScope.token,
            "Movie cleanup left an owner or invented a normal frame");
      Check(!textureState.passes && !textureState.completedEpoch &&
                !textureLifecycleState.uploadSerial,
            "Movie cleanup invented texture uploads");
    }
    else
    {
      Check(!submissions && !scenes && !FrameRestoreCases::presents && Restores <= 1,
            "Unqualified movie cleanup continued or repeated Restore");
    }
    CheckReferences(f);
    Cases.push_back(name);
  }

  static void Run()
  {
    Case(Kind::Healthy, "MovieCleanupHealthy");
    Case(Kind::Lost, "MovieCleanupLostPrimary");
    Case(Kind::PriorRestoreLost, "MovieCleanupPriorRestoreLostAgain");
    Case(Kind::OwnerChanged, "MovieCleanupOwnerChanged");
    Case(Kind::MethodChanged, "MovieCleanupMethodChanged");
    Case(Kind::WrongCaller, "MovieCleanupWrongCaller");
    Case(Kind::NonVideo, "MovieCleanupNonVideo");
    Case(Kind::Deadline, "MovieCleanupSharedDeadline");
  }
} // namespace MovieCleanupCases

static LONG WINAPI Exception(EXCEPTION_POINTERS *p)
{
  std::fprintf(
      stderr,
      "Fixture exception %08lx RVA %08lx stage %lu kind %lu branch %lu normal invalid %lu\n",
      p->ExceptionRecord->ExceptionCode, p->ContextRecord->Eip - config.gameBase,
      quadFrameRecoveryState.lastStage, quadFrameRecoveryState.callerKind,
      flushOwnerState.branchKind, normalFrameState.invalidated);
  return EXCEPTION_EXECUTE_HANDLER;
}

int main()
{
  SetUnhandledExceptionFilter(&Exception);
  try
  {
    for (DWORD base : {0x20000000u, 0x30000000u})
    {
      RequestedBase = base;
      CasesForBase();
      FrameRestoreCases::Run();
      MovieCleanupCases::Run();
    }
    std::printf("{\"passed\":true,\"mappedBases\":2,\"checks\":%u,\"cases\":[", Checks);
    for (size_t i = 0; i < Cases.size(); ++i)
    {
      std::printf("%s\"%s\"", i ? "," : "", Cases[i].c_str());
    }
    std::puts("]}");
    return 0;
  }
  catch (const std::exception &e)
  {
    std::fprintf(stderr, "%s stage%lu kind%lu reject%lu\n", e.what(),
                 quadFrameRecoveryState.lastStage, quadFrameRecoveryState.callerKind,
                 quadFrameRecoveryState.rejectReason);
  }
  catch (const FatalSignal &)
  {
    std::fprintf(stderr, "Unexpected fatal stage%lu kind%lu reject%lu\n",
                 quadFrameRecoveryState.lastStage, quadFrameRecoveryState.callerKind,
                 quadFrameRecoveryState.rejectReason);
  }
  return 1;
}
