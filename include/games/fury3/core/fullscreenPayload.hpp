// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_FURY3_CORE_FULLSCREEN_PAYLOAD_HPP
#define HAVGAMEPATCHER_GAMES_FURY3_CORE_FULLSCREEN_PAYLOAD_HPP

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

struct FullscreenConfig
{
  DWORD magic;
  DWORD version;
  DWORD gameBase;

  DWORD getProcAddressIatRva;
  DWORD getModuleHandleAIatRva;
  DWORD stretchBltIatRva;
};

struct FullscreenState
{
  DWORD busy;
  DWORD active;
  DWORD savedValid;
  DWORD stage;
  DWORD failureCount;
  DWORD lastError;

  HWND window;
  DWORD threadId;
  LONG style;
  LONG exStyle;
  HMENU menu;
  WINDOWPLACEMENT placement;
  RECT monitor;

  DWORD enterAttempts;
  DWORD enterSuccesses;
  DWORD leaveAttempts;
  DWORD leaveSuccesses;
  DWORD frames;
  DWORD barFailures;
  DWORD lastFailureStage;
};

static_assert(sizeof(void *) == 4, "The transplanted payload requires x86.");
static_assert(sizeof(FullscreenConfig) == 24, "Unexpected config layout.");
static_assert(sizeof(FullscreenState) == 132, "Unexpected state layout.");

extern "C"
{
  extern FullscreenConfig config;
  extern FullscreenState state;

  void __cdecl ToggleFullscreen();
  void __cdecl PollJoystick();
  void __cdecl PrepareMainMenu();
  void __cdecl LeaveMainMenu();
  void __cdecl ResizeMovie(HWND parent);

  BOOL WINAPI StretchFrame(HDC destination,
                           int x,
                           int y,
                           int width,
                           int height,
                           HDC source,
                           int sourceX,
                           int sourceY,
                           int sourceWidth,
                           int sourceHeight,
                           DWORD operation);

  DWORD WINAPI UpdateMenuCheck(HMENU menu, UINT item, UINT flags);
}

#endif // HAVGAMEPATCHER_GAMES_FURY3_CORE_FULLSCREEN_PAYLOAD_HPP
