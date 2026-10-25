// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MENU_NATIVE_ADDRESSES_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_MENU_NATIVE_ADDRESSES_HPP
#if defined(HAVGAMEPATCHER_HELLBENDER_ENGLISH_GAME_ADDRESSES_HPP)
enum
{
  menuNativeViewVtable = 0xeb3f0,
  menuNativeBitmapVtable = 0xec3f0,
  menuNativeBitmapEntry = 0x3f900,
  menuNativeBitmapCaller = 0x387bf,
  menuNativeStretchImport = 0x479a4c,
  menuNativeStretchCaller = 0x3f992,
  menuNativeQueueEntry = 0x54b40,
  menuNativeQueueCaller = 0x39743,
  menuNativeResumeEntry = 0x4d800,
  menuNativeInvalidateImport = 0x479fa0
};
#elif defined(HAVGAMEPATCHER_HELLBENDER_FRENCH_GAME_ADDRESSES_HPP) ||                              \
    defined(HAVGAMEPATCHER_HELLBENDER_GERMAN_GAME_ADDRESSES_HPP)
enum
{
  menuNativeViewVtable = 0xefc28,
  menuNativeBitmapVtable = 0xeee30,
  menuNativeBitmapEntry = 0x2d4d0,
  menuNativeBitmapCaller = 0x487ff,
  menuNativeStretchImport = 0x47c9f0,
  menuNativeStretchCaller = 0x2d562,
  menuNativeQueueEntry = 0x1df70,
  menuNativeQueueCaller = 0x23313,
  menuNativeResumeEntry = 0x5470,
  menuNativeInvalidateImport = 0x47d014
};
#else
#error Menu completion requires a verified retail edition address header
#endif
#endif
