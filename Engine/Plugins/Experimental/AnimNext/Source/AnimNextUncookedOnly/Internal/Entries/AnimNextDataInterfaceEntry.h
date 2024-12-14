// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextRigVMAssetEntry.h"
#include "Param/ParamType.h"
#include "StructUtils/PropertyBag.h"
#include "AnimNextDataInterfaceEntry.generated.h"

class UAnimNextDataInterface;
class UAnimNextRigVMAssetEditorData;
class UAssetDefinition_AnimNextDataInterfaceEntry;

namespace UE::AnimNext::Editor
{
	class FVariablesOutlinerHierarchy;
	struct FVariablesOutlinerDataInterfaceItem;
	class SVariablesOutlinerDataInterfaceLabel;
	class SVariablesOutlinerValue;
	class FVariableProxyCustomization;
	class SAddVariablesDialog;
	class SVariableOverride;
}

namespace UE::AnimNext::UncookedOnly
{
	struct FUtils;
}

namespace UE::AnimNext::Tests
{
	class FDataInterfaceCompile;
}

UENUM()
enum class EAnimNextDataInterfaceAutomaticBindingMode : uint8
{
	// No automatic binding will be performed
	NoBinding,

	// Public variables that exist on shared data interfaces on this asset and its host will be bound together if they share an interface
	BindSharedInterfaces,
};

// Enum describing how a variable value is overriden
UENUM()
enum class EAnimNextDataInterfaceValueOverrideStatus
{
	// No override present in the implementation hierarchy
	NotOverridden,

	// Override present in this asset 
	OverriddenInThisAsset,

	// Override present in a parent asset
	OverriddenInParentAsset,
};

UCLASS(MinimalAPI, Category = "Data Interfaces", DisplayName = "Data Interface")
class UAnimNextDataInterfaceEntry : public UAnimNextRigVMAssetEntry
{
	GENERATED_BODY()

	friend class UAnimNextRigVMAssetEditorData;
	friend class UAssetDefinition_AnimNextDataInterfaceEntry;
	friend class UE::AnimNext::Editor::FVariablesOutlinerHierarchy;
	friend struct UE::AnimNext::Editor::FVariablesOutlinerDataInterfaceItem;
	friend struct UE::AnimNext::UncookedOnly::FUtils;
	friend class UE::AnimNext::Tests::FDataInterfaceCompile;
	friend class UE::AnimNext::Editor::SVariablesOutlinerDataInterfaceLabel;
	friend class UE::AnimNext::Editor::SVariablesOutlinerValue;
	friend class UE::AnimNext::Editor::FVariableProxyCustomization;
	friend class UE::AnimNext::Editor::SAddVariablesDialog;
	friend class UE::AnimNext::Editor::SVariableOverride;

	// UAnimNextRigVMAssetEntry interface
	virtual void Initialize(UAnimNextRigVMAssetEditorData* InEditorData) override;
	virtual FName GetEntryName() const override;
	virtual void SetEntryName(FName InName, bool bSetupUndoRedo = true) override {}
	virtual FText GetDisplayName() const override;
	virtual FText GetDisplayNameTooltip() const override;

	// Set the data interface that this entry represents
	ANIMNEXTUNCOOKEDONLY_API void SetDataInterface(UAnimNextDataInterface* InDataInterface, bool bSetupUndoRedo = true);

	// Get the data interface that this entry represents
	ANIMNEXTUNCOOKEDONLY_API UAnimNextDataInterface* GetDataInterface() const;

	// Get the path to the data interface that this entry represents
	ANIMNEXTUNCOOKEDONLY_API FSoftObjectPath GetDataInterfacePath() const;

	// Set an overriden value for the specified named variable, using the current value as the initial override
	// @return true if the value was set successfully
	ANIMNEXTUNCOOKEDONLY_API bool SetValueOverrideToDefault(FName InName, bool bSetupUndoRedo = true);

	// Set an overriden value for the specified name and type
	// @return true if the value was set successfully
	ANIMNEXTUNCOOKEDONLY_API bool SetValueOverride(FName InName, const FAnimNextParamType& InType, TConstArrayView<uint8> InValue, bool bSetupUndoRedo = true);

	// Set an overriden value for the specified name and type
	// @return true if the value was set successfully
	template<typename ValueType>
	bool SetValueOverride(FName InName, const ValueType& InValue, bool bSetupUndoRedo = true)
	{
		return SetValueOverride(InName, FAnimNextParamType::GetType<ValueType>(), TConstArrayView<uint8>((uint8*)&InValue, sizeof(ValueType)), bSetupUndoRedo);
	}

	// Clear the overriden value for the specified name in this entry
	// @return true if the value was cleared successfully
	ANIMNEXTUNCOOKEDONLY_API bool ClearValueOverride(FName InName, bool bSetupUndoRedo = true);

	// Get the overriden value for the specified name, if any exist in this entry
	// @return true if the override exists
	ANIMNEXTUNCOOKEDONLY_API bool GetValueOverride(FName InName, const FProperty*& OutProperty, TConstArrayView<uint8>& OutValue) const;
	ANIMNEXTUNCOOKEDONLY_API bool GetValueOverride(FName InName, FAnimNextParamType& OutType, const FProperty*& OutProperty, TConstArrayView<uint8>& OutValue) const;

	// Get the overriden value for the specified name & type, if any exist in the implementation hierarchy. Note: does not return default values, only overrides
	// @return the status of the override
	ANIMNEXTUNCOOKEDONLY_API EAnimNextDataInterfaceValueOverrideStatus FindValueOverrideRecursive(FName InName, const FProperty*& OutProperty, TConstArrayView<uint8>& OutValue) const;
	ANIMNEXTUNCOOKEDONLY_API EAnimNextDataInterfaceValueOverrideStatus FindValueOverrideRecursive(FName InName, FAnimNextParamType& OutType, const FProperty*& OutProperty, TConstArrayView<uint8>& OutValue) const;

	// Get whether this entry contains an override value for the specified named variable
	// @return true if this entry contains an override for the specified variable
	ANIMNEXTUNCOOKEDONLY_API bool HasValueOverride(FName InName) const;
	ANIMNEXTUNCOOKEDONLY_API bool HasValueOverride(FName InName, FAnimNextParamType& OutType) const;

	// Get the value before this data interface 'layer'. Value could be the base value, or overriden by any values in-between in the implementation
	// hierarchy, but any overrides in this entry are skipped.
	// @return true if the value was found
	ANIMNEXTUNCOOKEDONLY_API bool GetDefaultValueRecursive(FName InName, const FProperty*& OutProperty, TConstArrayView<uint8>& OutValue) const;
	
	// Get whether this entry contains an override value for the specified named variable that differs from the inherited default (recursive call)
	// @return true if this entry contains a default override for the specified variable
	ANIMNEXTUNCOOKEDONLY_API bool HasValueOverrideNotMatchingDefault(FName InName) const;

	// Get whether this entry contains an override value for the specified named variable, or if any overrides exist in the implementation hierarchy
	// @return the status of the override
	ANIMNEXTUNCOOKEDONLY_API EAnimNextDataInterfaceValueOverrideStatus GetValueOverrideStatusRecursive(FName InName) const;

	// Get the property bag that contains the value override for the specified named variable
	ANIMNEXTUNCOOKEDONLY_API EAnimNextDataInterfaceValueOverrideStatus FindValueOverridePropertyBagRecursive(FName InName, FInstancedPropertyBag*& OutPropertyBag) const;

	// Get the property bag that contains the value overrides in this entry
	FInstancedPropertyBag& GetValueOverridePropertyBag() { return ValueOverrides; }

	// Recompiles this asset when the linked data interface is modified
	void HandleDataInterfaceModified(UAnimNextRigVMAssetEditorData* InEditorData, EAnimNextEditorDataNotifType InType, UObject* InSubject);

	// Internal helper functions used to find overrides
	const UAnimNextDataInterfaceEntry* FindOverrideRecursiveHelper(TFunctionRef<bool(const UAnimNextDataInterfaceEntry*)> InPredicate) const;
	EAnimNextDataInterfaceValueOverrideStatus FindOverrideStatusRecursiveHelper(TFunctionRef<bool(const UAnimNextDataInterfaceEntry*)> InPredicate) const;

	/** The implemented interface */
	UPROPERTY(VisibleAnywhere, Category = "Data Interface")
	TObjectPtr<UAnimNextDataInterface> DataInterface;

	/** Soft reference to the Data Interface for error reporting */
	UPROPERTY()
	FSoftObjectPath DataInterfacePath;

	/** Property bag for overriden values */
	UPROPERTY()
	FInstancedPropertyBag ValueOverrides;

	/** How to automatically bind to the hosting graph or module */
	UPROPERTY(EditAnywhere, Category = "Data Interface")
	EAnimNextDataInterfaceAutomaticBindingMode AutomaticBinding = EAnimNextDataInterfaceAutomaticBindingMode::BindSharedInterfaces;
};
