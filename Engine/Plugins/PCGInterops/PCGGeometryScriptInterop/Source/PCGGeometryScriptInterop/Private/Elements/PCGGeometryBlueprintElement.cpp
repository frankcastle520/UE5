// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGGeometryBlueprintElement.h"

#include "Data/PCGDynamicMeshData.h"
#include "Elements/PCGDynamicMeshBaseElement.h"

#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"

#define LOCTEXT_NAMESPACE "PCGGeometryBlueprintElement"

UPCGGeometryBlueprintElement::UPCGGeometryBlueprintElement()
	: UPCGBlueprintElement()
{
	// Setup the element for basic dynamic mesh processing
	bIsCacheable = false;
	bHasDefaultInPin = false;
	bHasDefaultOutPin = false;

	CustomInputPins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::DynamicMesh).SetRequiredPin();
	CustomOutputPins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh);
}

void UPCGGeometryBlueprintElement::ExecuteWithContext_Implementation(FPCGContext& InContext, const FPCGDataCollection& Input, FPCGDataCollection& Output)
{
	// Verify that we are in the right setup
	const bool bHasASingleInputPin = (bHasDefaultInPin && CustomInputPins.IsEmpty()) || (!bHasDefaultInPin && CustomInputPins.Num() == 1);
	const bool bHasASingleOutputPin = (bHasDefaultOutPin && CustomOutputPins.IsEmpty()) || (!bHasDefaultOutPin && CustomOutputPins.Num() == 1);

#if WITH_EDITOR
	const UBlueprint* Blueprint = Cast<UBlueprint>(GetClass()->ClassGeneratedBy);
	auto IsFunctionOverridden = [Blueprint](const FName FunctionName)
	{
		return Blueprint && Blueprint->FunctionGraphs.ContainsByPredicate([FunctionName](const TObjectPtr<UEdGraph>& Graph) { return Graph->GetFName() == FunctionName;});
	};
	
	const bool bIsExecuteOverridden = IsFunctionOverridden(GET_FUNCTION_NAME_CHECKED(UPCGBlueprintElement, Execute));
	const bool bIsProcessDynMeshOverridden = IsFunctionOverridden(GET_FUNCTION_NAME_CHECKED(UPCGGeometryBlueprintElement, ProcessDynamicMesh));
#else
	// We can't know in runtime build as the functions are compiled.
	const bool bIsExecuteOverridden = false;
	const bool bIsProcessDynMeshOverridden = false;
#endif // WITH_EDITOR
	
	if (!bHasASingleInputPin || !bHasASingleOutputPin || bIsExecuteOverridden)
	{
		// Make sure to throw a warning if we are in this case and Process Dynamic Mesh is overridden to warn the user their function won't be called
		if (bIsProcessDynMeshOverridden)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("DynMeshOverridenButNotCalled", "Process Dynamic Mesh was overridden, but we don't have the expected setup (single input and output pin)"
				" or Execute is also overriden. Process Dynamic Mesh won't be called."), &GetContext());
		}
		
		// If we have not the right number of pins, or Execute is overriden, just call the parent function.
		UPCGBlueprintElement::ExecuteWithContext_Implementation(InContext, Input, Output);
		return;
	}

	const FName InputPinLabel = bHasDefaultInPin ? PCGPinConstants::DefaultInputLabel : CustomInputPins[0].Label;
	const FName OutputPinLabel = bHasDefaultOutPin ? PCGPinConstants::DefaultOutputLabel : CustomOutputPins[0].Label;
	
	for (const FPCGTaggedData& InputData : Input.GetInputsByPin(InputPinLabel))
	{
		if (!Cast<const UPCGDynamicMeshData>(InputData.Data))
		{
			continue;
		}

		UPCGDynamicMeshData* ProcessingMesh = CopyOrStealInputData(InputData);
		TArray<FString> OutTags;

		ProcessDynamicMesh(ProcessingMesh->GetMutableDynamicMesh(), OutTags);
		FPCGTaggedData& OutputData = Output.TaggedData.Emplace_GetRef(InputData);
		OutputData.Tags.Append(OutTags);
		OutputData.Data = ProcessingMesh;
		OutputData.Pin = OutputPinLabel;
	}
}
UPCGDynamicMeshData* UPCGGeometryBlueprintElement::CopyOrStealInputData(const FPCGTaggedData& InTaggedData) const
{
	if (bIsCacheable || IsCacheableOverride())
	{
		// Verification that the user didn't change default settings
		PCGLog::LogWarningOnGraph(LOCTEXT("SettingsDifferent", "In PCG Geometry Blueprint Element, the default settings were changed (not cacheable and not verifying outputs used multiple times)."
			"Use the normal BP element if you want this behavior. Will always copy and never steal."), &GetContext());
		const UPCGDynamicMeshData* InData = Cast<const UPCGDynamicMeshData>(InTaggedData.Data);
		return InData ? CastChecked<UPCGDynamicMeshData>(InData->DuplicateData(&GetContext())) : nullptr;
	}
	
	return IPCGDynamicMeshBaseElement::CopyOrSteal(InTaggedData, &GetContext());
}

#undef LOCTEXT_NAMESPACE
