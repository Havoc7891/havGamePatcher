// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_FLUSH_OWNERSHIP_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_FLUSH_OWNERSHIP_HPP

struct FlushOwnerState
{
  volatile LONG busy;
  DWORD generation, reentries, invalidations, incomplete, active, activeValid;
  DWORD ownerThread, ownerBase, entryStack, nativeReturn, activeGeneration;
  DWORD rawCalls, nativeReturned, nativeBool, rawResult, rawSurface;
  DWORD entryDrawLock, afterDrawLock, branchKind, originalTarget, continuation, rawOwned;
};

static FlushOwnerState flushOwnerState = {};

extern "C" void __cdecl BeginCleanupReadinessOwner(DWORD token);
extern "C" void __cdecl EndCleanupReadinessOwner(DWORD token);

namespace FlushOwnership
{
  struct Context
  {
    DWORD configured, sameOwner, restoreSuccesses, backbuffer;
  };

  static Context entry = {}, returned = {};
  static DWORD savedMagic = 0, savedVersion = 0;

  static void Increment(DWORD &value)
  {
    auto address = reinterpret_cast<volatile LONG *>(&value);
    for (DWORD attempt = 0; attempt < 8; ++attempt)
    {
      const DWORD before = static_cast<DWORD>(_InterlockedCompareExchange(address, 0, 0));
      if (before == 0xffffffffu)
      {
        break;
      }
      if (static_cast<DWORD>(_InterlockedCompareExchange(address, static_cast<LONG>(before + 1),
                                                         static_cast<LONG>(before))) == before)
      {
        return;
      }
    }
    _InterlockedOr(reinterpret_cast<volatile LONG *>(&flushOwnerState.incomplete), 1);
  }

  static void Invalidate()
  {
    flushOwnerState.activeValid = 0;
    Increment(flushOwnerState.invalidations);
  }

  struct Writer
  {
    bool held = _InterlockedCompareExchange(&flushOwnerState.busy, 1, 0) == 0;

    Writer()
    {
      if (!held)
      {
        Increment(flushOwnerState.reentries);
        Invalidate();
        _InterlockedOr(reinterpret_cast<volatile LONG *>(&flushOwnerState.incomplete), 1);
      }
    }

    ~Writer()
    {
      if (held)
      {
        _InterlockedExchange(&flushOwnerState.busy, 0);
      }
    }
  };

  static bool ImageConfigured(DWORD expectedBase)
  {
    return Configured() && config.gameBase == expectedBase && !(expectedBase & 0xfffu) &&
           expectedBase <= 0xffffffffu - readinessImageSize &&
           GameRva::backbuffer <= readinessImageSize - 4 &&
           readinessExecuteBuffer <= readinessImageSize - 4;
  }

  static bool ConfigurationUnchanged()
  {
    return ImageConfigured(flushOwnerState.ownerBase) && config.magic == savedMagic &&
           config.version == savedVersion;
  }

  static bool Owned(DWORD token)
  {
    const auto &s = flushOwnerState;
    return token && s.active && token == s.activeGeneration &&
           s.ownerThread == TextureLifecycle::ThreadId();
  }

  static void Capture(Context &value)
  {
    value.configured = ConfigurationUnchanged();
    value.sameOwner = value.configured && NormalFrameOwnership::SameOwner();
    value.restoreSuccesses = state.restoreSuccesses;
    value.backbuffer = value.configured ? Read(GameRva::backbuffer) : 0;
  }

  static void Classify()
  {
    auto &s = flushOwnerState;
    s.branchKind = 0;
    if (!s.nativeReturned || !s.activeValid)
    {
      return;
    }
    if (!s.rawCalls && !s.entryDrawLock && s.nativeBool == 1 && !s.afterDrawLock)
    {
      s.branchKind = 1;
    }
    else if (s.rawCalls == 1 && s.rawOwned)
    {
      if (s.rawResult == S_OK && s.nativeBool == 1 && !s.afterDrawLock)
      {
        s.branchKind = 2;
      }
      else if (s.rawResult == 0x88760248u && s.nativeBool == 1 && !s.afterDrawLock)
      {
        s.branchKind = 3;
      }
      else if (s.rawResult != S_OK && s.rawResult != 0x88760248u && !s.nativeBool)
      {
        s.branchKind = 4;
      }
    }
    if (!s.branchKind)
    {
      Invalidate();
    }
  }
} // namespace FlushOwnership

extern "C" DWORD __cdecl BeginOwnedFlush(const DWORD *registers, DWORD nativeContinuation)
{
  using namespace FlushOwnership;
  SurfaceRestoreOwnership::LastErrorScope error;
  Writer writer;
  if (!writer.held)
  {
    return 0;
  }
  auto &s = flushOwnerState;
  if (s.active)
  {
    Increment(s.reentries);
    Invalidate();
    return 0;
  }
  if (s.generation == 0xffffffffu)
  {
    s.incomplete = 1;
    return 0;
  }
  if (!ImageConfigured(config.gameBase))
  {
    return 0;
  }
  const DWORD stack = registers[3] <= 0xfffffff3u ? registers[3] + 12 : 0;
  DWORD nativeReturn = 0;
  if (!NormalFrameOwnership::StackRead(stack, &nativeReturn, 4) ||
      registers[10] != config.gameBase + readinessFlushEntry)
  {
    return 0;
  }
  savedMagic = config.magic;
  savedVersion = config.version;
  s.active = s.activeValid = 1;
  s.ownerThread = TextureLifecycle::ThreadId();
  s.ownerBase = config.gameBase;
  s.entryStack = stack;
  s.nativeReturn = nativeReturn;
  s.activeGeneration = ++s.generation;
  s.rawCalls = s.nativeReturned = s.nativeBool = s.rawResult = s.rawSurface = 0;
  s.afterDrawLock = s.branchKind = s.rawOwned = 0;
  s.originalTarget = registers[10];
  s.continuation = nativeContinuation;
  Capture(entry);
  s.entryDrawLock = Read(GameRva::drawLock);
  BeginCleanupReadinessOwner(s.activeGeneration);
  return s.activeGeneration;
}

extern "C" void __cdecl RecordOwnedUnlock(const DWORD *registers)
{
  using namespace FlushOwnership;
  SurfaceRestoreOwnership::LastErrorScope error;
  if (!flushOwnerState.active)
  {
    return;
  }
  Writer writer;
  if (!writer.held)
  {
    return;
  }
  auto &s = flushOwnerState;
  if (!Owned(s.activeGeneration))
  {
    Invalidate();
    return;
  }
  Increment(s.rawCalls);
  if (s.rawCalls != 1)
  {
    Invalidate();
    return;
  }
  const DWORD stack = registers[3] <= 0xfffffff7u ? registers[3] + 8 : 0;
  DWORD words[9];
  s.rawResult = registers[7];
  if (!ConfigurationUnchanged() || s.nativeReturned || s.entryStack < 0x20 ||
      stack != s.entryStack - 0x20 ||
      !NormalFrameOwnership::StackRead(stack, words, sizeof(words)) ||
      words[1] != s.ownerBase + readinessUnlockReturn || words[5] != s.continuation ||
      words[6] != s.activeGeneration || words[7] != s.originalTarget ||
      words[8] != s.nativeReturn || !words[2] || words[2] != entry.backbuffer)
  {
    Invalidate();
    return;
  }
  s.rawSurface = words[2];
  s.rawOwned = 1;
}

extern "C" void __cdecl AfterOwnedFlush(const DWORD *registers, DWORD token)
{
  using namespace FlushOwnership;
  SurfaceRestoreOwnership::LastErrorScope error;
  Writer writer;
  if (!writer.held || !Owned(token))
  {
    return;
  }
  auto &s = flushOwnerState;
  if (s.nativeReturned)
  {
    Invalidate();
    return;
  }
  s.nativeReturned = 1;
  s.nativeBool = registers[7];
  if (registers[3] > 0xfffffff3u || registers[3] + 12 != s.entryStack || !ConfigurationUnchanged())
  {
    Invalidate();
  }
  Capture(returned);
  s.afterDrawLock = ConfigurationUnchanged() ? Read(GameRva::drawLock) : 1;
  Classify();
}

extern "C" void __cdecl EndOwnedFlush(DWORD token)
{
  using namespace FlushOwnership;
  SurfaceRestoreOwnership::LastErrorScope error;
  Writer writer;
  if (!writer.held || !Owned(token))
  {
    return;
  }
  if (!ConfigurationUnchanged())
  {
    Invalidate();
  }
  EndCleanupReadinessOwner(token);
  flushOwnerState.active = 0;
}

// Replay the exact native CMP and leave the native Unlock result unchanged.
extern "C" __declspec(naked) void OwnedUnlockResultHook()
{
  __asm {
    push 0
    pushfd
    pushad
    mov ebp,esp
    mov eax,dword ptr [config+8]
    add eax,readinessUnlockResume
    mov [ebp+36],eax
    cld
    sub esp,528
    and esp,-16
    fxsave [esp]
    push ebp
    call RecordOwnedUnlock
    add esp,4
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    cmp eax,088760248h
    ret
  }
}

#endif
