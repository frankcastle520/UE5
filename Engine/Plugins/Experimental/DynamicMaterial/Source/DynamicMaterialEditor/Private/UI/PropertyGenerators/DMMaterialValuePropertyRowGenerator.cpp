// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/PropertyGenerators/DMMaterialValuePropertyRowGenerator.h"

#include "Components/DMMaterialComponent.h"
#include "Components/DMMaterialValue.h"
#include "Components/DMMaterialValueDynamic.h"
#include "DynamicMaterialEditorModule.h"
#include "IDetailPropertyRow.h"
#include "Model/DynamicMaterialModelBase.h"
#include "Model/DynamicMaterialModelDynamic.h"
#include "UI/Utils/DMWidgetStatics.h"
#include "UI/Widgets/Editor/SDMMaterialComponentEditor.h"
#include "UI/Widgets/SDMMaterialEditor.h"

const TSharedRef<FDMMaterialValuePropertyRowGenerator>& FDMMaterialValuePropertyRowGenerator::Get()
{
	static TSharedRef<FDMMaterialValuePropertyRowGenerator> Generator = MakeShared<FDMMaterialValuePropertyRowGenerator>();
	return Generator;
}

void FDMMaterialValuePropertyRowGenerator::AddComponentProperties(const TSharedRef<SDMMaterialComponentEditor>& InComponentEditorWidget, UDMMaterialComponent* InComponent,
	TArray<FDMPropertyHandle>& InOutPropertyRows, TSet<UDMMaterialComponent*>& InOutProcessedObjects)
{
	if (!IsValid(InComponent))
	{
		return;
	}

	if (InOutProcessedObjects.Contains(InComponent))
	{
		return;
	}

	UDMMaterialValue* Value = Cast<UDMMaterialValue>(InComponent);

	if (!Value)
	{
		return;
	}

	// The base material value class is abstract and not allowed.
	if (Value->GetClass() == UDMMaterialValue::StaticClass())
	{
		return;
	}

	InOutProcessedObjects.Add(InComponent);

	if (TSharedPtr<SDMMaterialEditor> EditorWidget = InComponentEditorWidget->GetEditorWidget())
	{
		if (UDynamicMaterialModelBase* MaterialModelBase = EditorWidget->GetMaterialModelBase())
		{
			if (UDynamicMaterialModelDynamic* MaterialModelDynamic = Cast<UDynamicMaterialModelDynamic>(MaterialModelBase))
			{
				if (UDMMaterialComponentDynamic* ComponentDynamic = MaterialModelDynamic->GetComponentDynamic(Value->GetFName()))
				{
					FDynamicMaterialEditorModule::Get().GeneratorComponentPropertyRows(InComponentEditorWidget, ComponentDynamic, InOutPropertyRows, InOutProcessedObjects);
				}

				return;
			}
		}
	}

	if (Value->AllowEditValue())
	{
		FDMPropertyHandle Handle = FDMWidgetStatics::Get().GetPropertyHandle(&*InComponentEditorWidget, Value, UDMMaterialValue::ValueName);

		Handle.ResetToDefaultOverride = FResetToDefaultOverride::Create(
			FIsResetToDefaultVisible::CreateUObject(Value, &UDMMaterialValue::CanResetToDefault),
			FResetToDefaultHandler::CreateUObject(Value, &UDMMaterialValue::ResetToDefault)
		);

		Handle.bEnabled = true;

		InOutPropertyRows.Add(Handle);
	}

	const TArray<FName>& Properties = Value->GetEditableProperties();

	for (const FName& Property : Properties)
	{
		if (Property == UDMMaterialValue::ValueName)
		{
			continue;
		}
			
		if (InComponent->IsPropertyVisible(Property))
		{
			AddPropertyEditRows(InComponentEditorWidget, InComponent, Property, InOutPropertyRows, InOutProcessedObjects);
		}
	}
}
