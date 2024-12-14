// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StructViewerFilter.h"
#include "StructViewerModule.h"
#include "Widgets/SCompoundWidget.h"

class IPropertyHandle;
class IPropertyUtilities;
class IAssetReferenceFilter;
class SComboButton;

/**
 * Filter used by the instanced struct struct picker.
 */
class STRUCTUTILSEDITOR_API FInstancedStructFilter : public IStructViewerFilter
{
public:
	/** The base struct for the property that classes must be a child-of. */
	TWeakObjectPtr<const UScriptStruct> BaseStruct = nullptr;

	/** The array of allowed structs */
	TArray<TSoftObjectPtr<const UScriptStruct>> AllowedStructs;

	/** The array of disallowed structs */
	TArray<TSoftObjectPtr<const UScriptStruct>> DisallowedStructs;

	// A flag controlling whether we allow UserDefinedStructs
	bool bAllowUserDefinedStructs = false;

	// A flag controlling whether we allow to select the BaseStruct
	bool bAllowBaseStruct = true;

	virtual bool IsStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const UScriptStruct* InStruct, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override;
	virtual bool IsUnloadedStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const FSoftObjectPath& InStructPath, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override;

	// Optional filter to prevent selection of some structs e.g. ones in a plugin that is inaccessible from the object being edited
	TSharedPtr<IAssetReferenceFilter> AssetReferenceFilter;
};

class STRUCTUTILSEDITOR_API SInstancedStructPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInstancedStructPicker) { }
		SLATE_EVENT(FOnStructPicked, OnStructPicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<IPropertyHandle> InStructProperty, TSharedPtr<IPropertyUtilities> InPropertyUtils);

	FOnStructPicked OnStructPicked;

private:
	TSharedPtr<SComboButton> ComboButton;
	TSharedPtr<IPropertyHandle> StructProperty;
	TSharedPtr<IPropertyUtilities> PropUtils;

	/** The base struct that we're allowing to be picked (controlled by the "BaseStruct" meta-data) */
	TWeakObjectPtr<UScriptStruct> BaseScriptStruct = nullptr;

	FText GetDisplayValueString() const;
	FText GetTooltipText() const;
	const FSlateBrush* GetDisplayValueIcon() const;
	TSharedRef<SWidget> GenerateStructPicker();
	void StructPicked(const UScriptStruct* InStruct);
};
