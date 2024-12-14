// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeInstanceData.h"
#include "StateTreeReference.h"
#include "StateTreeTaskBase.h"

#include "StateTreeRunParallelStateTreeTask.generated.h"

USTRUCT()
struct FStateTreeRunParallelStateTreeTaskInstanceData
{
	GENERATED_BODY()

	/** State tree and parameters that will be run when this task is started. */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta=(SchemaCanBeOverriden))
	FStateTreeReference StateTree;

	UPROPERTY(Transient)
	FStateTreeInstanceData TreeInstanceData;

	UPROPERTY(Transient)
	TObjectPtr<const UStateTree> RunningStateTree = nullptr;
};

/**
* Task that will run another state tree in the current state while allowing the current tree to continue selection and process of child state.
* It will succeed, fail or run depending on the result of the parallel tree.
* Less efficient then Linked Asset state, it has the advantage of allowing multiple trees to run in parallel.
*/
USTRUCT(meta = (DisplayName = "Run Parallel Tree", Category = "Common"))
struct STATETREEMODULE_API FStateTreeRunParallelStateTreeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FStateTreeRunParallelStateTreeTaskInstanceData;
	
	FStateTreeRunParallelStateTreeTask();

#if WITH_EDITORONLY_DATA
	// Sets event handling priority
	void SetEventHandlingPriority(const EStateTreeTransitionPriority NewPriority)
	{
		EventHandlingPriority = NewPriority;
	}
#endif	
	
protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void TriggerTransitions(FStateTreeExecutionContext& Context) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual EDataValidationResult Compile(FStateTreeDataView InstanceDataView, TArray<FText>& ValidationMessages) override;
	virtual void PostEditInstanceDataChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent, FStateTreeDataView InstanceDataView) override;
	virtual void PostLoad(FStateTreeDataView InstanceDataView) override;
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
	virtual FName GetIconName() const override
	{
		return FName("StateTreeEditorStyle|Node.RunParallel");
	}
	virtual FColor GetIconColor() const override
	{
		return UE::StateTree::Colors::Grey;
	}
#endif // WITH_EDITOR

	const FStateTreeReference& GetStateTreeToRun(FStateTreeExecutionContext& Context, FInstanceDataType& InstanceData) const;

	/** If set the task will look at the linked state tree override to replace the state tree it's running. */
	UPROPERTY(EditAnywhere, Category = Parameter)
	FGameplayTag StateTreeOverrideTag;

#if WITH_EDITORONLY_DATA
	/**
	 * At what priority the events should be handled in the parallel State Tree.
	 * If set to 'Normal' the order of the States in the State Tree will define the handling order.
	 * If the priority is set to Low, the main tree is let to handle the transitions first.
	 * If set to High or above, the parallel tree has change to handle events first.
	 * If multiple tasks has same priority, the State order of the States defines the handling order.
	 * The tree handling order is: States and handle from leaf to root, tasks before and handled before transitions per State.
	 */
	UPROPERTY(EditAnywhere, Category = Parameter)
	EStateTreeTransitionPriority EventHandlingPriority = EStateTreeTransitionPriority::Normal;
#endif // WITH_EDITORONLY_DATA	
};