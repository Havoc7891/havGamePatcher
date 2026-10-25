// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_TEXTURE_RECOVERY_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_TEXTURE_RECOVERY_HPP

struct TextureRecoveryState
{
  DWORD busy, pendingEpoch, completedEpoch, passes, successes, failures;
  DWORD surfacesRestored, texturesReloaded, lastStage, lastSlot, lastResult, cachedCount;
  DWORD reserved[4];
  BYTE savedScratch[4096];
};

static_assert(sizeof(TextureRecoveryState) == 4160, "Texture recovery ABI");

extern "C"
{
  TextureRecoveryState textureState = {};
}

namespace TextureRecovery
{
  constexpr DWORD surfaceLost = 0x887601c2, invalidState = 0x80070057;
  constexpr DWORD textureCount = GameRva::textureCount, surfaceTable = GameRva::textureSurfaceTable;
  constexpr DWORD textureTable = GameRva::textureInterfaceTable,
                  gpuNames = GameRva::textureGpuNames;
  constexpr DWORD cpuCache = GameRva::textureCpuCache, selectedSlot = GameRva::textureSelectedSlot;
  constexpr DWORD dimension = GameRva::textureDimension, sourceBase = GameRva::textureSourceBase;
  constexpr DWORD sourceX = GameRva::textureSourceX, sourceY = GameRva::textureSourceY;
  constexpr DWORD scratch = GameRva::textureScratch,
                  stagingSurface = GameRva::textureStagingSurface;
  constexpr DWORD stagingTexture = GameRva::textureStagingInterface;
  constexpr DWORD legacyDevice = GameRva::legacyDevice, handles = GameRva::textureHandles;

  using SurfaceMethod = HRESULT(WINAPI *)(void *);
  using SurfaceDescription = HRESULT(WINAPI *)(void *, void *);
  using ReferenceMethod = ULONG(WINAPI *)(void *);

  struct CacheErrorScope
  {
    using GetError = DWORD(WINAPI *)();
    using SetError = void(WINAPI *)(DWORD);
    SetError setter = *reinterpret_cast<SetError *>(config.gameBase + GameRva::setLastError);
    DWORD value = (*reinterpret_cast<GetError *>(config.gameBase + GameRva::getLastError))();

    ~CacheErrorScope()
    {
      setter(value);
    }
  };

  struct CacheResults
  {
    DWORD sourceIsLost, destinationIsLost, restoreResult, postRestoreIsLost, nativeUploadResult;
  };

  static CacheResults cacheResults = {};

  struct Source
  {
    DWORD base, x, y, record, maxLod, forcedLod, lod;
  };

  static DWORD Method(DWORD object, DWORD offset)
  {
    if (!object)
    {
      return 0;
    }

    const auto table = *reinterpret_cast<const DWORD *const *>(object);
    return table ? table[offset / 4] : 0;
  }

  static HRESULT SurfaceCall(DWORD object, DWORD offset)
  {
    return reinterpret_cast<SurfaceMethod>(Method(object, offset))(
        reinterpret_cast<void *>(object));
  }

  static bool Fail(DWORD result)
  {
    textureState.lastResult = result;
    TextureLifecycle::Increment(textureState.failures);
    return false;
  }

  static bool MatchingName(const char *gpu, const char *cpu)
  {
    for (DWORD index = 0; index < 16; ++index)
    {
      if (gpu[index] != cpu[index])
      {
        return false;
      }

      if (!gpu[index])
      {
        return true;
      }
    }

    return false;
  }

  static bool SourceFor(DWORD slot, Source &source)
  {
    const char *name = reinterpret_cast<const char *>(config.gameBase + gpuNames + slot * 20);
    DWORD match = 0;
    for (DWORD index = 0; index < 1024; ++index)
    {
      const DWORD record = config.gameBase + cpuCache + index * 76;
      if (MatchingName(name, reinterpret_cast<const char *>(record)))
      {
        if (match)
        {
          return false;
        }

        match = record;
      }
    }

    if (!match)
    {
      return false;
    }

    const auto fields = reinterpret_cast<const DWORD *>(match);
    const DWORD maxLod = fields[4], forcedLod = fields[17];
    if (maxLod > 3 || forcedLod > 3)
    {
      return false;
    }

    const DWORD lod = forcedLod ? forcedLod : ((maxLod + 1) * 65535) >> 16;
    if (Read(GameRva::textureLodSizes + lod * 4) != 64)
    {
      return false;
    }

    source = {fields[5 + lod], fields[9 + lod], fields[13 + lod], match, maxLod, forcedLod, lod};
    const LONG x = static_cast<LONG>(source.x) >> 16;
    const LONG yBytes = static_cast<LONG>(source.y & 0xffff00ff) >> 8;
    return source.base && source.base <= 0xffff0000 && x >= 0 && x <= 192 && yBytes >= 0 &&
           yBytes <= 192 * 256 && !(yBytes & 255);
  }

  struct Slot
  {
    DWORD surface, texture, handle;
    BYTE name[20];
    Source source;
  };

  // Static pass workspace avoids a large stack probe in the CRT-free payload.
  // Identity records do not own COM references. Each active pair is retained separately.
  static Slot savedSlots[1024] = {};

  struct Context
  {
    DWORD base = config.gameBase, lifecycle = TextureLifecycle::Serial();
    CacheErrorScope::GetError errorGet =
        *reinterpret_cast<CacheErrorScope::GetError *>(base + GameRva::getLastError);
    CacheErrorScope::SetError errorSet =
        *reinterpret_cast<CacheErrorScope::SetError *>(base + GameRva::setLastError);

    DWORD thread = TextureLifecycle::ThreadId(), serial = TextureLifecycle::UploadSerial();
    DWORD epoch = state.restoreSuccesses, completed = textureState.completedEpoch;

    DWORD count = Read(textureCount), draw = Read(GameRva::drawObject), device = Read(legacyDevice);
    DWORD primary = Read(GameRva::primary), backbuffer = Read(GameRva::backbuffer);
    DWORD staging = Read(stagingSurface), stagingInterface = Read(stagingTexture);

    DWORD directDraw = Read(GameRva::directDraw), fullscreen = Read(GameRva::fullscreenActive);
    DWORD hardware = Read(GameRva::hardware);
    DWORD renderBpp = Read(GameRva::renderBpp), mission = state.depth, ended = state.ended;
    DWORD video = state.videoDepth, menu = state.mainMenuDepth, payloadBusy = state.busy;
    DWORD menuVisible = Read(GameRva::mainMenuVisible), gameStage = Read(GameRva::gameStage);

    DWORD guard0 = Read(GameRva::uploadGuard0), guard1 = Read(GameRva::uploadGuard1);
    DWORD guard2 = Read(GameRva::uploadGuard2), index0 = Read(GameRva::uploadIndex0);
    DWORD index1 = Read(GameRva::uploadIndex1), index2 = Read(GameRva::uploadIndex2);

    DWORD selection = Read(selectedSlot), size = Read(dimension), source = Read(sourceBase);
    DWORD x = Read(sourceX), y = Read(sourceY), session = Read(GameRva::drawSession);

    DWORD slot = 0xffffffff;
    bool invalidated = false, gateEntered = false, destinationRestored = false;
    DWORD gateResult = invalidState;

    bool Current()
    {
      bool current =
          !invalidated && !TextureLifecycle::Incomplete() && Configured() &&
          config.gameBase == base && lifecycle == TextureLifecycle::Serial() &&
          thread == TextureLifecycle::ThreadId() && serial == TextureLifecycle::UploadSerial() &&
          epoch == state.restoreSuccesses && epoch == textureState.pendingEpoch &&
          completed == textureState.completedEpoch && textureState.busy && !state.restoreBusy &&
          !state.transitionDepth && !state.setupCleanupBusy && !Read(GameRva::quit) &&
          Read(GameRva::mode) == 2 && directDraw == Read(GameRva::directDraw) &&
          fullscreen == Read(GameRva::fullscreenActive) && hardware == Read(GameRva::hardware) &&
          !Read(GameRva::drawLock) && !Read(GameRva::textureLock) && count == Read(textureCount) &&
          draw && draw == Read(GameRva::drawObject) && device && device == Read(legacyDevice) &&
          primary && primary == Read(GameRva::primary) && backbuffer &&
          backbuffer == Read(GameRva::backbuffer) && staging == Read(stagingSurface) &&
          stagingInterface == Read(stagingTexture) && renderBpp == Read(GameRva::renderBpp) &&
          mission == state.depth && ended == state.ended && video == state.videoDepth &&
          menu == state.mainMenuDepth && payloadBusy == state.busy &&
          menuVisible == Read(GameRva::mainMenuVisible) && gameStage == Read(GameRva::gameStage) &&
          guard0 == Read(GameRva::uploadGuard0) && guard1 == Read(GameRva::uploadGuard1) &&
          guard2 == Read(GameRva::uploadGuard2) && index0 == Read(GameRva::uploadIndex0) &&
          index1 == Read(GameRva::uploadIndex1) && index2 == Read(GameRva::uploadIndex2) &&
          selection == Read(selectedSlot) && size == Read(dimension) &&
          source == Read(sourceBase) && x == Read(sourceX) && y == Read(sourceY) &&
          session == Read(GameRva::drawSession);

      if (current && slot != 0xffffffff)
      {
        const Slot &saved = savedSlots[slot];
        current = saved.surface == Read(surfaceTable + slot * 4) &&
                  saved.texture == Read(textureTable + slot * 4) &&
                  saved.handle == Read(handles + slot * 4);
        const auto name = reinterpret_cast<const BYTE *>(base + gpuNames + slot * 20);

        for (DWORD index = 0; current && index < 20; ++index)
        {
          current = saved.name[index] == name[index];
        }

        if (current && saved.name[0])
        {
          const Source &s = saved.source;
          const auto fields = reinterpret_cast<const DWORD *>(s.record);
          current = MatchingName(reinterpret_cast<const char *>(saved.name),
                                 reinterpret_cast<const char *>(s.record)) &&
                    fields[4] == s.maxLod && fields[17] == s.forcedLod &&
                    fields[5 + s.lod] == s.base && fields[9 + s.lod] == s.x &&
                    fields[13 + s.lod] == s.y && Read(GameRva::textureLodSizes + s.lod * 4) == 64;
        }
      }

      if (!current && !invalidated)
      {
        invalidated = true;
      }

      return current;
    }
  };

  struct References
  {
    Context &context;
    DWORD objects[2] = {}, releases[2] = {}, count = 0;

    explicit References(Context &value) : context(value) {}

    bool Add(DWORD object)
    {
      if (!context.Current() || count == 2 || !Method(object, 4) || !Method(object, 8))
      {
        return false;
      }

      const auto add = reinterpret_cast<ReferenceMethod>(Method(object, 4));
      objects[count] = object;
      releases[count] = Method(object, 8);

      // Record ownership before AddRef, so an invalidated return still releases it.
      ++count;
      add(reinterpret_cast<void *>(object));
      return context.Current();
    }

    bool Close()
    {
      bool current = context.Current();
      while (count)
      {
        --count;
        reinterpret_cast<ReferenceMethod>(releases[count])(
            reinterpret_cast<void *>(objects[count]));
        current = context.Current() && current;
      }

      return current;
    }

    ~References()
    {
      if (count)
      {
        Close();
      }
    }
  };

  static bool CallSurface(Context &context, DWORD object, DWORD offset, DWORD &result)
  {
    if (!context.Current() || !Method(object, offset))
    {
      textureState.lastResult = invalidState;
      return false;
    }

    result = SurfaceCall(object, offset);
    textureState.lastResult = result;
    const bool current = context.Current();

    if (!current)
    {
      textureState.lastResult = invalidState;
    }

    return current;
  }

  static bool HealthySource(Context &context)
  {
    return CallSurface(context, context.staging, 0x60, cacheResults.sourceIsLost) &&
           cacheResults.sourceIsLost == S_OK;
  }

  static Context *activeUploadContext = nullptr;

  static bool ReadyDestination(Context &context, DWORD destination);

  // Called only by the ordinary wrapper after BeginUpload. CA90 owns both
  // retained pairs for this scope. Its staging Lock/Unlock may have dispatched
  // callbacks since the outer preflight, so recheck before native Texture::Load.
  static HRESULT AdmitCacheLoad(DWORD destination, DWORD source, DWORD serial)
  {
    Context *context = activeUploadContext;
    if (!context)
    {
      return static_cast<HRESULT>(invalidState);
    }

    struct GateError
    {
      CacheErrorScope::SetError set;
      DWORD value;

      ~GateError()
      {
        set(value);
      }
    } error{context->errorSet, context->errorGet()};

    if (TextureLifecycle::Incomplete() || context->gateEntered || !serial ||
        context->serial == 0xffffffffu || serial != context->serial + 1 ||
        serial != TextureLifecycle::UploadSerial() || context->slot >= context->count)
    {
      return static_cast<HRESULT>(invalidState);
    }

    context->gateEntered = true;
    context->serial = serial;
    const Slot &slot = savedSlots[context->slot];

    if (destination != slot.texture || source != context->stagingInterface || !context->Current())
    {
      context->gateResult = invalidState;
      return static_cast<HRESULT>(context->gateResult);
    }

    if (!ReadyDestination(*context, slot.surface))
    {
      context->gateResult = textureState.lastResult;
      return static_cast<HRESULT>(context->gateResult);
    }

    context->gateResult = S_OK;
    return S_OK;
  }

  struct NativeUploadScope
  {
    explicit NativeUploadScope(Context &context)
    {
      context.gateEntered = false;
      context.gateResult = invalidState;
      activeUploadContext = &context;
    }

    ~NativeUploadScope()
    {
      activeUploadContext = nullptr;
    }
  };

  static bool Description(Context &context, DWORD object, bool source, DWORD (&format)[8])
  {
    // Every descriptor query gets a fresh public loss check, including staging.
    DWORD &loss = source ? cacheResults.sourceIsLost : cacheResults.destinationIsLost;
    if (!CallSurface(context, object, 0x60, loss) || loss != S_OK)
    {
      return false;
    }

    if (!context.Current() || !Method(object, 0x58))
    {
      textureState.lastResult = invalidState;
      return false;
    }

    DWORD description[27];
    volatile DWORD *cleared = description;
    for (DWORD index = 0; index < 27; ++index)
    {
      cleared[index] = 0;
    }

    description[0] = sizeof(description);
    const HRESULT result = reinterpret_cast<SurfaceDescription>(Method(object, 0x58))(
        reinterpret_cast<void *>(object), description);
    textureState.lastResult = result;

    if (!context.Current())
    {
      textureState.lastResult = invalidState;
      return false;
    }

    if (result != S_OK)
    {
      return false;
    }

    if (description[2] != 64 || description[3] != 64 || description[18] != 32 ||
        !(description[19] & 0x40) || description[21] != 16)
    {
      textureState.lastResult = invalidState;
      return false;
    }

    for (DWORD index = 0; index < 8; ++index)
    {
      format[index] = description[18 + index];
    }

    return true;
  }

  // Stage two can restore a destination long before its upload. Permit one
  // additional Restore for this named slot, shared by the outer preflight and
  // the final Load gate. Both object pairs remain retained throughout.
  static bool ReadyDestination(Context &context, DWORD destination)
  {
    if (!HealthySource(context) ||
        !CallSurface(context, destination, 0x60, cacheResults.destinationIsLost))
    {
      return false;
    }

    if (cacheResults.destinationIsLost == S_OK)
    {
      return true;
    }

    if (cacheResults.destinationIsLost != surfaceLost || context.destinationRestored)
    {
      return false;
    }

    // Consume the budget before the call, including failed or reentrant calls.
    context.destinationRestored = true;
    if (!CallSurface(context, destination, 0x6c, cacheResults.restoreResult) ||
        cacheResults.restoreResult != S_OK)
    {
      return false;
    }

    TextureLifecycle::Increment(textureState.surfacesRestored);
    if (!CallSurface(context, destination, 0x60, cacheResults.postRestoreIsLost) ||
        cacheResults.postRestoreIsLost != S_OK)
    {
      return false;
    }

    // Restore does not restore pixel contents. Recheck compatible destinations
    // before the ordinary uploader rebuilds them from the still-healthy source.
    DWORD sourceFormat[8], destinationFormat[8];
    if (!Description(context, context.staging, true, sourceFormat) ||
        !Description(context, destination, false, destinationFormat))
    {
      return false;
    }

    for (DWORD index = 0; index < 8; ++index)
    {
      if (sourceFormat[index] != destinationFormat[index])
      {
        textureState.lastResult = invalidState;
        return false;
      }
    }

    // Descriptor callbacks may dispatch messages. A second loss is rejected,
    // without another Restore and without discarding the staging pixels.
    return HealthySource(context) &&
           CallSurface(context, destination, 0x60, cacheResults.postRestoreIsLost) &&
           cacheResults.postRestoreIsLost == S_OK;
  }

  struct BusyScope
  {
    BusyScope()
    {
      textureState.busy = 1;
    }

    ~BusyScope()
    {
      textureState.busy = 0;
    }
  };

  struct UploadScope
  {
    Context &context;
    DWORD slot, size, base, x, y, session;
    bool active = true;

    explicit UploadScope(Context &value)
        : context(value), slot(value.selection), size(value.size), base(value.source), x(value.x),
          y(value.y), session(value.session)
    {

      auto bytes = reinterpret_cast<volatile BYTE *>(config.gameBase + scratch);
      for (DWORD index = 0; index < 4096; ++index)
      {
        textureState.savedScratch[index] = bytes[index];
      }

      context.session = 0;
      Write(GameRva::drawSession, 0);
    }

    bool Close()
    {
      if (!active)
      {
        return context.Current();
      }

      active = false;
      if (!context.Current())
      {
        return false;
      }

      Write(selectedSlot, slot);
      Write(dimension, size);
      Write(sourceBase, base);
      Write(sourceX, x);
      Write(sourceY, y);
      Write(GameRva::drawSession, session);

      context.selection = slot;
      context.size = size;
      context.source = base;
      context.x = x;
      context.y = y;
      context.session = session;

      auto bytes = reinterpret_cast<volatile BYTE *>(config.gameBase + scratch);

      for (DWORD index = 0; index < 4096; ++index)
      {
        bytes[index] = textureState.savedScratch[index];
      }

      return true;
    }

    ~UploadScope()
    {
      if (active)
      {
        Close();
      }
    }
  };
} // namespace TextureRecovery

extern "C" bool __cdecl RecoverLostTextures()
{
  using namespace TextureRecovery;
  if (!Configured())
  {
    return true;
  }

  if (textureState.busy)
  {
    return false;
  }

  const DWORD epoch = state.restoreSuccesses;
  if (!epoch || epoch == textureState.completedEpoch)
  {
    return true;
  }

  if (state.restoreBusy || state.transitionDepth || state.setupCleanupBusy || Read(GameRva::quit) ||
      Read(GameRva::mode) != 2 || !Read(GameRva::directDraw) || !Read(GameRva::fullscreenActive) ||
      !Read(GameRva::hardware))
  {
    return true;
  }

  CacheErrorScope error;
  BusyScope busy;
  TextureLifecycle::Increment(textureState.passes);
  textureState.pendingEpoch = epoch;
  textureState.cachedCount = 0;
  textureState.lastStage = 1;
  textureState.lastSlot = 0xffffffff;
  textureState.lastResult = invalidState;

  cacheResults.sourceIsLost = cacheResults.destinationIsLost = invalidState;
  cacheResults.restoreResult = cacheResults.postRestoreIsLost = invalidState;
  cacheResults.nativeUploadResult = invalidState;

  Context context;

  if (!context.count || context.count > 1024 || !context.staging || !context.stagingInterface ||
      !context.guard0 || !context.guard1 || !context.guard2 || context.index0 > 15 ||
      context.index1 > 15 || context.index2 > 15 || !context.Current())
  {
    return Fail(invalidState);
  }

  // Capture and validate all named CPU sources before any destination mutation
  for (DWORD slot = 0; slot < context.count; ++slot)
  {
    Slot &saved = savedSlots[slot];
    saved.surface = Read(surfaceTable + slot * 4);
    saved.texture = Read(textureTable + slot * 4);
    saved.handle = Read(handles + slot * 4);
    const auto name = reinterpret_cast<const BYTE *>(context.base + gpuNames + slot * 20);

    for (DWORD index = 0; index < 20; ++index)
    {
      saved.name[index] = name[index];
    }

    if (!saved.surface || !saved.texture || !Method(saved.texture, 0x18))
    {
      return Fail(invalidState);
    }

    if (saved.name[0])
    {
      if (!saved.handle || !SourceFor(slot, saved.source))
      {
        return Fail(invalidState);
      }

      TextureLifecycle::Increment(textureState.cachedCount);
    }
  }

  References stagingReferences(context);
  if (!stagingReferences.Add(context.staging) || !stagingReferences.Add(context.stagingInterface))
  {
    return Fail(invalidState);
  }

  DWORD sourceFormat[8];
  if (!HealthySource(context) || !Description(context, context.staging, true, sourceFormat))
  {
    return Fail(textureState.lastResult);
  }

  textureState.lastStage = 2;
  for (DWORD slot = 0; slot < context.count; ++slot)
  {
    context.slot = slot;
    textureState.lastSlot = slot;
    const Slot &saved = savedSlots[slot];
    References destinationReferences(context);

    if (!destinationReferences.Add(saved.surface) || !destinationReferences.Add(saved.texture))
    {
      return Fail(invalidState);
    }

    if (!HealthySource(context) ||
        !CallSurface(context, saved.surface, 0x60, cacheResults.destinationIsLost))
    {
      return Fail(textureState.lastResult);
    }

    if (cacheResults.destinationIsLost == surfaceLost)
    {
      if (!CallSurface(context, saved.surface, 0x6c, cacheResults.restoreResult) ||
          cacheResults.restoreResult != S_OK)
      {
        return Fail(textureState.lastResult);
      }

      TextureLifecycle::Increment(textureState.surfacesRestored);
      if (!CallSurface(context, saved.surface, 0x60, cacheResults.postRestoreIsLost) ||
          cacheResults.postRestoreIsLost != S_OK)
      {
        return Fail(textureState.lastResult);
      }
    }
    else if (cacheResults.destinationIsLost != S_OK)
    {
      return Fail(cacheResults.destinationIsLost);
    }

    DWORD format[8];
    if (!HealthySource(context) || !Description(context, saved.surface, false, format))
    {
      return Fail(textureState.lastResult);
    }

    for (DWORD index = 0; index < 8; ++index)
    {
      if (sourceFormat[index] != format[index])
      {
        return Fail(invalidState);
      }
    }

    if (!destinationReferences.Close())
    {
      return Fail(invalidState);
    }
  }

  textureState.lastStage = 3;
  {
    UploadScope upload(context);
    for (DWORD slot = 0; slot < context.count; ++slot)
    {
      context.slot = slot;
      textureState.lastSlot = slot;
      const Slot &saved = savedSlots[slot];

      if (!context.Current())
      {
        return Fail(invalidState);
      }

      if (!saved.name[0])
      {
        continue;
      }

      context.destinationRestored = false;
      References destinationReferences(context);
      if (!destinationReferences.Add(saved.surface) || !destinationReferences.Add(saved.texture))
      {
        return Fail(invalidState);
      }

      if (!ReadyDestination(context, saved.surface))
      {
        return Fail(textureState.lastResult);
      }

      context.selection = slot;
      context.size = 64;
      context.source = saved.source.base;
      context.x = saved.source.x;
      context.y = saved.source.y;

      Write(selectedSlot, context.selection);
      Write(dimension, context.size);
      Write(sourceBase, context.source);
      Write(sourceX, context.x);
      Write(sourceY, context.y);

      if (!context.Current())
      {
        return Fail(invalidState);
      }

      int uploaded;
      {
        NativeUploadScope nativeUpload(context);
        uploaded = reinterpret_cast<int(__cdecl *)()>(context.base + GameRva::uploadTexture)();
      }

      cacheResults.nativeUploadResult = uploaded ? S_OK
                                        : context.gateEntered && context.gateResult != S_OK
                                            ? context.gateResult
                                            : 0x80004005;

      // The gate adopts exactly one wrapper entry. More entries invalidate the
      // staging owner even when a nested call restores all pointers and names.
      if (!context.gateEntered || !context.Current())
      {
        return Fail(invalidState);
      }

      if (!uploaded)
      {
        return Fail(cacheResults.nativeUploadResult);
      }

      TextureLifecycle::Increment(textureState.texturesReloaded);
      if (!destinationReferences.Close())
      {
        return Fail(invalidState);
      }
    }

    if (!upload.Close())
    {
      return Fail(invalidState);
    }
  }

  if (!stagingReferences.Close() || !context.Current())
  {
    return Fail(invalidState);
  }

  // Later destinations must not silently change an earlier completed slot
  for (DWORD slot = 0; slot < context.count; ++slot)
  {
    context.slot = slot;
    if (!context.Current())
    {
      return Fail(invalidState);
    }
  }

  textureState.lastStage = 4;
  textureState.lastResult = S_OK;
  textureState.completedEpoch = epoch;
  TextureLifecycle::Increment(textureState.successes);

  return true;
}

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_TEXTURE_RECOVERY_HPP
