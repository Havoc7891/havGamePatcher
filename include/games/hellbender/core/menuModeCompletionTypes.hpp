// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MENU_MODE_COMPLETION_TYPES_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MENU_MODE_COMPLETION_TYPES_HPP

struct MenuCompletionOwner
{
  DWORD base, app, frameObject, frame, viewObject, view, document, thread;
  DWORD ownerGeneration, transitionSerial, mode, lifecycle, frameEpoch;
  DWORD bitmapObject, bitmapHandle, bitmapBits, bitmapWidth, bitmapHeight;
  RECT client;
};

struct MenuModeCompletionState
{
  DWORD incomplete;
  DWORD pending, generation, armed, completed;
  DWORD suppressed, cancelled, invalidations, reentries;
  DWORD transitionTracked, startMode, startPending, paintDepth;
  DWORD paintGeneration, bitmapDepth, bitmapGeneration, paintCalls;
  DWORD bitmapCalls, gdiCalls, gdiFailures, gdiSuccess;
  DWORD ready, readyPaintGeneration, nativeResult, nativeError;
  DWORD attempts, retryPosted, retryPostResult, presentRequests;
  DWORD renewed, requestBusy, queueDepth, deferredResume;
  DWORD deferredResumeOwner, deferredResumeDocument, deferredResumeCount, resumedCount;
  DWORD cancelledResume, quitCancelled, completedAt, clockFailures;
  DWORD noWitness, launchDeferrals, preservedNativeQueue, rearmPending;
  MenuCompletionOwner owner;
};

static_assert(sizeof(MenuCompletionOwner) == 88, "Menu completion owner ABI");
static_assert(sizeof(MenuModeCompletionState) == 268, "Menu completion state ABI");

extern "C"
{
  void __cdecl MenuCompletionBeginTransition(void *app, DWORD token);
  void __cdecl MenuCompletionEndTransition(void *app, DWORD token);
  BOOL __cdecl MenuCompletionSuppress(void *app, DWORD token, BOOL fresh);
  void __cdecl MenuCompletionAfterShow();
}

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MENU_MODE_COMPLETION_TYPES_HPP
