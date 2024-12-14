// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IStateTreeSchemaProvider.h"
#include "StateTreeNodeBase.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditorTypes.h"
#include "StateTreeEvents.h"
#include "StateTreeState.generated.h"

class UStateTreeState;
class UStateTree;

/**
 * Editor representation of an event description.
 */
USTRUCT()
struct FStateTreeEventDesc
{
	GENERATED_BODY()

	FStateTreeEventDesc() = default;

	FStateTreeEventDesc(FGameplayTag InTag)
		: Tag(InTag)
	{}

	/** Event Tag. */
	UPROPERTY(EditDefaultsOnly, Category = "Event")
	FGameplayTag Tag;

	/** Event Payload Struct. */
	UPROPERTY(EditDefaultsOnly, Category = "Event")
	TObjectPtr<const UScriptStruct> PayloadStruct;

	/** If set to true, the event is consumed (later state selection cannot react to it) if state selection can be made. */
	UPROPERTY(EditDefaultsOnly, Category = "Event")
	bool bConsumeEventOnSelect = true;
	
	bool IsValid() const
	{
		return Tag.IsValid() || PayloadStruct;
	}

	FStateTreeEvent& GetTemporaryEvent()
	{
		TemporaryEvent.Tag = Tag;
		TemporaryEvent.Payload = FInstancedStruct(PayloadStruct);

		return TemporaryEvent;
	}

	bool operator==(const FStateTreeEventDesc& Other) const
	{
		return Tag == Other.Tag && PayloadStruct == Other.PayloadStruct;
	}

private:
	/** Temporary event used as a source value in bindings. */
	UPROPERTY(Transient)
	FStateTreeEvent TemporaryEvent;
};

/**
 * Editor representation of a transition in StateTree
 */
USTRUCT()
struct STATETREEEDITORMODULE_API FStateTreeTransition
{
	GENERATED_BODY()

	// Macro needed to avoid deprecation errors with members being copied or created.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FStateTreeTransition() = default;
	FStateTreeTransition(const EStateTreeTransitionTrigger InTrigger, const EStateTreeTransitionType InType, const UStateTreeState* InState = nullptr);
	FStateTreeTransition(const EStateTreeTransitionTrigger InTrigger, const FGameplayTag InEventTag, const EStateTreeTransitionType InType, const UStateTreeState* InState = nullptr);
	FStateTreeTransition(const FStateTreeTransition&) = default;
	FStateTreeTransition(FStateTreeTransition&&) = default;
	FStateTreeTransition& operator=(const FStateTreeTransition&) = default;
	FStateTreeTransition& operator=(FStateTreeTransition&&) = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	template<typename T, typename... TArgs>
	TStateTreeEditorNode<T>& AddCondition(TArgs&&... InArgs)
	{
		FStateTreeEditorNode& CondNode = Conditions.AddDefaulted_GetRef();
		CondNode.ID = FGuid::NewGuid();
		CondNode.Node.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		const FStateTreeNodeBase& Node = CondNode.Node.GetMutable<FStateTreeNodeBase>();
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Node.GetInstanceDataType()))
		{
			CondNode.Instance.InitializeAs(InstanceType);
		}
		return static_cast<TStateTreeEditorNode<T>&>(CondNode);
	}

	FGuid GetEventID() const
	{
		return FGuid::Combine(ID, FGuid::NewDeterministicGuid(TEXT("Event")));
	}

	void PostSerialize(const FArchive& Ar);

	/** When to try trigger the transition. */
	UPROPERTY(EditDefaultsOnly, Category = "Transition")
	EStateTreeTransitionTrigger Trigger = EStateTreeTransitionTrigger::OnStateCompleted;

	/** Defines the event required to be present during state selection for the transition to trigger. */
	UPROPERTY(EditDefaultsOnly, Category = "Transition", DisplayName = "Required Event")
	FStateTreeEventDesc RequiredEvent; 

	/** Transition target state. */
	UPROPERTY(EditDefaultsOnly, Category = "Transition", meta=(DisplayName="Transition To"))
	FStateTreeStateLink State;

	UPROPERTY(EditDefaultsOnly, Category = "Transition")
	FGuid ID;

	/**
	 * Transition priority when multiple transitions happen at the same time.
	 * During transition handling, the transitions are visited from leaf to root.
	 * The first visited transition, of highest priority, that leads to a state selection, will be activated.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Transition")
	EStateTreeTransitionPriority Priority = EStateTreeTransitionPriority::Normal;

	/** Delay the triggering of the transition. */
	UPROPERTY(EditDefaultsOnly, Category = "Transition")
	bool bDelayTransition = false;

	/** Transition delay duration in seconds. */
	UPROPERTY(EditDefaultsOnly, Category = "Transition", meta = (EditCondition = "bDelayTransition", UIMin = "0", ClampMin = "0", UIMax = "25", ClampMax = "25", ForceUnits="s"))
	float DelayDuration = 0.0f;

	/** Transition delay random variance in seconds. */
	UPROPERTY(EditDefaultsOnly, Category = "Transition", meta = (EditCondition = "bDelayTransition", UIMin = "0", ClampMin = "0", UIMax = "25", ClampMax = "25", ForceUnits="s"))
	float DelayRandomVariance = 0.0f;

	/** Expression of conditions that need to evaluate to true to allow transition to be triggered. */
	UPROPERTY(EditDefaultsOnly, Category = "Transition", meta = (BaseStruct = "/Script/StateTreeModule.StateTreeConditionBase", BaseClass = "/Script/StateTreeModule.StateTreeConditionBlueprintBase"))
	TArray<FStateTreeEditorNode> Conditions;

	/** True if the Transition is Enabled (i.e. not explicitly disabled in the asset). */
	UPROPERTY(EditDefaultsOnly, Category = "Debug")
	bool bTransitionEnabled = true;

#if WITH_EDITORONLY_DATA
	UE_DEPRECATED(5.5, "Use RequiredEvent.Tag instead.")
	UPROPERTY()
	FGameplayTag EventTag_DEPRECATED;
#endif // WITH_EDITORONLY_DATA
};

template<>
struct TStructOpsTypeTraits<FStateTreeTransition> : public TStructOpsTypeTraitsBase2<FStateTreeTransition>
{
	enum 
	{
		WithPostSerialize = true,
	};
};


USTRUCT()
struct STATETREEEDITORMODULE_API FStateTreeStateParameters
{
	GENERATED_BODY()

	void ResetParametersAndOverrides()
	{
		// Reset just the parameters, keep the bFixedLayout intact.
		Parameters.Reset();
		PropertyOverrides.Reset();
	}

	/** Removes overrides that do appear in Parameters. */
	void RemoveUnusedOverrides();
	
	UPROPERTY(EditDefaultsOnly, Category = Parameters)
	FInstancedPropertyBag Parameters;

	/** Overrides for parameters. */
	UPROPERTY()
	TArray<FGuid> PropertyOverrides;

	UPROPERTY(EditDefaultsOnly, Category = Parameters)
	bool bFixedLayout = false;

	UPROPERTY(EditDefaultsOnly, Category = Parameters, meta = (IgnoreForMemberInitializationTest))
	FGuid ID;
};

/**
 * Editor representation of a state in StateTree
 */
UCLASS(BlueprintType, EditInlineNew, CollapseCategories)
class STATETREEEDITORMODULE_API UStateTreeState : public UObject, public IStateTreeSchemaProvider
{
	GENERATED_BODY()

public:
	UStateTreeState(const FObjectInitializer& ObjectInitializer);
	virtual ~UStateTreeState() override;

	virtual void PostInitProperties() override;
	virtual void PreEditChange(FEditPropertyChain& PropertyAboutToChange) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;;
	void UpdateParametersFromLinkedSubtree();
	void OnTreeCompiled(const UStateTree& StateTree);

	const UStateTreeState* GetRootState() const;
	const UStateTreeState* GetNextSiblingState() const;
	const UStateTreeState* GetNextSelectableSiblingState() const;

	/** @return the path of the state as string. */
	FString GetPath() const;
	
	/** @return true if the property of specified ID is overridden. */
	bool IsParametersPropertyOverridden(const FGuid PropertyID) const
	{
		return Parameters.PropertyOverrides.Contains(PropertyID);
	}

	/** Sets the override status of specified property by ID. */
	void SetParametersPropertyOverridden(const FGuid PropertyID, const bool bIsOverridden);

	/** @returns Default parameters from linked state or asset). */
	const FInstancedPropertyBag* GetDefaultParameters() const;
	
	// StateTree Builder API
	/** @return state link to this state. */
	FStateTreeStateLink GetLinkToState() const;
	
	/** Adds child state with specified name. */
	UStateTreeState& AddChildState(const FName ChildName, const EStateTreeStateType StateType = EStateTreeStateType::State)
	{
		UStateTreeState* ChildState = NewObject<UStateTreeState>(this, FName(), RF_Transactional);
		check(ChildState);
		ChildState->Name = ChildName;
		ChildState->Parent = this;
		ChildState->Type = StateType;
		Children.Add(ChildState);
		return *ChildState;
	}

	/**
	 * Adds enter condition of specified type.
	 * @return reference to the new condition.
	 */
	template<typename T, typename... TArgs>
	TStateTreeEditorNode<T>& AddEnterCondition(TArgs&&... InArgs)
	{
		FStateTreeEditorNode& CondNode = EnterConditions.AddDefaulted_GetRef();
		CondNode.ID = FGuid::NewGuid();
		CondNode.Node.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		const FStateTreeNodeBase& Node = CondNode.Node.GetMutable<FStateTreeNodeBase>();
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Node.GetInstanceDataType()))
		{
			CondNode.Instance.InitializeAs(InstanceType);
		}
		return static_cast<TStateTreeEditorNode<T>&>(CondNode);
	}

	/**
	 * Adds Task of specified type.
	 * @return reference to the new Task.
	 */
	template<typename T, typename... TArgs>
	TStateTreeEditorNode<T>& AddTask(TArgs&&... InArgs)
	{
		FStateTreeEditorNode& TaskItem = Tasks.AddDefaulted_GetRef();
		TaskItem.ID = FGuid::NewGuid();
		TaskItem.Node.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		const FStateTreeNodeBase& Node = TaskItem.Node.GetMutable<FStateTreeNodeBase>();
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Node.GetInstanceDataType()))
		{
			TaskItem.Instance.InitializeAs(InstanceType);
		}
		return static_cast<TStateTreeEditorNode<T>&>(TaskItem);
	}

	/** Sets linked asset and updates parameters to match the linked asset. */
	void SetLinkedStateAsset(UStateTree* InLinkedAsset)
	{
		check(Type == EStateTreeStateType::LinkedAsset);
		LinkedAsset = InLinkedAsset;
		UpdateParametersFromLinkedSubtree();
	}
	
	/**
	 * Adds Transition.
	 * @return reference to the new Transition.
	 */
	FStateTreeTransition& AddTransition(const EStateTreeTransitionTrigger InTrigger, const EStateTreeTransitionType InType, const UStateTreeState* InState = nullptr)
	{
		FStateTreeTransition& Transition = Transitions.Emplace_GetRef(InTrigger, InType, InState);
		Transition.ID = FGuid::NewGuid();
		return Transition;
	}

	FStateTreeTransition& AddTransition(const EStateTreeTransitionTrigger InTrigger, const FGameplayTag InEventTag, const EStateTreeTransitionType InType, const UStateTreeState* InState = nullptr)
	{
		FStateTreeTransition& Transition = Transitions.Emplace_GetRef(InTrigger, InEventTag, InType, InState);
		Transition.ID = FGuid::NewGuid();
		return Transition;
	}

	FGuid GetEventID() const
	{
		return FGuid::Combine(ID, FGuid::NewDeterministicGuid(TEXT("Event")));
	}

	// ~StateTree Builder API

	// IStateTreeSchemaProvider API
	/**
	* @return Class of schema used by the state tree containing this state.
	*/
	virtual TSubclassOf<UStateTreeSchema> GetSchema() const override;

	// ~IStateTreeSchemaProvider API

	// Note: these properties are customized out in FStateTreeStateDetails, adding a new property might require to adjust the customization.
	
	/** Display name of the State */
	UPROPERTY(EditDefaultsOnly, Category = "State")
	FName Name;

	/** GameplayTag describing the State */
	UPROPERTY(EditDefaultsOnly, Category = "State")
	FGameplayTag Tag;

	/** Display color of the State */
	UPROPERTY(EditDefaultsOnly, Category = "State", DisplayName = "Color")
	FStateTreeEditorColorRef ColorRef;

	/** Type the State, allows e.g. states to be linked to other States. */
	UPROPERTY(EditDefaultsOnly, Category = "State")
	EStateTreeStateType Type = EStateTreeStateType::State;

	/** How to treat child states when this State is selected.  */
	UPROPERTY(EditDefaultsOnly, Category = "State")
	EStateTreeStateSelectionBehavior SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;

	/** Subtree to run as extension of this State. */
	UPROPERTY(EditDefaultsOnly, Category = "State", Meta=(DirectStatesOnly, SubtreesOnly))
	FStateTreeStateLink LinkedSubtree;

	/** Another State Tree asset to run as extension of this State. */
	UPROPERTY(EditDefaultsOnly, Category = "State")
	TObjectPtr<UStateTree> LinkedAsset = nullptr;

	/** Parameters of this state. If the state is linked to another state or asset, the parameters are for the linked state. */
	UPROPERTY(EditDefaultsOnly, Category = "State")
	FStateTreeStateParameters Parameters;

	/** Should state's required event and enter conditions be evaluated when transition leads directly to it's child. */
	UPROPERTY(EditDefaultsOnly, Category = "Enter Conditions")
	bool bCheckPrerequisitesWhenActivatingChildDirectly = true;

	UPROPERTY(EditDefaultsOnly, Category = "Enter Conditions", meta=(InlineEditConditionToggle))
	bool bHasRequiredEventToEnter = false;

	/** Defines the event required to be present during state selection for the state to be selected. */
	UPROPERTY(EditDefaultsOnly, Category = "Enter Conditions", meta = (EditCondition = "bHasRequiredEventToEnter"))
	FStateTreeEventDesc RequiredEventToEnter;
	
	/** Weight used to scale the normalized final utility score for this state */
	UPROPERTY(EditDefaultsOnly, Category = "Utility", meta=(ClampMin=0))
	float Weight = 1.f;

	/** Expression of enter conditions that needs to evaluate true to allow the state to be selected. */
	UPROPERTY(EditDefaultsOnly, Category = "Enter Conditions", meta = (BaseStruct = "/Script/StateTreeModule.StateTreeConditionBase", BaseClass = "/Script/StateTreeModule.StateTreeConditionBlueprintBase"))
	TArray<FStateTreeEditorNode> EnterConditions;

	UPROPERTY(EditDefaultsOnly, Category = "Tasks", meta = (BaseStruct = "/Script/StateTreeModule.StateTreeTaskBase", BaseClass = "/Script/StateTreeModule.StateTreeTaskBlueprintBase"))
	TArray<FStateTreeEditorNode> Tasks;

	/** Expression of enter conditions that needs to evaluate true to allow the state to be selected. */
	UPROPERTY(EditDefaultsOnly, Category = "Utility", meta = (BaseStruct = "/Script/StateTreeModule.StateTreeConsiderationBase", BaseClass = "/Script/StateTreeModule.StateTreeConsiderationBlueprintBase"))
	TArray<FStateTreeEditorNode> Considerations;

	// Single item used when schema calls for single task per state.
	UPROPERTY(EditDefaultsOnly, Category = "Task", meta = (BaseStruct = "/Script/StateTreeModule.StateTreeTaskBase", BaseClass = "/Script/StateTreeModule.StateTreeTaskBlueprintBase"))
	FStateTreeEditorNode SingleTask;

	UPROPERTY(EditDefaultsOnly, Category = "Transitions")
	TArray<FStateTreeTransition> Transitions;

	UPROPERTY()
	TArray<TObjectPtr<UStateTreeState>> Children;

	UPROPERTY(EditDefaultsOnly, Category = "State", meta = (IgnoreForMemberInitializationTest))
	FGuid ID;

	UPROPERTY(meta = (ExcludeFromHash))
	bool bExpanded = true;

	UPROPERTY(EditDefaultsOnly, Category = "State")
	bool bEnabled = true;

	UPROPERTY(meta = (ExcludeFromHash))
	TObjectPtr<UStateTreeState> Parent = nullptr;
};
