// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/BaseAssetToolkit.h"

class UWorkspaceAssetEditor;
class UWorkspaceSchema;
struct FWorkspaceOutlinerItemExport;

namespace UE::Workspace
{

typedef TWeakPtr<SWidget> FGlobalSelectionId;
using FOnClearGlobalSelection = FSimpleDelegate;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnFocussedAssetChanged, TObjectPtr<UObject>);

// RAII helper allowing for a multi-widget selection scope within a WorkspaceEditor instance
struct WORKSPACEEDITOR_API FWorkspaceEditorSelectionScope
{
	FWorkspaceEditorSelectionScope(const TSharedPtr<class IWorkspaceEditor>& InWorkspaceEditor);	
	~FWorkspaceEditorSelectionScope();

	TWeakPtr<class IWorkspaceEditor> WeakWorkspaceEditor; 
};

class IWorkspaceEditor : public FBaseAssetToolkit
{
public:
	IWorkspaceEditor(UAssetEditor* InOwningAssetEditor) : FBaseAssetToolkit(InOwningAssetEditor) {}

	// Open the supplied assets for editing within the workspace editor
	virtual void OpenAssets(TConstArrayView<FAssetData> InAssets) = 0;

	// Open the supplied objects for editing within the workspace editor
	virtual void OpenObjects(TConstArrayView<UObject*> InObjects) = 0;

	// Close the supplied objects if they are open for editing within the workspace editor
	virtual void CloseObjects(TConstArrayView<UObject*> InObjects) = 0;

	// Show the supplied objects in the workspace editor details panel
	virtual void SetDetailsObjects(const TArray<UObject*>& InObjects) = 0;

	// Refresh the workspace editor details panel
	virtual void RefreshDetails() = 0;

	// Exposes the editor WorkspaceSchema
	virtual UWorkspaceSchema* GetSchema() const = 0;

	// Set the _current_ global selection (last SWidget with selection set) with delegate to clear it selection on next SetGlobalSelection()
	virtual void SetGlobalSelection(FGlobalSelectionId SelectionId, FOnClearGlobalSelection OnClearSelectionDelegate) = 0;

	virtual void SetFocussedAsset(const TObjectPtr<UObject> InAsset) = 0;
	virtual const TObjectPtr<UObject> GetFocussedAssetOfClass(const TObjectPtr<UClass> InClass ) const = 0;
	
	template<typename AssetClass>
	TObjectPtr<AssetClass> GetFocussedAsset() const { return Cast<AssetClass>(GetFocussedAssetOfClass(AssetClass::StaticClass())); }
	TObjectPtr<UObject> GetFocussedAsset() const { return GetFocussedAssetOfClass(UObject::StaticClass()); }

	// Multi-cast delegate broadcasted whenever the asset focussed inside of the WorkspaceEditor changes
	virtual FOnFocussedAssetChanged& OnFocussedAssetChanged() = 0;

	// Get the current single selection of the outliner.
	// @return true if a single selection is active
	virtual bool GetOutlinerSelection(TArray<FWorkspaceOutlinerItemExport>& OutExports) const = 0;

	// Delegate fired when selection changes in the workspace outliner
	using FOnOutlinerSelectionChanged = TMulticastDelegate<void(TConstArrayView<FWorkspaceOutlinerItemExport> InExports)>;
	virtual FOnOutlinerSelectionChanged& OnOutlinerSelectionChanged() = 0;

	// Retrieves the common DetailsView widget
	virtual TSharedPtr<IDetailsView> GetDetailsView() = 0;
};

}