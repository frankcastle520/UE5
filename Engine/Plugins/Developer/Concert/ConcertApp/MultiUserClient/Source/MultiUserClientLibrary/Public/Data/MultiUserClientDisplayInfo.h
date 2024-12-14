// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ConcertMessageData.h"

#include "MultiUserClientDisplayInfo.generated.h"

/** Blueprint copy of FConcertClientInfo. Can describe offline clients, too. */
USTRUCT(BlueprintType)
struct FMultiUserClientDisplayInfo
{
	GENERATED_BODY()

	/** Holds the display name of the user that owns this instance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Client Info")
	FString DisplayName;

	FMultiUserClientDisplayInfo() = default;
	explicit FMultiUserClientDisplayInfo(const FConcertClientInfo& ClientInfo)
		: DisplayName(ClientInfo.DisplayName)
	{}
};