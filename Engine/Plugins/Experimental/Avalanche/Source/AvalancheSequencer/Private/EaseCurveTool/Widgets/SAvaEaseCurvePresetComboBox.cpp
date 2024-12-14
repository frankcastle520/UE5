// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAvaEaseCurvePresetComboBox.h"
#include "EaseCurveTool/AvaEaseCurvePreset.h"
#include "EaseCurveTool/AvaEaseCurveStyle.h"
#include "EaseCurveTool/AvaEaseCurveSubsystem.h"
#include "EaseCurveTool/AvaEaseCurveToolSettings.h"
#include "EaseCurveTool/Widgets/SAvaEaseCurvePreview.h"
#include "Editor.h"
#include "DetailLayoutBuilder.h"
#include "Framework/SlateDelegates.h"
#include "Internationalization/Text.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformWrapPanel.h"

#define LOCTEXT_NAMESPACE "SAvaEaseCurvePresetComboBox"

void SAvaEaseCurvePresetComboBox::Construct(const FArguments& InArgs)
{
	DisplayRate = InArgs._DisplayRate;
	bAllowEditMode = InArgs._AllowEditMode;
	OnPresetChanged = InArgs._OnPresetChanged;
	OnQuickPresetChanged = InArgs._OnQuickPresetChanged;
	
	ChildSlot
		[
			SNew(SComboButton)
			.OnGetMenuContent(this, &SAvaEaseCurvePresetComboBox::GeneratePresetDropdown)
			.OnMenuOpenChanged_Lambda([this](const bool bInOpening)
				{
					bEditMode.Set(false);
				})
			.ButtonContent()
			[
				SAssignNew(SelectedRowContainer, SBox)
			]
		];
}

TSharedRef<SWidget> SAvaEaseCurvePresetComboBox::GenerateSearchRowWidget()
{
	static const FVector2D ButtonImageSize = FVector2D(FAvaEaseCurveStyle::Get().GetFloat(TEXT("ToolButton.ImageSize")));
	
	TSharedRef<SHorizontalBox> RowWidget = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("SearchHintLabel", "Search"))
			.OnTextChanged(this, &SAvaEaseCurvePresetComboBox::OnSearchTextChanged)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(3.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.ButtonStyle(FAvaEaseCurveStyle::Get(), TEXT("ToolButton"))
			.VAlign(VAlign_Center)
			.ToolTipText(LOCTEXT("ReloadJsonPresetsToolTip", "Reload ease curve presets from Json files"))
			.OnClicked(this, &SAvaEaseCurvePresetComboBox::ReloadJsonPresets)
			[
				SNew(SImage)
				.DesiredSizeOverride(ButtonImageSize)
				.Image(FAppStyle::GetBrush(TEXT("Icons.Refresh")))
			]
		];
	
	if (bAllowEditMode)
	{
		RowWidget->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAvaEaseCurveStyle::Get(), TEXT("ToolButton"))
				.VAlign(VAlign_Center)
				.ToolTipText(LOCTEXT("ExploreJsonPresetsFolderToolTip", "Opens the folder location for the Json ease curve presets"))
				.OnClicked(this, &SAvaEaseCurvePresetComboBox::ExploreJsonPresetsFolder)
				[
					SNew(SImage)
					.DesiredSizeOverride(ButtonImageSize)
					.Image(FAppStyle::GetBrush(TEXT("Icons.FolderOpen")))
				]
			];
		
		RowWidget->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3.f, 0.f, 3.f, 0.f)
			[
				SNew(SCheckBox)
				.Style(FAvaEaseCurveStyle::Get(), TEXT("ToolToggleButton"))
				.Padding(4.f)
				.ToolTipText(LOCTEXT("ToggleEditModeToolTip", "Enable editing of ease curve presets and categories"))
				.IsChecked_Lambda([this]()
					{
						return bEditMode.Get(false) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
				.OnCheckStateChanged(this, &SAvaEaseCurvePresetComboBox::ToggleEditMode)
				[
					SNew(SImage)
					.DesiredSizeOverride(ButtonImageSize)
					.Image(FAppStyle::GetBrush(TEXT("Icons.Edit")))
				]
			];
		
		RowWidget->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAvaEaseCurveStyle::Get(), TEXT("ToolButton"))
				.VAlign(VAlign_Center)
				.ToolTipText(LOCTEXT("CreateCategoryToolTip", "Creates a new empty category"))
				.Visibility_Lambda([this]()
					{
						return bEditMode.Get(false) ? EVisibility::Visible : EVisibility::Collapsed;
					})
				.OnClicked(this, &SAvaEaseCurvePresetComboBox::CreateNewCategory)
				[
					SNew(SImage)
					.DesiredSizeOverride(ButtonImageSize)
					.Image(FAppStyle::GetBrush(TEXT("Icons.Plus")))
				]
			];
	}

	return RowWidget;
}

TSharedRef<SWidget> SAvaEaseCurvePresetComboBox::GeneratePresetDropdown()
{
	TSharedRef<SWidget> OutWidget = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(3.f)
		[
			GenerateSearchRowWidget()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.Padding(3.f, 0.f, 3.f, 3.f)
		[
			SNew(SBox)
			.MaxDesiredHeight(960.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(GroupWidgetsParent, SBox)
				]
			]
		];

	RegenerateGroupWrapBox();

	return OutWidget;
}

void SAvaEaseCurvePresetComboBox::UpdateGroupsContent()
{
	const UAvaEaseCurveSubsystem& EaseCurveSubsystem = UAvaEaseCurveSubsystem::Get();
	const TArray<FString>& EaseCurveCategories = EaseCurveSubsystem.GetEaseCurveCategories();

	auto GenerateNoPresetsWidget = [](const FText& InText)
		{
			return SNew(SBox)
				.WidthOverride(300.f)
				.HeightOverride(200.f)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(FAppStyle::Get(), TEXT("HintText"))
					.Text(InText)
				];
		};

	if (EaseCurveCategories.Num() == 0)
	{
		GroupWidgetsParent->SetContent(GenerateNoPresetsWidget(LOCTEXT("NoPresetsLabel", "No ease curve presets")));
		return;
	}

	if (!SearchText.IsEmpty())
	{
		int32 TotalVisiblePresets = 0;
		for (const TSharedPtr<SAvaEaseCurvePresetGroup>& GroupWidget : GroupWidgets)
		{
			TotalVisiblePresets += GroupWidget->GetVisiblePresetCount();
		}
		if (TotalVisiblePresets == 0)
		{
			GroupWidgetsParent->SetContent(GenerateNoPresetsWidget(LOCTEXT("NoPresetsFoundLabel", "No ease curve presets found")));
			return;
		}
	}

	GroupWidgetsParent->SetContent(GroupWrapBox.ToSharedRef());
}

void SAvaEaseCurvePresetComboBox::RegenerateGroupWrapBox()
{
	const UAvaEaseCurveSubsystem& EaseCurveSubsystem = UAvaEaseCurveSubsystem::Get();
	const TArray<FString>& EaseCurveCategories = EaseCurveSubsystem.GetEaseCurveCategories();
	const int32 CurvePresetCount = EaseCurveCategories.Num();

	GroupWidgets.Empty(CurvePresetCount);

	GroupWrapBox = SNew(SUniformWrapPanel)
		.HAlign(HAlign_Center)
		.SlotPadding(FMargin(2.f, 1.f))
		.EvenRowDistribution(true)
		.NumColumnsOverride_Lambda([]()
			{
				const int32 EaseCurveCategoryCount = UAvaEaseCurveSubsystem::Get().GetEaseCurveCategories().Num();
				return FMath::Min(5, EaseCurveCategoryCount);
			});

	for (const FString& Category : EaseCurveCategories)
	{
		TSharedRef<SAvaEaseCurvePresetGroup> NewGroupWidget = SNew(SAvaEaseCurvePresetGroup)
			.CategoryName(Category)
			.Presets(EaseCurveSubsystem.GetEaseCurvePresets(Category))
			.SelectedPreset(SelectedItem)
			.IsEditMode(bEditMode)
			.DisplayRate(DisplayRate.Get())
			.OnCategoryDelete(this, &SAvaEaseCurvePresetComboBox::HandleCategoryDelete)
			.OnCategoryRename(this, &SAvaEaseCurvePresetComboBox::HandleCategoryRename)
			.OnPresetDelete(this, &SAvaEaseCurvePresetComboBox::HandlePresetDelete)
			.OnPresetRename(this, &SAvaEaseCurvePresetComboBox::HandlePresetRename)
			.OnBeginPresetMove(this, &SAvaEaseCurvePresetComboBox::HandleBeginPresetMove)
			.OnEndPresetMove(this, &SAvaEaseCurvePresetComboBox::HandleEndPresetMove)
			.OnPresetClick(this, &SAvaEaseCurvePresetComboBox::HandlePresetClick)
			.OnSetQuickEase(this, &SAvaEaseCurvePresetComboBox::HandleSetQuickEase);

		GroupWidgets.Add(NewGroupWidget);

		GroupWrapBox->AddSlot()
			.HAlign(HAlign_Left)
			[
				NewGroupWidget
			];
	}

	UpdateGroupsContent();
}

void SAvaEaseCurvePresetComboBox::GenerateSelectedRowWidget()
{
	TSharedPtr<SWidget> OutRowWidget;
	
	if (!SelectedItem.IsValid())
	{
		OutRowWidget = SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(FText::FromString("Select Preset..."));
	}
	else
	{
		OutRowWidget = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(0.f, 2.f, 5.f, 2.f)
			[
				SNew(SBorder)
				.BorderBackgroundColor(FStyleColors::White25)
				[
					SNew(SAvaEaseCurvePreview)
					.PreviewSize(12.f)
					.CustomToolTip(true)
					.DisplayRate(DisplayRate.Get())
					.Tangents_Lambda([this]()
						{
							return SelectedItem.IsValid() ? SelectedItem->Tangents : FAvaEaseCurveTangents();
						})
				]
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 5.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FStyleColors::Foreground)
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Text_Lambda([this]()
						{
							return SelectedItem.IsValid() ? FText::FromString(SelectedItem->Name) : FText();
						})
				]
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FStyleColors::White25)
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Text_Lambda([this]()
						{
							return SelectedItem.IsValid() ? FText::FromString(SelectedItem->Category) : FText();
						})
				]
			];
	}

	SelectedRowContainer->SetContent(OutRowWidget.ToSharedRef());
}

FReply SAvaEaseCurvePresetComboBox::OnDeletePresetClick()
{
	if (SelectedItem.IsValid())
	{
		if (UAvaEaseCurveSubsystem* EaseCurveSubsystem = GEditor->GetEditorSubsystem<UAvaEaseCurveSubsystem>())
		{
			EaseCurveSubsystem->RemovePreset(*SelectedItem);
		}

		ClearSelection();
	}

	return FReply::Handled();
}

void SAvaEaseCurvePresetComboBox::OnSearchTextChanged(const FText& InSearchText)
{
	SearchText = InSearchText;

	for (const TSharedPtr<SAvaEaseCurvePresetGroup>& GroupWidget : GroupWidgets)
	{
		GroupWidget->SetSearchText(SearchText);
	}

	UpdateGroupsContent();
}

bool SAvaEaseCurvePresetComboBox::HasSelection() const
{
	return SelectedItem.IsValid();
}

void SAvaEaseCurvePresetComboBox::ClearSelection()
{
	SelectedItem.Reset();

	GenerateSelectedRowWidget();
}

bool SAvaEaseCurvePresetComboBox::GetSelectedItem(FAvaEaseCurvePreset& OutPreset) const
{
	if (!SelectedItem.IsValid())
	{
		return false;
	}

	OutPreset = *SelectedItem;

	return true;
}

bool SAvaEaseCurvePresetComboBox::SetSelectedItem(const FString& InName)
{
	UAvaEaseCurveSubsystem& EaseCurveSubsystem = UAvaEaseCurveSubsystem::Get();

	const TSharedPtr<FAvaEaseCurvePreset>& FoundItem = EaseCurveSubsystem.FindPreset(InName);
	if (!FoundItem.IsValid())
	{
		return false;
	}

	SelectedItem = FoundItem;

	GenerateSelectedRowWidget();

	return true;
}

bool SAvaEaseCurvePresetComboBox::SetSelectedItem(const FAvaEaseCurveTangents& InTangents)
{
	UAvaEaseCurveSubsystem& EaseCurveSubsystem = UAvaEaseCurveSubsystem::Get();

	const TSharedPtr<FAvaEaseCurvePreset>& FoundItem = EaseCurveSubsystem.FindPresetByTangents(InTangents);
	if (!FoundItem.IsValid())
	{
		return false;
	}

	SelectedItem = FoundItem;

	GenerateSelectedRowWidget();
	
	return true;
}

bool SAvaEaseCurvePresetComboBox::SetSelectedItem(const FAvaEaseCurvePreset& InPreset)
{
	return SetSelectedItem(InPreset.Name);
}

FReply SAvaEaseCurvePresetComboBox::ReloadJsonPresets()
{
	UAvaEaseCurveSubsystem::Get().ReloadPresetsFromJson();

	RegenerateGroupWrapBox();

	return FReply::Handled();
}

FReply SAvaEaseCurvePresetComboBox::ExploreJsonPresetsFolder()
{
	UAvaEaseCurveSubsystem::Get().ExploreJsonPresetsFolder();

	return FReply::Handled();
}

FReply SAvaEaseCurvePresetComboBox::CreateNewCategory()
{
	UAvaEaseCurveSubsystem::Get().AddNewPresetCategory();

	ReloadJsonPresets();

	return FReply::Handled();
}

void SAvaEaseCurvePresetComboBox::ToggleEditMode(const ECheckBoxState bInNewState)
{
	bEditMode.Set(bInNewState == ECheckBoxState::Checked ? true : false);

	RegenerateGroupWrapBox();
}

bool SAvaEaseCurvePresetComboBox::HandleCategoryDelete(const FString& InCategoryName)
{
	if (!UAvaEaseCurveSubsystem::Get().RemovePresetCategory(InCategoryName))
	{
		return false;
	}

	ReloadJsonPresets();

	return true;
}

bool SAvaEaseCurvePresetComboBox::HandleCategoryRename(const FString& InCategoryName, const FString& InNewName)
{
	return UAvaEaseCurveSubsystem::Get().RenamePresetCategory(InCategoryName, InNewName);
}

bool SAvaEaseCurvePresetComboBox::HandlePresetDelete(const TSharedPtr<FAvaEaseCurvePreset>& InPreset)
{
	return UAvaEaseCurveSubsystem::Get().RemovePreset(*InPreset);
}

bool SAvaEaseCurvePresetComboBox::HandlePresetRename(const TSharedPtr<FAvaEaseCurvePreset>& InPreset, const FString& InNewName)
{
	return UAvaEaseCurveSubsystem::Get().RenamePreset(InPreset->Category, InPreset->Name, InNewName);
}

bool SAvaEaseCurvePresetComboBox::HandleBeginPresetMove(const TSharedPtr<FAvaEaseCurvePreset>& InPreset, const FString& InNewCategoryName)
{
	for (const TSharedPtr<SAvaEaseCurvePresetGroup>& GroupWidget : GroupWidgets)
	{
		if (!GroupWidget->GetCategoryName().Equals(InNewCategoryName))
		{
			GroupWidget->NotifyCanDrop(true);
		}
	}

	return true;
}

bool SAvaEaseCurvePresetComboBox::HandleEndPresetMove(const TSharedPtr<FAvaEaseCurvePreset>& InPreset, const FString& InNewCategoryName)
{
	for (const TSharedPtr<SAvaEaseCurvePresetGroup>& GroupWidget : GroupWidgets)
	{
		if (!GroupWidget->GetCategoryName().Equals(InNewCategoryName))
		{
			GroupWidget->NotifyCanDrop(false);
		}
	}

	if (!InPreset.IsValid() || InPreset->Category.Equals(InNewCategoryName))
	{
		return false;
	}

	if (!UAvaEaseCurveSubsystem::Get().ChangePresetCategory(InPreset, InNewCategoryName))
	{
		return false;
	}

	ReloadJsonPresets();

	return true;
}

bool SAvaEaseCurvePresetComboBox::HandlePresetClick(const TSharedPtr<FAvaEaseCurvePreset>& InPreset)
{
	SetSelectedItem(*InPreset);

	OnPresetChanged.ExecuteIfBound(InPreset);

	return true;
}

bool SAvaEaseCurvePresetComboBox::HandleSetQuickEase(const TSharedPtr<FAvaEaseCurvePreset>& InPreset)
{
	UAvaEaseCurveToolSettings* const EaseCurveToolSettings = GetMutableDefault<UAvaEaseCurveToolSettings>();
	EaseCurveToolSettings->SetQuickEaseTangents(InPreset->Tangents.ToJson());
	EaseCurveToolSettings->SaveConfig();

	OnQuickPresetChanged.ExecuteIfBound(InPreset);

	return true;
}

#undef LOCTEXT_NAMESPACE
