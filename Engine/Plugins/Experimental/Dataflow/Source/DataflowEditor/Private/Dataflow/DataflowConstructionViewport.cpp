// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowConstructionViewport.h"

#include "Dataflow/DataflowActor.h"
#include "Dataflow/DataflowEditorCommands.h"
#include "Dataflow/DataflowEditorMode.h"
#include "Dataflow/DataflowConstructionViewportClient.h"
#include "Dataflow/DataflowEditorToolkit.h"
#include "EditorModeManager.h"
#include "Dataflow/DataflowContent.h"
#include "Dataflow/DataflowConstructionViewportToolbar.h"
#include "Dataflow/DataflowConstructionScene.h"
#include "Dataflow/DataflowEditorPreviewSceneBase.h"
#include "Dataflow/DataflowRenderingViewMode.h"
#include "Dataflow/DataflowSimulationPanel.h"

#define LOCTEXT_NAMESPACE "SDataflowConstructionViewport"


SDataflowConstructionViewport::SDataflowConstructionViewport()
{
}

void SDataflowConstructionViewport::Construct(const FArguments& InArgs, const FAssetEditorViewportConstructionArgs& InViewportConstructionArgs)
{
	SAssetEditorViewport::FArguments ParentArgs;
	ParentArgs._EditorViewportClient = InArgs._ViewportClient;
	SAssetEditorViewport::Construct(ParentArgs, InViewportConstructionArgs);
	Client->VisibilityDelegate.BindSP(this, &SDataflowConstructionViewport::IsVisible);
}

TSharedPtr<SWidget> SDataflowConstructionViewport::MakeViewportToolbar()
{
	return SNew(SDataflowConstructionViewportSelectionToolBar, SharedThis(this))
		.CommandList(CommandList);
}

void SDataflowConstructionViewport::OnFocusViewportToSelection()
{
	const UDataflowEditorMode* const DataflowEdMode = GetEdMode();

	if (DataflowEdMode)
	{
		const FBox BoundingBox = DataflowEdMode->SelectionBoundingBox();
		if (BoundingBox.IsValid && !(BoundingBox.Min == FVector::Zero() && BoundingBox.Max == FVector::Zero()))
		{
			Client->FocusViewportOnBox(BoundingBox);
		}
	}
}

UDataflowEditorMode* SDataflowConstructionViewport::GetEdMode() const
{
	if (const FEditorModeTools* const EditorModeTools = Client->GetModeTools())
	{
		if (UDataflowEditorMode* const DataflowEdMode = Cast<UDataflowEditorMode>(EditorModeTools->GetActiveScriptableMode(UDataflowEditorMode::EM_DataflowEditorModeId)))
		{
			return DataflowEdMode;
		}
	}
	return nullptr;
}

void SDataflowConstructionViewport::BindCommands()
{
	SAssetEditorViewport::BindCommands();

	const FDataflowEditorCommandsImpl& CommandInfos = FDataflowEditorCommands::Get();

	for (const TPair<FName, TSharedPtr<FUICommandInfo>>& SetViewModeCommand : CommandInfos.SetConstructionViewModeCommands)
	{
		CommandList->MapAction(
			SetViewModeCommand.Value,
			FExecuteAction::CreateLambda([this, ViewModeName = SetViewModeCommand.Key]()
			{
				if (UDataflowEditorMode* const EdMode = GetEdMode())
				{
					EdMode->SetConstructionViewMode(ViewModeName);
				}
			}),
			FCanExecuteAction::CreateLambda([this, ViewModeName = SetViewModeCommand.Key]()
			{ 
				if (const UDataflowEditorMode* const EdMode = GetEdMode())
				{
					return EdMode->CanChangeConstructionViewModeTo(ViewModeName);
				}
				return false; 
			}),
			FIsActionChecked::CreateLambda([this, ViewModeName = SetViewModeCommand.Key]()
			{
				if (const UDataflowEditorMode* const EdMode = GetEdMode())
				{
					return EdMode->GetConstructionViewMode()->GetName() == ViewModeName;
				}
				return false;
			})
		);
	}
}

bool SDataflowConstructionViewport::IsVisible() const
{
	// Intentionally not calling SEditorViewport::IsVisible because it will return false if our simulation is more than 250ms.
	return ViewportWidget.IsValid();
}

TSharedRef<class SEditorViewport> SDataflowConstructionViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SDataflowConstructionViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}

void SDataflowConstructionViewport::OnFloatingButtonClicked()
{
}


#undef LOCTEXT_NAMESPACE
