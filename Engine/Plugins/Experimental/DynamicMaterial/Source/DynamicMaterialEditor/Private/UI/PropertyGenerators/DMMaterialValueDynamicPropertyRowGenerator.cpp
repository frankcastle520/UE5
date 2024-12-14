// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/PropertyGenerators/DMMaterialValueDynamicPropertyRowGenerator.h"

#include "Components/DMMaterialComponent.h"
#include "Components/DMMaterialValue.h"
#include "Components/DMMaterialValueDynamic.h"
#include "DMEDefs.h"
#include "IDetailPropertyRow.h"
#include "UI/Utils/DMWidgetStatics.h"
#include "UI/Widgets/Editor/SDMMaterialComponentEditor.h"

const TSharedRef<FDMMaterialValueDynamicPropertyRowGenerator>& FDMMaterialValueDynamicPropertyRowGenerator::Get()
{
	static TSharedRef<FDMMaterialValueDynamicPropertyRowGenerator> Generator = MakeShared<FDMMaterialValueDynamicPropertyRowGenerator>();
	return Generator;
}

void FDMMaterialValueDynamicPropertyRowGenerator::AddComponentProperties(const TSharedRef<SDMMaterialComponentEditor>& InComponentEditorWidget, UDMMaterialComponent* InComponent,
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

	UDMMaterialValueDynamic* ValueDynamic = Cast<UDMMaterialValueDynamic>(InComponent);

	if (!ValueDynamic)
	{
		return;
	}

	// The base material value class is abstract and not allowed.
	if (ValueDynamic->GetClass() == UDMMaterialValueDynamic::StaticClass())
	{
		return;
	}

	UDMMaterialValue* ParentValue = ValueDynamic->GetParentValue();

	if (!ParentValue)
	{
		return;
	}

	InOutProcessedObjects.Add(InComponent);

	if (ParentValue->AllowEditValue())
	{
		FDMPropertyHandle Handle = FDMWidgetStatics::Get().GetPropertyHandle(&*InComponentEditorWidget, ValueDynamic, UDMMaterialValue::ValueName);

		Handle.ResetToDefaultOverride = FResetToDefaultOverride::Create(
			FIsResetToDefaultVisible::CreateUObject(ValueDynamic, &UDMMaterialValueDynamic::CanResetToDefault),
			FResetToDefaultHandler::CreateUObject(ValueDynamic, &UDMMaterialValueDynamic::ResetToDefault)
		);

		Handle.bEnabled = true;

		InOutPropertyRows.Add(Handle);
	}

	const TArray<FName>& Properties = ParentValue->GetEditableProperties();
	const int32 StartRow = InOutPropertyRows.Num();

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

	const int32 EndRow = InOutPropertyRows.Num();

	for (int32 RowIndex = StartRow; RowIndex < EndRow; ++RowIndex)
	{
		InOutPropertyRows[RowIndex].bEnabled = false;
	}
}
