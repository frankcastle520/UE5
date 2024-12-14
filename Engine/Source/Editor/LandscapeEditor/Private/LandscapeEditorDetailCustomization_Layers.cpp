// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeEditorDetailCustomization_Layers.h"
#include "IDetailChildrenBuilder.h"
#include "Framework/Commands/UIAction.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Brushes/SlateColorBrush.h"
#include "Layout/WidgetPath.h"
#include "SlateOptMacros.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SErrorText.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "EditorClassUtils.h"
#include "LandscapeEditorDetailCustomization_LayersBrushStack.h" // FLandscapeBrushDragDropOp
#include "LandscapeEditorModule.h"
#include "LandscapeEditorObject.h"
#include "Landscape.h"
#include "Styling/AppStyle.h"
#include "DetailLayoutBuilder.h"
#include "IDetailPropertyRow.h"
#include "DetailCategoryBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/SlateIconFinder.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "Kismet2/SClassPickerDialog.h"

#include "SLandscapeEditor.h"
#include "Dialogs/DlgPickAssetPath.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "DesktopPlatformModule.h"
#include "AssetRegistry/AssetRegistryModule.h"

#include "LandscapeRender.h"
#include "Materials/MaterialExpressionLandscapeVisibilityMask.h"
#include "LandscapeEdit.h"
#include "IDetailGroup.h"
#include "Widgets/SBoxPanel.h"
#include "LandscapeEditorDetailCustomization_TargetLayers.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "LandscapeEditorCommands.h"

#define LOCTEXT_NAMESPACE "LandscapeEditor.Layers"

TSharedRef<IDetailCustomization> FLandscapeEditorDetailCustomization_Layers::MakeInstance()
{
	return MakeShareable(new FLandscapeEditorDetailCustomization_Layers);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FLandscapeEditorDetailCustomization_Layers::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& LayerCategory = DetailBuilder.EditCategory("Edit Layers");

	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode 
		&& LandscapeEdMode->GetLandscape() 
		&& (LandscapeEdMode->CurrentToolMode != nullptr)
		&& (FName(LandscapeEdMode->CurrentTool->GetToolName()) != TEXT("Mask")))
	{
		LayerCategory.AddCustomBuilder(MakeShareable(new FLandscapeEditorCustomNodeBuilder_Layers(DetailBuilder.GetThumbnailPool().ToSharedRef())));

		LayerCategory.AddCustomRow(FText())
			.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([]() { return ShouldShowLayersErrorMessageTip() ? EVisibility::Visible : EVisibility::Collapsed; })))
			[
				SNew(SMultiLineEditableTextBox)
				.IsReadOnly(true)
				.Font(DetailBuilder.GetDetailFontBold())
				.BackgroundColor(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateLambda([]() { return FAppStyle::GetColor("ErrorReporting.WarningBackgroundColor"); })))
				.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateStatic(&FLandscapeEditorDetailCustomization_Layers::GetLayersErrorMessageText)))
				.AutoWrapText(true)
			];
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

bool FLandscapeEditorDetailCustomization_Layers::ShouldShowLayersErrorMessageTip()
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode && LandscapeEdMode->DoesCurrentToolAffectEditLayers())
	{
		return !LandscapeEdMode->CanEditLayer();
	}
	return false;
}

FText FLandscapeEditorDetailCustomization_Layers::GetLayersErrorMessageText()
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	FText Reason;
	if (LandscapeEdMode && !LandscapeEdMode->CanEditLayer(&Reason))
	{
		return Reason;
	}
	return FText::GetEmpty();
}

//////////////////////////////////////////////////////////////////////////

FEdModeLandscape* FLandscapeEditorCustomNodeBuilder_Layers::GetEditorMode()
{
	return (FEdModeLandscape*)GLevelEditorModeTools().GetActiveMode(FBuiltinEditorModes::EM_Landscape);
}

FLandscapeEditorCustomNodeBuilder_Layers::FLandscapeEditorCustomNodeBuilder_Layers(TSharedRef<FAssetThumbnailPool> InThumbnailPool)
	: ThumbnailPool(InThumbnailPool)
	, CurrentSlider(INDEX_NONE)
{
}

FLandscapeEditorCustomNodeBuilder_Layers::~FLandscapeEditorCustomNodeBuilder_Layers()
{
}

void FLandscapeEditorCustomNodeBuilder_Layers::SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren)
{
}

void FLandscapeEditorCustomNodeBuilder_Layers::GenerateHeaderRowContent(FDetailWidgetRow& NodeRow)
{
	NodeRow.NameWidget
		[
			SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("LayersLabel", "Layers"))
		];


	TSharedRef<SWidget> AddButton = PropertyCustomizationHelpers::MakeAddButton(
		FSimpleDelegate::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::CreateLayer),
		MakeAttributeSPLambda(this, [this] { FText Reason; CanCreateLayer(Reason); return Reason; }),
		MakeAttributeSPLambda(this, [this] { FText Reason; return CanCreateLayer(Reason); }));

	NodeRow.ValueWidget
		[
			SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1) // Fill the entire width if possible
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
						.Text(this, &FLandscapeEditorCustomNodeBuilder_Layers::GetNumLayersText)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(2.0f, 0.0f)
				[
					AddButton
				]
		];
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FLandscapeEditorCustomNodeBuilder_Layers::GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode != NULL)
	{
		TSharedPtr<SDragAndDropVerticalBox> LayerList = SNew(SDragAndDropVerticalBox)
			.OnCanAcceptDrop(this, &FLandscapeEditorCustomNodeBuilder_Layers::HandleCanAcceptDrop)
			.OnAcceptDrop(this, &FLandscapeEditorCustomNodeBuilder_Layers::HandleAcceptDrop)
			.OnDragDetected(this, &FLandscapeEditorCustomNodeBuilder_Layers::HandleDragDetected);

		LayerList->SetDropIndicator_Above(*FAppStyle::GetBrush("LandscapeEditor.TargetList.DropZone.Above"));
		LayerList->SetDropIndicator_Below(*FAppStyle::GetBrush("LandscapeEditor.TargetList.DropZone.Below"));

		ChildrenBuilder.AddCustomRow(FText::FromString(FString(TEXT("Edit Layers"))))
			.Visibility(EVisibility::Visible)
			[
				LayerList.ToSharedRef()
			];

		InlineTextBlocks.Empty();
		InlineTextBlocks.AddDefaulted(LandscapeEdMode->GetLayerCount());
		// Slots are displayed in the opposite order of LandscapeEditLayers
		for (int32 i = LandscapeEdMode->GetLayerCount()-1; i >= 0 ; --i)
		{
			TSharedPtr<SWidget> GeneratedRowWidget = GenerateRow(i);

			if (GeneratedRowWidget.IsValid())
			{
				LayerList->AddSlot()
					.AutoHeight()
					[
						GeneratedRowWidget.ToSharedRef()
					];
			}
		}
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedPtr<SWidget> FLandscapeEditorCustomNodeBuilder_Layers::GenerateRow(int32 InLayerIndex)
{
	TSharedRef<SWidget> DeleteButton = PropertyCustomizationHelpers::MakeDeleteButton(
		FSimpleDelegate::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::DeleteLayer, InLayerIndex),
		MakeAttributeSPLambda(this, [this, InLayerIndex] { FText Reason; CanDeleteLayer(InLayerIndex, Reason); return Reason; }),
		MakeAttributeSPLambda(this, [this, InLayerIndex] { FText Reason; return CanDeleteLayer(InLayerIndex, Reason); }));

	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	TSharedPtr<SWidget> RowWidget = SNew(SLandscapeEditorSelectableBorder)
		.Padding(0.f)
		.VAlign(VAlign_Center)
		.OnContextMenuOpening(this, &FLandscapeEditorCustomNodeBuilder_Layers::OnLayerContextMenuOpening, InLayerIndex)
		.OnSelected(this, &FLandscapeEditorCustomNodeBuilder_Layers::OnLayerSelectionChanged, InLayerIndex)
		.IsSelected(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::IsLayerSelected, InLayerIndex)))
		.Visibility(EVisibility::Visible)
		[
			SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
						.Padding(FMargin(2.0f, 0.0f, 2.0f, 0.0f))
						[
							SNew(SImage)
								.Image(FCoreStyle::Get().GetBrush("VerticalBoxDragIndicatorShort"))
						]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
						.Padding(FMargin(2.0f, 0.0f, 2.0f, 0.0f))
						[
							SNew(SImage)
								.Image(this, &FLandscapeEditorCustomNodeBuilder_Layers::GetEditLayerIconBrush, InLayerIndex)
								// TODO [jonathan.bard] : investigate why this doesn't work : the tooltip just doesn't show up
								.ToolTip(this, &FLandscapeEditorCustomNodeBuilder_Layers::GetEditLayerTypeTooltip, InLayerIndex)
						]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "NoBorder")
						.OnClicked(this, &FLandscapeEditorCustomNodeBuilder_Layers::OnToggleLock, InLayerIndex)
						.ToolTipText(LOCTEXT("LandscapeLayerLock", "Locks the current layer"))
						[
							SNew(SImage)
								.Image(this, &FLandscapeEditorCustomNodeBuilder_Layers::GetLockBrushForLayer, InLayerIndex)
						]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
						.ContentPadding(0.0f)
						.ButtonStyle(FAppStyle::Get(), "NoBorder")
						.IsEnabled(MakeAttributeSPLambda(this, [this, InLayerIndex] { FText Reason; return CanToggleVisibility(InLayerIndex, Reason); }))
						.OnClicked(this, &FLandscapeEditorCustomNodeBuilder_Layers::OnToggleVisibility, InLayerIndex)
						.ToolTipText(MakeAttributeSPLambda(this, [this, InLayerIndex] { FText Reason; CanToggleVisibility(InLayerIndex, Reason); return Reason; }))
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Content()
						[
							SNew(SImage)
								.Image(this, &FLandscapeEditorCustomNodeBuilder_Layers::GetVisibilityBrushForLayer, InLayerIndex)
						]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0)
				.VAlign(VAlign_Center)
				.Padding(4, 0)
				[
					SNew(SHorizontalBox)
						.Clipping(EWidgetClipping::OnDemand)

						+ SHorizontalBox::Slot()
						.Padding(0)
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Left)
						[
							SAssignNew(InlineTextBlocks[InLayerIndex], SInlineEditableTextBlock)
								.IsEnabled(MakeAttributeSPLambda(this, [this, InLayerIndex] { FText Reason; return CanRenameLayer(InLayerIndex, Reason); }))
								.Text(this, &FLandscapeEditorCustomNodeBuilder_Layers::GetLayerDisplayName, InLayerIndex)
								.ColorAndOpacity(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::GetLayerTextColor, InLayerIndex)))
								.ToolTipText(MakeAttributeSPLambda(this, [this, InLayerIndex] { FText Reason; CanRenameLayer(InLayerIndex, Reason); return Reason; }))
								.OnVerifyTextChanged(FOnVerifyTextChanged::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::CanRenameLayerTo, InLayerIndex))
								.OnTextCommitted(FOnTextCommitted::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::SetLayerName, InLayerIndex))
						]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(0, 2)
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.Padding(0)
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Left)
						[
							SNew(STextBlock)
								.IsEnabled(MakeAttributeSPLambda(this, [this, InLayerIndex] { FText Reason; return CanSetLayerAlpha(InLayerIndex, Reason); }))
								.Visibility(this, &FLandscapeEditorCustomNodeBuilder_Layers::GetLayerAlphaVisibility, InLayerIndex)
								.Text(LOCTEXT("LandscapeLayerAlpha", "Alpha"))
								.ToolTipText(MakeAttributeSPLambda(this, [this, InLayerIndex] { FText Reason; CanSetLayerAlpha(InLayerIndex, Reason); return Reason; }))
								.ColorAndOpacity(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::GetLayerTextColor, InLayerIndex)))
						]
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(0, 2)
						.HAlign(HAlign_Left)
						.FillWidth(1.0f)
						[
							SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(-1.0f)
								.MaxValue(1.0f)
								.MinSliderValue(-1.0f)
								.MaxSliderValue(1.0f)
								.Delta(0.01f)
								.MinDesiredValueWidth(60.0f)
								.IsEnabled(MakeAttributeSPLambda(this, [this, InLayerIndex] { FText Reason; return CanSetLayerAlpha(InLayerIndex, Reason); }))
								.Visibility(this, &FLandscapeEditorCustomNodeBuilder_Layers::GetLayerAlphaVisibility, InLayerIndex)
								.ToolTipText(MakeAttributeSPLambda(this, [this, InLayerIndex] { FText Reason; CanSetLayerAlpha(InLayerIndex, Reason); return Reason; }))
								.Value(this, &FLandscapeEditorCustomNodeBuilder_Layers::GetLayerAlpha, InLayerIndex)
								.OnValueChanged_Lambda([this, InLayerIndex](float InValue) { SetLayerAlpha(InValue, InLayerIndex, false); })
								.OnValueCommitted_Lambda([this, InLayerIndex](float InValue, ETextCommit::Type InCommitType) { SetLayerAlpha(InValue, InLayerIndex, true); })
								.OnBeginSliderMovement_Lambda([this, InLayerIndex]()
							{
								CurrentSlider = InLayerIndex;
								GEditor->BeginTransaction(LOCTEXT("Landscape_Layers_SetAlpha", "Set Layer Alpha"));
							})
								.OnEndSliderMovement_Lambda([this](double)
							{
								GEditor->EndTransaction();
								CurrentSlider = INDEX_NONE;
							})
						]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(2.0f, 0.0f)
				[
					DeleteButton
				]
		];

	return RowWidget;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FText FLandscapeEditorCustomNodeBuilder_Layers::GetLayerDisplayName(int32 InLayerIndex) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (LandscapeEdMode && Landscape)
	{
		return FText::FromName(LandscapeEdMode->GetLayerName(InLayerIndex));
	}

	return FText::FromString(TEXT("None"));
}

bool FLandscapeEditorCustomNodeBuilder_Layers::IsLayerSelected(int32 InLayerIndex) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode)
	{
		return LandscapeEdMode->GetCurrentLayerIndex() == InLayerIndex;
	}

	return false;
}

bool FLandscapeEditorCustomNodeBuilder_Layers::CanRenameLayerTo(const FText& InNewText, FText& OutErrorMessage, int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode)
	{
		if (!LandscapeEdMode->CanRenameLayerTo(InLayerIndex, *InNewText.ToString()))
		{
			OutErrorMessage = LOCTEXT("Landscape_Layers_RenameFailed_AlreadyExists", "This edit layer name already exists");
			return false;
		}
	}
	return true;
}

void FLandscapeEditorCustomNodeBuilder_Layers::SetLayerName(const FText& InText, ETextCommit::Type InCommitType, int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode)
	{
		const FScopedTransaction Transaction(LOCTEXT("Landscape_Layers_Rename", "Rename Layer"));
		LandscapeEdMode->SetLayerName(InLayerIndex, *InText.ToString());
		OnLayerSelectionChanged(InLayerIndex);
	}
}

FSlateColor FLandscapeEditorCustomNodeBuilder_Layers::GetLayerTextColor(int32 InLayerIndex) const
{
	return IsLayerSelected(InLayerIndex) ? FStyleColors::ForegroundHover : FSlateColor::UseForeground();
}

void FLandscapeEditorCustomNodeBuilder_Layers::FillAddBrushMenu(FMenuBuilder& MenuBuilder, TArray<ALandscapeBlueprintBrushBase*> Brushes)
{
	for (ALandscapeBlueprintBrushBase* Brush : Brushes)
	{
		FUIAction AddAction = FUIAction(FExecuteAction::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::AddBrushToCurrentLayer, Brush));
		MenuBuilder.AddMenuEntry(FText::FromString(Brush->GetActorLabel()), FText(), FSlateIcon(), AddAction);
	}
}

void FLandscapeEditorCustomNodeBuilder_Layers::FillClearTargetLayerMenu(FMenuBuilder& MenuBuilder, int32 InLayerIndex, TArray<ULandscapeLayerInfoObject*> InUsedLayerInfos)
{
	// Clear All Weightmap Data
	FUIAction ClearAction = FUIAction(FExecuteAction::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::ClearTargetLayersOnLayer, InLayerIndex, ELandscapeClearMode::Clear_Weightmap));
	MenuBuilder.AddMenuEntry(LOCTEXT("LandscapeClearAllWeightmap", "All"), FText(), FSlateIcon(), ClearAction);
	MenuBuilder.AddMenuSeparator();

	// Clear Per LayerInfo
	for (ULandscapeLayerInfoObject* LayerInfo : InUsedLayerInfos)
	{
		FUIAction ClearLayerInfoAction = FUIAction(
			FExecuteAction::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::ClearTargetLayerOnLayer,InLayerIndex, LayerInfo),
			FCanExecuteAction::CreateSPLambda(this, [this, InLayerIndex, LayerInfo]() { FText Reason; return CanClearTargetLayerOnLayer(InLayerIndex, LayerInfo, Reason); }));
		TAttribute<FText> CanClearTargetLayerOnLayerTooltipText = MakeAttributeSPLambda(this, [this, InLayerIndex, LayerInfo] { FText Reason; CanClearTargetLayerOnLayer(InLayerIndex, LayerInfo, Reason); return Reason; });
		MenuBuilder.AddMenuEntry(FText::FromName(LayerInfo->LayerName), CanClearTargetLayerOnLayerTooltipText, FSlateIcon(), ClearLayerInfoAction);
	}
}

void FLandscapeEditorCustomNodeBuilder_Layers::FillClearLayerMenu(FMenuBuilder& MenuBuilder, int32 InLayerIndex)
{
	const UEnum* EnumPtr = FindObject<UEnum>(nullptr, TEXT("/Script/Landscape.ELandscapeClearMode"), true);
	if (ensure(EnumPtr != nullptr))
	{
		// NumEnums()-1 to exclude Enum Max Value
		for (int32 i = 0; i < EnumPtr->NumEnums()-1; ++i)
		{
			ELandscapeClearMode EnumValue = (ELandscapeClearMode)EnumPtr->GetValueByIndex(i);
			if(EnumValue == ELandscapeClearMode::Clear_Weightmap)
			{
				TArray<ULandscapeLayerInfoObject*> UsedLayerInfos;
				FEdModeLandscape* LandscapeEdMode = GetEditorMode();
				check(LandscapeEdMode);
				ALandscape* Landscape = LandscapeEdMode->GetLandscape();
				check(Landscape);

				Landscape->GetUsedPaintLayers(InLayerIndex, UsedLayerInfos);
				if (UsedLayerInfos.Num() > 0)
				{
					FUIAction ClearLayerAction = FUIAction(
						FExecuteAction(),
						FCanExecuteAction::CreateSPLambda(this, [this, InLayerIndex, EnumValue] { FText Reason; return CanClearTargetLayersOnLayer(InLayerIndex, EnumValue, Reason); }));
					TAttribute<FText> CanClearLayerTooltipText = MakeAttributeSPLambda(this, [this, InLayerIndex, EnumValue] { FText Reason; CanClearTargetLayersOnLayer(InLayerIndex, EnumValue, Reason); return Reason; });
					MenuBuilder.AddSubMenu(
						EnumPtr->GetDisplayNameTextByIndex(i),
						CanClearLayerTooltipText,
						FNewMenuDelegate::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::FillClearTargetLayerMenu, InLayerIndex, UsedLayerInfos),
						ClearLayerAction,
						/*InExtensionHook = */NAME_None,
						EUserInterfaceActionType::None);
				}
			}
			else
			{
				FUIAction ClearLayerAction = FUIAction(
					FExecuteAction::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::ClearTargetLayersOnLayer, InLayerIndex, EnumValue),
					FCanExecuteAction::CreateSPLambda(this, [this, InLayerIndex, EnumValue]() { FText Reason; return CanClearTargetLayersOnLayer(InLayerIndex, EnumValue, Reason); }));
				TAttribute<FText> CanClearLayerActionTooltipText = MakeAttributeSPLambda(this, [this, InLayerIndex, EnumValue] { FText Reason; CanClearTargetLayersOnLayer(InLayerIndex, EnumValue, Reason); return Reason; });
				MenuBuilder.AddMenuEntry(EnumPtr->GetDisplayNameTextByIndex(i), CanClearLayerActionTooltipText, FSlateIcon(), ClearLayerAction);
			}
		}
	}
}

void FLandscapeEditorCustomNodeBuilder_Layers::AddBrushToCurrentLayer(ALandscapeBlueprintBrushBase* Brush)
{
	const FScopedTransaction Transaction(LOCTEXT("LandscapeBrushAddToCurrentLayerTransaction", "Add brush to current layer"));
	GetEditorMode()->AddBrushToCurrentLayer(Brush);
}

TSharedPtr<SWidget> FLandscapeEditorCustomNodeBuilder_Layers::OnLayerContextMenuOpening(int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (Landscape && LandscapeEdMode && LandscapeEdMode->DoesCurrentToolAffectEditLayers())
	{
		const FLandscapeLayer* Layer = LandscapeEdMode->GetLayer(InLayerIndex);
		FMenuBuilder MenuBuilder(true, nullptr);
		MenuBuilder.BeginSection("LandscapeEditorLayerActions", LOCTEXT("LandscapeEditorLayerActions.Heading", "Edit Layers"));
		{
			if (Layer)
			{
				const ULandscapeEditLayerBase* EditLayer = Layer->EditLayer;
				check(EditLayer != nullptr);

				// Rename Layer
				FUIAction RenameLayerAction = FUIAction(
					FExecuteAction::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::RenameLayer, InLayerIndex),
					FCanExecuteAction::CreateSPLambda(this, [this, InLayerIndex] { FText Reason; return CanRenameLayer(InLayerIndex, Reason); }));
				TAttribute<FText> CanRenameLayerTooltipText = MakeAttributeSPLambda(this, [this, InLayerIndex] { FText Reason; CanRenameLayer(InLayerIndex, Reason); return Reason; });
				MenuBuilder.AddMenuEntry(LOCTEXT("RenameLayer", "Rename..."), CanRenameLayerTooltipText, FSlateIcon(), RenameLayerAction);

				// Clear Layer
				FUIAction ClearLayerAction = FUIAction(
					FExecuteAction(),
					FCanExecuteAction::CreateSPLambda(this, [this, InLayerIndex] { FText Reason; return CanClearLayer(InLayerIndex, Reason); }));
				TAttribute<FText> CanClearLayerTooltipText = MakeAttributeSPLambda(this, [this, InLayerIndex] { FText Reason; CanClearLayer(InLayerIndex, Reason); return Reason; });
				MenuBuilder.AddSubMenu(
					LOCTEXT("LandscapeEditorClearLayerSubMenu", "Clear"),
					CanClearLayerTooltipText,
					FNewMenuDelegate::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::FillClearLayerMenu, InLayerIndex),
					ClearLayerAction, 
					/*InExtensionHook = */NAME_None,
					EUserInterfaceActionType::None);

				// Delete Layer
				FUIAction DeleteLayerAction = FUIAction(
					FExecuteAction::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::DeleteLayer, InLayerIndex),
					FCanExecuteAction::CreateSPLambda(this, [this, InLayerIndex] { FText Reason; return CanDeleteLayer(InLayerIndex, Reason); }));
				TAttribute<FText> DeleteLayerTooltipText = MakeAttributeSPLambda(this, [this, InLayerIndex] { FText Reason; CanDeleteLayer(InLayerIndex, Reason); return Reason; });
				MenuBuilder.AddMenuEntry(LOCTEXT("DeleteLayer", "Delete..."), DeleteLayerTooltipText, FSlateIcon(), DeleteLayerAction);

				// Collapse Layer
				FUIAction CollapseLayerAction = FUIAction(
					FExecuteAction::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::CollapseLayer, InLayerIndex),
					FCanExecuteAction::CreateSPLambda(this, [this, InLayerIndex] { FText Reason; return CanCollapseLayer(InLayerIndex, Reason); }));
				TAttribute<FText> CollapseLayerTooltipText = MakeAttributeSPLambda(this, [this, InLayerIndex] { FText Reason; CanCollapseLayer(InLayerIndex, Reason); return Reason; });
				MenuBuilder.AddMenuEntry(LOCTEXT("CollapseLayer", "Collapse..."), CollapseLayerTooltipText, FSlateIcon(), CollapseLayerAction);

				// Add custom actions : 
				if (!EditLayer->GetActions().IsEmpty())
				{
					MenuBuilder.AddMenuSeparator();

					for (const ULandscapeEditLayerBase::FEditLayerAction& LayerAction : EditLayer->GetActions())
					{
						FUIAction LayerUIAction = FUIAction(
							FExecuteAction::CreateSPLambda(this, [this, InLayerIndex, LayerAction] { ExecuteCustomLayerAction(InLayerIndex, LayerAction); }),
							FCanExecuteAction::CreateSPLambda(this, [this, InLayerIndex, LayerAction] { FText Reason; return CanExecuteCustomLayerAction(InLayerIndex, LayerAction, Reason); }));
						TAttribute<FText> LayerActionTooltipText = MakeAttributeSPLambda(this, [this, InLayerIndex, LayerAction] { FText Reason; CanExecuteCustomLayerAction(InLayerIndex, LayerAction, Reason); return Reason; });
						MenuBuilder.AddMenuEntry(LayerAction.GetLabel(), LayerActionTooltipText, FSlateIcon(), LayerUIAction);
					}
				}
			}
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("LandscapeEditorLayerVisibility", LOCTEXT("LandscapeEditorLayerVisibility.Heading", "Visibility"));
		{
			if (Layer)
			{
				if (Layer->bVisible)
				{
					// Hide Selected Layer
					FUIAction HideSelectedLayerAction = FUIAction(FExecuteAction::CreateSPLambda(this, [this, InLayerIndex] { OnToggleVisibility(InLayerIndex); }),
						FCanExecuteAction::CreateSPLambda(this, [this, InLayerIndex] { FText Reason; return CanToggleVisibility(InLayerIndex, Reason); }));
					TAttribute<FText> HideSelectedLayerActionTooltipText = MakeAttributeSPLambda(this, [this, InLayerIndex] { FText Reason; CanToggleVisibility(InLayerIndex, Reason); return Reason; });
					MenuBuilder.AddMenuEntry(LOCTEXT("HideSelected", "Hide Selected"), HideSelectedLayerActionTooltipText, FSlateIcon(), HideSelectedLayerAction);
				}
				else
				{
					// Show Selected Layer
					FUIAction ShowSelectedLayerAction = FUIAction(FExecuteAction::CreateSPLambda(this, [this, InLayerIndex] { OnToggleVisibility(InLayerIndex); }),
						FCanExecuteAction::CreateSPLambda(this, [this, InLayerIndex] { FText Reason; return CanToggleVisibility(InLayerIndex, Reason); }));
					TAttribute<FText> ShowSelectedLayerActionTooltipText = MakeAttributeSPLambda(this, [this, InLayerIndex] { FText Reason; CanToggleVisibility(InLayerIndex, Reason); return Reason; });
					MenuBuilder.AddMenuEntry(LOCTEXT("ShowSelected", "Show Selected"), ShowSelectedLayerActionTooltipText, FSlateIcon(), ShowSelectedLayerAction);
				}

				// Show Only Selected Layer
				FUIAction ShowOnlySelectedLayerAction = FUIAction(FExecuteAction::CreateSPLambda(this, [this, InLayerIndex] { ShowOnlySelectedLayer(InLayerIndex); }));
				MenuBuilder.AddMenuEntry(LOCTEXT("ShowOnlySelected", "Show Only Selected"), LOCTEXT("ShowOnlySelectedLayerTooltip", "Show Only Selected Layer"), FSlateIcon(), ShowOnlySelectedLayerAction);
			}

			// Show All Layers
			FUIAction ShowAllLayersAction = FUIAction(FExecuteAction::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::ShowAllLayers));
			MenuBuilder.AddMenuEntry(LOCTEXT("ShowAllLayers", "Show All Layers"), LOCTEXT("ShowAllLayersTooltip", "Show All Layers"), FSlateIcon(), ShowAllLayersAction);
		}
		MenuBuilder.EndSection();

		const TArray<ALandscapeBlueprintBrushBase*>& Brushes = LandscapeEdMode->GetBrushList();
		TArray<ALandscapeBlueprintBrushBase*> FilteredBrushes = Brushes.FilterByPredicate([](ALandscapeBlueprintBrushBase* Brush) { return Brush->GetOwningLandscape() == nullptr; });
		if (FilteredBrushes.Num())
		{
			MenuBuilder.BeginSection("LandscapeEditorBrushActions", LOCTEXT("LandscapeEditorBrushActions.Heading", "Brushes"));
			{
				MenuBuilder.AddSubMenu(
					LOCTEXT("LandscaeEditorBrushAddSubMenu", "Add"),
					LOCTEXT("LandscaeEditorBrushAddSubMenuToolTip", "Add brush to current layer"),
					FNewMenuDelegate::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_Layers::FillAddBrushMenu, FilteredBrushes),
					false,
					FSlateIcon()
				);
			}
			MenuBuilder.EndSection();
		}

		return MenuBuilder.MakeWidget();
	}
	return nullptr;
}

bool FLandscapeEditorCustomNodeBuilder_Layers::CanRenameLayer(int32 InLayerIndex, FText& OutReason) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	const FLandscapeLayer* Layer = LandscapeEdMode->GetLayer(InLayerIndex);
	if (Layer == nullptr)
	{
		return false;
	}

	if (Layer->bLocked)
	{
		OutReason = LOCTEXT("RenameLayer_CantRenameLocked", "Cannot rename a locked edit layer");
		return false;
	}

	OutReason = LOCTEXT("RenameLayer_CanRename", "Rename the edit layer");
	return true;
}

void FLandscapeEditorCustomNodeBuilder_Layers::RenameLayer(int32 InLayerIndex)
{
	if (InlineTextBlocks.IsValidIndex(InLayerIndex) && InlineTextBlocks[InLayerIndex].IsValid())
	{
		InlineTextBlocks[InLayerIndex]->EnterEditingMode();
	}
}


bool FLandscapeEditorCustomNodeBuilder_Layers::CanClearTargetLayerOnLayer(int32 InLayerIndex, ULandscapeLayerInfoObject* InLayerInfo, FText& OutReason) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	const FLandscapeLayer* Layer = LandscapeEdMode->GetLayer(InLayerIndex);
	if (Layer == nullptr)
	{
		return false;
	}

	const ULandscapeEditLayerBase* EditLayer = Layer->EditLayer;
	check(EditLayer != nullptr);

	// Visibility case : 
	if (InLayerInfo->LayerName == UMaterialExpressionLandscapeVisibilityMask::ParameterName)
	{
		if (!EditLayer->SupportsTargetType(ELandscapeToolTargetType::Visibility))
		{
			OutReason = FText::Format(LOCTEXT("ClearTargetLayer_CantClearVisibilityOnLayerType", "Cannot clear visibility : the type of layer {0} ({1}) doesn't support visibility"),
				FText::FromName(Layer->Name), EditLayer->GetClass()->GetDisplayNameText());
			return false;
		}
	}
		
	if (!EditLayer->SupportsTargetType(ELandscapeToolTargetType::Weightmap))
	{
		OutReason = FText::Format(LOCTEXT("ClearTargetLayer_CantClearWeightmapOnLayerType", "Cannot clear weightmap : the type of layer {0} ({1}) doesn't support weightmaps"),
			FText::FromName(Layer->Name), EditLayer->GetClass()->GetDisplayNameText());
		return false;
	}

	OutReason = LOCTEXT("ClearTargetLayer_CanClear", "Clear the target layer on this edit layer");
	return true;
}

void FLandscapeEditorCustomNodeBuilder_Layers::ClearTargetLayerOnLayer(int32 InLayerIndex, ULandscapeLayerInfoObject* InLayerInfo)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (Landscape)
	{
		const FLandscapeLayer* Layer = LandscapeEdMode->GetLayer(InLayerIndex);
		if (Layer)
		{
			EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(LOCTEXT("Landscape_ClearTargetLayer_Message", "The layer {0} : {1} content will be completely cleared.  Continue?"), FText::FromName(Layer->Name), FText::FromName(InLayerInfo->LayerName)));
			if (Result == EAppReturnType::Yes)
			{
				const FScopedTransaction Transaction(LOCTEXT("Landscape_Layers_TargetClear", "Clear Target Layer"));
				Landscape->ClearPaintLayer(InLayerIndex, InLayerInfo);
				LandscapeEdMode->RequestUpdateLayerUsageInformation();
			}
		}
	}
}

bool FLandscapeEditorCustomNodeBuilder_Layers::CanClearLayer(int32 InLayerIndex, FText& OutReason) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	const FLandscapeLayer* Layer = LandscapeEdMode->GetLayer(InLayerIndex);
	if (Layer == nullptr)
	{
		return false;
	}

	const ULandscapeEditLayerBase* EditLayer = Layer->EditLayer;
	check(EditLayer != nullptr);

	if (Layer->bLocked)
	{
		OutReason = LOCTEXT("ClearLayer_CantClearLocked", "Cannot clear a locked edit layer");
		return false;
	}

	if (!EditLayer->SupportsEditingTools())
	{
		OutReason = FText::Format(LOCTEXT("ClearLayer_CantClearLayerWithoutPersistentTextures", "Cannot clear an edit layer which doesn't have editable textures (procedural)"),
			FText::FromName(Layer->Name), EditLayer->GetClass()->GetDisplayNameText());
		return false;
	}

	OutReason = LOCTEXT("ClearLayer_CanClear", "Clear the edit layer");
	return true;
}

bool FLandscapeEditorCustomNodeBuilder_Layers::CanClearTargetLayersOnLayer(int32 InLayerIndex, ELandscapeClearMode InClearMode, FText& OutReason) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	const FLandscapeLayer* Layer = LandscapeEdMode->GetLayer(InLayerIndex);
	if (Layer == nullptr)
	{
		return false;
	}

	const ULandscapeEditLayerBase* EditLayer = Layer->EditLayer;
	check(EditLayer != nullptr);

	// TODO [jonathan.bard] : Ideally, ELandscapeClearMode should be deprecated and we would treat Visibility and Weightmap separately but for now just keep on treating visibility as a standard weightmap : 
	if ((InClearMode == ELandscapeClearMode::Clear_Weightmap)
		&& !EditLayer->SupportsTargetType(ELandscapeToolTargetType::Weightmap) 
		&& !EditLayer->SupportsTargetType(ELandscapeToolTargetType::Visibility))
	{
			OutReason = FText::Format(LOCTEXT("ClearTargetLayers_CantClearWeightmapsOnLayerType", "Cannot clear weightmaps : the type of layer {0} ({1}) doesn't support weightmaps"), 
				FText::FromName(Layer->Name), EditLayer->GetClass()->GetDisplayNameText());
			return false;
	}
	else if ((InClearMode == ELandscapeClearMode::Clear_Heightmap)
		&& !EditLayer->SupportsTargetType(ELandscapeToolTargetType::Heightmap))
	{
		OutReason = FText::Format(LOCTEXT("ClearTargetLayers_CantClearHeightmapOnLayerType", "Cannot clear heightmap : the type of layer {0} ({1}) doesn't support heightmaps"),
			FText::FromName(Layer->Name), EditLayer->GetClass()->GetDisplayNameText());
			return false;
	}
	if ((InClearMode == ELandscapeClearMode::Clear_All)
		&& !EditLayer->SupportsTargetType(ELandscapeToolTargetType::Heightmap)
		&& !EditLayer->SupportsTargetType(ELandscapeToolTargetType::Weightmap)
		&& !EditLayer->SupportsTargetType(ELandscapeToolTargetType::Visibility))
	if ((InClearMode == ELandscapeClearMode::Clear_Heightmap) && !EditLayer->SupportsTargetType(ELandscapeToolTargetType::Heightmap))
	{
		OutReason = FText::Format(LOCTEXT("ClearTargetLayers_CantClearOnLayerType", "Cannot clear : the type of layer {0} ({1}) doesn't support heightmaps or weightmaps"),
			FText::FromName(Layer->Name), EditLayer->GetClass()->GetDisplayNameText());
		return false;
	}

	OutReason = LOCTEXT("ClearTargetLayers_CanClear", "Clear the target layers on the edit layer");
	return true;
}

void FLandscapeEditorCustomNodeBuilder_Layers::ClearTargetLayersOnLayer(int32 InLayerIndex, ELandscapeClearMode InClearMode)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (Landscape)
	{
		const FLandscapeLayer* Layer = LandscapeEdMode->GetLayer(InLayerIndex);
		if (Layer)
		{
			EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(LOCTEXT("Landscape_ClearLayer_Message", "The layer {0} content will be completely cleared.  Continue?"), FText::FromName(Layer->Name)));
			if (Result == EAppReturnType::Yes)
			{
				const FScopedTransaction Transaction(LOCTEXT("Landscape_Layers_Clean", "Clear Layer"));
				Landscape->ClearLayer(InLayerIndex, nullptr, InClearMode);
				OnLayerSelectionChanged(InLayerIndex);
				if (InClearMode & ELandscapeClearMode::Clear_Weightmap)
				{
					LandscapeEdMode->RequestUpdateLayerUsageInformation();
				}
			}
		}
	}
}

bool FLandscapeEditorCustomNodeBuilder_Layers::CanDeleteLayer(int32 InLayerIndex, FText& OutReason) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (!Landscape || Landscape->GetLayers().Num() <= 1)
	{
		OutReason = LOCTEXT("DeleteLayer_CantDeleteLastLayer", "The last layer cannot be deleted");
		return false;
	}

	const FLandscapeLayer* Layer = LandscapeEdMode->GetLayer(InLayerIndex);
	if (Layer == nullptr)
	{
		return false;
	}

	if (Layer->bLocked)
	{
		OutReason = LOCTEXT("DeleteLayer_CantDeleteLocked", "Cannot delete a locked edit layer");
		return false;
	}


	OutReason = LOCTEXT("DeleteLayer_CanDelete", "Delete the edit layer");
	return true;
}

void FLandscapeEditorCustomNodeBuilder_Layers::DeleteLayer(int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (Landscape && Landscape->GetLayers().Num() > 1)
	{
		const FLandscapeLayer* Layer = LandscapeEdMode->GetLayer(InLayerIndex);
		if (Layer)
		{
			EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(LOCTEXT("Landscape_DeleteLayer_Message", "The layer {0} will be deleted.  Continue?"), FText::FromName(Layer->Name)));
			if (Result == EAppReturnType::Yes)
			{
				const FScopedTransaction Transaction(LOCTEXT("Landscape_Layers_Delete", "Delete Layer"));
				Landscape->DeleteLayer(InLayerIndex);
				int32 NewLayerSelectionIndex = Landscape->GetLayerConst(InLayerIndex) ? InLayerIndex : 0;
				OnLayerSelectionChanged(NewLayerSelectionIndex);
				LandscapeEdMode->RefreshDetailPanel();
			}
		}
	}
}

bool FLandscapeEditorCustomNodeBuilder_Layers::CanCollapseLayer(int32 InLayerIndex, FText& OutReason) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (!Landscape || Landscape->GetLayers().Num() <= 1)
	{
		OutReason = LOCTEXT("Landscape_CollapseLayer_Reason_NotEnoughLayersToCollapse", "Not enough layers to do collapse");
		return false;
	}

	if (InLayerIndex < 1)
	{
		OutReason = LOCTEXT("Landscape_CollapseLayer_Reason_CantCollapseBaseLayer", "Cannot collapse the first layer");
		return false;
	}
	
	const FLandscapeLayer* TopLayer = LandscapeEdMode->GetLayer(InLayerIndex);
	const FLandscapeLayer* BottomLayer = LandscapeEdMode->GetLayer(InLayerIndex - 1);
	if ((TopLayer == nullptr) || (BottomLayer == nullptr))
	{
		return false;
	}

	const ULandscapeEditLayerBase* TopEditLayer = TopLayer->EditLayer;
	const ULandscapeEditLayerBase* BottomEditLayer = BottomLayer->EditLayer;
	check((TopEditLayer != nullptr) && (BottomEditLayer != nullptr));

	if (!TopEditLayer->SupportsCollapsingTo())
	{
		OutReason = FText::Format(LOCTEXT("Landscape_CollapseLayer_Reason_TopLayerDoesntSupportCollapsing", "Cannot collapse layer '{0}' onto layer '{1}'. The type of layer '{0}' ({2}) doesn't support collapsing to another one"), 
			FText::FromName(TopLayer->Name), FText::FromName(BottomLayer->Name), TopEditLayer->GetClass()->GetDisplayNameText());
		return false;
	}

	if (!BottomEditLayer->SupportsBeingCollapsedAway())
	{
		OutReason = FText::Format(LOCTEXT("Landscape_CollapseLayer_Reason_BottomLayerDoesntSupportCollapsing", "Cannot collapse layer '{0}' onto layer '{1}'. The type of layer '{1}' ({2}) doesn't support being collapsed away"),
			FText::FromName(TopLayer->Name), FText::FromName(BottomLayer->Name), BottomEditLayer->GetClass()->GetDisplayNameText());
		return false;
	}

	if (TopLayer->bLocked)
	{
		OutReason = FText::Format(LOCTEXT("Landscape_CollapseLayer_Reason_TopLayerIsLocked", "Cannot collapse layer '{0}' onto layer '{1}'. Layer '{0}' will be deleted in the operation but it is currently locked"),
			FText::FromName(TopLayer->Name), FText::FromName(BottomLayer->Name));
		return false;
	}

	if (BottomLayer->bLocked)
	{
		OutReason = FText::Format(LOCTEXT("Landscape_CollapseLayer_Reason_BottomLayerIsLocked", "Cannot collapse layer '{0}' onto layer '{1}'. Destination layer '{1}' is currently locked"),
			FText::FromName(TopLayer->Name), FText::FromName(BottomLayer->Name));
		return false;
	}

	// Can't collapse on layer that has a Brush because result will change...
	if (TopLayer->Brushes.Num() > 0)
	{
		OutReason = FText::Format(LOCTEXT("Landscape_CollapseLayer_Reason_TopLayerHasBrush", "Cannot collapse layer '{0}' onto layer '{1}'. Layer '{0}' contains brush(es)"),
			FText::FromName(TopLayer->Name), FText::FromName(BottomLayer->Name));
		return false;
	}

	if (BottomLayer->Brushes.Num() > 0)
	{
		OutReason = FText::Format(LOCTEXT("Landscape_CollapseLayer_Reason_BottomLayerHasBrush", "Cannot collapse layer '{0}' onto layer '{1}'. Layer '{1}' contains brush(es)"),
			FText::FromName(TopLayer->Name), FText::FromName(BottomLayer->Name));
		return false;
	}
	
	OutReason = FText::Format(LOCTEXT("Landscape_CollapseLayer_Reason_Collapse", "Collapse layer '{0}' onto layer '{1}'"),
		FText::FromName(TopLayer->Name), FText::FromName(BottomLayer->Name));
	return true;
}

void FLandscapeEditorCustomNodeBuilder_Layers::CollapseLayer(int32 InLayerIndex)
{
	FText Reason;
	check(CanCollapseLayer(InLayerIndex, Reason));

	if (FEdModeLandscape* LandscapeEdMode = GetEditorMode())
	{
		ALandscape* Landscape = LandscapeEdMode->GetLandscape();
		const FLandscapeLayer* Layer = LandscapeEdMode->GetLayer(InLayerIndex);
		const FLandscapeLayer* BaseLayer = LandscapeEdMode->GetLayer(InLayerIndex - 1);
		if (Landscape && Layer && BaseLayer)
		{
			EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(LOCTEXT("Landscape_CollapseLayer_Message", "The layer {0} will be collapsed into layer {1}.  Continue?"), FText::FromName(Layer->Name), FText::FromName(BaseLayer->Name)));
			if (Result == EAppReturnType::Yes)
			{
				const FScopedTransaction Transaction(LOCTEXT("Landscape_Layers_Collapse", "Collapse Layer"));
				Landscape->CollapseLayer(InLayerIndex);
				OnLayerSelectionChanged(InLayerIndex - 1);
				LandscapeEdMode->RefreshDetailPanel();
			}
		}
	}
}

bool FLandscapeEditorCustomNodeBuilder_Layers::CanExecuteCustomLayerAction(int32 InLayerIndex, const ULandscapeEditLayerBase::FEditLayerAction& InCustomLayerAction, FText& OutReason) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (Landscape == nullptr)
	{
		OutReason = LOCTEXT("Landscape_CannotExecuteCustomLayerAction_Invalid", "Invalid landscape");
		return false;
	}

	const FLandscapeLayer* Layer = LandscapeEdMode->GetLayer(InLayerIndex);
	if (Layer == nullptr)
	{
		return false;
	}

	const ULandscapeEditLayerBase* EditLayer = Layer->EditLayer;
	check(EditLayer != nullptr);
	ULandscapeEditLayerBase::FEditLayerAction::FExecuteParams ExecuteParams(Layer, Landscape);
	return InCustomLayerAction.GetCanExecuteDelegate().Execute(ExecuteParams, OutReason);
}

void FLandscapeEditorCustomNodeBuilder_Layers::ExecuteCustomLayerAction(int32 InLayerIndex, const ULandscapeEditLayerBase::FEditLayerAction& InCustomLayerAction)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr)
	{
		const FLandscapeLayer* Layer = LandscapeEdMode->GetLayer(InLayerIndex);
		if (Layer != nullptr)
		{
			const ULandscapeEditLayerBase* EditLayer = Layer->EditLayer;
			check(EditLayer != nullptr);

			const FScopedTransaction Transaction(InCustomLayerAction.GetLabel());
			ULandscapeEditLayerBase::FEditLayerAction::FExecuteParams ExecuteParams(Layer, Landscape);
			ULandscapeEditLayerBase::FEditLayerAction::FExecuteResult Result = InCustomLayerAction.GetExecuteDelegate().Execute(ExecuteParams);
			if (!Result.bSuccess)
			{
				// Indicate to the user that the action failed : 
				FMessageDialog::Open(EAppMsgType::Ok, Result.Reason);
			}
		}
	}
}

void FLandscapeEditorCustomNodeBuilder_Layers::ShowOnlySelectedLayer(int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (Landscape)
	{
		const FScopedTransaction Transaction(LOCTEXT("ShowOnlySelectedLayer", "Show Only Selected Layer"));
		Landscape->ShowOnlySelectedLayer(InLayerIndex);
		OnLayerSelectionChanged(InLayerIndex);
	}
}

void FLandscapeEditorCustomNodeBuilder_Layers::ShowAllLayers()
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (Landscape)
	{
		const FScopedTransaction Transaction(LOCTEXT("ShowAllLayers", "Show All Layers"));
		Landscape->ShowAllLayers();
	}
}

TSubclassOf<ULandscapeEditLayerBase> FLandscapeEditorCustomNodeBuilder_Layers::PickEditLayerClass() const
{
	class FLandscapeEditLayerClassFilter : public IClassViewerFilter
	{
	public:
		FLandscapeEditLayerClassFilter()
		{
			AllowedChildrenOfClasses.Add(ULandscapeEditLayerBase::StaticClass());
			DisallowedClassFlags = CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists;
		};

		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< class FClassViewerFilterFuncs > InFilterFuncs) override
		{
			bool bIsCorrectClass = InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InClass) != EFilterReturn::Failed;
			bool bValidClassFlags = !InClass->HasAnyClassFlags(DisallowedClassFlags);

			return (bIsCorrectClass && bValidClassFlags);
		};

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const class IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< class FClassViewerFilterFuncs > InFilterFuncs) override
		{
			return false;
		};

	private:
		/** All children of these classes will be included unless filtered out by another setting. */
		TSet<const UClass*> AllowedChildrenOfClasses;

		/** Disallowed class flags. */
		EClassFlags DisallowedClassFlags;
	};

	// Load the classviewer module to display a class picker
	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

	// Fill in options
	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;

	TSharedPtr<FLandscapeEditLayerClassFilter> Filter = MakeShareable(new FLandscapeEditLayerClassFilter());
	Options.ClassFilters.Add(Filter.ToSharedRef());

	const FText TitleText = LOCTEXT("PickEditLayerClass", "Pick Landscape Edit Layer Class");
	UClass* ChosenClass = nullptr;
	SClassPickerDialog::PickClass(TitleText, Options, ChosenClass, ULandscapeEditLayerBase::StaticClass());
	return TSubclassOf<ULandscapeEditLayerBase>(ChosenClass);
}

void FLandscapeEditorCustomNodeBuilder_Layers::CreateLayer()
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (Landscape)
	{
		if (TSubclassOf<ULandscapeEditLayerBase> EditLayerClass = PickEditLayerClass())
		{
			// Disallow multiple layers of certain types : 
			if (!EditLayerClass.GetDefaultObject()->SupportsMultiple())
			{
				if (int32 NumLayersOfThisType = Landscape->GetLayersOfType(EditLayerClass).Num(); NumLayersOfThisType > 0)
				{
					FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("Landscape_CreateLayer_CannotCreateMultiple", "Cannot create layer of type {0} : {1} {1}|plural(one=layer, other=layers) of this type already {1}|plural(one=exists, other=exist) and only 1 is allowed"), 
						EditLayerClass->GetDisplayNameText(), NumLayersOfThisType));
					return;
				}
			}

			const FScopedTransaction Transaction(LOCTEXT("Landscape_Layers_Create", "Create Layer"));
			Landscape->CreateLayer(FName(EditLayerClass.GetDefaultObject()->GetDefaultName()), EditLayerClass);
			OnLayerSelectionChanged(Landscape->GetLayerCount() - 1);

			LandscapeEdMode->RefreshDetailPanel();
		}
	}
}

FText FLandscapeEditorCustomNodeBuilder_Layers::GetNumLayersText() const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (Landscape)
	{
		return FText::Format(LOCTEXT("NumEditLayersText", "{0} Edit {0}|plural(one=Layer, other=Layers)"), Landscape->GetLayers().Num());
	}

	return FText();
}

bool FLandscapeEditorCustomNodeBuilder_Layers::CanCreateLayer(FText& OutReason) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (Landscape)
	{
		if (Landscape->IsMaxLayersReached())
		{
			OutReason = LOCTEXT("CreateLayerTooltip_MaxLayersReached", "Creates a new edit layer.\nCurrently disabled as the max number of layers has been reached. This can be adjusted in the landscape project settings : MaxNumberOfLayers)");
			return false;
		}
	}

	OutReason = LOCTEXT("CreateLayerTooltip", "Creates a new edit layer");
	return true;
}

void FLandscapeEditorCustomNodeBuilder_Layers::OnLayerSelectionChanged(int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode && LandscapeEdMode->GetCurrentLayerIndex() != InLayerIndex)
	{
		FScopedTransaction Transaction(LOCTEXT("Landscape_Layers_SetCurrentLayer", "Set Current Layer"));
		LandscapeEdMode->SetCurrentLayer(InLayerIndex);
		LandscapeEdMode->UpdateTargetList();
	}
}

TOptional<float> FLandscapeEditorCustomNodeBuilder_Layers::GetLayerAlpha(int32 InLayerIndex) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();

	if (LandscapeEdMode)
	{
		return LandscapeEdMode->GetLayerAlpha(InLayerIndex);
	}

	return 1.0f;
}

bool FLandscapeEditorCustomNodeBuilder_Layers::CanSetLayerAlpha(int32 InLayerIndex, FText& OutReason) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	const FLandscapeLayer* Layer = LandscapeEdMode->GetLayer(InLayerIndex);
	if (Layer == nullptr)
	{
		return false;
	}

	if (Layer->bLocked)
	{
		OutReason = LOCTEXT("SetLayerAlpha_LayerIsLocked", "Cannot change the alpha of a locked edit layer");
		return false;
	}

	OutReason = LOCTEXT("SetLayerAlpha_CanSet", "Set the edit layer's alpha");
	return true;
}

void FLandscapeEditorCustomNodeBuilder_Layers::SetLayerAlpha(float InAlpha, int32 InLayerIndex, bool bCommit)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode)
	{
		// We get multiple commits when editing through the text box
		if (LandscapeEdMode->GetLayerAlpha(InLayerIndex) == LandscapeEdMode->GetClampedLayerAlpha(InAlpha))
		{
			return;
		}

		FScopedTransaction Transaction(LOCTEXT("Landscape_Layers_SetAlpha", "Set Layer Alpha"), CurrentSlider == INDEX_NONE && bCommit);
		// Set Value when using slider or when committing text
		LandscapeEdMode->SetLayerAlpha(InLayerIndex, InAlpha);
	}
}

bool FLandscapeEditorCustomNodeBuilder_Layers::CanToggleVisibility(int32 InLayerIndex, FText& OutReason) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	const FLandscapeLayer* Layer = LandscapeEdMode->GetLayer(InLayerIndex);
	if (Layer == nullptr)
	{
		return false;
	}

	if (Layer->bLocked)
	{
		OutReason = LOCTEXT("ToggleVisibility_CantToggleLocked", "Cannot change the visibility of a locked edit layer");
		return false;
	}

	OutReason = LOCTEXT("ToggleVisibility_CanToggle", "Toggle the visibility of the edit layer");
	return true;
}

FReply FLandscapeEditorCustomNodeBuilder_Layers::OnToggleVisibility(int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode)
	{
		const FScopedTransaction Transaction(LOCTEXT("Landscape_Layers_SetVisibility", "Set Layer Visibility"));
		LandscapeEdMode->SetLayerVisibility(!LandscapeEdMode->IsLayerVisible(InLayerIndex), InLayerIndex);
		if (LandscapeEdMode->IsLayerVisible(InLayerIndex))
		{
			OnLayerSelectionChanged(InLayerIndex);
		}
	}
	return FReply::Handled();
}

const FSlateBrush* FLandscapeEditorCustomNodeBuilder_Layers::GetVisibilityBrushForLayer(int32 InLayerIndex) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	bool bIsVisible = LandscapeEdMode && LandscapeEdMode->IsLayerVisible(InLayerIndex);
	return bIsVisible ? FAppStyle::GetBrush("Level.VisibleIcon16x") : FAppStyle::GetBrush("Level.NotVisibleIcon16x");
}

FReply FLandscapeEditorCustomNodeBuilder_Layers::OnToggleLock(int32 InLayerIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode)
	{
		const FScopedTransaction Transaction(LOCTEXT("Landscape_Layers_Locked", "Set Layer Locked"));
		LandscapeEdMode->SetLayerLocked(InLayerIndex, !LandscapeEdMode->IsLayerLocked(InLayerIndex));
	}
	return FReply::Handled();
}

EVisibility FLandscapeEditorCustomNodeBuilder_Layers::GetLayerAlphaVisibility(int32 InLayerIndex) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	bool bIsVisible = LandscapeEdMode && LandscapeEdMode->IsLayerAlphaVisible(InLayerIndex);
	return bIsVisible ? EVisibility::Visible : EVisibility::Hidden;
}

TSharedPtr<IToolTip> FLandscapeEditorCustomNodeBuilder_Layers::GetEditLayerTypeTooltip(int32 InLayerIndex) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	const ULandscapeEditLayerBase* EditLayer = (LandscapeEdMode != nullptr) ? LandscapeEdMode->GetEditLayer(InLayerIndex) : nullptr;
	return (EditLayer != nullptr) ? TSharedPtr<IToolTip>(FEditorClassUtils::GetTooltip(EditLayer->GetClass())) : nullptr;
}

const FSlateBrush* FLandscapeEditorCustomNodeBuilder_Layers::GetLockBrushForLayer(int32 InLayerIndex) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	bool bIsLocked = LandscapeEdMode && LandscapeEdMode->IsLayerLocked(InLayerIndex);
	return bIsLocked ? FAppStyle::GetBrush(TEXT("PropertyWindow.Locked")) : FAppStyle::GetBrush(TEXT("PropertyWindow.Unlocked"));
}

const FSlateBrush* FLandscapeEditorCustomNodeBuilder_Layers::GetEditLayerIconBrush(int32 InLayerIndex) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	const ULandscapeEditLayerBase* EditLayer = (LandscapeEdMode != nullptr) ? LandscapeEdMode->GetEditLayer(InLayerIndex) : nullptr;
	return (EditLayer != nullptr) ? FSlateIconFinder::FindIconBrushForClass(EditLayer->GetClass()) : nullptr;
}

int32 FLandscapeEditorCustomNodeBuilder_Layers::SlotIndexToLayerIndex(int32 SlotIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (!Landscape)
	{
		return INDEX_NONE;
	}
	
	check(Landscape->GetLayers().IsValidIndex(SlotIndex));
	return Landscape->GetLayers().Num() - SlotIndex - 1;
}

FReply FLandscapeEditorCustomNodeBuilder_Layers::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, int32 SlotIndex, SVerticalBox::FSlot* Slot)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode && LandscapeEdMode->DoesCurrentToolAffectEditLayers())
	{
		int32 LayerIndex = SlotIndexToLayerIndex(SlotIndex);
		const FLandscapeLayer* Layer = LandscapeEdMode->GetLayer(LayerIndex);
		if (Layer && !Layer->bLocked)
		{
			TSharedPtr<SWidget> Row = GenerateRow(LayerIndex);
			if (Row.IsValid())
			{
				return FReply::Handled().BeginDragDrop(FLandscapeListElementDragDropOp::New(SlotIndex, Slot, Row));
			}
		}
	}
	return FReply::Unhandled();
}

TOptional<SDragAndDropVerticalBox::EItemDropZone> FLandscapeEditorCustomNodeBuilder_Layers::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, SVerticalBox::FSlot* Slot)
{
	TSharedPtr<FLandscapeListElementDragDropOp> DragDropOperation = DragDropEvent.GetOperationAs<FLandscapeListElementDragDropOp>();
	if (DragDropOperation.IsValid())
	{
		return DropZone;
	}
	return TOptional<SDragAndDropVerticalBox::EItemDropZone>();
}

FReply FLandscapeEditorCustomNodeBuilder_Layers::HandleAcceptDrop(FDragDropEvent const& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, int32 SlotIndex, SVerticalBox::FSlot* Slot)
{
	TSharedPtr<FLandscapeListElementDragDropOp> DragDropOperation = DragDropEvent.GetOperationAs<FLandscapeListElementDragDropOp>();

	if (!DragDropOperation.IsValid())
	{
		return FReply::Unhandled();
	}

		FEdModeLandscape* LandscapeEdMode = GetEditorMode();
		ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
	if (!Landscape)
	{
		return FReply::Unhandled();
	}

	// See if we're actually getting a drag from the blueprint brush list, rather than
	// from the edit layer list
	if (DragDropOperation->IsOfType<FLandscapeBrushDragDropOp>())
	{
		int32 StartingBrushIndex = DragDropOperation->SlotIndexBeingDragged;
		int32 StartingLayerIndex = LandscapeEdMode->GetCurrentLayerIndex();
		int32 DestinationLayerIndex = SlotIndexToLayerIndex(SlotIndex);

		if (StartingLayerIndex == DestinationLayerIndex)
		{
			// See comment further below about not returning Handled()
			return FReply::Unhandled();
		}

		ALandscapeBlueprintBrushBase* Brush = Landscape->GetBrushForLayer(StartingLayerIndex, StartingBrushIndex);
		if (!ensure(Brush))
		{
			return FReply::Unhandled();
		}

		const FScopedTransaction Transaction(LOCTEXT("Landscape_LayerBrushes_MoveLayers", "Move Brush to Layer"));
		Landscape->RemoveBrushFromLayer(StartingLayerIndex, StartingBrushIndex);
		Landscape->AddBrushToLayer(DestinationLayerIndex, Brush);

		LandscapeEdMode->SetCurrentLayer(DestinationLayerIndex);
		LandscapeEdMode->RefreshDetailPanel();

		// HACK: We don't return FReply::Handled() here because otherwise, SDragAndDropVerticalBox::OnDrop
		// will apply UI slot reordering after we return. Properly speaking, we should have a way to signal 
		// that the operation was handled yet that it is not one that SDragAndDropVerticalBox should deal with.
		// For now, however, just make sure to return Unhandled.
		return FReply::Unhandled();
	}

	// This must be a drag from our own list.
	int32 StartingLayerIndex = SlotIndexToLayerIndex(DragDropOperation->SlotIndexBeingDragged);
	int32 DestinationLayerIndex = SlotIndexToLayerIndex(SlotIndex);
	const FScopedTransaction Transaction(LOCTEXT("Landscape_Layers_Reorder", "Reorder Layer"));
	if (Landscape->ReorderLayer(StartingLayerIndex, DestinationLayerIndex))
	{
		LandscapeEdMode->SetCurrentLayer(DestinationLayerIndex);
		LandscapeEdMode->RefreshDetailPanel();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

TSharedRef<FLandscapeListElementDragDropOp> FLandscapeListElementDragDropOp::New(int32 InSlotIndexBeingDragged, SVerticalBox::FSlot* InSlotBeingDragged, TSharedPtr<SWidget> WidgetToShow)
{
	TSharedRef<FLandscapeListElementDragDropOp> Operation = MakeShareable(new FLandscapeListElementDragDropOp);

	Operation->MouseCursor = EMouseCursor::GrabHandClosed;
	Operation->SlotIndexBeingDragged = InSlotIndexBeingDragged;
	Operation->SlotBeingDragged = InSlotBeingDragged;
	Operation->WidgetToShow = WidgetToShow;

	Operation->Construct();

	return Operation;
}

TSharedPtr<SWidget> FLandscapeListElementDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ContentBrowser.AssetDragDropTooltipBackground"))
		.Content()
		[
			WidgetToShow.ToSharedRef()
		];
}

#undef LOCTEXT_NAMESPACE