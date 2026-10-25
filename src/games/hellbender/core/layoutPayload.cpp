// SPDX-License-Identifier: MIT

#include "layoutPayload.hpp"
#include "frameNativeAddresses.hpp"
#include "presentationNativeAddresses.hpp"
#include "failedPresentationTypes.hpp"

// Standalone code, transplanted by the maintained recipe generator.
// No CRT, static DLL imports, allocations, worker threads, or runtime data files.
// The shell helper resolves system COM APIs dynamically.
extern "C"
{
  PayloadConfig config = {0x48474c50, 1};
  PayloadState state = {};
}

template <class T> static T Api(DWORD rva)
{
  return *reinterpret_cast<T *>(config.gameBase + rva);
}

// The lowercase constants keep the imported API names out of the payload's IAT
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

struct ErrorScope
{
  DWORD value = Call(GetLastError, getLastError)();

  ~ErrorScope()
  {
    Call(SetLastError, setLastError)(value);
  }
};

#include "textureLifecycle.hpp"
#include "modeAdmission.hpp"
#include "textureRecovery.hpp"
#include "emptyCacheDeferral.hpp"
#include "textureLoadRecovery.hpp"
#include "textureTransfer.hpp"
#include "d3dBridge.hpp"

static bool Equal(const RECT &a, const RECT &b)
{
  return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

static bool Contains(const RECT &a, const RECT &b)
{
  return a.left <= b.left && a.top <= b.top && a.right >= b.right && a.bottom >= b.bottom;
}

static bool Outer(HWND w, RECT &r)
{
  return Call(GetWindowRect, getWindowRect)(w, &r) != FALSE;
}

static bool Client(HWND w, RECT &r)
{
  if (!Call(GetClientRect, getClientRect)(w, &r))
  {
    return false;
  }

  POINT tl = {r.left, r.top}, br = {r.right, r.bottom};
  if (!Call(ClientToScreen, clientToScreen)(w, &tl) ||
      !Call(ClientToScreen, clientToScreen)(w, &br))
  {
    return false;
  }

  r.left = tl.x;
  r.top = tl.y;
  r.right = br.x;
  r.bottom = br.y;

  return true;
}

static HWND FrameWindow()
{
  DWORD app = Read(GameRva::app);
  if (!app)
  {
    return nullptr;
  }

  DWORD frame = *reinterpret_cast<DWORD *>(app + 0x1c);
  return frame ? *reinterpret_cast<HWND *>(frame + 0x1c) : nullptr;
}

static HWND ViewWindow()
{
  DWORD view = Read(GameRva::view);
  return view ? *reinterpret_cast<HWND *>(view + 0x1c) : nullptr;
}

static bool Owned(HWND frame, HWND view)
{
  return frame && view && Call(IsWindow, isWindow)(frame) && Call(IsWindow, isWindow)(view) &&
         Call(IsChild, isChild)(frame, view) &&
         Call(GetWindowThreadProcessId, getWindowThreadProcessId)(frame, nullptr) ==
             Call(GetCurrentThreadId, getCurrentThreadId)() &&
         Call(GetWindowThreadProcessId, getWindowThreadProcessId)(view, nullptr) ==
             Call(GetCurrentThreadId, getCurrentThreadId)();
}

#include "focusRecovery.hpp"
#include "menuShell.hpp"
#include "displayReadiness.hpp"
#include "movieRecovery.hpp"
#include "preMovieRecovery.hpp"
#include "flipProbe.hpp"
#include "readinessNativeAddresses.hpp"
#include "surfaceRestoreOwnership.hpp"
#include "normalFrameOwnership.hpp"
#include "worldRelockTypes.hpp"
#include "quadFrameRecovery.hpp"
#include "worldRelockRecovery.hpp"
#include "frameFailureHooks.hpp"
#include "flushOwnership.hpp"
#include "lostSurfaceReadiness.hpp"
#include "hardwareModeCompletion.hpp"
#include "softwareModeCompletion.hpp"
#include "menuModeCompletion.hpp"

static constexpr DWORD softwarePresentation = 1, hardwarePresentation = 2;

static DWORD PresentationKind()
{
  if (Read(GameRva::legacyVideo) || Call(GetSystemMetrics, getSystemMetrics)(SM_CXSCREEN) != 640 ||
      Call(GetSystemMetrics, getSystemMetrics)(SM_CYSCREEN) != 480)
  {
    return 0;
  }

  const DWORD mode = Read(GameRva::mode);
  if (mode == 1 && !Read(GameRva::directDraw) && !Read(GameRva::hardware) &&
      !Read(GameRva::fullscreenActive))
  {
    return softwarePresentation;
  }

  if (mode == 2 && Read(GameRva::directDraw) && Read(GameRva::hardware) &&
      Read(GameRva::fullscreenActive))
  {
    return hardwarePresentation;
  }

  return 0;
}

static bool PresentationEligible()
{
  return PresentationKind() != 0;
}

static bool Eligible()
{
  if (state.cancelled || state.mainMenuDepth || state.transitionDepth || Read(GameRva::quit) ||
      !PresentationEligible())
  {
    return false;
  }

  if (state.videoDepth && state.videoReady &&
      reinterpret_cast<HWND>(Read(GameRva::movieWindow)) == ViewWindow())
  {
    return true;
  }

  // Source buffers legitimately change size during loading and scripted scenes.
  // The destination stays full size until the actual main screen is rebuilt.
  return state.contentActive && !Read(GameRva::mainMenuVisible);
}

static bool SameWindows()
{
  return FrameWindow() == state.frame && ViewWindow() == state.view &&
         Owned(state.frame, state.view);
}

static bool Fits()
{
  RECT view;
  if (!Client(state.view, view) || view.left || view.top || view.right != 640 || view.bottom != 480)
  {
    return false;
  }

  HWND parent = Call(GetParent, getParent)(state.view);
  for (int level = 0; parent && level < 32; ++level)
  {
    RECT area;
    if (!Client(parent, area) || !Contains(area, view))
    {
      return false;
    }

    if (parent == state.frame)
    {
      return true;
    }

    parent = Call(GetParent, getParent)(parent);
  }

  return false;
}

static constexpr UINT positionFlags =
    SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED;

static bool Position(const RECT &r)
{
  if (state.active != softwarePresentation || PresentationKind() != softwarePresentation ||
      !Eligible() || !SameWindows() || Call(GetMenu, getMenu)(state.frame))
  {
    return false;
  }

  const LONG width = r.right - r.left, height = r.bottom - r.top;
  if (width < 1 || height < 1 || width > 4096 || height > 4096)
  {
    return false;
  }

  const BOOL ok = Call(SetWindowPos, setWindowPos)(state.frame, nullptr, r.left, r.top, width,
                                                   height, positionFlags);

  // A nested mode transition owns its resulting geometry. Do not claim it.
  if (!state.cancelled && ok && !Outer(state.frame, state.last))
  {
    return false;
  }

  return ok && Eligible() && SameWindows() && !Call(GetMenu, getMenu)(state.frame);
}

// Called only with busy held. A replacement menu or moved frame belongs to the game.
static bool Restore(bool preserveGeometry)
{
  if (!state.active)
  {
    return true;
  }

  if (FrameWindow() != state.frame || !Call(IsWindow, isWindow)(state.frame))
  {
    state.active = 0;
    return true;
  }

  if (state.active == hardwarePresentation)
  {
    // Hardware presentation owns its geometry. SetMenu redraws the frame
    // itself, without a separate resize, activation or layout request.
    HMENU current = Call(GetMenu, getMenu)(state.frame);

    if (!current && !Call(SetMenu, setMenu)(state.frame, state.menu))
    {
      return false;
    }

    state.active = 0;
    ++state.restoredCount;
    return true;
  }

  RECT before;
  if (!Outer(state.frame, before))
  {
    return false;
  }

  HMENU current = Call(GetMenu, getMenu)(state.frame);
  const bool restoreRect = !preserveGeometry && !state.cancelled && Equal(before, state.last) &&
                           (!current || current == state.menu);

  if (!current && !Call(SetMenu, setMenu)(state.frame, state.menu))
  {
    return false;
  }

  RECT after;
  if (!Outer(state.frame, after))
  {
    return false;
  }

  BOOL ok;
  if (restoreRect && !state.cancelled && Equal(after, state.last) &&
      Call(GetMenu, getMenu)(state.frame) == state.menu)
  {
    ok = Call(SetWindowPos, setWindowPos)(state.frame, nullptr, state.saved.left, state.saved.top,
                                          state.saved.right - state.saved.left,
                                          state.saved.bottom - state.saved.top, positionFlags);
  }
  else
  {
    ok = Call(SetWindowPos, setWindowPos)(state.frame, nullptr, 0, 0, 0, 0,
                                          positionFlags | SWP_NOMOVE | SWP_NOSIZE);
  }

  if (!ok)
  {
    return false;
  }

  state.active = 0;
  ++state.restoredCount;
  return true;
}

static void Fail(bool preserveGeometry)
{
  ++state.failureCount;
  state.blocked = 1;
  Restore(preserveGeometry || state.cancelled != 0);
}

static void Apply()
{
  state.frame = FrameWindow();
  state.view = ViewWindow();
  if (!Owned(state.frame, state.view))
  {
    return;
  }

  state.menu = Call(GetMenu, getMenu)(state.frame);
  if (!state.menu || !Outer(state.frame, state.saved))
  {
    return;
  }

  state.last = state.saved;
  state.active = PresentationKind();
  if (!Call(SetMenu, setMenu)(state.frame, nullptr) || !Eligible() || !SameWindows())
  {
    Fail(false);
    return;
  }

  if (state.active == hardwarePresentation)
  {
    if (PresentationKind() != hardwarePresentation || Call(GetMenu, getMenu)(state.frame))
    {
      Fail(true);
      return;
    }

    ++state.appliedCount;
    return;
  }

  if (!Position(state.last))
  {
    Fail(false);
    return;
  }

  for (int iteration = 0; iteration < 3 && !Fits(); ++iteration)
  {
    RECT view, next;
    if (!Client(state.view, view) || !Outer(state.frame, next))
    {
      Fail(false);
      return;
    }

    const LONG dx = -view.left, dy = -view.top;
    next.left += dx;
    next.right += dx + 640 - (view.right - view.left);
    next.top += dy;
    next.bottom += dy + 480 - (view.bottom - view.top);

    if (!Position(next))
    {
      Fail(false);
      return;
    }
  }

  if (!Fits())
  {
    Fail(false);
    return;
  }

  ++state.appliedCount;
}

// Synchronize only at verified UI/render boundaries on the owning thread.
// A failed attempt stays blocked until a new scene or completed mode transition.
static void Synchronize(bool newAttempt)
{
  if (state.busy)
  {
    return;
  }

  state.busy = 1;
  if (state.active)
  {
    RECT now;
    const bool originalWindows = SameWindows();
    if (!Eligible() || state.active != PresentationKind() || !originalWindows ||
        Call(GetMenu, getMenu)(state.frame) ||
        (state.active == softwarePresentation &&
         (!Outer(state.frame, now) || !Equal(now, state.last) || !Fits())))
    {
      state.blocked = 1;
      if (!Restore(!PresentationEligible() || state.active != PresentationKind() ||
                   !originalWindows))
      {
        ++state.failureCount;
      }
    }
  }
  else
  {
    if (newAttempt)
    {
      state.cancelled = 0;
      state.blocked = 0;
    }

    if (!state.blocked && Eligible())
    {
      Apply();
    }
  }

  state.busy = 0;
}

extern "C" void __cdecl EnterMission()
{
  if (!Configured())
  {
    return;
  }

  ErrorScope error;
  if (state.depth++ != 0)
  {
    return;
  }

  state.ended = 0;
  if (!state.mainMenuDepth && !state.transitionDepth && !Read(GameRva::mainMenuVisible))
  {
    state.contentActive = 1;
  }

  Synchronize(true);
}

extern "C" void __cdecl AfterFrame()
{
  if (!Configured())
  {
    return;
  }

  ErrorScope error;
  Synchronize(false);
}

extern "C" void __cdecl BeforeTeardown()
{
  if (!Configured())
  {
    return;
  }

  ErrorScope error;
  AdvanceTextureLifecycle();
  AdvanceTextureFrameEpoch();

  if (state.depth == 1)
  {
    state.ended = 1;
  }

  // Teardown can lead to results, another movie, or another mission
  Synchronize(false);
}

extern "C" void __cdecl LeaveMission()
{
  if (!Configured())
  {
    return;
  }

  ErrorScope error;
  if (!state.depth || --state.depth != 0)
  {
    return;
  }

  state.ended = 1;
  Synchronize(false);
}

extern "C" void __cdecl BeforeTransition()
{
  if (!Configured())
  {
    return;
  }

  ErrorScope error;
  AdvanceTextureLifecycle();
  AdvanceTextureFrameEpoch();

  if (!state.transitionDepth)
  {
    BeginFocusTransition();
  }

  ++state.transitionDepth;
  InvalidateShellFullscreen();
  if (state.transitionDepth == 1)
  {
    ClearShellFullscreen();
  }

  if (state.busy)
  {
    state.cancelled = 1;
    state.blocked = 1;
    return;
  }

  state.busy = 1;
  if (!Restore(false))
  {
    ++state.failureCount;
  }

  state.blocked = state.active;
  state.cancelled = 0;
  state.busy = 0;
}

extern "C" void __cdecl AfterTransition()
{
  if (!Configured())
  {
    return;
  }

  ErrorScope error;
  if (!state.transitionDepth || --state.transitionDepth != 0)
  {
    return;
  }

  Synchronize(true);
  FinishFocusTransition();
  SyncShellFullscreen();
}

extern "C" void __cdecl BeforeMainMenuShow()
{
  if (!Configured())
  {
    return;
  }

  ErrorScope error;
  AdvanceTextureFrameEpoch();
  ++state.mainMenuDepth;
  InvalidateShellFullscreen();
  state.contentActive = 0;

  if (state.busy)
  {
    state.cancelled = 1;
    state.blocked = 1;
    return;
  }

  state.busy = 1;
  if (!Restore(false))
  {
    ++state.failureCount;
  }

  state.blocked = state.active;
  state.busy = 0;
}

extern "C" void __cdecl AfterMainMenuShow()
{
  if (!Configured())
  {
    return;
  }

  ErrorScope error;
  if (!state.mainMenuDepth || --state.mainMenuDepth != 0)
  {
    return;
  }

  // A failed splash creation must still leave the application's menu usable
  if (!state.videoDepth)
  {
    state.contentActive = 0;
  }

  Synchronize(false);
  ReassertShellFullscreen();
  MenuCompletionAfterShow();
}

extern "C" void __cdecl AfterMainMenuHide()
{
  if (!Configured())
  {
    return;
  }

  ErrorScope error;

  // Mode 2 attempts also destroy the splash. They do not mean the user left
  // the main menu, especially when that display-mode attempt subsequently fails.
  if (!state.transitionDepth && !state.mainMenuDepth && !Read(GameRva::mainMenuVisible) &&
      !Read(GameRva::quit))
  {
    state.contentActive = 1;
  }

  Synchronize(true);
}

// Standalone movie replay can leave the main-screen bitmap allocated.
// Hide its menu temporarily and fit the view before Smacker caches its size.
extern "C" void __cdecl BeforeVideoOpen(HWND requestedView)
{
  if (!Configured())
  {
    return;
  }

  ErrorScope error;
  AdvanceTextureFrameEpoch();
  if (state.videoDepth++ != 0)
  {
    return;
  }

  state.videoReady = requestedView && requestedView == ViewWindow() &&
                     requestedView == reinterpret_cast<HWND>(Read(GameRva::movieWindow)) &&
                     PresentationEligible() && !Read(GameRva::quit);
  state.videoBorrowed = state.active;
  Synchronize(true);
}

extern "C" void __cdecl AfterVideoCleanup()
{
  if (!Configured())
  {
    return;
  }

  ErrorScope error;
  if (!state.videoDepth || --state.videoDepth != 0)
  {
    return;
  }

  state.videoReady = 0;
  state.videoBorrowed = 0;

  // A standalone replay returns to the main screen. Campaign movies retain
  // the hidden menu through loading, scripted scenes, and the next mission.
  Synchronize(false);
}

// Restore a lost implicit backbuffer through its primary before the original
// surface operation. Public DirectDraw methods leave the native Lock/Unlock
// result intact. Setup, transitions, cleanup and other surfaces retain their
// original path. Every attempt is bounded to one primary Restore call.
extern "C" void __cdecl BeforeSurfaceUnlock(void *surface)
{
  if (!Configured() || PostRestoreRendererBusy())
  {
    return;
  }
  if (state.restoreBusy)
  {
    SurfaceRestoreOwnership::Increment(surfaceRestoreGuardState.reentries);
    return;
  }
  if (state.transitionDepth || state.setupCleanupBusy || Read(GameRva::quit) ||
      Read(GameRva::mode) != 2 || !Read(GameRva::directDraw) || !Read(GameRva::fullscreenActive) ||
      !Read(GameRva::hardware))
  {
    return;
  }

  const DWORD primary = Read(GameRva::primary), backbuffer = Read(GameRva::backbuffer);
  if (!primary || !backbuffer || primary == backbuffer ||
      reinterpret_cast<DWORD>(surface) != backbuffer)
  {
    return;
  }

  auto primaryMethods = *reinterpret_cast<DWORD **>(primary);
  auto backbufferMethods = *reinterpret_cast<DWORD **>(backbuffer);
  if (!primaryMethods || !backbufferMethods || !primaryMethods[0x60 / 4] ||
      !primaryMethods[0x6c / 4] || !backbufferMethods[0x60 / 4])
  {
    return;
  }

  SurfaceRestoreOwnership::LastErrorScope error;
  const SurfaceRestoreOwnership::Owner owner;

  struct RecoveryScope
  {
    RecoveryScope()
    {
      state.restoreBusy = 1;
    }

    ~RecoveryScope()
    {
      state.restoreBusy = 0;
    }
  } recovery;

  const SurfaceRestoreOwnership::Methods methods(primaryMethods, backbufferMethods);
  constexpr DWORD surfaceLost = 0x887601c2;
  state.lastBackbufferIsLost = methods.backbufferLost(surface);
  if (!owner.Current(1))
  {
    return;
  }

  if (state.lastBackbufferIsLost != surfaceLost)
  {
    return;
  }

  SurfaceRestoreOwnership::RestoreAndVerify(owner, methods, [] { return true; });
}

// The caller remains the original 40B760 routine. Check for loss before the
// shim starts GPU readback, then run the original 40B7D0 helper exactly once.
// A failed shim Lock can leave private conversion state latched, so it is never
// retried here and its result is never replaced with a synthetic success.
extern "C" int __cdecl BackbufferLockHook(void *surface, void *descriptor)
{
  {
    SurfaceRestoreOwnership::LastErrorScope error;
    const DWORD receipt = LostSurfaceReadiness::BeginPreLockRestore(
        surface, descriptor, reinterpret_cast<DWORD>(_AddressOfReturnAddress()));
    BeforeSurfaceUnlock(surface);
    LostSurfaceReadiness::FinishPreLockRestore(receipt);
  }
  using Lock = int(__cdecl *)(void *, void *);
  return reinterpret_cast<Lock>(config.gameBase + GameRva::surfaceLock)(surface, descriptor);
}

// GetAttachedSurface owns a separate reference to the backbuffer. The original
// resource cleanup releases the primary/D3D resources but forgets this reference.
extern "C" void __cdecl ReleaseBackbuffer()
{
  if (!Configured())
  {
    return;
  }

  ErrorScope error;
  AdvanceTextureLifecycle();
  AdvanceTextureFrameEpoch();
  DWORD buffer = Read(GameRva::backbuffer);

  if (buffer && Read(GameRva::drawLock))
  {
    reinterpret_cast<int(__cdecl *)()>(config.unlockOriginal)();
  }

  Write(GameRva::drawLock, 0);
  Write(GameRva::drawSession, 0);
  Write(GameRva::backbuffer, 0);

  if (buffer)
  {
    using Release = ULONG(WINAPI *)(void *);
    auto methods = *reinterpret_cast<DWORD **>(buffer);
    reinterpret_cast<Release>(methods[2])(reinterpret_cast<void *>(buffer));
  }
}

extern "C" void __cdecl AfterFullscreenSetup(int result)
{
  if (!Configured() || result)
  {
    return;
  }

  ErrorScope error;
  ++state.setupFailureCount;
  if (state.setupCleanupBusy)
  {
    return;
  }

  state.setupCleanupBusy = 1;

  // Do this before COM cleanup can process window messages. Early setup
  // failures never hide the cursor. Checked startup failures undo their own
  // cursor changes, so this cleanup must not call ShowCursor again.
  Write(GameRva::software, 1);
  Write(GameRva::renderBpp, 8);
  Write(GameRva::hardware, 0);
  ReleaseBackbuffer();
  reinterpret_cast<void(__cdecl *)()>(config.releaseResourcesOriginal)();
  const DWORD draw = Read(GameRva::drawObject);

  if (draw)
  {
    auto methods = *reinterpret_cast<DWORD **>(draw);
    using RestoreMode = HRESULT(WINAPI *)(void *);
    using CooperativeLevel = HRESULT(WINAPI *)(void *, HWND, DWORD);
    reinterpret_cast<RestoreMode>(methods[0x4c / 4])(reinterpret_cast<void *>(draw));
    reinterpret_cast<CooperativeLevel>(methods[0x50 / 4])(
        reinterpret_cast<void *>(draw), reinterpret_cast<HWND>(Read(GameRva::drawWindow)), 8);
  }

  Write(GameRva::drawLock, 0);
  Write(GameRva::drawSession, 0);
  state.setupCleanupBusy = 0;
}

// Both renderers wait for the new flip chain before startup drawing. Hardware
// keeps its native drawing path. Software uses checked low-level operations so
// a failed warmup can take the native setup failure path without a fatal error.
extern "C" int __cdecl PrepareStartupFrames()
{
  if (!Configured())
  {
    return 2;
  }

  ErrorScope error;
  if (!DisplayReadiness::Wait(DisplayReadiness::Phase::startup))
  {
    return 0;
  }

  if (Read(GameRva::hardwarePreference) || Read(GameRva::hardware))
  {
    return 2;
  }

  int cursorHides = 0;
  int cursorCount;
  do
  {
    cursorCount = Call(ShowCursor, showCursor)(FALSE);
    ++cursorHides;
  } while (cursorCount > 0);

  for (int frame = 0; frame != 3; ++frame)
  {
    if (!Read(GameRva::backbuffer) || !Read(GameRva::primary))
    {
      break;
    }

    if (!reinterpret_cast<int(__cdecl *)()>(config.lockOriginal)())
    {
      break;
    }

    Write(GameRva::renderBpp, 8);
    Write(GameRva::drawSession, 1);
    reinterpret_cast<void(__cdecl *)()>(config.clearOriginal)();
    Write(GameRva::drawSession, 0);

    if (!reinterpret_cast<int(__cdecl *)()>(config.unlockOriginal)())
    {
      break;
    }

    Write(GameRva::renderBpp, 8);
    const DWORD primary = Read(GameRva::primary);
    if (!primary)
    {
      break;
    }

    auto methods = *reinterpret_cast<DWORD **>(primary);
    using Flip = HRESULT(WINAPI *)(void *, void *, DWORD);
    if (reinterpret_cast<Flip>(methods[0x2c / 4])(reinterpret_cast<void *>(primary), nullptr, 1) !=
        S_OK)
    {
      break;
    }

    if (frame == 2)
    {
      return 1;
    }
  }

  // Restore the exact display count even when several ShowCursor calls were
  // needed. The setup failure epilogue and mode recovery perform the cleanup.
  while (cursorHides-- > 0)
  {
    Call(ShowCursor, showCursor)(TRUE);
  }

  return 0;
}

// Keep actual geometry and the original instruction capacity unchanged. A real
// 4097-vertex region avoids native D3DIM's small-buffer allocation path, which
// expands a reported 65534-vertex device limit beyond its own 65535 limit.
namespace ExecutePadding
{
  constexpr DWORD vertexCount = 4097;
  constexpr DWORD vertexBytes = vertexCount * 32;
} // namespace ExecutePadding

extern "C" void __cdecl AfterRendererInit(int result)
{
  if (!result || !Configured())
  {
    return;
  }

  const DWORD offset = Read(GameRva::instructionOffset);
  const DWORD capacity = Read(GameRva::executeCapacity);
  if (offset >= ExecutePadding::vertexBytes || capacity < offset)
  {
    return;
  }

  const DWORD instructionBytes = capacity - offset;
  if (instructionBytes > 0xffffffffu - ExecutePadding::vertexBytes)
  {
    return;
  }

  Write(GameRva::instructionOffset, ExecutePadding::vertexBytes);
  Write(GameRva::executeCapacity, ExecutePadding::vertexBytes + instructionBytes);
  ++state.layoutPadCount;
}

extern "C" void __cdecl PrepareExecuteData(DWORD *data)
{
  if (!Configured() || !data || data[0] != 48 ||
      Read(GameRva::instructionOffset) < ExecutePadding::vertexBytes)
  {
    return;
  }

  state.lastActualVertexCount = data[2];
  if (data[2] < ExecutePadding::vertexCount)
  {
    data[2] = ExecutePadding::vertexCount;
    ++state.executePadCount;
  }
}

extern "C" void __cdecl PrepareStateExecuteData(DWORD *data)
{
  if (!Configured() || !data || data[0] != 48)
  {
    return;
  }

  data[2] = ExecutePadding::vertexCount;
  data[3] = ExecutePadding::vertexBytes;
}

extern "C" int __cdecl FinishOwnedFlushUnlock(int result, DWORD token)
{
  if (!result || !Configured())
  {
    return result;
  }

  // A completed primary restore can precede Windows becoming ready to accept
  // the texture uploads. Keep the existing recovery pass and its ownership
  // checks, but wait for the display first when a recovery is actually pending.
  const bool pending =
      state.restoreSuccesses && state.restoreSuccesses != textureState.completedEpoch &&
      !state.transitionDepth && !state.setupCleanupBusy && !state.restoreBusy &&
      !textureState.busy && !Read(GameRva::quit) && Read(GameRva::mode) == 2 &&
      Read(GameRva::directDraw) && Read(GameRva::fullscreenActive) && Read(GameRva::hardware);
  if ((!pending || WaitForFlushReadiness(token)) && RecoverTexturesForFlush() != 0)
  {
    return result;
  }

  using Fatal = void(__cdecl *)(const char *);
  reinterpret_cast<Fatal>(config.gameBase + GameRva::fatalError)("Cannot restore texture surfaces");
  return 0;
}

extern "C" int __cdecl FinishFlushUnlock(int result)
{
  return FinishOwnedFlushUnlock(result, 0);
}

extern "C" __declspec(naked) int __cdecl RendererInitHook()
{
  __asm {
    pushfd
    pushad
    cld
    call PrepareRenderer
    test eax, eax
    jz unavailable
    call AdvanceTextureLifecycle
    popad
    popfd
    call CallRendererInitForScope
    pushfd
    pushad
    push dword ptr [esp + 28]
    call AfterRendererInit
    lea esp, [esp + 4]
    popad
    popfd
    ret
  unavailable:
    popad
    popfd
    mov eax, 0
    ret
  }
}

extern "C" __declspec(naked) void ExecuteDataHook()
{
  __asm {
    pushfd
    pushad
    push dword ptr [esp + 40]
    call PrepareExecuteData
    lea esp, [esp + 4]
    popad
    popfd
    mov eax, dword ptr [ecx]
    call dword ptr [eax + 18h]
    jmp dword ptr [config + 88]
  }
}

extern "C" __declspec(naked) void StateInstructionsHook()
{
  __asm {
    mov ebx, dword ptr [esp + 24h]
    lea ebx, [ebx + 20020h]
    mov edx, 1
    jmp dword ptr [config + 92]
  }
}

extern "C" __declspec(naked) void StateExecuteDataHook()
{
  __asm {
    pushfd
    pushad
    push dword ptr [esp + 40]
    call PrepareStateExecuteData
    lea esp, [esp + 4]
    popad
    popfd
    mov eax, dword ptr [ecx]
    call dword ptr [eax + 18h]
    jmp dword ptr [config + 96]
  }
}

extern "C" __declspec(naked) int __cdecl FlushUnlockHook()
{
  __asm {
    // The native target and invocation token belong to this stack frame.
    push dword ptr [config+52]
    push 0
    pushfd
    pushad
    cld
    mov ebp,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    push offset nativeReturned
    push ebp
    call BeginOwnedFlush
    add esp,8
    mov [ebp+36],eax
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    call dword ptr [esp+4]
  nativeReturned:
    pushfd
    pushad
    cld
    mov ebp,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    push dword ptr [ebp+36]
    push ebp
    call AfterOwnedFlush
    add esp,8
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd

         // This is the existing post-flush work with the fresh token added.
    pushfd
    pushad
    mov ebp,esp
    cld
    push dword ptr [ebp+36]
    push dword ptr [ebp+28]
    call FinishOwnedFlushUnlock
    lea esp,[esp+8]
    mov [esp+28],eax
    popad
    popfd

    pushfd
    pushad
    cld
    mov ebp,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    push dword ptr [ebp+36]
    push ebp
    call TryAbortUnlockFrame
    add esp,8
    test eax,eax
    jz NotAborted
    mov [ebp+44],eax
  NotAborted:
    push dword ptr [ebp+36]
    call EndOwnedFlush
    add esp,4
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    lea esp,[esp+8]
    ret
  }
}

// No C++ code in naked hooks. The original cdecl/thiscall ABI remains intact.
extern "C" __declspec(naked) int __cdecl SurfaceUnlockHook(void *)
{
  __asm {
    pushfd
    pushad
    push dword ptr [esp + 40]
    call BeforeSurfaceUnlock
    lea esp, [esp + 4]
    popad
    popfd
        // Replay the five original entry bytes before resuming at 0040B865
    mov eax, dword ptr [esp + 4]
    push ebx
    jmp dword ptr [config + 80]
  }
}

// Installed only at the two clear routines' drawPolygon(vertices, 4, 0x10)
// calls. Each routine zeroes four 36-byte input vertices but never sets depth.
// The original converter divides maximum depth by each vertex's depth, giving
// 0/0 for these quads. Equal positive depths produce rhw=1 without changing
// screen coordinates, output z, colors or texture coordinates. Ordinary game
// geometry continues through the original converter without this hook.
extern "C" __declspec(naked) int __cdecl ClearPolygonHook(void *, int, DWORD)
{
  __asm {
    mov eax, dword ptr [esp + 4]
    mov dword ptr [eax + 08h], 1
    mov dword ptr [eax + 02ch], 1
    mov dword ptr [eax + 050h], 1
    mov dword ptr [eax + 074h], 1
    jmp dword ptr [config + 76]
  }
}

static __declspec(naked) void MissionOriginal()
{
  __asm {
    sub esp, missionStackBytes
    jmp dword ptr [config + 12]
  }
}

extern "C" __declspec(naked) int __cdecl MissionHook(int)
{
  __asm {
    pushfd
    pushad
    call EnterMission
    popad
    popfd
    push dword ptr [esp + 4]
    call MissionOriginal
    lea esp, [esp + 4]
    pushfd
    pushad
    call LeaveMission
    popad
    popfd
    ret
  }
}

extern "C" __declspec(naked) void __cdecl TeardownHook()
{
  __asm {
    pushfd
    pushad
    call BeforeTeardown
    popad
    popfd
    jmp dword ptr [config + 20]
  }
}

static __declspec(naked) void ModeOriginal()
{
  __asm {
    mov eax, fs:[0]
    jmp dword ptr [config + 24]
  }
}

extern "C" void __cdecl AfterModeAttempt(void *app, DWORD requestedMode, DWORD previousFailures)
{
  if (!Configured())
  {
    return;
  }

  ErrorScope error;
  if (requestedMode == 2 && state.setupFailureCount != previousFailures &&
      !state.setupCleanupBusy && !Read(GameRva::quit) && Read(GameRva::mode) != 2 &&
      !Read(GameRva::directDraw) && !Read(GameRva::fullscreenActive) &&
      reinterpret_cast<DWORD>(app) == Read(GameRva::app))
  {
    ++state.recoveryCount;

    // Hold the outer transition scope until software fullscreen has finished
    // restoring the view. Only an observed setup failure permits this retry.
    using Mode = void(__thiscall *)(void *, int);
    reinterpret_cast<Mode>(&ModeOriginal)(app, 1);
  }

  AfterTransition();
}

extern "C" __declspec(naked) void ModeHook()
{
  __asm {
    pushfd
    pushad
    call SuppressModeChange
    test eax,eax
    jnz RecoveryBusy
    popad
    popfd
    lea esp, [esp - 4]
    pushfd
    pushad
    mov ebx, esp
    sub esp, 528
    and esp, -16
    fxsave [esp]
    cld
    push dword ptr [ebx + 24]
    call BeginModeAdmission
    add esp, 4
    mov dword ptr [ebx + 36], eax
    call BeforeTransition
    fxrstor [esp]
    mov esp, ebx
    popad
    popfd
             // Locals below the original return address retain this call's evidence
    push dword ptr [state + 104]
    push ecx
    push dword ptr [esp + 16]
    call ModeOriginal
    pushfd
    pushad
    mov ebx, esp
    sub esp, 528
    and esp, -16
    fxsave [esp]
    cld
             // Saved frame: app+36, failures+40, token+44, return+48, argument+52
    push dword ptr [ebx + 40]
    push dword ptr [ebx + 52]
    push dword ptr [ebx + 36]
    call AfterModeAttempt
    lea esp, [esp + 12]
    push dword ptr [ebx + 44]
    push dword ptr [ebx + 36]
    call EndModeAdmission
    add esp, 8
    fxrstor [esp]
    mov esp, ebx
    popad
    popfd
    lea esp, [esp + 12]
    ret 4
  RecoveryBusy:
    popad
    popfd
    ret 4
  }
}

extern "C" __declspec(naked) int __cdecl FullscreenSetupHook()
{
  __asm {
    pushfd
    pushad
    cld
    call AdvanceTextureLifecycle
    popad
    popfd
    lea esp,[esp-4]
    mov dword ptr [esp],0
    push ebx
    push esi
    lea eax,[esp+8]
    push eax
    push dword ptr [esp+16]
    call CallOwnedFullscreenSetup
    lea esp,[esp+16]
    pushfd
    pushad
    test dword ptr [esp+36],1
    jz NoRootCleanup
    push eax
    call AfterFullscreenSetup
    lea esp,[esp+4]
  NoRootCleanup:
    push dword ptr [esp+36]
    call FinishFullscreenSetupScope
    lea esp,[esp+4]
    popad
    popfd
    lea esp,[esp+4]
    ret
  }
}

extern "C" __declspec(naked) void __cdecl ResourceReleaseHook()
{
  __asm {
    pushfd
    pushad
    call ReleaseBackbuffer
    popad
    popfd
    jmp dword ptr [config + 48]
  }
}

extern "C" __declspec(naked) void StartupFramesHook()
{
  __asm {
    pushfd
    pushad
    call PrepareStartupFrames
    cmp eax, 2
    je original
    test eax, eax
    jz failed
    popad
    popfd
    jmp dword ptr [config + 60]
  failed:
    popad
    popfd
    jmp dword ptr [config + 64]
  original:
    popad
    popfd
    push 0
    call esi
    test eax, eax
    jmp dword ptr [config + 56]
  }
}

static __declspec(naked) void MainMenuShowOriginal()
{
  __asm {
    mov eax, fs:[0]
    jmp dword ptr [config + 36]
  }
}

extern "C" __declspec(naked) void MainMenuShowHook()
{
  __asm {
    pushfd
    pushad
    call BeforeMainMenuShow
    popad
    popfd
    call MainMenuShowOriginal
    pushfd
    pushad
    call AfterMainMenuShow
    popad
    popfd
    ret
  }
}

static __declspec(naked) void MainMenuHideOriginal()
{
  __asm {
    push esi
    mov esi, ecx
    mov ecx, dword ptr [ecx + 0ech]
    jmp dword ptr [config + 40]
  }
}

extern "C" __declspec(naked) void MainMenuHideHook()
{
  __asm {
    call MainMenuHideOriginal
    pushfd
    pushad
    call AfterMainMenuHide
    popad
    popfd
    ret
  }
}

extern "C" __declspec(naked) int __cdecl VideoOpenHook(HWND, const char *)
{
  __asm {
    pushfd
    pushad
        // 36 bytes saved above the original return address and arguments
    push dword ptr [esp + 40]
    call BeforeVideoOpen
    lea esp, [esp + 4]
    popad
    popfd
    jmp dword ptr [config + 28]
  }
}

extern "C" __declspec(naked) void __cdecl VideoCleanupHook()
{
  __asm {
    call dword ptr [config + 32]
    pushfd
    pushad
    call AfterVideoCleanup
    popad
    popfd
    ret
  }
}

#include "failedPresentationRecovery.hpp"

#include "presentationInput.hpp"
#include "fullscreenSetup.hpp"
#include "unlockFrameRecovery.hpp"
#include "postRestoreRenderer.hpp"
