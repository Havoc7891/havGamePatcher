// SPDX-License-Identifier: MIT

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <initializer_list>
#include "layoutPayload.hpp"

extern "C"
{
  PayloadConfig config = {};
  PayloadState state = {};
  TextureLoadState loadState = {};
}

// Only the existing recovery boundary is mocked. The transfer and lifecycle
// implementations below are the same headers compiled into the patch payload.
struct FixtureRecoveryState
{
  DWORD busy, passes;
};

static FixtureRecoveryState textureState = {};

static bool Configured()
{
  return config.magic == 0x48474c50 && config.version == 1 && config.gameBase != 0;
}

static DWORD Read(DWORD rva)
{
  return *reinterpret_cast<volatile DWORD *>(config.gameBase + rva);
}

#include "textureLifecycle.hpp"
#include "textureTransfer.hpp"

namespace
{
  BYTE *image = nullptr;
  BYTE *destinationBytes = nullptr;
  constexpr DWORD IncomingError = 0x12340123;
  constexpr DWORD NativeError = 0x98760987;
  constexpr DWORD Failure = 0x8876dead;
  DWORD U;
  BYTE *nativeStack, *sourcePixels;
  DWORD synthetic[160];
  int checks = 0;
  int originalCalls, adds, releases, locks, unlocks, lostQueries;
  DWORD nativeResult, lostResult, lockResult, unlockResult;
  LONG requestedPitch;
  DWORD requestedPixels;
  bool invalidFormat;
  int triggerPhase;

  enum Phase
  {
    NoPhase,
    AddReference,
    NativeLoad,
    IsLost,
    LockSurface,
    UnlockSurface,
    ReleaseReference
  };

  enum Action
  {
    NoAction,
    Nested,
    ChangeSlot,
    ChangeDevice,
    ChangeName,
    InvalidateGeneration,
    ChangeSource,
    NestedCapture
  };

  Action triggerAction;
  bool triggerArmed;

  struct Object
  {
    DWORD *vtable;
    int references;
  };

  DWORD methods[33];
  Object objects[4];
  BYTE expectedPixels[8192];

  DWORD Address(const void *p)
  {
    return static_cast<DWORD>(reinterpret_cast<uintptr_t>(p));
  }

  DWORD &At(DWORD rva)
  {
    return *reinterpret_cast<DWORD *>(image + rva);
  }

  DWORD *Stage()
  {
    return synthetic;
  }

  void Require(bool value, const char *name)
  {
    if (!value)
    {
      std::printf("FAIL %s\n", name);
      std::fflush(stdout);
      ExitProcess(1);
    }

    ++checks;
  }

  void Trigger(Phase phase)
  {
    if (!triggerArmed || triggerPhase != phase)
    {
      return;
    }

    triggerArmed = false;

    switch (triggerAction)
    {
    case Nested:
      TransferTexture(Address(objects), Address(objects + 1), U);
      break;

    case ChangeSlot:
      At(GameRva::textureSelectedSlot) = 1;
      break;

    case ChangeDevice:
      ++At(GameRva::legacyDevice);
      break;

    case ChangeName:
      reinterpret_cast<char *>(image + GameRva::textureGpuNames)[0] = 'X';
      break;

    case InvalidateGeneration:
      ++TextureTransfer::textureTransferStats.generation;
      break;

    case ChangeSource:
      std::memset(sourcePixels, 0xee, 8192);
      break;

    case NestedCapture:
      CaptureTextureSource(U, Address(sourcePixels) + 8192);
      break;

    default:
      break;
    }
  }

  DWORD WINAPI AddRef(void *p)
  {
    ++adds;
    ++static_cast<Object *>(p)->references;
    Trigger(AddReference);
    SetLastError(0xadd);

    return static_cast<Object *>(p)->references;
  }

  DWORD WINAPI Release(void *p)
  {
    ++releases;
    --static_cast<Object *>(p)->references;
    Trigger(ReleaseReference);
    SetLastError(0xdec);

    return static_cast<Object *>(p)->references;
  }

  DWORD WINAPI Lost(void *)
  {
    ++lostQueries;
    Trigger(IsLost);
    SetLastError(0x1057);

    return lostResult;
  }

  DWORD WINAPI Lock(void *, void *rectangle, DWORD *descriptor, DWORD flags, void *event)
  {
    ++locks;
    Require(rectangle == nullptr && event == nullptr && flags == 1,
            "destination Lock uses native full-surface WAIT contract");
    if (!lockResult)
    {
      std::memcpy(descriptor, Stage() + 128, 108);
      descriptor[4] = static_cast<DWORD>(requestedPitch);
      descriptor[9] = requestedPixels;

      if (invalidFormat)
      {
        descriptor[22] ^= 0x400;
      }
    }

    Trigger(LockSurface);
    SetLastError(0x10cc);

    return lockResult;
  }

  DWORD WINAPI Unlock(void *, void *pointer)
  {
    ++unlocks;
    Require(pointer == nullptr, "destination Unlock uses matching full-surface argument");
    Trigger(UnlockSurface);
    SetLastError(0x110cc);

    return unlockResult;
  }

  DWORD WINAPI Original(void *destination, void *source)
  {
    ++originalCalls;
    Require(destination == objects && source == objects + 1,
            "original Load receives exact original COM objects");
    TextureLifecycle::BeginUpload();
    Trigger(NativeLoad);
    SetLastError(NativeError);

    return nativeResult;
  }

  void Reset()
  {
    config.magic = 0x48474c50;
    config.version = 1;
    config.gameBase = Address(image);
    state = {};
    loadState = {};
    textureState = {};
    textureLifecycleState = {};
    std::memset(&TextureTransfer::textureTransferStats, 0,
                sizeof(TextureTransfer::textureTransferStats));
    std::memset(&TextureTransfer::textureTransferState, 0xa5,
                sizeof(TextureTransfer::textureTransferState));
    std::memset(synthetic, 0, sizeof(synthetic));
    std::memset(nativeStack, 0, 4096);

    for (unsigned i = 0; i < sizeof(expectedPixels); ++i)
    {
      expectedPixels[i] = static_cast<BYTE>(i * 7 + (i >> 8));
    }

    std::memcpy(sourcePixels, expectedPixels, 8192);
    std::memset(destinationBytes, 0xcd, 64 * 256);

    for (Object &object : objects)
    {
      object.references = 7;
    }

    originalCalls = adds = releases = locks = unlocks = lostQueries = 0;
    nativeResult = lostResult = lockResult = unlockResult = 0;
    requestedPitch = 128;
    requestedPixels = Address(destinationBytes);
    invalidFormat = false;
    triggerPhase = NoPhase;
    triggerAction = NoAction;
    triggerArmed = false;
    SetLastError(IncomingError);
    DWORD *s = Stage();
    s[0] = 1;
    s[1] = 1;
    s[2] = 0xcc73;
    s[3] = GetCurrentThreadId();
    s[4] = s[5] = U;
    s[9] = Address(image);
    s[10] = 15;
    s[11] = 13184;
    s[12] = Address(image) + 0xc0ff;
    s[13] = Address(objects + 3);
    s[16] = 0;
    s[17] = 2;
    s[18] = 90;
    s[19] = Address(objects);
    s[20] = Address(objects + 2);
    s[21] = Address(objects + 1);
    s[22] = Address(objects + 3);
    s[24] = 64;
    s[25] = 0x18001000;
    s[26] = 64;
    s[27] = 128;
    s[32] = 7;
    s[33] = 100;
    s[34] = 4;
    s[35] = 3;
    s[36] = 0;
    s[44] = 1;
    s[46] = 11;
    s[47] = 5;
    s[48] = GetCurrentThreadId();
    s[49] = 2;
    s[50] = 1;
    s[51] = 16;
    s[54] = 0x19100000;
    s[56] = 1;
    DWORD *d = s + 128;
    d[0] = 108;
    d[1] = 0x100f;
    d[2] = d[3] = 64;
    d[4] = 128;
    d[9] = Address(sourcePixels);
    d[18] = 32;
    d[19] = 0x41;
    d[21] = 16;
    d[22] = 0x7c00;
    d[23] = 0x3e0;
    d[24] = 0x1f;
    d[25] = 0x8000;
    std::memcpy(reinterpret_cast<BYTE *>(s) + 620, "FIXTURE.RAW", 12);
    textureLifecycleState.serial = s[32];
    textureLifecycleState.uploadSerial = s[33];
    state.restoreSuccesses = s[34];
    At(GameRva::legacyDevice) = s[54];
    At(GameRva::textureSelectedSlot) = s[16];
    At(GameRva::textureCount) = s[17];
    At(GameRva::textureStagingInterface) = s[21];
    At(GameRva::textureStagingSurface) = s[22];
    At(GameRva::textureLock) = s[23];
    At(GameRva::mode) = s[49];
    At(GameRva::hardware) = s[50];
    At(GameRva::renderBpp) = s[51];
    At(GameRva::textureSourceBase) = s[25];
    At(GameRva::textureSourceX) = s[26];
    At(GameRva::textureSourceY) = s[27];
    At(GameRva::textureDimension) = s[24];
    textureState.busy = s[44];
    state.busy = s[36];
    At(GameRva::mainMenuVisible) = s[55];
    At(GameRva::gameStage) = s[56];
    At(GameRva::legacyVideo) = s[57];
    textureState.passes = s[35];
    textureLifecycleState.frameSerial = s[46];
    textureLifecycleState.frameEpoch = s[47];
    At(GameRva::drawLock) = s[52];
    At(GameRva::textureHandles) = s[18];
    At(GameRva::textureInterfaceTable) = s[19];
    At(GameRva::textureSurfaceTable) = s[20];
    std::memcpy(image + GameRva::textureGpuNames, reinterpret_cast<BYTE *>(s) + 620, 20);
    std::memcpy(nativeStack + 0x20, d, 108);
    *reinterpret_cast<DWORD *>(nativeStack + 0x1c) = s[13];
    *reinterpret_cast<DWORD *>(nativeStack + 0x8c) = s[12];
  }

  void Capture()
  {
    DWORD error = GetLastError();
    CaptureTextureSource(U, Address(sourcePixels) + 8192);
    Require(GetLastError() == error, "pre-Unlock capture preserves LastError");
    Require(TextureTransfer::textureTransferStats.busy == 0,
            "capture guard released before native Unlock");
  }

  DWORD Run(bool capture = true)
  {
    if (capture)
    {
      Capture();
    }

    At(GameRva::drawLock) = 0;
    DWORD result = TransferTexture(Address(objects), Address(objects + 1), U);
    Require(GetLastError() == NativeError,
            "original immediate post-Load LastError survives every cleanup path");
    Require(TextureTransfer::textureTransferStats.busy == 0 &&
                TextureTransfer::textureTransferStats.activeGeneration == 0,
            "both ownership guards released");
    Require(adds == releases, "all acquired COM references released");

    for (const Object &object : objects)
    {
      Require(object.references == 7, "original COM reference count restored");
    }

    Require(TextureTransfer::textureTransferState.row.value[29] == static_cast<DWORD>(adds),
            "telemetry records acquired references");
    Require(TextureTransfer::textureTransferState.row.value[30] == static_cast<DWORD>(releases),
            "telemetry records released references");
    Require(TextureTransfer::textureTransferState.row.value[18] == nativeResult,
            "native HRESULT recorded independently");
    return result;
  }

  void Unchanged()
  {
    bool unchanged = true;

    for (int i = 0; i < 64 * 256; ++i)
    {
      unchanged = unchanged && destinationBytes[i] == 0xcd;
    }

    Require(unchanged, "rejected operation leaves destination bytes unchanged");
  }

  void Copied()
  {
    bool copied = true;
    bool paddingPreserved = true;

    for (int row = 0; row < 64; ++row)
    {
      copied = copied && std::memcmp(destinationBytes + row * requestedPitch,
                                     expectedPixels + row * 128, 128) == 0;
      for (int i = 128; i < requestedPitch; ++i)
      {
        paddingPreserved = paddingPreserved && destinationBytes[row * requestedPitch + i] == 0xcd;
      }
    }

    Require(copied, "all copied rows equal private pre-Unlock snapshot");
    Require(paddingPreserved, "all row padding preserved");
  }

  void Arm(Phase phase, Action action)
  {
    triggerPhase = phase;
    triggerAction = action;
    triggerArmed = true;
  }
} // namespace

extern "C" HRESULT WINAPI LoadTextureWithRecovery(void *destination, void *source)
{
  return static_cast<HRESULT>(Original(destination, source));
}

int main()
{
  image = static_cast<BYTE *>(
      VirtualAlloc(nullptr, 0x1000000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  destinationBytes = static_cast<BYTE *>(
      VirtualAlloc(nullptr, 64 * 256, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  nativeStack =
      static_cast<BYTE *>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  sourcePixels =
      static_cast<BYTE *>(VirtualAlloc(nullptr, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  U = Address(nativeStack);
  Require(image && destinationBytes && nativeStack && sourcePixels, "fixture local allocations");
  methods[1] = Address(reinterpret_cast<void *>(AddRef));
  methods[2] = Address(reinterpret_cast<void *>(Release));
  methods[0x60 / 4] = Address(reinterpret_cast<void *>(Lost));
  methods[0x64 / 4] = Address(reinterpret_cast<void *>(Lock));
  methods[0x80 / 4] = Address(reinterpret_cast<void *>(Unlock));

  for (int i = 0; i < 4; ++i)
  {
    objects[i] = {methods, 7};
  }

  for (unsigned i = 0; i < sizeof(expectedPixels); ++i)
  {
    expectedPixels[i] = static_cast<BYTE>(i * 7 + (i >> 8));
  }

  for (int pitch : {128, 192})
  {
    Reset();
    requestedPitch = pitch;
    Require(Run() == 0 && originalCalls == 1 && locks == 1 && unlocks == 1,
            "single original and balanced successful copy");
    Require(TextureTransfer::textureTransferStats.copied == 1, "success published");
    Copied();
  }

  Reset();
  At(GameRva::drawLock) = 1;
  Require(Run() == 0 && TextureTransfer::textureTransferStats.copied == 1,
          "native drawLock1 to0 transition is admitted");
  Copied();
  Reset();
  Arm(NativeLoad, ChangeSource);
  Require(Run() == 0 && TextureTransfer::textureTransferStats.copied == 1,
          "source remains private across native callbacks");
  Copied();

  Reset();
  nativeResult = Failure;
  Require(Run() == Failure && originalCalls == 1 && locks == 0 && unlocks == 0,
          "native failure remains authoritative with no added Lock");
  Unchanged();
  Reset();
  lostResult = Failure;
  Require(Run() == Failure && locks == 0 && TextureTransfer::textureTransferStats.failed == 1,
          "admitted IsLost failure propagates");
  Unchanged();
  Reset();
  lockResult = Failure;
  Require(Run() == Failure && locks == 1 && unlocks == 0 &&
              TextureTransfer::textureTransferStats.failed == 1,
          "failed Lock propagates and is not unlocked");
  Unchanged();
  Reset();
  unlockResult = Failure;
  Require(Run() == Failure && unlocks == 1 && TextureTransfer::textureTransferStats.failed == 1,
          "failed Unlock propagates after copy");
  Copied();
  Reset();
  invalidFormat = true;
  Require(Run() == 0 && locks == 1 && unlocks == 1 &&
              TextureTransfer::textureTransferStats.skipped == 1,
          "unsupported format skips and balances Lock");
  Unchanged();
  Reset();
  requestedPitch = -128;
  Require(Run() == 0 && unlocks == 1 && TextureTransfer::textureTransferStats.skipped == 1,
          "negative pitch explicitly unsupported");
  Unchanged();

  for (DWORD pointer : {0u, 0xffffu, 0xfffffff0u, 0x80000000u})
  {
    Reset();
    requestedPixels = pointer;
    Require(Run() == 0 && unlocks == 1 && TextureTransfer::textureTransferStats.skipped == 1,
            "invalid destination range skips and balances Lock");
    Unchanged();
  }

  for (Phase phase :
       {AddReference, NativeLoad, IsLost, LockSurface, UnlockSurface, ReleaseReference})
  {
    Reset();
    Arm(phase, Nested);
    Require(Run() == 0 && originalCalls == 2 &&
                TextureTransfer::textureTransferStats.nestedEntries == 1 &&
                TextureTransfer::textureTransferStats.copied == 0,
            "nested invocation keeps original calls and invalidates outer success");
    if (phase == UnlockSurface || phase == ReleaseReference)
    {
      Copied();
      Require(TextureTransfer::textureTransferState.row.value[28] == 64,
              "post-copy invalidation retains actual row count");
    }
    else
    {
      Unchanged();
    }

    Require(unlocks == locks, "nested callback does not leak a destination Lock");
  }

  for (Action action : {ChangeSlot, ChangeDevice, ChangeName, InvalidateGeneration})
  {
    Reset();
    Arm(NativeLoad, action);
    Require(Run() == 0 && originalCalls == 1 && locks == 0 &&
                TextureTransfer::textureTransferStats.copied == 0,
            "changed ownership after native call blocks destination correction");
    Unchanged();
  }

  for (Phase phase :
       {AddReference, NativeLoad, IsLost, LockSurface, UnlockSurface, ReleaseReference})
  {
    Reset();
    Arm(phase, NestedCapture);
    Require(Run() == 0 && originalCalls == 1 &&
                TextureTransfer::textureTransferStats.nestedEntries == 1 &&
                TextureTransfer::textureTransferStats.copied == 0,
            "nested pre-Unlock capture invalidates owner without overwriting private bytes");
    if (phase == UnlockSurface || phase == ReleaseReference)
    {
      Copied();
    }
    else
    {
      Unchanged();
    }

    Require(TextureTransfer::textureTransferStats.readyGeneration == 0,
            "nested capture leaves no reusable pending owner");
  }

  for (int corruption = 0; corruption < 10; ++corruption)
  {
    Reset();
    DWORD *descriptor = reinterpret_cast<DWORD *>(nativeStack + 0x20);

    switch (corruption)
    {
    case 0:
      At(GameRva::textureCount) = 0;
      break;

    case 1:
      At(GameRva::textureSelectedSlot) = At(GameRva::textureCount);
      break;

    case 2:
      At(GameRva::textureLock) = 1;
      break;

    case 3:
      descriptor[4] = 192;
      break;

    case 4:
      descriptor[21] = 32;
      break;

    case 5:
      descriptor[9] = 0x80000000u;
      break;

    case 6:
      *reinterpret_cast<DWORD *>(nativeStack + 0x1c) = Address(objects + 2);
      break;

    case 7:
      At(GameRva::textureSurfaceTable) = At(GameRva::textureStagingSurface);
      break;

    case 8:
      At(GameRva::textureInterfaceTable) = At(GameRva::textureStagingInterface);
      break;

    case 9:
      std::memset(image + GameRva::textureGpuNames, 'X', 20);
      break;
    }

    Require(Run() == 0 && originalCalls == 1 && adds == 0 && locks == 0 &&
                TextureTransfer::textureTransferStats.skipped == 1,
            "unsupported source preserves original Load without adding destination operations");
    Unchanged();
  }

  Reset();
  Capture();
  Require(TextureTransfer::textureTransferStats.readyGeneration != 0,
          "capture may remain pending when native exits without Load");
  expectedPixels[8191] ^= 0x55;
  sourcePixels[8191] = expectedPixels[8191];
  Capture();
  Require(Run(false) == 0 && TextureTransfer::textureTransferStats.copied == 1,
          "fresh capture supersedes abandoned pending owner");
  Copied();
  Require(TextureTransfer::textureTransferStats.readyGeneration == 0,
          "successful Load consumes pending owner once");
  int priorLocks = locks;
  DWORD priorCopies = TextureTransfer::textureTransferStats.copied;
  TransferTexture(Address(objects), Address(objects + 1), U);
  Require(locks == priorLocks && TextureTransfer::textureTransferStats.copied == priorCopies,
          "reused canonical stack without fresh capture cannot repeat copy");
  for (int corruption = 0; corruption < 5; ++corruption)
  {
    Reset();
    Capture();

    switch (corruption)
    {
    case 0:
      ++*reinterpret_cast<DWORD *>(nativeStack + 0x8c);
      break;

    case 1:
      ++*reinterpret_cast<DWORD *>(nativeStack + 0x20 + 16);
      break;

    case 2:
      ++TextureTransfer::textureTransferStats.generation;
      break;

    case 3:
      ++textureLifecycleState.serial;
      break;

    case 4:
      TextureLifecycle::BeginUpload();
      break;
    }

    Require(Run(false) == 0 && locks == 0 && TextureTransfer::textureTransferStats.skipped == 1,
            "changed pending identity cannot be consumed");
    Unchanged();
  }

  Reset();
  CaptureTextureSource(U, Address(sourcePixels) + 8190);
  Require(TextureTransfer::textureTransferStats.readyGeneration == 0 &&
              TextureTransfer::textureTransferStats.busy == 0,
          "wrong converted end pointer cannot stage source");
  Reset();
  TextureTransfer::textureTransferStats.generation = 0xfffffffeu;
  Require(Run() == 0 && TextureTransfer::textureTransferStats.incompleteFlags && locks == 0,
          "generation saturation fails closed while preserving native Load");
  Unchanged();
  Reset();
  Capture();
  config.gameBase = 0;
  Require(Run(false) == 0 && originalCalls == 1 && adds == 0 && locks == 0,
          "invalidated configuration preserves native Load without reading game globals");
  Unchanged();
  Reset();
  config.gameBase = 0;
  CaptureTextureSource(0, 0);
  Require(TextureTransfer::textureTransferStats.readyGeneration == 0 &&
              TextureTransfer::textureTransferStats.busy == 0 && GetLastError() == IncomingError,
          "unconfigured source hook rejects before reading the native stack");
  Require(TransferTexture(Address(objects), Address(objects + 1), 0) == 0 && originalCalls == 1 &&
              adds == 0 && locks == 0,
          "unconfigured Load passes through exactly once");
  Unchanged();
  Reset();
  textureLifecycleState.incomplete = 1;
  Require(Run() == 0 && originalCalls == 1 && locks == 0 &&
              TextureTransfer::textureTransferStats.staged == 0,
          "incomplete lifecycle rejects source admission and preserves native Load");
  Unchanged();
  Reset();
  Capture();
  textureLifecycleState.incomplete = 1;
  Require(Run(false) == 0 && originalCalls == 1 && locks == 0,
          "lifecycle invalidation between capture and Load rejects stale source");
  Unchanged();
  Reset();
  textureLifecycleState.uploadSerial = 0xfffffffeu;
  Require(Run() == 0 && originalCalls == 1 && locks == 0 &&
              TextureTransfer::textureTransferStats.staged == 0,
          "upload serial near saturation cannot admit correction");
  Unchanged();
  Reset();
  Capture();
  Require(TextureTransfer::textureTransferState.source[28] == 0 &&
              TextureTransfer::textureTransferState.source[29] == 0 &&
              TextureTransfer::textureTransferState.source[30] == 0 &&
              TextureTransfer::textureTransferState.source[31] == 0,
          "unused diagnostic metadata remains zero without edition-specific reads");
  Require(Run(false) == 0 && TextureTransfer::textureTransferStats.copied == 1,
          "zero unused metadata does not change admission");
  Copied();
  std::printf("{\"passed\":true,\"checks\":%d}\n", checks);
  VirtualFree(destinationBytes, 0, MEM_RELEASE);
  VirtualFree(image, 0, MEM_RELEASE);
  VirtualFree(nativeStack, 0, MEM_RELEASE);
  VirtualFree(sourcePixels, 0, MEM_RELEASE);

  return 0;
}
