// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_FAILED_PRESENTATION_TYPES_HPP
#define HAVGAMEPATCHER_FAILED_PRESENTATION_TYPES_HPP

struct FailedPresentationState
{
  DWORD busy, pending, frameReady, generation, attempts, successes, failures, rejected;
  DWORD skipped, confirmed, lastResult, restoreResult, cacheResult, lastStage, originalResult,
      reserved;
};

static_assert(sizeof(FailedPresentationState) == 64, "Failed presentation state ABI");

extern "C"
{
  extern FailedPresentationState failedPresentationState;
  void __cdecl FailedPresentationFinishFrame();
  DWORD __cdecl BeginFailedPresentationScope(const DWORD *outerReturn);
  int __cdecl CallFailedPresentationOriginal();
  void __cdecl EndFailedPresentationScope(DWORD token);
  BOOL __cdecl FailedPresentationSuppress(void *app, DWORD token);
  void __cdecl CaptureFailedPresentationFlip(DWORD surface,
                                             DWORD target,
                                             DWORD flags,
                                             DWORD caller,
                                             DWORD depth,
                                             DWORD ordinal,
                                             const DWORD *nativeStack);
  void __cdecl CompleteFailedPresentationFlip(HRESULT result, DWORD error);
  void __cdecl RecordFailedPresentationBase(DWORD base);
}

#endif
