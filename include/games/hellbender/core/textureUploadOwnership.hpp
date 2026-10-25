// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_TEXTURE_UPLOAD_OWNERSHIP_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_TEXTURE_UPLOAD_OWNERSHIP_HPP

struct TextureUploadObservation
{
  DWORD validMask;
  DWORD restoreEpoch, texturePendingEpoch, textureCompletedEpoch;
  DWORD textureBusy, restoreBusy, transitionDepth, setupCleanupBusy, payloadBusy;
  DWORD missionDepth, ended, videoDepth, mainMenuDepth;
  DWORD mode, directDraw, fullscreenActive, hardware, renderBpp;
  DWORD drawLock, textureLock, drawSession, quit, mainMenuVisible, gameStage;
  DWORD drawObject, legacyDevice, primary, backbuffer;
  DWORD count, slot, destinationInterface, destinationSurface, handle;
  DWORD stagingInterface, stagingSurface;
  DWORD configMagic, configVersion, gameBase;
  BYTE name[20];
};

static_assert(sizeof(TextureUploadObservation) == 172, "Texture upload ownership fields");

namespace TextureUploadOwnership
{
  using Load = HRESULT(WINAPI *)(void *, void *);
  constexpr DWORD configuredFields = 1, selectedFields = 2, payloadFields = 4;
  constexpr DWORD eligibleRequired = 0x7fff, identityRequired = 0xff;
  constexpr DWORD legacyDevice = GameRva::legacyDevice, textureHandles = GameRva::textureHandles;

  static void Observe(TextureUploadObservation &value)
  {
    auto cleared = reinterpret_cast<volatile DWORD *>(&value);
    for (DWORD index = 0; index < sizeof(value) / sizeof(DWORD); ++index)
    {
      cleared[index] = 0;
    }

    value.configMagic = config.magic;
    value.configVersion = config.version;
    value.gameBase = config.gameBase;

    value.restoreEpoch = state.restoreSuccesses;
    value.texturePendingEpoch = textureState.pendingEpoch;
    value.textureCompletedEpoch = textureState.completedEpoch;
    value.textureBusy = textureState.busy;
    value.restoreBusy = state.restoreBusy;
    value.transitionDepth = state.transitionDepth;
    value.setupCleanupBusy = state.setupCleanupBusy;
    value.payloadBusy = state.busy;
    value.missionDepth = state.depth;
    value.ended = state.ended;
    value.videoDepth = state.videoDepth;
    value.mainMenuDepth = state.mainMenuDepth;
    value.validMask = payloadFields;

    // Bound native address arithmetic before observing the selected cache slot
    if (!Configured() || config.gameBase > 0xffffffffu - (GameRva::fullscreenActive + 4))
    {
      return;
    }

    value.validMask |= configuredFields;
    value.mode = Read(GameRva::mode);
    value.directDraw = Read(GameRva::directDraw);
    value.fullscreenActive = Read(GameRva::fullscreenActive);
    value.hardware = Read(GameRva::hardware);
    value.renderBpp = Read(GameRva::renderBpp);

    value.drawLock = Read(GameRva::drawLock);
    value.textureLock = Read(GameRva::textureLock);
    value.drawSession = Read(GameRva::drawSession);
    value.quit = Read(GameRva::quit);
    value.mainMenuVisible = Read(GameRva::mainMenuVisible);
    value.gameStage = Read(GameRva::gameStage);

    value.drawObject = Read(GameRva::drawObject);
    value.legacyDevice = Read(legacyDevice);
    value.primary = Read(GameRva::primary);
    value.backbuffer = Read(GameRva::backbuffer);

    value.count = Read(GameRva::textureCount);
    value.slot = Read(GameRva::textureSelectedSlot);
    value.stagingInterface = Read(GameRva::textureStagingInterface);
    value.stagingSurface = Read(GameRva::textureStagingSurface);

    if (!value.count || value.count > 1024 || value.slot >= value.count)
    {
      return;
    }

    value.destinationInterface = Read(GameRva::textureInterfaceTable + value.slot * 4);
    value.destinationSurface = Read(GameRva::textureSurfaceTable + value.slot * 4);
    value.handle = Read(textureHandles + value.slot * 4);

    const auto name = reinterpret_cast<const volatile BYTE *>(
        config.gameBase + GameRva::textureGpuNames + value.slot * 20);

    for (DWORD index = 0; index < sizeof(value.name); ++index)
    {
      value.name[index] = name[index];
    }

    value.validMask |= selectedFields;
  }

  static DWORD Changes(const TextureUploadObservation &before,
                       const TextureUploadObservation &after)
  {
    DWORD result = 0;
    if (before.restoreEpoch != after.restoreEpoch ||
        before.texturePendingEpoch != after.texturePendingEpoch ||
        before.textureCompletedEpoch != after.textureCompletedEpoch)
    {
      result |= 1;
    }

    if (before.drawObject != after.drawObject || before.legacyDevice != after.legacyDevice ||
        before.primary != after.primary || before.backbuffer != after.backbuffer)
    {
      result |= 2;
    }

    if (before.mode != after.mode || before.hardware != after.hardware ||
        before.renderBpp != after.renderBpp || before.directDraw != after.directDraw ||
        before.fullscreenActive != after.fullscreenActive || before.quit != after.quit ||
        before.missionDepth != after.missionDepth || before.ended != after.ended ||
        before.videoDepth != after.videoDepth || before.mainMenuDepth != after.mainMenuDepth ||
        before.mainMenuVisible != after.mainMenuVisible || before.gameStage != after.gameStage)
    {
      result |= 4;
    }

    if (before.count != after.count || before.slot != after.slot ||
        before.destinationInterface != after.destinationInterface ||
        before.destinationSurface != after.destinationSurface || before.handle != after.handle ||
        before.stagingInterface != after.stagingInterface ||
        before.stagingSurface != after.stagingSurface)
    {
      result |= 8;
    }

    if (before.configMagic != after.configMagic || before.configVersion != after.configVersion ||
        before.gameBase != after.gameBase || before.validMask != after.validMask)
    {
      result |= 16;
    }

    for (DWORD index = 0; index < sizeof(before.name); ++index)
    {
      if (before.name[index] != after.name[index])
      {
        result |= 32;
      }
    }

    if (before.textureBusy != after.textureBusy || before.restoreBusy != after.restoreBusy ||
        before.transitionDepth != after.transitionDepth ||
        before.setupCleanupBusy != after.setupCleanupBusy ||
        before.payloadBusy != after.payloadBusy || before.drawLock != after.drawLock ||
        before.textureLock != after.textureLock || before.drawSession != after.drawSession)
    {
      result |= 64;
    }

    return result;
  }

  static DWORD EligibleMask(const TextureUploadObservation &value,
                            const TextureUploadObservation &before,
                            DWORD serialBefore,
                            DWORD serialAfter)
  {
    if (TextureLifecycle::Incomplete() || !(value.validMask & configuredFields) ||
        !(before.validMask & configuredFields))
    {
      return 0;
    }

    const bool phaseStable = (Changes(before, value) & (4 | 16 | 64)) == 0;
    const bool epochStable = before.restoreEpoch == value.restoreEpoch &&
                             before.texturePendingEpoch == value.texturePendingEpoch &&
                             before.textureCompletedEpoch == value.textureCompletedEpoch;

    return ((!value.textureBusy) << 0) | ((!value.restoreBusy) << 1) |
           ((!value.transitionDepth) << 2) | ((!value.setupCleanupBusy) << 3) |
           ((!value.quit) << 4) | ((value.mode == 2) << 5) | ((value.directDraw != 0) << 6) |
           ((value.fullscreenActive != 0) << 7) | ((value.hardware != 0) << 8) |
           ((value.renderBpp == 8 || value.renderBpp == 16) << 9) | ((!value.drawLock) << 10) |
           ((!value.textureLock) << 11) | (phaseStable << 12) | (epochStable << 13) |
           ((serialBefore != 0 && serialBefore == serialAfter) << 14);
  }

  static DWORD IdentityMask(const TextureUploadObservation &value,
                            DWORD destination,
                            DWORD source,
                            const TextureUploadObservation &before)
  {
    if (!(value.validMask & configuredFields))
    {
      return 0;
    }

    DWORD result = (value.count != 0 && value.count <= 1024) ? 1 : 0;
    if (!(value.validMask & selectedFields))
    {
      return result;
    }

    result |= 2;
    result |= (destination == value.destinationInterface) << 2;
    result |= (source == value.stagingInterface) << 3;
    result |= (value.destinationSurface != 0) << 4;
    result |= (value.stagingSurface != 0) << 5;
    result |= (value.destinationSurface != value.stagingSurface) << 6;

    const bool unchanged = value.drawObject && value.legacyDevice && value.primary &&
                           value.backbuffer && (before.validMask & selectedFields) &&
                           (Changes(before, value) & (2 | 8 | 16 | 32)) == 0;
    result |= unchanged << 7;

    return result;
  }

} // namespace TextureUploadOwnership

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_TEXTURE_UPLOAD_OWNERSHIP_HPP
