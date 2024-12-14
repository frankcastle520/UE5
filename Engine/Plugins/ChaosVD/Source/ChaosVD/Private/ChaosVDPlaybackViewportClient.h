// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "EditorViewportClient.h"

class UChaosVDCoreSettings;
struct FChaosVDGameFrameData;
class FChaosVDScene;
enum class EChaosVDActorTrackingMode;

/** Client viewport class used for to handle a Chaos Visual Debugger world Interaction/Rendering.
 * It re-routes interaction events to our Chaos VD scene
 */
class FChaosVDPlaybackViewportClient : public FEditorViewportClient
{
public:

	FChaosVDPlaybackViewportClient(const TSharedPtr<FEditorModeTools>& InModeTools, const TSharedPtr<SEditorViewport>& InEditorViewportWidget);
	virtual ~FChaosVDPlaybackViewportClient() override;

	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;

	void SetScene(TWeakPtr<FChaosVDScene> InScene);

	virtual UWorld* GetWorld() const override { return CVDWorld; };

	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) override;

	void ToggleObjectTrackingIfSelected();
	bool IsAutoTrackingSelectedObject() const { return bAutoTrackSelectedObject; };
	void SetAutoTrackingViewDistance(float NewDistance);
	float GetAutoTrackingViewDistance() const { return TrackingViewDistance; }

	void GoToLocation(const FVector& InLocation);

	void TrackSelectedObject();

	void FocusOnSelectedObject();
	
	virtual void UpdateMouseDelta() override;

	void HandleCVDSceneUpdated();

	bool GetCanSelectTranslucentGeometry() const { return bAllowTranslucentHitProxies; };
	void SetCanSelectTranslucentGeometry(bool bCanSelect);
	void ToggleCanSelectTranslucentGeometry();

private:

	void HandleFocusRequest(FBox BoxToFocusOn);
	void HandleActorMoving(AActor* MovedActor) const;

	FDelegateHandle FocusRequestDelegateHandle;
	UWorld* CVDWorld;
	TWeakPtr<FChaosVDScene> CVDScene;

	bool bAutoTrackSelectedObject = false;
	float TrackingViewDistance = 120.0f;
	bool bAllowTranslucentHitProxies = true;
};
