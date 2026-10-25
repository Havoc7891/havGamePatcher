// SPDX-License-Identifier: MIT

// Runs retail child setup, presentation and input-pump spans in a private image.
// COM, window queries and the native mode-change body are controlled fixture boundaries.
// Initial mode cases are independent dispatch tests, not a continuous display-mode cycle.
#include "support/ownedReadinessFixture.hpp"
#include "rendererNative.hpp"
#include "presentationNative.hpp"
#include "inputNative.hpp"
#include <memory>
static_assert(sizeof(NormalFrameState) == 40, "Lean frame ownership ABI");
static_assert(sizeof(FlushOwnerState) == 92, "Lean flush ownership ABI");
static_assert(sizeof(FullscreenSetup::ScopeState) == 16, "Private root ABI");
static_assert(sizeof(HardwareModeCompletionState) == 168, "Lean hardware completion ABI");
static_assert(sizeof(SoftwareModeCompletionState) == 228, "Lean software completion ABI");

namespace Transaction
{
  struct FatalError
  {
  };
  enum class Fault
  {
    None,
    OldUnlock,
    BankCreate,
    MainExecute,
    ParentMutation,
    NewSurfaceLost
  };

  struct Com
  {
    DWORD *table = nullptr;
    ULONG refs = 0, adds = 0, drops = 0;
    DWORD identity = 0;
    bool locked = false;
    std::vector<BYTE> bytes;
  };

  struct Environment
  {
    Fixture fixture;
    DWORD rootTable[9] = {}, root2Table[9] = {}, device2Table[12] = {}, deviceTable[21] = {},
          viewportTable[6] = {}, executeTable[7] = {}, surfaceTable[25] = {};
    Com oldRoot, oldDevice, oldViewport, oldExecute, root, root2, device2, device, viewport,
        palette, oldStaging, oldTexture;
    std::vector<std::unique_ptr<Com>> allocations;
    DWORD surfaceCreates = 0, executeCreates = 0, executeCalls = 0, lazyUploads = 0,
          nativeEnumCalls = 0, formatCalls = 0, forbidden = 0;
    Fault fault = Fault::None;
    BYTE savedClone[460] = {};
    DWORD cloneProtect = 0;
    bool testReentry = false;
    Environment();
    ~Environment();
  };

  static Environment *env = nullptr;
  static bool readinessTestEnabled = false;
  static void InjectReadinessAtRootQuery();
  static HRESULT WINAPI RestorePrimary(void *pointer);

  static void Relative(BYTE *at, DWORD target, BYTE op = 0xe8)
  {
    at[0] = op;
    *reinterpret_cast<DWORD *>(at + 1) = target - reinterpret_cast<DWORD>(at + 5);
  }

  static ULONG WINAPI Add(void *p)
  {
    auto o = static_cast<Com *>(p);
    ++o->adds;
    return ++o->refs;
  }

  static ULONG WINAPI Release(void *p)
  {
    auto o = static_cast<Com *>(p);
    Check(o->refs != 0, "Child reference ownership");
    ++o->drops;
    return --o->refs;
  }

  static HRESULT ReturnObject(void **out, Com &o)
  {
    *out = &o;
    Add(&o);
    return S_OK;
  }

  static HRESULT WINAPI RootQuery(void *, const GUID *i, void **out)
  {
    Check(!std::memcmp(i, &D3DBridge::root2Id, sizeof(GUID)), "Root IID");
    return ReturnObject(out, env->root2);
  }

  static HRESULT WINAPI DrawQuery(void *p, const GUID *i, void **out)
  {
    if (reinterpret_cast<DWORD>(i) == config.gameBase + FixtureRva::drawIid)
    {
      InjectReadinessAtRootQuery();
      return ReturnObject(out, env->root);
    }
    return FakeQuery(p, *i, out);
  }

  static HRESULT WINAPI Device2Query(void *, const GUID *i, void **out)
  {
    Check(!std::memcmp(i, &D3DBridge::device1Id, sizeof(GUID)), "Legacy IID");
    return ReturnObject(out, env->device);
  }

  static HRESULT WINAPI DeviceQuery(void *, const GUID *i, void **out)
  {
    Check(!std::memcmp(i, &D3DBridge::device2Id, sizeof(GUID)), "Scene IID");
    return ReturnObject(out, env->device2);
  }

  static HRESULT WINAPI CreateDevice(void *, const GUID *, void *s, void **out)
  {
    Check(s == &env->fixture.backbuffer, "Retained target changed");
    return ReturnObject(out, env->device2);
  }

  static HRESULT WINAPI EnumDevices(void *, DWORD callback, void *context)
  {
    ++env->nativeEnumCalls;
    DWORD d[43] = {};
    d[0] = sizeof(d);
    d[2] = 1;
    d[26] = 0x10;
    d[27] = 0x81;
    d[32] = 0xc000a;
    d[33] = 1;
    GUID id = {};
    using F = HRESULT(WINAPI *)(GUID *, const char *, const char *, DWORD *, DWORD *, void *);
    reinterpret_cast<F>(callback)(&id, "fixture", "fixture", d, d, context);
    return S_OK;
  }

  static HRESULT WINAPI EnumFormats(void *, DWORD callback, void *context)
  {
    ++env->formatCalls;
    DDSURFACEDESC d = {};
    d.dwSize = sizeof(d);
    d.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    d.ddpfPixelFormat.dwFlags = DDPF_RGB;
    d.ddpfPixelFormat.dwRGBBitCount = 16;
    d.ddpfPixelFormat.dwRBitMask = 0xf800;
    d.ddpfPixelFormat.dwGBitMask = 0x07e0;
    d.ddpfPixelFormat.dwBBitMask = 0x001f;
    using F = HRESULT(WINAPI *)(DDSURFACEDESC *, void *);
    reinterpret_cast<F>(callback)(&d, context);
    return S_OK;
  }

  static HRESULT WINAPI CreateViewport(void *, void **out, void *)
  {
    return ReturnObject(out, env->viewport);
  }

  static HRESULT WINAPI AddViewport(void *, void *)
  {
    return S_OK;
  }

  static HRESULT WINAPI SetViewport(void *, const DWORD *d)
  {
    Check(d[0] == 44 && d[3] == 640 && d[4] == 480, "Viewport dimensions");
    return S_OK;
  }

  static HRESULT WINAPI Scene(void *)
  {
    return S_OK;
  }

  static HRESULT WINAPI SurfaceQuery(void *p, const GUID *, void **out)
  {
    return ReturnObject(out, *static_cast<Com *>(p));
  }

  static HRESULT WINAPI SurfaceLost(void *)
  {
    return env->fault == Fault::NewSurfaceLost ? DDERR_SURFACELOST : S_OK;
  }

  static HRESULT WINAPI Handle(void *p, void *, DWORD *out)
  {
    *out = static_cast<Com *>(p)->identity;
    return S_OK;
  }

  static HRESULT WINAPI CreateSurface(void *, const DDSURFACEDESC *d, void **out, void *)
  {
    ++env->surfaceCreates;
    *out = nullptr;
    if (env->fault == Fault::BankCreate && env->surfaceCreates == 4)
    {
      return E_OUTOFMEMORY;
    }
    Check(d->dwWidth == 64 && d->dwHeight == 64, "Bank dimensions");
    auto p = std::make_unique<Com>();
    p->table = env->surfaceTable;
    p->identity = 100 + env->surfaceCreates;
    ReturnObject(out, *p);
    env->allocations.push_back(std::move(p));
    return S_OK;
  }

  static HRESULT WINAPI Lock(void *p, DWORD *d)
  {
    auto o = static_cast<Com *>(p);
    Check(!o->locked, "Double execute lock");
    o->locked = true;
    d[4] = reinterpret_cast<DWORD>(o->bytes.data());
    return S_OK;
  }

  static void CheckBusyReentry()
  {
    const DWORD frames = TextureLifecycle::FrameSerial(),
                transitions = modeAdmissionState.transitionSerial;
    const DWORD owner = modeAdmissionState.ownerGeneration;
    DWORD before = 0, after = 0;
    __asm {
 mov before,esp
 push 1
 mov ecx,0x1234
 call ModeHook
 mov after,esp
    }
    Check(before == after, "Nested ModeHook stack imbalance");
    FrameHook();
    Check(frames == TextureLifecycle::FrameSerial() &&
              transitions == modeAdmissionState.transitionSerial &&
              owner == modeAdmissionState.ownerGeneration,
          "Nested input advanced ownership");
    Check(!normalFrameState.depth && !normalFrameState.generation,
          "Nested frame crossed ownership gate");
  }

  static HRESULT WINAPI Unlock(void *p)
  {
    auto o = static_cast<Com *>(p);
    Check(o->locked, "Unlock without lock");
    if (o == &env->oldExecute && env->fault == Fault::OldUnlock)
    {
      return DDERR_SURFACELOST;
    }
    o->locked = false;
    if (o == &env->oldExecute && env->testReentry)
    {
      CheckBusyReentry();
    }
    if (o == &env->oldExecute && env->fault == Fault::ParentMutation)
    {
      Write(GameRva::drawWindow, 0);
    }
    return S_OK;
  }

  static HRESULT WINAPI ExecuteData(void *, const DWORD *)
  {
    return S_OK;
  }

  static HRESULT WINAPI Execute(void *, void *p, void *, DWORD)
  {
    Check(!static_cast<Com *>(p)->locked, "Execute locked buffer");
    ++env->executeCalls;
    return S_OK;
  }

  static HRESULT WINAPI CreateExecute(void *, const DWORD *d, void **out, void *)
  {
    ++env->executeCreates;
    *out = nullptr;
    if (env->fault == Fault::MainExecute && env->executeCreates == 2)
    {
      return E_OUTOFMEMORY;
    }
    auto p = std::make_unique<Com>();
    p->table = env->executeTable;
    p->bytes.resize(0x50000);
    Check(d[0] == 20, "Execute descriptor");
    ReturnObject(out, *p);
    env->allocations.push_back(std::move(p));
    return S_OK;
  }

  static void __cdecl Fatal(const char *)
  {
    throw FatalError();
  }

  static void __cdecl Forbidden()
  {
    ++env->forbidden;
    throw std::runtime_error("CPU fallback entered");
  }

  static bool restoreDuringFrame = false;

  static void __cdecl FrameBody()
  {
    using Native = int(__cdecl *)();
    Check(reinterpret_cast<Native>(config.gameBase + FixtureRva::initialState)() == 1,
          "Fresh native scene execution failed");
    if (restoreDuringFrame)
    {
      ++state.restoreSuccesses;
    }
  }

  static DWORD flipMutation = 0;
  static DWORD rawCalls = 0;
  static bool nestedRaw = false;

  static BOOL WINAPI NativeRect(HWND, RECT *r)
  {
    *r = {0, 0, 640, 480};
    return TRUE;
  }

  static int __cdecl NativeAppTail()
  {
    return 1;
  }

  static void InstallPresentation(Environment &e)
  {
    for (const auto &s : PresentSpans)
    {
      std::memcpy(e.fixture.image + s.rva, s.bytes, s.size);
    }
    for (DWORD r : PresentRelocations)
    {
      *reinterpret_cast<DWORD *>(e.fixture.image + r) += config.gameBase - 0x400000;
    }
    Relative(e.fixture.image + PresentRva::flipHook, reinterpret_cast<DWORD>(&FlipProbeHook));
    Relative(e.fixture.image + PresentRva::outer, reinterpret_cast<DWORD>(&PresentRecoveryFrame));
    e.fixture.image[presentationGameReturn] = 0xc3;
    std::memcpy(e.fixture.image + PresentRva::postApp, "\x33\xc0\xc3", 3);
    Write(PresentRva::appGlobal, Read(GameRva::app));
    const DWORD table = config.gameBase + 0x383000;
    *reinterpret_cast<DWORD *>(Read(GameRva::app)) = table;
    *reinterpret_cast<DWORD *>(table + 0xc4) = reinterpret_cast<DWORD>(&SoftwarePresentHook);
    *reinterpret_cast<DWORD *>(table + 0x60) = reinterpret_cast<DWORD>(&NativeAppTail);
    Write(PresentRva::windowRect, reinterpret_cast<DWORD>(&NativeRect));
    Write(PresentRva::height, 480);
    // Force the normal gameplay hardware branch, not the movie surface path.
    Write(recoveryNativePausedAliasLegacyVideo, 0);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  }

  static HRESULT WINAPI RawFlip(void *, void *target, DWORD flags)
  {
    Check(!target && flags == 1, "Raw present parameters");
    ++rawCalls;
    if (nestedRaw)
    {
      return S_OK;
    }
    Check(FailedPresentation::scopeDepth == 1 && FailedPresentation::ScopeCurrent(),
          "Actual presentation scope missing");
    Check(FailedPresentation::flipValid && FailedPresentation::FrameCurrent(),
          "Actual native chain did not qualify");
    Check(PostRestoreRenderer::flipPresentationToken == FailedPresentation::scopeToken,
          "Presentation token not pinned");
    if (flipMutation == 5)
    {
      ++FailedPresentation::scopeToken;
    }
    if (flipMutation == 6)
    {
      ++failedPresentationState.generation;
    }
    if (flipMutation == 7)
    {
      ++normalFrameState.generation;
    }
    if (flipMutation == 8)
    {
      ++modeAdmissionState.ownerGeneration;
    }
    if (flipMutation == 9)
    {
      nestedRaw = true;
      ObserveFlip(&env->fixture.primary, nullptr, 1, config.gameBase + recoveryGenericFlipCaller);
      nestedRaw = false;
    }
    if (flipMutation == 10)
    {
      const DWORD token = BeginFailedPresentationScope(nullptr);
      EndFailedPresentationScope(token);
    }
    if (flipMutation == 11)
    {
      ++FailedPresentation::flipOrdinal;
    }
    if (flipMutation == 13)
    {
      ++FailedPresentation::nativeContinuation;
    }
    if (flipMutation == 12)
    {
      ++textureLifecycleState.frameSerial;
    }

    if (flipMutation == 1)
    {
      Write(recoveryNativeActive, 0);
    }
    if (flipMutation == 2)
    {
      ++state.restoreSuccesses;
    }
    return flipMutation == 3 ? E_FAIL : S_OK;
  }

  static int __cdecl LazyUpload()
  {
    ++env->lazyUploads;
    return 1;
  }

  Environment::Environment()
  {
    env = this;
    readinessTestEnabled = false;
    PostRestoreRenderer::setupReadinessActive = false;
    PostRestoreRenderer::setupReadinessInvalidated = false;
    PresentationInput::scopeState = {};
    SoftwareModeCompletion::activePresentDepth = 0;
    SoftwareModeCompletion::activePaintDepth = 0;
    softwareModeCompletionState = {};
    FailedPresentation::scopeDepth = FailedPresentation::scopeToken =
        FailedPresentation::flipToken = 0;
    FailedPresentation::incomplete = FailedPresentation::flipValid =
        FailedPresentation::failureArmed = false;
    PostRestoreRenderer::nextFlipToken = 0;
    PostRestoreRenderer::scope = {};
    UnlockFrame::scope = {};
    normalFrameState = {};
    flushOwnerState = {};
    FullscreenSetup::scopeState = {};
    bridgeState = {};
    quadFrameRecoveryState = {};
    failedPresentationState = {};
    worldRelockState = {};
    flipProbeState = {};
    hardwareModeCompletionState = {};
    modeAdmissionState = {};
    PostRestoreRenderer::baseline = {};
    PostRestoreRenderer::confirmationArmed = false;
    PostRestoreRenderer::retained = 0;
    PostRestoreRenderer::flipToken = 0;
    bridgeState.beginCalls = bridgeState.endCalls = 1;
    bridgeState.lastStage = D3DBridge::end;
    bridgeState.lastResult = S_OK;
    fixture.methods[0] = reinterpret_cast<DWORD>(&DrawQuery);
    fixture.methods[27] = reinterpret_cast<DWORD>(&RestorePrimary);
    fixture.methods[6] = reinterpret_cast<DWORD>(&CreateSurface);
    for (DWORD *t : {rootTable, root2Table, device2Table, deviceTable, viewportTable, executeTable,
                     surfaceTable})
    {
      t[1] = reinterpret_cast<DWORD>(&Add);
      t[2] = reinterpret_cast<DWORD>(&Release);
    }
    rootTable[0] = reinterpret_cast<DWORD>(&RootQuery);
    rootTable[4] = reinterpret_cast<DWORD>(&EnumDevices);
    rootTable[7] = reinterpret_cast<DWORD>(&CreateViewport);
    root2Table[8] = reinterpret_cast<DWORD>(&CreateDevice);
    device2Table[0] = reinterpret_cast<DWORD>(&Device2Query);
    device2Table[10] = device2Table[11] = reinterpret_cast<DWORD>(&Scene);
    deviceTable[0] = reinterpret_cast<DWORD>(&DeviceQuery);
    deviceTable[6] = reinterpret_cast<DWORD>(&CreateExecute);
    deviceTable[8] = reinterpret_cast<DWORD>(&Execute);
    deviceTable[9] = reinterpret_cast<DWORD>(&AddViewport);
    deviceTable[14] = reinterpret_cast<DWORD>(&EnumFormats);
    viewportTable[5] = reinterpret_cast<DWORD>(&SetViewport);
    executeTable[4] = reinterpret_cast<DWORD>(&Lock);
    executeTable[5] = reinterpret_cast<DWORD>(&Unlock);
    executeTable[6] = reinterpret_cast<DWORD>(&ExecuteData);
    surfaceTable[0] = reinterpret_cast<DWORD>(&SurfaceQuery);
    surfaceTable[4] = reinterpret_cast<DWORD>(&Handle);
    surfaceTable[24] = reinterpret_cast<DWORD>(&SurfaceLost);
    oldRoot.table = root.table = rootTable;
    root2.table = root2Table;
    device2.table = device2Table;
    oldDevice.table = device.table = deviceTable;
    oldViewport.table = viewport.table = viewportTable;
    oldExecute.table = executeTable;
    oldExecute.bytes.resize(0x50000);
    oldExecute.locked = true;
    oldRoot.refs = oldDevice.refs = oldViewport.refs = oldExecute.refs = 1;
    palette.table = rootTable;
    palette.refs = 1;
    oldStaging.table = oldTexture.table = surfaceTable;
    oldStaging.refs = oldTexture.refs = 2;
    DWORD protection = 0;
    Check(VirtualProtect(fixture.image, 0x600000, PAGE_EXECUTE_READWRITE, &protection) != FALSE,
          "Native image permission");
    for (const auto &s : NativeSpans)
    {
      std::memcpy(fixture.image + s.rva, s.bytes, s.size);
    }
    for (DWORD r : NativeRelocations)
    {
      *reinterpret_cast<DWORD *>(fixture.image + r) += config.gameBase - 0x400000;
    }
    Relative(fixture.image + FixtureRva::rendererInitHook,
             reinterpret_cast<DWORD>(&RendererInitHook));
    Relative(fixture.image + FixtureRva::stateInstructionsHook,
             reinterpret_cast<DWORD>(&StateInstructionsHook), 0xe9);
    Relative(fixture.image + FixtureRva::stateExecuteDataHook,
             reinterpret_cast<DWORD>(&StateExecuteDataHook), 0xe9);
    Relative(fixture.image + FixtureRva::beginSceneBridge,
             reinterpret_cast<DWORD>(&BeginSceneBridge));
    Relative(fixture.image + FixtureRva::endSceneBridge, reinterpret_cast<DWORD>(&EndSceneBridge));
    Relative(fixture.image + GameRva::fatalError, reinterpret_cast<DWORD>(&Fatal), 0xe9);
    Relative(fixture.image + FixtureRva::cpuCacheDestroy, reinterpret_cast<DWORD>(&Forbidden),
             0xe9);
    Relative(fixture.image + FixtureRva::cpuCacheCreate, reinterpret_cast<DWORD>(&Forbidden), 0xe9);
    Relative(fixture.image + FixtureRva::upload, reinterpret_cast<DWORD>(&LazyUpload), 0xe9);
    config.rendererInitOriginal = config.gameBase + FixtureRva::renderer;
    config.stateInstructionsResume = config.gameBase + FixtureRva::stateInstructionsResume;
    config.stateExecuteDataResume = config.gameBase + FixtureRva::stateExecuteDataResume;
    auto clone = const_cast<BYTE *>(guardedRendererImage);
    Check(VirtualProtect(clone, 460, PAGE_EXECUTE_READWRITE, &cloneProtect) != FALSE,
          "Clone permission");
    std::memcpy(savedClone, clone, 460);
    std::memcpy(clone, RendererCloneTemplate, 460);
    for (const auto &r : RendererCloneAbsolute)
    {
      *reinterpret_cast<DWORD *>(clone + r.offset) = config.gameBase + r.target;
    }
    Relative(clone + 16, reinterpret_cast<DWORD>(&ChildCreateDevice));
    Relative(clone + 426, config.gameBase + FixtureRva::textureBank);
    Relative(clone + 443, config.gameBase + FixtureRva::initialState);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    config.frameOriginal = reinterpret_cast<DWORD>(&FrameBody);
    Relative(fixture.image + recoveryOuterFrameReturn - 5, reinterpret_cast<DWORD>(&FrameHook));
    fixture.image[recoveryOuterFrameReturn] = 0xc3;
    modeAdmissionState.app = Read(GameRva::app);
    modeAdmissionState.frameObject = *reinterpret_cast<DWORD *>(Read(GameRva::app) + 0x1c);
    modeAdmissionState.frame = reinterpret_cast<DWORD>(fixture.frame);
    modeAdmissionState.thread = GetCurrentThreadId();
    modeAdmissionState.ownerGeneration = 1;
    modeAdmissionState.transitionSerial = 1;
    state.blocked = 1;
    Write(GameRva::renderBpp, 8);
    Write(recoveryNativeActive, 1);
    Write(FixtureRva::quality, 1);
    Write(recoveryNativeSurfaceWidth, 640);
    Write(recoveryNativeSurfaceHeight, 480);
    Write(GameRva::hardwarePreference, 1);
    Write(FixtureRva::retainedFourthParent, reinterpret_cast<DWORD>(&palette));
    Write(GameRva::textureStagingSurface, reinterpret_cast<DWORD>(&oldStaging));
    Write(GameRva::textureStagingInterface, reinterpret_cast<DWORD>(&oldStaging));
    Write(GameRva::textureSurfaceTable, reinterpret_cast<DWORD>(&oldTexture));
    Write(GameRva::textureInterfaceTable, reinterpret_cast<DWORD>(&oldTexture));
    Write(GameRva::textureHandles, 123);
    std::memcpy(fixture.image + GameRva::textureGpuNames, "old.raw", 8);
    for (DWORD i = 0; i < 12; ++i)
    {
      Write(FixtureRva::cpuAllocations + 4 * i, 0x18000000 + i * 0x10000);
    }
    Write(GameRva::d3dRoot, reinterpret_cast<DWORD>(&oldRoot));
    Write(GameRva::legacyDevice, reinterpret_cast<DWORD>(&oldDevice));
    Write(FixtureRva::viewport, reinterpret_cast<DWORD>(&oldViewport));
    Write(recoveryExecuteBuffer, reinterpret_cast<DWORD>(&oldExecute));
    Write(GameRva::instructionOffset, 0x20020);
    Write(GameRva::executeCapacity, 0x40000);
    const DWORD allocation = reinterpret_cast<DWORD>(oldExecute.bytes.data());
    Write(recoveryExecuteAllocation, allocation);
    Write(recoveryExecuteInstructionBegin, allocation + 0x20020);
    Write(recoveryExecuteInstructionEnd, allocation + 0x20060);
    Write(recoveryExecuteCopyRecord, allocation + 0x20024);
    Write(recoveryExecuteVertexPointer, allocation);
    FullscreenSetup::scopeState.busy = 1;
    FullscreenSetup::scopeState.activeToken = 1;
    FullscreenSetup::scopeState.activeThread = GetCurrentThreadId();
    StampPostRestoreRenderer(3, Read(GameRva::drawObject));
    FullscreenSetup::scopeState = {};
    Check(PostRestoreRenderer::baseline.valid, "Baseline stamp");
    ++state.restoreSuccesses;
    textureState.pendingEpoch = state.restoreSuccesses;
  }

  Environment::~Environment()
  {
    auto clone = const_cast<BYTE *>(guardedRendererImage);
    std::memcpy(clone, savedClone, 460);
    DWORD ignored = 0;
    VirtualProtect(clone, 460, cloneProtect, &ignored);
    env = nullptr;
  }

  static DWORD Before()
  {
    DWORD caller = config.gameBase + recoveryOuterFrameReturn;
    DWORD registers[8] = {};
    registers[3] = reinterpret_cast<DWORD>(&caller) - 4;
    return PostRestoreBeforeFrame(registers);
  }

  static void Confirm(DWORD mutation)
  {
    Environment e;
    e.testReentry = true;
    Check(Before() == 1 && PostRestoreRenderer::baseline.restored == state.restoreSuccesses,
          "Reentry rebuild failed");
    Check(PostRestoreRenderer::scope.awaitingFrame == 1 && PostRestoreRenderer::confirmationArmed &&
              !PostRestoreRenderer::flipToken,
          "Rebuild manufactured frame/present");
    using Native = void(__cdecl *)();
    restoreDuringFrame = mutation == 4;
    reinterpret_cast<Native>(config.gameBase + recoveryOuterFrameReturn - 5)();
    restoreDuringFrame = false;
    if (mutation == 4)
    {
      Check(state.restoreSuccesses == PostRestoreRenderer::baseline.restored + 1,
            "Restore callback did not execute during the native frame");
      Check(!PostRestoreRenderer::confirmationArmed &&
                PostRestoreRenderer::scope.awaitingFrame == 1,
            "Restore during frame was confirmed");
      Cases.push_back("restore-during-frame-" + std::to_string(RequestedBase));
      return;
    }
    Check(quadFrameRecoveryState.frameReturned == 1,
          "Production frame return did not validate ownership");
    Check(normalFrameState.frameReturned && !normalFrameState.depth &&
              PostRestoreRenderer::scope.awaitingFrame == 2,
          "Completed native frame did not arm presentation");
    e.fixture.methods[11] = reinterpret_cast<DWORD>(&RawFlip);
    flipMutation = mutation;
    InstallPresentation(e);
    rawCalls = 0;
    bool fatal = false;
    try
    {
      Check(reinterpret_cast<int(__cdecl *)()>(config.gameBase + PresentRva::outer)() == 0,
            "Native present return changed");
    }
    catch (FatalError &)
    {
      fatal = true;
    }
    Check(rawCalls == (mutation == 9 ? 2u : 1u), "Actual raw Flip not reached once");
    Check(fatal == (mutation == 3), "Raw failure path changed");
    Check(!PostRestoreRenderer::flipToken, "Raw Flip token was not consumed");
    Check(PostRestoreRenderer::scope.awaitingFrame == (mutation ? 1u : 0u),
          "Confirmation phase incorrect");
    if (mutation == 2)
    {
      Check(PostRestoreRenderer::baseline.restored + 1 == state.restoreSuccesses,
            "New restore incorrectly consumed");
      Check(PostRestoreRenderer::nativeDispatches == 1,
            "New restore silently rebuilt before confirmation");
    }
    if (!mutation)
    {
      PostRestoreRenderer::scope.awaitingFrame = 1;
      CompletePostRestoreFlip(PostRestoreRenderer::nextFlipToken, S_OK);
      Check(PostRestoreRenderer::scope.awaitingFrame == 1, "Duplicate confirmation accepted");
    }
    Cases.push_back("native-frame-raw-flip-" + std::to_string(mutation) + "-" +
                    std::to_string(RequestedBase));
  }

  static void Run(Fault fault)
  {
    Environment e;
    e.fault = fault;
    Check(PostRestoreRenderer::Quiet() && PostRestoreRenderer::EmptyExecute(),
          "Invalid transaction preconditions");
    bool fatal = false;
    try
    {
      Check(Before() == 1, "Frame admission rejected");
    }
    catch (FatalError &)
    {
      fatal = true;
    }
    if (fault != Fault::None)
    {
      Check(fatal && PostRestoreRenderer::baseline.restored != state.restoreSuccesses,
            "Partial failure did not fail closed");
      Check(PostRestoreRenderer::scope.busy == 1, "Fatal reopened admission");
    }
    else
    {
      Check(!fatal && PostRestoreRenderer::baseline.restored == state.restoreSuccesses &&
                PostRestoreRenderer::scope.awaitingFrame == 1,
            "Native rebuild failed");
      Check(e.nativeEnumCalls == 1 && e.formatCalls == 1, "Native capability paths bypassed");
      Check(e.surfaceCreates == 97 && e.executeCreates == 2 && e.executeCalls == 1,
            "Native setup chain incomplete");
      Check(!e.oldExecute.refs && !e.oldExecute.locked && !e.oldRoot.refs && !e.oldDevice.refs &&
                !e.oldViewport.refs,
            "Old children retained");
      Check(!e.oldStaging.refs && !e.oldTexture.refs, "Native old bank retained COM references");
      Check(PostRestoreRenderer::NewBank() && PostRestoreRenderer::EmptyExecute(),
            "Native bank/reset invalid");
      Check(Read(recoveryExecuteAllocation) != reinterpret_cast<DWORD>(e.oldExecute.bytes.data()),
            "Stale execute allocation");
      Check(textureState.pendingEpoch == state.restoreSuccesses &&
                textureState.completedEpoch == state.restoreSuccesses,
            "Wrong epoch consumption");
      Check(!textureState.passes && !TextureLifecycle::UploadSerial(), "Fake cache pass/upload");
      BYTE material[64] = {};
      std::memcpy(material + 8, "valk.raw", 9);
      using Select = int(__cdecl *)(void *);
      Check(reinterpret_cast<Select>(config.gameBase + FixtureRva::lazyLookup)(material) == 1 &&
                e.lazyUploads == 1,
            "Native lazy refill did not upload");
      Check(reinterpret_cast<Select>(config.gameBase + FixtureRva::lazyLookup)(material) == 1 &&
                e.lazyUploads == 1,
            "Native material did not reuse slot");
    }
    Check(!e.forbidden, "CPU fallback");
    Check(e.palette.refs == 1 && e.palette.adds == e.palette.drops,
          "Palette reference ownership changed");
    for (DWORD i = 0; i < 12; ++i)
    {
      Check(Read(FixtureRva::cpuAllocations + 4 * i) == 0x18000000 + i * 0x10000,
            "CPU allocations changed");
    }
    Check(e.fixture.draw.references == 1 && e.fixture.primary.references == 1 &&
              e.fixture.backbuffer.references == 1,
          "Parent reference leak");
    Check(Read(GameRva::drawObject) == reinterpret_cast<DWORD>(&e.fixture.draw) &&
              Read(GameRva::primary) == reinterpret_cast<DWORD>(&e.fixture.primary) &&
              Read(GameRva::backbuffer) == reinterpret_cast<DWORD>(&e.fixture.backbuffer),
          "Parent identity changed");
    Cases.push_back("native-transaction-" + std::to_string(static_cast<int>(fault)) + "-" +
                    std::to_string(RequestedBase));
  }
} // namespace Transaction

static LONG WINAPI ReportException(EXCEPTION_POINTERS *p)
{
  std::fprintf(stderr, "exception code=%08lx ip=%08lx stage=%lu reason=%lu frames=%lu\n",
               p->ExceptionRecord->ExceptionCode, p->ContextRecord->Eip,
               PostRestoreRenderer::scope.setup, PostRestoreRenderer::scope.awaitingFrame,
               TextureLifecycle::FrameSerial());
  auto stack = reinterpret_cast<DWORD *>(p->ContextRecord->Esp);
  for (int i = 0; i < 12; ++i)
  {
    std::fprintf(stderr, "stack%d=%08lx\n", i, stack[i]);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

static DWORD PumpModeCalls = 0, PumpNativeInputs = 0, PumpPeeks = 0, PumpMode = 0;
static bool PumpRepeat = false, PumpInRaw = false;
static DWORD PumpFault = 0, ForeignCalls = 0, KeyUps = 0;
static bool Sequence = false, KeepMode = false, NativeKeyHeld = false;

static SHORT WINAPI PumpKey(int)
{
  return 0;
}

static HWND WINAPI PumpFocus()
{
  return Active->view;
}

static int WINAPI PumpMetrics(int metric)
{
  return metric == SM_CXSCREEN ? 1920 : 1080;
}

static BOOL WINAPI PumpPeek(MSG *message, HWND, UINT, UINT, UINT)
{
  const DWORD ordinal = PumpPeeks++;
  if (ordinal >= (Sequence ? 3u : 1u))
  {
    return FALSE;
  }
  *message = {};
  message->hwnd = Active->frame;
  message->message = Sequence && ordinal == 2 ? WM_KEYUP : WM_KEYDOWN;
  message->wParam = VK_F4;
  message->time = Active->now + ordinal + 1;
  message->lParam = PumpRepeat ? (1u << 30) : 1;
  return TRUE;
}

static BOOL WINAPI PumpTranslate(const MSG *)
{
  return TRUE;
}

static LRESULT WINAPI PumpDispatch(const MSG *)
{
  return 0;
}

static void __cdecl PumpIdle() {}

static void __fastcall PumpNativeMode(void *app, void *, DWORD mode)
{
  Check(reinterpret_cast<DWORD>(app) == Read(GameRva::app), "Mode app owner changed");
  ++PumpModeCalls;
  const DWORD count = PumpModeCalls;
  reinterpret_cast<void(__thiscall *)(void *, DWORD)>(&ModeHook)(app, 99);
  Check(PumpModeCalls == count, "Mode reentry during owned transition was accepted");
  if (!KeepMode)
  {
    Write(GameRva::mode, mode);
    Write(GameRva::hardware, mode == 2);
    Write(GameRva::directDraw, mode == 2);
    Write(GameRva::fullscreenActive, mode == 2);
  }
  SetLastError(0x24681357);
}

static void __fastcall PumpNativePaint(void *, void *, void *)
{
  Check(SoftwareModeCompletion::activePaintDepth == 1, "Actual paint wrapper scope missing");
  reinterpret_cast<void(__thiscall *)(void *, DWORD)>(&ModeHook)(
      reinterpret_cast<void *>(Read(GameRva::app)), PumpMode);
  Check(!PumpModeCalls, "Mode entered from native paint callback");
}

static BOOL __fastcall PumpNativeInput(void *app, void *, MSG *message)
{
  Check(message->wParam == VK_F4, "Pump key changed");
  if (message->message == WM_KEYUP)
  {
    ++KeyUps;
    NativeKeyHeld = false;
    return TRUE;
  }
  Check(message->message == WM_KEYDOWN, "Pump input changed");
  NativeKeyHeld = true;
  ++PumpNativeInputs;
  const bool hardware = Read(GameRva::mode) == 2;
  if (hardware && PumpFault != 12 && !(Sequence && PumpPeeks == 2))
  {
    Check(PresentationInputCurrent(), "Native pump input scope missing");
  }
  if (!hardware)
  {
    Check(!PresentationInput::scopeState.call,
          "Software input claimed a hardware presentation scope");
  }
  if (PumpFault == 1)
  {
    ++PresentationInput::scopeState.presentationToken;
  }
  if (PumpFault == 2)
  {
    ++PresentationInput::scopeState.nativeSlot;
  }
  if (PumpFault == 3)
  {
    ++PresentationInput::scopeState.continuation;
  }
  if (PumpFault == 4)
  {
    ++modeAdmissionState.ownerGeneration;
  }
  if (PumpFault == 5)
  {
    ++modeAdmissionState.thread;
  }
  if (PumpFault == 6)
  {
    reinterpret_cast<BOOL(__thiscall *)(void *, MSG *)>(&ModeAdmissionPreTranslate)(app, message);
    Check(PresentationInput::scopeState.invalidated,
          "Nested input did not revoke outer authorization");
  }
  if (PumpFault == 7)
  {
    state.restoreBusy = 1;
  }
  if (PumpFault == 8)
  {
    UnlockFrame::scope.busy = 1;
  }
  if (PumpFault == 9)
  {
    reinterpret_cast<void(__thiscall *)(void *, void *)>(&SoftwareDrawHook)(
        reinterpret_cast<void *>(Read(GameRva::view)), nullptr);
    Check(!SoftwareModeCompletion::activePaintDepth, "Paint wrapper retained depth");
  }
  if (PumpFault == 10)
  {
    const DWORD frames = TextureLifecycle::FrameSerial();
    reinterpret_cast<void(__cdecl *)()>(config.gameBase + recoveryOuterFrameReturn - 5)();
    Check(TextureLifecycle::FrameSerial() == frames,
          "Nested frame entered during old presentation");
  }
  if (PumpFault == 11)
  {
    ++PresentationInput::scopeState.consumedToken;
  }
  const DWORD callsBefore = PumpModeCalls;
  SetLastError(0x13572468);
  reinterpret_cast<void(__thiscall *)(void *, DWORD)>(&ModeHook)(app, PumpMode);
  Check(GetLastError() == (PumpModeCalls > callsBefore ? 0x24681357u : 0x13572468u),
        "Mode wrapper changed LastError");
  if (PumpFault == 7)
  {
    state.restoreBusy = 0;
  }
  if (PumpFault == 8)
  {
    UnlockFrame::scope.busy = 0;
  }
  return TRUE;
}

static __declspec(noinline) BOOL __fastcall ForeignInput(void *app, void *, MSG *message)
{
  const BOOL result =
      reinterpret_cast<BOOL(__thiscall *)(void *, MSG *)>(&ModeAdmissionPreTranslate)(app, message);
  ++ForeignCalls;
  return result;
}

static HRESULT WINAPI PumpRawFlip(void *surface, void *target, DWORD flags)
{
  if (PumpInRaw)
  {
    reinterpret_cast<void(__thiscall *)(void *, DWORD)>(&ModeHook)(
        reinterpret_cast<void *>(Read(GameRva::app)), PumpMode);
    Check(PumpModeCalls == 0, "Mode entered during raw Flip callback");
  }
  Check(surface == &Active->primary && !target && flags == 1, "Pump raw present inputs changed");
  Check(FailedPresentation::scopeDepth == 1 && FailedPresentation::ScopeCurrent() &&
            FailedPresentation::flipValid && FailedPresentation::FrameCurrent(),
        "Pump raw frame ownership missing");
  return S_OK;
}

static void InstallPump(Transaction::Environment &e)
{
  Transaction::InstallPresentation(e);
  modeAdmissionState.imageBase = config.gameBase;
  for (const auto &s : PumpSpans)
  {
    std::memcpy(e.fixture.image + s.rva, s.bytes, s.size);
  }
  for (DWORD r : PumpRelocations)
  {
    *reinterpret_cast<DWORD *>(e.fixture.image + r) += config.gameBase - 0x400000;
  }
  Transaction::Relative(e.fixture.image + PresentRva::outer,
                        reinterpret_cast<DWORD>(&PresentRecoveryFrame));
  e.fixture.image[presentationGameReturn] = 0xc3;
  const DWORD table = *reinterpret_cast<DWORD *>(Read(GameRva::app));
  *reinterpret_cast<DWORD *>(table + 0x60) = config.gameBase + PumpRva::entry;
  *reinterpret_cast<DWORD *>(table + 0x5c) = reinterpret_cast<DWORD>(&ModeAdmissionPreTranslate);
  Write(PumpRva::peek, reinterpret_cast<DWORD>(&PumpPeek));
  Write(PumpRva::translate, reinterpret_cast<DWORD>(&PumpTranslate));
  Write(PumpRva::dispatch, reinterpret_cast<DWORD>(&PumpDispatch));
  Write(modeNativeGetKeyState, reinterpret_cast<DWORD>(&PumpKey));
  Write(GameRva::getFocus, reinterpret_cast<DWORD>(&PumpFocus));
  Write(GameRva::getSystemMetrics, reinterpret_cast<DWORD>(&PumpMetrics));
  Transaction::Relative(e.fixture.image + PumpRva::idle, reinterpret_cast<DWORD>(&PumpIdle), 0xe9);
  Transaction::Relative(e.fixture.image + modeNativePreTranslate,
                        reinterpret_cast<DWORD>(&PumpNativeInput), 0xe9);
  config.modeResume = reinterpret_cast<DWORD>(&PumpNativeMode);
  Transaction::Relative(e.fixture.image + modeNativeDraw, reinterpret_cast<DWORD>(&PumpNativePaint),
                        0xe9);
  if (PumpFault == 12)
  {
    *reinterpret_cast<DWORD *>(table + 0x5c) = reinterpret_cast<DWORD>(&ForeignInput);
  }
  e.fixture.methods[11] = reinterpret_cast<DWORD>(&PumpRawFlip);
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
}

static void PumpCase(DWORD initial, bool repeat, bool raw, DWORD fault = 0)
{
  Transaction::Environment e;
  PresentationInput::scopeState = {};
  PumpFault = fault;
  ForeignCalls = 0;
  Sequence = KeepMode = NativeKeyHeld = false;
  KeyUps = 0;
  PumpModeCalls = PumpNativeInputs = PumpPeeks = 0;
  PumpMode = initial ? initial - 1 : 2;
  PumpRepeat = repeat;
  PumpInRaw = raw;
  Transaction::flipMutation = 0;
  if (initial == 2)
  {
    Check(Transaction::Before() == 1, "Child preparation failed");
    reinterpret_cast<void(__cdecl *)()>(config.gameBase + recoveryOuterFrameReturn - 5)();
    Check(PostRestoreRenderer::scope.awaitingFrame == 2, "Fresh hardware frame missing");
  }
  else
  {
    Write(GameRva::mode, initial);
    Write(GameRva::hardware, 0);
    Write(GameRva::directDraw, 0);
    Write(GameRva::fullscreenActive, 0);
    failedPresentationState.frameReady = 0;
    Check(!FailedPresentation::FrameCurrent(), "Software phase unexpectedly owns a hardware frame");
  }
  InstallPump(e);
  Check(reinterpret_cast<int(__cdecl *)()>(config.gameBase + PumpRva::branch)() ==
            (initial == 2 ? 0 : 1),
        "Native presentation return changed");
  Check(PumpNativeInputs == (repeat ? 0u : 1u), "Native input dispatch count differs");
  const bool blocked = repeat || (fault && fault != 9 && fault != 10);
  Check(PumpModeCalls == (blocked ? 0u : 1u), "Pump mode request admission differs");
  Check(Read(GameRva::mode) == (blocked ? initial : PumpMode), "Native mode result changed");
  Check(!PresentationInput::scopeState.call && !PresentationInput::scopeState.consumedToken,
        "Presentation input authorization leaked");
  Check(!SoftwareModeCompletion::activePresentDepth && !SoftwareModeCompletion::activePaintDepth,
        "Software active scope leaked");
  Check(!PresentationInputCurrent(), "Expired input scope remained valid");
  if (fault == 12)
  {
    Check(ForeignCalls == 1, "Foreign dispatch boundary not exercised");
  }
  Cases.emplace_back("pump-mode-" + std::to_string(initial) + "-" + std::to_string(repeat) + "-" +
                     std::to_string(raw) + "-fault" + std::to_string(fault) + "-" +
                     std::to_string(RequestedBase));
}

static void RearmCase()
{
  Transaction::Environment e;
  PresentationInput::scopeState = {};
  PumpFault = PumpModeCalls = PumpNativeInputs = PumpPeeks = KeyUps = 0;
  PumpMode = 1;
  Sequence = KeepMode = true;
  PumpRepeat = PumpInRaw = NativeKeyHeld = false;
  Transaction::flipMutation = 0;
  Check(Transaction::Before() == 1, "Rearm child setup failed");
  InstallPump(e);
  DWORD previousGeneration = 0;
  for (DWORD presentation = 1; presentation <= 2; ++presentation)
  {
    reinterpret_cast<void(__cdecl *)()>(config.gameBase + recoveryOuterFrameReturn - 5)();
    Check(failedPresentationState.frameReady &&
              failedPresentationState.generation > previousGeneration,
          "Rearm lacks a genuinely new completed frame");
    previousGeneration = failedPresentationState.generation;
    PumpPeeks = 0;
    Check(reinterpret_cast<int(__cdecl *)()>(config.gameBase + PumpRva::branch)() == 0,
          "Rearm presentation result changed");
    Check(PumpModeCalls == presentation,
          "Same presentation repeated mode or next presentation failed to rearm");
    Check(KeyUps == presentation && !NativeKeyHeld,
          "Native key-up forwarding did not clear fixture latch");
    Check(!PresentationInput::scopeState.call && !PresentationInput::scopeState.consumedToken &&
              !FailedPresentation::scopeDepth,
          "Old presentation scope survived");
  }
  Check(PumpNativeInputs == 4, "Repeated fresh key did not traverse the actual native pump");
  Sequence = KeepMode = false;
  Cases.emplace_back("same-presentation-repeat-keyup-next-frame-rearm-" +
                     std::to_string(RequestedBase));
}

static void MenuCase()
{
  Transaction::Environment e;
  PresentationInput::scopeState = {};
  PumpFault = PumpModeCalls = PumpNativeInputs = PumpPeeks = KeyUps = 0;
  PumpMode = 2;
  Sequence = KeepMode = PumpRepeat = PumpInRaw = false;
  Transaction::flipMutation = 0;
  Write(GameRva::mode, 0);
  Write(GameRva::hardware, 0);
  Write(GameRva::directDraw, 0);
  Write(GameRva::fullscreenActive, 0);
  Write(GameRva::mainMenuVisible, 1);
  state.contentActive = 0;
  InstallPump(e);
  Check(reinterpret_cast<int(__thiscall *)(void *)>(config.gameBase + PumpRva::entry)(
            reinterpret_cast<void *>(Read(GameRva::app))) == 1,
        "Standalone menu pump failed");
  Check(PumpModeCalls == 1 && Read(GameRva::mode) == 2 && !PresentationInput::scopeState.call,
        "Standalone menu input blocked or borrowed hardware presentation");
  Cases.emplace_back("standalone-main-menu-native-pump-" + std::to_string(RequestedBase));
}

namespace ShutdownPresentation
{
  static DWORD App, Table, Target, InputStack, HostStack, Flags, NativeCalls, PreparationCalls;

  struct Registers
  {
    DWORD edi, esi, ebp, savedEsp, ebx, edx, ecx, eax, flags;
  };

  static Registers Output;
  static __declspec(align(16)) BYTE FloatingBefore[512], FloatingAfter[512];

  __declspec(naked) static void CaptureExit()
  {
    __asm {
      pushfd
      pushad
      fxsave FloatingAfter
      mov esi,esp
      mov edi,offset Output
      mov ecx,9
      cld
      rep movsd
      mov esp,HostStack
      popad
      popfd
      ret
    }
  }

  __declspec(naked) static void Invoke()
  {
    __asm {
      pushfd
      pushad
      mov HostStack,esp
      mov esp,InputStack
      pcmpeqd xmm0,xmm0
      fxsave FloatingBefore
      push Flags
      popfd
      mov edi,11111111h
      mov esi,App
      mov ebp,33333333h
      mov ebx,44444444h
      mov eax,11221122h
      mov ecx,22332233h
      mov edx,33443344h
      jmp Target
    }
  }

  __declspec(naked) static void OriginalPresenter()
  {
    __asm {
      inc NativeCalls
      mov eax,5a5aa5a5h
      mov dword ptr fs:[34h],87654321h
      ret
    }
  }

  static void __cdecl CloseDuringPreparation()
  {
    ++PreparationCalls;
    Write(GameRva::quit, 1);
    *reinterpret_cast<DWORD *>(App + 0x1c) = 0;
  }

  __declspec(naked) static void NativePreparation()
  {
    __asm {
      pushfd
      pushad
      cld
      call CloseDuringPreparation
      popad
      popfd
      ret
    }
  }

  enum class Route
  {
    NativeMenuTail,
    Qualified,
    NotQuitting,
    LiveFrame,
    WrongOwner,
    WrongThread,
    WrongAppGlobal,
    WrongCaller
  };

  static void Run(Route route, DWORD flags = 0x202)
  {
    Fixture fixture;
    modeAdmissionState = {};
    softwareModeCompletionState = {};
    SoftwareModeCompletion::activePresentDepth = SoftwareModeCompletion::activePaintDepth = 0;
    const DWORD frame = config.gameBase + 0x381000;
    App = config.gameBase + 0x380000;
    Table = config.gameBase + 0x383000;
    *reinterpret_cast<DWORD *>(App) = Table;
    *reinterpret_cast<DWORD *>(Table + 0xc4) = reinterpret_cast<DWORD>(&SoftwarePresentHook);
    *reinterpret_cast<DWORD *>(App + 0x1c) = route == Route::NativeMenuTail ? frame : 0;
    Write(GameRva::quit, route == Route::NativeMenuTail ? 0 : 1);
    Write(GameRva::mainMenuVisible, route == Route::NativeMenuTail ? 0 : 1);
    Write(GameRva::gameStage, 0);
    modeAdmissionState.imageBase = config.gameBase;
    modeAdmissionState.app = App;
    modeAdmissionState.frameObject = frame;
    modeAdmissionState.frame = reinterpret_cast<DWORD>(fixture.frame);
    modeAdmissionState.ownerGeneration = 7;
    modeAdmissionState.thread = GetCurrentThreadId();
    for (const auto &span : PresentSpans)
    {
      std::memcpy(fixture.image + span.rva, span.bytes, span.size);
    }
    for (DWORD rva : PresentRelocations)
    {
      *reinterpret_cast<DWORD *>(fixture.image + rva) += config.gameBase - 0x400000;
    }
    // Stop before the native SEH epilogue, whose enclosing function is not in this fixture.
    fixture.image[PresentRva::menuReturn] = 0xc3;
    Transaction::Relative(fixture.image + PresentRva::menuPreparation,
                          reinterpret_cast<DWORD>(&NativePreparation), 0xe9);
    if (route != Route::NativeMenuTail)
    {
      // Negative cases observe whether ordinary drawing is reached, without driving a GPU.
      Transaction::Relative(fixture.image + PresentRva::generic,
                            reinterpret_cast<DWORD>(&OriginalPresenter), 0xe9);
    }
    Target = config.gameBase +
             (route == Route::NativeMenuTail ? PresentRva::menuStart : PresentRva::menuCall - 4);
    switch (route)
    {
    case Route::NotQuitting:
      Write(GameRva::quit, 0);
      break;
    case Route::LiveFrame:
      *reinterpret_cast<DWORD *>(App + 0x1c) = frame;
      break;
    case Route::WrongOwner:
      modeAdmissionState.app += 4;
      break;
    case Route::WrongThread:
      ++modeAdmissionState.thread;
      break;
    case Route::WrongAppGlobal:
      Write(GameRva::app, App + 4);
      break;
    case Route::WrongCaller:
      std::memcpy(fixture.image + 0x390000, fixture.image + PresentRva::menuCall - 4, 10);
      fixture.image[0x39000a] = 0xc3;
      Target = config.gameBase + 0x390000;
      break;
    default:
      break;
    }
    DWORD protection = 0;
    Check(VirtualProtect(fixture.image, 0x600000, PAGE_EXECUTE_READWRITE, &protection) != FALSE,
          "Shutdown fixture protection failed");
    Check(FlushInstructionCache(GetCurrentProcess(), fixture.image, 0x600000) != FALSE,
          "Shutdown fixture instruction flush failed");
    const auto admission = modeAdmissionState;
    const auto completion = softwareModeCompletionState;
    NativeCalls = PreparationCalls = 0;
    Flags = flags;
    // StackRead must observe the current thread's real stack, not static scratch storage.
    BYTE stack[8192] = {};
    InputStack = reinterpret_cast<DWORD>(stack + 6000);
    *reinterpret_cast<DWORD *>(InputStack) = reinterpret_cast<DWORD>(&CaptureExit);
    SetLastError(0x12345678);
    Invoke();
    const bool skip = route == Route::NativeMenuTail || route == Route::Qualified;
    if (skip)
    {
      Check(!NativeCalls && !SoftwareModeCompletion::activePresentDepth &&
                !softwareModeCompletionState.presentCalls && !softwareModeCompletionState.pending,
            "Shutdown skip entered a drawing scope");
      Check(Output.eax == Table && Output.ecx == App && Output.edx == 0x33443344 &&
                Output.edi == 0x11111111 && Output.esi == App && Output.ebp == 0x33333333 &&
                Output.ebx == 0x44444444,
            "Shutdown skip changed entry registers");
      Check((Output.flags & 0xcd5) == (Flags & 0xcd5), "Shutdown skip changed flags or DF");
      Check(Output.savedEsp == InputStack, "Shutdown skip changed stack cleanup");
      Check(GetLastError() == 0x12345678, "Shutdown skip changed LastError");
      Check(std::memcmp(FloatingBefore, FloatingAfter, sizeof(FloatingBefore)) == 0,
            "Shutdown skip changed floating-point state");
      Check(std::memcmp(&admission, &modeAdmissionState, sizeof(admission)) == 0 &&
                std::memcmp(&completion, &softwareModeCompletionState, sizeof(completion)) == 0,
            "Shutdown skip changed owner or completion state");
    }
    else
    {
      Check(NativeCalls == 1 && Output.eax == 0x5a5aa5a5,
            "Shutdown guard suppressed ordinary or foreign drawing");
      Check(!SoftwareModeCompletion::activePresentDepth && GetLastError() == 0x87654321,
            "Ordinary presenter scope or LastError changed");
    }
    if (route == Route::NativeMenuTail)
    {
      Check(PreparationCalls == 1 && Read(GameRva::mainMenuVisible) == 1 &&
                Read(GameRva::quit) == 1 && !*reinterpret_cast<DWORD *>(App + 0x1c),
            "Native menu publication or controlled nested close did not run");
    }
    Cases.push_back("shutdown-presentation-" + std::to_string(static_cast<int>(route)) + "-" +
                    std::to_string(flags) + "-" + std::to_string(RequestedBase));
  }
} // namespace ShutdownPresentation

namespace Transaction
{
  enum class ReadinessCase
  {
    PrimaryLoss,
    BackbufferLoss,
    LossClears,
    NonLossFailure,
    RestoreFailure,
    PersistentBackbuffer,
    SecondLoss,
    SharedDeadline,
    OwnerMutation,
    RecursiveReadiness
  };

  static ReadinessCase readinessCase;
  static DWORD rootQueries, restores, primaryChecks, backbufferChecks, initialEpoch;
  static bool injected;

  static void InjectReadinessAtRootQuery()
  {
    if (!readinessTestEnabled)
    {
      return;
    }
    ++rootQueries;
    Check(rootQueries == 1 && !env->oldRoot.refs && !env->oldExecute.refs && !env->oldDevice.refs &&
              !env->oldViewport.refs,
          "Readiness loss was not injected after native child teardown");
    injected = true;
    if (readinessCase == ReadinessCase::BackbufferLoss)
    {
      env->fixture.backbufferResult = DDERR_SURFACELOST;
    }
    else
    {
      env->fixture.primaryResult =
          readinessCase == ReadinessCase::NonLossFailure ? E_FAIL : DDERR_SURFACELOST;
    }
    if (readinessCase == ReadinessCase::SharedDeadline)
    {
      env->fixture.pendingOperation = Operation::Cooperative;
      env->fixture.pendingUntil = env->fixture.now + 3500;
    }
  }

  static HRESULT WINAPI ObserveLoss(void *pointer)
  {
    const bool primary = pointer == &env->fixture.primary;
    if (injected)
    {
      ++(primary ? primaryChecks : backbufferChecks);
      if (readinessCase == ReadinessCase::LossClears && primaryChecks == 2)
      {
        env->fixture.primaryResult = env->fixture.backbufferResult = S_OK;
      }
      if (readinessCase == ReadinessCase::SecondLoss && state.restoreSuccesses > initialEpoch)
      {
        env->fixture.primaryResult = DDERR_SURFACELOST;
      }
    }
    return FakeLost(pointer);
  }

  static HRESULT WINAPI RestorePrimary(void *pointer)
  {
    ++restores;
    Check(pointer == &env->fixture.primary && restores == 1 && state.restoreBusy == 1,
          "Recovery did not own exactly one genuine primary Restore");
    Check(!Read(recoveryExecuteBuffer) && !Read(GameRva::legacyDevice) &&
              Read(GameRva::d3dRoot) == reinterpret_cast<DWORD>(&env->root) &&
              !PostRestoreRenderer::nativeDispatches && !env->surfaceCreates,
          "Recovery started after renderer dispatch or changed the new native root");
    if (readinessCase == ReadinessCase::RestoreFailure)
    {
      return E_FAIL;
    }
    env->fixture.primaryResult = env->fixture.backbufferResult = S_OK;
    switch (readinessCase)
    {
    case ReadinessCase::PersistentBackbuffer:
      env->fixture.backbufferResult = DDERR_SURFACELOST;
      break;
    case ReadinessCase::SharedDeadline:
      env->fixture.now += 2000;
      break;
    case ReadinessCase::OwnerMutation:
      ++modeAdmissionState.ownerGeneration;
      break;
    case ReadinessCase::RecursiveReadiness:
      Check(PrepareRenderer() == 0, "Recursive readiness entered the transaction");
      break;
    default:
      break;
    }
    return S_OK;
  }

  static void FinishFreshFrameAndPresent(Environment &e)
  {
    BYTE material[64] = {};
    std::memcpy(material + 8, "valk.raw", 9);
    using Select = int(__cdecl *)(void *);
    Check(reinterpret_cast<Select>(config.gameBase + FixtureRva::lazyLookup)(material) == 1 &&
              e.lazyUploads == 1,
          "Recovered bank did not permit the real native lazy upload");
    using Native = void(__cdecl *)();
    reinterpret_cast<Native>(config.gameBase + recoveryOuterFrameReturn - 5)();
    Check(normalFrameState.frameReturned && !normalFrameState.depth &&
              PostRestoreRenderer::scope.awaitingFrame == 2,
          "Recovered child did not complete a real owned frame");
    e.fixture.methods[11] = reinterpret_cast<DWORD>(&RawFlip);
    flipMutation = 0;
    rawCalls = 0;
    InstallPresentation(e);
    Check(reinterpret_cast<int(__cdecl *)()>(config.gameBase + PresentRva::outer)() == 0,
          "Native presentation return changed");
    Check(rawCalls == 1 && !PostRestoreRenderer::scope.awaitingFrame,
          "Recovery did not reach one qualified raw S_OK presentation");
  }

  static void ReadinessRun(ReadinessCase selected)
  {
    Environment e;
    readinessCase = selected;
    rootQueries = restores = primaryChecks = backbufferChecks = 0;
    injected = false;
    readinessTestEnabled = true;
    initialEpoch = state.restoreSuccesses;
    e.fixture.methods[24] = reinterpret_cast<DWORD>(&ObserveLoss);
    e.fixture.methods[27] = reinterpret_cast<DWORD>(&RestorePrimary);
    surfaceRestoreGuardState = {};
    PostRestoreRenderer::setupReadinessActive = false;
    PostRestoreRenderer::setupReadinessInvalidated = false;
    bool fatal = false;
    try
    {
      Check(Before() == 1, "Native child frame admission rejected");
    }
    catch (FatalError &)
    {
      fatal = true;
    }
    const bool success = selected == ReadinessCase::PrimaryLoss ||
                         selected == ReadinessCase::BackbufferLoss ||
                         selected == ReadinessCase::LossClears;
    Check(rootQueries == 1 && !e.oldRoot.refs && !e.oldDevice.refs && !e.oldViewport.refs &&
              !e.oldExecute.refs && !e.oldExecute.locked,
          "The native child setup/root query was repeated or teardown was bypassed");
    Check(fatal != success, "Child readiness success/failure disposition changed");
    Check(!state.restoreBusy && !readinessState.busy, "Readiness ownership leaked");
    Check(!PostRestoreRenderer::setupReadinessActive, "Private readiness scope leaked");
    if (success)
    {
      const bool recovered =
          selected == ReadinessCase::PrimaryLoss || selected == ReadinessCase::BackbufferLoss;
      Check(restores == static_cast<DWORD>(recovered) &&
                state.restoreSuccesses == initialEpoch + static_cast<DWORD>(recovered),
            "Wrong Restore count or epoch publication");
      Check(readinessState.calls == 2u && readinessState.successes == 1,
            "Fresh successful readiness was bypassed");
      Check(PostRestoreRenderer::nativeDispatches == 1 && e.nativeEnumCalls == 1 &&
                e.surfaceCreates == 97 && e.executeCreates == 2 && e.executeCalls == 1,
            "Native renderer setup did not run exactly once");
      Check(PostRestoreRenderer::baseline.restored == state.restoreSuccesses &&
                textureState.pendingEpoch == state.restoreSuccesses &&
                textureState.completedEpoch == state.restoreSuccesses && !textureState.passes,
            "Owned restoration epoch was not consumed by the new bank");
      Check(primaryChecks >= (recovered ? 4u : 2u) && backbufferChecks >= (recovered ? 3u : 2u),
            "Both retained surfaces were not rechecked");
      FinishFreshFrameAndPresent(e);
    }
    else
    {
      Check(!PostRestoreRenderer::nativeDispatches && !e.surfaceCreates && !e.executeCreates &&
                PostRestoreRenderer::scope.busy == 1 && !PostRestoreRenderer::scope.awaitingFrame,
            "Failed readiness dispatched or resumed a partial renderer");
      const bool noRestore = selected == ReadinessCase::NonLossFailure;
      Check(restores == (noRestore ? 0u : 1u), "Unexpected retry or skipped Restore boundary");
      if (selected == ReadinessCase::SecondLoss)
      {
        Check(state.restoreSuccesses == initialEpoch + 1 && readinessState.calls == 2,
              "Second loss did not occur in the fresh post-restore readiness probe");
      }
      if (selected == ReadinessCase::SharedDeadline)
      {
        Check(e.fixture.slept >= 3500 && e.fixture.now >= 5600,
              "The first readiness budget was not spent before restoration");
      }
    }
    Check(e.fixture.draw.references == 1 && e.fixture.draw4.references == 1 &&
              e.fixture.primary.references == 1 && e.fixture.backbuffer.references == 1 &&
              e.palette.refs == 1 && !e.forbidden,
          "Child readiness leaked retained parents or entered CPU fallback");
    Cases.push_back("child-readiness-" + std::to_string(static_cast<int>(selected)) + "-" +
                    std::to_string(RequestedBase));
  }
} // namespace Transaction

int main()
{
  SetUnhandledExceptionFilter(&ReportException);
  try
  {
    for (DWORD base : {0x20000000u, 0x30000000u})
    {
      RequestedBase = base;
      for (auto fault : {Transaction::Fault::None, Transaction::Fault::OldUnlock,
                         Transaction::Fault::BankCreate, Transaction::Fault::MainExecute,
                         Transaction::Fault::ParentMutation, Transaction::Fault::NewSurfaceLost})
      {
        Transaction::Run(fault);
      }
      for (DWORD mutation = 0; mutation <= 13; ++mutation)
      {
        Transaction::Confirm(mutation);
      }
      for (DWORD initial : {2u, 1u, 0u})
      {
        PumpCase(initial, false, initial == 2);
        PumpCase(initial, true, false);
      }
      for (DWORD fault = 1; fault <= 12; ++fault)
      {
        PumpCase(2, false, false, fault);
      }
      for (auto selected :
           {Transaction::ReadinessCase::PrimaryLoss, Transaction::ReadinessCase::BackbufferLoss,
            Transaction::ReadinessCase::LossClears, Transaction::ReadinessCase::NonLossFailure,
            Transaction::ReadinessCase::RestoreFailure,
            Transaction::ReadinessCase::PersistentBackbuffer,
            Transaction::ReadinessCase::SecondLoss, Transaction::ReadinessCase::SharedDeadline,
            Transaction::ReadinessCase::OwnerMutation,
            Transaction::ReadinessCase::RecursiveReadiness})
      {
        Transaction::ReadinessRun(selected);
      }
      RearmCase();
      MenuCase();
      ShutdownPresentation::Run(ShutdownPresentation::Route::NativeMenuTail);
      for (DWORD flags : {0x202u, 0x247u, 0x602u, 0x647u})
      {
        ShutdownPresentation::Run(ShutdownPresentation::Route::Qualified, flags);
      }
      for (auto route :
           {ShutdownPresentation::Route::NotQuitting, ShutdownPresentation::Route::LiveFrame,
            ShutdownPresentation::Route::WrongOwner, ShutdownPresentation::Route::WrongThread,
            ShutdownPresentation::Route::WrongAppGlobal, ShutdownPresentation::Route::WrongCaller})
      {
        ShutdownPresentation::Run(route);
      }
    }
    std::printf("{\"passed\":true,\"checks\":%u,\"executions\":%u,\"mappedBases\":2}\n", Checks,
                static_cast<unsigned>(Cases.size()));
    return 0;
  }
  catch (const std::exception &error)
  {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
