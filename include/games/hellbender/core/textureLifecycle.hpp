// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_TEXTURE_LIFECYCLE_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_TEXTURE_LIFECYCLE_HPP

#include <intrin.h>

#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedOr)
#pragma intrinsic(__readfsdword)

struct TextureLifecycleState
{
  DWORD serial, uploadSerial, frameSerial, frameEpoch, incomplete;
};

extern "C"
{
  TextureLifecycleState textureLifecycleState = {};
}

namespace TextureLifecycle
{
  static DWORD ReadCounter(const DWORD &counter)
  {
    return *reinterpret_cast<const volatile DWORD *>(&counter);
  }

  static bool Incomplete()
  {
    return ReadCounter(textureLifecycleState.incomplete) != 0;
  }

  // Never reuse an ownership generation after overflow or unbounded contention
  static DWORD Increment(DWORD &counter)
  {
    for (DWORD attempt = 0; attempt < 16; ++attempt)
    {
      const DWORD before = ReadCounter(counter);
      if (before == 0xffffffffu)
      {
        _InterlockedOr(reinterpret_cast<volatile long *>(&textureLifecycleState.incomplete), 1);
        return 0;
      }

      if (static_cast<DWORD>(_InterlockedCompareExchange(
              reinterpret_cast<volatile long *>(&counter), static_cast<long>(before + 1),
              static_cast<long>(before))) == before)
      {
        return before + 1;
      }
    }

    _InterlockedOr(reinterpret_cast<volatile long *>(&textureLifecycleState.incomplete), 2);
    return 0;
  }

  static DWORD Serial()
  {
    return ReadCounter(textureLifecycleState.serial);
  }

  static DWORD UploadSerial()
  {
    return ReadCounter(textureLifecycleState.uploadSerial);
  }

  static DWORD FrameSerial()
  {
    return ReadCounter(textureLifecycleState.frameSerial);
  }

  static DWORD FrameEpoch()
  {
    return ReadCounter(textureLifecycleState.frameEpoch);
  }

  static DWORD ThreadId()
  {
    return __readfsdword(0x24);
  }

  static DWORD BeginUpload()
  {
    return Increment(textureLifecycleState.uploadSerial);
  }
} // namespace TextureLifecycle

extern "C" void __cdecl AdvanceTextureLifecycle()
{
  if (Configured())
  {
    TextureLifecycle::Increment(textureLifecycleState.serial);
  }
}

extern "C" void __cdecl AdvanceTextureFrameEpoch()
{
  if (Configured())
  {
    TextureLifecycle::Increment(textureLifecycleState.frameEpoch);
  }
}

extern "C" void __cdecl AdvanceTextureFrame()
{
  if (Configured())
  {
    TextureLifecycle::Increment(textureLifecycleState.frameSerial);
  }
}

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_TEXTURE_LIFECYCLE_HPP
