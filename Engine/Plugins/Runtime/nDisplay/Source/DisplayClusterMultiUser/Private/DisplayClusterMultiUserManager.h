// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertTransactionEvents.h"
#include "IConcertClientTransactionBridge.h"

struct FConcertTransactionFilterArgs;

/**
 * Manager for handling multi user transactions in nDisplay.
 */
class FDisplayClusterMultiUserManager
{
public:
	FDisplayClusterMultiUserManager();
	virtual ~FDisplayClusterMultiUserManager();

private:
	void OnApplyRemoteTransaction(ETransactionNotification Notification, const bool bIsSnapshot);
	ETransactionFilterResult ShouldObjectBeTransacted(const FConcertTransactionFilterArgs& FilterArgs);
};
