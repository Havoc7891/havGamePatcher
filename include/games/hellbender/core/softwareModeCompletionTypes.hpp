// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_SOFTWARE_MODE_COMPLETION_TYPES_HPP
#define HAVGAMEPATCHER_SOFTWARE_MODE_COMPLETION_TYPES_HPP

struct SoftwareCompletionOwner
{
  DWORD base, app, frameObject, frame, viewObject, view, thread, ownerGeneration, transitionSerial;
  DWORD mode, lifecycle, frameEpoch, restoreEpoch, frameSerial;
  DWORD sourceDc, bitmap, bits, renderBits, width, height, bpp;
  DWORD lowResolution, sourceWidth, sourceHeight;
};

struct SoftwareModeCompletionState
{
  DWORD incomplete;
  DWORD pending, generation, armed, completed;
  DWORD suppressed, cancelled, invalidations, nested;
  DWORD frameDepth, frameStarted, frameReady, frameSlot;
  DWORD frameGeneration, startMode, transitionTracked, startPending;
  DWORD presentDepth, paintDepth, presentGeneration, paintGeneration;
  DWORD presentCalls, paintCalls, gdiCalls, gdiFailures;
  DWORD gdiSuccess, nativeResult, nativeError, completedAt;
  DWORD clockFailures, frameWitnesses, presentWitnesses, reserved;
  SoftwareCompletionOwner owner;
};

static_assert(sizeof(SoftwareCompletionOwner) == 96, "Software completion owner ABI");
static_assert(sizeof(SoftwareModeCompletionState) == 228, "Software completion state ABI");

extern "C"
{
  void __cdecl SoftwareCompletionBeginTransition(void *app, DWORD token);
  void __cdecl SoftwareCompletionEndTransition(void *app, DWORD token);
  BOOL __cdecl SoftwareCompletionSuppress(void *app, DWORD token);
  void __cdecl SoftwareCompletionBeginFrame(const DWORD *registers);
  void __cdecl SoftwareCompletionFinishFrame(const DWORD *registers);
}

#endif // HAVGAMEPATCHER_SOFTWARE_MODE_COMPLETION_TYPES_HPP
