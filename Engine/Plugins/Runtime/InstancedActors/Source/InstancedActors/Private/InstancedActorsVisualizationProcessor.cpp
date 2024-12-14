// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstancedActorsVisualizationProcessor.h"
#include "MassStationaryISMSwitcherProcessor.h"


UInstancedActorsVisualizationProcessor::UInstancedActorsVisualizationProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	// This processor needs to be executed before UMassStationaryISMSwitcherProcessors since that's the processor
	// responsible for executing what UInstancedActorsVisualizationProcessor calculates.
	// Missing this dependency would result in client-side one-frame representation absence when switching
	// from actor representation back to ISM.
	ExecutionOrder.ExecuteBefore.Add(UMassStationaryISMSwitcherProcessor::StaticClass()->GetFName());

	UpdateParams.bTestCollisionAvailibilityForActorVisualization = false;
}

void UInstancedActorsVisualizationProcessor::ConfigureQueries() 
{
	Super::ConfigureQueries();

	EntityQuery.ClearTagRequirements(FMassTagBitSet(*FMassVisualizationProcessorTag::StaticStruct()));
	EntityQuery.AddTagRequirement<FInstancedActorsVisualizationProcessorTag>(EMassFragmentPresence::All);
}