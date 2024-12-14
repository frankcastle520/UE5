// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUVEditor2DViewportToolBar.h"

#include "EditorViewportCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "UVEditorCommands.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "UVEditorUXSettings.h"
#include "UVEditor2DViewportClient.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "SViewportToolBarComboMenu.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"

#define LOCTEXT_NAMESPACE "UVEditor2DViewportToolbar"

void SUVEditor2DViewportToolBar::Construct(const FArguments& InArgs)
{
	CommandList = InArgs._CommandList;
	Viewport2DClient = InArgs._Viewport2DClient;

	const FMargin ToolbarSlotPadding(4.0f, 1.0f);

	ChildSlot
	[
		SNew( SBorder )
		.BorderImage(FAppStyle::Get().GetBrush("EditorViewportToolBar.Background"))
		.Cursor(EMouseCursor::Default)
		[
			SNew( SHorizontalBox )

			// The first slot is just a spacer so that we get three evenly spaced columns 
			// and the selection toolbar can go in the center of the center one.
			+ SHorizontalBox::Slot()
			.Padding(ToolbarSlotPadding)
			.HAlign(HAlign_Right)

			+ SHorizontalBox::Slot()
			.Padding(ToolbarSlotPadding)
			.HAlign(HAlign_Center)
			[
				MakeSelectionToolBar(InArgs._Extenders)
			]

	        + SHorizontalBox::Slot()
			.Padding(ToolbarSlotPadding)
			.HAlign(HAlign_Right)
			[
				MakeTransformToolBar(InArgs._Extenders)
			]

			+ SHorizontalBox::Slot()
			.Padding(ToolbarSlotPadding)
			.HAlign(HAlign_Right)
			[
				MakeGizmoToolBar(InArgs._Extenders)
			]
		]
	];

	SViewportToolBar::Construct(SViewportToolBar::FArguments());
}

TSharedRef<SWidget> SUVEditor2DViewportToolBar::MakeSelectionToolBar(const TSharedPtr<FExtender> InExtenders)
{

	FSlimHorizontalToolBarBuilder ToolbarBuilder(CommandList, FMultiBoxCustomization::None, InExtenders);

	// Use a custom style
	FName ToolBarStyle = "EditorViewportToolBar";
	ToolbarBuilder.SetStyle(&FAppStyle::Get(), ToolBarStyle);
	ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);

	// Transform controls should not be focusable as it fights with the press space to change transform 
	// mode feature, which we may someday have.
	ToolbarBuilder.SetIsFocusable(false);

	// Widget controls
	ToolbarBuilder.BeginSection("SelectionModes");
	{
		ToolbarBuilder.BeginBlockGroup();

		ToolbarBuilder.AddToolBarButton(FUVEditorCommands::Get().VertexSelection, NAME_None,
			TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), TEXT("VertexSelection"));

		ToolbarBuilder.AddToolBarButton(FUVEditorCommands::Get().EdgeSelection, NAME_None,
			TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), TEXT("EdgeSelection"));

		ToolbarBuilder.AddToolBarButton(FUVEditorCommands::Get().TriangleSelection, NAME_None,
			TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), TEXT("TriangleSelection"));

		ToolbarBuilder.AddToolBarButton(FUVEditorCommands::Get().IslandSelection, NAME_None,
			TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), TEXT("IslandSelection"));

		ToolbarBuilder.AddToolBarButton(FUVEditorCommands::Get().FullMeshSelection, NAME_None,
			TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), TEXT("FullMeshSelection"));

		ToolbarBuilder.EndBlockGroup();
	}

	ToolbarBuilder.EndSection();

	return ToolbarBuilder.MakeWidget();
}

TSharedRef<SWidget> SUVEditor2DViewportToolBar::MakeGizmoToolBar(const TSharedPtr<FExtender> InExtenders)
{
	// The following is modeled after portions of STransformViewportToolBar, which gets 
	// used in SCommonEditorViewportToolbarBase.

	// The buttons are hooked up to actual functions via command bindings in SEditorViewport::BindCommands(),
	// and the toolbar gets built in SUVEditor2DViewport::MakeViewportToolbar().

	FSlimHorizontalToolBarBuilder ToolbarBuilder(CommandList, FMultiBoxCustomization::None, InExtenders);

	// Use a custom style
	FName ToolBarStyle = "EditorViewportToolBar";
	ToolbarBuilder.SetStyle(&FAppStyle::Get(), ToolBarStyle);
	ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);

	// Transform controls should not be focusable as it fights with the press space to change transform 
	// mode feature, which we may someday have.
	ToolbarBuilder.SetIsFocusable(false);

	// Widget controls
	ToolbarBuilder.BeginSection("Transform");
	{
		ToolbarBuilder.BeginBlockGroup();

		// Select Mode
		static FName SelectModeName = FName(TEXT("SelectMode"));
		ToolbarBuilder.AddToolBarButton(FEditorViewportCommands::Get().SelectMode, NAME_None, TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), SelectModeName);

		// Translate Mode
		static FName TranslateModeName = FName(TEXT("TranslateMode"));
		ToolbarBuilder.AddToolBarButton(FEditorViewportCommands::Get().TranslateMode, NAME_None, TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), TranslateModeName);

		ToolbarBuilder.EndBlockGroup();
	}

	ToolbarBuilder.EndSection();

	return ToolbarBuilder.MakeWidget();
}

// This is mostly copied from the version found in STransformViewportToolbar.cpp, which provides the
// transform/snapping controls for the level editor main window. We only want a subset of that 
// functionality though and moreover we want to store the state in the UVEditor instead of the editor
// global settings. So we make a few tweaks here from the original to control the exact snapping options
// available and the activation methods.
TSharedRef<SWidget> SUVEditor2DViewportToolBar::MakeTransformToolBar(const TSharedPtr< FExtender > InExtenders)
{
	FSlimHorizontalToolBarBuilder ToolbarBuilder(CommandList, FMultiBoxCustomization::None, InExtenders);

	// Use a custom style
	FName ToolBarStyle = "EditorViewportToolBar";
	ToolbarBuilder.SetStyle(&FAppStyle::Get(), ToolBarStyle);
	ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);

	// Transform controls cannot be focusable as it fights with the press space to change transform mode feature
	ToolbarBuilder.SetIsFocusable(false);

	ToolbarBuilder.BeginSection("LocationGridSnap");
	{
		// Grab the existing UICommand 
		// TODO: Should we replace this with our own version at some point? 
		// Same for the other commands below for rotation and scaling.
		TSharedPtr<FUICommandInfo> Command = FEditorViewportCommands::Get().LocationGridSnap;

		static FName PositionSnapName = FName(TEXT("PositionSnap"));

		// Setup a GridSnapSetting with the UICommand
		ToolbarBuilder.AddWidget(
			SNew(SViewportToolBarComboMenu)
			.IsChecked(this, &SUVEditor2DViewportToolBar::IsLocationGridSnapChecked)
			.OnCheckStateChanged(this, &SUVEditor2DViewportToolBar::HandleToggleLocationGridSnap)
			.Label(this, &SUVEditor2DViewportToolBar::GetLocationGridLabel)
			.OnGetMenuContent(this, &SUVEditor2DViewportToolBar::FillLocationGridSnapMenu)
			.ToggleButtonToolTip(Command->GetDescription())
			.MenuButtonToolTip(LOCTEXT("LocationGridSnap_ToolTip", "Set the Translation Snap value"))
			.Icon(Command->GetIcon())
			.MinDesiredButtonWidth(24.0f)
			.ParentToolBar(SharedThis(this)),
			PositionSnapName,
			false,
			HAlign_Fill,

			// explicitly specify what this widget should look like as a menu item
			FNewMenuDelegate::CreateLambda([this, Command](FMenuBuilder& InMenuBuilder)
				{
					// TODO - debug why can't just use the Command / mapping isn't working 
					InMenuBuilder.AddMenuEntry(Command);

					InMenuBuilder.AddWrapperSubMenu(
						LOCTEXT("GridSnapMenuSettings", "Translation Snap Settings"),
						LOCTEXT("GridSnapMenuSettings_ToolTip", "Set the Translation Snap value"),
						FOnGetContent::CreateSP(this, &SUVEditor2DViewportToolBar::FillLocationGridSnapMenu),
						FSlateIcon(Command->GetIcon())
					);
				}
		));
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("RotationGridSnap");
	{
		// Grab the existing UICommand 
		TSharedPtr<FUICommandInfo> Command = FEditorViewportCommands::Get().RotationGridSnap;

		static FName RotationSnapName = FName(TEXT("RotationSnap"));

		// Setup a GridSnapSetting with the UICommand
		ToolbarBuilder.AddWidget(
			SNew(SViewportToolBarComboMenu)
			.IsChecked(this, &SUVEditor2DViewportToolBar::IsRotationGridSnapChecked)
			.OnCheckStateChanged(this, &SUVEditor2DViewportToolBar::HandleToggleRotationGridSnap)
			.Label(this, &SUVEditor2DViewportToolBar::GetRotationGridLabel)
			.OnGetMenuContent(this, &SUVEditor2DViewportToolBar::FillRotationGridSnapMenu)
			.ToggleButtonToolTip(Command->GetDescription())
			.MenuButtonToolTip(LOCTEXT("RotationGridSnap_ToolTip", "Set the Rotation Snap value"))
			.Icon(Command->GetIcon())
			.ParentToolBar(SharedThis(this)),
			RotationSnapName,
			false,
			HAlign_Fill,

			// explicitly specify what this widget should look like as a menu item
			FNewMenuDelegate::CreateLambda([this, Command](FMenuBuilder& InMenuBuilder)
				{
					InMenuBuilder.AddMenuEntry(Command);

					InMenuBuilder.AddWrapperSubMenu(
						LOCTEXT("RotationGridSnapMenuSettings", "Rotation Snap Settings"),
						LOCTEXT("RotationGridSnapMenuSettings_ToolTip", "Set the Rotation Snap value"),
						FOnGetContent::CreateSP(this, &SUVEditor2DViewportToolBar::FillRotationGridSnapMenu),
						FSlateIcon(Command->GetIcon())
					);
				}
		));
	}
	ToolbarBuilder.EndSection();


	ToolbarBuilder.BeginSection("ScaleGridSnap");
	{
		// Grab the existing UICommand 
		TSharedPtr<FUICommandInfo> Command = FEditorViewportCommands::Get().ScaleGridSnap;

		static FName ScaleSnapName = FName(TEXT("ScaleSnap"));

		// Setup a GridSnapSetting with the UICommand
		ToolbarBuilder.AddWidget(
			SNew(SViewportToolBarComboMenu)
			.Cursor(EMouseCursor::Default)
			.IsChecked(this, &SUVEditor2DViewportToolBar::IsScaleGridSnapChecked)
			.OnCheckStateChanged(this, &SUVEditor2DViewportToolBar::HandleToggleScaleGridSnap)
			.Label(this, &SUVEditor2DViewportToolBar::GetScaleGridLabel)
			.OnGetMenuContent(this, &SUVEditor2DViewportToolBar::FillScaleGridSnapMenu)
			.ToggleButtonToolTip(Command->GetDescription())
			.MenuButtonToolTip(LOCTEXT("ScaleGridSnap_ToolTip", "Set the Scaling Snap value"))
			.Icon(Command->GetIcon())
			.MinDesiredButtonWidth(24.0f)
			.ParentToolBar(SharedThis(this)),
			ScaleSnapName,
			false,
			HAlign_Fill,

			// explicitly specify what this widget should look like as a menu item
			FNewMenuDelegate::CreateLambda([this, Command](FMenuBuilder& InMenuBuilder)
				{
					InMenuBuilder.AddMenuEntry(Command);

					InMenuBuilder.AddWrapperSubMenu(
						LOCTEXT("ScaleGridSnapMenuSettings", "Scale Snap Settings"),
						LOCTEXT("ScaleGridSnapMenuSettings_ToolTip", "Set the Scale Snap value"),
						FOnGetContent::CreateSP(this, &SUVEditor2DViewportToolBar::FillScaleGridSnapMenu),
						FSlateIcon(Command->GetIcon())
					);
				}
		));
	}
	ToolbarBuilder.EndSection();

	TSharedRef<SWidget> TransformBar = ToolbarBuilder.MakeWidget();
	TransformBar->SetEnabled(TAttribute<bool>::CreateLambda([this]() {return Viewport2DClient->AreWidgetButtonsEnabled(); }));
	return TransformBar;
}

// The following methods again mimic the patterns found in the STransformViewportToolbar.cpp,
// to serve as a drop in replacement for the menu infrastructure above. These have been altered
// to adjust the menu options and change how the snap settings are stored/forwarded to the UVEditor.

FText SUVEditor2DViewportToolBar::GetLocationGridLabel() const
{
	return FText::AsNumber(Viewport2DClient->GetLocationGridSnapValue());
}

FText SUVEditor2DViewportToolBar::GetRotationGridLabel() const
{
	return FText::Format(LOCTEXT("GridRotation - Number - DegreeSymbol", "{0}\u00b0"), FText::AsNumber(Viewport2DClient->GetRotationGridSnapValue()));
}

FText SUVEditor2DViewportToolBar::GetScaleGridLabel() const
{
	FNumberFormattingOptions NumberFormattingOptions;
	NumberFormattingOptions.MaximumFractionalDigits = 5;

	const float CurGridAmount = Viewport2DClient->GetScaleGridSnapValue();
	return FText::AsNumber(CurGridAmount, &NumberFormattingOptions);
}

TSharedRef<SWidget> SUVEditor2DViewportToolBar::FillLocationGridSnapMenu()
{
	TArray<float> GridSizes;
	GridSizes.Reserve(FUVEditorUXSettings::MaxLocationSnapValue());
	for (int32 Index = 0; Index < FUVEditorUXSettings::MaxLocationSnapValue(); ++Index)
	{
		GridSizes.Add(FUVEditorUXSettings::LocationSnapValue(Index));
	}

	using namespace UE::UnrealEd;

	FLocationGridCheckboxListExecuteActionDelegate ExecuteDelegate =
		FLocationGridCheckboxListExecuteActionDelegate::CreateLambda(
			[this, GridSizes](int CurrGridSizeIndex)
			{
				const float CurrGridSize = GridSizes[CurrGridSizeIndex];
				Viewport2DClient->SetLocationGridSnapValue(CurrGridSize);
			}
		);

	FLocationGridCheckboxListIsCheckedDelegate IsCheckedDelegate = FLocationGridCheckboxListIsCheckedDelegate::CreateLambda(
		[this, GridSizes](int CurrGridSizeIndex)
		{
			const float CurrGridSize = GridSizes[CurrGridSizeIndex];
			return FMath::IsNearlyEqual(Viewport2DClient->GetLocationGridSnapValue(), CurrGridSize);
		}
	);

	return CreateLocationGridSnapMenu(ExecuteDelegate, IsCheckedDelegate, GridSizes, CommandList);
}

TSharedRef<SWidget> SUVEditor2DViewportToolBar::FillRotationGridSnapMenu()
{
	const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();

	TArray<float> GridSizes = ViewportSettings->CommonRotGridSizes;

	using namespace UE::UnrealEd;
	FRotationGridCheckboxListExecuteActionDelegate ExecuteDelegate =
		FRotationGridCheckboxListExecuteActionDelegate::CreateLambda(
			[this, GridSizes](int InCurrGridAngleIndex, ERotationGridMode InGridMode)
			{
				const float CurrGridAngle = GridSizes[InCurrGridAngleIndex];
				Viewport2DClient->SetRotationGridSnapValue(CurrGridAngle);
			}
		);

	FRotationGridCheckboxListIsCheckedDelegate IsCheckedDelegate = FRotationGridCheckboxListIsCheckedDelegate::CreateLambda(
		[this, GridSizes](int InCurrGridAngleIndex, ERotationGridMode InGridMode)
		{
			const float CurrGridAngle = GridSizes[InCurrGridAngleIndex];
			return FMath::IsNearlyEqual(Viewport2DClient->GetRotationGridSnapValue(), CurrGridAngle);
		}
	);

	return CreateRotationGridSnapMenu(ExecuteDelegate, IsCheckedDelegate, CommandList);
}

TSharedRef<SWidget> SUVEditor2DViewportToolBar::FillScaleGridSnapMenu()
{
	const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();
	TArray<float> GridSizes = ViewportSettings->ScalingGridSizes;

	UE::UnrealEd::FScaleGridCheckboxListExecuteActionDelegate ExecuteDelegate =
		UE::UnrealEd::FScaleGridCheckboxListExecuteActionDelegate::CreateLambda(
			[this, GridSizes](int CurrGridScaleIndex)
			{
				const float CurGridAmount = GridSizes[CurrGridScaleIndex];
				Viewport2DClient->SetScaleGridSnapValue(CurGridAmount);
		}
		);

	UE::UnrealEd::FScaleGridCheckboxListIsCheckedDelegate IsCheckedDelegate =
		UE::UnrealEd::FScaleGridCheckboxListIsCheckedDelegate::CreateLambda(
			[this, GridSizes](int CurrGridScaleIndex)
		{
				const float CurrGridAmount = GridSizes[CurrGridScaleIndex];
				return FMath::IsNearlyEqual(Viewport2DClient->GetScaleGridSnapValue(), CurrGridAmount);
		}
		);

	return UE::UnrealEd::CreateScaleGridSnapMenu(ExecuteDelegate, IsCheckedDelegate, GridSizes, CommandList);
}

ECheckBoxState SUVEditor2DViewportToolBar::IsLocationGridSnapChecked() const
{
	return Viewport2DClient->GetLocationGridSnapEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState SUVEditor2DViewportToolBar::IsRotationGridSnapChecked() const
{
	return Viewport2DClient->GetRotationGridSnapEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState SUVEditor2DViewportToolBar::IsScaleGridSnapChecked() const
{
	return Viewport2DClient->GetScaleGridSnapEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SUVEditor2DViewportToolBar::HandleToggleLocationGridSnap(ECheckBoxState InState)
{
	Viewport2DClient->SetLocationGridSnapEnabled(InState == ECheckBoxState::Checked);
}

void SUVEditor2DViewportToolBar::HandleToggleRotationGridSnap(ECheckBoxState InState)
{
	Viewport2DClient->SetRotationGridSnapEnabled(InState == ECheckBoxState::Checked);
}

void SUVEditor2DViewportToolBar::HandleToggleScaleGridSnap(ECheckBoxState InState)
{
	Viewport2DClient->SetScaleGridSnapEnabled(InState == ECheckBoxState::Checked);
}

#undef LOCTEXT_NAMESPACE
