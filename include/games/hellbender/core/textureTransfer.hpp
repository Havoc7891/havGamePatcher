// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_TEXTURE_TRANSFER_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_TEXTURE_TRANSFER_HPP

#include <intrin.h>

// Keep a private copy of the native converter output until the corresponding Load.
// Reentrant calls invalidate ownership without touching another call's source bytes.
namespace TextureTransfer
{
  struct Stats
  {
    DWORD magic, version, size;
    volatile DWORD busy, generation, incompleteFlags, captures, loads, nestedEntries, staged;
    volatile DWORD admitted, copied, skipped, failed;

    DWORD readyGeneration, activeGeneration;
    volatile DWORD invalidations;

    DWORD reserved[15];
  };

  struct Row
  {
    DWORD value[32];
  };

  struct State
  {
    DWORD source[160];
    BYTE pixels[8192];
    DWORD held[4], heldCount;
    Row row;
  };

  extern "C"
  {
    Stats textureTransferStats = {0x48474354, 1, sizeof(Stats)};
    State textureTransferState = {};
  }

  // Direct TEB access preserves the native caller's error state without an API call
  static DWORD ThreadId()
  {
    return __readfsdword(0x24);
  }

  static DWORD LastError()
  {
    return __readfsdword(0x34);
  }

  static void SetError(DWORD value)
  {
    __writefsdword(0x34, value);
  }

  static void Leave()
  {
    _InterlockedExchange(reinterpret_cast<volatile LONG *>(&textureTransferStats.busy), 0);
  }

  static_assert(sizeof(Stats) == 128 && sizeof(State) == 8980, "Compact ABI");

  static DWORD Cas(volatile DWORD *address, DWORD old, DWORD value)
  {
    return static_cast<DWORD>(
        _InterlockedCompareExchange(reinterpret_cast<volatile LONG *>(address),
                                    static_cast<LONG>(value), static_cast<LONG>(old)));
  }

  static DWORD Increment(volatile DWORD &value)
  {
    DWORD old = value;
    for (DWORD retry = 0; retry < 16 && old < 0xfffffffeu; ++retry)
    {
      DWORD observed = Cas(&value, old, old + 1);
      if (observed == old)
      {
        return old + 1;
      }

      old = observed;
    }

    _InterlockedOr(reinterpret_cast<volatile LONG *>(&textureTransferStats.incompleteFlags),
                   old >= 0xfffffffeu ? 2 : 4);
    return 0;
  }

  static DWORD Field(DWORD offset)
  {
    return textureTransferState.source[offset / 4];
  }

  static DWORD Method(DWORD object, DWORD offset)
  {
    return *reinterpret_cast<DWORD *>(*reinterpret_cast<DWORD *>(object) + offset);
  }

  static bool Equal(const DWORD *a, const DWORD *b, DWORD words)
  {
    for (DWORD i = 0; i < words; ++i)
    {
      if (a[i] != b[i])
      {
        return false;
      }
    }

    return true;
  }

  static void Copy(BYTE *to, const BYTE *from, DWORD bytes)
  {
    for (DWORD i = 0; i < bytes; ++i)
    {
      reinterpret_cast<volatile BYTE *>(to)[i] = from[i];
    }
  }

  static DWORD Hash(const BYTE *bytes, DWORD count)
  {
    DWORD value = 2166136261u;
    for (DWORD i = 0; i < count; ++i)
    {
      value = (value ^ bytes[i]) * 16777619u;
    }

    return value;
  }

  static bool Current(DWORD serial, DWORD token)
  {
    if (!Configured() || TextureLifecycle::Incomplete() || textureTransferStats.incompleteFlags ||
        textureTransferStats.generation != token || !token || ThreadId() != Field(12) ||
        config.gameBase != Field(36) || TextureLifecycle::Serial() != Field(128) ||
        TextureLifecycle::UploadSerial() != serial || state.restoreSuccesses != Field(136) ||
        Read(GameRva::legacyDevice) != Field(216) ||
        Read(GameRva::textureSelectedSlot) != Field(64) ||
        Read(GameRva::textureCount) != Field(68) ||
        Read(GameRva::textureStagingInterface) != Field(84) ||
        Read(GameRva::textureStagingSurface) != Field(88) || Read(GameRva::textureLock) ||
        Read(GameRva::mode) != Field(196) || Read(GameRva::hardware) != Field(200) ||
        Read(GameRva::renderBpp) != Field(204) || Read(GameRva::textureSourceBase) != Field(100) ||
        Read(GameRva::textureSourceX) != Field(104) ||
        Read(GameRva::textureSourceY) != Field(108) || Read(GameRva::textureDimension) != 64 ||
        textureState.busy != Field(176) || state.busy != Field(144) ||
        Read(GameRva::mainMenuVisible) != Field(220) || Read(GameRva::gameStage) != Field(224) ||
        Read(GameRva::legacyVideo) != Field(228) || textureState.passes != Field(140) ||
        TextureLifecycle::FrameSerial() != Field(184) ||
        TextureLifecycle::FrameEpoch() != Field(188))
    {
      return false;
    }

    DWORD slot = Field(64);
    if (!Field(68) || Field(68) > 1024 || slot >= Field(68))
    {
      return false;
    }

    return Read(GameRva::textureHandles + slot * 4) == Field(72) &&
           Read(GameRva::textureInterfaceTable + slot * 4) == Field(76) &&
           Read(GameRva::textureSurfaceTable + slot * 4) == Field(80) &&
           Equal(reinterpret_cast<DWORD *>(config.gameBase + GameRva::textureGpuNames + slot * 20),
                 textureTransferState.source + 620 / 4, 5);
  }

  static bool UserSpan(DWORD address, DWORD bytes)
  {
    return address >= 65536 && bytes <= 0x7ffe0000u && address <= 0x7fff0000u - bytes;
  }

  static bool Snapshot(DWORD u, DWORD convertedEnd, DWORD token)
  {
    if (!Configured() || TextureLifecycle::Incomplete() || !UserSpan(u, 0x90))
    {
      return false;
    }

    DWORD *s = textureTransferState.source;
    for (DWORD i = 0; i < 160; ++i)
    {
      reinterpret_cast<volatile DWORD *>(s)[i] = 0;
    }

    s[3] = ThreadId();
    s[4] = u;
    s[5] = u;
    s[9] = config.gameBase;
    s[12] = *reinterpret_cast<DWORD *>(u + 0x8c);
    s[13] = *reinterpret_cast<DWORD *>(u + 0x1c);

    // Unused diagnostic-only source metadata stays zero in the maintained payload
    s[16] = Read(GameRva::textureSelectedSlot);
    s[17] = Read(GameRva::textureCount);
    s[21] = Read(GameRva::textureStagingInterface);
    s[22] = Read(GameRva::textureStagingSurface);
    s[23] = Read(GameRva::textureLock);
    s[24] = Read(GameRva::textureDimension);
    s[25] = Read(GameRva::textureSourceBase);
    s[26] = Read(GameRva::textureSourceX);
    s[27] = Read(GameRva::textureSourceY);

    s[32] = TextureLifecycle::Serial();
    s[33] = TextureLifecycle::UploadSerial();
    s[34] = state.restoreSuccesses;
    s[35] = textureState.passes;
    s[36] = state.busy;
    s[44] = textureState.busy;
    s[45] = loadState.busy;
    s[46] = TextureLifecycle::FrameSerial();
    s[47] = TextureLifecycle::FrameEpoch();
    s[48] = TextureLifecycle::ThreadId();

    s[49] = Read(GameRva::mode);
    s[50] = Read(GameRva::hardware);
    s[51] = Read(GameRva::renderBpp);
    s[52] = Read(GameRva::drawLock);
    s[53] = Read(GameRva::drawSession);
    s[54] = Read(GameRva::legacyDevice);
    s[55] = Read(GameRva::mainMenuVisible);
    s[56] = Read(GameRva::gameStage);
    s[57] = Read(GameRva::legacyVideo);

    DWORD slot = Field(64), count = Field(68);

    if (!count || count > 1024 || slot >= count || Field(92) || Field(96) != 64 ||
        Field(132) >= 0xfffffffeu || Field(208) > 1)
    {
      return false;
    }

    s[18] = Read(GameRva::textureHandles + slot * 4);
    s[19] = Read(GameRva::textureInterfaceTable + slot * 4);
    s[20] = Read(GameRva::textureSurfaceTable + slot * 4);

    if (!UserSpan(s[19], 4) || !UserSpan(s[20], 4) || !UserSpan(s[21], 4) || !UserSpan(s[22], 4) ||
        s[19] == s[21] || s[20] == s[22] || s[13] != s[22])
    {
      return false;
    }

    Copy(reinterpret_cast<BYTE *>(s) + 512, reinterpret_cast<const BYTE *>(u + 0x20), 108);
    Copy(reinterpret_cast<BYTE *>(s) + 620,
         reinterpret_cast<const BYTE *>(config.gameBase + GameRva::textureGpuNames + slot * 20),
         20);

    bool terminated = false;
    const BYTE *name = reinterpret_cast<BYTE *>(s) + 620;

    for (DWORD i = 0; i < 20; ++i)
    {
      if (!name[i])
      {
        terminated = true;
        break;
      }
    }

    DWORD *d = s + 128;
    if (!name[0] || !terminated || d[0] != 108 || d[2] != 64 || d[3] != 64 || d[4] != 128 ||
        d[18] != 32 || !(d[19] & 0x40) || d[21] != 16 || !UserSpan(d[9], 8192) ||
        convertedEnd != d[9] + 8192)
    {
      return false;
    }

    Copy(textureTransferState.pixels, reinterpret_cast<const BYTE *>(d[9]), 8192);
    return Current(Field(132), token);
  }

  static bool OwnSource(DWORD destination, DWORD source, DWORD u, DWORD token)
  {
    DWORD ready = textureTransferStats.readyGeneration;
    textureTransferStats.readyGeneration = 0;
    if (!Configured() || TextureLifecycle::Incomplete() || !token || ready != token - 1 || !ready ||
        !UserSpan(u, 0x90) || Field(12) != ThreadId() || Field(16) != u || Field(20) != u ||
        Field(48) != *reinterpret_cast<DWORD *>(u + 0x8c) ||
        Field(52) != *reinterpret_cast<DWORD *>(u + 0x1c) ||
        !Equal(textureTransferState.source + 128, reinterpret_cast<DWORD *>(u + 0x20), 27) ||
        destination != Field(76) || source != Field(84) || Read(GameRva::drawLock) ||
        !Current(Field(132), token))
    {
      return false;
    }

    return true;
  }

  static void FillRow(DWORD incomingError, DWORD token)
  {
    DWORD *r = textureTransferState.row.value;
    r[3] = Field(12);
    r[4] = Field(20);
    r[5] = Field(128);
    r[6] = Field(132);
    r[8] = Field(136);
    r[9] = Field(64);
    r[10] = Field(68);
    r[11] = Field(72);
    r[12] = Field(76);
    r[13] = Field(80);
    r[14] = Field(84);
    r[15] = Field(88);

    r[16] = token - 1;
    r[17] = token;

    r[23] = Hash(textureTransferState.pixels, 8192);
    r[24] = incomingError;
    r[26] = textureTransferStats.nestedEntries;
    r[27] = 128;
    r[31] = Field(184);
  }

  static bool Retain(DWORD object, DWORD serial, DWORD token)
  {
    if (!object || !Current(serial, token))
    {
      return false;
    }

    using Reference = DWORD(WINAPI *)(void *);
    reinterpret_cast<Reference>(Method(object, 4))(reinterpret_cast<void *>(object));
    textureTransferState.held[textureTransferState.heldCount++] = object;
    return Current(serial, token);
  }

  static bool Release(DWORD serial, DWORD token)
  {
    bool current = Current(serial, token);
    using Reference = DWORD(WINAPI *)(void *);
    while (textureTransferState.heldCount)
    {
      DWORD object = textureTransferState.held[--textureTransferState.heldCount];
      reinterpret_cast<Reference>(Method(object, 8))(reinterpret_cast<void *>(object));
      ++textureTransferState.row.value[30];
      current = Current(serial, token) && current;
    }

    return current;
  }

  static DWORD Correct(DWORD serial, DWORD token)
  {
    DWORD *r = textureTransferState.row.value;
    DWORD surface = Field(80);
    using Simple = DWORD(WINAPI *)(void *);
    using Lock = DWORD(WINAPI *)(void *, void *, DWORD *, DWORD, void *);
    using Unlock = DWORD(WINAPI *)(void *, void *);

    if (!Current(serial, token))
    {
      r[2] = 4;
      return 0;
    }

    r[19] = reinterpret_cast<Simple>(Method(surface, 0x60))(reinterpret_cast<void *>(surface));
    if (!Current(serial, token))
    {
      r[2] = 4;
      return 0;
    }

    if (r[19])
    {
      r[1] = 3;
      r[2] = 5;
      return r[19];
    }

    DWORD descriptor[27];
    for (DWORD i = 0; i < 27; ++i)
    {
      reinterpret_cast<volatile DWORD *>(descriptor)[i] = 0;
    }

    descriptor[0] = 108;
    r[20] = reinterpret_cast<Lock>(Method(surface, 0x64))(reinterpret_cast<void *>(surface),
                                                          nullptr, descriptor, 1, nullptr);

    if (r[20])
    {
      r[1] = 3;
      r[2] = 6;
      return r[20];
    }

    // Every successful Lock reaches the single Unlock below, including bad metadata.
    bool current = Current(serial, token);
    LONG pitch = static_cast<LONG>(descriptor[4]);
    r[22] = descriptor[4];
    DWORD pixels = descriptor[9];
    bool format = descriptor[0] == 108 && descriptor[2] == 64 && descriptor[3] == 64 &&
                  descriptor[18] == 32 && (descriptor[19] & 0x40) && descriptor[21] == 16 &&
                  Equal(descriptor + 18, textureTransferState.source + (512 + 72) / 4, 8);

    // The native converter supplies positive pitches. Unsupported layouts retain the native result.
    bool extent = pitch >= 128 && pitch <= 65536 && pixels >= 65536 && pixels < 0x7fff0000u &&
                  pixels <= 0x7fff0000u - static_cast<DWORD>(pitch) * 63 - 128;

    if (current && format && extent)
    {
      const BYTE *source = textureTransferState.pixels;
      for (DWORD row = 0; row < 64; ++row)
      {
        Copy(reinterpret_cast<BYTE *>(pixels + row * static_cast<DWORD>(pitch)), source + row * 128,
             128);
        ++r[28];
      }
    }
    else
    {
      r[2] = !current ? 4 : !format ? 7 : 8;
    }

    r[21] =
        reinterpret_cast<Unlock>(Method(surface, 0x80))(reinterpret_cast<void *>(surface), nullptr);
    if (r[21])
    {
      r[1] = 3;
      r[2] = 9;
      return r[21];
    }

    if (!Current(serial, token))
    {
      r[2] = 4;
      return 0;
    }

    if (r[28] == 64)
    {
      r[1] = 2;
      r[2] = 0;
    }

    return 0;
  }
} // namespace TextureTransfer

extern "C" void WINAPI CaptureTextureSource(DWORD u, DWORD convertedEnd)
{
  using namespace TextureTransfer;
  DWORD error = LastError();
  DWORD token = Increment(textureTransferStats.generation);
  Increment(textureTransferStats.captures);

  if (Cas(&textureTransferStats.busy, 0, 1) != 0)
  {
    Increment(textureTransferStats.nestedEntries);
    Increment(textureTransferStats.invalidations);
    SetError(error);
    return;
  }

  if (textureTransferStats.readyGeneration)
  {
    Increment(textureTransferStats.invalidations);
  }

  textureTransferStats.readyGeneration = 0;
  if (token && !textureTransferStats.incompleteFlags && Snapshot(u, convertedEnd, token))
  {
    textureTransferStats.readyGeneration = token;
    Increment(textureTransferStats.staged);
  }

  Leave();
  SetError(error);
}

extern "C" DWORD WINAPI TransferTexture(DWORD destination, DWORD source, DWORD u)
{
  using namespace TextureTransfer;

  DWORD incomingError = LastError();
  DWORD token = Increment(textureTransferStats.generation);
  Increment(textureTransferStats.loads);

  if (Cas(&textureTransferStats.busy, 0, 1) != 0)
  {
    Increment(textureTransferStats.nestedEntries);
    Increment(textureTransferStats.invalidations);
    Increment(textureTransferStats.skipped);
    SetError(incomingError);

    return static_cast<DWORD>(LoadTextureWithRecovery(reinterpret_cast<void *>(destination),
                                                      reinterpret_cast<void *>(source)));
  }

  textureTransferStats.activeGeneration = token;
  for (DWORD i = 0; i < 32; ++i)
  {
    reinterpret_cast<volatile DWORD *>(textureTransferState.row.value)[i] = 0;
  }

  textureTransferState.heldCount = 0;
  DWORD *r = textureTransferState.row.value;
  r[1] = 1;
  r[2] = 1;
  r[3] = ThreadId();
  r[4] = u;
  r[12] = destination;
  r[14] = source;

  bool owned = OwnSource(destination, source, u, token);
  DWORD serial = 0;

  if (owned)
  {
    FillRow(incomingError, token);
    serial = Field(132);
    owned = Retain(Field(76), serial, token) && Retain(Field(84), serial, token) &&
            Retain(Field(80), serial, token) && Retain(Field(88), serial, token);
    r[29] = textureTransferState.heldCount;

    if (!owned)
    {
      r[2] = 2;
    }
  }

  SetError(incomingError);
  DWORD result = static_cast<DWORD>(LoadTextureWithRecovery(reinterpret_cast<void *>(destination),
                                                            reinterpret_cast<void *>(source)));
  DWORD nativeError = LastError();

  r[18] = result;
  r[25] = nativeError;
  r[7] = TextureLifecycle::UploadSerial();

  if (owned && result == 0)
  {
    if (Current(serial + 1, token))
    {
      Increment(textureTransferStats.admitted);
      r[2] = 0;
      DWORD addedResult = Correct(serial + 1, token);

      if (addedResult)
      {
        result = addedResult;
      }
    }
    else
    {
      r[2] = 3;
    }
  }
  else if (owned)
  {
    r[2] = 10;
  }

  if (textureTransferState.heldCount && !Release(serial + 1, token) && r[1] == 2)
  {
    r[1] = 1;
    r[2] = 11;
  }

  if (r[2] == 2 || r[2] == 3 || r[2] == 4 || r[2] == 11)
  {
    Increment(textureTransferStats.invalidations);
  }

  if (r[1] == 2)
  {
    Increment(textureTransferStats.copied);
  }
  else if (r[1] == 3)
  {
    Increment(textureTransferStats.failed);
  }
  else
  {
    Increment(textureTransferStats.skipped);
  }

  textureTransferStats.activeGeneration = 0;
  SetError(nativeError);
  Leave();
  return result;
}

extern "C" __declspec(naked) void TextureSourceHook()
{
  __asm
  {
    pushfd
    pushad
    cld
    mov eax, fs:[0x34]
    push eax
    lea eax, [esp + 40]
    mov edx, [esp + 8]
    push edx
    push eax
    call CaptureTextureSource
    pop eax
    mov fs:[0x34], eax
    popad
    popfd
    mov eax, [esp + 0x1c]
    push eax
    jmp dword ptr [config + 100]
  }
}

extern "C" __declspec(naked) HRESULT WINAPI TextureLoadHook(void *, void *)
{
  __asm
      {
    push ebp
    mov ebp, esp
    pushfd
    cld
    lea eax, [ebp + 16]
    push eax
    push [ebp + 12]
    push [ebp + 8]
    call TransferTexture
    popfd
    leave
    test eax, eax
    ret 8
      }
}

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_TEXTURE_TRANSFER_HPP
