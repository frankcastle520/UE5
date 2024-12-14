// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorViewportClient.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "InputBehaviorSet.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"

class UChaosClothComponent;
class UChaosClothAssetEditorMode;
class UTransformProxy;
class UCombinedTransformGizmo;
class FTransformGizmoDataBinder;

namespace UE::Chaos::ClothAsset
{

class FChaosClothAssetEditorToolkit;
class FChaosClothPreviewScene;
class FClothEditorSimulationVisualization;

/**
 * Viewport client for the 3d sim preview in the cloth editor. Currently same as editor viewport
 * client but doesn't allow editor gizmos/widgets.
 */
class CHAOSCLOTHASSETEDITOR_API FChaosClothAssetEditor3DViewportClient : public FEditorViewportClient, 
	public TSharedFromThis<FChaosClothAssetEditor3DViewportClient>, 
	public IClickBehaviorTarget,
	public IClickDragBehaviorTarget,
	public IInputBehaviorSource
{
public:

	FChaosClothAssetEditor3DViewportClient(FEditorModeTools* InModeTools, TSharedPtr<FChaosClothPreviewScene> InPreviewScene, 
		TSharedPtr<FClothEditorSimulationVisualization> InVisualization,
		const TWeakPtr<SEditorViewport>& InEditorViewportWidget = nullptr);

	// Call this after construction to initialize callbacks when settings change
	void RegisterDelegates();

	virtual ~FChaosClothAssetEditor3DViewportClient();

	// Delete the viewport gizmo and transform proxy
	void DeleteViewportGizmo();

	void ClearSelectedComponents();

	void EnableSimMeshWireframe(bool bEnable) { bSimMeshWireframe = bEnable; }
	bool SimMeshWireframeEnabled() const { return bSimMeshWireframe; }

	void EnableRenderMeshWireframe(bool bEnable);
	bool RenderMeshWireframeEnabled() const { return bRenderMeshWireframe; }

	void SetClothEdMode(TObjectPtr<UChaosClothAssetEditorMode> ClothEdMode);
	void SetClothEditorToolkit(TWeakPtr<const FChaosClothAssetEditorToolkit> ClothToolkit);

	void SoftResetSimulation();
	void HardResetSimulation();
	void SuspendSimulation();
	void ResumeSimulation();
	bool IsSimulationSuspended() const;
	void SetEnableSimulation(bool bEnable);
	bool IsSimulationEnabled() const;

	// LODIndex == INDEX_NONE is LOD Auto
	void SetLODModel(int32 LODIndex);
	bool IsLODModelSelected(int32 LODIndex) const;
	int32 GetLODModel() const;
	int32 GetNumLODs() const;

	FBox PreviewBoundingBox() const;

	TWeakPtr<FChaosClothPreviewScene> GetClothPreviewScene();
	TWeakPtr<const FChaosClothPreviewScene> GetClothPreviewScene() const;
	UChaosClothComponent* GetPreviewClothComponent();
	const UChaosClothComponent* GetPreviewClothComponent() const;
	TWeakPtr<FClothEditorSimulationVisualization> GetSimulationVisualization() {
		return ClothEditorSimulationVisualization;
	}
	TWeakPtr<const FChaosClothAssetEditorToolkit> GetClothToolKit() const { return ClothToolkit; }

private:

	// FGCObject override
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	// FEditorViewportClient overrides
	virtual bool CanSetWidgetMode(UE::Widget::EWidgetMode NewMode) const override { return false; }
	virtual void SetWidgetMode(UE::Widget::EWidgetMode NewMode) override {}
	virtual UE::Widget::EWidgetMode GetWidgetMode() const override { return UE::Widget::EWidgetMode::WM_None; }
	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) override;

	// IClickBehaviorTarget
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	// IClickDragBehaviorTarget
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override {}
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override {}
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override {}
	virtual void OnTerminateDragSequence() override {}

	// IInputBehaviorSource
	virtual const UInputBehaviorSet* GetInputBehaviors() const override;

	void OnAssetViewerSettingsChanged(const FName& InPropertyName);
	void SetAdvancedShowFlagsForScene(const bool bAdvancedShowFlags);

	void ComponentSelectionChanged(UObject* NewSelection);

	// Update the selected components based on hitproxy
	void UpdateSelection(HHitProxy* HitProxy);

	TWeakPtr<FChaosClothPreviewScene> ClothPreviewScene;

	TObjectPtr<UChaosClothAssetEditorMode> ClothEdMode;

	TWeakPtr<const FChaosClothAssetEditorToolkit> ClothToolkit;

	TWeakPtr<FClothEditorSimulationVisualization> ClothEditorSimulationVisualization;
	
	bool bSimMeshWireframe = true;
	bool bRenderMeshWireframe = false;

	// Dataflow render support
	UE::Dataflow::FTimestamp LastModifiedTimestamp = UE::Dataflow::FTimestamp::Invalid;

	// Gizmo support
	TObjectPtr<UTransformProxy> TransformProxy = nullptr;
	TObjectPtr<UCombinedTransformGizmo> Gizmo = nullptr;
	TSharedPtr<FTransformGizmoDataBinder> DataBinder = nullptr;

	TObjectPtr<UInputBehaviorSet> InputBehaviorSet;
};
} // namespace UE::Chaos::ClothAsset
