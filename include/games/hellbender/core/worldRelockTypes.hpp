// SPDX-License-Identifier: MIT
#ifndef HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_WORLD_RELOCK_TYPES_HPP
#define HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_WORLD_RELOCK_TYPES_HPP

#include "frameNativeAddresses.hpp"

enum
{
  relockNativeDecision = recoveryRelockDecision,
  relockNativeFatalResume = recoveryRelockFatalResume,
  relockNativeSuccessResume = recoveryRelockSuccessResume,
  relockNativeFrameGate = recoveryRelockFrameGate,
  relockNativeFrameResume = recoveryRelockFrameResume,
  relockNativeFrameValue = recoveryRelockFrameValue,
  relockNativeFatalMessage = recoveryRelockFatalMessage
};

struct WorldRelockState
{
  volatile LONG busy;
  DWORD sequence, observations, published, invalidations, reentries, incomplete, tokenValid;
  DWORD tokenGeneration, consumed, accepted, rejected, attempts, lastStage, lastReason, lastResult;
  DWORD lastError, tokenBase, tokenThread, tokenStack, tokenSurface, tokenDescriptor,
      tokenFrameGeneration, tokenLifecycle;
  DWORD tokenFrameSerial, tokenFrameEpoch, tokenRestoreEpoch, tokenOwnerSlot, tokenContinuation,
      tokenOuterReturn, decisionStack, gateStack;
  DWORD tokenPrimary, tokenDraw, tokenDevice, tokenExecute, tokenBeginCalls, tokenEndCalls,
      tokenBridgeFailures, reserved;
};

static_assert(sizeof(WorldRelockState) == 160, "World relock state ABI");

#endif // HAVGAMEPATCHER_GAMES_HELLBENDER_CORE_WORLD_RELOCK_TYPES_HPP
