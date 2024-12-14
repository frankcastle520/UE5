// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils/DMMaterialStageFunctionLibrary.h"

#include "Components/DMMaterialStage.h"
#include "Components/DMMaterialStageThroughput.h"
#include "Components/DMMaterialSubStage.h"
#include "Components/DMRenderTargetRenderer.h"
#include "Components/MaterialStageExpressions/DMMSETextureSample.h"
#include "Components/MaterialStageInputs/DMMSIExpression.h"
#include "Components/MaterialStageInputs/DMMSIValue.h"
#include "Components/MaterialValues/DMMaterialValueRenderTarget.h"
#include "Components/MaterialValues/DMMaterialValueTexture.h"

UDMMaterialStageInputValue* UDMMaterialStageFunctionLibrary::FindDefaultStageOpacityInputValue(UDMMaterialStage* InStage)
{
	if (!IsValid(InStage))
	{
		return nullptr;
	}

	int32 OpacityInputIndex = -1;

	if (UDMMaterialStageThroughput* const ThroughputSource = Cast<UDMMaterialStageThroughput>(InStage->GetSource()))
	{
		const TArray<FDMMaterialStageConnector>& Connectors = ThroughputSource->GetInputConnectors();
		for (const FDMMaterialStageConnector& Connector : Connectors)
		{
			if (Connector.Name.ToString() == TEXT("Opacity"))
			{
				OpacityInputIndex = Connector.Index;
			}
		}
	}

	const TArray<UDMMaterialStageInput*>& StageInputs = InStage->GetInputs();
	const TArray<FDMMaterialStageConnection>& InputConnectionMap = InStage->GetInputConnectionMap();

	if (InputConnectionMap.IsValidIndex(OpacityInputIndex) && !InputConnectionMap[OpacityInputIndex].Channels.IsEmpty())
	{
		if (InputConnectionMap[OpacityInputIndex].Channels[0].SourceIndex >= FDMMaterialStageConnectorChannel::FIRST_STAGE_INPUT)
		{
			const int32 StageInputIndex = InputConnectionMap[OpacityInputIndex].Channels[0].SourceIndex - FDMMaterialStageConnectorChannel::FIRST_STAGE_INPUT;

			return Cast<UDMMaterialStageInputValue>(StageInputs[StageInputIndex]);
		}
	}

	return nullptr;
}

void UDMMaterialStageFunctionLibrary::SetStageInputToRenderer(UDMMaterialStage* InStage, TSubclassOf<UDMRenderTargetRenderer> InRendererClass, int32 InInputIndex)
{
	UDMMaterialStageInputExpression* InputExpression = UDMMaterialStageInputExpression::ChangeStageInput_Expression(
		InStage,
		UDMMaterialStageExpressionTextureSample::StaticClass(),
		InInputIndex,
		FDMMaterialStageConnectorChannel::WHOLE_CHANNEL,
		0,
		FDMMaterialStageConnectorChannel::THREE_CHANNELS
	);

	if (!ensure(InputExpression))
	{
		return;
	}

	UDMMaterialSubStage* SubStage = InputExpression->GetSubStage();

	if (!ensure(SubStage))
	{
		return;
	}

	UDMMaterialStageInputValue* InputValue = UDMMaterialStageInputValue::ChangeStageInput_NewLocalValue(
		SubStage,
		0,
		FDMMaterialStageConnectorChannel::WHOLE_CHANNEL,
		UDMMaterialValueRenderTarget::StaticClass(),
		FDMMaterialStageConnectorChannel::WHOLE_CHANNEL
	);

	if (!ensure(InputValue))
	{
		return;
	}

	UDMMaterialValueRenderTarget* RenderTargetValue = Cast<UDMMaterialValueRenderTarget>(InputValue->GetValue());

	if (!RenderTargetValue)
	{
		return;
	}

	UDMRenderTargetRenderer::CreateRenderTargetRenderer(InRendererClass, RenderTargetValue);
}
