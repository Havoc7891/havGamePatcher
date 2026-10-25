// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MODE_NATIVE_ADDRESSES_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MODE_NATIVE_ADDRESSES_HPP
#include "readinessNativeAddresses.hpp"
#if defined(HAVGAMEPATCHER_HELLBENDER_ENGLISH_GAME_ADDRESSES_HPP)
enum
{
  rendererNativeChildSetup = 0x2ac0,
  rendererNativeTextureTeardown = 0x2900,
  rendererNativeCpuAllocations = 0x1e59f0,
  rendererNativeQuality = 0x112614,
  modeNativeFullscreenSetup = 0x22b0,
  modeNativeFullscreenReturn = 0x3aaeb,
  modeNativeViewport = 0x100280,
  modeNativePalette = 0x10023c,
  modeNativePreTranslate = 0x3ba10,
  modeNativeInputPumpReturn = 0x3a330,
  modeNativeGetKeyState = 0x479f30,
  modeNativeFlipReturn = 0x3add4,
  modeNativePresent = 0x3acd0,
  modeNativeMenuPresentReturn = 0x3b323,
  modeNativeDraw = 0x38620,
  modeNativeViewVtable = 0xeb3f0,
  modeNativeBitBlt = 0x4799a8,
  modeNativeStretchBlt = 0x479a4c,
  modeNativeBitBltReturn = 0x387f6,
  modeNativeStretchBltReturn = 0x38831,
  modeNativeSourceDc = 0x10d7c0,
  modeNativeBitmap = 0x10d7c8,
  modeNativeBits = 0x178b38,
  modeNativeRenderBits = 0x1b3884,
  modeNativeLowResolution = 0x111f98
};
#elif defined(HAVGAMEPATCHER_HELLBENDER_FRENCH_GAME_ADDRESSES_HPP) ||                              \
    defined(HAVGAMEPATCHER_HELLBENDER_GERMAN_GAME_ADDRESSES_HPP)
enum
{
  rendererNativeChildSetup = 0xba50,
  rendererNativeTextureTeardown = 0xb890,
  rendererNativeCpuAllocations = 0x26b170,
  rendererNativeQuality = 0x1028cc,
  modeNativeFullscreenSetup = 0xb240,
  modeNativeFullscreenReturn = 0x246cb,
  modeNativeViewport = 0x1032f0,
  modeNativePalette = 0x1032ac,
  modeNativePreTranslate = 0x258a0,
  modeNativeInputPumpReturn = 0x23f10,
  modeNativeGetKeyState = 0x47d084,
  modeNativeFlipReturn = 0x249ac,
  modeNativeSourceDc = 0x104650,
  modeNativeBitmap = 0x104658,
  modeNativeBits = 0x1397f8,
  modeNativeRenderBits = 0x467fe4,
  modeNativeLowResolution = 0x1141a8,
  modeNativePresent = 0x248b0,
  modeNativeMenuPresentReturn = 0x24f09,
  modeNativeDraw = 0x48660,
  modeNativeViewVtable = 0xefc28,
  modeNativeBitBlt = 0x47c9b0,
  modeNativeBitBltReturn = 0x48836,
  modeNativeStretchBlt = 0x47c9f0,
  modeNativeStretchBltReturn = 0x48871
};
#else
#error A verified retail edition address header must be force-included
#endif
#endif
