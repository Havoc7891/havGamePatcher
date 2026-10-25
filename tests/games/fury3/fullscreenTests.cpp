// SPDX-License-Identifier: MIT

#include "fullscreenPayload.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

using Config = FullscreenConfig;
using StatePrefix = FullscreenState;

using ToggleFunction = void(__cdecl *)();
using StretchFunction = BOOL(WINAPI *)(HDC, int, int, int, int, HDC, int, int, int, int, DWORD);
using CheckMenuFunction = DWORD(WINAPI *)(HMENU, UINT, UINT);

static DWORD savedStack;

__declspec(naked) static DWORD InvokeWithAbi(ToggleFunction)
{
  __asm
  {
        push ebp
        push ebx
        push esi
        push edi
        mov eax, dword ptr [esp+20]
        mov savedStack, esp
        mov ebp, 13579bdfh
        mov ebx, 2468ace0h
        mov esi, 31415926h
        mov edi, 27182818h
        call eax
        xor edx, edx
        cmp esp, savedStack
        je StackOkay
        or edx, 1
        mov esp, savedStack
    StackOkay:
        cmp ebp, 13579bdfh
        je FrameOkay
        or edx, 2
    FrameOkay:
        cmp ebx, 2468ace0h
        je BaseOkay
        or edx, 4
    BaseOkay:
        cmp esi, 31415926h
        je SourceOkay
        or edx, 8
    SourceOkay:
        cmp edi, 27182818h
        je DestinationOkay
        or edx, 16
    DestinationOkay:
        mov eax, edx
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
  }
}

struct Check
{
  std::string name;
  bool passed;
};

static std::vector<Check> checks;
static ToggleFunction toggleFunction;
static StatePrefix *payloadState;
static bool attemptReentry;
static unsigned reentryCount;
static unsigned rejectedShowCount;
static unsigned activationCount;
static HWND fixtureWindow;
static HMENU fixtureMenu;
static unsigned prepareCalls;
static unsigned leaveCalls;
static bool originalSawExpectedMenu;
static bool expectedMenuAtOriginal;
static bool originalSawIdleTransition;
static bool originalPresentFilledClient;
static StretchFunction fixtureStretch;
static bool RenderCurrentClient(StretchFunction stretch, HWND window);

static void __cdecl OriginalLifecycle(DWORD operation)
{
  if (operation == 1)
  {
    ++prepareCalls;
  }
  else
  {
    ++leaveCalls;
  }

  originalSawExpectedMenu =
      GetMenu(fixtureWindow) == (expectedMenuAtOriginal ? fixtureMenu : nullptr);
  originalSawIdleTransition = payloadState && payloadState->busy == 0;
  originalPresentFilledClient =
      !payloadState->active || RenderCurrentClient(fixtureStretch, fixtureWindow);
}

static bool InstallLifecycleStub(BYTE *game, DWORD rva, DWORD operation)
{
  BYTE stub[] = {0x68, 0, 0, 0, 0, 0xb8, 0, 0, 0, 0, 0xff, 0xd0, 0x83, 0xc4, 4, 0xc3};
  *reinterpret_cast<DWORD *>(stub + 1) = operation;
  *reinterpret_cast<DWORD *>(stub + 6) = reinterpret_cast<DWORD>(&OriginalLifecycle);
  DWORD oldProtection = 0;

  if (!VirtualProtect(game + rva, sizeof(stub), PAGE_EXECUTE_READWRITE, &oldProtection))
  {
    return false;
  }

  std::memcpy(game + rva, stub, sizeof(stub));
  FlushInstructionCache(GetCurrentProcess(), game + rva, sizeof(stub));

  return true;
}

static void Expect(const char *name, bool result)
{
  checks.push_back({name, result});
  std::printf("%s %s\n", result ? "PASS" : "FAIL", name);
}

static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
  if (message == WM_WINDOWPOSCHANGING)
  {
    auto *position = reinterpret_cast<WINDOWPOS *>(lParam);

    if ((position->flags & SWP_SHOWWINDOW) != 0)
    {
      ++rejectedShowCount;
      position->flags &= ~SWP_SHOWWINDOW;
    }

    position->flags |= SWP_NOACTIVATE;

    if (attemptReentry && payloadState && payloadState->busy && toggleFunction)
    {
      ++reentryCount;
      toggleFunction();
    }
  }

  if (message == WM_ACTIVATE && LOWORD(wParam) != WA_INACTIVE)
  {
    ++activationCount;
  }

  return DefWindowProcW(window, message, wParam, lParam);
}

struct Dib
{
  HDC dc = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ previous = nullptr;
  DWORD *pixels = nullptr;
  int width;
  int height;

  Dib(int requestedWidth, int requestedHeight) : width(requestedWidth), height(requestedHeight)
  {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    dc = CreateCompatibleDC(nullptr);
    bitmap =
        CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void **>(&pixels), nullptr, 0);

    if (bitmap)
    {
      previous = SelectObject(dc, bitmap);
    }

    SetStretchBltMode(dc, COLORONCOLOR);
  }

  ~Dib()
  {
    if (previous)
    {
      SelectObject(dc, previous);
    }

    if (bitmap)
    {
      DeleteObject(bitmap);
    }

    if (dc)
    {
      DeleteDC(dc);
    }
  }

  bool Valid() const
  {
    return dc && bitmap && pixels;
  }

  void Fill(DWORD color)
  {
    GdiFlush();

    for (int index = 0; index < width * height; ++index)
    {
      pixels[index] = color;
    }
  }

  DWORD Pixel(int x, int y) const
  {
    return pixels[y * width + x] & 0xffffff;
  }
};

static bool VerifyImage(
    const Dib &target, const RECT &frame, const RECT &content, DWORD imageColor, DWORD outsideColor)
{
  GdiFlush();

  for (int y = 0; y < target.height; ++y)
  {
    for (int x = 0; x < target.width; ++x)
    {
      const bool inFrame = x >= frame.left && x < frame.right && y >= frame.top && y < frame.bottom;
      const bool inContent =
          x >= content.left && x < content.right && y >= content.top && y < content.bottom;
      const DWORD expected = inContent ? imageColor : (inFrame ? 0 : outsideColor);

      if (target.Pixel(x, y) != expected)
      {
        std::printf("Pixel mismatch x=%d y=%d actual=%06lx expected=%06lx\n", x, y,
                    target.Pixel(x, y), expected);

        return false;
      }
    }
  }

  return true;
}

static bool RenderCurrentClient(StretchFunction stretch, HWND window)
{
  RECT client{};

  if (!GetClientRect(window, &client) || client.right < 1 || client.bottom < 1)
  {
    return false;
  }

  constexpr DWORD untouched = 0x00204060;
  constexpr DWORD color = 0x00bb7700;
  Dib source(32, 20);
  Dib target(client.right + 2, client.bottom + 2);

  if (!source.Valid() || !target.Valid())
  {
    return false;
  }

  source.Fill(color);
  target.Fill(untouched);
  const BOOL result =
      stretch(target.dc, 0, 0, client.right, client.bottom, source.dc, 0, 0, 32, 20, SRCCOPY);

  return result && VerifyImage(target, client, client, color, untouched);
}

static bool SameRect(const RECT &first, const RECT &second)
{
  return first.left == second.left && first.top == second.top && first.right == second.right &&
         first.bottom == second.bottom;
}

static void TestRendering(StretchFunction stretch, bool fullscreen, HWND window)
{
  constexpr DWORD untouched = 0x00204060;
  constexpr DWORD sourceColor = 0x0000bb44;
  Dib source(64, 48);
  Dib target(440, 320);
  Expect(fullscreen ? "active_memory_dib_created" : "inactive_memory_dib_created",
         source.Valid() && target.Valid());

  if (!source.Valid() || !target.Valid())
  {
    return;
  }

  source.Fill(0x00bb0044);

  for (int y = 7; y < 27; ++y)
  {
    for (int x = 8; x < 40; ++x)
    {
      source.pixels[y * source.width + x] = sourceColor;
    }
  }

  target.Fill(untouched);

  if (fullscreen)
  {
    SetWindowPos(window, nullptr, 0, 0, 400, 240, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }

  const int targetX = fullscreen ? 0 : 10;
  const int targetY = fullscreen ? 0 : 20;
  BOOL result = stretch(target.dc, targetX, targetY, 400, 240, source.dc, 8, 7, 32, 20, SRCCOPY);
  RECT frame{targetX, targetY, targetX + 400, targetY + 240};
  RECT content = frame;
  Expect(fullscreen ? "active_wide_client_filled_pixels" : "inactive_exact_forwarding_pixels",
         result && VerifyImage(target, frame, content, sourceColor, untouched));

  if (!fullscreen)
  {
    return;
  }

  target.Fill(untouched);
  SetWindowPos(window, nullptr, 0, 0, 240, 240, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  result = stretch(target.dc, 0, 0, 240, 240, source.dc, 8, 7, 32, 20, SRCCOPY);
  frame = RECT{0, 0, 240, 240};
  content = frame;
  Expect("active_square_client_filled_pixels",
         result && VerifyImage(target, frame, content, sourceColor, untouched));
  target.Fill(untouched);
  SetWindowPos(window, nullptr, 0, 0, 400, 300, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  result = stretch(target.dc, 0, 0, 400, 300, source.dc, 8, 7, 32, 20, SRCCOPY);
  frame = RECT{0, 0, 400, 300};
  Expect("active_4_to_3_client_filled_pixels",
         result && VerifyImage(target, frame, frame, sourceColor, untouched));
}

static DWORD WINAPI ForeignThreadToggle(void *)
{
  toggleFunction();

  return 0;
}

int main(int argc, char **argv)
{
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

  if (argc != 3)
  {
    std::fprintf(stderr, "Usage: fullscreenTests.exe payload.dll result.json\n");

    return 2;
  }

  HMODULE payload = LoadLibraryA(argv[1]);

  if (!payload)
  {
    std::fprintf(stderr, "Payload failed to load, Win32 error %lu\n", GetLastError());

    return 2;
  }

  auto *payloadConfig = reinterpret_cast<Config *>(GetProcAddress(payload, "config"));
  payloadState = reinterpret_cast<StatePrefix *>(GetProcAddress(payload, "state"));
  toggleFunction = reinterpret_cast<ToggleFunction>(GetProcAddress(payload, "ToggleFullscreen"));
  auto stretch = reinterpret_cast<StretchFunction>(GetProcAddress(payload, "StretchFrame"));
  fixtureStretch = stretch;
  auto updateMenu = reinterpret_cast<CheckMenuFunction>(GetProcAddress(payload, "UpdateMenuCheck"));
  auto prepareMainMenu =
      reinterpret_cast<ToggleFunction>(GetProcAddress(payload, "PrepareMainMenu"));
  auto leaveMainMenu = reinterpret_cast<ToggleFunction>(GetProcAddress(payload, "LeaveMainMenu"));

  if (!payloadConfig || !payloadState || !toggleFunction || !stretch || !updateMenu ||
      !prepareMainMenu || !leaveMainMenu)
  {
    std::fprintf(stderr, "Expected payload exports missing\n");
    FreeLibrary(payload);

    return 2;
  }

  auto *game = static_cast<BYTE *>(
      VirtualAlloc(nullptr, 0x140000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

  if (!game)
  {
    std::fprintf(stderr, "Synthetic data allocation failed\n");
    FreeLibrary(payload);

    return 2;
  }

  *reinterpret_cast<FARPROC *>(game + 0x1000) =
      GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetProcAddress");
  *reinterpret_cast<FARPROC *>(game + 0x1004) =
      GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetModuleHandleA");
  *reinterpret_cast<FARPROC *>(game + 0x1008) =
      GetProcAddress(GetModuleHandleA("gdi32.dll"), "StretchBlt");
  *payloadConfig = {0x46534650, 1, reinterpret_cast<DWORD>(game), 0x1000, 0x1004, 0x1008};
  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = WindowProcedure;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.lpszClassName = L"Fury3OwnedHiddenPayloadFixture";
  RegisterClassW(&windowClass);
  HMENU menu = CreateMenu();
  AppendMenuW(menu, MF_STRING, 0x9c45, L"Full Screen");
  AppendMenuW(menu, MF_STRING, 0x100, L"Unrelated");
  HWND window = CreateWindowExW(WS_EX_APPWINDOW, windowClass.lpszClassName, L"Owned hidden fixture",
                                WS_OVERLAPPEDWINDOW, 70, 90, 456, 345, nullptr, menu,
                                windowClass.hInstance, nullptr);

  if (!window || !menu)
  {
    std::fprintf(stderr, "Owned hidden window creation failed\n");

    return 2;
  }

  fixtureWindow = window;
  fixtureMenu = menu;

  if (!InstallLifecycleStub(game, 0x4fbf0, 1) || !InstallLifecycleStub(game, 0x4fc90, 2))
  {
    std::fprintf(stderr, "Owned lifecycle stubs could not be installed\n");

    return 2;
  }

  *reinterpret_cast<HWND *>(game + 0x1329cc) = window;
  *reinterpret_cast<DWORD *>(game + 0x132464) = 0;
  std::vector<BYTE> originalGame(game, game + 0x140000);
  RECT originalRect{};
  GetWindowRect(window, &originalRect);
  const LONG originalStyle = GetWindowLongW(window, GWL_STYLE);
  const LONG originalExStyle = GetWindowLongW(window, GWL_EXSTYLE);
  WINDOWPLACEMENT originalPlacement{};
  originalPlacement.length = sizeof(originalPlacement);
  GetWindowPlacement(window, &originalPlacement);
  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);

  Expect("initial_state_inactive", payloadState->active == 0 && payloadState->busy == 0);
  TestRendering(stretch, false, window);
  updateMenu(menu, 0x9c45, MF_BYCOMMAND | MF_CHECKED);
  Expect("inactive_menu_check_uses_new_state",
         (GetMenuState(menu, 0x9c45, MF_BYCOMMAND) & MF_CHECKED) == 0);
  updateMenu(menu, 0x100, MF_BYCOMMAND | MF_CHECKED);
  Expect("unrelated_menu_check_forwarded",
         (GetMenuState(menu, 0x100, MF_BYCOMMAND) & MF_CHECKED) != 0);

  attemptReentry = true;
  toggleFunction();
  RECT fullscreenRect{};
  GetWindowRect(window, &fullscreenRect);
  Expect("enter_sets_private_state", payloadState->active == 1 && payloadState->busy == 0);
  Expect("enter_uses_monitor_bounds", SameRect(fullscreenRect, monitor.rcMonitor));
  Expect("enter_removes_caption_frame_and_menu",
         (GetWindowLongW(window, GWL_STYLE) & (WS_CAPTION | WS_THICKFRAME)) == 0 &&
             GetMenu(window) == nullptr);
  Expect("enter_not_topmost", (GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0);
  Expect("entry_reentrancy_guard_exercised", reentryCount > 0 && payloadState->active == 1);
  updateMenu(menu, 0x9c45, MF_BYCOMMAND | MF_UNCHECKED);
  Expect("active_menu_check_uses_new_state",
         (GetMenuState(menu, 0x9c45, MF_BYCOMMAND) & MF_CHECKED) != 0);
  TestRendering(stretch, true, window);

  toggleFunction();
  RECT restoredRect{};
  GetWindowRect(window, &restoredRect);
  Expect("exit_clears_private_state", payloadState->active == 0 && payloadState->busy == 0);
  Expect("exit_exact_window_rect", SameRect(restoredRect, originalRect));
  Expect("exit_exact_style_and_exstyle",
         GetWindowLongW(window, GWL_STYLE) == originalStyle &&
             GetWindowLongW(window, GWL_EXSTYLE) == originalExStyle);
  Expect("exit_restores_same_menu", GetMenu(window) == menu);
  WINDOWPLACEMENT restoredPlacement{};
  restoredPlacement.length = sizeof(restoredPlacement);
  GetWindowPlacement(window, &restoredPlacement);
  Expect("exit_restores_placement",
         (!IsWindowVisible(window) || originalPlacement.showCmd == restoredPlacement.showCmd) &&
             originalPlacement.flags == restoredPlacement.flags &&
             SameRect(originalPlacement.rcNormalPosition, restoredPlacement.rcNormalPosition));

  bool cyclesPassed = true;

  for (int cycle = 0; cycle < 8; ++cycle)
  {
    toggleFunction();
    cyclesPassed = cyclesPassed && payloadState->active == 1 && payloadState->busy == 0;
    toggleFunction();
    GetWindowRect(window, &restoredRect);
    cyclesPassed = cyclesPassed && payloadState->active == 0 && payloadState->busy == 0 &&
                   SameRect(restoredRect, originalRect) && GetMenu(window) == menu &&
                   GetWindowLongW(window, GWL_STYLE) == originalStyle &&
                   GetWindowLongW(window, GWL_EXSTYLE) == originalExStyle;
  }

  Expect("eight_repeated_cycles_restore_exactly", cyclesPassed);
  auto lifecycle =
      [&](ToggleFunction function, bool preparing, bool expectedMenu, const char *label)
  {
    expectedMenuAtOriginal = expectedMenu;
    originalSawExpectedMenu = false;
    originalSawIdleTransition = false;
    originalPresentFilledClient = false;
    const unsigned beforeReentry = reentryCount;
    const bool shouldReenter =
        payloadState->active && GetMenu(window) != (expectedMenu ? menu : nullptr);
    const unsigned beforePrepare = prepareCalls;
    const unsigned beforeLeave = leaveCalls;
    const DWORD abi = InvokeWithAbi(function);
    Expect((std::string(label) + "_original_called_once").c_str(),
           prepareCalls == beforePrepare + (preparing ? 1u : 0u) &&
               leaveCalls == beforeLeave + (preparing ? 0u : 1u));
    Expect((std::string(label) + "_menu_correct_before_original_present").c_str(),
           originalSawExpectedMenu);
    Expect((std::string(label) + "_abi_and_reentry_guard").c_str(),
           abi == 0 && originalSawIdleTransition && payloadState->busy == 0);

    if (shouldReenter)
    {
      Expect((std::string(label) + "_nested_toggle_blocked_during_menu_resize").c_str(),
             reentryCount > beforeReentry && payloadState->active == 1);
    }

    if (payloadState->active)
    {
      Expect((std::string(label) + "_first_present_fills_current_client").c_str(),
             originalPresentFilledClient);
    }
  };

  lifecycle(prepareMainMenu, true, true, "windowed_prepare");
  Expect("windowed_prepare_keeps_original_frame",
         GetWindowLongW(window, GWL_STYLE) == originalStyle && GetMenu(window) == menu);
  toggleFunction();
  Expect("fullscreen_enter_main_menu_keeps_native_menu",
         payloadState->active == 1 && GetMenu(window) == menu);
  updateMenu(menu, 0x9c45, MF_BYCOMMAND | MF_UNCHECKED);
  Expect("fullscreen_main_menu_checkmark_checked",
         (GetMenuState(menu, 0x9c45, MF_BYCOMMAND) & MF_CHECKED) != 0);
  RECT menuRect{};
  GetWindowRect(window, &menuRect);
  Expect("fullscreen_main_menu_uses_monitor_bounds", SameRect(menuRect, monitor.rcMonitor));
  lifecycle(leaveMainMenu, false, false, "fullscreen_leave_for_loading");
  Expect("fullscreen_gameplay_keeps_native_menu_hidden",
         payloadState->active == 1 && GetMenu(window) == nullptr);
  toggleFunction();
  Expect("windowed_from_gameplay_restores_native_menu",
         payloadState->active == 0 && GetMenu(window) == menu);
  lifecycle(leaveMainMenu, false, true, "windowed_leave");
  Expect("windowed_leave_keeps_original_frame",
         GetWindowLongW(window, GWL_STYLE) == originalStyle && GetMenu(window) == menu);
  toggleFunction();
  Expect("fullscreen_enter_gameplay_hides_native_menu",
         payloadState->active == 1 && GetMenu(window) == nullptr);
  lifecycle(prepareMainMenu, true, true, "fullscreen_return_to_main_menu");
  Expect("fullscreen_return_restores_native_menu",
         payloadState->active == 1 && GetMenu(window) == menu);
  GetWindowRect(window, &menuRect);
  Expect("fullscreen_return_keeps_monitor_bounds", SameRect(menuRect, monitor.rcMonitor));
  lifecycle(leaveMainMenu, false, false, "fullscreen_second_leave");
  lifecycle(prepareMainMenu, true, true, "fullscreen_second_return");
  toggleFunction();
  GetWindowRect(window, &restoredRect);
  Expect("windowed_from_main_menu_restores_exactly",
         payloadState->active == 0 && GetMenu(window) == menu &&
             SameRect(restoredRect, originalRect) &&
             GetWindowLongW(window, GWL_STYLE) == originalStyle &&
             GetWindowLongW(window, GWL_EXSTYLE) == originalExStyle);
  updateMenu(menu, 0x9c45, MF_BYCOMMAND | MF_CHECKED);
  Expect("windowed_main_menu_checkmark_unchecked",
         (GetMenuState(menu, 0x9c45, MF_BYCOMMAND) & MF_CHECKED) == 0);
  Expect("menu_transitions_exercised_reentrancy", reentryCount > 54);
  Expect("game_memory_unchanged", std::memcmp(game, originalGame.data(), originalGame.size()) == 0);
  Expect("legacy_fullscreen_flag_remains_zero", *reinterpret_cast<DWORD *>(game + 0x132464) == 0);

  *reinterpret_cast<DWORD *>(game + 0x132464) = 1;
  toggleFunction();
  Expect("legacy_active_refuses_transition",
         payloadState->active == 0 && *reinterpret_cast<DWORD *>(game + 0x132464) == 1);
  *reinterpret_cast<DWORD *>(game + 0x132464) = 0;
  HANDLE foreignThread = CreateThread(nullptr, 0, ForeignThreadToggle, nullptr, 0, nullptr);

  if (foreignThread)
  {
    WaitForSingleObject(foreignThread, 5000);
    CloseHandle(foreignThread);
  }

  Expect("foreign_thread_cannot_transition",
         foreignThread != nullptr && payloadState->active == 0 && payloadState->busy == 0);
  std::printf("Hidden guard: visible=%d attemptedShow=%u activations=%u originalShowCmd=%u "
              "restoredShowCmd=%u\n",
              IsWindowVisible(window), rejectedShowCount, activationCount,
              originalPlacement.showCmd, restoredPlacement.showCmd);
  Expect("fixture_stayed_hidden_and_inactive",
         !IsWindowVisible(window) && rejectedShowCount == 0 && activationCount == 0);
  attemptReentry = false;

  const size_t total = checks.size();
  size_t failed = 0;

  for (const auto &check : checks)
  {
    if (!check.passed)
    {
      ++failed;
    }
  }

  FILE *output = nullptr;
  fopen_s(&output, argv[2], "wb");

  if (output)
  {
    std::fprintf(
        output,
        "{\n  \"format\": \"Fury3MenuFullscreenPayloadOwnedFixture\",\n  \"passed\": %s,\n  "
        "\"checksPassed\": %zu,\n  \"checksTotal\": %zu,\n  \"reentrantCallsExercised\": %u,\n  "
        "\"hiddenOwnedWindow\": true,\n  \"gameLaunched\": false,\n  \"gameAttached\": false,\n  "
        "\"displaySettingsModified\": false,\n  \"checks\": [\n",
        failed ? "false" : "true", total - failed, total, reentryCount);

    for (size_t index = 0; index < total; ++index)
    {
      std::fprintf(output, "    {\"name\": \"%s\", \"passed\": %s}%s\n", checks[index].name.c_str(),
                   checks[index].passed ? "true" : "false", index + 1 < total ? "," : "");
    }

    std::fprintf(output, "  ],\n  \"limitations\": [\"Synthetic data and hidden owned window "
                         "only\", \"Does not validate game input, palette realization, mission "
                         "transitions, cutscenes or interactive fullscreen\"]\n}\n");
    std::fclose(output);
  }

  DestroyWindow(window);
  UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
  VirtualFree(game, 0, MEM_RELEASE);
  FreeLibrary(payload);
  std::printf("RESULT %zu/%zu checks passed\n", total - failed, total);

  return failed || !output ? 1 : 0;
}
