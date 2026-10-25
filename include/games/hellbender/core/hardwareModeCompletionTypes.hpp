// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_HARDWARE_MODE_COMPLETION_TYPES_HPP
#define HAVGAMEPATCHER_HARDWARE_MODE_COMPLETION_TYPES_HPP

struct HardwareCompletionOwner
{
  DWORD base, app, frameObject, frame, viewObject, view, thread, ownerGeneration, transitionSerial;
  DWORD lifecycle, frameEpoch, restoreEpoch, draw, primary, backbuffer, device, execute;
};

struct HardwareModeCompletionState
{
  DWORD incomplete;
  DWORD pending, frameStarted, frameReady, generation;
  DWORD armed, completed, suppressed, invalidations;
  DWORD cancelled, failedFlips, nestedFlips, clockFailures;
  DWORD frameWitnesses, flipWitnesses, startMode, transitionTracked;
  DWORD startPending, frameSerial, endCalls, bridgeFailures;
  DWORD uploadSerial, completedAt, beginCalls, reserved;
  HardwareCompletionOwner owner;
};

struct HardwareFlipCompletion
{
  DWORD valid, generation, frameSerial, uploadSerial, endCalls, bridgeFailures, probeCall,
      probeDepth, beginCalls;
  HardwareCompletionOwner owner;
};

static_assert(sizeof(HardwareCompletionOwner) == 68, "Hardware completion owner ABI");
static_assert(sizeof(HardwareModeCompletionState) == 168, "Hardware completion state ABI");
static_assert(sizeof(HardwareFlipCompletion) == 104, "Hardware completion call ABI");

extern "C"
{
  void __cdecl HardwareCompletionBeginTransition(void *app, DWORD token);
  void __cdecl HardwareCompletionEndTransition(void *app, DWORD token);
  BOOL __cdecl HardwareCompletionSuppress(void *app, DWORD token);
  void __cdecl HardwareCompletionBeginFrame();
  void __cdecl HardwareCompletionFinishFrame();
  void __cdecl CaptureHardwareFlipCompletion(HardwareFlipCompletion *call,
                                             DWORD surface,
                                             DWORD target,
                                             DWORD flags,
                                             DWORD caller,
                                             DWORD depth,
                                             DWORD ordinal);
  void __cdecl CompleteHardwareFlipCompletion(const HardwareFlipCompletion *call, HRESULT result);
}

#endif // HAVGAMEPATCHER_HARDWARE_MODE_COMPLETION_TYPES_HPP
