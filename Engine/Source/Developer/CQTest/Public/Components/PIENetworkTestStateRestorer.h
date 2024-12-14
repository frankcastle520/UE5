// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UObject/SoftObjectPath.h"
#include "Templates/SubclassOf.h"

class UWorld;
class AGameModeBase;

/**
 * Object which saves the state of the game before starting the test,
 * Then restores it after the test is complete.
 */
class CQTEST_API FPIENetworkTestStateRestorer
{
public:
	FPIENetworkTestStateRestorer() = default;

	/**
	 * Construct the PIENetworkTestStateRestorer.
	 *
	 * @param InGameInstanceClass - Current game instance prior to the test.
	 * @param InGameMode - Current game mode prior to the test.
	 */
	FPIENetworkTestStateRestorer(const FSoftClassPath InGameInstanceClass, TSubclassOf<AGameModeBase> InGameMode);

	/** Restore to the previous game instance and mode. */
	void Restore();

private:
	bool SetWasLoadedFlag = false;
	FSoftClassPath OriginalGameInstance = FSoftClassPath();
	TSubclassOf<AGameModeBase> OriginalGameMode = nullptr;
};
