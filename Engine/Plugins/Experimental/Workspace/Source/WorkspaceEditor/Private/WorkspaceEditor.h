// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "IWorkspaceEditor.h"
#include "WorkspaceAssetRegistryInfo.h"
#include "WorkspaceEditorModeUILayer.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"

struct FWorkspaceDocumentState;
struct FWorkspaceAssetRegistryExports;
class UWorkspace;
class FDocumentTracker;
class FDocumentTabFactory;
class FTabInfo;
class FTabManager;
class IToolkitHost;
class SWorkspaceTabWrapper;
class IStructureDetailsView;

namespace UE::Workspace
{

struct FGraphDocumentSummoner;
struct FWorkspaceTabSummoner;
struct FAssetDocumentSummoner;
class FWorkspaceEditorModule;
class SWorkspaceView;

namespace WorkspaceModes
{
	extern const FName WorkspaceEditor;
}

namespace WorkspaceTabs
{
	extern const FName Details;
	extern const FName WorkspaceView;
}

class FWorkspaceEditor : public IWorkspaceEditor, public FGCObject
{
public:
	FWorkspaceEditor(UWorkspaceAssetEditor* InOwningAssetEditor);
	virtual ~FWorkspaceEditor() override {}

private:
	friend class FWorkspaceEditorMode;
	friend class FWorkspaceEditorModule;
	friend class SGraphDocument;
	friend SWorkspaceTabWrapper;
	friend struct FGraphDocumentSummoner;
	friend struct FWorkspaceTabSummoner;
	friend struct FAssetDocumentSummoner;
	friend struct FWorkspaceEditorSelectionScope;

	// FBaseAssetToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void CreateWidgets() override;
	virtual void PostInitAssetEditor() override;
	virtual void InitToolMenuContext(FToolMenuContext& InMenuContext) override;
	virtual void SaveAsset_Execute() override;
	virtual bool OnRequestClose(EAssetEditorCloseReason InCloseReason) override;
	virtual void OnClose() override;
	virtual void RegisterToolbar() override;
	virtual bool ShouldReopenEditorForSavedAsset(const UObject* Asset) const override;
	virtual void RemoveEditingObject(UObject* Object);

	// FAssetEditorToolkit interface
	virtual void GetSaveableObjects(TArray<UObject*>& OutObjects) const override;
	virtual bool CanSaveAsset() const override;
	virtual FText GetTabSuffix() const override;
	virtual FText GetToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual void FindInContentBrowser_Execute() override;
	virtual void OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit) override;
	virtual void OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit) override;

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(Workspace);
	}
	virtual FString GetReferencerName() const override
	{
		return TEXT("FWorkspaceEditor");
	}

	// IWorkspaceEditor interface
	virtual void OpenAssets(TConstArrayView<FAssetData> InAssets) override;
	virtual void OpenObjects(TConstArrayView<UObject*> InObjects) override;
	virtual void CloseObjects(TConstArrayView<UObject*> InObjects) override;
	virtual void SetDetailsObjects(const TArray<UObject*>& InObjects) override;
	virtual void RefreshDetails() override;
	virtual UWorkspaceSchema* GetSchema() const override;
	virtual bool GetOutlinerSelection(TArray<FWorkspaceOutlinerItemExport>& OutExports) const override;
	virtual FOnOutlinerSelectionChanged& OnOutlinerSelectionChanged() override;
	virtual void SetGlobalSelection(FGlobalSelectionId SelectionId, FOnClearGlobalSelection OnClearSelectionDelegate) override;
	virtual void SetFocussedAsset(const TObjectPtr<UObject> InAsset) override;	
	virtual const TObjectPtr<UObject> GetFocussedAssetOfClass(const TObjectPtr<UClass> AssetClass) const override;
	virtual FOnFocussedAssetChanged& OnFocussedAssetChanged() override;
	virtual TSharedPtr<IDetailsView> GetDetailsView() override;

	void HandleOutlinerSelectionChanged(TConstArrayView<FWorkspaceOutlinerItemExport> InExports);

	void BindCommands();

	void ExtendMenu();

	void ExtendToolbar();

	void CloseDocumentTab(const UObject* DocumentID) const;

	bool InEditingMode() const;

	void RestoreEditedObjectState();

	void SaveEditedObjectState() const;

	TSharedPtr<SDockTab> OpenDocument(const UObject* InForObject, FDocumentTracker::EOpenDocumentCause InCause);

	void RecordDocumentState(const TInstancedStruct<FWorkspaceDocumentState>& InState) const;

	void NavigateBack();
	void NavigateForward();

	void SaveAssetEntries();
	bool AreAssetEntriesModified() const;

	/** The asset being edited */
	TObjectPtr<UWorkspace> Workspace = nullptr;

	// Document tracker
	TSharedPtr<FDocumentTracker> DocumentManager;

	TSharedPtr<SWorkspaceView> WorkspaceView;

	TMap<FName, TSharedPtr<FWorkspaceEditorModeUILayer>> ModeUILayers;
	TArray<TSharedPtr<class IToolkit>> HostedToolkits;

	/** Tabs to be registered into the Workspace */
	FWorkflowAllowedTabSet TabFactories;

	bool bSavingTransientWorkspace = false;
	bool bSavingWorkspace = false;
	bool bSavingAssetEntries = false;
	bool bClosingDown = false;

	TArray<TPair<FGlobalSelectionId, FOnClearGlobalSelection>> GlobalSelections;
	FOnOutlinerSelectionChanged OnOutlinerSelectionChangedDelegate;
	bool bSelectionScopeCleared = true;
	int32 SelectionScopeDepth = 0;
	
	TArray<FWorkspaceOutlinerItemExport> LastSelectedExports;
	FOnFocussedAssetChanged OnFocussedAssetChangedDelegate;
	TSharedPtr<FWorkspaceItem> EditorMenuCategory;
};

}