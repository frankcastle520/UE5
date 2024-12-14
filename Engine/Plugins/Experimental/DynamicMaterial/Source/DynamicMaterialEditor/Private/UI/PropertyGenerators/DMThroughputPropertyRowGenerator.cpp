// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/PropertyGenerators/DMThroughputPropertyRowGenerator.h"

#include "Components/DMMaterialStage.h"
#include "Components/DMMaterialStageInput.h"
#include "Components/DMMaterialStageThroughput.h"
#include "Components/DMMaterialValue.h"
#include "DynamicMaterialEditorModule.h"

const TSharedRef<FDMThroughputPropertyRowGenerator>& FDMThroughputPropertyRowGenerator::Get()
{
	static TSharedRef<FDMThroughputPropertyRowGenerator> Generator = MakeShared<FDMThroughputPropertyRowGenerator>();
	return Generator;
}

void FDMThroughputPropertyRowGenerator::AddComponentProperties(const TSharedRef<SDMMaterialComponentEditor>& InComponentEditorWidget,
	UDMMaterialComponent* InComponent, TArray<FDMPropertyHandle>& InOutPropertyRows,
	TSet<UDMMaterialComponent*>& InOutProcessedObjects)
{
	if (!IsValid(InComponent))
	{
		return;
	}

	if (InOutProcessedObjects.Contains(InComponent))
	{
		return;
	}

	UDMMaterialStageThroughput* Throughput = Cast<UDMMaterialStageThroughput>(InComponent);

	if (!Throughput)
	{
		return;
	}

	InOutProcessedObjects.Add(Throughput);

	const TArray<FName>& ThroughputProperties = Throughput->GetEditableProperties();

	for (const FName& ThroughputProperty : ThroughputProperties)
	{
		if (InComponent->IsPropertyVisible(ThroughputProperty))
		{
			AddPropertyEditRows(InComponentEditorWidget, InComponent, ThroughputProperty, InOutPropertyRows, InOutProcessedObjects);
		}
	}

	UDMMaterialStage* Stage = Throughput->GetStage();

	if (!Stage)
	{
		return;
	}

	InOutProcessedObjects.Add(Stage);

	static const FName StageInputsName = GET_MEMBER_NAME_CHECKED(UDMMaterialStage, Inputs);

	{
		const TArray<FDMMaterialStageConnector>& InputConnectors = Throughput->GetInputConnectors();
		const TArray<FDMMaterialStageConnection>& InputMap = Stage->GetInputConnectionMap();
		TArray<UDMMaterialStageInput*> Inputs = Stage->GetInputs();

		for (int32 InputIdx = 0; InputIdx < InputConnectors.Num(); ++InputIdx)
		{
			if (!Throughput->IsInputVisible(InputIdx) || !Throughput->CanChangeInput(InputIdx))
			{
				continue;
			}

			const int32 StartRow = InOutPropertyRows.Num();

			for (const FDMMaterialStageConnectorChannel& Channel : InputMap[InputIdx].Channels)
			{
				const int32 StageInputIdx = Channel.SourceIndex - FDMMaterialStageConnectorChannel::FIRST_STAGE_INPUT;

				if (Inputs.IsValidIndex(StageInputIdx))
				{
					FDynamicMaterialEditorModule::GeneratorComponentPropertyRows(InComponentEditorWidget, Inputs[StageInputIdx], InOutPropertyRows, InOutProcessedObjects);
				}
			}

			for (int32 PropertyRowIdx = StartRow; PropertyRowIdx < InOutPropertyRows.Num(); ++PropertyRowIdx)
			{
				FDMPropertyHandle& PropertyRow = InOutPropertyRows[PropertyRowIdx];

				if (PropertyRow.NameOverride.IsSet())
				{
					continue;
				}

				if (!PropertyRow.PropertyHandle.IsValid()
					|| !PropertyRow.PropertyHandle->GetProperty())
				{
					continue;
				}

				TArray<UObject*> Outers;
				PropertyRow.PropertyHandle->GetOuterObjects(Outers);

				if (Outers.IsEmpty() || !Outers[0]->IsA<UDMMaterialValue>())
				{
					continue;
				}

				if (!PropertyRow.NameOverride.IsSet() || PropertyRow.NameOverride.GetValue().IsEmpty())

				{
					if (!PropertyRow.ValueName.IsNone())
					{
						PropertyRow.NameOverride = FText::FromName(PropertyRow.ValueName);
					}
					else if (PropertyRow.PropertyHandle.IsValid())
					{
						PropertyRow.NameOverride = PropertyRow.PropertyHandle->GetPropertyDisplayName();
					}
					else
					{
						PropertyRow.NameOverride = InputConnectors[InputIdx].Name;
					}
				}
			}
		}
	}

	const TArray<FName>& StageProperties = Stage->GetEditableProperties();

	for (const FName& StageProperty : StageProperties)
	{
		if (StageProperty != StageInputsName)
		{
			AddPropertyEditRows(InComponentEditorWidget, Stage, StageProperty, InOutPropertyRows, InOutProcessedObjects);
			continue;
		}
	}
}
