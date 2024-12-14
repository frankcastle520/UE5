// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayInteractionsTypes.h"
#include "Annotations/SmartObjectSlotEntranceAnnotation.h"
#include "SmartObjectSubsystem.h"
#include "StateTreeTask_GetSlotEntranceTags.generated.h"

class USmartObjectSubsystem;
class UNavigationQueryFilter;

USTRUCT()
struct FStateTreeTask_GetSlotEntranceTags_InstanceData
{
	GENERATED_BODY()

	/** Handle to the slot entrance to get the tags from. */
	UPROPERTY(EditAnywhere, Category = "Input")
	FSmartObjectSlotEntranceHandle SlotEntranceHandle;

	/** Tags defined on the slot entrance. */
	UPROPERTY(EditAnywhere, Category = "Output")
	FGameplayTagContainer EntranceTags;
};

/**
 * Gets Gameplay Tags defined at specified Smart Object slot entrance.
 */
USTRUCT(meta = (DisplayName = "Get Slot Entrance Tags", Category="Gameplay Interactions|Smart Object"))
struct FStateTreeTask_GetSlotEntranceTags : public FGameplayInteractionStateTreeTask
{
	GENERATED_BODY()

	FStateTreeTask_GetSlotEntranceTags();
	
	using FInstanceDataType = FStateTreeTask_GetSlotEntranceTags_InstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
	virtual FName GetIconName() const override
	{
		return FName("StateTreeEditorStyle|Node.Tag");
	}
	virtual FColor GetIconColor() const override
	{
		return UE::StateTree::Colors::Blue;
	}
#endif
	bool UpdateResult(const FStateTreeExecutionContext& Context) const;

	/** Handle to retrieve USmartObjectSubsystem. */
	TStateTreeExternalDataHandle<USmartObjectSubsystem> SmartObjectSubsystemHandle;
};
