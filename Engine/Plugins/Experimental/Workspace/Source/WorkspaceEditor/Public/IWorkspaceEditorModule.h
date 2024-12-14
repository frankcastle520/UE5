// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorModes.h"
#include "GraphEditor.h"
#include "StructUtils/InstancedStruct.h"
#include "WorkspaceFactory.h"
#include "Misc/Attribute.h"
#include "Modules/ModuleInterface.h"
#include "IWorkspaceOutlinerItemDetails.h"

class SWidget;
class UEdGraph;
class IDetailsView;
class FTabManager;
class FWorkflowAllowedTabSet;
class IDetailCustomization;
class FLayoutExtender;

struct FSlateBrush;
struct FTopLevelAssetPath;
struct FWorkspaceDocumentState;
struct FRegisterCustomClassLayoutParams;

namespace UE::Workspace
{
	class IWorkspaceEditor;
}

namespace UE::Workspace
{

namespace WorkspaceTabs
{
	WORKSPACEEDITOR_API extern const FName TopLeftDocumentArea;
	WORKSPACEEDITOR_API extern const FName BottomLeftDocumentArea;
	WORKSPACEEDITOR_API extern const FName TopMiddleDocumentArea;
	WORKSPACEEDITOR_API extern const FName BottomMiddleDocumentArea;
	WORKSPACEEDITOR_API extern const FName TopRightDocumentArea;
	WORKSPACEEDITOR_API extern const FName BottomRightDocumentArea;
}

// Context passed to workspace editor delegates
struct FWorkspaceEditorContext
{
	FWorkspaceEditorContext(const TSharedRef<IWorkspaceEditor>& InWorkspaceEditor, UObject* InObject);

	// The current workspace editor
	TSharedRef<IWorkspaceEditor> WorkspaceEditor;

	// The object being edited
	UObject* Object;
};

struct FWorkspaceBreadcrumb : TSharedFromThis<FWorkspaceBreadcrumb>
{
	using FOnGetBreadcrumbLabel = TDelegate<TAttribute<FText>()>;
	using FOnBreadcrumbClicked = TDelegate<void()>;
	using FCanSaveBreadcrumb = TDelegate<bool()>;
	using FOnSaveBreadcrumb = TDelegate<void()>;
	
	FOnGetBreadcrumbLabel OnGetLabel;
	FOnBreadcrumbClicked OnClicked;
	FCanSaveBreadcrumb CanSave;
	FOnSaveBreadcrumb OnSave;
};

using FOnRedirectWorkspaceContext = TDelegate<UObject*(UObject*)>;

using FOnMakeDocumentWidget = TDelegate<TSharedRef<SWidget>(const FWorkspaceEditorContext&)>;

using FOnGetTabIcon = TDelegate<const FSlateBrush*(const FWorkspaceEditorContext&)>;

using FOnGetTabName = TDelegate<TAttribute<FText>(const FWorkspaceEditorContext&)>;

using FOnGetDocumentState = TDelegate<TInstancedStruct<FWorkspaceDocumentState>(const FWorkspaceEditorContext&, TSharedRef<SWidget>)>;

using FOnSetDocumentState = TDelegate<void(const FWorkspaceEditorContext&, TSharedRef<SWidget>, const TInstancedStruct<FWorkspaceDocumentState>&)>;

using FOnGetDocumentBreadcrumbTrail = TDelegate<void(const FWorkspaceEditorContext&, TArray<TSharedPtr<FWorkspaceBreadcrumb>>&)>;

// Arguments used to make document widgets for objects
struct FObjectDocumentArgs
{
	FObjectDocumentArgs() = default;

	FObjectDocumentArgs(FOnMakeDocumentWidget InOnMakeDocumentWidget, FName InSpawnLocation = WorkspaceTabs::TopMiddleDocumentArea)
		: OnMakeDocumentWidget(InOnMakeDocumentWidget)
		, SpawnLocation(InSpawnLocation)
	{}

	FObjectDocumentArgs(FOnRedirectWorkspaceContext InOnRedirectWorkspaceContext)
		: OnRedirectWorkspaceContext(InOnRedirectWorkspaceContext)
	{}
	
	// Delegate called to redirect the context to another document object (e.g. a subobject)
	FOnRedirectWorkspaceContext OnRedirectWorkspaceContext;

	// Delegate called to generate a widget for the supplied object
	FOnMakeDocumentWidget OnMakeDocumentWidget;

	// Delegate called to build a struct used to store the document's state
	FOnGetDocumentState OnGetDocumentState;

	// Delegate called to use a struct to restore the document's state
	FOnSetDocumentState OnSetDocumentState;

	// Delegate called to get the tab icon to display. If this is unset, the icon will default to the asset icon for the class
	FOnGetTabIcon OnGetTabIcon;

	// Delegate called to get the tab name to display. If this is unset, the object's name will be used
	FOnGetTabName OnGetTabName;

	// Where to spawn the widget in the workspace layout - e.g. one of WorkspaceTabs
	FName SpawnLocation = WorkspaceTabs::TopMiddleDocumentArea;

	// Delegate called to get the bread crumb trail for this document tab
	FOnGetDocumentBreadcrumbTrail OnGetDocumentBreadcrumbTrail;

	// EditorMode ID to be associated with this asset, will be used to try and activate matching editor mode when this asset is focussed
	FEditorModeID DocumentEditorMode = NAME_None;
};


using FOnGraphSelectionChanged = TDelegate<void(const FWorkspaceEditorContext&, const FGraphPanelSelectionSet&)>;

using FOnCreateActionMenu = TDelegate<FActionMenuContent(const FWorkspaceEditorContext&, UEdGraph*, const FVector2D&, const TArray<UEdGraphPin*>&, bool, SGraphEditor::FActionMenuClosed)>;

using FOnNodeTextCommitted = TDelegate<void(const FWorkspaceEditorContext&, const FText&, ETextCommit::Type, UEdGraphNode*)>;

using FOnCanPerformActionOnSelectedNodes = TDelegate<bool(const FWorkspaceEditorContext&, const FGraphPanelSelectionSet&)>;
using FOnPerformActionOnSelectedNodes = TDelegate<void(const FWorkspaceEditorContext&, const FGraphPanelSelectionSet&)>;

using FOnCanPasteNodes = TDelegate<bool(const FWorkspaceEditorContext&, const FString&)>;
using FOnPasteNodes = TDelegate<void(const FWorkspaceEditorContext&, const FVector2D&, const FString&)>;

using FOnDuplicateSelectedNodes = TDelegate<void(const FWorkspaceEditorContext&, const FVector2D&, const FGraphPanelSelectionSet&)>;

using FOnGetWorkspaceDetailCustomizationInstance = TDelegate<TSharedRef<IDetailCustomization>(TWeakPtr<IWorkspaceEditor>)>;

using FOnNodeDoubleClicked = TDelegate<void(const FWorkspaceEditorContext&, const UEdGraphNode* InNode)>;

// Arguments used to make document widgets for graphs
struct FGraphDocumentWidgetArgs
{
	// Where to spawn the widget in the workspace layout - e.g. one of WorkspaceTabs
	FName SpawnLocation = WorkspaceTabs::TopMiddleDocumentArea;

	FOnCreateActionMenu OnCreateActionMenu;

	FOnNodeTextCommitted OnNodeTextCommitted;

	FOnCanPerformActionOnSelectedNodes OnCanCutSelectedNodes;
	FOnPerformActionOnSelectedNodes OnCutSelectedNodes;

	FOnCanPerformActionOnSelectedNodes OnCanCopySelectedNodes;
	FOnPerformActionOnSelectedNodes OnCopySelectedNodes;

	FOnCanPasteNodes OnCanPasteNodes;
	FOnPasteNodes OnPasteNodes;

	FOnCanPerformActionOnSelectedNodes OnCanDeleteSelectedNodes;
	FOnPerformActionOnSelectedNodes OnDeleteSelectedNodes;

	FOnCanPerformActionOnSelectedNodes OnCanDuplicateSelectedNodes;
	FOnDuplicateSelectedNodes OnDuplicateSelectedNodes;

	FOnCanPerformActionOnSelectedNodes OnCanSelectAllNodes;
	FOnPerformActionOnSelectedNodes OnSelectAllNodes;

	FOnGraphSelectionChanged OnGraphSelectionChanged;

	FOnNodeDoubleClicked OnNodeDoubleClicked;
};

// Enum describing how to open a workspace
enum class EOpenWorkspaceMethod : int32
{
	// If the asset is already used in a workspace, open that (if not already opened)
	// If the asset is already used in more than one workspace, let the user choose the workspace to open it in
	// If the asset is not yet in a workspace, create a default workspace, add the asset and open the workspace
	Default,

	// Always open a new workspace asset and add the asset to it
	AlwaysOpenNewWorkspace,
};

class IWorkspaceEditorModule : public IModuleInterface
{
public:
	// Open an object inside a workspace editor.
	virtual IWorkspaceEditor* OpenWorkspaceForObject(UObject* InObject, EOpenWorkspaceMethod InOpenMethod, const TSubclassOf<UWorkspaceFactory> WorkSpaceFactoryClass = UWorkspaceFactory::StaticClass()) = 0;

	// Register a widget factory method to spawn for a particular class
	virtual void RegisterObjectDocumentType(const FTopLevelAssetPath& InClassPath, const FObjectDocumentArgs& InArgs) = 0;

	// Unregister a widget factory method to spawn for a particular class
	virtual void UnregisterObjectDocumentType(const FTopLevelAssetPath& InClassPath) = 0;

	// Make the required args for a document widget for a UEdGraph
	virtual FObjectDocumentArgs CreateGraphDocumentArgs(const FGraphDocumentWidgetArgs& InArgs) = 0;

	// Event to allow registering details customizations
	DECLARE_EVENT_TwoParams(IWorkspaceEditorModule, FOnRegisterDetailCustomizations, const TWeakPtr<IWorkspaceEditor>&, TSharedPtr<IDetailsView>&);
	virtual FOnRegisterDetailCustomizations& OnRegisterWorkspaceDetailsCustomization() = 0;

	virtual void RegisterWorkspaceItemDetails(const FOutlinerItemDetailsId& InDetailsId, TSharedPtr<IWorkspaceOutlinerItemDetails> InDetails) = 0;
	virtual void UnregisterWorkspaceItemDetails(const FOutlinerItemDetailsId& InDetailsId) = 0;

	// Event to allow registering tabs to other elements
	DECLARE_EVENT_ThreeParams(IWorkspaceEditorModule, FOnRegisterTabs, FWorkflowAllowedTabSet& TabFactories, const TSharedRef<FTabManager>&, TSharedPtr<IWorkspaceEditor>);
	virtual FOnRegisterTabs& OnRegisterTabsForEditor() = 0;

	// Event to allow extending the layout
	DECLARE_EVENT_TwoParams(IWorkspaceEditorModule, FOnExtendTabs, FLayoutExtender&, TSharedPtr<IWorkspaceEditor>);
	virtual FOnExtendTabs& OnExtendTabs() = 0;

	// Event to allow extending the FToolMenuContext
	DECLARE_EVENT_TwoParams(IWorkspaceEditorModule, FOnExtendToolMenuContext, const TWeakPtr<IWorkspaceEditor>&, FToolMenuContext&);
	virtual FOnExtendToolMenuContext& OnExtendToolMenuContext() = 0;
};

}
