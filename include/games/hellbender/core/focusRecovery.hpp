// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_FOCUS_RECOVERY_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_FOCUS_RECOVERY_HPP

// Mode changes hide and show the original frame while suppressing its activation
// handler. Repair only a lost child focus inside a still-foreground game frame.
// No foreground activation, asynchronous retry, or per-frame action is involved.
extern "C"
{
  FocusState focusState = {};
}

static bool FocusPhase()
{
  return state.contentActive && Read(GameRva::gameStage) && !Read(GameRva::quit) &&
         !Read(GameRva::mainMenuVisible) && !state.mainMenuDepth && !state.videoDepth &&
         !state.setupCleanupBusy && !state.restoreBusy && !textureState.busy;
}

static void SkipFocus(FocusSkip reason)
{
  focusState.armed = 0;
  focusState.lastSkipReason = static_cast<DWORD>(reason);
  ++focusState.skips;
}

static bool FocusWindowsAvailable(HWND frame, HWND view)
{
  return Call(IsWindowVisible, isWindowVisible)(frame) &&
         Call(IsWindowVisible, isWindowVisible)(view) &&
         Call(IsWindowEnabled, isWindowEnabled)(frame) &&
         Call(IsWindowEnabled, isWindowEnabled)(view) && !Call(IsIconic, isIconic)(frame);
}

extern "C" void __cdecl BeginFocusTransition()
{
  if (!Configured())
  {
    return;
  }

  ErrorScope error;
  if (state.transitionDepth)
  {
    return;
  }

  if (focusState.busy || state.busy)
  {
    SkipFocus(FocusSkip::reentry);
    return;
  }

  focusState.armed = 0;
  focusState.lastSkipReason = static_cast<DWORD>(FocusSkip::none);
  ++focusState.captures;
  focusState.frame = FrameWindow();
  focusState.view = ViewWindow();
  focusState.initialMode = Read(GameRva::mode);
  focusState.beforeFocus = Call(GetFocus, getFocus)();
  focusState.beforeForeground = Call(GetForegroundWindow, getForegroundWindow)();
  focusState.afterFocus = nullptr;
  focusState.afterForeground = nullptr;

  if (!FocusPhase() || focusState.initialMode > 2)
  {
    SkipFocus(FocusSkip::phase);
    return;
  }

  if (!Owned(focusState.frame, focusState.view))
  {
    SkipFocus(FocusSkip::windows);
    return;
  }

  if (!FocusWindowsAvailable(focusState.frame, focusState.view))
  {
    SkipFocus(FocusSkip::unavailable);
    return;
  }

  if (focusState.beforeForeground != focusState.frame)
  {
    SkipFocus(FocusSkip::foreground);
    return;
  }

  if (focusState.beforeFocus != focusState.view)
  {
    SkipFocus(FocusSkip::initialFocus);
    return;
  }

  focusState.armed = 1;
}

extern "C" void __cdecl FinishFocusTransition()
{
  if (!Configured())
  {
    return;
  }

  ErrorScope error;
  if (state.transitionDepth || state.busy)
  {
    return;
  }

  if (!focusState.armed)
  {
    return;
  }

  // Consume the one-shot before any API can deliver a nested window message
  focusState.armed = 0;
  if (focusState.busy)
  {
    SkipFocus(FocusSkip::reentry);
    return;
  }

  if (!FocusPhase())
  {
    SkipFocus(FocusSkip::phase);
    return;
  }

  const DWORD mode = Read(GameRva::mode);
  if (mode > 2)
  {
    SkipFocus(FocusSkip::phase);
    return;
  }

  if (mode == focusState.initialMode)
  {
    SkipFocus(FocusSkip::unchangedMode);
    return;
  }

  const HWND frame = focusState.frame, view = focusState.view;
  if (FrameWindow() != frame || ViewWindow() != view || !Owned(frame, view))
  {
    SkipFocus(FocusSkip::windows);
    return;
  }

  if (!FocusWindowsAvailable(frame, view))
  {
    SkipFocus(FocusSkip::unavailable);
    return;
  }

  focusState.afterForeground = Call(GetForegroundWindow, getForegroundWindow)();
  focusState.afterFocus = Call(GetFocus, getFocus)();
  if (focusState.afterForeground != frame)
  {
    SkipFocus(FocusSkip::foreground);
    return;
  }

  if (focusState.afterFocus && focusState.afterFocus != frame)
  {
    SkipFocus(FocusSkip::currentFocus);
    return;
  }

  focusState.busy = 1;
  ++focusState.attempts;
  Call(SetFocus, setFocus)(view);
  focusState.afterFocus = Call(GetFocus, getFocus)();
  focusState.afterForeground = Call(GetForegroundWindow, getForegroundWindow)();

  if (FrameWindow() != frame || ViewWindow() != view || Read(GameRva::mode) != mode ||
      !FocusPhase() || state.transitionDepth)
  {
    ++focusState.failures;
    focusState.lastSkipReason = static_cast<DWORD>(FocusSkip::stateChanged);
  }
  else if (focusState.afterFocus == view && focusState.afterForeground == frame)
  {
    ++focusState.successes;
    focusState.lastSkipReason = static_cast<DWORD>(FocusSkip::none);
  }
  else
  {
    ++focusState.failures;
    focusState.lastSkipReason = static_cast<DWORD>(FocusSkip::focusFailed);
  }

  focusState.busy = 0;
}

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_FOCUS_RECOVERY_HPP
