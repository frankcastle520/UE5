// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaTagHandle.h"
#include "AvaTransitionCondition.h"
#include "AvaTransitionLayer.h"
#include "AvaTransitionLayerCondition.generated.h"

struct FAvaTransitionContext;

USTRUCT()
struct FAvaTransitionLayerConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Parameter")
	EAvaTransitionLayerCompareType LayerType = EAvaTransitionLayerCompareType::Same;

	UPROPERTY(EditAnywhere, Category="Parameter", meta=(EditCondition="LayerType==EAvaTransitionLayerCompareType::MatchingTag", EditConditionHides))
	FAvaTagHandle SpecificLayer;
};

USTRUCT(Category="Transition Logic", meta=(Hidden))
struct AVALANCHETRANSITION_API FAvaTransitionLayerCondition : public FAvaTransitionCondition
{
	GENERATED_BODY()

	using FInstanceDataType = FAvaTransitionLayerConditionInstanceData;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FAvaTransitionLayerCondition() = default;
	virtual ~FAvaTransitionLayerCondition() override = default;
	FAvaTransitionLayerCondition(const FAvaTransitionLayerCondition&) = default;
	FAvaTransitionLayerCondition(FAvaTransitionLayerCondition&&) = default;
	FAvaTransitionLayerCondition& operator=(const FAvaTransitionLayerCondition&) = default;
	FAvaTransitionLayerCondition& operator=(FAvaTransitionLayerCondition&&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	//~ Begin FStateTreeNodeBase
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& InId, FStateTreeDataView InInstanceDataView, const IStateTreeBindingLookup& InBindingLookup, EStateTreeNodeFormatting InFormatting) const override;
#endif
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void PostLoad(FStateTreeDataView InInstanceDataView) override;
	//~ End FStateTreeNodeBase

	/** Gets all the Behavior Instances that match the Layer Query. Always excludes the Instance belonging to this Transition */
	TArray<const FAvaTransitionBehaviorInstance*> QueryBehaviorInstances(const FStateTreeExecutionContext& InContext) const;

	UE_DEPRECATED(5.5, "LayerType has been moved to Instance Data")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the Instance Data LayerType instead"))
	EAvaTransitionLayerCompareType LayerType_DEPRECATED = EAvaTransitionLayerCompareType::None;

	UE_DEPRECATED(5.5, "SpecificLayer has been moved to Instance Data")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the Instance Data SpecificLayer instead"))
	FAvaTagHandle SpecificLayer_DEPRECATED;
};
