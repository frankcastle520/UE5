// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaTransitionEnums.h"
#include "AvaTransitionLayerCondition.h"
#include "AvaTransitionStateMatchCondition.generated.h"

USTRUCT()
struct FAvaTransitionStateMatchConditionInstanceData : public FAvaTransitionLayerConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Parameter")
	EAvaTransitionRunState TransitionState = EAvaTransitionRunState::Running;
};

USTRUCT(DisplayName="Check State of other Scene in Layer", Category="Transition Logic")
struct AVALANCHETRANSITION_API FAvaTransitionStateMatchCondition : public FAvaTransitionLayerCondition
{
	GENERATED_BODY()

	using FInstanceDataType = FAvaTransitionStateMatchConditionInstanceData;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FAvaTransitionStateMatchCondition() = default;
	virtual ~FAvaTransitionStateMatchCondition() override = default;
	FAvaTransitionStateMatchCondition(const FAvaTransitionStateMatchCondition&) = default;
	FAvaTransitionStateMatchCondition(FAvaTransitionStateMatchCondition&&) = default;
	FAvaTransitionStateMatchCondition& operator=(const FAvaTransitionStateMatchCondition&) = default;
	FAvaTransitionStateMatchCondition& operator=(FAvaTransitionStateMatchCondition&&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	//~ Begin FStateTreeNodeBase
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& InId, FStateTreeDataView InInstanceDataView, const IStateTreeBindingLookup& InBindingLookup, EStateTreeNodeFormatting InFormatting) const override;
#endif
	virtual const UStruct* GetInstanceDataType() const override { return FAvaTransitionStateMatchConditionInstanceData::StaticStruct(); }
	virtual void PostLoad(FStateTreeDataView InInstanceDataView) override;
	//~ End FStateTreeNodeBase

	//~ Begin FStateTreeConditionBase
	virtual bool TestCondition(FStateTreeExecutionContext& InContext) const override;
	//~ End FStateTreeConditionBase

	UE_DEPRECATED(5.5, "TransitionState has been moved to Instance Data")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the Instance Data TransitionState instead"))
	EAvaTransitionRunState TransitionState_DEPRECATED = EAvaTransitionRunState::Unknown;
};
