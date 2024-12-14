// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVMPropertyPath.h"
#include "MVVMBlueprintPin.h"
#include "View/MVVMViewTypes.h"
#include "Types/MVVMConditionOperation.h"

#include "MVVMBlueprintViewCondition.generated.h"

struct FEdGraphEditAction;
class UEdGraph;
class UK2Node;
class UMVVMK2Node_IsConditionValid;
class UWidgetBlueprint;

/**
 * Binding for an event that MVVM will listen too. Does not imply 
 * the MVVM graph itself will use events.
 * 
 * Ex: UButton::OnClick 
 */
UCLASS(Within = MVVMBlueprintView)
class MODELVIEWVIEWMODELBLUEPRINT_API UMVVMBlueprintViewCondition : public UObject
{
	GENERATED_BODY()

public:
	enum class EMessageType : uint8
	{
		Info,
		Warning,
		Error
	};

	struct FMessage
	{
		FText MessageText;
		EMessageType MessageType;
	};

public:
	/** Whether the event is enabled or disabled by default. The instance may enable the event at runtime. */
	UPROPERTY(EditAnywhere, Category = "Viewmodel")
	bool bEnabled = true;

	/** The event is visible in the editor, but is not compiled and cannot be used at runtime. */
	UPROPERTY(EditAnywhere, Category = "Viewmodel")
	bool bCompile = true;

public:
	const FMVVMBlueprintPropertyPath& GetConditionPath() const
	{
		return ConditionPath;
	}
	void SetConditionPath(FMVVMBlueprintPropertyPath ConditionPath);

	const FMVVMBlueprintPropertyPath& GetDestinationPath() const
	{
		return DestinationPath;
	}
	void SetDestinationPath(FMVVMBlueprintPropertyPath DestinationPath);

	EMVVMConditionOperation GetOperation() const
	{
		return ConditionOperation;
	}

	void SetOperation(EMVVMConditionOperation Operation);

	float GetOperationValue()
	{
		return Value;
	}
	void SetOperationValue(float NewValue);

	float GetOperationMaxValue()
	{
		return MaxValue;
	}
	void SetOperationMaxValue(float NewMaxValue);

public:
	UEdGraph* GetWrapperGraph() const
	{
		return CachedWrapperGraph;
	}

	FName GetWrapperGraphName() const
	{
		return GraphName;
	}

	enum ERemoveWrapperGraphParam
	{
		RemoveConversionFunctionCurrentValues, // when removing or changing the conversion function, we want to remove all the conversion function parameters
		LeaveConversionFunctionCurrentValues // when we remove the wrapper graph because the destination path or condition properties are changed, we want to keep the conversion function parameters
	};
	void RemoveWrapperGraph(ERemoveWrapperGraphParam ActionForCurrentValues = RemoveConversionFunctionCurrentValues);

	UK2Node* GetWrapperDestinationNode() const
	{
		return CachedWrapperDestinationNode;
	}

	UEdGraph* GetOrCreateWrapperGraph();

public:
	TArrayView<const FMVVMBlueprintPin> GetPins() const
	{
		return SavedPins;
	}

	/** Generates SavedPins from the wrapper graph, if it exists. */
	void SavePinValues();
	/** Keep the orphaned pins. Add the missing pins. */
	void UpdatePinValues();
	/** Keep the orphaned pins. Add the missing pins. */
	bool HasOrphanedPin() const;
	/** Condition sources are tested at runtime to check if they are valid. */
	void UpdateConditionKey(FMVVMViewClass_ConditionKey ConditionKey);

	UEdGraphPin* GetOrCreateGraphPin(const FMVVMBlueprintPinId& Pin);

	FMVVMBlueprintPropertyPath GetPinPath(const FMVVMBlueprintPinId& Pin) const;
	void SetPinPath(const FMVVMBlueprintPinId& Pin, const FMVVMBlueprintPropertyPath& Path);
	// To set a pin when loading the asset (no graph generation)
	void SetPinPathNoGraphGeneration(const FMVVMBlueprintPinId& Pin, const FMVVMBlueprintPropertyPath& Path);

	FSimpleMulticastDelegate OnWrapperGraphModified;

public:
	TArray<FText> GetCompilationMessages(EMessageType InMessageType) const;
	bool HasCompilationMessage(EMessageType InMessageType) const;
	void AddCompilationToBinding(FMessage MessageToAdd) const;
	void ResetCompilationMessages();

public:
	/** 
	 * Get a string that identifies this event. 
	 */
	FText GetDisplayName(bool bUseDisplayName) const;

	/**
	 * Get a string that identifies this event and is specifically formatted for search. 
	 * This includes the display name and variable name of all fields and widgets, as well as all function keywords.
	 * For use in the UI, use GetDisplayNameString()
	 */
	FString GetSearchableString() const;

public:
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChainEvent) override;

private:
	const UFunction* GetDestinationSignature() const;
	void HandleGraphChanged(const FEdGraphEditAction& Action);
	void HandleUserDefinedPinRenamed(UK2Node* InNode, FName OldPinName, FName NewPinName);
	UWidgetBlueprint* GetWidgetBlueprintInternal() const;
	void SetCachedWrapperGraphInternal(UEdGraph* Graph, UK2Node* Node, UMVVMK2Node_IsConditionValid* SourceNode);
	UEdGraph* CreateWrapperGraphInternal();
	void LoadPinValuesInternal();
	void UpdateConditionKeyInternal();

private:
	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FMVVMBlueprintPropertyPath ConditionPath;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	EMVVMConditionOperation ConditionOperation;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	float Value;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	float MaxValue;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FMVVMBlueprintPropertyPath DestinationPath;

	/**
	 * The pin that are modified and we saved data.
	 * The data may not be modified. We used the default value of the K2Node in that case.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	TArray<FMVVMBlueprintPin> SavedPins;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FName GraphName;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FMVVMViewClass_ConditionKey ConditionKey;

	mutable TArray<FMessage> Messages;
	bool bLoadingPins = false;

	UPROPERTY(Transient, DuplicateTransient)
	mutable TObjectPtr<UEdGraph> CachedWrapperGraph;

	UPROPERTY(Transient, DuplicateTransient)
	mutable TObjectPtr<UK2Node> CachedWrapperDestinationNode;

	UPROPERTY(Transient, DuplicateTransient)
	mutable TObjectPtr<UMVVMK2Node_IsConditionValid> CachedConditionValidNode;

	FDelegateHandle OnGraphChangedHandle;
	FDelegateHandle OnUserDefinedPinRenamedHandle;

	bool bNeedsToRegenerateChildren;
};
