// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackEditorThumbnail/TrackThumbnailUtils.h"

#include "Camera/CameraTypes.h"
#include "CanvasTypes.h"
#include "EngineModule.h"
#include "LegacyScreenPercentageDriver.h"
#include "MovieSceneToolsUserSettings.h"
#include "SceneViewExtension.h"

namespace UE::MoveSceneTools
{
	void PreDrawThumbnailSetupSequencer(ISequencer& Sequencer, FFrameTime CaptureFrame)
	{
		Sequencer.EnterSilentMode();
		Sequencer.SetPlaybackStatus(EMovieScenePlayerStatus::Jumping);
		Sequencer.SetLocalTimeDirectly(CaptureFrame);
		Sequencer.ForceEvaluate();
	}

	void PostDrawThumbnailCleanupSequencer(ISequencer& Sequencer)
	{
		Sequencer.ExitSilentMode();
	}

	void DrawViewportThumbnail(
		FRenderTarget& ThumbnailRenderTarget,
		const FIntPoint& RenderTargetSize,
		FSceneInterface& Scene,
		const FMinimalViewInfo& ViewInfo,
		EThumbnailQuality Quality
		)
	{
		FSceneViewFamilyContext ViewFamily( FSceneViewFamily::ConstructionValues(&ThumbnailRenderTarget, &Scene, FEngineShowFlags(ESFIM_Game))
		.SetTime(FGameTime::GetTimeSinceAppStart())
		.SetResolveScene(true));

		FSceneViewStateInterface* ViewStateInterface = nullptr;

		// Screen percentage is not supported in thumbnail.
		ViewFamily.EngineShowFlags.ScreenPercentage = false;

		switch (Quality)
		{
		case EThumbnailQuality::Draft:
			ViewFamily.EngineShowFlags.DisableAdvancedFeatures();
			ViewFamily.EngineShowFlags.SetPostProcessing(false);
			break;

		case EThumbnailQuality::Normal:
		case EThumbnailQuality::Best:
			ViewFamily.EngineShowFlags.SetMotionBlur(false);

			// Default eye adaptation requires a viewstate.
			ViewFamily.EngineShowFlags.EyeAdaptation = true;
			UMovieSceneUserThumbnailSettings* ThumbnailSettings = GetMutableDefault<UMovieSceneUserThumbnailSettings>();
			FSceneViewStateInterface* Ref = ThumbnailSettings->ViewState.GetReference();
			if (!Ref)
			{
				ThumbnailSettings->ViewState.Allocate(ViewFamily.GetFeatureLevel());
			}
			ViewStateInterface = ThumbnailSettings->ViewState.GetReference();
			break;
		}

		FSceneViewInitOptions ViewInitOptions;

		// Use target exposure without blend. 
		ViewInitOptions.bInCameraCut = true;
		ViewInitOptions.SceneViewStateInterface = ViewStateInterface;

		ViewInitOptions.BackgroundColor = FLinearColor::Black;
		ViewInitOptions.SetViewRectangle(FIntRect(FIntPoint::ZeroValue, RenderTargetSize));
		ViewInitOptions.ViewFamily = &ViewFamily;

		ViewInitOptions.ViewOrigin = ViewInfo.Location;
		ViewInitOptions.ViewRotationMatrix = FInverseRotationMatrix(ViewInfo.Rotation) * FMatrix(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));

		ViewInitOptions.ProjectionMatrix = ViewInfo.CalculateProjectionMatrix();

		FSceneView* NewView = new FSceneView(ViewInitOptions);
		ViewFamily.Views.Add(NewView);

		const float GlobalResolutionFraction = 1.f;
		ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(ViewFamily, GlobalResolutionFraction));

		FCanvas Canvas(&ThumbnailRenderTarget, nullptr, FGameTime::GetTimeSinceAppStart(), Scene.GetFeatureLevel());
		Canvas.Clear(FLinearColor::Transparent);

		ViewFamily.ViewExtensions = GEngine->ViewExtensions->GatherActiveExtensions(FSceneViewExtensionContext(&Scene));
		for (const FSceneViewExtensionRef& Extension : ViewFamily.ViewExtensions)
		{
			Extension->SetupViewFamily(ViewFamily);
			Extension->SetupView(ViewFamily, *NewView);
		}

		GetRendererModule().BeginRenderingViewFamily(&Canvas, &ViewFamily);
	}
}
