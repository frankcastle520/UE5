// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPersonaEditMode.h"

class FPhysicsControlAssetEditor;
class FPhysicsControlAssetEditorData;
class FEditorViewportClient;
class UFont;

/**
 * The Physics Control Asset Editor edit mode.
 */
class PHYSICSCONTROLEDITOR_API FPhysicsControlAssetEditorEditMode : public IPersonaEditMode
{
public:
	/** The name of the mode. */
	static FName ModeName;

	FPhysicsControlAssetEditorEditMode();

	void SetEditorData(
		const TSharedRef<FPhysicsControlAssetEditor> InPhysicsControlAssetEditor,
		TSharedPtr<FPhysicsControlAssetEditorData>   InEditorData);

	// IPersonaEditMode overrides.
	virtual bool GetCameraTarget(FSphere& OutTarget) const override;
	virtual class IPersonaPreviewScene& GetAnimPreviewScene() const override;
	virtual void GetOnScreenDebugInfo(TArray<FText>& OutDebugInfo) const override {}
	virtual void Exit() override;
	// ~END IPersonaEditMode overrides.

	// FEdMode overrides.
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual bool InputAxis(FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime) override;
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const override { return true; }
	virtual bool AllowWidgetMove() override { return false; }
	virtual bool ShouldDrawWidget() const override { return false; }
	virtual bool UsesTransformWidget() const override { return false; }
	virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const override { return false; }
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual bool ReceivedFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport);
	virtual bool LostFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport);
	// ~END FEdMode overrides.

protected:
	/** Simulation mouse forces */
	bool SimMousePress(FEditorViewportClient* InViewportClient, FKey Key);
	void SimMouseMove(FEditorViewportClient* InViewportClient, float DeltaX, float DeltaY);
	bool SimMouseRelease();
	bool SimMouseWheelUp(FEditorViewportClient* InViewportClient);
	bool SimMouseWheelDown(FEditorViewportClient* InViewportClient);

	void OpenBodyMenu(FEditorViewportClient* InViewportClient);
	void OpenSelectionMenu(FEditorViewportClient* InViewportClient);

	/** Called when no scene proxy is hit, deselects everything */
	void HitNothing(FEditorViewportClient* InViewportClient);

protected:
	TWeakPtr<FPhysicsControlAssetEditor> PhysicsControlAssetEditor;

	TSharedPtr<FPhysicsControlAssetEditorData> EditorData;

	/** Font used for drawing debug text to the viewport */
	UFont* PhysicsControlAssetEditorFont;

	/** Misc consts */
	const float	SimHoldDistanceChangeDelta;
	const float	SimMinHoldDistance;
	const float SimGrabMoveSpeed;

	// For mouse forces TODO Danny surely this doesn't need to be here unless its actually shared!
	FIntPoint LastClickPos;
	FVector LastClickOrigin;
	FVector LastClickDirection;
	FVector LastClickHitPos;
	FVector LastClickHitNormal;
	bool bLastClickHit;

	/** Simulation mouse forces */
	float SimGrabPush;
	float SimGrabMinPush;
	FVector SimGrabLocation;
	FVector SimGrabX;
	FVector SimGrabY;
	FVector SimGrabZ;

	/** For interacting with the asset while the simulation is running */
	TArray<FTransform> StartManRelConTM;
	TArray<FTransform> StartManParentConTM;
	TArray<FTransform> StartManChildConTM;
	float DragX;
	float DragY;

};
