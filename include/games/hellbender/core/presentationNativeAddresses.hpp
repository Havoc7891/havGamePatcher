// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_PRESENTATION_NATIVE_ADDRESSES_HPP
#define HAVGAMEPATCHER_PRESENTATION_NATIVE_ADDRESSES_HPP
#if defined(HAVGAMEPATCHER_HELLBENDER_ENGLISH_GAME_ADDRESSES_HPP)
enum
{
  presentationNativeFailure = 0x2296,
  presentationNativeFailureResume = 0x229b,
  presentationNativeFailureMessage = 0x1002c0,
  presentationGameReturn = 0x81cbe,
  presentationAppReturn = 0x6f30e,
  presentationWrapperReturn = 0x26295,
};
#elif defined(HAVGAMEPATCHER_HELLBENDER_FRENCH_GAME_ADDRESSES_HPP) ||                              \
    defined(HAVGAMEPATCHER_HELLBENDER_GERMAN_GAME_ADDRESSES_HPP)
enum
{
  presentationNativeFailure = 0xb226,
  presentationNativeFailureResume = 0xb22b,
  presentationNativeFailureMessage = 0x103330,
  presentationGameReturn = 0x8da03,
  presentationAppReturn = 0x29b3e,
  presentationWrapperReturn = 0x36a65,
};
#else
#error A verified retail edition address header is required
#endif
#endif
