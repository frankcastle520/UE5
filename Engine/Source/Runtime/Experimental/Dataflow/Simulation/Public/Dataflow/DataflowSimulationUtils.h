// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dataflow/DataflowObjectInterface.h"
#include "Dataflow/DataflowSimulationContext.h"

class UDataflow;

namespace UE::Dataflow
{
	/** Evaluate the simulation graph given a simulation context and a simulation flag */
	DATAFLOWSIMULATION_API void EvaluateSimulationGraph(const TObjectPtr<UDataflow>& SimulationGraph,
		const TSharedPtr<UE::Dataflow::FDataflowSimulationContext>& SimulationContext, const float DeltaTime, const float SimulationTime);
};


