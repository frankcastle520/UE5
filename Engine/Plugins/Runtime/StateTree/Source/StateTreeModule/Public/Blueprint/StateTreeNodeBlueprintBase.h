// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeExecutionTypes.h"
#include "StateTreeNodeBase.h"
#include "UObject/ObjectMacros.h"
#include "StateTreeNodeBlueprintBase.generated.h"

struct FStateTreeEvent;
struct FStateTreeEventQueue;
struct FStateTreeInstanceStorage;
struct FStateTreeLinker;
struct FStateTreeExecutionContext;
struct FStateTreeBlueprintPropertyRef;
class UStateTree;

UENUM()
enum class EStateTreeBlueprintPropertyCategory : uint8
{
	NotSet,
	Input,	
	Parameter,
	Output,
	ContextObject,
};


/** Struct use to copy external data to the Blueprint item instance, resolved during StateTree linking. */
struct STATETREEMODULE_API FStateTreeBlueprintExternalDataHandle
{
	const FProperty* Property = nullptr;
	FStateTreeExternalDataHandle Handle;
};


UCLASS(Abstract)
class STATETREEMODULE_API UStateTreeNodeBlueprintBase : public UObject
{
	GENERATED_BODY()

public:
	/** Sends event to the StateTree. */
	UFUNCTION(BlueprintCallable, Category = "StateTree", meta = (HideSelfPin = "true", DisplayName = "StateTree Send Event"))
	void SendEvent(const FStateTreeEvent& Event);

	/** Request state transition. */
	UFUNCTION(BlueprintCallable, Category = "StateTree", meta = (HideSelfPin = "true", DisplayName = "StateTree Request Transition"))
	void RequestTransition(const FStateTreeStateLink& TargetState, const EStateTreeTransitionPriority Priority = EStateTreeTransitionPriority::Normal);

	/** Returns a reference to selected property in State Tree. */
	UFUNCTION(CustomThunk)
	void GetPropertyReference(const FStateTreeBlueprintPropertyRef& PropertyRef) const;

	/** Returns true if reference to selected property in State Tree is accessible. */
	UFUNCTION()
	bool IsPropertyRefValid(const FStateTreeBlueprintPropertyRef& PropertyRef) const;

	/** @return text describing the property, either direct value or binding description. Used internally. */
	UFUNCTION(BlueprintCallable, Category = "StateTree", meta=( BlueprintInternalUseOnly="true" ))
	FText GetPropertyDescriptionByPropertyName(FName PropertyName) const;

#if WITH_EDITOR
	FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const;

	FName GetIconName() const
	{
		return IconName;
	}
	
	FColor GetIconColor() const
	{
		return IconColor;
	}
#endif
	
protected:

	/** Event to implement to get node description. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Get Description"))
	FText ReceiveGetDescription(EStateTreeNodeFormatting Formatting) const;

	virtual UWorld* GetWorld() const override;
	AActor* GetOwnerActor(const FStateTreeExecutionContext& Context) const;

	/** These methods are const as they set mutable variables and need to be called from a const method. */
	void SetCachedInstanceDataFromContext(const FStateTreeExecutionContext& Context) const;
	void ClearCachedInstanceData() const;
	
	UE_DEPRECATED(5.2, "Use SetCachedInstanceDataFromContext() instead.")
	void SetCachedEventQueueFromContext(const FStateTreeExecutionContext& Context) const { SetCachedInstanceDataFromContext(Context); }
	UE_DEPRECATED(5.2, "Use ClearCachedInstanceData() instead.")
	void ClearCachedEventQueue() const { ClearCachedInstanceData(); }
	
private:
	DECLARE_FUNCTION(execGetPropertyReference);

	void* GetMutablePtrToProperty(const FStateTreeBlueprintPropertyRef& PropertyRef, FProperty*& OutSourceProperty) const;
	
	/** Cached instance data while the node is active. */
	mutable TWeakPtr<FStateTreeInstanceStorage> WeakInstanceStorage;

	/** Cached owner while the node is active. */
	UPROPERTY()
	mutable TObjectPtr<UObject> CachedOwner = nullptr;

	/** Cached State Tree of owning execution frame. */
	UPROPERTY()
	mutable TObjectPtr<const UStateTree> CachedFrameStateTree = nullptr;

	/** Cached root state of owning execution frame. */
	mutable FStateTreeStateHandle CachedFrameRootState;

	/** Cached State where the node is processed on. */
	mutable FStateTreeStateHandle CachedState;

#if WITH_EDITORONLY_DATA
	/** Description of the node. */
	UPROPERTY(EditDefaultsOnly, Category="Description")
	FText Description;

	/**
	 * Name of the icon in format:
	 *		StyleSetName | StyleName [ | SmallStyleName | StatusOverlayStyleName]
	 *		SmallStyleName and StatusOverlayStyleName are optional.
	 *		Example: "StateTreeEditorStyle|Node.Animation"
	 */
	UPROPERTY(EditDefaultsOnly, Category="Description")
	FName IconName;

	/** Color of the icon. */
	UPROPERTY(EditDefaultsOnly, Category="Description")
	FColor IconColor = UE::StateTree::Colors::Grey;
#endif // 	WITH_EDITORONLY_DATA

#if WITH_EDITOR
	/** Cached values used during editor to make some BP nodes simpler to use. */
	static FGuid CachedNodeID;
	static const IStateTreeBindingLookup* CachedBindingLookup;
#endif	
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "StateTreeEvents.h"
#include "StateTreeExecutionContext.h"
#endif
