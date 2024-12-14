// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TraitCore/TraitHandle.h"
#include "TraitCore/TraitSharedData.h"

#include "AnimNextAnimGraphTraitInterfacesTest.generated.h"

USTRUCT()
struct FTraitWithOneChildSharedData : public FAnimNextTraitSharedData
{
	GENERATED_BODY()

	UPROPERTY()
	FAnimNextTraitHandle Child;
};

USTRUCT()
struct FTraitWithChildrenSharedData : public FAnimNextTraitSharedData
{
	GENERATED_BODY()

	UPROPERTY()
	FAnimNextTraitHandle Children[2];
};
