// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MODE_ADMISSION_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MODE_ADMISSION_HPP

#include <stddef.h>
#include "modeNativeAddresses.hpp"
#include "modeAdmissionTypes.hpp"
#include "hardwareModeCompletionTypes.hpp"
#include "softwareModeCompletionTypes.hpp"
#include "menuModeCompletionTypes.hpp"

#pragma intrinsic(__writefsdword)

extern "C"
{
  ModeAdmissionState modeAdmissionState = {};
}

namespace ModeAdmission
{
  constexpr DWORD nativeEntry = modeNativePreTranslate, modifierImport = modeNativeGetKeyState;
  using Clock = DWORD(WINAPI *)();
  using Native = BOOL(__thiscall *)(void *, MSG *);

  enum Decision : DWORD
  {
    forward,
    repeat,
    active,
    stale,
    reentry
  };

  struct LastError
  {
    const DWORD saved = __readfsdword(0x34);

    ~LastError()
    {
      __writefsdword(0x34, saved);
    }
  };

  static void Increment(DWORD &value)
  {
    if (value != 0xffffffffu)
    {
      ++value;
    }
  }

  struct Owner
  {
    DWORD base, app, frameObject = 0, frame = 0, thread;
    decltype(&::IsWindow) isWindow = nullptr;
    decltype(&::IsChild) isChild = nullptr;
    decltype(&::GetWindowThreadProcessId) windowThread = nullptr;
    decltype(&::GetKeyState) keyState = nullptr;
    decltype(&::GetModuleHandleA) module = nullptr;
    decltype(&::GetProcAddress) address = nullptr;

    Owner(DWORD imageBase, void *application)
        : base(imageBase), app(reinterpret_cast<DWORD>(application)),
          thread(TextureLifecycle::ThreadId())
    {
      if (!ConfiguredBase() || !app || ReadAt(GameRva::app) != app)
      {
        return;
      }
      frameObject = *reinterpret_cast<volatile DWORD *>(app + 0x1c);
      if (!frameObject)
      {
        return;
      }
      frame = *reinterpret_cast<volatile DWORD *>(frameObject + 0x1c);
      isWindow = Import<decltype(isWindow)>(GameRva::isWindow);
      isChild = Import<decltype(isChild)>(GameRva::isChild);
      windowThread = Import<decltype(windowThread)>(GameRva::getWindowThreadProcessId);
      keyState = Import<decltype(keyState)>(modifierImport);
      module = Import<decltype(module)>(GameRva::getModuleHandleA);
      address = Import<decltype(address)>(GameRva::getProcAddress);
    }

    template <class T> T Import(DWORD rva) const
    {
      return *reinterpret_cast<T *>(base + rva);
    }

    DWORD ReadAt(DWORD rva) const
    {
      return *reinterpret_cast<volatile DWORD *>(base + rva);
    }

    bool ConfiguredBase() const
    {
      return base && config.magic == 0x48474c50 && config.version == 1 && config.gameBase == base;
    }

    bool Current() const
    {
      return ConfiguredBase() && app && frameObject && frame &&
             TextureLifecycle::ThreadId() == thread && ReadAt(GameRva::app) == app &&
             *reinterpret_cast<volatile DWORD *>(app + 0x1c) == frameObject &&
             *reinterpret_cast<volatile DWORD *>(frameObject + 0x1c) == frame;
    }

    bool Windows(HWND target = nullptr) const
    {
      if (!Current() || !isWindow || !windowThread || !isChild ||
          !isWindow(reinterpret_cast<HWND>(frame)) || !Current() ||
          windowThread(reinterpret_cast<HWND>(frame), nullptr) != thread || !Current())
      {
        return false;
      }
      if (!target || reinterpret_cast<DWORD>(target) == frame)
      {
        return true;
      }
      return isWindow(target) && Current() && isChild(reinterpret_cast<HWND>(frame), target) &&
             Current() && windowThread(target, nullptr) == thread && Current();
    }

    bool Matches(DWORD generation) const
    {
      const auto &s = modeAdmissionState;
      return Current() && generation && s.ownerGeneration == generation && s.imageBase == base &&
             s.app == app && s.frameObject == frameObject && s.frame == frame && s.thread == thread;
    }
  };

  static DWORD Adopt(const Owner &owner)
  {
    auto &s = modeAdmissionState;
    if (!owner.Current())
    {
      return 0;
    }
    if (owner.Matches(s.ownerGeneration))
    {
      return s.ownerGeneration;
    }
    // A foreign thread never clears or steals an established UI owner's state.
    // A replacement UI thread therefore stays native for this process lifetime.
    if ((s.thread && s.thread != owner.thread) || s.ownerGeneration == 0xffffffffu)
    {
      return 0;
    }

    s.imageBase = owner.base;
    s.app = owner.app;
    s.frameObject = owner.frameObject;
    s.frame = owner.frame;
    s.thread = owner.thread;
    ++s.ownerGeneration;
    s.transitionDepth = s.inputDepth = s.clockValid = s.completedAt = s.clockAddress = 0;
    Increment(s.resets);
    return s.ownerGeneration;
  }

  static bool Tick(const Owner &owner, DWORD generation, DWORD &now)
  {
    if (!owner.Matches(generation) || !owner.module || !owner.address)
    {
      return false;
    }
    auto clock = reinterpret_cast<Clock>(modeAdmissionState.clockAddress);
    if (!clock)
    {
      const HMODULE kernel = owner.module("kernel32.dll");
      if (!owner.Matches(generation) || !kernel)
      {
        return false;
      }
      clock = reinterpret_cast<Clock>(owner.address(kernel, "GetTickCount"));
      if (!owner.Matches(generation) || !clock)
      {
        return false;
      }

      modeAdmissionState.clockAddress = reinterpret_cast<DWORD>(clock);
    }
    now = clock();
    return owner.Matches(generation);
  }

  struct InputCall
  {
    DWORD consumed, original, base, ownerGeneration;
    DWORD generation, index, tracked, app;
    DWORD frameObject, frame, thread, entryError;
    DWORD reserved[4];
  };

  static_assert(sizeof(InputCall) == 64, "Input wrapper stack locals");

  static void Prepare(void *app, MSG *message, InputCall &call)
  {
    LastError error;
    const DWORD base = config.gameBase;
    const auto raw = reinterpret_cast<volatile DWORD *>(&call);
    for (DWORD i = 0; i < sizeof(call) / sizeof(DWORD); ++i)
    {
      raw[i] = 0;
    }
    call.original = base + nativeEntry;
    call.base = base;
    call.app = reinterpret_cast<DWORD>(app);
    call.entryError = error.saved;
    Owner owner(base, app);
    if (!message || !owner.Current())
    {
      return;
    }
    // Both native pumps own a complete MSG. Copy before callbacks can reuse it.
    MSG copied;
    const auto source = reinterpret_cast<const volatile DWORD *>(message);
    const auto output = reinterpret_cast<DWORD *>(&copied);
    for (DWORD i = 0; i < sizeof(MSG) / sizeof(DWORD); ++i)
    {
      output[i] = source[i];
    }
    if (!copied.hwnd || copied.wParam != VK_F4 ||
        (copied.message != WM_KEYDOWN && copied.message != WM_SYSKEYDOWN) ||
        (static_cast<DWORD>(copied.lParam) & (1u << 29)) || !owner.keyState ||
        !owner.Windows(copied.hwnd))
    {
      return;
    }
    const SHORT control = owner.keyState(VK_CONTROL);
    if (!owner.Current())
    {
      return;
    }
    const SHORT shift = owner.keyState(VK_SHIFT);
    if (!owner.Current())
    {
      return;
    }
    const SHORT alt = owner.keyState(VK_MENU);
    if (!owner.Current() || ((control | shift | alt) & 0x8000))
    {
      return;
    }
    const DWORD token = Adopt(owner);
    if (!token)
    {
      return;
    }
    DWORD now = 0;
    const bool clock = Tick(owner, token, now);
    if (!owner.Matches(token))
    {
      return;
    }
    const bool hardwarePending = HardwareCompletionSuppress(app, token) != FALSE;
    const bool softwarePending = SoftwareCompletionSuppress(app, token) != FALSE;
    const bool menuFresh = clock && !(static_cast<DWORD>(copied.lParam) & (1u << 30)) &&
                           (!modeAdmissionState.clockValid ||
                            static_cast<LONG>(copied.time - modeAdmissionState.completedAt) > 0);
    const bool menuPending = MenuCompletionSuppress(app, token, menuFresh) != FALSE;
    const bool failedPresentationPending = FailedPresentationSuppress(app, token) != FALSE;
    const bool completionPending =
        hardwarePending || softwarePending || menuPending || failedPresentationPending;

    auto &s = modeAdmissionState;
    if (!clock)
    {
      s.clockValid = 0;
      Increment(s.clockFailures);
    }
    else if (s.clockValid && now - s.completedAt >= 0x80000000u)
    {
      s.clockValid = 0;
      Increment(s.expiredBarriers);
    }
    DWORD decision = forward;
    if (static_cast<DWORD>(copied.lParam) & (1u << 30))
    {
      decision = repeat;
      Increment(s.suppressedRepeat);
    }
    else if (s.transitionDepth || completionPending)
    {
      decision = active;
      Increment(s.suppressedActive);
    }
    else if (s.inputDepth)
    {
      decision = reentry;
      Increment(s.suppressedReentry);
    }
    else if (clock && s.clockValid && static_cast<LONG>(copied.time - s.completedAt) <= 0)
    {
      decision = stale;
      Increment(s.suppressedStale);
    }
    else
    {
      ++s.inputDepth;
      Increment(s.accepted);
      call.tracked = 1;
    }
    call.consumed = decision != forward;
    call.ownerGeneration = token;
    call.frameObject = owner.frameObject;
    call.frame = owner.frame;
    call.thread = owner.thread;
  }

  static void Complete(const InputCall &call, DWORD)
  {
    LastError error;
    if (!call.tracked)
    {
      return;
    }
    Owner owner(call.base, reinterpret_cast<void *>(call.app));
    if (!owner.Matches(call.ownerGeneration) || owner.frameObject != call.frameObject ||
        owner.frame != call.frame || owner.thread != call.thread)
    {
      return;
    }

    if (modeAdmissionState.inputDepth)
    {
      --modeAdmissionState.inputDepth;
    }
  }
} // namespace ModeAdmission

extern "C" DWORD __cdecl BeginModeAdmission(void *app)
{
  ModeAdmission::LastError error;
  ModeAdmission::Owner owner(config.gameBase, app);
  if (!owner.Windows())
  {
    return 0;
  }
  const DWORD token = ModeAdmission::Adopt(owner);
  if (!token)
  {
    return 0;
  }
  {

    auto &s = modeAdmissionState;
    if (s.transitionDepth == 0xffffffffu)
    {
      s.incomplete = 1;
      return 0;
    }
    ++s.transitionDepth;
    ModeAdmission::Increment(s.transitionSerial);
  }
  HardwareCompletionBeginTransition(app, token);
  SoftwareCompletionBeginTransition(app, token);
  MenuCompletionBeginTransition(app, token);
  return token;
}

extern "C" void __cdecl EndModeAdmission(void *app, DWORD token)
{
  ModeAdmission::LastError error;
  if (!token || token != modeAdmissionState.ownerGeneration ||
      reinterpret_cast<DWORD>(app) != modeAdmissionState.app ||
      TextureLifecycle::ThreadId() != modeAdmissionState.thread)
  {
    return;
  }
  ModeAdmission::Owner owner(modeAdmissionState.imageBase, app);
  if (!owner.Matches(token) || !modeAdmissionState.transitionDepth)
  {
    return;
  }
  if (modeAdmissionState.transitionDepth > 1)
  {

    --modeAdmissionState.transitionDepth;
    return;
  }
  // Keep the outer scope active during dynamic clock resolution and callbacks.
  DWORD now = 0;
  const bool clock = ModeAdmission::Tick(owner, token, now);
  if (!owner.Matches(token) || modeAdmissionState.transitionDepth != 1)
  {
    return;
  }
  {

    auto &s = modeAdmissionState;
    s.transitionDepth = 0;
    s.clockValid = clock;
    s.completedAt = clock ? now : 0;
    if (!clock)
    {
      ModeAdmission::Increment(s.clockFailures);
    }
    ModeAdmission::Increment(s.completedTransitions);
  }
  HardwareCompletionEndTransition(app, token);
  SoftwareCompletionEndTransition(app, token);
  MenuCompletionEndTransition(app, token);
}

extern "C" void __cdecl PrepareModeAdmission(void *app,
                                             MSG *message,
                                             ModeAdmission::InputCall *call)
{
  ModeAdmission::Prepare(app, message, *call);
  BeginPresentationInput(app, message, call);
}

extern "C" void __cdecl CompleteModeAdmission(const ModeAdmission::InputCall *call, DWORD result)
{
  EndPresentationInput(call);
  ModeAdmission::Complete(*call, result);
}

extern "C" __declspec(naked) BOOL ModeAdmissionPreTranslate()
{
  __asm {
    lea esp, [esp - 64]
    pushfd
    pushad
    mov ebx, esp
    sub esp, 528
    and esp, -16
    fxsave [esp]
    cld
    lea eax, [ebx + 36]
    push eax
    push dword ptr [ebx + 104]
    push dword ptr [ebx + 24]
    call PrepareModeAdmission
    add esp, 12
    fxrstor [esp]
    mov esp, ebx
    popad
    popfd
    pushfd
    cmp dword ptr [esp + 4], 0
    jne consumed
    popfd
    push dword ptr [esp + 68]
    call dword ptr [esp + 8]
    pushfd
    pushad
    mov ebx, esp
    sub esp, 528
    and esp, -16
    fxsave [esp]
    cld
    push dword ptr [ebx + 28]
    lea eax, [ebx + 36]
    push eax
    call CompleteModeAdmission
    add esp, 8
    fxrstor [esp]
    mov esp, ebx
    popad
    popfd
    lea esp, [esp + 64]
    ret 4
  consumed:
    popfd
    mov eax, 1
    lea esp, [esp + 64]
    ret 4
  }
}

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MODE_ADMISSION_HPP
