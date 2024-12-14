// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaRCControllerId.h"
#include "AvaTransitionEnums.h"
#include "Conditions/AvaTransitionCondition.h"
#include "AvaTransitionRCControllerMatchCondition.generated.h"

class UAvaSceneSubsystem;
class URCVirtualPropertyBase;
struct FAvaTransitionScene;

USTRUCT()
struct FAvaTransitionRCControllerMatchConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Parameter")
	FAvaRCControllerId ControllerId;

	UPROPERTY(EditAnywhere, Category="Parameter")
	EAvaTransitionComparisonResult ValueComparisonType = EAvaTransitionComparisonResult::None;
};

USTRUCT(DisplayName="Compare RC Controller Values", Category="Remote Control")
struct AVALANCHE_API FAvaTransitionRCControllerMatchCondition : public FAvaTransitionCondition
{
	GENERATED_BODY()

	using FInstanceDataType = FAvaTransitionRCControllerMatchConditionInstanceData;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FAvaTransitionRCControllerMatchCondition() = default;
	virtual ~FAvaTransitionRCControllerMatchCondition() override = default;
	FAvaTransitionRCControllerMatchCondition(const FAvaTransitionRCControllerMatchCondition&) = default;
	FAvaTransitionRCControllerMatchCondition(FAvaTransitionRCControllerMatchCondition&&) = default;
	FAvaTransitionRCControllerMatchCondition& operator=(const FAvaTransitionRCControllerMatchCondition&) = default;
	FAvaTransitionRCControllerMatchCondition& operator=(FAvaTransitionRCControllerMatchCondition&&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	//~ Begin FStateTreeNodeBase
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& InId, FStateTreeDataView InInstanceDataView, const IStateTreeBindingLookup& InBindingLookup, EStateTreeNodeFormatting InFormatting) const override;
#endif
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void PostLoad(FStateTreeDataView InInstanceDataView) override;
	virtual bool Link(FStateTreeLinker& InLinker) override;
	//~ End FStateTreeNodeBase

	//~ Begin FStateTreeConditionBase
	virtual bool TestCondition(FStateTreeExecutionContext& InContext) const override;
	//~ End FStateTreeConditionBase

	UE_DEPRECATED(5.5, "ControllerId has been moved to Instance Data")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the Instance Data ControllerId instead"))
	FAvaRCControllerId ControllerId_DEPRECATED;

	UE_DEPRECATED(5.5, "ValueComparisonType has been moved to Instance Data")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the Instance Data ValueComparisonType instead"))
	EAvaTransitionComparisonResult ValueComparisonType_DEPRECATED = EAvaTransitionComparisonResult::None;

	TStateTreeExternalDataHandle<UAvaSceneSubsystem> SceneSubsystemHandle;
};
