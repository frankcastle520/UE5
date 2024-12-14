// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowObjectInterface.h"
#include "BaseCharacterFXEditorCommands.h"
#include "Styling/AppStyle.h"

class FDragDropEvent;
struct FDataflowOutput;
struct FGeometry;
class IStructureDetailsView;
class UDataflow;
class UDataflowEdNode;
struct FDataflowNode;
class UEdGraphNode;
class SDataflowGraphEditor;
class UDataflowBaseContent;

typedef TSet<class UObject*> FGraphPanelSelectionSet;

/*
* FDataflowEditorCommandsImpl
* 
*/
class DATAFLOWEDITOR_API FDataflowEditorCommandsImpl : public TBaseCharacterFXEditorCommands<FDataflowEditorCommandsImpl>
{
public:

	FDataflowEditorCommandsImpl();

	// Note: We need to explicitly disable warnings on these constructors/operators for clang to be happy with deprecated variables
	// (See Confluence page "How to Deprecate Code in UE > How to deprecate variables inside a UStruct")
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	~FDataflowEditorCommandsImpl() = default;
	FDataflowEditorCommandsImpl(const FDataflowEditorCommandsImpl&) = default;
	FDataflowEditorCommandsImpl(FDataflowEditorCommandsImpl&&) = default;
	FDataflowEditorCommandsImpl& operator=(const FDataflowEditorCommandsImpl&) = default;
	FDataflowEditorCommandsImpl& operator=(FDataflowEditorCommandsImpl&&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	// TBaseCharacterFXEditorCommands<> interface
	 virtual void RegisterCommands() override;

	// TInteractiveToolCommands<>
	// Each tool will have its own TInteractiveToolCommands<> object stored in the DataflowToolRegistry, so this should not return anything
	virtual void GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs) override {}

	/**
	* Add or remove commands relevant to Tool to the given UICommandList.
	* Call this when the active tool changes (eg on ToolManager.OnToolStarted / OnToolEnded)
	* @param bUnbind if true, commands are removed, otherwise added
	*/
	 static void UpdateToolCommandBinding(UInteractiveTool* Tool, TSharedPtr<FUICommandList> UICommandList, bool bUnbind = false);
	
	TSharedPtr<FUICommandInfo> EvaluateNode;
	TSharedPtr<FUICommandInfo> CreateComment;
	TSharedPtr<FUICommandInfo> ToggleEnabledState;
	TSharedPtr<FUICommandInfo> ToggleObjectSelection;
	TSharedPtr<FUICommandInfo> ToggleFaceSelection;
	TSharedPtr<FUICommandInfo> ToggleVertexSelection;
	TSharedPtr<FUICommandInfo> AddOptionPin;
	TSharedPtr<FUICommandInfo> RemoveOptionPin;
	TSharedPtr<FUICommandInfo> ZoomToFitGraph;

	TMap<FName, TSharedPtr<FUICommandInfo>> SetConstructionViewModeCommands;

	UE_DEPRECATED(5.5, "Dataflow Tool commands are now stored in FDataflowToolRegistry")
	const static FString BeginWeightMapPaintToolIdentifier;
	UE_DEPRECATED(5.5, "Dataflow Tool commands are now stored in FDataflowToolRegistry")
	TSharedPtr<FUICommandInfo> BeginWeightMapPaintTool;
	
	const static FString AddWeightMapNodeIdentifier;
	TSharedPtr<FUICommandInfo> AddWeightMapNode;

	const static FString RebuildSimulationSceneIdentifier;
	TSharedPtr<FUICommandInfo> RebuildSimulationScene;
	
	const static FString PauseSimulationSceneIdentifier;
	TSharedPtr<FUICommandInfo> PauseSimulationScene;

	const static FString StartSimulationSceneIdentifier;
	TSharedPtr<FUICommandInfo> StartSimulationScene;
	
	const static FString StepSimulationSceneIdentifier;
	TSharedPtr<FUICommandInfo> StepSimulationScene;
	
	// @todo(brice) Remove Example Tools
	//const static FString BeginAttributeEditorToolIdentifier;
	//TSharedPtr<FUICommandInfo> BeginAttributeEditorTool;
	//
	//const static FString BeginMeshSelectionToolIdentifier;
	//TSharedPtr<FUICommandInfo> BeginMeshSelectionTool;
};

//@todo(brice) Merge this into the above class
class DATAFLOWEDITOR_API FDataflowEditorCommands
{
public:
	typedef TFunction<void(const FDataflowNode*, const FDataflowOutput*)> FGraphEvaluationCallback;
	typedef TFunction<void(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)> FOnDragDropEventCallback;

	static void Register();
	static void Unregister();
	static bool IsRegistered();

	static const FDataflowEditorCommandsImpl& Get();

	/*
	* Node evaluation utility function.
	* 
	* @param Context  The evaluation context.
	* @param InOutLastNodeTimestamp  The last evaluation time used to trigger the evaluation when the node's timestamp is more recent than this value. If the node is evaluated, the value also gets updated with the evaluated node's current timestamp.
	* @param Dataflow  The dataflow asset used to search for the NodeName when Node is nullptr.
	* @param Node  The node to evaluate. When null, a node with the given NodeName will be evaluated instead if it exists.
	* @param Output  The node's output to evaluate. When no output are specified, all outputs will be evaluated.
	* @param NodeName  When no node is specified, then the node will be searched instead within the Dataflow's graph using NodeName, otherwise NodeName is ignored.
	* @param Asset  When Asset is non null, if the node is a terminal node, and if the node timestamp is more recent than InOutLastNodeTimestamp. then the node SetAssetValue method will be called on this asset.
	* @return  The node that has been evaluated if any.
	*/
	static const FDataflowNode* EvaluateNode(UE::Dataflow::FContext& Context, UE::Dataflow::FTimestamp& InOutLastNodeTimestamp,
		const UDataflow* Dataflow, const FDataflowNode* Node, const FDataflowOutput* Output = nullptr, 
		const FString& NodeName = FString(), UObject* Asset = nullptr);

	/*
	*  DeleteNodes
	*/
	static void DeleteNodes(UDataflow* Graph, const FGraphPanelSelectionSet& SelectedNodes);

	/*
	* OnNodeVerifyTitleCommit
	*/
	static bool OnNodeVerifyTitleCommit(const FText& NewText, UEdGraphNode* GraphNode, FText& OutErrorMessage);

	/*
	* OnNodeTitleCommitted
	*/
	static void OnNodeTitleCommitted(const FText& InNewText, ETextCommit::Type InCommitType, UEdGraphNode* GraphNode);

	/*
	* OnNotifyPropertyPreChange
	*/
	static void OnNotifyPropertyPreChange(TSharedPtr<IStructureDetailsView> PropertiesEditor, UDataflow* Graph, class FEditPropertyChain* PropertyAboutToChange);

	/*
	*  OnPropertyValueChanged
	*/
	static void OnPropertyValueChanged(UDataflow* Graph, TSharedPtr<UE::Dataflow::FEngineContext>& Context, UE::Dataflow::FTimestamp& OutLastNodeTimestamp, const FPropertyChangedEvent& PropertyChangedEvent, const TSet<TObjectPtr<UObject>>& NewSelection = TSet<TObjectPtr<UObject>>());
	static void OnAssetPropertyValueChanged(TObjectPtr<UDataflowBaseContent> Content, const FPropertyChangedEvent& PropertyChangedEvent);

	/*
	*  OnSelectedNodesChanged
	*/
	static void OnSelectedNodesChanged(TSharedPtr<IStructureDetailsView> PropertiesEditor, UObject* Asset, UDataflow* Graph, const TSet<TObjectPtr<UObject>>& NewSelection);

	/*
	*  ToggleEnabledState
	*/
	static void ToggleEnabledState(UDataflow* Graph);

	/*
	*  DuplicateNodes
	*/
	static void DuplicateNodes(UDataflow* Graph, const TSharedPtr<SDataflowGraphEditor>& DataflowGraphEditor, const FGraphPanelSelectionSet& SelectedNodes);

	/*
	*  CopyNodes
	*/
	static void CopyNodes(UDataflow* Graph, const TSharedPtr<SDataflowGraphEditor>& DataflowGraphEditor, const FGraphPanelSelectionSet& SelectedNodes);

	/*
	*  PasteSelectedNodes
	*/
	static void PasteNodes(UDataflow* Graph, const TSharedPtr<SDataflowGraphEditor>& DataflowGraphEditor);

	/*
	*  RenameNode
	*/
	static void RenameNode(const TSharedPtr<SDataflowGraphEditor>& DataflowGraphEditor, UEdGraphNode* EdNode);
};


