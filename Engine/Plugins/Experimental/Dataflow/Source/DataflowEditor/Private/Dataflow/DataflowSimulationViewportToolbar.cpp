// Copyright Epic Games, Inc. All Rights Reserved.
#include "Dataflow/DataflowSimulationViewportToolbar.h"
#include "Dataflow/DataflowSimulationViewport.h"
#include "Dataflow/DataflowEditorCommands.h"
#include "Dataflow/DataflowSimulationScene.h"
#include "Styling/AppStyle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SEditorViewportToolBarMenu.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "DataflowSimulationViewportToolBar"

void SDataflowSimulationViewportToolBar::Construct(const FArguments& InArgs, TSharedPtr<SDataflowSimulationViewport> InDataflowViewport)
{
	EditorViewport = InDataflowViewport;
	CommandList = InArgs._CommandList;
	Extenders = InArgs._Extenders;
	
	SCommonEditorViewportToolbarBase::Construct(SCommonEditorViewportToolbarBase::FArguments(), InDataflowViewport);
}

void SDataflowSimulationViewportToolBar::ExtendLeftAlignedToolbarSlots(TSharedPtr<SHorizontalBox> MainBoxPtr, TSharedPtr<SViewportToolBar> ParentToolBarPtr) const
{
	const TSharedPtr<class FDataflowSimulationScene>& SimulationScene = EditorViewport.Pin()->GetSimulationScene();

	auto HasCacheAsset = [SimulationScene]()
	{
		if(SimulationScene && SimulationScene->GetPreviewSceneDescription())
		{
			return (SimulationScene->GetPreviewSceneDescription()->CacheAsset == nullptr) ? EVisibility::Visible : EVisibility::Collapsed;
		}
		return EVisibility::Collapsed;
	};
	
	const FMargin ToolbarSlotPadding(2.0f, 2.0f);
	MainBoxPtr->AddSlot()
		.Padding(ToolbarSlotPadding)
		[
			SNew(SBox)
			.Visibility(TAttribute<EVisibility>::Create(HasCacheAsset))
			[
				MakeToolBar(Extenders)
			]
		];
}

TSharedRef<SWidget> SDataflowSimulationViewportToolBar::MakeToolBar(const TSharedPtr<FExtender> InExtenders) const
{
	FSlimHorizontalToolBarBuilder ToolbarBuilder(CommandList, FMultiBoxCustomization::None, InExtenders);
	
	const FName ToolBarStyle = "EditorViewportToolBar";
	ToolbarBuilder.SetStyle(&FAppStyle::Get(), ToolBarStyle);
	ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);

	ToolbarBuilder.BeginSection("Sim Controls");
	ToolbarBuilder.BeginBlockGroup();
	{
		ToolbarBuilder.AddToolBarButton(FDataflowEditorCommands::Get().RebuildSimulationScene,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Animation.Backward_End"),
			FName(*FDataflowEditorCommands::Get().RebuildSimulationSceneIdentifier));

		ToolbarBuilder.AddToolBarButton(FDataflowEditorCommands::Get().PauseSimulationScene,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Animation.Pause"),
			FName(*FDataflowEditorCommands::Get().PauseSimulationSceneIdentifier));

		ToolbarBuilder.AddToolBarButton(FDataflowEditorCommands::Get().StartSimulationScene,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Play"),
			FName(*FDataflowEditorCommands::Get().StartSimulationSceneIdentifier));

		ToolbarBuilder.AddToolBarButton(FDataflowEditorCommands::Get().StepSimulationScene,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Animation.Forward_Step"),
			FName(*FDataflowEditorCommands::Get().StepSimulationSceneIdentifier));
	}
	ToolbarBuilder.EndBlockGroup();
	ToolbarBuilder.EndSection();

	return ToolbarBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
