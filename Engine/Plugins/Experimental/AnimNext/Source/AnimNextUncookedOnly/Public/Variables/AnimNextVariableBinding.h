// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextVariableBindingData.h"
#include "StructUtils/InstancedStruct.h"
#include "AnimNextVariableBinding.generated.h"

USTRUCT()
struct FAnimNextVariableBinding
{
	GENERATED_BODY()

	FAnimNextVariableBinding() = default;

	// Returns whether this binding is valid
	bool IsValid() const
	{
		return BindingData.IsValid() && BindingData.Get<FAnimNextVariableBindingData>().IsValid();
	}

	UPROPERTY(EditAnywhere, Category = "Binding", meta=(ExcludeBaseStruct))
	TInstancedStruct<FAnimNextVariableBindingData> BindingData;
};

