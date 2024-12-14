// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/PropertyGenerators/DMComponentPropertyRowGenerator.h"

#include "Components/DMMaterialComponent.h"
#include "DynamicMaterialEditorModule.h"
#include "Model/DynamicMaterialModelDynamic.h"
#include "Runtime/Launch/Resources/Version.h"
#include "UI/Utils/DMWidgetStatics.h"
#include "UI/Widgets/Editor/SDMMaterialComponentEditor.h"
#include "UI/Widgets/SDMMaterialEditor.h"

const TSharedRef<FDMComponentPropertyRowGenerator>& FDMComponentPropertyRowGenerator::Get()
{
	static TSharedRef<FDMComponentPropertyRowGenerator> Generator = MakeShared<FDMComponentPropertyRowGenerator>();
	return Generator;
}

void FDMComponentPropertyRowGenerator::AddComponentProperties(const TSharedRef<SDMMaterialComponentEditor>& InComponentEditorWidget, UDMMaterialComponent* InComponent,
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

	InOutProcessedObjects.Add(InComponent);

	const TArray<FName>& Properties = InComponent->GetEditableProperties();

	for (const FName& Property : Properties)
	{
		if (InComponent->IsPropertyVisible(Property))
		{
			AddPropertyEditRows(InComponentEditorWidget, InComponent, Property, InOutPropertyRows, InOutProcessedObjects);
		}
	}
}

void FDMComponentPropertyRowGenerator::AddPropertyEditRows(const TSharedRef<SDMMaterialComponentEditor>& InComponentEditorWidget, UDMMaterialComponent* InComponent, 
	const FName& InProperty, TArray<FDMPropertyHandle>& InOutPropertyRows, TSet<UDMMaterialComponent*>& InOutProcessedObjects)
{
	if (!IsValid(InComponent))
	{
		return;
	}

	FProperty* Property = InComponent->GetClass()->FindPropertyByName(InProperty);

	if (!Property)
	{
		return;
	}

	void* MemoryPtr = Property->ContainerPtrToValuePtr<void>(InComponent);

	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper ArrayHelper(ArrayProperty, MemoryPtr);

		for (int32 Idx = 0; Idx < ArrayHelper.Num(); ++Idx)
		{
#if ENGINE_MAJOR_VERSION < 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION <= 2)
			void* ElemPtr = ArrayHelper.GetRawPtr(Idx);
#else
			void* ElemPtr = ArrayHelper.GetElementPtr(Idx);
#endif
			AddPropertyEditRows(InComponentEditorWidget, InComponent, ArrayProperty->Inner, ElemPtr, InOutPropertyRows, InOutProcessedObjects);
		}
	}
	else
	{
		AddPropertyEditRows(InComponentEditorWidget, InComponent, Property, MemoryPtr, InOutPropertyRows, InOutProcessedObjects);
	}
}

void FDMComponentPropertyRowGenerator::AddPropertyEditRows(const TSharedRef<SDMMaterialComponentEditor>& InComponentEditorWidget, UDMMaterialComponent* InComponent,
	FProperty* InProperty, void* MemoryPtr, TArray<FDMPropertyHandle>& InOutPropertyRows, TSet<UDMMaterialComponent*>& InOutProcessedObjects)
{
	if (InProperty->IsA<FArrayProperty>())
	{
		return;
	}

	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(InProperty))
	{
		if (ObjectProperty->PropertyClass->IsChildOf(UDMMaterialComponent::StaticClass()))
		{
			UObject** ValuePtr = static_cast<UObject**>(MemoryPtr);
			UObject* Value = *ValuePtr;
			UDMMaterialComponent* ComponentValue = Cast<UDMMaterialComponent>(Value);
			FDynamicMaterialEditorModule::GeneratorComponentPropertyRows(InComponentEditorWidget, ComponentValue, InOutPropertyRows, InOutProcessedObjects);
			return;
		}
	}

	FDMPropertyHandle& Handle = InOutPropertyRows.Add_GetRef(FDMWidgetStatics::Get().GetPropertyHandle(&*InComponentEditorWidget, InComponent, InProperty->GetFName()));
	Handle.bEnabled = !IsDynamic(InComponentEditorWidget);
}

bool FDMComponentPropertyRowGenerator::AllowKeyframeButton(UDMMaterialComponent* InComponent, FProperty* InProperty)
{
	return false;
}

bool FDMComponentPropertyRowGenerator::IsDynamic(const TSharedRef<SDMMaterialComponentEditor>& InComponentEditorWidget)
{
	if (TSharedPtr<SDMMaterialEditor> EditorWidget = InComponentEditorWidget->GetEditorWidget())
	{
		if (UDynamicMaterialModelBase* MaterialModelBase = EditorWidget->GetMaterialModelBase())
		{
			return !!Cast<UDynamicMaterialModelDynamic>(MaterialModelBase);
		}
	}

	return false;
}
