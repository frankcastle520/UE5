// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowConstructionViewportToolbar.h"

#include "Dataflow/DataflowConstructionViewport.h"
#include "Dataflow/DataflowConstructionViewportClient.h"
#include "Dataflow/DataflowConstructionVisualization.h"
#include "Dataflow/DataflowEditorCommands.h"
#include "Dataflow/DataflowEditorMode.h"
#include "Dataflow/DataflowRenderingViewMode.h"
#include "EditorModeManager.h"
#include "SEditorViewportViewMenu.h"
#include "ToolMenus.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "SDataflowConstructionViewportSelectionToolBar"

void SDataflowConstructionViewportSelectionToolBar::Construct(const FArguments& InArgs, TSharedPtr<SDataflowConstructionViewport> InDataflowViewport)
{
	EditorViewport = InDataflowViewport;
	SCommonEditorViewportToolbarBase::Construct(SCommonEditorViewportToolbarBase::FArguments(), InDataflowViewport);

	CommandList = InArgs._CommandList;

	UToolMenu* const ViewModeMenu = UToolMenus::Get()->RegisterMenu("DataflowEditor.ConstructionViewModeMenu");

	// Dynamically populate the view mode menu based on which ViewModes are available for the current node selection
	ViewModeMenu->AddDynamicSection("DataflowConstructionViewModeMenuSection",
		FNewToolMenuDelegate::CreateLambda([this](UToolMenu* ViewModeMenu)
		{	
			const TSharedPtr<const SDataflowConstructionViewport> PinnedViewport = EditorViewport.Pin();
			if (PinnedViewport.IsValid())
			{
				FToolMenuSection& ViewModesSection = ViewModeMenu->AddSection(NAME_None, FText());

				const TSharedPtr<const FEditorViewportClient> ViewportClient = PinnedViewport->GetViewportClient();
				check(ViewportClient.IsValid());

				const FEditorModeTools* const EditorModeTools = ViewportClient->GetModeTools();
				check(EditorModeTools);

				if (const UDataflowEditorMode* const DataflowEdMode = Cast<UDataflowEditorMode>(EditorModeTools->GetActiveScriptableMode(UDataflowEditorMode::EM_DataflowEditorModeId)))
				{
					for (const TPair<FName, TSharedPtr<FUICommandInfo>>& NameAndCommand : FDataflowEditorCommandsImpl::Get().SetConstructionViewModeCommands)
					{
						const FName ViewModeName = NameAndCommand.Key;
						if (DataflowEdMode->CanChangeConstructionViewModeTo(ViewModeName))
						{
							ViewModesSection.AddMenuEntry(NameAndCommand.Value);
						}
					}
				}
			}
		}));

	const FMargin ToolbarSlotPadding(4.0f, 1.0f);
	TSharedPtr<SHorizontalBox> MainBoxPtr;

	ChildSlot
	[
		SNew( SBorder )
		.BorderImage(FAppStyle::Get().GetBrush("EditorViewportToolBar.Background"))
		.Cursor(EMouseCursor::Default)
		[
			SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
			[
				SAssignNew( MainBoxPtr, SHorizontalBox )
			]
		]
	];

	// Display (Lit, Unlit, Wireframe, etc.)
	MainBoxPtr->AddSlot()
		.AutoWidth()
		.Padding(ToolbarSlotPadding)
		[
			MakeDisplayToolBar(InArgs._Extenders)
		];

	// Show menu
	MainBoxPtr->AddSlot()
		.AutoWidth()
		.Padding(ToolbarSlotPadding)
		[
			SNew(SEditorViewportToolbarMenu)
				.Label(LOCTEXT("ShowMenuTitle", "Show"))
				.Cursor(EMouseCursor::Default)
				.ParentToolBar(SharedThis(this))
				.OnGetMenuContent(this, &SDataflowConstructionViewportSelectionToolBar::GenerateShowMenu)
		];

	// View mode (Sim2D/Sim3D/Render)
	MainBoxPtr->AddSlot()
		.Padding(ToolbarSlotPadding)
		.HAlign(HAlign_Right)
		[
			MakeToolBar(InArgs._Extenders)
		];

	// See SCommonEditorViewportToolbarBase::Construct for more possible menus to add
}

TSharedRef<SWidget> SDataflowConstructionViewportSelectionToolBar::GenerateShowMenu() const
{
	FMenuBuilder MenuBuilder(false, CommandList);

	using namespace UE::Dataflow;

	if (const TSharedPtr<SDataflowConstructionViewport> PinnedEditorViewport = EditorViewport.Pin())
	{
		if (const TSharedPtr<FEditorViewportClient> ViewportClient = PinnedEditorViewport->GetViewportClient())
		{
			for (const TPair<FName, TUniquePtr<IDataflowConstructionVisualization>>& Visualization : FDataflowConstructionVisualizationRegistry::GetInstance().GetVisualizations())
			{
				Visualization.Value->ExtendViewportShowMenu(StaticCastSharedPtr<FDataflowConstructionViewportClient>(ViewportClient), MenuBuilder);
			}
		}
	}

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SDataflowConstructionViewportSelectionToolBar::MakeToolBar(const TSharedPtr<FExtender> InExtenders)
{
	// The following is modeled after portions of STransformViewportToolBar, which gets 
	// used in SCommonEditorViewportToolbarBase.
	// The buttons are hooked up to actual functions via command bindings in SChaosClothAssetEditorRestSpaceViewport::BindCommands(),
	// and the toolbar gets built in SDataflowConstructionViewport::MakeViewportToolbar().

	FSlimHorizontalToolBarBuilder ToolbarBuilder(CommandList, FMultiBoxCustomization::None, InExtenders);

	// Use a custom style
	const FName ToolBarStyle = "EditorViewportToolBar";
	ToolbarBuilder.SetStyle(&FAppStyle::Get(), ToolBarStyle);
	ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);

	ToolbarBuilder.BeginBlockGroup();
	{
		// View mode selector (2D/3D/Render)
		ToolbarBuilder.AddWidget(
			SAssignNew(ViewModeDropDown, SEditorViewportToolbarMenu)
			.ParentToolBar(SharedThis(this))
			.Cursor(EMouseCursor::Default)
			.Label(this, &SDataflowConstructionViewportSelectionToolBar::GetViewModeMenuLabel)
			.LabelIcon(this, &SDataflowConstructionViewportSelectionToolBar::GetViewModeMenuLabelIcon)
			.OnGetMenuContent(this, &SDataflowConstructionViewportSelectionToolBar::GenerateViewModeMenuContent)
		);
	}
	ToolbarBuilder.EndBlockGroup();
	ToolbarBuilder.EndSection(); // View Controls

	return ToolbarBuilder.MakeWidget();
}

FText SDataflowConstructionViewportSelectionToolBar::GetViewModeMenuLabel() const
{
	FText Label = LOCTEXT("DataflowConstructionViewMenuTitle_Default", "View");

	if (const TSharedPtr<SDataflowConstructionViewport> PinnedViewport = EditorViewport.Pin())
	{
		const TSharedPtr<FEditorViewportClient> ViewportClient = PinnedViewport->GetViewportClient();
		check(ViewportClient.IsValid());
		const FEditorModeTools* const EditorModeTools = ViewportClient->GetModeTools();
		const UDataflowEditorMode* const DataflowEdMode = Cast<UDataflowEditorMode>(EditorModeTools->GetActiveScriptableMode(UDataflowEditorMode::EM_DataflowEditorModeId));
		if (DataflowEdMode)
		{
			Label = DataflowEdMode->GetConstructionViewMode()->GetButtonText();
		}
	}

	return Label;
}

const FSlateBrush* SDataflowConstructionViewportSelectionToolBar::GetViewModeMenuLabelIcon() const
{
	return FStyleDefaults::GetNoBrush();
}

TSharedRef<SWidget> SDataflowConstructionViewportSelectionToolBar::GenerateViewModeMenuContent() const
{
	return UToolMenus::Get()->GenerateWidget("DataflowEditor.ConstructionViewModeMenu", FToolMenuContext(CommandList));
}

TSharedRef<SWidget> SDataflowConstructionViewportSelectionToolBar::MakeDisplayToolBar(const TSharedPtr<FExtender> InExtenders)
{
	const TSharedRef<SEditorViewport> ViewportRef = StaticCastSharedPtr<SEditorViewport>(EditorViewport.Pin()).ToSharedRef();

	return SNew(SEditorViewportViewMenu, ViewportRef, SharedThis(this))
		.Cursor(EMouseCursor::Default)
		.MenuExtenders(InExtenders);
}

#undef LOCTEXT_NAMESPACE
