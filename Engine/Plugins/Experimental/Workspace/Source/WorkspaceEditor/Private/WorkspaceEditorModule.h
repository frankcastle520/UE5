// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IWorkspaceEditorModule.h"
#include "UObject/TopLevelAssetPath.h"
#include "IWorkspaceOutlinerItemDetails.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"

struct FWorkspaceAssetRegistryExports;
class SWorkspaceTabWrapper;

namespace UE::Workspace
{
	struct FAssetDocumentSummoner;
	class FWorkspaceEditor;
	class FWorkspaceOutlinerMode;
}

namespace UE::Workspace
{

class FWorkspaceEditorModule : public IWorkspaceEditorModule
{
private:
	// IModuleInterface interface
	virtual void StartupModule() override;
	
	// IWorkspaceModule interface
	virtual void RegisterObjectDocumentType(const FTopLevelAssetPath& InClassPath, const FObjectDocumentArgs& InParams) override;
	virtual void UnregisterObjectDocumentType(const FTopLevelAssetPath& InClassPath) override;
	virtual FObjectDocumentArgs CreateGraphDocumentArgs(const FGraphDocumentWidgetArgs& InArgs) override;
	virtual IWorkspaceEditor* OpenWorkspaceForObject(UObject* InObject, EOpenWorkspaceMethod InOpenMethod, const TSubclassOf<UWorkspaceFactory> WorkSpaceFactoryClass) override;	
	virtual FOnRegisterDetailCustomizations& OnRegisterWorkspaceDetailsCustomization() override;
	virtual void RegisterWorkspaceItemDetails(const FOutlinerItemDetailsId& InItemDetailsId, TSharedPtr<IWorkspaceOutlinerItemDetails> InItemDetails) override;	
	virtual void UnregisterWorkspaceItemDetails(const FOutlinerItemDetailsId& InItemDetails) override;
	virtual FOnRegisterTabs& OnRegisterTabsForEditor() override { return RegisterTabsForEditor; }
	virtual FOnExtendTabs& OnExtendTabs() override { return ExtendTabsForEditor; }
	virtual FOnExtendToolMenuContext& OnExtendToolMenuContext() override { return ExtendToolMenuContext; }

	// Find an existing registered object document type. Note this redirects based on FObjectDocumentArgs::OnRedirectWorkspaceContext
	const FObjectDocumentArgs* FindObjectDocumentType(const UObject* InObject) const;

	// Find the set of allowed object types for the specified spawn location
	TArray<FTopLevelAssetPath> GetAllowedObjectTypesForArea(FName InSpawnLocation) const;

	// Get the exported set of assets in asset registry tags for a workspace's FAssetData
	static bool GetExportedAssetsForWorkspace(const FAssetData& InWorkspaceAsset, FWorkspaceAssetRegistryExports& OutExports);

	// Applies any previously registered details-view customizations
	void ApplyWorkspaceDetailsCustomization(const TWeakPtr<IWorkspaceEditor>& Editor, TSharedPtr<IDetailsView>& DetailsView) const;

	TMap<FTopLevelAssetPath, FObjectDocumentArgs> ObjectDocumentArgs;

	TMap<FName, TSet<FTopLevelAssetPath>> DocumentAreaMap;
	
	static TMap<FOutlinerItemDetailsId, TSharedPtr<IWorkspaceOutlinerItemDetails>> OutlinerItemDetails;
	static TSharedPtr<IWorkspaceOutlinerItemDetails> GetOutlinerItemDetails(const FOutlinerItemDetailsId& DetailId);

	/** Event called to allow external clients to register details customizations */
	FOnRegisterDetailCustomizations OnRegisterDetailCustomizations;

	/** Event called to allow external clients to register additional tabs for the specified editor */
	FOnRegisterTabs RegisterTabsForEditor;
	
	/** Event called to allow external clients to extend the tab layout for the specified editor */
	FOnExtendTabs ExtendTabsForEditor;

	/** Event called to allow external clients to add details to the ToolMenuContext used for the toolbar and context menus */
	FOnExtendToolMenuContext ExtendToolMenuContext;

	friend struct FAssetDocumentSummoner;
	friend struct FWorkspaceOutlinerTreeItem;
	friend class SWorkspaceOutlinerTreelabel;
	friend class FWorkspaceEditor;
	friend class FWorkspaceOutlinerMode;
	friend class FWorkspaceOutlinerSourceControlColumn;
	friend class FWorkspaceOutlinerFileStateColumn;
	friend SWorkspaceTabWrapper;
	friend FWorkspaceEditorContext;
};

}
