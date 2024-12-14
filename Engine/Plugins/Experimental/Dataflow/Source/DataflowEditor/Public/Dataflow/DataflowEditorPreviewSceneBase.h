// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AdvancedPreviewScene.h"

class UDataflowBaseContent;
class UDataflowEditor;
class FAssetEditorModeManager;

/**
 * Dataflow preview scene base
 * @brief the scene is holding all the objects that will be
 * visible and potentially editable within the viewport
 */
class DATAFLOWEDITOR_API FDataflowPreviewSceneBase : public FAdvancedPreviewScene
{
public:

	FDataflowPreviewSceneBase(FPreviewScene::ConstructionValues ConstructionValues, UDataflowEditor* Editor);
	virtual ~FDataflowPreviewSceneBase();
	
	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	/** Dataflow editor content accessors */
	TObjectPtr<UDataflowBaseContent>& GetEditorContent();
	const TObjectPtr<UDataflowBaseContent>& GetEditorContent() const;

	/** Dataflow terminal contents accessors */
	TArray<TObjectPtr<UDataflowBaseContent>>& GetTerminalContents();
	const TArray<TObjectPtr<UDataflowBaseContent>>& GetTerminalContents() const;

	/** Root scene actor accessors */
	TObjectPtr<AActor> GetRootActor() { return RootSceneActor; }
	const TObjectPtr<AActor> GetRootActor() const { return RootSceneActor; }

	/** Dataflow mode manager accessors */
	TSharedPtr<FAssetEditorModeManager>& GetDataflowModeManager() { return DataflowModeManager; }
	const TSharedPtr<FAssetEditorModeManager>& GetDataflowModeManager() const { return DataflowModeManager; }
	
	/** Build the scene bounding box */
	FBox GetBoundingBox() const;

	/** Tick data flow scene */
	virtual void TickDataflowScene(const float DeltaSeconds) {}

	/** Check if a primitive component is selected */
	bool IsComponentSelected(const UPrimitiveComponent* InComponent) const;

	/** Check if the preview scene can run simulation */
	virtual bool CanRunSimulation() const {return false;}
	
protected:
	
	/** Root scene actor */
	TObjectPtr<AActor> RootSceneActor = nullptr; 

	/** Dataflow editor linked to that preview scene */
	UDataflowEditor* DataflowEditor = nullptr;

	/** Mode Manager for selection */
	TSharedPtr<FAssetEditorModeManager> DataflowModeManager;
};

