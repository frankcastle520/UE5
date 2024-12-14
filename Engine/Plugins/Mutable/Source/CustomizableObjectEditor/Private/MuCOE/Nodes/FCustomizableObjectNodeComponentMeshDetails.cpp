// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableObjectNodeComponentMeshDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "MuCOE/Nodes/CustomizableObjectNodeComponentMesh.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"


#define LOCTEXT_NAMESPACE "CustomizableObjectDetails"


TSharedRef<IDetailCustomization> FCustomizableObjectNodeComponentMeshDetails::MakeInstance()
{
	return MakeShareable(new FCustomizableObjectNodeComponentMeshDetails);
}


void FCustomizableObjectNodeComponentMeshDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FCustomizableObjectNodeDetails::CustomizeDetails(DetailBuilder);

	
	const IDetailsView* DetailsView = DetailBuilder.GetDetailsView();
	if (DetailsView->GetSelectedObjects().IsEmpty())
	{
		return;
	}

	NodeComponentMesh = Cast<UCustomizableObjectNodeComponentMesh>(DetailsView->GetSelectedObjects()[0].Get());
	if (!NodeComponentMesh.IsValid())
	{
		return;
	}

	// LOD Picker
	IDetailCategoryBuilder& LODCustomSettings = DetailBuilder.EditCategory("LOD Custom Settings");

	LODCustomSettings.AddCustomRow((LOCTEXT("LODCustomModeSelect", "Select LOD")))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("LODCustomSettingsSelectTitle", "LOD"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.ToolTipText(LOCTEXT("LODCustomSettingsSelectTooltip", "Select the component's LOD to edit."))
	]
	.ValueContent()
	.VAlign(VAlign_Center)
	[
		OnGenerateLODComboBoxForPicker()
	];

	DetailBuilder.HideProperty("LODReductionSettings");

	FName BonesToRemovePropertyPath = FName("LODReductionSettings[" + FString::FromInt(NodeComponentMesh.Get()->SelectedLOD) + "].BonesToRemove");
	TSharedRef<IPropertyHandle> BonesToRemoveProperty = DetailBuilder.GetProperty(BonesToRemovePropertyPath);
	
	// Bones to remove widget
	LODCustomSettings.AddProperty(BonesToRemoveProperty);
}


void FCustomizableObjectNodeComponentMeshDetails::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	FCustomizableObjectNodeDetails::CustomizeDetails(DetailBuilder);

	WeakDetailsBuilder = DetailBuilder;
}


TSharedRef<SWidget> FCustomizableObjectNodeComponentMeshDetails::OnGenerateLODComboBoxForPicker()
{
	return SNew(SComboButton)
		.OnGetMenuContent(this, &FCustomizableObjectNodeComponentMeshDetails::OnGenerateLODMenuForPicker)
		.VAlign(VAlign_Center)
		.ButtonContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(this, &FCustomizableObjectNodeComponentMeshDetails::GetCurrentLODName)
		];
}


TSharedRef<SWidget> FCustomizableObjectNodeComponentMeshDetails::OnGenerateLODMenuForPicker()
{
	if (NodeComponentMesh.IsValid())
	{
		int32 NumLODs = NodeComponentMesh->NumLODs;
		FMenuBuilder MenuBuilder(true, NULL);

		for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
		{
			FText LODString = FText::FromString((TEXT("LOD ") + FString::FromInt(LODIndex)));
			FUIAction Action(FExecuteAction::CreateSP(this, &FCustomizableObjectNodeComponentMeshDetails::OnSelectedLODChanged, LODIndex));
			MenuBuilder.AddMenuEntry(LODString, FText::GetEmpty(), FSlateIcon(), Action);
		}

		return MenuBuilder.MakeWidget();
	}

	return SNullWidget::NullWidget;
}


void FCustomizableObjectNodeComponentMeshDetails::OnSelectedLODChanged(int32 NewLODIndex)
{
	NodeComponentMesh.Get()->SelectedLOD = NewLODIndex;

	WeakDetailsBuilder.Pin()->ForceRefreshDetails();
}


FText FCustomizableObjectNodeComponentMeshDetails::GetCurrentLODName() const
{
	FText LODText;

	if (NodeComponentMesh.IsValid())
	{
		LODText = FText::FromString(FString(TEXT("LOD ")) + FString::FromInt(NodeComponentMesh.Get()->SelectedLOD));
	}

	return LODText;
}

#undef LOCTEXT_NAMESPACE

