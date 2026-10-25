// SPDX-License-Identifier: MIT

#include "fullscreenPayload.hpp"
#include <mmsystem.h>
#include <digitalv.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using Config = FullscreenConfig;
using StatePrefix = FullscreenState;
using ResizeFunction = void(__cdecl *)(HWND);
using ToggleFunction = void(__cdecl *)();

struct Check
{
  std::string name;
  bool passed;
};

static std::vector<Check> checks;
static ResizeFunction resizeMovie;
static HWND parentWindow;
static HWND childWindow;
static WNDPROC childProcedure;
static BYTE *game;
static unsigned putCalls;
static unsigned originalCalls;
static unsigned childReentries;
static unsigned successfulNotifications;
static unsigned abortedNotifications;
static unsigned rejectedShowCount;
static unsigned activationCount;
static bool failPut;
static unsigned sourceFault;
static bool reenterPut;
static bool reenterChild;
static bool reenterOriginal;
static bool autoResize;
static HWND lastOriginalParent;
static DWORD savedStack;
static const char *currentStep = "startup";
static unsigned foregroundActivationCount;
static bool insideNativePlayCommand;
static bool insidePayloadOperation;
static unsigned nativePlayActivations;
static unsigned unexpectedActivations;
static unsigned payloadActivations;

static void Expect(const char *name, bool passed)
{
  checks.push_back({name, passed});
  std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
}

__declspec(naked) static DWORD InvokeWithAbi(ResizeFunction, HWND)
{
  __asm
  {
        push ebp
        push ebx
        push esi
        push edi
        mov eax, dword ptr [esp+20]
        mov ecx, dword ptr [esp+24]
        mov savedStack, esp
        mov ebp, 13579bdfh
        mov ebx, 2468ace0h
        mov esi, 31415926h
        mov edi, 27182818h
        push ecx
        call eax
        add esp, 4
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

static void __cdecl OriginalMovie(HWND parent)
{
  ++originalCalls;
  lastOriginalParent = parent;

  if (reenterOriginal)
  {
    resizeMovie(parent);
  }
}

static MCIERROR WINAPI ObservedMciSendString(LPCSTR command,
                                             LPSTR result,
                                             UINT length,
                                             HWND callback)
{
  if (std::strncmp(command, "put mov destination", 19) == 0)
  {
    ++putCalls;

    if (reenterPut)
    {
      resizeMovie(parentWindow);
    }

    if (failPut)
    {
      return MCIERR_UNSUPPORTED_FUNCTION;
    }
  }

  if (sourceFault && std::strcmp(command, "where mov source") == 0)
  {
    if (sourceFault == 1)
    {
      return MCIERR_INVALID_DEVICE_ID;
    }

    if (result && length)
    {
      strcpy_s(result, length, sourceFault == 2 ? "0 0 broken" : "0 0 0 120");
    }

    return 0;
  }

  const bool previous = insidePayloadOperation;
  insidePayloadOperation = true;
  const MCIERROR error = mciSendStringA(command, result, length, callback);
  insidePayloadOperation = previous;

  return error;
}

static BOOL WINAPI
ObservedMoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint)
{
  const bool previous = insidePayloadOperation;
  insidePayloadOperation = true;
  const BOOL result = MoveWindow(window, x, y, width, height, repaint);
  insidePayloadOperation = previous;

  return result;
}

static FARPROC WINAPI ObservedGetProcAddress(HMODULE module, LPCSTR name)
{
  if (reinterpret_cast<ULONG_PTR>(name) > 0xffff && std::strcmp(name, "mciSendStringA") == 0)
  {
    return reinterpret_cast<FARPROC>(&ObservedMciSendString);
  }

  if (reinterpret_cast<ULONG_PTR>(name) > 0xffff && std::strcmp(name, "MoveWindow") == 0)
  {
    return reinterpret_cast<FARPROC>(&ObservedMoveWindow);
  }

  return GetProcAddress(module, name);
}

static LRESULT CALLBACK ParentProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
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
  }

  if (message == WM_ACTIVATE && LOWORD(wParam) != WA_INACTIVE)
  {
    ++activationCount;
    const bool foreground = GetForegroundWindow() == window;
    foregroundActivationCount += foreground ? 1 : 0;

    if (insidePayloadOperation)
    {
      ++payloadActivations;
    }
    else if (insideNativePlayCommand)
    {
      ++nativePlayActivations;
    }
    else
    {
      ++unexpectedActivations;
    }

    std::printf("Activation diagnostic: step=%s visible=%d foreground=%d\n", currentStep,
                IsWindowVisible(window), foreground);
  }

  if (message == WM_SIZE && autoResize && resizeMovie)
  {
    resizeMovie(window);
  }

  if (message == MM_MCINOTIFY)
  {
    if (wParam == MCI_NOTIFY_SUCCESSFUL)
    {
      ++successfulNotifications;
    }
    else
    {
      ++abortedNotifications;
    }
  }

  return DefWindowProcW(window, message, wParam, lParam);
}

static LRESULT CALLBACK ChildProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
  if (message == WM_WINDOWPOSCHANGED && reenterChild)
  {
    ++childReentries;
    resizeMovie(parentWindow);
  }

  return CallWindowProcW(childProcedure, window, message, wParam, lParam);
}

static bool
Command(const std::string &command, std::string *result = nullptr, HWND callback = nullptr)
{
  char text[256]{};
  insideNativePlayCommand = command == "play mov window notify";
  MCIERROR code = mciSendStringA(command.c_str(), text, sizeof(text), callback);
  insideNativePlayCommand = false;

  if (code != 0)
  {
    char error[256]{};
    mciGetErrorStringA(code, error, sizeof(error));
    std::printf("MCI error %lu: %s\n", static_cast<unsigned long>(code), error);
  }

  if (result)
  {
    *result = text;
  }

  return code == 0;
}

static bool Rectangle(const char *request, RECT &rectangle)
{
  std::string result;

  if (!Command(request, &result))
  {
    return false;
  }

  return sscanf_s(result.c_str(), "%ld %ld %ld %ld", &rectangle.left, &rectangle.top,
                  &rectangle.right, &rectangle.bottom) == 4;
}

static bool FullDestination()
{
  RECT client{}, child{}, destination{}, source{};

  if (!GetClientRect(parentWindow, &client) || !GetWindowRect(childWindow, &child))
  {
    return false;
  }

  MapWindowPoints(nullptr, parentWindow, reinterpret_cast<POINT *>(&child), 2);

  return EqualRect(&client, &child) && Rectangle("where mov destination", destination) &&
         EqualRect(&client, &destination) && Rectangle("where mov source", source) &&
         source.left == 0 && source.top == 0 && source.right == 160 && source.bottom == 120;
}

static bool RenderedFrameFillsClient()
{
  RECT client{};
  GetClientRect(parentWindow, &client);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = client.right;
  info.bmiHeader.biHeight = -client.bottom;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  DWORD *pixels = nullptr;
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP bitmap =
      CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void **>(&pixels), nullptr, 0);

  if (!dc || !bitmap)
  {
    return false;
  }

  HGDIOBJ previous = SelectObject(dc, bitmap);
  std::memset(pixels, 0x5a, static_cast<size_t>(client.right) * client.bottom * sizeof(DWORD));
  MCI_DGV_UPDATE_PARMS update{};
  update.hDC = dc;
  update.rc = client;
  const MCIERROR code =
      mciSendCommandA(mciGetDeviceIDA("mov"), MCI_UPDATE, MCI_DGV_UPDATE_HDC | MCI_DGV_RECT,
                      reinterpret_cast<DWORD_PTR>(&update));
  GdiFlush();
  auto color = [&](int x, int y)
  {
    return pixels[static_cast<size_t>(y) * client.right + x] & 0xffffff;
  };

  auto red = [](DWORD rgb)
  {
    return (rgb >> 16) & 255;
  };

  auto green = [](DWORD rgb)
  {
    return (rgb >> 8) & 255;
  };

  auto blue = [](DWORD rgb)
  {
    return rgb & 255;
  };

  auto magenta = [&](DWORD rgb)
  {
    return red(rgb) > 80 && blue(rgb) > 80 && green(rgb) < 20;
  };

  const DWORD topLeft = color(client.right / 4, client.bottom / 4);
  const DWORD topRight = color(client.right * 3 / 4, client.bottom / 4);
  const DWORD bottomLeft = color(client.right / 4, client.bottom * 3 / 4);
  const DWORD bottomRight = color(client.right * 3 / 4, client.bottom * 3 / 4);
  const bool correct =
      code == 0 && magenta(color(1, 1)) && magenta(color(client.right - 2, 1)) &&
      magenta(color(1, client.bottom - 2)) && magenta(color(client.right - 2, client.bottom - 2)) &&
      red(topLeft) > green(topLeft) * 2 && green(topRight) > red(topRight) * 2 &&
      blue(bottomLeft) > red(bottomLeft) * 2 && red(bottomRight) > blue(bottomRight) * 2 &&
      green(bottomRight) > blue(bottomRight) * 2;

  if (!correct)
  {
    std::printf("Frame diagnostic: code=%lu corners=%06lx,%06lx,%06lx,%06lx "
                "quadrants=%06lx,%06lx,%06lx,%06lx\n",
                code, color(1, 1), color(client.right - 2, 1), color(1, client.bottom - 2),
                color(client.right - 2, client.bottom - 2), topLeft, topRight, bottomLeft,
                bottomRight);
  }

  SelectObject(dc, previous);
  DeleteObject(bitmap);
  DeleteDC(dc);

  return correct;
}

static void Pump(DWORD milliseconds)
{
  const ULONGLONG stop = GetTickCount64() + milliseconds;
  do
  {
    MSG message{};

    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }

    Sleep(5);
  } while (GetTickCount64() < stop);
}

static DWORD &Global(DWORD rva)
{
  return *reinterpret_cast<DWORD *>(game + rva);
}

static void CheckGuard(const char *name, HWND parent)
{
  const unsigned beforePut = putCalls;
  const unsigned beforeOriginal = originalCalls;
  RECT before{}, after{};
  GetWindowRect(childWindow, &before);
  const DWORD abi = InvokeWithAbi(resizeMovie, parent);
  GetWindowRect(childWindow, &after);
  Expect(name, abi == 0 && beforePut == putCalls && beforeOriginal == originalCalls &&
                   EqualRect(&before, &after));
}

static bool WriteReport(const wchar_t *path)
{
  FILE *file = nullptr;

  if (_wfopen_s(&file, path, L"wb") != 0)
  {
    return false;
  }

  size_t passed = 0;

  for (const auto &check : checks)
  {
    passed += check.passed ? 1 : 0;
  }

  std::fprintf(file,
               "{\"format\":\"OwnedMciMovieValidation\",\"passed\":%s,\"checksTotal\":%zu,"
               "\"checksPassed\":%zu,\"gameLaunched\":false,\"gameAttached\":false,\"videoSource\":"
               "\"generated checkerboard and silent "
               "PCM\",\"successfulNotifications\":%u,\"abortedNotifications\":%u,\"showAttempts\":%"
               "u,\"activations\":%u,\"results\":[",
               passed == checks.size() ? "true" : "false", checks.size(), passed,
               successfulNotifications, abortedNotifications, rejectedShowCount, activationCount);

  for (size_t i = 0; i < checks.size(); ++i)
  {
    std::fprintf(file, "%s{\"name\":\"%s\",\"passed\":%s}", i ? "," : "", checks[i].name.c_str(),
                 checks[i].passed ? "true" : "false");
  }

  std::fprintf(file,
               "],\"activationAttribution\":{\"nativePlayWindowCommand\":%u,\"payloadOperations\":%"
               "u,\"unexpected\":%u,\"foregroundNotifications\":%u}}\n",
               nativePlayActivations, payloadActivations, unexpectedActivations,
               foregroundActivationCount);
  std::fclose(file);

  return passed == checks.size();
}

int wmain(int argc, wchar_t **argv)
{
  if (argc != 4)
  {
    return 2;
  }

  HMODULE payload = LoadLibraryW(argv[1]);

  if (!payload)
  {
    return 3;
  }

  resizeMovie = reinterpret_cast<ResizeFunction>(GetProcAddress(payload, "ResizeMovie"));
  auto toggle = reinterpret_cast<ToggleFunction>(GetProcAddress(payload, "ToggleFullscreen"));
  auto *payloadConfig = reinterpret_cast<Config *>(GetProcAddress(payload, "config"));
  auto *payloadState = reinterpret_cast<StatePrefix *>(GetProcAddress(payload, "state"));
  Expect("payload-exports", resizeMovie && toggle && payloadConfig && payloadState);

  if (!resizeMovie || !toggle || !payloadConfig || !payloadState)
  {
    return WriteReport(argv[3]) ? 0 : 1;
  }

  game = static_cast<BYTE *>(
      VirtualAlloc(nullptr, 0x150000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));

  if (!game)
  {
    return 4;
  }

  Global(0x100) = reinterpret_cast<DWORD>(&ObservedGetProcAddress);
  Global(0x104) = reinterpret_cast<DWORD>(&GetModuleHandleA);
  Global(0x108) = reinterpret_cast<DWORD>(&StretchBlt);
  *payloadConfig = {0x46534650, 1, reinterpret_cast<DWORD>(game), 0x100, 0x104, 0x108};
  const BYTE originalStub[] = {0xff, 0x74, 0x24, 0x04, 0xb8, 0,    0,   0,
                               0,    0xff, 0xd0, 0x83, 0xc4, 0x04, 0xc3};
  std::memcpy(game + 0x50050, originalStub, sizeof(originalStub));
  Global(0x50055) = reinterpret_cast<DWORD>(&OriginalMovie);
  FlushInstructionCache(GetCurrentProcess(), game + 0x50050, sizeof(originalStub));
  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = ParentProcedure;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.lpszClassName = L"OwnedFury3MovieValidation";
  RegisterClassW(&windowClass);
  parentWindow = CreateWindowExW(0, windowClass.lpszClassName, L"Owned hidden movie fixture",
                                 WS_OVERLAPPEDWINDOW, 60, 60, 640, 400, nullptr, nullptr,
                                 windowClass.hInstance, nullptr);
  HWND otherWindow = CreateWindowExW(0, windowClass.lpszClassName, L"Owned second hidden parent",
                                     WS_OVERLAPPEDWINDOW, 80, 80, 320, 240, nullptr, nullptr,
                                     windowClass.hInstance, nullptr);
  Global(0x1329cc) = reinterpret_cast<DWORD>(parentWindow);
  char videoPath[MAX_PATH]{};
  WideCharToMultiByte(CP_ACP, 0, argv[2], -1, videoPath, MAX_PATH, nullptr, nullptr);
  char open[1024]{};
  sprintf_s(open, "open \"%s\" type avivideo alias mov parent %lu style child", videoPath,
            reinterpret_cast<DWORD>(parentWindow));
  currentStep = "open-avi";
  const bool opened = Command(open);
  Expect("owned-uncompressed-avi-opens", opened);

  if (!opened)
  {
    return WriteReport(argv[3]) ? 0 : 1;
  }

  std::string response;
  Expect("movie-child-handle-query", Command("status mov window handle", &response));
  childWindow = reinterpret_cast<HWND>(std::strtoul(response.c_str(), nullptr, 10));
  Expect("movie-child-belongs-to-hidden-parent", IsWindow(childWindow) &&
                                                     GetParent(childWindow) == parentWindow &&
                                                     !IsWindowVisible(parentWindow));
  Global(0x5fc7c) = reinterpret_cast<DWORD>(childWindow);
  Global(0x143048) = 1;
  childProcedure = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrW(childWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ChildProcedure)));
  const MCIDEVICEID device = mciGetDeviceIDA("mov");
  currentStep = "initial-resize";
  Expect("movie-time-format-set", Command("set mov time format frames"));
  Expect("movie-audio-present",
         Command("capability mov has audio", &response) && response == "true");
  Expect("movie-original-source",
         Command("where mov source", &response) && response == "0 0 160 120");
  Expect("windowed-resize-preserves-cdecl-abi", InvokeWithAbi(resizeMovie, parentWindow) == 0);
  Expect("windowed-full-child-destination-and-uncropped-source", FullDestination());
  Expect("windowed-success-never-calls-original", originalCalls == 0 && putCalls == 1);
  Expect("windowed-rendered-checkerboard-fills-client", RenderedFrameFillsClient());
  currentStep = "window-resize";
  autoResize = true;
  const unsigned beforeWide = putCalls;
  SetWindowPos(parentWindow, nullptr, 0, 0, 900, 350, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
  Expect("windowed-wide-resize-event-fills-client", FullDestination() && putCalls > beforeWide);
  SetWindowPos(parentWindow, nullptr, 0, 0, 300, 600, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
  Expect("windowed-tall-resize-event-fills-client", FullDestination());
  Expect("windowed-tall-rendered-checkerboard-fills-client", RenderedFrameFillsClient());
  const unsigned beforePutReentry = putCalls;
  reenterPut = true;
  resizeMovie(parentWindow);
  reenterPut = false;
  Expect("mci-reentry-is-bounded", putCalls == beforePutReentry + 1 && originalCalls == 0);
  reenterChild = true;
  SetWindowPos(parentWindow, nullptr, 0, 0, 600, 450, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
  reenterChild = false;
  Expect("child-size-reentry-is-bounded",
         childReentries > 0 && originalCalls == 0 && FullDestination());
  payloadState->busy = 1;
  const unsigned beforeBusy = putCalls;
  resizeMovie(parentWindow);
  Expect("fullscreen-transition-busy-does-not-block-movie",
         putCalls == beforeBusy + 1 && payloadState->busy == 1 && FullDestination());
  payloadState->busy = 0;
  currentStep = "fullscreen-toggle";
  RECT beforeFullscreen{};
  GetWindowRect(parentWindow, &beforeFullscreen);
  toggle();
  Expect("hidden-parent-enters-fullscreen",
         payloadState->active == 1 && !IsWindowVisible(parentWindow));
  Expect("fullscreen-resize-event-fills-client-and-source", FullDestination());
  Expect("fullscreen-rendered-checkerboard-fills-client", RenderedFrameFillsClient());
  toggle();
  RECT afterFullscreen{};
  GetWindowRect(parentWindow, &afterFullscreen);
  Expect("fullscreen-exit-restores-windowed-rectangle",
         payloadState->active == 0 && EqualRect(&beforeFullscreen, &afterFullscreen));
  Expect("fullscreen-exit-resizes-movie-to-restored-client", FullDestination());
  currentStep = "invalid-guards";
  autoResize = false;
  CheckGuard("null-parent-guard", nullptr);
  CheckGuard("different-owned-parent-guard", otherWindow);
  Global(0x143048) = 0;
  CheckGuard("closed-movie-guard", parentWindow);
  Global(0x143048) = 1;
  Global(0x5fc7c) = 0;
  CheckGuard("absent-child-guard", parentWindow);
  Global(0x5fc7c) = reinterpret_cast<DWORD>(otherWindow);
  CheckGuard("nonchild-window-guard", parentWindow);
  Global(0x5fc7c) = reinterpret_cast<DWORD>(childWindow);
  SetParent(childWindow, otherWindow);
  CheckGuard("mismatched-child-parent-guard", parentWindow);
  SetParent(childWindow, parentWindow);
  LONG style = GetWindowLongW(parentWindow, GWL_STYLE);
  SetWindowLongW(parentWindow, GWL_STYLE, style | WS_MINIMIZE);
  CheckGuard("minimized-parent-guard", parentWindow);
  SetWindowLongW(parentWindow, GWL_STYLE, style);
  SetWindowPos(parentWindow, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
  CheckGuard("zero-client-guard", parentWindow);
  SetWindowPos(parentWindow, nullptr, 0, 0, 600, 450, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
  currentStep = "failure-fallback";
  failPut = true;
  reenterOriginal = true;
  const unsigned beforeFailure = originalCalls;
  const unsigned beforeFailurePut = putCalls;
  Expect("failure-fallback-preserves-cdecl-abi", InvokeWithAbi(resizeMovie, parentWindow) == 0);
  Expect("mci-failure-forwards-once-to-guarded-original", originalCalls == beforeFailure + 1 &&
                                                              lastOriginalParent == parentWindow &&
                                                              putCalls == beforeFailurePut + 1);
  reenterOriginal = false;

  for (unsigned fault = 1; fault <= 3; ++fault)
  {
    sourceFault = fault;
    MoveWindow(childWindow, 10, 20, 100, 80, FALSE);
    RECT before{}, after{};
    GetWindowRect(childWindow, &before);
    const unsigned originalBefore = originalCalls;
    resizeMovie(parentWindow);
    GetWindowRect(childWindow, &after);
    const char *names[] = {"", "source-failure-restores-without-unsafe-original",
                           "malformed-source-restores-without-unsafe-original",
                           "zero-source-restores-without-unsafe-original"};
    Expect(names[fault], originalCalls == originalBefore && EqualRect(&before, &after));
  }

  sourceFault = 0;
  failPut = false;
  resizeMovie(parentWindow);
  Expect("valid-call-recovers-after-mci-failure", FullDestination());
  Expect("audio-still-present-after-resizing",
         Command("capability mov has audio", &response) && response == "true");
  Expect("seek-before-playback", Command("seek mov to start"));
  currentStep = "play-window-notify";
  Expect("play-with-original-callback-notify",
         Command("play mov window notify", nullptr, parentWindow));
  Pump(150);
  Expect("playback-started", Command("status mov mode", &response) && response == "playing");
  Expect("original-play-window-command-retains-full-destination", FullDestination());
  std::string initialPosition;
  Command("status mov position", &initialPosition);
  currentStep = "resize-during-playback";
  resizeMovie(parentWindow);
  autoResize = true;
  SetWindowPos(parentWindow, nullptr, 0, 0, 720, 380, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
  toggle();
  toggle();
  Expect("resizing-does-not-reopen-device", mciGetDeviceIDA("mov") == device);
  Expect("resizing-keeps-playing", Command("status mov mode", &response) && response == "playing");
  Pump(200);
  Expect("resizing-does-not-reset-play-position",
         Command("status mov position", &response) &&
             std::strtoul(response.c_str(), nullptr, 10) >
                 std::strtoul(initialPosition.c_str(), nullptr, 10));
  Expect("playing-frame-still-fills-client", FullDestination());
  currentStep = "playback-completion";
  Pump(3300);
  Expect("original-notify-delivered-on-completion",
         successfulNotifications == 1 && abortedNotifications == 0);
  Expect("completed-playback-stopped",
         Command("status mov mode", &response) && response == "stopped");
  autoResize = false;
  SetWindowLongPtrW(childWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(childProcedure));
  currentStep = "close-avi";
  Expect("movie-closes-normally", Command("close mov"));
  Global(0x143048) = 0;
  Global(0x5fc7c) = 0;
  Expect("owned-parent-never-shown", !IsWindowVisible(parentWindow) && rejectedShowCount == 0);
  Expect("movie-resize-does-not-activate-parent",
         payloadActivations == 0 && unexpectedActivations == 0);
  DestroyWindow(otherWindow);
  DestroyWindow(parentWindow);
  VirtualFree(game, 0, MEM_RELEASE);
  FreeLibrary(payload);

  return WriteReport(argv[3]) ? 0 : 1;
}
