// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaTransitionEnums.h"
#include "Conditions/AvaTransitionCondition.h"
#include "AvaTransitionTypeMatchCondition.generated.h"

USTRUCT()
struct FAvaTransitionTypeMatchConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Parameter")
	EAvaTransitionType TransitionType = EAvaTransitionType::In;
};

USTRUCT(DisplayName="My Transition Type is", Category="Transition Logic")
struct AVALANCHETRANSITION_API FAvaTransitionTypeMatchCondition : public FAvaTransitionCondition
{
	GENERATED_BODY()

	using FInstanceDataType = FAvaTransitionTypeMatchConditionInstanceData;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FAvaTransitionTypeMatchCondition() = default;
	virtual ~FAvaTransitionTypeMatchCondition() override = default;
	FAvaTransitionTypeMatchCondition(const FAvaTransitionTypeMatchCondition&) = default;
	FAvaTransitionTypeMatchCondition(FAvaTransitionTypeMatchCondition&&) = default;
	FAvaTransitionTypeMatchCondition& operator=(const FAvaTransitionTypeMatchCondition&) = default;
	FAvaTransitionTypeMatchCondition& operator=(FAvaTransitionTypeMatchCondition&&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	//~ Begin FStateTreeNodeBase
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& InId, FStateTreeDataView InInstanceDataView, const IStateTreeBindingLookup& InBindingLookup, EStateTreeNodeFormatting InFormatting) const override;
#endif
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void PostLoad(FStateTreeDataView InInstanceDataView) override;
	//~ End FStateTreeNodeBase

	//~ Begin FStateTreeConditionBase
	virtual bool TestCondition(FStateTreeExecutionContext& InContext) const override;
	//~ End FStateTreeConditionBase

	UE_DEPRECATED(5.5, "TransitionType has been moved to Instance Data")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the Instance Data TransitionType instead"))
	EAvaTransitionType TransitionType_DEPRECATED = EAvaTransitionType::None;
};
