// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Param/ParamType.h"

struct FRigVMCompileSettings;
class IPropertyHandle;
class URigVMController;
class URigVMGraph;
class URigVMNode;
class URigVMPin;
struct FAnimNextVariableBindingData;
template<typename T> struct TInstancedStruct;

namespace UE::AnimNext::UncookedOnly
{
	class FModule;
	struct FUtils;
}

namespace UE::AnimNext::Editor
{
	class FVariableBindingPropertyCustomization;
}

namespace UE::AnimNext::UncookedOnly
{

// Info about a variable binding gleaned from FindBindingInfo and ForEachBinding
struct FVariableBindingInfo
{
	// The binding's type
	FAnimNextParamType Type;
	// Display name for editor
	FText DisplayName;
	// Tooltip to display in editor
	FText Tooltip;
	// Function used to access this binding, if any
	const UFunction* Function = nullptr;
	// Property for this binding, if any
	const FProperty* Property = nullptr;
	// Whether this binding is safe to be accessed on worker threads
	bool bThreadSafe = false;
};

// Interface used in editor/uncooked situations to determine the characteristics of a variable binding
class IVariableBindingType
{
public:
	virtual ~IVariableBindingType() = default;

protected:
	struct FBindingGraphInput
	{
		// Name of the target variable that this binding is bound to
		FName VariableName;
		// CPPType of the variable
		FString CPPType;
		// CPPTypeObject of the variable
		TObjectPtr<UObject> CPPTypeObject;
		// Binding data of the type that this processor is registered against
		TConstStructView<FAnimNextVariableBindingData> BindingData;
	};

	struct FBindingGraphFragmentArgs
	{
		// The event (e.g. FRigUnit_AnimNextExecuteBindings) that is currently being processed
		UScriptStruct* Event;
		// All inputs, corresponding to variables
		TConstArrayView<FBindingGraphInput> Inputs;
		// Controller to use for instantiation 
		URigVMController* Controller;
		// Graph to instantiate nodes into
		URigVMGraph* BindingGraph;
		// The exec pin of the last node that was instantiated, for chaining 
		URigVMPin* ExecTail;
		// The current spawn location, useful for making user-readable graphs
		FVector2D CurrentLocation;
	};
	
private:
	// Create a widget used to edit the binding (displayed in a submenu from a combobox)
	virtual TSharedRef<SWidget> CreateEditWidget(const TSharedRef<IPropertyHandle>& InPropertyHandle, const FAnimNextParamType& InType) const = 0;

	// Get the display text for the specified instance ID
	virtual FText GetDisplayText(TConstStructView<FAnimNextVariableBindingData> InBindingData) const = 0;

	// Get the tooltip text for the specified instance ID
	virtual FText GetTooltipText(TConstStructView<FAnimNextVariableBindingData> InBindingData) const = 0;

	// Transforms the inputs into graph fragments. Called to convert variable bindings (derived from FAnimNextVariableBindingData) into intermediate
	// RigVM graphs for consumption by the compiler  
	virtual void BuildBindingGraphFragment(const FRigVMCompileSettings& InSettings, const FBindingGraphFragmentArgs& InArgs, URigVMPin*& OutExecTail, FVector2D& OutLocation) const = 0;

	friend class FModule;
	friend struct FUtils;
	friend class Editor::FVariableBindingPropertyCustomization;
};

}