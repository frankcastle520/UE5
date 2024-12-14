// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowComponentSelectionState.h"
#include "Dataflow/DataflowContent.h"
#include "Delegates/Delegate.h"
#include "InputBehaviorSet.h"


class FDataflowEditorToolkit;
class ADataflowActor;
class UTransformProxy;
class UCombinedTransformGizmo;
class FTransformGizmoDataBinder;
class FDataflowPreviewSceneBase;
class UInputBehaviorSet;
class USelection;
class FDataflowConstructionViewportClient;
namespace UE::Dataflow
{
	class IDataflowConstructionViewMode;
}

class DATAFLOWEDITOR_API FDataflowConstructionViewportClient : public FEditorViewportClient
{
public:
	using Super = FEditorViewportClient;

	FDataflowConstructionViewportClient(FEditorModeTools* InModeTools, FPreviewScene* InPreviewScene,  const bool bCouldTickScene,
								  const TWeakPtr<SEditorViewport> InEditorViewportWidget = nullptr);

	void SetConstructionViewMode(const UE::Dataflow::IDataflowConstructionViewMode* InViewMode);

	// IInputBehaviorSource
	// virtual const UInputBehaviorSet* GetInputBehaviors() const override;
	USelection* GetSelectedComponents();

	/** Set the data flow toolkit used to create the client*/
	void SetDataflowEditorToolkit(TWeakPtr<FDataflowEditorToolkit> DataflowToolkit);
	
	/** Get the data flow toolkit  */
	const TWeakPtr<FDataflowEditorToolkit>& GetDataflowEditorToolkit() const { return DataflowEditorToolkitPtr; }

	/** Set the tool command list */
	void SetToolCommandList(TWeakPtr<FUICommandList> ToolCommandList);

	// FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FDataflowConstructionViewportClient"); }

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSelectionChangedMulticast, const TArray<UPrimitiveComponent*>&)
	FOnSelectionChangedMulticast OnSelectionChangedMulticast;

private:

	// FEditorViewportClient interface
	virtual void Tick(float DeltaSeconds) override;
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) override;

	/** Toolkit used to create the viewport client */
	TWeakPtr<FDataflowEditorToolkit> DataflowEditorToolkitPtr = nullptr;

	/** Dataflow preview scene from the toolkit */
	FDataflowPreviewSceneBase* PreviewScene = nullptr;

	// @todo(brice) : Is this needed?
	TWeakPtr<FUICommandList> ToolCommandList;

	/** Construction view mode */
	const UE::Dataflow::IDataflowConstructionViewMode* ConstructionViewMode = nullptr;

	/** Behavior set for the behavior UI */
	TObjectPtr<UInputBehaviorSet> BehaviorSet;
	
	/** Flag to enable scene ticking from the client */
	bool bEnableSceneTicking = false;

	// Saved view transforms for the currently inactive view modes (e.g. store the 3D camera here while in 2D mode and vice-versa)
	TMap<FName, FViewportCameraTransform> SavedInactiveViewTransforms;
};
