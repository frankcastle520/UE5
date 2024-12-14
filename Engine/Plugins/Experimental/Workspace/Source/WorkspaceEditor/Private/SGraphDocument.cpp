// Copyright Epic Games, Inc. All Rights Reserved.

#include "SGraphDocument.h"

#include "Framework/Commands/GenericCommands.h"
#include "WorkspaceEditor.h"
#include "EdGraph/EdGraph.h"
#include "HAL/PlatformApplicationMisc.h"

namespace UE::Workspace
{

void SGraphDocument::Construct(const FArguments& InArgs, TSharedRef<FWorkspaceEditor> InHostingApp, UEdGraph* InGraph)
{
	HostingAppPtr = InHostingApp;
	EdGraph = InGraph;

	BindCommands();

	OnCanDeleteSelectedNodes = InArgs._OnCanDeleteSelectedNodes;
	OnDeleteSelectedNodes = InArgs._OnDeleteSelectedNodes;
	OnCanCutSelectedNodes = InArgs._OnCanCutSelectedNodes;
	OnCutSelectedNodes = InArgs._OnCutSelectedNodes;
	OnCanCopySelectedNodes = InArgs._OnCanCopySelectedNodes;
	OnCopySelectedNodes = InArgs._OnCopySelectedNodes;
	OnCanPasteNodes = InArgs._OnCanPasteNodes;
	OnPasteNodes = InArgs._OnPasteNodes;
	OnCanDuplicateSelectedNodes = InArgs._OnCanDuplicateSelectedNodes;
	OnDuplicateSelectedNodes = InArgs._OnDuplicateSelectedNodes;

	SGraphEditor::FGraphEditorEvents Events;
	Events.OnCreateActionMenu = SGraphEditor::FOnCreateActionMenu::CreateLambda([this, OnCreateActionMenu = InArgs._OnCreateActionMenu](UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bInAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed)
	{
		if(OnCreateActionMenu.IsBound())
		{
			return OnCreateActionMenu.Execute(FWorkspaceEditorContext(HostingAppPtr.Pin().ToSharedRef(), EdGraph), InGraph, InNodePosition, InDraggedPins, bInAutoExpand, InOnMenuClosed);
		}
		return FActionMenuContent();
	});
	Events.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateLambda([this, OnGraphSelectionChanged = InArgs._OnGraphSelectionChanged](const TSet<UObject*>& NewSelection)
	{
		OnGraphSelectionChanged.ExecuteIfBound(FWorkspaceEditorContext(HostingAppPtr.Pin().ToSharedRef(), EdGraph), NewSelection);

		if (const TSharedPtr<FWorkspaceEditor> SharedWorkspaceEditor = HostingAppPtr.Pin())
		{
			SharedWorkspaceEditor->SetGlobalSelection(AsShared(), FOnClearGlobalSelection::CreateRaw(this, &SGraphDocument::OnResetSelection));
		}
	});
	Events.OnTextCommitted = ::FOnNodeTextCommitted::CreateLambda([this, OnNodeTextCommitted = InArgs._OnNodeTextCommitted](const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged)
	{
		OnNodeTextCommitted.ExecuteIfBound(FWorkspaceEditorContext(HostingAppPtr.Pin().ToSharedRef(), EdGraph), NewText, CommitInfo, NodeBeingChanged);
	});
	Events.OnNodeDoubleClicked = FSingleNodeEvent::CreateLambda([this, OnNodeDoubleClicked = InArgs._OnNodeDoubleClicked](UEdGraphNode* InNode)
	{
		OnNodeDoubleClicked.ExecuteIfBound(FWorkspaceEditorContext(HostingAppPtr.Pin().ToSharedRef(), EdGraph), InNode);
	});

	ChildSlot
	[
		SAssignNew(GraphEditor, SGraphEditor)
		.AdditionalCommands(CommandList)
		.IsEditable(this, &SGraphDocument::IsEditable, InGraph)
		.GraphToEdit(InGraph)
		.GraphEvents(Events)
		.AssetEditorToolkit(HostingAppPtr)
		.OnNavigateHistoryBack(InArgs._OnNavigateHistoryBack)		
		.OnNavigateHistoryForward(InArgs._OnNavigateHistoryForward)
	];
}

void SGraphDocument::BindCommands()
{
	CommandList = MakeShared<FUICommandList>();

	CommandList->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SGraphDocument::DeleteSelectedNodes),
		FCanExecuteAction::CreateSP(this, &SGraphDocument::CanDeleteSelectedNodes));

	CommandList->MapAction(FGenericCommands::Get().Cut,
		FExecuteAction::CreateSP(this, &SGraphDocument::CutSelectedNodes),
		FCanExecuteAction::CreateSP(this, &SGraphDocument::CanCutSelectedNodes));

	CommandList->MapAction(FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &SGraphDocument::CopySelectedNodes),
		FCanExecuteAction::CreateSP(this, &SGraphDocument::CanCopySelectedNodes));

	CommandList->MapAction(FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &SGraphDocument::PasteNodes),
		FCanExecuteAction::CreateSP(this, &SGraphDocument::CanPasteNodes));

	CommandList->MapAction(FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &SGraphDocument::DuplicateSelectedNodes),
		FCanExecuteAction::CreateSP(this, &SGraphDocument::CanDuplicateSelectedNodes));

	CommandList->MapAction(FGenericCommands::Get().SelectAll,
		FExecuteAction::CreateSP(this, &SGraphDocument::SelectAllNodes),
		FCanExecuteAction::CreateSP(this, &SGraphDocument::CanSelectAllNodes));
}

bool SGraphDocument::CanDeleteSelectedNodes() const
{
	const bool bCan = OnCanCutSelectedNodes.IsBound()
		? IsEditable(EdGraph) && OnCanCutSelectedNodes.Execute(FWorkspaceEditorContext(HostingAppPtr.Pin().ToSharedRef(), EdGraph), GraphEditor->GetSelectedNodes())
		: false;
	return bCan;
}

void SGraphDocument::DeleteSelectedNodes()
{
	const FGraphPanelSelectionSet SelectedNodes = GraphEditor->GetSelectedNodes();
	OnDeleteSelectedNodes.ExecuteIfBound(FWorkspaceEditorContext(HostingAppPtr.Pin().ToSharedRef(), EdGraph), SelectedNodes);
}

bool SGraphDocument::CanCutSelectedNodes() const
{
	const bool bCan = OnCanCutSelectedNodes.IsBound() 
		? IsEditable(EdGraph) && OnCanCutSelectedNodes.Execute(FWorkspaceEditorContext(HostingAppPtr.Pin().ToSharedRef(), EdGraph), GraphEditor->GetSelectedNodes())
		: false;
	return bCan;
}

void SGraphDocument::CutSelectedNodes()
{
	const FGraphPanelSelectionSet SelectedNodes = GraphEditor->GetSelectedNodes();
	OnCutSelectedNodes.ExecuteIfBound(FWorkspaceEditorContext(HostingAppPtr.Pin().ToSharedRef(), EdGraph), SelectedNodes);
}

bool SGraphDocument::CanCopySelectedNodes() const
{
	const bool bCan = OnCanCopySelectedNodes.IsBound()
		? OnCanCopySelectedNodes.Execute(FWorkspaceEditorContext(HostingAppPtr.Pin().ToSharedRef(), EdGraph), GraphEditor->GetSelectedNodes())
		: false;
	return bCan;
}

void SGraphDocument::CopySelectedNodes()
{
	const FGraphPanelSelectionSet SelectedNodes = GraphEditor->GetSelectedNodes();
	OnCopySelectedNodes.ExecuteIfBound(FWorkspaceEditorContext(HostingAppPtr.Pin().ToSharedRef(), EdGraph), SelectedNodes);
}

bool SGraphDocument::CanPasteNodes() const
{
	FString TextToImport;
	FPlatformApplicationMisc::ClipboardPaste(TextToImport);

	const bool bCan = OnCanPasteNodes.IsBound()
		? IsEditable(EdGraph) && !TextToImport.IsEmpty() && OnCanPasteNodes.Execute(FWorkspaceEditorContext(HostingAppPtr.Pin().ToSharedRef(), EdGraph), TextToImport)
		: false;
	return bCan;
}

void SGraphDocument::PasteNodes()
{
	FString TextToImport;
	FPlatformApplicationMisc::ClipboardPaste(TextToImport);

	OnPasteNodes.ExecuteIfBound(FWorkspaceEditorContext(HostingAppPtr.Pin().ToSharedRef(), EdGraph), GraphEditor->GetPasteLocation(), TextToImport);
}

bool SGraphDocument::CanDuplicateSelectedNodes() const
{
	const bool bCan = OnCanDuplicateSelectedNodes.IsBound()
		? IsEditable(EdGraph) && OnCanDuplicateSelectedNodes.Execute(FWorkspaceEditorContext(HostingAppPtr.Pin().ToSharedRef(), EdGraph), GraphEditor->GetSelectedNodes())
		: false;
	return bCan;
}

void SGraphDocument::DuplicateSelectedNodes()
{
	const FGraphPanelSelectionSet SelectedNodes = GraphEditor->GetSelectedNodes();
	OnDuplicateSelectedNodes.ExecuteIfBound(FWorkspaceEditorContext(HostingAppPtr.Pin().ToSharedRef(), EdGraph), GraphEditor->GetPasteLocation(), SelectedNodes);
}

bool SGraphDocument::CanSelectAllNodes() const
{
	const bool bCan = GraphEditor.IsValid();
	return bCan;
}

void SGraphDocument::SelectAllNodes()
{
	GraphEditor->SelectAllNodes();
}


bool SGraphDocument::IsEditable(UEdGraph* InGraph) const
{
	return InGraph && HostingAppPtr.Pin()->InEditingMode() && InGraph->bEditable;
}

void SGraphDocument::OnResetSelection()
{
	GraphEditor->ClearSelectionSet();
}
};