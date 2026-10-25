// SPDX-License-Identifier: MIT

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#include "layoutPayload.hpp"

extern "C"
{
  PayloadConfig config = {};
  PayloadState state = {};
}

template <class T> static T Api(DWORD rva)
{
  return *reinterpret_cast<T *>(config.gameBase + rva);
}

#define Call(name, rva) Api<decltype(&::name)>(GameRva::rva)

static DWORD Read(DWORD rva)
{
  return *reinterpret_cast<volatile DWORD *>(config.gameBase + rva);
}

static void Write(DWORD rva, DWORD value)
{
  *reinterpret_cast<volatile DWORD *>(config.gameBase + rva) = value;
}

static bool Configured()
{
  return config.magic == 0x48474c50 && config.version == 1 && config.gameBase != 0;
}

#include "textureLifecycle.hpp"
#include "textureRecovery.hpp"
#include "textureLoadRecovery.hpp"

static unsigned Checks = 0;

static void Check(bool condition, const char *message)
{
  if (!condition)
  {
    throw std::runtime_error(message);
  }

  ++Checks;
}

static DWORD NativeErrorValue = 0;

static DWORD WINAPI FakeGetError()
{
  return NativeErrorValue;
}

static void WINAPI FakeSetError(DWORD value)
{
  NativeErrorValue = value;
}

enum class Operation
{
  Add,
  Release,
  IsLost,
  Description,
  Restore,
  Load,
  NativeUpload
};

struct Object
{
  DWORD *table = nullptr;
  DWORD references = 1, additions = 0, releases = 0, restores = 0;
  bool staging = false;
};

struct Fixture;
static Fixture *Active = nullptr;
static ULONG WINAPI FakeAdd(void *object);
static ULONG WINAPI FakeRelease(void *object);
static HRESULT WINAPI FakeIsLost(void *object);
static HRESULT WINAPI FakeRestore(void *object);
static HRESULT WINAPI FakeDescription(void *object, void *description);
static HRESULT WINAPI FakeLoad(void *destination, void *source);
static int __cdecl FakeUpload();

struct Fixture
{
  BYTE *image = nullptr;
  DWORD methods[33] = {};
  Object destination, source, destinationSurface, sourceSurface;
  std::vector<DWORD> loadIncomingError;
  std::function<void()> callback;
  Operation callbackOperation = Operation::Load;
  DWORD loads = 0, nativeUploads = 0;
  bool sourceLost = false, destinationLost = false, lostOnFirstLoad = false;
  bool requireHeldPairs = true, invalidFormat = false, nativeSkipsLoad = false;
  HRESULT restoreResult = S_OK, firstResult = S_OK, retryResult = S_OK;
  DWORD originalSlot = 0, originalSize = 128, originalSource = 0x123400, originalX = 0x10000;

  Fixture()
  {
    Active = this;
    image = static_cast<BYTE *>(
        VirtualAlloc(nullptr, 0x600000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Check(image != nullptr, "Owned fixture allocation failed");
    config = {};
    config.magic = 0x48474c50;
    config.version = 1;
    config.gameBase = reinterpret_cast<DWORD>(image);
    state = {};
    textureState = {};
    loadState = {};
    textureLifecycleState = {};
    TextureRecovery::activeUploadContext = nullptr;
    methods[1] = reinterpret_cast<DWORD>(&FakeAdd);
    methods[2] = reinterpret_cast<DWORD>(&FakeRelease);
    methods[0x18 / 4] = reinterpret_cast<DWORD>(&FakeLoad);
    methods[0x58 / 4] = reinterpret_cast<DWORD>(&FakeDescription);
    methods[0x60 / 4] = reinterpret_cast<DWORD>(&FakeIsLost);
    methods[0x6c / 4] = reinterpret_cast<DWORD>(&FakeRestore);
    destination.table = source.table = destinationSurface.table = sourceSurface.table = methods;
    source.staging = sourceSurface.staging = true;
    Write(GameRva::getLastError, reinterpret_cast<DWORD>(&FakeGetError));
    Write(GameRva::setLastError, reinterpret_cast<DWORD>(&FakeSetError));
    Write(GameRva::mode, 2);
    Write(GameRva::directDraw, 1);
    Write(GameRva::fullscreenActive, 1);
    Write(GameRva::hardware, 1);
    Write(GameRva::renderBpp, 16);
    Write(GameRva::drawObject, 0x1000);
    Write(GameRva::legacyDevice, 0x2000);
    Write(GameRva::primary, 0x3000);
    Write(GameRva::backbuffer, 0x4000);
    Write(GameRva::textureCount, 1);
    Write(GameRva::textureSelectedSlot, originalSlot);
    Write(GameRva::textureDimension, originalSize);
    Write(GameRva::textureSourceBase, originalSource);
    Write(GameRva::textureSourceX, originalX);
    Write(GameRva::textureInterfaceTable, reinterpret_cast<DWORD>(&destination));
    Write(GameRva::textureSurfaceTable, reinterpret_cast<DWORD>(&destinationSurface));
    Write(GameRva::textureStagingInterface, reinterpret_cast<DWORD>(&source));
    Write(GameRva::textureStagingSurface, reinterpret_cast<DWORD>(&sourceSurface));
    Write(GameRva::textureHandles, 7);
    Write(GameRva::uploadGuard0, 1);
    Write(GameRva::uploadGuard1, 1);
    Write(GameRva::uploadGuard2, 1);
    Write(GameRva::textureLodSizes, 64);
    const char name[] = "FIXTURE.RAW";
    std::memcpy(image + GameRva::textureGpuNames, name, sizeof(name));
    std::memcpy(image + GameRva::textureCpuCache, name, sizeof(name));
    Write(GameRva::textureCpuCache + 5 * 4, reinterpret_cast<DWORD>(image + 0x380000));

    for (DWORD index = 0; index < 4096; ++index)
    {
      image[GameRva::textureScratch + index] = static_cast<BYTE>(index);
    }

    // Owned mock native uploader, never a game image or live process.
    image[GameRva::uploadTexture] = 0xb8;
    const DWORD target = reinterpret_cast<DWORD>(&FakeUpload);
    std::memcpy(image + GameRva::uploadTexture + 1, &target, sizeof(target));
    image[GameRva::uploadTexture + 5] = 0xff;
    image[GameRva::uploadTexture + 6] = 0xe0;
    FlushInstructionCache(GetCurrentProcess(), image + GameRva::uploadTexture, 7);
    NativeErrorValue = 0x12345678;
  }

  ~Fixture()
  {
    config = {};
    VirtualFree(image, 0, MEM_RELEASE);
    Active = nullptr;
  }

  void On(Operation operation, std::function<void()> action)
  {
    callbackOperation = operation;
    callback = action;
  }

  void Notify(Operation operation)
  {
    if (callback && operation == callbackOperation)
    {
      auto action = callback;
      callback = {};
      action();
    }
  }

  void Held()
  {
    if (requireHeldPairs)
    {
      Check(destination.references == 2 && source.references == 2 &&
                destinationSurface.references == 2 && sourceSurface.references == 2,
            "Added COM call ran before all four references were retained");
    }
  }

  HRESULT Load()
  {
    return LoadTextureWithRecovery(&destination, &source);
  }

  bool Recover()
  {
    requireHeldPairs = false;
    state.restoreSuccesses = 1;

    return RecoverLostTextures();
  }

  void Balanced()
  {
    for (Object *object : {&destination, &source, &destinationSurface, &sourceSurface})
    {
      Check(object->references == 1 && object->additions == object->releases,
            "Unbalanced retained reference");
    }

    Check(!loadState.busy && !textureState.busy && !TextureRecovery::activeUploadContext,
          "Operation left a busy owner behind");
    Check(sourceSurface.restores == 0, "Staging source was restored");
  }
};

static ULONG WINAPI FakeAdd(void *pointer)
{
  auto &object = *static_cast<Object *>(pointer);
  ++object.references;
  ++object.additions;
  NativeErrorValue = 0xadd;
  Active->Notify(Operation::Add);

  return object.references;
}

static ULONG WINAPI FakeRelease(void *pointer)
{
  auto &object = *static_cast<Object *>(pointer);
  Check(object.references > 1, "Reference released without ownership");
  --object.references;
  ++object.releases;
  NativeErrorValue = 0xfeed;
  Active->Notify(Operation::Release);

  return object.references;
}

static HRESULT WINAPI FakeIsLost(void *pointer)
{
  auto &object = *static_cast<Object *>(pointer);
  Active->Held();
  NativeErrorValue = 0x1057;
  Active->Notify(Operation::IsLost);

  return (object.staging ? Active->sourceLost : Active->destinationLost)
             ? static_cast<HRESULT>(TextureRecovery::surfaceLost)
             : S_OK;
}

static HRESULT WINAPI FakeRestore(void *pointer)
{
  auto &object = *static_cast<Object *>(pointer);
  Active->Held();
  ++object.restores;
  NativeErrorValue = 0x567;
  Active->Notify(Operation::Restore);

  if (Active->restoreResult == S_OK)
  {
    Active->destinationLost = false;
  }

  return Active->restoreResult;
}

static HRESULT WINAPI FakeDescription(void *pointer, void *output)
{
  auto &object = *static_cast<Object *>(pointer);
  Active->Held();
  Check(!(object.staging ? Active->sourceLost : Active->destinationLost),
        "Descriptor queried before lost destination was restored");
  auto words = static_cast<DWORD *>(output);
  Check(words[0] == 108, "Wrong surface descriptor size");
  words[2] = 64;
  words[3] = 64;
  words[18] = 32;
  words[19] = 0x40;
  words[21] = 16;
  words[22] = 0xf800;
  words[23] = 0x07e0;
  words[24] = (!object.staging && Active->invalidFormat) ? 0x7f : 0x1f;
  NativeErrorValue = 0xd35c;
  Active->Notify(Operation::Description);

  return S_OK;
}

static HRESULT WINAPI FakeLoad(void *destination, void *source)
{
  auto &fixture = *Active;
  Check(destination == &fixture.destination && source == &fixture.source,
        "Native Load arguments changed");
  fixture.Held();
  ++fixture.loads;
  fixture.loadIncomingError.push_back(NativeErrorValue);
  const DWORD call = fixture.loads;
  fixture.Notify(Operation::Load);
  NativeErrorValue = 0xa000 + call;

  if (call == 1 && fixture.lostOnFirstLoad)
  {
    fixture.destinationLost = true;
    return static_cast<HRESULT>(TextureRecovery::surfaceLost);
  }

  return call == 1 ? fixture.firstResult : fixture.retryResult;
}

static int __cdecl FakeUpload()
{
  auto &fixture = *Active;
  ++fixture.nativeUploads;
  Check(fixture.destination.references == 2 && fixture.source.references == 2 &&
            fixture.destinationSurface.references == 2 && fixture.sourceSurface.references == 2,
        "Cache upload did not retain both COM pairs");
  fixture.Notify(Operation::NativeUpload);

  if (fixture.nativeSkipsLoad)
  {
    return 1;
  }

  if (fixture.Load() != S_OK)
  {
    return 0;
  }

  std::memset(fixture.image + GameRva::textureScratch, 0xee, 4096);
  return 1;
}

int main()
{
  try
  {
    {
      Fixture fixture;
      Check(fixture.Load() == S_OK && fixture.loads == 1, "Healthy upload failed");
      Check(fixture.destinationSurface.restores == 0, "Healthy destination restored");
      Check(fixture.loadIncomingError[0] == 0x12345678 && NativeErrorValue == 0xa001,
            "Native LastError was not preserved through readiness and cleanup");
      Check(TextureLifecycle::UploadSerial() == 1, "Ordinary upload did not advance serial");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.destinationLost = true;
      Check(fixture.Load() == S_OK && fixture.destinationSurface.restores == 1 &&
                fixture.loads == 1,
            "Lost destination was not restored before first Load");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.lostOnFirstLoad = true;
      Check(fixture.Load() == S_OK && fixture.loads == 2 &&
                fixture.destinationSurface.restores == 1,
            "Single post-Load retry failed");
      Check(TextureLifecycle::UploadSerial() == 1,
            "Native retry incorrectly counted as wrapper entry");
      Check(fixture.loadIncomingError[1] == 0xa001 && NativeErrorValue == 0xa002,
            "Retry LastError was not restored after cleanup");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.destinationLost = fixture.lostOnFirstLoad = true;
      Check(static_cast<DWORD>(fixture.Load()) == TextureRecovery::surfaceLost &&
                fixture.loads == 1 && fixture.destinationSurface.restores == 1,
            "Preflight and retry consumed more than one Restore budget");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.On(Operation::Description, [&] { fixture.destinationLost = true; });
      Check(fixture.Load() == S_OK && fixture.loads == 1 &&
                fixture.destinationSurface.restores == 1,
            "Destination loss during descriptor callback was not rechecked");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.sourceLost = true;
      Check(static_cast<DWORD>(fixture.Load()) == TextureRecovery::surfaceLost &&
                fixture.loads == 0,
            "Lost staging pixels reached native Load");
      Check(NativeErrorValue == 0x12345678, "Rejected preflight changed caller LastError");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.invalidFormat = true;
      Check(static_cast<DWORD>(fixture.Load()) == TextureRecovery::invalidState &&
                fixture.loads == 0,
            "Mismatching source/destination formats were admitted");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.destinationLost = true;
      fixture.restoreResult = E_FAIL;
      Check(fixture.Load() == E_FAIL && fixture.loads == 0 &&
                fixture.destinationSurface.restores == 1,
            "Restore failure was hidden");
      fixture.Balanced();
    }

    for (Operation operation : {Operation::Add, Operation::IsLost, Operation::Description,
                                Operation::Restore, Operation::Load, Operation::Release})
    {
      Fixture fixture;

      if (operation == Operation::Restore)
      {
        fixture.destinationLost = true;
      }

      fixture.On(operation, [] { AdvanceTextureLifecycle(); });
      Check(fixture.Load() != S_OK, "Lifecycle callback failed to invalidate upload");
      Check(TextureLifecycle::Serial() == 1, "Lifecycle callback was not reached");
      Check(NativeErrorValue == (fixture.loads ? 0xa001u : 0x12345678u),
            "Invalidation cleanup lost native LastError");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.On(Operation::IsLost, [] { config = {}; });
      Check(fixture.Load() != S_OK && fixture.loads == 0, "Invalid configuration was reused");
      Check(NativeErrorValue == 0x12345678, "Captured error setter was not retained");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.On(Operation::Description, [&] { fixture.image[GameRva::textureGpuNames] = 'X'; });
      Check(fixture.Load() != S_OK && fixture.loads == 0, "Changed selected name was admitted");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.On(Operation::Load,
                 [&] { Check(fixture.Load() == S_OK, "Nested native passthrough failed"); });
      Check(static_cast<DWORD>(fixture.Load()) == TextureRecovery::invalidState &&
                fixture.loads == 2,
            "Nested upload did not invalidate outer success");
      Check(TextureLifecycle::UploadSerial() == 2, "Nested entry did not increment serial");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.firstResult = E_FAIL;
      fixture.On(Operation::Load, [] { AdvanceTextureLifecycle(); });
      Check(fixture.Load() == E_FAIL && fixture.loads == 1 && NativeErrorValue == 0xa001,
            "Native failure was replaced during invalidation");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.requireHeldPairs = false;
      config.magic = 0;
      Check(fixture.Load() == S_OK && TextureLifecycle::UploadSerial() == 1,
            "Unconfigured passthrough lost native behavior or upload serial");
      AdvanceTextureLifecycle();
      AdvanceTextureFrameEpoch();
      AdvanceTextureFrame();
      Check(TextureLifecycle::Serial() == 0 && TextureLifecycle::FrameEpoch() == 0 &&
                TextureLifecycle::FrameSerial() == 0 && NativeErrorValue == 0xa001,
            "Unconfigured callback changed ownership or LastError");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      textureLifecycleState.uploadSerial = 0xfffffffe;
      Check(TextureLifecycle::BeginUpload() == 0xffffffff && !TextureLifecycle::Incomplete(),
            "Last unique upload generation was not issued");
      Check(TextureLifecycle::BeginUpload() == 0 && TextureLifecycle::Incomplete() &&
                TextureLifecycle::UploadSerial() == 0xffffffff,
            "Upload generation wrapped instead of failing closed");
      fixture.requireHeldPairs = false;
      Check(fixture.Load() == S_OK && TextureLifecycle::Incomplete(),
            "Overflow changed original passthrough or cleared incomplete state");
      fixture.Balanced();
    }

    for (DWORD *counter : {&textureLifecycleState.serial, &textureLifecycleState.frameSerial,
                           &textureLifecycleState.frameEpoch})
    {
      Fixture fixture;
      *counter = 0xffffffff;
      NativeErrorValue = 0x12345678;
      AdvanceTextureLifecycle();
      AdvanceTextureFrameEpoch();
      AdvanceTextureFrame();
      Check(*counter == 0xffffffff && TextureLifecycle::Incomplete() &&
                NativeErrorValue == 0x12345678,
            "Lifecycle/frame overflow reused a generation or changed LastError");
    }

    {
      Fixture fixture;
      fixture.destinationLost = true;
      Check(fixture.Recover() && fixture.loads == 1 && fixture.nativeUploads == 1 &&
                fixture.destinationSurface.restores == 1 && textureState.completedEpoch == 1,
            "Owned cache recovery did not complete");
      Check(TextureLifecycle::UploadSerial() == 1 && NativeErrorValue == 0x12345678,
            "Cache wrapper serial or caller LastError changed");
      Check(Read(GameRva::textureDimension) == fixture.originalSize &&
                Read(GameRva::textureSourceBase) == fixture.originalSource &&
                Read(GameRva::textureSourceX) == fixture.originalX,
            "Successful cache upload did not restore source selection");
      for (DWORD index = 0; index < 4096; ++index)
      {
        if (fixture.image[GameRva::textureScratch + index] != static_cast<BYTE>(index))
        {
          throw std::runtime_error("Cache upload did not restore scratch bytes");
        }
      }

      Check(true, "Complete scratch equality");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.sourceLost = true;
      Check(!fixture.Recover() && fixture.loads == 0 && fixture.destinationSurface.restores == 0,
            "Cache recovered from an invalid staging source");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.On(Operation::NativeUpload, [&] { fixture.image[GameRva::textureGpuNames] = 'X'; });
      Check(!fixture.Recover() && fixture.loads == 0 && textureState.completedEpoch == 0,
            "Cache admission allowed an immutable slot name change");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.On(Operation::NativeUpload,
                 [&] { Check(fixture.Load() == S_OK, "Initial owned cache load failed"); });
      Check(!fixture.Recover() && fixture.loads == 1 && TextureLifecycle::UploadSerial() == 2 &&
                textureState.completedEpoch == 0,
            "Cache gate accepted two wrapper entries for one staging owner");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.nativeSkipsLoad = true;
      Check(!fixture.Recover() && fixture.loads == 0 && textureState.completedEpoch == 0,
            "Cache upload without matching Load was counted as complete");
      fixture.Balanced();
    }

    {
      Fixture fixture;
      fixture.On(Operation::NativeUpload, [] { config = {}; });
      Check(!fixture.Recover() && fixture.loads == 0 && NativeErrorValue == 0x12345678,
            "Cache callback reused invalid native globals or error API");
      fixture.Balanced();
    }

    std::printf("{\"passed\":true,\"checks\":%u,\"gameAccessed\":false}\n", Checks);
    return 0;
  }
  catch (const std::exception &error)
  {
    std::printf("{\"passed\":false,\"checks\":%u,\"error\":\"%s\"}\n", Checks, error.what());
    return 1;
  }
}
