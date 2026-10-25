// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_TEXTURE_LOAD_RECOVERY_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_TEXTURE_LOAD_RECOVERY_HPP

#include "textureUploadOwnership.hpp"

extern "C"
{
  TextureLoadState loadState = {};
}

namespace TextureLoadRecovery
{
  using Load = HRESULT(WINAPI *)(void *, void *);
  using Reference = ULONG(WINAPI *)(void *);

  struct Control
  {
    DWORD stage = 0, rejectionReason = 0, restoredThisCall = 0;
    DWORD sourceInitial = 0, destinationInitial = 0, restoreResult = 0;
    DWORD sourceAfter = 0, destinationAfter = 0;
  };

  struct Upload
  {
    const TextureUploadObservation &before;
    DWORD serial, lifecycle, thread, destination, source;

    bool Current() const
    {
      if (TextureLifecycle::Incomplete() || TextureLifecycle::Serial() != lifecycle ||
          TextureLifecycle::ThreadId() != thread)
      {
        return false;
      }

      TextureUploadObservation current;
      TextureUploadOwnership::Observe(current);
      return TextureUploadOwnership::EligibleMask(current, before, serial,
                                                  TextureLifecycle::UploadSerial()) ==
                 TextureUploadOwnership::eligibleRequired &&
             TextureUploadOwnership::IdentityMask(current, destination, source, before) ==
                 TextureUploadOwnership::identityRequired;
    }
  };

  struct BusyScope
  {
    BusyScope()
    {
      loadState.busy = 1;
    }

    ~BusyScope()
    {
      loadState.busy = 0;
    }
  };

  struct NativeError
  {
    decltype(&::GetLastError) get = Call(GetLastError, getLastError);
    decltype(&::SetLastError) set = Call(SetLastError, setLastError);
    DWORD value = get();

    ~NativeError()
    {
      // Retain the API target too, since a callback can invalidate configuration.
      set(value);
    }
  };

  // References outlive every added COM call, including a reentrant cleanup.
  // Releases run while the outer upload guard is still held.
  struct References
  {
    const Upload &upload;
    DWORD held[4] = {}, releases[4] = {}, count = 0;

    bool Acquire(DWORD object)
    {
      if (!upload.Current())
      {
        return false;
      }

      const auto add = reinterpret_cast<Reference>(TextureRecovery::Method(object, 4));
      held[count] = object;
      releases[count] = TextureRecovery::Method(object, 8);
      ++count;
      add(reinterpret_cast<void *>(object));

      return upload.Current();
    }

    bool ReleaseAll()
    {
      bool current = upload.Current();
      while (count)
      {
        const DWORD object = held[--count];
        reinterpret_cast<Reference>(releases[count])(reinterpret_cast<void *>(object));
        current = upload.Current() && current;
      }

      return current;
    }
  };

  static bool Methods(DWORD object, bool surface)
  {
    using namespace TextureRecovery;
    return Method(object, 4) && Method(object, 8) &&
           (surface ? (Method(object, 0x58) && Method(object, 0x60) && Method(object, 0x6c))
                    : Method(object, 0x18));
  }

  static HRESULT Rejected(Control &control, HRESULT result, DWORD reason)
  {
    control.rejectionReason = reason;
    return result != S_OK ? result : static_cast<HRESULT>(TextureRecovery::invalidState);
  }

  static HRESULT Query(const Upload &upload,
                       Control &control,
                       DWORD object,
                       DWORD offset,
                       DWORD stage,
                       DWORD &stored)
  {
    control.stage = stage;
    if (!upload.Current())
    {
      return Rejected(control, S_OK, 1);
    }

    if (offset == 0x6c)
    {
      control.restoredThisCall = 1;
    }

    const HRESULT result = TextureRecovery::SurfaceCall(object, offset);
    stored = result;
    if (!upload.Current())
    {
      return Rejected(control, result, 1);
    }

    return result;
  }

  static HRESULT
  Description(const Upload &upload, Control &control, DWORD object, DWORD stage, DWORD (&format)[8])
  {
    control.stage = stage;
    if (!upload.Current())
    {
      return Rejected(control, S_OK, 1);
    }

    DWORD &loss =
        object == upload.before.stagingSurface ? control.sourceAfter : control.destinationAfter;
    const HRESULT healthy = Query(upload, control, object, 0x60, stage, loss);

    if (healthy != S_OK)
    {
      return Rejected(control, healthy, control.rejectionReason ? control.rejectionReason : 2);
    }

    DWORD description[27];
    volatile DWORD *cleared = description;
    for (DWORD index = 0; index < 27; ++index)
    {
      cleared[index] = 0;
    }

    description[0] = sizeof(description);
    const HRESULT result = reinterpret_cast<TextureRecovery::SurfaceDescription>(
        TextureRecovery::Method(object, 0x58))(reinterpret_cast<void *>(object), description);

    if (!upload.Current())
    {
      return Rejected(control, result, 1);
    }

    if (result != S_OK)
    {
      return Rejected(control, result, 2);
    }

    if (description[2] != 64 || description[3] != 64 || description[18] != 32 ||
        !(description[19] & 0x40) || description[21] != 16)
    {
      return Rejected(control, S_OK, 3);
    }

    for (DWORD index = 0; index < 8; ++index)
    {
      format[index] = description[18 + index];
    }

    return S_OK;
  }

  // A staging Restore would discard converted source pixels and is forbidden.
  // The destination is checked and restored before any descriptor or Load call.
  static HRESULT Ready(const Upload &upload, Control &control, bool requireRestore)
  {
    using namespace TextureRecovery;
    DWORD &sourceResult = requireRestore ? control.sourceAfter : control.sourceInitial;
    DWORD &destinationResult =
        requireRestore ? control.destinationAfter : control.destinationInitial;
    HRESULT result = Query(upload, control, upload.before.stagingSurface, 0x60, 3, sourceResult);

    if (result != S_OK)
    {
      return Rejected(control, result, control.rejectionReason ? control.rejectionReason : 2);
    }

    result = Query(upload, control, upload.before.destinationSurface, 0x60, 4, destinationResult);
    if (static_cast<DWORD>(result) == surfaceLost && !control.rejectionReason)
    {
      if (control.restoredThisCall)
      {
        return Rejected(control, result, 4);
      }

      result =
          Query(upload, control, upload.before.destinationSurface, 0x6c, 5, control.restoreResult);
      if (result != S_OK)
      {
        return Rejected(control, result, control.rejectionReason ? control.rejectionReason : 2);
      }

      result = Query(upload, control, upload.before.destinationSurface, 0x60, 6,
                     control.destinationAfter);
      if (result != S_OK)
      {
        return Rejected(control, result, control.rejectionReason ? control.rejectionReason : 2);
      }

      result = Query(upload, control, upload.before.stagingSurface, 0x60, 7, control.sourceAfter);
      if (result != S_OK)
      {
        return Rejected(control, result, control.rejectionReason ? control.rejectionReason : 2);
      }
    }
    else if (result != S_OK)
    {
      return Rejected(control, result, control.rejectionReason ? control.rejectionReason : 2);
    }
    else if (requireRestore)
    {
      return Rejected(control, static_cast<HRESULT>(surfaceLost), 4);
    }

    DWORD sourceFormat[8], destinationFormat[8];
    result = Description(upload, control, upload.before.stagingSurface, 8, sourceFormat);
    if (result != S_OK)
    {
      return result;
    }

    result = Description(upload, control, upload.before.destinationSurface, 9, destinationFormat);
    if (result != S_OK)
    {
      return result;
    }

    for (DWORD index = 0; index < 8; ++index)
    {
      if (sourceFormat[index] != destinationFormat[index])
      {
        return Rejected(control, S_OK, 3);
      }
    }

    // Descriptor callbacks can pump messages. Check loss once more before Load.
    result = Query(upload, control, upload.before.stagingSurface, 0x60, 10, control.sourceAfter);
    if (result != S_OK)
    {
      return Rejected(control, result, control.rejectionReason ? control.rejectionReason : 2);
    }

    result = Query(upload, control, upload.before.destinationSurface, 0x60, 11,
                   control.destinationAfter);
    return result == S_OK
               ? result
               : Rejected(control, result, control.rejectionReason ? control.rejectionReason : 2);
  }

  static HRESULT Run(const Upload &upload, Control &control, Load load, NativeError &error)
  {
    HRESULT result = Ready(upload, control, false);

    // A destination can become lost after its initial healthy check, including
    // while a descriptor query pumps messages. Re-enter the readiness sequence
    // once only when a later public IsLost observed that exact destination loss.
    // The repeated sequence rechecks the source and requires persistent loss
    // before spending the same single Restore budget used by all upload paths.
    if (static_cast<DWORD>(result) == TextureRecovery::surfaceLost &&
        (control.stage == 9 || control.stage == 11) &&
        control.destinationAfter == TextureRecovery::surfaceLost && control.rejectionReason == 2 &&
        !control.restoredThisCall && upload.Current())
    {
      control.rejectionReason = 0;
      result = Ready(upload, control, true);
    }

    if (result != S_OK)
    {
      return result;
    }

    if (!upload.Current())
    {
      return Rejected(control, S_OK, 1);
    }

    control.stage = 12;

    // Added preflight COM calls must not change the LastError seen by native Load
    error.set(error.value);
    const HRESULT first =
        load(reinterpret_cast<void *>(upload.destination), reinterpret_cast<void *>(upload.source));
    error.value = error.get();
    TextureLifecycle::Increment(loadState.calls);
    loadState.initialResult = first;

    if (static_cast<DWORD>(first) == TextureRecovery::surfaceLost)
    {
      TextureLifecycle::Increment(loadState.lostResults);
    }

    if (!upload.Current())
    {
      return Rejected(control, first, 1);
    }

    if (static_cast<DWORD>(first) != TextureRecovery::surfaceLost || control.restoredThisCall)
    {
      return first;
    }

    // Preserve the former single post-Load retry only if preflight did not
    // already use this upload's one destination Restore.
    loadState.lastSlot = upload.before.slot;
    loadState.lastStage = 1;
    result = Ready(upload, control, true);
    loadState.sourceResult = control.sourceAfter;
    loadState.destinationResult = control.destinationAfter;
    loadState.restoreResult = control.restoreResult;

    if (control.restoredThisCall)
    {
      TextureLifecycle::Increment(loadState.attempts);
    }

    if (result != S_OK)
    {
      if (control.restoredThisCall)
      {
        TextureLifecycle::Increment(loadState.failures);
      }

      return first;
    }

    if (!upload.Current())
    {
      TextureLifecycle::Increment(loadState.failures);
      return Rejected(control, first, 1);
    }

    loadState.lastStage = 3;
    control.stage = 13;
    error.set(error.value);
    const HRESULT retry =
        load(reinterpret_cast<void *>(upload.destination), reinterpret_cast<void *>(upload.source));
    error.value = error.get();
    loadState.retryResult = retry;

    if (!upload.Current())
    {
      TextureLifecycle::Increment(loadState.failures);
      loadState.lastStage = 5;
      return Rejected(control, first, 1);
    }

    if (retry == S_OK)
    {
      TextureLifecycle::Increment(loadState.successes);
    }
    else
    {
      TextureLifecycle::Increment(loadState.failures);
    }

    loadState.lastStage = 4;
    return retry;
  }
} // namespace TextureLoadRecovery

extern "C" HRESULT WINAPI LoadTextureWithRecovery(void *destination, void *source)
{
  using namespace TextureLoadRecovery;
  using namespace TextureRecovery;

  // Every wrapper entry invalidates an older staging owner, even passthrough.
  const DWORD serial = TextureLifecycle::BeginUpload();
  if (textureState.busy)
  {
    // The cache owner retained both object pairs before entering native CA90
    const HRESULT admitted = AdmitCacheLoad(reinterpret_cast<DWORD>(destination),
                                            reinterpret_cast<DWORD>(source), serial);

    if (admitted != S_OK)
    {
      return admitted;
    }

    const auto load = reinterpret_cast<Load>(Method(reinterpret_cast<DWORD>(destination), 0x18));
    return load(destination, source);
  }

  const auto load = reinterpret_cast<Load>(Method(reinterpret_cast<DWORD>(destination), 0x18));
  TextureUploadObservation before;
  TextureUploadOwnership::Observe(before);
  const Upload upload{before,
                      serial,
                      TextureLifecycle::Serial(),
                      TextureLifecycle::ThreadId(),
                      reinterpret_cast<DWORD>(destination),
                      reinterpret_cast<DWORD>(source)};

  if (!Configured() || loadState.busy || !upload.Current() || !Methods(upload.destination, false) ||
      !Methods(upload.source, false) || !Methods(before.destinationSurface, true) ||
      !Methods(before.stagingSurface, true))
  {
    return load(destination, source);
  }

  TextureLoadRecovery::BusyScope busy;
  NativeError error;
  Control control;
  TextureLoadRecovery::References references{upload};
  HRESULT result = static_cast<HRESULT>(invalidState);

  if (references.Acquire(upload.destination) && references.Acquire(upload.source) &&
      references.Acquire(before.destinationSurface) && references.Acquire(before.stagingSurface))
  {
    result = Run(upload, control, load, error);
  }

  if (!references.ReleaseAll())
  {
    result = Rejected(control, result, 1);
  }

  return result;
}

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_TEXTURE_LOAD_RECOVERY_HPP
