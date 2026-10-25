// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MENU_MODE_COMPLETION_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MENU_MODE_COMPLETION_HPP

#include "menuModeCompletionTypes.hpp"

#include "menuNativeAddresses.hpp"

extern "C"
{
  MenuModeCompletionState menuModeCompletionState = {};
}

namespace MenuModeCompletion
{
  constexpr DWORD viewVtable = menuNativeViewVtable, bitmapVtable = menuNativeBitmapVtable;
  constexpr DWORD bitmapEntry = menuNativeBitmapEntry, bitmapCaller = menuNativeBitmapCaller;
  constexpr DWORD stretchImport = menuNativeStretchImport, stretchCaller = menuNativeStretchCaller;
  constexpr DWORD queueEntry = menuNativeQueueEntry, queueCaller = menuNativeQueueCaller,
                  resumeEntry = menuNativeResumeEntry;
  constexpr DWORD invalidateImport = menuNativeInvalidateImport;
  using Preservation = HardwareModeCompletion::Preservation;

  static void Increment(DWORD &value)
  {
    if (value == 0xffffffffu)
    {
      menuModeCompletionState.incomplete = 1;
    }
    else
    {
      ++value;
    }
  }

  static bool Phase()
  {
    return Configured() && !state.depth && !state.videoDepth && !state.mainMenuDepth &&
           !Read(GameRva::quit) && !Read(GameRva::gameStage) &&
           Read(GameRva::mainMenuVisible) == 1 && Read(GameRva::mode) <= 1 &&
           !Read(GameRva::directDraw) && !Read(GameRva::fullscreenActive);
  }

  static bool Quiet()
  {
    return !modeAdmissionState.transitionDepth && !state.transitionDepth && !state.busy &&
           !state.setupCleanupBusy && !state.restoreBusy && !textureState.busy && !loadState.busy &&
           !readinessState.busy && !movieRecoveryState.busy && !preMovieRecoveryState.busy &&
           !preMovieRecoveryState.scopeDepth && !TextureTransfer::textureTransferStats.busy &&
           !TextureLifecycle::Incomplete();
  }

  static bool SameRect(const RECT &a, const RECT &b)
  {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
  }

  static bool UiOwnerCurrent(const MenuCompletionOwner &o)
  {
    if (!Configured() || config.gameBase != o.base || TextureLifecycle::ThreadId() != o.thread ||
        !o.app || !o.frameObject || !o.frame || !o.viewObject || !o.view ||
        Read(GameRva::app) != o.app || Read(GameRva::view) != o.viewObject ||
        modeAdmissionState.ownerGeneration != o.ownerGeneration ||
        modeAdmissionState.imageBase != o.base || modeAdmissionState.thread != o.thread ||
        modeAdmissionState.app != o.app || modeAdmissionState.frameObject != o.frameObject ||
        modeAdmissionState.frame != o.frame)
    {
      return false;
    }
    return *reinterpret_cast<volatile DWORD *>(o.app + 0x1c) == o.frameObject &&
           *reinterpret_cast<volatile DWORD *>(o.frameObject + 0x1c) == o.frame &&
           *reinterpret_cast<volatile DWORD *>(o.viewObject + 0x1c) == o.view &&
           *reinterpret_cast<volatile DWORD *>(o.viewObject) == o.base + viewVtable;
  }

  static bool OwnerCurrent(const MenuCompletionOwner &o)
  {
    return UiOwnerCurrent(o) && o.document && o.bitmapObject && o.bitmapHandle && o.bitmapBits &&
           modeAdmissionState.transitionSerial == o.transitionSerial &&
           *reinterpret_cast<volatile DWORD *>(o.app + 0xf4) == o.document &&
           *reinterpret_cast<volatile DWORD *>(o.app + 0xec) == o.bitmapObject &&
           *reinterpret_cast<volatile DWORD *>(o.bitmapObject) == o.base + bitmapVtable &&
           *reinterpret_cast<volatile DWORD *>(o.bitmapObject + 0x60) == o.bitmapHandle &&
           *reinterpret_cast<volatile DWORD *>(o.bitmapObject + 0x64) == o.bitmapBits &&
           *reinterpret_cast<volatile DWORD *>(o.bitmapObject + 8) == o.bitmapWidth &&
           *reinterpret_cast<volatile DWORD *>(o.bitmapObject + 12) == o.bitmapHeight;
  }

  static bool Current()
  {
    const auto &s = menuModeCompletionState;
    const auto &o = s.owner;
    return s.pending && !s.rearmPending && !s.incomplete && OwnerCurrent(o) && Phase() && Quiet() &&
           Read(GameRva::mode) == o.mode && TextureLifecycle::Serial() == o.lifecycle &&
           TextureLifecycle::FrameEpoch() == o.frameEpoch;
  }

  static bool WindowCurrent()
  {
    auto &s = menuModeCompletionState;
    if (!Current())
    {
      return false;
    }
    const auto o = s.owner;
    ModeAdmission::Owner nativeOwner(o.base, reinterpret_cast<void *>(o.app));
    RECT client = {};
    const auto visible = Api<decltype(&::IsWindowVisible)>(GameRva::isWindowVisible);
    const auto iconic = Api<decltype(&::IsIconic)>(GameRva::isIconic);
    const auto getClient = Api<decltype(&::GetClientRect)>(GameRva::getClientRect);
    return nativeOwner.Windows(reinterpret_cast<HWND>(o.view)) && Current() && visible &&
           visible(reinterpret_cast<HWND>(o.view)) && Current() && iconic &&
           !iconic(reinterpret_cast<HWND>(o.frame)) && Current() && getClient &&
           getClient(reinterpret_cast<HWND>(o.view), &client) && Current() &&
           SameRect(client, o.client);
  }

  static void Invalidate()
  {
    auto &s = menuModeCompletionState;
    if (s.ready || s.gdiSuccess)
    {
      Increment(s.invalidations);
    }
    s.ready = s.gdiSuccess = 0;
  }

  static void CancelResume()
  {
    auto &s = menuModeCompletionState;
    if (s.deferredResume)
    {
      Increment(s.cancelledResume);
    }
    s.deferredResume = s.deferredResumeOwner = s.deferredResumeDocument = 0;
  }

  static void Cancel()
  {
    auto &s = menuModeCompletionState;
    if (s.pending)
    {
      Increment(s.cancelled);
    }
    s.pending = s.rearmPending = 0;
    Invalidate();
  }

  static bool Capture(MenuCompletionOwner &o)
  {
    if (!Phase() || !Quiet())
    {
      return false;
    }
    auto words = reinterpret_cast<volatile DWORD *>(&o);
    for (DWORD index = 0; index != sizeof(o) / sizeof(DWORD); ++index)
    {
      words[index] = 0;
    }
    o.base = config.gameBase;
    o.app = modeAdmissionState.app;
    o.frameObject = modeAdmissionState.frameObject;
    o.frame = modeAdmissionState.frame;
    o.viewObject = Read(GameRva::view);
    if (!o.app || !o.viewObject)
    {
      return false;
    }
    o.view = *reinterpret_cast<volatile DWORD *>(o.viewObject + 0x1c);
    o.document = *reinterpret_cast<volatile DWORD *>(o.app + 0xf4);
    o.thread = TextureLifecycle::ThreadId();
    o.ownerGeneration = modeAdmissionState.ownerGeneration;
    o.transitionSerial = modeAdmissionState.transitionSerial;
    o.mode = Read(GameRva::mode);
    o.lifecycle = TextureLifecycle::Serial();
    o.frameEpoch = TextureLifecycle::FrameEpoch();
    o.bitmapObject = *reinterpret_cast<volatile DWORD *>(o.app + 0xec);
    if (!o.bitmapObject)
    {
      return false;
    }
    o.bitmapHandle = *reinterpret_cast<volatile DWORD *>(o.bitmapObject + 0x60);
    o.bitmapBits = *reinterpret_cast<volatile DWORD *>(o.bitmapObject + 0x64);
    o.bitmapWidth = *reinterpret_cast<volatile DWORD *>(o.bitmapObject + 8);
    o.bitmapHeight = *reinterpret_cast<volatile DWORD *>(o.bitmapObject + 12);
    const auto getClient = Api<decltype(&::GetClientRect)>(GameRva::getClientRect);
    ModeAdmission::Owner nativeOwner(o.base, reinterpret_cast<void *>(o.app));
    return OwnerCurrent(o) && nativeOwner.Windows(reinterpret_cast<HWND>(o.view)) &&
           OwnerCurrent(o) && getClient && getClient(reinterpret_cast<HWND>(o.view), &o.client) &&
           OwnerCurrent(o) && Phase() && Quiet() && Read(GameRva::mode) == o.mode &&
           TextureLifecycle::Serial() == o.lifecycle &&
           TextureLifecycle::FrameEpoch() == o.frameEpoch && !o.client.left && !o.client.top &&
           o.client.right > 0 && o.client.bottom > 0 && o.bitmapWidth && o.bitmapWidth <= 8192 &&
           o.bitmapHeight && o.bitmapHeight <= 8192;
  }

  static void Arm()
  {
    auto &s = menuModeCompletionState;
    if (s.paintDepth || s.bitmapDepth || s.requestBusy)
    {

      Invalidate();
      Increment(s.reentries);
      return;
    }
    MenuCompletionOwner owner;
    if (!Capture(owner))
    {

      Invalidate();
      if (s.pending && UiOwnerCurrent(s.owner) && Phase())
      {
        // Resource absence is not permission to release the native launch.
        // Only a later owned queue checkpoint may capture replacement pins.
        s.rearmPending = 1;
      }
      else
      {
        Cancel();
        CancelResume();
      }
      return;
    }

    s.owner = owner;
    Increment(s.generation);
    Increment(s.armed);
    s.pending = 1;
    s.rearmPending = 0;
    s.attempts = s.retryPosted = s.retryPostResult = s.completedAt = 0;
    Invalidate();
  }

  static void Renew()
  {
    auto &s = menuModeCompletionState;
    if (Current() && !s.requestBusy && !s.paintDepth && !s.bitmapDepth && !s.ready)
    {

      s.attempts = s.retryPosted = s.retryPostResult = 0;
      Increment(s.renewed);
    }
  }

  static void RefreshOwner()
  {
    auto &s = menuModeCompletionState;
    if (!s.pending || !UiOwnerCurrent(s.owner) || !Phase() || !Quiet() || s.requestBusy ||
        s.paintDepth || s.bitmapDepth)
    {
      return;
    }
    if (s.deferredResume &&
        s.deferredResumeDocument != *reinterpret_cast<volatile DWORD *>(s.owner.app + 0xf4))
    {

      CancelResume();
    }
    if (s.rearmPending || !OwnerCurrent(s.owner) || Read(GameRva::mode) != s.owner.mode ||
        TextureLifecycle::Serial() != s.owner.lifecycle ||
        TextureLifecycle::FrameEpoch() != s.owner.frameEpoch)
    {
      Arm();
      return;
    }
    const DWORD generation = s.generation;
    const auto getClient = Api<decltype(&::GetClientRect)>(GameRva::getClientRect);
    RECT client = {};
    const BOOL captured = getClient && getClient(reinterpret_cast<HWND>(s.owner.view), &client);
    if (s.generation != generation)
    {
      return;
    }
    if (!captured || !Current())
    {

      Invalidate();
      s.rearmPending = 1;
      return;
    }
    if (!SameRect(client, s.owner.client))
    {
      // An actual user/native resize creates a new presentation obligation.
      // It is not a retry based on elapsed time or the old failed BOOL.
      Arm();
    }
  }

  struct ScopeCall
  {
    DWORD nativeEntry, tracked, kind, generation, scopeGeneration, paintGeneration, calls, failures;
    DWORD object, dc, destination, bitmap;
    RECT area;
  };

  static_assert(sizeof(ScopeCall) == 64, "Menu native scope stack reservation");

  static DWORD paintDc = 0, destinationDc = 0;

  static void BeginDraw(void *view, void *dc, ScopeCall &call)
  {
    Preservation preserve;
    call.nativeEntry = reinterpret_cast<DWORD>(&SoftwareDrawHook);
    call.tracked = 0;
    auto &s = menuModeCompletionState;
    if (!s.pending || !Configured() || TextureLifecycle::ThreadId() != s.owner.thread)
    {
      return;
    }

    if (reinterpret_cast<DWORD>(view) != s.owner.viewObject || !OwnerCurrent(s.owner))
    {
      if (s.paintDepth)
      {
        Invalidate();
      }
      return;
    }
    call.tracked = 1;
    call.kind = 0;
    call.generation = s.generation;
    call.object = reinterpret_cast<DWORD>(view);
    call.dc = reinterpret_cast<DWORD>(dc);
    call.destination = dc ? reinterpret_cast<volatile DWORD *>(dc)[1] : 0;
    call.calls = s.gdiCalls;
    call.failures = s.gdiFailures;
    call.bitmap = 0;
    Increment(s.paintDepth);
    Increment(s.paintGeneration);
    Increment(s.paintCalls);
    call.scopeGeneration = s.paintGeneration;
    call.paintGeneration = s.paintGeneration;
    Invalidate();
    if (s.paintDepth != 1 || s.bitmapDepth || !call.destination || !Current())
    {
      Increment(s.reentries);
      return;
    }
    paintDc = call.dc;
    destinationDc = call.destination;
    call.bitmap = s.owner.bitmapObject;
  }

  static void BeginBitmap(
      void *bitmap, void *dc, const RECT *area, DWORD operation, DWORD caller, ScopeCall &call)
  {
    Preservation preserve;
    call.nativeEntry = config.gameBase + bitmapEntry;
    call.tracked = 0;
    auto &s = menuModeCompletionState;
    if (!s.pending || !Configured() || TextureLifecycle::ThreadId() != s.owner.thread ||
        !s.paintDepth)
    {
      return;
    }

    call.tracked = 1;
    call.kind = 1;
    call.generation = s.generation;
    call.paintGeneration = s.paintGeneration;
    call.calls = s.gdiCalls;
    call.failures = s.gdiFailures;
    call.object = reinterpret_cast<DWORD>(bitmap);
    call.dc = reinterpret_cast<DWORD>(dc);
    call.destination = dc ? reinterpret_cast<volatile DWORD *>(dc)[1] : 0;
    call.bitmap = s.owner.bitmapObject;
    call.area = area ? *area : RECT{};
    Increment(s.bitmapDepth);
    Increment(s.bitmapGeneration);
    Increment(s.bitmapCalls);
    call.scopeGeneration = s.bitmapGeneration;
    if (s.paintDepth != 1 || s.bitmapDepth != 1 || caller != s.owner.base + bitmapCaller ||
        call.object != s.owner.bitmapObject || call.dc != paintDc ||
        call.destination != destinationDc || !area || !SameRect(call.area, s.owner.client) ||
        operation != SRCCOPY || !Current())
    {
      Increment(s.reentries);
      Invalidate();
      call.bitmap = 0;
    }
  }

  static bool SameScope(const ScopeCall &call)
  {
    const auto &s = menuModeCompletionState;
    return call.generation == s.generation && call.paintGeneration == s.paintGeneration &&
           call.calls != 0xffffffffu && s.gdiCalls == call.calls + 1 &&
           s.gdiFailures == call.failures && s.gdiSuccess && Current() && call.dc == paintDc &&
           call.destination == destinationDc &&
           *reinterpret_cast<volatile DWORD *>(call.dc + 4) == call.destination;
  }

  static void EndScope(const ScopeCall &call)
  {
    Preservation preserve;
    auto &s = menuModeCompletionState;
    if (!call.tracked || TextureLifecycle::ThreadId() != s.owner.thread)
    {
      return;
    }

    if (call.kind)
    {
      const bool valid = s.paintDepth == 1 && s.bitmapDepth == 1 && call.bitmap &&
                         call.scopeGeneration == s.bitmapGeneration && SameScope(call);
      if (s.bitmapDepth)
      {
        --s.bitmapDepth;
      }
      if (!valid)
      {
        Invalidate();
      }
      return;
    }
    const bool valid = s.paintDepth == 1 && !s.bitmapDepth && call.bitmap == s.owner.bitmapObject &&
                       call.bitmap && call.scopeGeneration == s.paintGeneration && SameScope(call);
    if (s.paintDepth)
    {
      --s.paintDepth;
    }
    if (!valid)
    {
      Invalidate();
      return;
    }
    s.ready = 1;
    s.readyPaintGeneration = s.paintGeneration;
  }

  struct GdiCall
  {
    DWORD valid, generation, paint, bitmap, calls, failures, dc, destination, source;
  };

  static GdiCall BeforeGdi(DWORD caller,
                           HDC destination,
                           int x,
                           int y,
                           int width,
                           int height,
                           HDC source,
                           int sourceX,
                           int sourceY,
                           int sourceWidth,
                           int sourceHeight,
                           DWORD operation)
  {
    Preservation preserve;
    GdiCall call = {};
    auto &s = menuModeCompletionState;
    if (!s.pending || !Configured() || TextureLifecycle::ThreadId() != s.owner.thread ||
        !s.paintDepth || !s.bitmapDepth)
    {
      return call;
    }

    Increment(s.gdiCalls);
    if (s.paintDepth != 1 || s.bitmapDepth != 1 || !Current() ||
        caller != s.owner.base + stretchCaller || !paintDc || !source ||
        reinterpret_cast<DWORD>(destination) != destinationDc ||
        *reinterpret_cast<volatile DWORD *>(paintDc + 4) != destinationDc ||
        x != s.owner.client.left || y != s.owner.client.top ||
        width != s.owner.client.right - s.owner.client.left ||
        height != s.owner.client.bottom - s.owner.client.top || sourceX || sourceY ||
        sourceWidth != static_cast<int>(s.owner.bitmapWidth) ||
        sourceHeight != static_cast<int>(s.owner.bitmapHeight) || operation != SRCCOPY)
    {
      Invalidate();
      return call;
    }
    call.valid = 1;
    call.generation = s.generation;
    call.paint = s.paintGeneration;
    call.bitmap = s.bitmapGeneration;
    call.calls = s.gdiCalls;
    call.failures = s.gdiFailures;
    call.dc = paintDc;
    call.destination = destinationDc;
    call.source = reinterpret_cast<DWORD>(source);
    return call;
  }

  static void AfterGdi(const GdiCall &call, BOOL result, DWORD error)
  {
    Preservation preserve;
    auto &s = menuModeCompletionState;
    if (!call.valid || TextureLifecycle::ThreadId() != s.owner.thread)
    {
      return;
    }

    s.nativeResult = result;
    s.nativeError = error;
    if (!result)
    {
      Increment(s.gdiFailures);
    }
    if (!result || !Current() || s.paintDepth != 1 || s.bitmapDepth != 1 ||
        call.generation != s.generation || call.paint != s.paintGeneration ||
        call.bitmap != s.bitmapGeneration || call.calls != s.gdiCalls ||
        call.failures != s.gdiFailures || call.dc != paintDc || call.destination != destinationDc ||
        *reinterpret_cast<volatile DWORD *>(call.dc + 4) != call.destination)
    {
      Invalidate();
      return;
    }
    s.gdiSuccess = 1;
  }

  static bool Complete()
  {
    auto &s = menuModeCompletionState;
    if (!s.ready || s.paintDepth || s.bitmapDepth || s.requestBusy ||
        s.readyPaintGeneration != s.paintGeneration || !WindowCurrent())
    {
      return false;
    }
    const DWORD generation = s.generation, paint = s.paintGeneration;
    ModeAdmission::Owner owner(s.owner.base, reinterpret_cast<void *>(s.owner.app));
    DWORD now = 0;
    const bool clock = ModeAdmission::Tick(owner, s.owner.ownerGeneration, now);

    if (!clock || !Current() || !s.ready || s.paintDepth || s.bitmapDepth || s.requestBusy ||
        s.generation != generation || s.paintGeneration != paint)
    {
      if (!clock)
      {
        Increment(s.clockFailures);
      }
      Invalidate();
      return false;
    }
    {

      modeAdmissionState.completedAt = now;
      modeAdmissionState.clockValid = 1;
    }
    s.completedAt = now;
    s.pending = s.ready = s.gdiSuccess = 0;
    Increment(s.completed);
    return true;
  }

  static bool Launch(DWORD command)
  {
    return command == 0x8003 || command == 0x8033 || command == 0x8005 ||
           (command >= 0x2710 && command <= 0x2713);
  }

  static void RequestOnce()
  {
    auto &s = menuModeCompletionState;
    if (s.attempts || s.requestBusy || s.paintDepth || s.bitmapDepth || !WindowCurrent())
    {
      return;
    }
    const DWORD generation = s.generation, failureCount = s.gdiFailures;
    const DWORD app = s.owner.app;
    const DWORD present =
        *reinterpret_cast<volatile DWORD *>(*reinterpret_cast<volatile DWORD *>(app) + 0xc4);
    if (present != reinterpret_cast<DWORD>(&SoftwarePresentHook))
    {
      return;
    }
    {

      s.attempts = s.requestBusy = 1;
      Increment(s.presentRequests);
    }
    using Present = void(__thiscall *)(void *);
    reinterpret_cast<Present>(present)(reinterpret_cast<void *>(app));
    {

      s.requestBusy = 0;
      if (!s.ready)
      {
        Increment(s.noWitness);
      }
    }
    // The native paint has fully returned before posting a single ordinary
    // retry. Failed GDI remains a failure and cannot publish completion.
    if (s.generation == generation && Current() && !s.ready && !s.retryPosted &&
        s.gdiFailures != failureCount)
    {
      const auto invalidate = Api<decltype(&::InvalidateRect)>(invalidateImport);
      if (invalidate)
      {
        {

          s.retryPosted = 1;
        }
        const BOOL result = invalidate(reinterpret_cast<HWND>(s.owner.view), nullptr, FALSE);

        if (s.generation == generation && Current())
        {
          s.retryPostResult = result;
        }
      }
    }
  }

  struct QueueCall
  {
    DWORD nativeEntry, action, resumeEntry, document;
    DWORD ownerGeneration, thread, frameObject, frame;
  };

  static_assert(sizeof(QueueCall) == 32, "Menu queue stack reservation");

  static void PrepareQueue(void *app, DWORD caller, QueueCall &call)
  {
    Preservation preserve;
    call.nativeEntry = config.gameBase + queueEntry;
    call.action = call.document = 0;
    call.resumeEntry = config.gameBase + resumeEntry;
    call.ownerGeneration = call.thread = call.frameObject = call.frame = 0;
    auto &s = menuModeCompletionState;
    if (!Configured() || caller != config.gameBase + queueCaller ||
        reinterpret_cast<DWORD>(app) != Read(GameRva::app) ||
        TextureLifecycle::ThreadId() != modeAdmissionState.thread)
    {
      return;
    }
    ModeAdmission::Owner nativeOwner(config.gameBase, app);
    if (!nativeOwner.Current())
    {
      return;
    }
    call.ownerGeneration = modeAdmissionState.ownerGeneration;
    call.thread = nativeOwner.thread;
    call.frameObject = nativeOwner.frameObject;
    call.frame = nativeOwner.frame;
    if (s.queueDepth || s.requestBusy)
    {

      Increment(s.reentries);
      call.action =
          Launch(*reinterpret_cast<volatile DWORD *>(reinterpret_cast<DWORD>(app) + 0x128));
      return;
    }

    struct QueueScope
    {
      QueueScope()
      {

        ++menuModeCompletionState.queueDepth;
      }

      ~QueueScope()
      {

        --menuModeCompletionState.queueDepth;
      }
    } scope;

    if (Read(GameRva::quit))
    {

      Cancel();
      CancelResume();
      if (Launch(*reinterpret_cast<volatile DWORD *>(reinterpret_cast<DWORD>(app) + 0x128)))
      {
        call.action = 1;
        Increment(s.quitCancelled);
      }
      return;
    }
    if (s.pending && (!UiOwnerCurrent(s.owner) || !Phase()))
    {

      Cancel();
      CancelResume();
    }
    if (s.pending)
    {
      RefreshOwner();
      Complete();
      if (s.pending)
      {
        RequestOnce();
        Complete();
      }
    }
    if (!nativeOwner.Current() || modeAdmissionState.ownerGeneration != call.ownerGeneration)
    {

      Cancel();
      CancelResume();
      call.action = 1;
      return;
    }
    const DWORD command = *reinterpret_cast<volatile DWORD *>(reinterpret_cast<DWORD>(app) + 0x128);
    if (s.pending)
    {
      if (Launch(command))
      {

        Increment(s.launchDeferrals);
        s.preservedNativeQueue = command;
        call.action = 1;
      }
      return;
    }
    if (s.deferredResume)
    {

      if (s.deferredResumeOwner == modeAdmissionState.ownerGeneration && Phase() &&
          s.deferredResumeDocument ==
              *reinterpret_cast<volatile DWORD *>(reinterpret_cast<DWORD>(app) + 0xf4))
      {
        call.action = 2;
        call.document = s.deferredResumeDocument;
        s.deferredResume = s.deferredResumeOwner = s.deferredResumeDocument = 0;
        Increment(s.resumedCount);
      }
      else
      {
        CancelResume();
      }
    }
  }

  static void CompleteResume(void *app, QueueCall &call)
  {
    Preservation preserve;
    call.action = 1;
    if (!Configured() || !call.ownerGeneration || !call.thread ||
        TextureLifecycle::ThreadId() != call.thread ||
        modeAdmissionState.ownerGeneration != call.ownerGeneration ||
        reinterpret_cast<DWORD>(app) != Read(GameRva::app) || Read(GameRva::quit))
    {
      return;
    }
    ModeAdmission::Owner owner(config.gameBase, app);
    if (!owner.Current() || owner.frameObject != call.frameObject || owner.frame != call.frame)
    {
      return;
    }
    const DWORD command = *reinterpret_cast<volatile DWORD *>(reinterpret_cast<DWORD>(app) + 0x128);
    if (menuModeCompletionState.pending && Launch(command))
    {

      Increment(menuModeCompletionState.launchDeferrals);
      menuModeCompletionState.preservedNativeQueue = command;
      return;
    }
    call.action = 0;
  }

  static BOOL PrepareResume(void *document, DWORD &nativeEntry)
  {
    Preservation preserve;
    nativeEntry = config.gameBase + resumeEntry;
    auto &s = menuModeCompletionState;
    if (!s.pending || !UiOwnerCurrent(s.owner) || !Phase() || !document ||
        reinterpret_cast<DWORD>(document) !=
            *reinterpret_cast<volatile DWORD *>(s.owner.app + 0xf4))
    {
      return FALSE;
    }
    Renew();

    if (s.deferredResume && s.deferredResumeDocument != reinterpret_cast<DWORD>(document))
    {
      CancelResume();
    }
    s.deferredResume = 1;
    s.deferredResumeOwner = s.owner.ownerGeneration;
    s.deferredResumeDocument = reinterpret_cast<DWORD>(document);
    Increment(s.deferredResumeCount);
    return TRUE;
  }
} // namespace MenuModeCompletion

extern "C" void __cdecl MenuCompletionBeginTransition(void *app, DWORD token)
{
  using namespace MenuModeCompletion;
  Preservation preserve;
  if (!Configured() || !token || modeAdmissionState.transitionDepth != 1 ||
      modeAdmissionState.ownerGeneration != token ||
      modeAdmissionState.app != reinterpret_cast<DWORD>(app) ||
      modeAdmissionState.thread != TextureLifecycle::ThreadId())
  {
    return;
  }

  auto &s = menuModeCompletionState;
  s.startMode = Read(GameRva::mode);
  s.startPending = s.pending && s.owner.ownerGeneration == token;
  s.transitionTracked = token;
  Invalidate();
}

extern "C" void __cdecl MenuCompletionEndTransition(void *app, DWORD token)
{
  using namespace MenuModeCompletion;
  Preservation preserve;
  auto &s = menuModeCompletionState;
  if (!Configured() || !token || modeAdmissionState.transitionDepth ||
      s.transitionTracked != token || modeAdmissionState.ownerGeneration != token ||
      modeAdmissionState.app != reinterpret_cast<DWORD>(app) ||
      modeAdmissionState.thread != TextureLifecycle::ThreadId())
  {
    return;
  }
  {

    s.transitionTracked = 0;
    if (!Phase() || (s.startMode == Read(GameRva::mode) && !s.startPending))
    {
      Cancel();
      return;
    }
  }
  Arm();
}

extern "C" void __cdecl MenuCompletionAfterShow()
{
  using namespace MenuModeCompletion;
  Preservation preserve;
  if (!Phase() || !Quiet())
  {
    return;
  }
  ModeAdmission::Owner owner(config.gameBase, reinterpret_cast<void *>(Read(GameRva::app)));
  if (!owner.Windows() || !ModeAdmission::Adopt(owner))
  {
    return;
  }
  Arm();
}

extern "C" BOOL __cdecl MenuCompletionSuppress(void *app, DWORD token, BOOL fresh)
{
  using namespace MenuModeCompletion;
  Preservation preserve;
  auto &s = menuModeCompletionState;
  if (!s.pending || !Configured() || TextureLifecycle::ThreadId() != s.owner.thread)
  {
    return FALSE;
  }
  if (reinterpret_cast<DWORD>(app) != s.owner.app || token != s.owner.ownerGeneration ||
      !UiOwnerCurrent(s.owner) || !Phase())
  {

    Cancel();
    return FALSE;
  }
  if (fresh)
  {
    Renew();
  }

  Increment(s.suppressed);
  return TRUE;
}

extern "C" void __cdecl MenuCompletionLaunchIntent(void *app)
{
  using namespace MenuModeCompletion;
  Preservation preserve;
  if (menuModeCompletionState.pending && Current() &&
      reinterpret_cast<DWORD>(app) == menuModeCompletionState.owner.app)
  {
    Renew();
  }
}

extern "C" void __cdecl PrepareMenuDraw(void *view, void *dc, MenuModeCompletion::ScopeCall *call)
{
  MenuModeCompletion::BeginDraw(view, dc, *call);
}

extern "C" void __cdecl PrepareMenuBitmap(void *bitmap,
                                          void *dc,
                                          const RECT *area,
                                          DWORD operation,
                                          DWORD caller,
                                          MenuModeCompletion::ScopeCall *call)
{
  MenuModeCompletion::BeginBitmap(bitmap, dc, area, operation, caller, *call);
}

extern "C" void __cdecl CompleteMenuScope(const MenuModeCompletion::ScopeCall *call)
{
  MenuModeCompletion::EndScope(*call);
}

extern "C" void __cdecl PrepareMenuQueue(void *app,
                                         DWORD caller,
                                         MenuModeCompletion::QueueCall *call)
{
  MenuModeCompletion::PrepareQueue(app, caller, *call);
}

extern "C" BOOL __cdecl PrepareMenuResume(void *document, DWORD *nativeEntry)
{
  return MenuModeCompletion::PrepareResume(document, *nativeEntry);
}

extern "C" void __cdecl CompleteMenuResume(void *app, MenuModeCompletion::QueueCall *call)
{
  MenuModeCompletion::CompleteResume(app, *call);
}

extern "C" BOOL WINAPI ObserveMenuStretchBlt(HDC destination,
                                             int x,
                                             int y,
                                             int width,
                                             int height,
                                             HDC source,
                                             int sourceX,
                                             int sourceY,
                                             int sourceWidth,
                                             int sourceHeight,
                                             DWORD operation)
{
  using namespace MenuModeCompletion;
  const auto native = Api<decltype(&::StretchBlt)>(stretchImport);
  const auto call =
      BeforeGdi(reinterpret_cast<DWORD>(_ReturnAddress()), destination, x, y, width, height, source,
                sourceX, sourceY, sourceWidth, sourceHeight, operation);
  const BOOL result = native(destination, x, y, width, height, source, sourceX, sourceY,
                             sourceWidth, sourceHeight, operation);
  const DWORD error = __readfsdword(0x34);
  AfterGdi(call, result, error);
  return result;
}

extern "C" __declspec(naked) void MenuDrawHook()
{
  __asm {
    lea esp,[esp-64]
    pushfd
    pushad
    mov ebx,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    cld
    lea eax,[ebx+36]
    push eax
    push dword ptr [ebx+104]
    push dword ptr [ebx+24]
    call PrepareMenuDraw
    add esp,12
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    push dword ptr [esp+68]
    call dword ptr [esp+4]
    pushfd
    pushad
    mov ebx,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    cld
    lea eax,[ebx+36]
    push eax
    call CompleteMenuScope
    add esp,4
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    lea esp,[esp+64]
    ret 4
  }
}

extern "C" __declspec(naked) void MenuBitmapHook()
{
  __asm {
    lea esp,[esp-64]
    pushfd
    pushad
    mov ebx,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    cld
    lea eax,[ebx+36]
    push eax
    push dword ptr [ebx+100]
    push dword ptr [ebx+112]
    push dword ptr [ebx+108]
    push dword ptr [ebx+104]
    push dword ptr [ebx+24]
    call PrepareMenuBitmap
    add esp,24
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    push dword ptr [esp+76]
    push dword ptr [esp+76]
    push dword ptr [esp+76]
    call dword ptr [esp+12]
    pushfd
    pushad
    mov ebx,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    cld
    lea eax,[ebx+36]
    push eax
    call CompleteMenuScope
    add esp,4
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    lea esp,[esp+64]
    ret 12
  }
}

extern "C" __declspec(naked) void MenuQueueHook()
{
  __asm {
    lea esp,[esp-32]
    pushfd
    pushad
    mov ebx,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    cld
    lea eax,[ebx+36]
    push eax
    push dword ptr [ebx+68]
    push dword ptr [ebx+24]
    call PrepareMenuQueue
    add esp,12
    cmp dword ptr [ebx+40],1
    je held
    cmp dword ptr [ebx+40],2
    je resume
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    call dword ptr [esp]
    lea esp,[esp+32]
    ret
  held:
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    lea esp,[esp+32]
    ret
  resume:
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    push ecx
    mov ecx,dword ptr [esp+16]
    call dword ptr [esp+12]
    pop ecx
    pushfd
    pushad
    mov ebx,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    cld
    lea eax,[ebx+36]
    push eax
    push dword ptr [ebx+24]
    call CompleteMenuResume
    add esp,8
    cmp dword ptr [ebx+40],1
    je held
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    call dword ptr [esp]
    lea esp,[esp+32]
    ret
  }
}

extern "C" __declspec(naked) void MenuResumeHook()
{
  __asm {
    lea esp,[esp-4]
    pushfd
    pushad
    mov ebx,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    cld
    lea eax,[ebx+36]
    push eax
    push dword ptr [ebx+24]
    call PrepareMenuResume
    add esp,8
    test eax,eax
    jnz held
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    call dword ptr [esp]
    lea esp,[esp+4]
    ret
  held:
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    lea esp,[esp+4]
    ret
  }
}

// These two exact native handlers are a MOV to the single pending slot and RET.
// Replay that instruction before observing the fresh launch request.
extern "C" __declspec(naked) void MenuNewGameHook()
{
  __asm {
    mov dword ptr [ecx+128h],8003h
    pushfd
    pushad
    mov ebx,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    cld
    push dword ptr [ebx+24]
    call MenuCompletionLaunchIntent
    add esp,4
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    ret
  }
}

extern "C" __declspec(naked) void MenuMultiplayerHook()
{
  __asm {
    mov dword ptr [ecx+128h],8033h
    pushfd
    pushad
    mov ebx,esp
    sub esp,528
    and esp,-16
    fxsave [esp]
    cld
    push dword ptr [ebx+24]
    call MenuCompletionLaunchIntent
    add esp,4
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    ret
  }
}

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MENU_MODE_COMPLETION_HPP
