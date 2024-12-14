// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/AssetUserData.h"
#include "AvaPlayableAssetUserData.generated.h"

class UAvaPlayable;

/**
 * AssetUserData for AvaPlayable.
 *
 * This is strategically injected in asset instances to be able to retrieve
 * the corresponding playable directly.
 */
UCLASS()
class UAvaPlayableAssetUserData : public UAssetUserData
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UAvaPlayable> PlayableWeak;
};
