// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreePropertyFunctionBase.h"
#include "StateTreeIntervalPropertyFunctions.generated.h"

struct FStateTreeExecutionContext;

USTRUCT()
struct STATETREEMODULE_API FStateTreeMakeIntervalPropertyFunctionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	float Min = 0.f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	float Max = 1.f;

	UPROPERTY(EditAnywhere, Category = Output)
	FFloatInterval Result = FFloatInterval(0.f, 1.f);
};

/**
 * Make an Interval from two floats.
 */
USTRUCT(DisplayName = "Make Interval")
struct STATETREEMODULE_API FStateTreeMakeIntervalPropertyFunction : public FStateTreePropertyFunctionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeMakeIntervalPropertyFunctionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void Execute(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const;
#endif
};
