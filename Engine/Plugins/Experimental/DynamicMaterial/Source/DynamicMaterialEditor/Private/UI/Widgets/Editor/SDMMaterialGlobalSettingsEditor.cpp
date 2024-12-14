// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Widgets/Editor/SDMMaterialGlobalSettingsEditor.h"

#include "DMEDefs.h"
#include "Model/DynamicMaterialModelBase.h"
#include "UI/PropertyGenerators/DMMaterialModelPropertyRowGenerator.h"
#include "UI/Widgets/SDMMaterialEditor.h"

void SDMMaterialGlobalSettingsEditor::PrivateRegisterAttributes(FSlateAttributeDescriptor::FInitializer&)
{
}

void SDMMaterialGlobalSettingsEditor::Construct(const FArguments& InArgs, const TSharedRef<SDMMaterialEditor>& InEditorWidget, UDynamicMaterialModelBase* InMaterialModelBase)
{
	SetCanTick(false);

	SDMObjectEditorWidgetBase::Construct(
		SDMObjectEditorWidgetBase::FArguments(), 
		InEditorWidget, 
		InMaterialModelBase
	);
}

UDynamicMaterialModelBase* SDMMaterialGlobalSettingsEditor::GetMaterialModelBase() const
{
	return Cast<UDynamicMaterialModelBase>(ObjectWeak.Get());
}

TArray<FDMPropertyHandle> SDMMaterialGlobalSettingsEditor::GetPropertyRows()
{
	TArray<FDMPropertyHandle> PropertyRows;

	FDMMaterialModelPropertyRowGenerator::AddMaterialModelProperties(
		SharedThis(this),
		GetMaterialModelBase(),
		PropertyRows
	);

	return PropertyRows;
}

void SDMMaterialGlobalSettingsEditor::OnUndo()
{
	if (TSharedPtr<SDMMaterialEditor> EditorWidget = GetEditorWidget())
	{
		EditorWidget->EditGlobalSettings(/* Force refresh */ true);
	}
}
