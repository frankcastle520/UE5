// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Optional.h"

#include "ScriptableToolGroupTag.generated.h"

UCLASS(Abstract, Blueprintable, Const)
class SCRIPTABLETOOLSFRAMEWORK_API UScriptableToolGroupTag
	: public UObject
{
public:
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "General")
	FString Name;
};
