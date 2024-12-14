// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowComponentSelectionState.h"
#include "Dataflow/DataflowContent.h"
#include "InputBehaviorSet.h"

class FDataflowEditorToolkit;
class ADataflowActor;
class UTransformProxy;
class UCombinedTransformGizmo;
class FTransformGizmoDataBinder;
class FDataflowPreviewSceneBase;
class UInputBehaviorSet;

class DATAFLOWEDITOR_API FDataflowSimulationViewportClient : public FEditorViewportClient //, public IInputBehaviorSource
{
//@todo(brice) : Add BehaviorUI support

public:
	using Super = FEditorViewportClient;

	FDataflowSimulationViewportClient(FEditorModeTools* InModeTools, FPreviewScene* InPreviewScene,  const bool bCouldTickScene,
								  const TWeakPtr<SEditorViewport> InEditorViewportWidget = nullptr);

	/** Set the data flow toolkit used to create the client*/
	void SetDataflowEditorToolkit(TWeakPtr<FDataflowEditorToolkit> DataflowToolkit);
	
	/** Get the data flow toolkit  */
	const TWeakPtr<FDataflowEditorToolkit>& GetDataflowEditorToolkit() const { return DataflowEditorToolkitPtr; }

	/** Set the tool command list */
	void SetToolCommandList(TWeakPtr<FUICommandList> ToolCommandList);

	// FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FDataflowSimulationViewportClient"); }

private:

	// FEditorViewportClient interface
	virtual void Tick(float DeltaSeconds) override;
	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) override;

	/** Toolkit used to create the viewport client */
	TWeakPtr<FDataflowEditorToolkit> DataflowEditorToolkitPtr = nullptr;

	/** Dataflow preview scene from the toolkit */
	FDataflowPreviewSceneBase* PreviewScene = nullptr;

	// @todo(brice) : Is this needed?
	TWeakPtr<FUICommandList> ToolCommandList;

	/** Behavior set for the behavior UI */
	TObjectPtr<UInputBehaviorSet> BehaviorSet;
	
	/** Flag to enable scene ticking from the client */
	bool bEnableSceneTicking = false;
};
