// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"

#include "InterchangeAssetUserData.generated.h"

/** Asset user data that can be used with Interchange on Actors and other objects  */
UCLASS(BlueprintType, meta = (ScriptName = "InterchangeUserData", DisplayName = "Interchange User Data"))
class INTERCHANGEIMPORT_API UInterchangeAssetUserData : public UAssetUserData
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Interchange User Data", meta = (ScriptName = "Metadata", DisplayName = "Metadata"))
	TMap<FString, FString> MetaData;
};