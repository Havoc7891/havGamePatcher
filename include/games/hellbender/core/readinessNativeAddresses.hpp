// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_READINESS_NATIVE_ADDRESSES_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_READINESS_NATIVE_ADDRESSES_HPP

// Verified native call sites and frame layout fields for each supported retail edition.
#if defined(HAVGAMEPATCHER_HELLBENDER_ENGLISH_GAME_ADDRESSES_HPP)
enum
{
  readinessFlushEntry = 0x2870,
  readinessFlushReturn = 0x3787,
  readinessCleanupEndReturn = 0x2659,
  readinessCleanupDispatchReturn = 0x6fd7e,
  readinessCleanupLoopReturn = 0x9fb92,
  readinessUnlockReturn = 0x28a4,
  readinessUnlockResume = 0x28e5,
  readinessOuterFrameReturn = 0x823c6,
  readinessExecuteBuffer = 0x100288,
  readinessNativePaused = 0x10d740,
  readinessNativeActive = 0x10d750,
  readinessImageSize = 0x4f2000
};
#elif defined(HAVGAMEPATCHER_HELLBENDER_FRENCH_GAME_ADDRESSES_HPP) ||                              \
    defined(HAVGAMEPATCHER_HELLBENDER_GERMAN_GAME_ADDRESSES_HPP)
enum
{
  readinessFlushEntry = 0xb800,
  readinessFlushReturn = 0xc827,
  readinessCleanupEndReturn = 0xb5e9,
  readinessCleanupDispatchReturn = 0x2a5be,
  readinessCleanupLoopReturn = 0x6222,
  readinessUnlockReturn = 0xb834,
  readinessUnlockResume = 0xb875,
  readinessOuterFrameReturn = 0x8e0dc,
  readinessExecuteBuffer = 0x1032f8,
  readinessNativePaused = 0x1045d0,
  readinessNativeActive = 0x1045e0,
#if defined(HAVGAMEPATCHER_HELLBENDER_FRENCH_GAME_ADDRESSES_HPP)
  readinessImageSize = 0x4f6000
#else
  readinessImageSize = 0x4f7000
#endif
};
#else
#error A verified retail edition address header must be force-included
#endif

#endif
