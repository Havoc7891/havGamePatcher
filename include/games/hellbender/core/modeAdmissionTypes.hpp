// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MODE_ADMISSION_TYPES_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MODE_ADMISSION_TYPES_HPP

struct ModeAdmissionState
{
  DWORD incomplete;
  DWORD imageBase, app, frameObject, frame;
  DWORD thread, ownerGeneration, transitionDepth, inputDepth;
  DWORD transitionSerial, clockValid, completedAt, clockAddress;
  DWORD accepted, suppressedRepeat, suppressedActive, suppressedStale;
  DWORD suppressedReentry, passedOther, resets, clockFailures;
  DWORD completedTransitions, expiredBarriers;
};

static_assert(sizeof(ModeAdmissionState) == 92, "Mode admission state ABI");
static_assert(sizeof(MSG) == 28, "Native x86 MSG layout");
static_assert(offsetof(MSG, hwnd) == 0 && offsetof(MSG, message) == 4 &&
                  offsetof(MSG, wParam) == 8 && offsetof(MSG, lParam) == 12 &&
                  offsetof(MSG, time) == 16 && offsetof(MSG, pt) == 20,
              "Native message offsets");

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MODE_ADMISSION_TYPES_HPP
