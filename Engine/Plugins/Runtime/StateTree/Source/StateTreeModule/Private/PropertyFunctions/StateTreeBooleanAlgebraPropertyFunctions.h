// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreePropertyFunctionBase.h"
#include "StateTreeBooleanAlgebraPropertyFunctions.generated.h"

struct FStateTreeExecutionContext;

USTRUCT()
struct STATETREEMODULE_API FStateTreeBooleanOperationPropertyFunctionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Param)
	bool bLeft = false;

	UPROPERTY(EditAnywhere, Category = Param)
	bool bRight = false;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bResult = false;
};

/**
 * Performs 'And' operation on two booleans.
 */
USTRUCT(meta=(DisplayName = "And", Category="Logic"))
struct STATETREEMODULE_API FStateTreeBooleanAndPropertyFunction : public FStateTreePropertyFunctionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBooleanOperationPropertyFunctionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void Execute(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const;
#endif
};

/**
 * Performs 'Or' operation on two booleans.
 */
USTRUCT(meta=(DisplayName = "Or", Category="Logic"))
struct STATETREEMODULE_API FStateTreeBooleanOrPropertyFunction : public FStateTreePropertyFunctionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBooleanOperationPropertyFunctionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void Execute(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const;
#endif
};

/**
 * Performs 'Exclusive Or' operation on two booleans.
 */
USTRUCT(meta=(DisplayName = "XOr", Category="Logic"))
struct STATETREEMODULE_API FStateTreeBooleanXOrPropertyFunction : public FStateTreePropertyFunctionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBooleanOperationPropertyFunctionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void Execute(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const;
#endif
};

USTRUCT()
struct STATETREEMODULE_API FStateTreeBooleanNotOperationPropertyFunctionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Param)
	bool bInput = false;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bResult = false;
};

/**
 * Performs 'Not' operation on a boolean.
 */
USTRUCT(meta=(DisplayName = "Not", Category="Logic"))
struct STATETREEMODULE_API FStateTreeBooleanNotPropertyFunction : public FStateTreePropertyFunctionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBooleanNotOperationPropertyFunctionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void Execute(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const;
#endif
};