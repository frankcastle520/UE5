// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseCharacterFXEditor.h"

#include "Dataflow/DataflowEngine.h"
#include "Dataflow/AssetDefinition_DataflowAsset.h"
#include "Dataflow/DataflowAssetFactory.h"
#include "Dataflow/DataflowEditorCommands.h"
#include "Dataflow/DataflowContent.h"
#include "Dataflow/DataflowSchema.h"
#include "Dataflow/DataflowSNode.h"
#include "Dataflow/DataflowSNodeFactories.h"
#include "Templates/SharedPointer.h"

#include "DataflowEditor.generated.h"

class FDataflowEditorToolkit;
class UDataflowBaseContent;

/** 
 * The actual asset editor class doesn't have that much in it, intentionally. 
 * 
 * Our current asset editor guidelines ask us to place as little business logic as possible
 * into the class, instead putting as much of the non-UI code into the subsystem as possible,
 * and the UI code into the toolkit (which this class owns).
 *
 * However, since we're using a mode and the Interactive Tools Framework, a lot of our business logic
 * ends up inside the mode and the tools, not the subsystem. The front-facing code is mostly in
 * the asset editor toolkit, though the mode toolkit has most of the things that deal with the toolbar
 * on the left.
 */

UCLASS()
class DATAFLOWEDITOR_API UDataflowEditor : public UBaseCharacterFXEditor
{
	GENERATED_BODY()

public:
	UDataflowEditor();

	// UBaseCharacterFXEditor interface
	virtual TSharedPtr<FBaseAssetToolkit> CreateToolkit() override;

	/** Initialize editor contents given a list of objects */
	virtual void Initialize(const TArray<TObjectPtr<UObject>>& InObjects, const TSubclassOf<AActor>& InPreviewClass = nullptr) override;

	/** Update the terminal contents */
	void UpdateTerminalContents(const UE::Dataflow::FTimestamp TimeStamp);
	
	/** Update the editor content */
	void UpdateEditorContent();

	/** Dataflow editor content accessors */
	TObjectPtr<UDataflowBaseContent>& GetEditorContent() { return EditorContent; }
	const TObjectPtr<UDataflowBaseContent>& GetEditorContent() const { return EditorContent; }

	/** Dataflow terminal contents accessors */
	TArray<TObjectPtr<UDataflowBaseContent>>& GetTerminalContents() { return TerminalContents; }
	const TArray<TObjectPtr<UDataflowBaseContent>>& GetTerminalContents() const { return TerminalContents; }

	/** Check if the terminals contents has changed or not */
	bool HasTerminalsDirty() const { return bHasTerminalsDirty; }

private :

	friend class FDataflowEditorToolkit;

	using ValidTerminalsType = TMap<TSharedPtr<FDataflowNode>,TObjectPtr<UDataflowBaseContent>>;

	/** Remove invalid terminal contents from the container */
	void RemoveTerminalContents(const TSharedPtr<UE::Dataflow::FGraph>& DataflowGraph, ValidTerminalsType& ValidTerminals);
	
	/** Add valid terminal nodes to the container */
	void AddTerminalContents(const TSharedPtr<UE::Dataflow::FGraph>& DataflowGraph, ValidTerminalsType& ValidTerminals);
	
	// Dataflow editor is the owner of the object list to edit/process and the dataflow mode
	// is the one holding the dynamic mesh components to be rendered in the viewport
	// It is why the data flow asset/owner/skelmesh have been added here. Could be added
	// in the subsystem if necessary
	UPROPERTY()
	TObjectPtr<UDataflowBaseContent> EditorContent;

	/** List of dataflow contents available in the graph and coming from all the terminal nodes */
	UPROPERTY()
	TArray<TObjectPtr<UDataflowBaseContent>> TerminalContents;

	/** Boolean to know when the terminals has been changed or not */
	bool bHasTerminalsDirty = false;
};

DECLARE_LOG_CATEGORY_EXTERN(LogDataflowEditor, Log, All);
