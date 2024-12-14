// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Dataflow/DataflowEditorCommands.h"
#include "Dataflow/DataflowSEditorInterface.h"
#include "EdGraphUtilities.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "Layout/SlateRect.h"
#include "NodeFactory.h"
#include "SGraphNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SWidget.h"


class FDataflowEditorToolkit;
class FDataflowGraphEditorNodeFactory;
class FDataflowSNodeFactory;
class UDataflow;
class UDataflowEditor;
struct FDataflowConnection;
namespace UE::Dataflow {
	class FContext;
}
/**
 * The SDataflowGraphEditpr class is a specialization of SGraphEditor
 * to display and manipulate the actions of a Dataflow asset
 * 
 * see(SDataprepGraphEditor for reference)
 */
class DATAFLOWEDITOR_API SDataflowGraphEditor : public SGraphEditor, public FGCObject, public FDataflowSEditorInterface
{
public:

	SLATE_BEGIN_ARGS(SDataflowGraphEditor)
		: _AdditionalCommands(static_cast<FUICommandList*>(nullptr))
		, _GraphToEdit(nullptr)
	{}

	SLATE_ARGUMENT(TSharedPtr<FUICommandList>, AdditionalCommands)
	SLATE_ATTRIBUTE(FGraphAppearanceInfo, Appearance)
	SLATE_ARGUMENT_DEFAULT(UEdGraph*, GraphToEdit) = nullptr;
	SLATE_ARGUMENT(FGraphEditorEvents, GraphEvents)
	SLATE_ARGUMENT(TSharedPtr<IStructureDetailsView>, DetailsView)
	SLATE_ARGUMENT(FDataflowEditorCommands::FGraphEvaluationCallback, EvaluateGraph)
	SLATE_ARGUMENT(FDataflowEditorCommands::FOnDragDropEventCallback, OnDragDropEvent)
	SLATE_ARGUMENT_DEFAULT(UDataflowEditor*, DataflowEditor) = nullptr;
	SLATE_END_ARGS()

	// This delegate exists in SGraphEditor but it is not multicast, and we are going to bind it to OnSelectedNodesChanged().
	// This new multicast delegate will be broadcast from the OnSelectedNodesChanged handler in case another class wants to be notified.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSelectionChangedMulticast, const FGraphPanelSelectionSet&)
	FOnSelectionChangedMulticast OnSelectionChangedMulticast;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnNodeDeletedMulticast, const FGraphPanelSelectionSet&)
	FOnNodeDeletedMulticast OnNodeDeletedMulticast;

	// SWidget overrides
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	bool IsControlDown() const;
	bool IsAltDown() const;
	//virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	//virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	// end SWidget

	/** */
	void Construct(const FArguments& InArgs, UObject* AssetOwner);

	/** */
	void EvaluateNode();

	/** */
	void DeleteNode();

	/** */
	void OnSelectedNodesChanged(const TSet<UObject*>& NewSelection);

	/** */
	void CreateComment();

	/** */
	void CreateVertexSelectionNode(const FString& InArray);

	/** */
	void AlignTop();

	/** */
	void AlignMiddle();

	/** */
	void AlignBottom();

	/** */
	void AlignLeft();

	/** */
	void AlignCenter();

	/** */
	void AlignRight();

	/** */
	void StraightenConnections();

	/** */
	void DistributeHorizontally();

	/** */
	void DistributeVertically();

	/** */
	void ToggleEnabledState();

	/** */
	void DuplicateSelectedNodes();

	/** */
	void ZoomToFitGraph();

	/** */
	void CopySelectedNodes();

	/** */
	void CutSelectedNodes();

	/** */
	void PasteSelectedNodes();

	/** */
	void RenameNode();
	bool CanRenameNode() const;

	SGraphEditor* GetGraphEditor() { return (SGraphEditor*)this; }

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SDataflowGraphEditor"); }

	/** FDataflowSNodeInterface */
	virtual TSharedPtr<UE::Dataflow::FContext> GetDataflowContext() const override;

	/** Return the currently selected editor. Only valid for the duration of the OnSelectedNodesChanged callback where the property editor is updated. */
	static const TWeakPtr<SDataflowGraphEditor>& GetSelectedGraphEditor() { return SelectedGraphEditor; }

private:
	/** Add an additional option pin to all selected Dataflow nodes for those that overrides the AddPin function. */
	void OnAddOptionPin();
	/** Return whether all currently selected Dataflow nodes can execute the AddPin function. */
	bool CanAddOptionPin() const;

	/** Remove an option pin from all selected Dataflow nodes for those that overrides the RemovePin function. */
	void OnRemoveOptionPin();
	/** Return whether all currently selected Dataflow nodes can execute the RemovePin function. */
	bool CanRemoveOptionPin() const;

	bool GetPinVisibility(SGraphEditor::EPinVisibility InVisibility) const;

	FDataflowEditorCommands::FOnDragDropEventCallback OnDragDropEventCallback;
	FDataflowEditorCommands::FGraphEvaluationCallback EvaluateGraphCallback;

	/** The asset that ownes this dataflow graph */
	TWeakObjectPtr<UObject> AssetOwner;

	/** The dataflow asset associated with this graph */
	TWeakObjectPtr<UDataflow> DataflowAsset;

	/** Command list associated with this graph editor */
	TSharedPtr<FUICommandList> GraphEditorCommands;

	/** The details view that responds to this widget. */
	TSharedPtr<IStructureDetailsView> DetailsView;

	/** Factory to create the associated SGraphNode classes for Dataprep graph's UEdGraph classes */
	static TSharedPtr<FDataflowGraphEditorNodeFactory> NodeFactory;

	/** The current graph editor when the selection callback is invoked. */
	static TWeakPtr<SDataflowGraphEditor> SelectedGraphEditor;

	/** Editor for the content */
	UDataflowEditor* DataflowEditor = nullptr;

	bool VKeyDown = false;
	bool LeftControlKeyDown = false;
	bool RightControlKeyDown = false;
	bool LeftAltKeyDown = false;
	bool RightAltKeyDown = false;
};

