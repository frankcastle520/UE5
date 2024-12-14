// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaTransitionEnums.h"
#include "AvaTransitionLayerCondition.h"
#include "AvaTransitionSceneMatchCondition.generated.h"

USTRUCT()
struct FAvaTransitionSceneMatchConditionInstanceData : public FAvaTransitionLayerConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Parameter")
    EAvaTransitionComparisonResult SceneComparisonType = EAvaTransitionComparisonResult::None;
};

USTRUCT(DisplayName="Compare other Scene in Layer", Category="Transition Logic")
struct AVALANCHETRANSITION_API FAvaTransitionSceneMatchCondition : public FAvaTransitionLayerCondition
{
	GENERATED_BODY()

	using FInstanceDataType = FAvaTransitionSceneMatchConditionInstanceData;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FAvaTransitionSceneMatchCondition() = default;
	virtual ~FAvaTransitionSceneMatchCondition() override = default;
	FAvaTransitionSceneMatchCondition(const FAvaTransitionSceneMatchCondition&) = default;
	FAvaTransitionSceneMatchCondition(FAvaTransitionSceneMatchCondition&&) = default;
	FAvaTransitionSceneMatchCondition& operator=(const FAvaTransitionSceneMatchCondition&) = default;
	FAvaTransitionSceneMatchCondition& operator=(FAvaTransitionSceneMatchCondition&&) = default;
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

	UE_DEPRECATED(5.5, "SceneComparisonType has been moved to Instance Data")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the Instance Data SceneComparisonType instead"))
	EAvaTransitionComparisonResult SceneComparisonType_DEPRECATED = EAvaTransitionComparisonResult::None;
};
