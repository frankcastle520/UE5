// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "QuixelAssetTypes.generated.h"

USTRUCT()
struct FSemanticTags
{
	GENERATED_BODY()

	UPROPERTY()
	FString Asset_Type;
};

USTRUCT()
struct FAssetMetaDataJson
{
	GENERATED_BODY()

	UPROPERTY()
	FString Id;

	UPROPERTY()
	TArray<FString> Categories;

	UPROPERTY()
	FSemanticTags SemanticTags;
};

class FQuixelAssetTypes
{
public:
	static TTuple<FString, FString> ExtractMeta(const FString& JsonFile);
};
