// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreePropertyFunctionBase.h"
#include "StateTreeFloatPropertyFunctions.generated.h"

struct FStateTreeExecutionContext;

USTRUCT()
struct STATETREEMODULE_API FStateTreeFloatCombinaisonPropertyFunctionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	float Left = 0.f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	float Right = 0.f;

	UPROPERTY(EditAnywhere, Category = Output)
	float Result = 0.f;
};

/**
 * Add two floats.
 */
USTRUCT(meta=(DisplayName = "Add", Category = "Math|Float"))
struct STATETREEMODULE_API FStateTreeAddFloatPropertyFunction : public FStateTreePropertyFunctionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeFloatCombinaisonPropertyFunctionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void Execute(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const;
#endif
};

/**
 * Subtract right float from left float.
 */
USTRUCT(meta=(DisplayName = "Subtract", Category = "Math|Float"))
struct STATETREEMODULE_API FStateTreeSubtractFloatPropertyFunction : public FStateTreePropertyFunctionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeFloatCombinaisonPropertyFunctionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void Execute(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const;
#endif
};

/**
 * Multiply the two given float.
 */
USTRUCT(meta=(DisplayName = "Multiply", Category = "Math|Float"))
struct STATETREEMODULE_API FStateTreeMultiplyFloatPropertyFunction : public FStateTreePropertyFunctionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeFloatCombinaisonPropertyFunctionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void Execute(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const;
#endif
};

/**
 * Divide left float by right float.
 */
USTRUCT(meta=(DisplayName = "Divide", Category = "Math|Float"))
struct STATETREEMODULE_API FStateTreeDivideFloatPropertyFunction : public FStateTreePropertyFunctionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeFloatCombinaisonPropertyFunctionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void Execute(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const;
#endif
};

USTRUCT()
struct STATETREEMODULE_API FStateTreeSingleFloatPropertyFunctionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	float Input = 0.f;

	UPROPERTY(EditAnywhere, Category = Output)
	float Result = 0.f;
};

/**
 * Invert the given float.
 */
USTRUCT(meta=(DisplayName = "Invert", Category = "Math|Float"))
struct STATETREEMODULE_API FStateTreeInvertFloatPropertyFunction : public FStateTreePropertyFunctionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeSingleFloatPropertyFunctionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void Execute(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const;
#endif
};

/**
 * Gives the absolute value of the given float.
 */
USTRUCT(meta=(DisplayName = "Absolute", Category = "Math|Float"))
struct STATETREEMODULE_API FStateTreeAbsoluteFloatPropertyFunction : public FStateTreePropertyFunctionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeSingleFloatPropertyFunctionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void Execute(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const;
#endif
};
