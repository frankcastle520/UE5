// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Widgets/Editor/PropertySelectorLayouts/SDMMaterialPropertySelector_WrapBase.h"

#include "Components/DMMaterialProperty.h"
#include "DetailLayoutBuilder.h"
#include "DMDefs.h"
#include "Model/DynamicMaterialModelEditorOnlyData.h"
#include "UI/Widgets/SDMMaterialEditor.h"
#include "Utils/DMPrivate.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SDMMaterialPropertySelector_WrapBase"

void SDMMaterialPropertySelector_WrapBase::Construct(const FArguments& InArgs, const TSharedRef<SDMMaterialEditor> InEditorWidget)
{
	SDMMaterialPropertySelector::Construct(
		SDMMaterialPropertySelector::FArguments(),
		InEditorWidget
	);
}

TSharedRef<SWidget> SDMMaterialPropertySelector_WrapBase::CreateSlot_PropertyList()
{
	using namespace UE::DynamicMaterialEditor::Private;

	TSharedRef<SWrapBox> NewSlotList = SNew(SWrapBox)
		.InnerSlotPadding(FVector2D(6.f, 3.f))
		.UseAllottedSize(true);

	UDynamicMaterialModelEditorOnlyData* EditorOnlyData = GetEditorOnlyData();

	if (!EditorOnlyData)
	{
		return NewSlotList;
	}

	const FMargin Padding = FMargin(0.f, 1.f);

	NewSlotList->AddSlot()
		.Padding(Padding)
		[
			CreateSlot_SelectButton(FDMMaterialEditorPage::GlobalSettings)
		];

	NewSlotList->AddSlot()
		.Padding(Padding)
		[
			CreateSlot_SelectButton(FDMMaterialEditorPage::Properties)
		];

	for (const TPair<EDMMaterialPropertyType, UDMMaterialProperty*>& PropertyPair : EditorOnlyData->GetMaterialProperties())
	{
		if (!PropertyPair.Value || !PropertyPair.Value->IsEnabled() || IsCustomMaterialProperty(PropertyPair.Key)
			|| !PropertyPair.Value->IsValidForModel(*EditorOnlyData)
			|| !EditorOnlyData->GetSlotForMaterialProperty(PropertyPair.Value->GetMaterialProperty()))
		{
			continue;
		}

		NewSlotList->AddSlot()
			.Padding(Padding)
			[
				CreateSlot_SelectButton({EDMMaterialEditorMode::EditSlot, PropertyPair.Key})
			];
	}

	return NewSlotList;
}

TSharedRef<SWidget> SDMMaterialPropertySelector_WrapBase::CreateSlot_SelectButton(const FDMMaterialEditorPage& InPage)
{
	const FText ButtonText = GetSelectButtonText(InPage, /* Short Name */ false);
	const FText ToolTip = GetButtonToolTip(InPage);

	return SNew(SCheckBox)
		.Style(FAppStyle::Get(), "DetailsView.SectionButton")
		.HAlign(EHorizontalAlignment::HAlign_Center)
		.Padding(0.f)
		.IsEnabled(this, &SDMMaterialPropertySelector_WrapBase::GetPropertySelectEnabled, InPage)
		.IsChecked(this, &SDMMaterialPropertySelector_WrapBase::GetPropertySelectState, InPage)
		.OnCheckStateChanged(this, &SDMMaterialPropertySelector_WrapBase::OnPropertySelectStateChanged, InPage)
		.ToolTipText(ToolTip)
		.Content()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("FilterBar.FilterImage"))
				.ColorAndOpacity(this, &SDMMaterialPropertySelector_WrapBase::GetPropertySelectButtonChipColor, InPage)
				.DesiredSizeOverride(FVector2D(8, 17))
			]
			+SHorizontalBox::Slot()
			.Padding(4.f, 4.f)
			.VAlign(VAlign_Center)
			.FillWidth(1.f)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(ButtonText)
				.Justification(ETextJustify::Center)
			]
		];
}

#undef LOCTEXT_NAMESPACE
