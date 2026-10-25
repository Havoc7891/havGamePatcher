// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_D3D_BRIDGE_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_D3D_BRIDGE_HPP

// Use the public Direct3D2 creation and scene interfaces so Windows can track
// hardware drawing before the game relocks its CPU-visible backbuffer.
// The game retains IDirect3DDevice1 for execute buffers and all other calls.
// Every interface acquired here is released in this call, except the returned
// legacy device, which follows the game's existing destruction path.
extern "C"
{
  BridgeState bridgeState = {};
}

namespace D3DBridge
{
  static const GUID root2Id = {
      0x6aae1ec1, 0x662a, 0x11d0, {0x88, 0x9d, 0x00, 0xaa, 0x00, 0xbb, 0xb7, 0x6a}};
  static const GUID device1Id = {
      0x64108800, 0x957d, 0x11d0, {0x89, 0xab, 0x00, 0xa0, 0xc9, 0x05, 0x41, 0x29}};
  static const GUID device2Id = {
      0x93281501, 0x8cf8, 0x11d0, {0x89, 0xab, 0x00, 0xa0, 0xc9, 0x05, 0x41, 0x29}};

  enum Stage : DWORD
  {
    arguments = 1,
    rootQuery,
    creation,
    legacyQuery,
    created,
    sceneQuery,
    begin,
    end
  };

  using Query = HRESULT(WINAPI *)(void *, const GUID *, void **);
  using Release = ULONG(WINAPI *)(void *);
  using Create = HRESULT(WINAPI *)(void *, const GUID *, void *, void **);
  using SceneMethod = HRESULT(WINAPI *)(void *);

  template <class MethodType> static MethodType Method(void *object, DWORD slot)
  {
    return reinterpret_cast<MethodType>((*reinterpret_cast<DWORD **>(object))[slot]);
  }

  struct OwnedInterface
  {
    void *value = nullptr;

    ~OwnedInterface()
    {
      if (value)
      {
        Method<Release>(value, 2)(value);
      }
    }

    void *Detach()
    {
      void *result = value;
      value = nullptr;
      return result;
    }
  };

  static HRESULT Failed(HRESULT result)
  {
    ++bridgeState.failures;
    bridgeState.lastResult = static_cast<DWORD>(result);
    return result;
  }

  static HRESULT Missing(HRESULT result)
  {
    return result == S_OK ? E_UNEXPECTED : result;
  }

  static HRESULT Scene(void *device, bool ending)
  {
    // The existing counters increment before the COM call returns. Keep its
    // entire scope, including interface release callbacks, distinguishable.
    struct SceneScope
    {
      bool counted = bridgeState.sceneDepth != 0xffffffffu;

      SceneScope()
      {
        if (counted)
        {
          ++bridgeState.sceneDepth;
        }
      }

      ~SceneScope()
      {
        if (counted && bridgeState.sceneDepth != 0xffffffffu)
        {
          --bridgeState.sceneDepth;
        }
      }
    } sceneScope;

    if (ending)
    {
      ++bridgeState.endCalls;
    }
    else
    {
      ++bridgeState.beginCalls;
    }

    bridgeState.lastStage = arguments;
    if (!Configured() || !device)
    {
      return Failed(E_INVALIDARG);
    }

    ErrorScope error;
    OwnedInterface device2;
    bridgeState.lastStage = sceneQuery;
    HRESULT result = Method<Query>(device, 0)(device, &device2Id, &device2.value);

    if (result != S_OK || !device2.value)
    {
      return Failed(Missing(result));
    }

    bridgeState.lastStage = ending ? end : begin;

    // SDK IDirect3DDevice2 vtable slots 10 and 11. The native implementation
    // reaches the same scene state as the legacy interface through its thunk.
    result = Method<SceneMethod>(device2.value, ending ? 11 : 10)(device2.value);
    bridgeState.lastResult = static_cast<DWORD>(result);

    return result == S_OK ? result : Failed(result);
  }
} // namespace D3DBridge

extern "C" HRESULT WINAPI CreateDeviceBridge(void *surface, const GUID *selected, void **output)
{
  using namespace D3DBridge;
  ++bridgeState.createAttempts;
  bridgeState.lastStage = arguments;

  if (output)
  {
    *output = nullptr;
  }

  if (!Configured() || !surface || !selected || !output)
  {
    return Failed(E_INVALIDARG);
  }

  ErrorScope error;
  void *root = reinterpret_cast<void *>(Read(GameRva::d3dRoot));
  if (!root)
  {
    return Failed(E_UNEXPECTED);
  }

  OwnedInterface root2, device2, legacy;
  bridgeState.lastStage = rootQuery;
  HRESULT result = Method<Query>(root, 0)(root, &root2Id, &root2.value);

  if (result != S_OK || !root2.value)
  {
    return Failed(Missing(result));
  }

  bridgeState.lastStage = creation;

  // IDirect3D2::CreateDevice is slot 8. Keep the game's selected device GUID
  // and backbuffer, then return the legacy interface it expects.
  result = Method<Create>(root2.value, 8)(root2.value, selected, surface, &device2.value);

  if (result != S_OK || !device2.value)
  {
    return Failed(Missing(result));
  }

  bridgeState.lastStage = legacyQuery;
  result = Method<Query>(device2.value, 0)(device2.value, &device1Id, &legacy.value);
  if (result != S_OK || !legacy.value)
  {
    return Failed(Missing(result));
  }

  *output = legacy.Detach();
  ++bridgeState.createSuccesses;
  bridgeState.lastStage = created;
  bridgeState.lastResult = S_OK;

  return S_OK;
}

extern "C" HRESULT WINAPI BeginSceneBridge(void *legacyDevice)
{
  return D3DBridge::Scene(legacyDevice, false);
}

extern "C" HRESULT WINAPI EndSceneBridge(void *legacyDevice)
{
  return D3DBridge::Scene(legacyDevice, true);
}

enum
{
  deviceIidRva = GameRva::deviceIid,
  deviceResumeRva = GameRva::deviceCreateResume
};

extern "C" __declspec(naked) void CreateDeviceHook()
{
  __asm {
    // Original code already pushed &gameDevice. EAX holds the backbuffer.
        mov edx, dword ptr [config + 8]
        add edx, deviceIidRva
        push edx
        push eax
        call CreateDeviceBridge
        // Continue at the original HRESULT test, after the replaced QI call.
        mov edx, dword ptr [config + 8]
        add edx, deviceResumeRva
        jmp edx
  }
}

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_D3D_BRIDGE_HPP
