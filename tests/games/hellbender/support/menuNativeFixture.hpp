// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_MENU_FIXTURE_NATIVE_ALIASES_HPP
#define HAVGAMEPATCHER_MENU_FIXTURE_NATIVE_ALIASES_HPP
#if defined(HAVGAMEPATCHER_HELLBENDER_ENGLISH_GAME_ADDRESSES_HPP)
constexpr DWORD fixtureAppVtable = 0xee4c0;
constexpr DWORD fixtureAppPresentSlot = 0xee584;
constexpr DWORD fixtureQueueSlot = 0xee560;
constexpr DWORD fixtureNewGameEntry = 0x55b80;
constexpr DWORD fixtureMultiplayerEntry = 0x55ba0;
#else
constexpr DWORD fixtureAppVtable = 0xede00;
constexpr DWORD fixtureAppPresentSlot = 0xedec4;
constexpr DWORD fixtureQueueSlot = 0xedea0;
constexpr DWORD fixtureNewGameEntry = 0x1efd0;
constexpr DWORD fixtureMultiplayerEntry = 0x1eff0;
#endif
#endif
