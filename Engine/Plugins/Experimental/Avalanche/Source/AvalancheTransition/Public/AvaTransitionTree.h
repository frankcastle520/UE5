// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaTagHandle.h"
#include "AvaTransitionEnums.h"
#include "StateTree.h"
#include "AvaTransitionTree.generated.h"

struct FAvaTransitionTask;

/**
 * Motion Design Transition Tree is a State Tree with the purpose of executing user-defined logic
 * when there's a Transition between multiple scenes in multiple layers.
 */
UCLASS(MinimalAPI, DisplayName = "Motion Design Transition Tree")
class UAvaTransitionTree : public UStateTree
{
	GENERATED_BODY()

public:
	AVALANCHETRANSITION_API FAvaTagHandle GetTransitionLayer() const;

	AVALANCHETRANSITION_API void SetTransitionLayer(FAvaTagHandle InTransitionLayer);

	AVALANCHETRANSITION_API bool IsEnabled() const;

	AVALANCHETRANSITION_API void SetEnabled(bool bInEnabled);

	/** Returns whether a Task of a given type exists and is enabled within an enabled state in the Transition Tree */
	AVALANCHETRANSITION_API bool ContainsTask(const UScriptStruct* InTaskStruct) const;

	/** Returns whether a Task of a given type exists and is enabled within an enabled state in the Transition Tree */
	template <class InTaskType UE_REQUIRES(std::is_base_of_v<FAvaTransitionTask, InTaskType>)>
	bool ContainsTask() const
	{
		return ContainsTask(InTaskType::StaticStruct());
	}

	void SetInstancingMode(EAvaTransitionInstancingMode InInstancingMode)
	{
		InstancingMode = InInstancingMode;
	}

	UFUNCTION(BlueprintPure, Category = "Transition Logic")
	EAvaTransitionInstancingMode GetInstancingMode() const
	{
		return InstancingMode;
	}

	static FName GetEnabledPropertyName()
	{
		return GET_MEMBER_NAME_CHECKED(UAvaTransitionTree, bEnabled);
	}

private:
	/** The Layer this Transition Logic Tree deals with */
	UPROPERTY()
	FAvaTagHandle TransitionLayer;

	/**
	 * Determines whether this Transition Logic is enabled, by default.
	 * Can be overriden by a Transition Instance to force the logic to run regardless
	 */
	UPROPERTY()
	bool bEnabled = true;

	UPROPERTY()
	EAvaTransitionInstancingMode InstancingMode = EAvaTransitionInstancingMode::New;
};
