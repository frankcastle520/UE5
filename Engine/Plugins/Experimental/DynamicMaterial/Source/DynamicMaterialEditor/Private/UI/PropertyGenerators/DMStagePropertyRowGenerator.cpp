// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/PropertyGenerators/DMStagePropertyRowGenerator.h"

#include "Components/DMMaterialComponent.h"
#include "Components/DMMaterialLayer.h"
#include "Components/DMMaterialStage.h"
#include "Components/DMMaterialStageSource.h"
#include "DetailLayoutBuilder.h"
#include "DMEDefs.h"
#include "DynamicMaterialEditorModule.h"
#include "Internationalization/Text.h"
#include "Internationalization/Text.h"
#include "Model/DynamicMaterialModel.h"
#include "Model/DynamicMaterialModelBase.h"
#include "UI/Menus/DMMaterialStageSourceMenus.h"
#include "UI/Widgets/Editor/SDMMaterialComponentEditor.h"
#include "UI/Widgets/Editor/SDMMaterialSlotEditor.h"
#include "UI/Widgets/Editor/SlotEditor/SDMMaterialSlotLayerItem.h"
#include "UI/Widgets/Editor/SlotEditor/SDMMaterialSlotLayerView.h"
#include "UI/Widgets/Editor/SlotEditor/SDMMaterialStage.h"
#include "UI/Widgets/SDMMaterialEditor.h"
#include "Widgets/Input/SComboButton.h"

#define LOCTEXT_NAMESPACE "DMStagePropertyRowGenerator"

const TSharedRef<FDMStagePropertyRowGenerator>& FDMStagePropertyRowGenerator::Get()
{
	static TSharedRef<FDMStagePropertyRowGenerator> Generator = MakeShared<FDMStagePropertyRowGenerator>();
	return Generator;
}

void FDMStagePropertyRowGenerator::AddComponentProperties(const TSharedRef<SDMMaterialComponentEditor>& InComponentEditorWidget, UDMMaterialComponent* InComponent,
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

	UDMMaterialStage* Stage = Cast<UDMMaterialStage>(InComponent);

	if (!Stage)
	{
		return;
	}

	UDMMaterialStageSource* Source = Stage->GetSource();

	if (!Source)
	{
		return;
	}

	FDynamicMaterialEditorModule::GeneratorComponentPropertyRows(InComponentEditorWidget, Source, InOutPropertyRows, InOutProcessedObjects);
	FDMComponentPropertyRowGenerator::AddComponentProperties(InComponentEditorWidget, Stage, InOutPropertyRows, InOutProcessedObjects);
}

#undef LOCTEXT_NAMESPACE
