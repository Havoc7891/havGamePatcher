// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_PRESENTATION_INPUT_HPP
#define HAVGAMEPATCHER_PRESENTATION_INPUT_HPP

namespace PresentationInput
{
  struct ScopeState
  {
    DWORD call, presentationToken, consumedToken, outerSlot;
    DWORD nativeSlot, continuation, pumpSlot, invalidated;
  } scopeState = {};

  static bool ReadCall(DWORD address, ModeAdmission::InputCall &call)
  {
    return QuadFrameRecovery::StackRead(address, reinterpret_cast<DWORD *>(&call), sizeof(call));
  }

  static bool Current()
  {
    const auto &s = scopeState;
    ModeAdmission::InputCall call;
    DWORD pump = 0;
    if (!s.call || s.invalidated || !s.presentationToken ||
        s.presentationToken != s.consumedToken ||
        s.presentationToken != FailedPresentation::scopeToken ||
        s.presentationToken != failedPresentationState.generation ||
        s.outerSlot != FailedPresentation::outerSlot ||
        s.nativeSlot != FailedPresentation::nativeSlot ||
        s.continuation != FailedPresentation::nativeContinuation ||
        !FailedPresentation::ScopeCurrent() || !ReadCall(s.call, call) || s.call > 0xffffffbfu ||
        s.pumpSlot != s.call + sizeof(call) || s.pumpSlot >= s.nativeSlot ||
        !QuadFrameRecovery::StackRead(s.pumpSlot, &pump, 4) ||
        pump != config.gameBase + modeNativeInputPumpReturn || call.base != config.gameBase ||
        call.original != call.base + modeNativePreTranslate || !call.tracked || call.consumed ||
        modeAdmissionState.inputDepth != 1)
    {
      return false;
    }
    ModeAdmission::Owner owner(call.base, reinterpret_cast<void *>(call.app));
    return owner.Matches(call.ownerGeneration) && owner.frameObject == call.frameObject &&
           owner.frame == call.frame && owner.thread == call.thread;
  }
} // namespace PresentationInput

extern "C" BOOL __cdecl PresentationInputCurrent()
{
  return PresentationInput::Current();
}

extern "C" void __cdecl BeginPresentationInput(void *app, const MSG *message, const void *address)
{
  using namespace PresentationInput;
  auto &s = scopeState;
  if (s.call)
  {
    s.invalidated = 1;
    return;
  }
  ModeAdmission::InputCall call;
  DWORD pump = 0;
  const DWORD slot = reinterpret_cast<DWORD>(address);
  if (!Configured() || slot > 0xffffffbfu || !ReadCall(slot, call) || !call.tracked ||
      call.consumed || call.app != reinterpret_cast<DWORD>(app) || call.app > 0xffffffcfu ||
      reinterpret_cast<DWORD>(message) != call.app + 0x30 || call.base != config.gameBase ||
      call.original != call.base + modeNativePreTranslate ||
      !QuadFrameRecovery::StackRead(slot + sizeof(call), &pump, 4) ||
      pump != call.base + modeNativeInputPumpReturn ||
      slot + sizeof(call) >= FailedPresentation::nativeSlot || !FailedPresentation::scopeToken ||
      !FailedPresentation::ScopeCurrent() || !FailedPresentation::flipValid ||
      FailedPresentation::failureArmed ||
      FailedPresentation::flipToken != FailedPresentation::scopeToken ||
      FailedPresentation::flipResult != S_OK ||
      FailedPresentation::flipOrdinal != flipProbeState.calls || flipProbeState.busy ||
      failedPresentationState.busy || failedPresentationState.pending ||
      !FailedPresentation::FrameCurrent(true, true) ||
      (s.consumedToken == FailedPresentation::scopeToken &&
       s.outerSlot == FailedPresentation::outerSlot))
  {
    return;
  }
  s.call = slot;
  s.presentationToken = s.consumedToken = FailedPresentation::scopeToken;
  s.outerSlot = FailedPresentation::outerSlot;
  s.nativeSlot = FailedPresentation::nativeSlot;
  s.continuation = FailedPresentation::nativeContinuation;
  s.pumpSlot = slot + sizeof(call);
  s.invalidated = 0;
  if (!Current())
  {
    s.call = 0;
    s.invalidated = 1;
  }
}

extern "C" void __cdecl EndPresentationInput(const void *call)
{
  auto &s = PresentationInput::scopeState;
  ModeAdmission::InputCall owned;
  if (s.call == reinterpret_cast<DWORD>(call) && PresentationInput::ReadCall(s.call, owned))
  {
    // The renderer may have legitimately changed during the mode transition.
    // Retire this call on the current thread's stack without requiring its
    // previous resource set or window owner to remain current.
    s.call = 0;
  }
}

extern "C" void __cdecl FinishPresentationInput()
{
  auto &s = PresentationInput::scopeState;
  s.call = s.presentationToken = s.consumedToken = s.outerSlot = 0;
  s.nativeSlot = s.continuation = s.pumpSlot = s.invalidated = 0;
}

#endif
