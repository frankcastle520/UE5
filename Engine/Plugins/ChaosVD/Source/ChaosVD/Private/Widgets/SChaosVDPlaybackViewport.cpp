// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SChaosVDPlaybackViewport.h"

#include "ChaosVDCommands.h"
#include "ChaosVDEditorMode.h"
#include "ChaosVDPlaybackController.h"
#include "ChaosVDPlaybackViewportClient.h"
#include "ChaosVDScene.h"
#include "EditorModeManager.h"
#include "SChaosVDGameFramesPlaybackControls.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SChaosVDTimelineWidget.h"
#include "Widgets/SChaosVDViewportToolbar.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

namespace Chaos::VisualDebugger::Cvars
{
	static bool bBroadcastGameFrameUpdateEvenIfNotChanged = false;
	static FAutoConsoleVariableRef CVarChaosVDBroadcastGameFrameUpdateEvenIfNotChanged(
		TEXT("p.Chaos.VD.Tool.BroadcastGameFrameUpdateEvenIfNotChanged"),
		bBroadcastGameFrameUpdateEvenIfNotChanged,
		TEXT("If true, each time we get a controller data updated event, a game frame update will be triggered even if the frame didn't change..."));
}

SChaosVDPlaybackViewport::~SChaosVDPlaybackViewport()
{
	if (ExternalInvalidateHandlerHandle.IsValid())
	{
		ExternalViewportInvalidationRequestHandler.Remove(ExternalInvalidateHandlerHandle);
		ExternalInvalidateHandlerHandle.Reset();
	}

	UnbindFromSceneUpdateEvents();

	PlaybackViewportClient->Viewport = nullptr;
	PlaybackViewportClient.Reset();
}

void SChaosVDPlaybackViewport::Construct(const FArguments& InArgs, TWeakPtr<FChaosVDScene> InScene, TWeakPtr<FChaosVDPlaybackController> InPlaybackController, TSharedPtr<FEditorModeTools> InEditorModeTools)
{
	Extender = MakeShared<FExtender>();

	EditorModeTools = InEditorModeTools;
	EditorModeTools->SetWidgetMode(UE::Widget::WM_Translate);
	EditorModeTools->SetDefaultMode(UChaosVDEditorMode::EM_ChaosVisualDebugger);
	EditorModeTools->ActivateDefaultMode();

	SEditorViewport::Construct(SEditorViewport::FArguments());

	CVDSceneWeakPtr = InScene;
	TSharedPtr<FChaosVDScene> ScenePtr = InScene.Pin();
	ensure(ScenePtr.IsValid());
	ensure(InPlaybackController.IsValid());

	PlaybackViewportClient = StaticCastSharedPtr<FChaosVDPlaybackViewportClient>(GetViewportClient());

	// TODO: Add a way to gracefully shutdown (close) the tool when a no recoverable situation like this happens (UE-191876)
	check(PlaybackViewportClient.IsValid());

	PlaybackViewportClient->SetScene(InScene);
	
	if (UChaosVDEditorMode* CVDEdMode = Cast<UChaosVDEditorMode>(EditorModeTools->GetActiveScriptableMode(UChaosVDEditorMode::EM_ChaosVisualDebugger)))
	{
		if (ScenePtr.IsValid())
		{
			CVDEdMode->SetWorld(ScenePtr->GetUnderlyingWorld());
		}
	}

	ChildSlot
	[
		// 3D Viewport
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.FillHeight(0.9f)
		[
			ViewportWidget.ToSharedRef()
		]
		// Playback controls
		// TODO: Now that the tool is In-Editor, see if we can/is worth use the Sequencer widgets
		// instead of these custom ones
		+SVerticalBox::Slot()
		.Padding(16.0f, 16.0f, 16.0f, 16.0f)
		.FillHeight(0.1f)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("PlaybackViewportWidgetGameFramesLabel", "Game Frames" ))
			]
			+SVerticalBox::Slot()
			[
				SAssignNew(GameFramesPlaybackControls, SChaosVDGameFramesPlaybackControls, InPlaybackController)
			]
		]
	];

	ExternalInvalidateHandlerHandle = ExternalViewportInvalidationRequestHandler.AddSP(this, &SChaosVDPlaybackViewport::HandleExternalViewportInvalidateRequest);

	RegisterNewController(InPlaybackController);
}

TSharedRef<SEditorViewport> SChaosVDPlaybackViewport::GetViewportWidget()
{
	return StaticCastSharedRef<SEditorViewport>(AsShared());
}

TSharedPtr<FExtender> SChaosVDPlaybackViewport::GetExtenders() const
{
	return Extender;
}

void SChaosVDPlaybackViewport::BindCommands()
{
	SEditorViewport::BindCommands();

	const FChaosVDCommands& Commands = FChaosVDCommands::Get();

	if (ensure(Client))
	{
		const TSharedRef<FChaosVDPlaybackViewportClient> ViewportClientRef = StaticCastSharedRef<FChaosVDPlaybackViewportClient>(Client.ToSharedRef());
		FUIAction ToggleObjectTrackingAction;
		ToggleObjectTrackingAction.ExecuteAction.BindSP(ViewportClientRef, &FChaosVDPlaybackViewportClient::ToggleObjectTrackingIfSelected);
		ToggleObjectTrackingAction.GetActionCheckState.BindLambda([WeakViewportClient = ViewportClientRef.ToWeakPtr()]()
		{
			TSharedPtr<const FChaosVDPlaybackViewportClient> ViewportPtr = WeakViewportClient.Pin();
			return ViewportPtr.IsValid() && ViewportPtr->IsAutoTrackingSelectedObject() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		});
		CommandList->MapAction(Commands.ToggleFollowSelectedObject, ToggleObjectTrackingAction);

		FUIAction ToggleOverrideFrameRateAction;
		ToggleOverrideFrameRateAction.ExecuteAction.BindSP(SharedThis(this), &SChaosVDPlaybackViewport::ToggleUseFrameRateOverride);
		ToggleOverrideFrameRateAction.GetActionCheckState.BindLambda([WeakThis = AsWeak()]()
		{
			TSharedPtr<const SChaosVDPlaybackViewport> ViewportPtr = StaticCastSharedPtr<const SChaosVDPlaybackViewport>(WeakThis.Pin());
			return ViewportPtr.IsValid() && ViewportPtr->IsUsingFrameRateOverride() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		});
		CommandList->MapAction(Commands.OverridePlaybackFrameRate, ToggleOverrideFrameRateAction);

		FUIAction ToggleTranslucentGeometrySelectionAction;
		ToggleTranslucentGeometrySelectionAction.ExecuteAction.BindSP(ViewportClientRef, &FChaosVDPlaybackViewportClient::ToggleCanSelectTranslucentGeometry);
		ToggleTranslucentGeometrySelectionAction.GetActionCheckState.BindLambda([WeakViewportClient = ViewportClientRef.ToWeakPtr()]()
		{
			TSharedPtr<const FChaosVDPlaybackViewportClient> ViewportPtr = WeakViewportClient.Pin();
			return ViewportPtr.IsValid() && ViewportPtr->GetCanSelectTranslucentGeometry() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		});
		CommandList->MapAction(Commands.AllowTranslucentSelection, ToggleTranslucentGeometrySelectionAction);
	}
}

EVisibility SChaosVDPlaybackViewport::GetTransformToolbarVisibility() const
{
	// We want to always show the transform tool bar. We disable each action that is not supported for a selected actor individually.
	// Without doing this, if you select an unsupported mode, the entire toolbar disappears
	return EVisibility::Visible;
}

void SChaosVDPlaybackViewport::GoToLocation(const FVector& InLocation) const
{
	if (PlaybackViewportClient)
	{
		PlaybackViewportClient->GoToLocation(InLocation);
	}
}

void SChaosVDPlaybackViewport::ToggleUseFrameRateOverride()
{
	if (TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin())
	{
		PlaybackControllerPtr->ToggleUseFrameRateOverride();
	}
}

bool SChaosVDPlaybackViewport::IsUsingFrameRateOverride() const
{
	if (TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin())
	{
		return PlaybackControllerPtr->IsUsingFrameRateOverride();
	}

	return false;
}

int32 SChaosVDPlaybackViewport::GetCurrentTargetFrameRateOverride() const
{
	TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin();
	return PlaybackControllerPtr ? PlaybackControllerPtr->GetFrameRateOverride() : FChaosVDPlaybackController::InvalidFrameRateOverride;
}

void SChaosVDPlaybackViewport::SetCurrentTargetFrameRateOverride(int32 NewTarget)
{
	if (TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin())
	{
		PlaybackControllerPtr->SetFrameRateOverride(NewTarget);
	}
}

void SChaosVDPlaybackViewport::ExecuteExternalViewportInvalidateRequest()
{
	ExternalViewportInvalidationRequestHandler.Broadcast();
}

void SChaosVDPlaybackViewport::OnFocusViewportToSelection()
{
	PlaybackViewportClient->FocusOnSelectedObject();
}

TSharedRef<FEditorViewportClient> SChaosVDPlaybackViewport::MakeEditorViewportClient()
{
	TSharedPtr<FChaosVDPlaybackViewportClient> NewViewport = MakeShared<FChaosVDPlaybackViewportClient>(EditorModeTools, GetViewportWidget());

	NewViewport->SetAllowCinematicControl(false);
	
	NewViewport->bSetListenerPosition = false;
	NewViewport->EngineShowFlags = FEngineShowFlags(ESFIM_Editor);
	NewViewport->LastEngineShowFlags = FEngineShowFlags(ESFIM_Editor);
	NewViewport->ViewportType = LVT_Perspective;
	NewViewport->bDrawAxes = true;
	NewViewport->bDisableInput = false;
	NewViewport->VisibilityDelegate.BindLambda([] {return true; });

	NewViewport->EngineShowFlags.DisableAdvancedFeatures();
	NewViewport->EngineShowFlags.SetSelectionOutline(true);
	NewViewport->EngineShowFlags.SetSnap(false);
	NewViewport->EngineShowFlags.SetBillboardSprites(true);

	return StaticCastSharedRef<FEditorViewportClient>(NewViewport.ToSharedRef());
}

TSharedPtr<SWidget> SChaosVDPlaybackViewport::MakeViewportToolbar()
{
	// Build our toolbar level toolbar
	TSharedRef< SChaosVDViewportToolbar > ToolBar = SNew(SChaosVDViewportToolbar, SharedThis(this));

	return 
		SNew(SVerticalBox)
		.Visibility( EVisibility::SelfHitTestInvisible )
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 1.0f, 0, 0)
		.VAlign(VAlign_Top)
		[
			ToolBar
		];
}

void SChaosVDPlaybackViewport::HandlePlaybackControllerDataUpdated(TWeakPtr<FChaosVDPlaybackController> InController)
{
	if (PlaybackController != InController)
	{
		RegisterNewController(InController);
	}

	PlaybackViewportClient->bNeedsRedraw = true;
}

void SChaosVDPlaybackViewport::HandlePostSelectionChange(const UTypedElementSelectionSet* ChangesSelectionSet)
{
	PlaybackViewportClient->bNeedsRedraw = true;
}

void SChaosVDPlaybackViewport::OnPlaybackSceneUpdated()
{
	PlaybackViewportClient->HandleCVDSceneUpdated();
}

void SChaosVDPlaybackViewport::OnSolverVisibilityUpdated(int32 SolverID, bool bNewVisibility)
{
	PlaybackViewportClient->HandleCVDSceneUpdated();	
}

void SChaosVDPlaybackViewport::BindToSceneUpdateEvents()
{	
	if (const TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin())
	{
		if (TSharedPtr<FChaosVDScene> ScenePtr = PlaybackControllerPtr->GetControllerScene().Pin())
		{
			ScenePtr->OnSceneUpdated().AddSP(this, &SChaosVDPlaybackViewport::OnPlaybackSceneUpdated);
			ScenePtr->OnSolverVisibilityUpdated().AddSP(this, &SChaosVDPlaybackViewport::OnSolverVisibilityUpdated);
		}
	}
}

void SChaosVDPlaybackViewport::UnbindFromSceneUpdateEvents()
{
	if (const TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin())
	{
		if (TSharedPtr<FChaosVDScene> ScenePtr = PlaybackControllerPtr->GetControllerScene().Pin())
		{
			ScenePtr->OnSceneUpdated().RemoveAll(this);
			ScenePtr->OnSolverVisibilityUpdated().RemoveAll(this);
		}
	}
}

void SChaosVDPlaybackViewport::RegisterNewController(TWeakPtr<FChaosVDPlaybackController> NewController)
{
	if (PlaybackController != NewController)
	{
		UnbindFromSceneUpdateEvents();	

		FChaosVDPlaybackControllerObserver::RegisterNewController(NewController);

		BindToSceneUpdateEvents();	
	}
}

void SChaosVDPlaybackViewport::HandleExternalViewportInvalidateRequest()
{
	if (PlaybackViewportClient)
	{
		PlaybackViewportClient->Invalidate();
	}
}

#undef LOCTEXT_NAMESPACE
