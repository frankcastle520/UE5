// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSimulationViewport.h"
#include "Dataflow/DataflowActor.h"
#include "Dataflow/DataflowEditorMode.h"
#include "Dataflow/DataflowSimulationViewportClient.h"
#include "Dataflow/DataflowEditorToolkit.h"
#include "EditorModeManager.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Dataflow/DataflowContent.h"
#include "Dataflow/DataflowEditorCommands.h"
#include "Dataflow/DataflowSimulationScene.h"
#include "Dataflow/DataflowSimulationPanel.h"
#include "Dataflow/DataflowSimulationViewportToolbar.h"
#include "Dataflow/DataflowSimulationVisualization.h"
#include "Widgets/Text/SRichTextBlock.h"

#define LOCTEXT_NAMESPACE "SDataflowSimulationViewport"

SDataflowSimulationViewport::SDataflowSimulationViewport()
{
}

const TSharedPtr<FDataflowSimulationScene>& SDataflowSimulationViewport::GetSimulationScene() const
{
	const TSharedPtr<FDataflowSimulationViewportClient> DataflowClient = StaticCastSharedPtr<FDataflowSimulationViewportClient>(Client);
	return DataflowClient->GetDataflowEditorToolkit().Pin()->GetSimulationScene();
}

void SDataflowSimulationViewport::Construct(const FArguments& InArgs, const FAssetEditorViewportConstructionArgs& InViewportConstructionArgs)
{
	SAssetEditorViewport::FArguments ParentArgs;
	ParentArgs._EditorViewportClient = InArgs._ViewportClient;
	SAssetEditorViewport::Construct(ParentArgs, InViewportConstructionArgs);
	Client->VisibilityDelegate.BindSP(this, &SDataflowSimulationViewport::IsVisible);

	if(static_cast<FDataflowSimulationScene*>(Client->GetPreviewScene())->CanRunSimulation())
	{
		TWeakPtr<FDataflowSimulationScene> SimulationScene = GetSimulationScene();

		auto HasCacheAsset = [SimulationScene]()
		{
			if(SimulationScene.Pin() && SimulationScene.Pin()->GetPreviewSceneDescription())
			{
				return (SimulationScene.Pin()->GetPreviewSceneDescription()->CacheAsset != nullptr) ? EVisibility::Visible : EVisibility::Collapsed;
			}
			return EVisibility::Collapsed;
		};
            
		ViewportOverlay->AddSlot()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Bottom)
			.FillWidth(1)
			.Padding(10.0f, 0.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("EditorViewport.OverlayBrush"))
				.Visibility(TAttribute<EVisibility>::Create(HasCacheAsset))
				.Padding(10.0f, 2.0f)
				[
					SNew(SDataflowSimulationPanel, SimulationScene)
					.ViewInputMin(this, &SDataflowSimulationViewport::GetViewMinInput)
					.ViewInputMax(this, &SDataflowSimulationViewport::GetViewMaxInput)
				]
			]
		];

		ViewportOverlay->AddSlot()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.FillWidth(1)
			.Padding(10.0f, 40.0f)
			[
				// Display text 
				SNew(SRichTextBlock)
					.DecoratorStyleSet(&FAppStyle::Get())
					.Text(this, &SDataflowSimulationViewport::GetDisplayString)
					.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("AnimViewport.MessageText"))
			]
		];

	}
}

FText SDataflowSimulationViewport::GetDisplayString() const
{
	using namespace UE::Dataflow;
	
	auto ConcatenateLine = [](const FText& InText, const FText& InNewLine)->FText
	{
		if (InText.IsEmpty())
		{
			return InNewLine;
		}
		return FText::Format(LOCTEXT("ViewportTextNewlineFormatter", "{0}\n{1}"), InText, InNewLine);
	};

	FText DisplayText;

	const TMap<FName, TUniquePtr<IDataflowSimulationVisualization>>& Visualizations = FDataflowSimulationVisualizationRegistry::GetInstance().GetVisualizations();
	for (const TPair<FName, TUniquePtr<IDataflowSimulationVisualization>>& Visualization : Visualizations)
	{
		FText Text = Visualization.Value->GetDisplayString(GetSimulationScene().Get());
		DisplayText = ConcatenateLine(DisplayText, Text);
	}
	
	return DisplayText;
}

TSharedPtr<SWidget> SDataflowSimulationViewport::MakeViewportToolbar()
{
	return SNew(SDataflowSimulationViewportToolBar, SharedThis(this)).CommandList(CommandList);
}

void SDataflowSimulationViewport::OnFocusViewportToSelection()
{
	if(const FDataflowPreviewSceneBase* PreviewScene = static_cast<FDataflowPreviewSceneBase*>(Client->GetPreviewScene()))
	{
		const FBox SceneBoundingBox = PreviewScene->GetBoundingBox();
		Client->FocusViewportOnBox(SceneBoundingBox);
	}
}

UDataflowEditorMode* SDataflowSimulationViewport::GetEdMode() const
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

void SDataflowSimulationViewport::BindCommands()
{
	SAssetEditorViewport::BindCommands();
	
	const FDataflowEditorCommandsImpl& CommandInfos = FDataflowEditorCommands::Get();

	CommandList->MapAction(
		CommandInfos.RebuildSimulationScene,
		FExecuteAction::CreateLambda([this]()
		{
			if(FDataflowSimulationScene* SimulationScene = StaticCast<FDataflowSimulationScene*>(Client->GetPreviewScene()))
			{
				SimulationScene->RebuildSimulationScene(false);
			}
		}),
		FCanExecuteAction::CreateLambda([this]() { return true; }),
		FIsActionChecked::CreateLambda([this]() { return false; }));

	CommandList->MapAction(
		CommandInfos.PauseSimulationScene,
		FExecuteAction::CreateLambda([this]()
		{
			if(FDataflowSimulationScene* SimulationScene = StaticCast<FDataflowSimulationScene*>(Client->GetPreviewScene()))
			{
				SimulationScene->PauseSimulationScene();
			}
		}),
		FCanExecuteAction::CreateLambda([this]() { return true; }),
		FIsActionChecked::CreateLambda([this]() { return false; }));

	CommandList->MapAction(
		CommandInfos.StartSimulationScene,
		FExecuteAction::CreateLambda([this]()
		{
			if(FDataflowSimulationScene* SimulationScene = StaticCast<FDataflowSimulationScene*>(Client->GetPreviewScene()))
			{
				SimulationScene->StartSimulationScene();
			}
		}),
		FCanExecuteAction::CreateLambda([this]() { return true; }),
		FIsActionChecked::CreateLambda([this]() { return false; }));

	CommandList->MapAction(
		CommandInfos.StepSimulationScene,
		FExecuteAction::CreateLambda([this]()
		{
			if(FDataflowSimulationScene* SimulationScene = StaticCast<FDataflowSimulationScene*>(Client->GetPreviewScene()))
			{
				SimulationScene->StepSimulationScene();
			}
		}),
		FCanExecuteAction::CreateLambda([this]() { return true; }),
		FIsActionChecked::CreateLambda([this]() { return false; }));
}

bool SDataflowSimulationViewport::IsVisible() const
{
	// Intentionally not calling SEditorViewport::IsVisible because it will return false if our simulation is more than 250ms.
	return ViewportWidget.IsValid();
}

TSharedRef<class SEditorViewport> SDataflowSimulationViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SDataflowSimulationViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}

void SDataflowSimulationViewport::OnFloatingButtonClicked()
{
}

float SDataflowSimulationViewport::GetViewMinInput() const
{
	return 0.0f;
}

float SDataflowSimulationViewport::GetViewMaxInput() const
{
	return GetSimulationScene()->GetTimeRange()[1]-GetSimulationScene()->GetTimeRange()[0];
}


#undef LOCTEXT_NAMESPACE
