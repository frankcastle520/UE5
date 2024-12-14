// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSimulationNodes.h"
#include "Dataflow/DataflowSimulationManager.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Dataflow/DataflowNodeColorsRegistry.h"
#include "Dataflow/Interfaces/DataflowPhysicsSolver.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowSimulationNodes)

namespace UE::Dataflow
{
	void RegisterDataflowSimulationNodes()
	{
		// Common nodes
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetSimulationTimeDataflowNode);
		
		// Solvers nodes
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetPhysicsSolversDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FAdvancePhysicsSolversDataflowNode);

		// Proxies nodes
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FFilterSimulationProxiesDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FSimulationProxiesTerminalDataflowNode);
		
		static constexpr FLinearColor CDefaultNodeBodyTintColor = FLinearColor(0.f, 0.f, 0.0f, 0.5f);
		
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("Terminal", FLinearColor(1.0f, 0.0f, 0.0f), CDefaultNodeBodyTintColor);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("Setup", FLinearColor(1.0f, 1.0f, 0.0f), CDefaultNodeBodyTintColor);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("Physics", FLinearColor(0.0f, 1.0f, 0.0f), CDefaultNodeBodyTintColor);
	}
}

void FGetSimulationTimeDataflowNode::EvaluateSimulation(UE::Dataflow::FDataflowSimulationContext& SimulationContext, const FDataflowOutput* Output) const
{
	SetValue(SimulationContext, FDataflowSimulationTime(SimulationContext.GetDeltaTime(), SimulationContext.GetSimulationTime()), &SimulationTime);
}

void FAdvancePhysicsSolversDataflowNode::EvaluateSimulation(UE::Dataflow::FDataflowSimulationContext& SimulationContext, const FDataflowOutput* Output) const
{
	const TArray<FDataflowSimulationProperty> SolverProperties = GetValue(SimulationContext, &PhysicsSolvers);
	const float SimulationDeltaTime = GetValue(SimulationContext, &SimulationTime).DeltaTime;
	
	if(!SolverProperties.IsEmpty())
	{
		for(const FDataflowSimulationProperty& SolverProperty : SolverProperties)
		{
			if(SolverProperty.SimulationProxy)
			{
				if(FDataflowPhysicsSolverProxy* SolverProxy = SolverProperty.SimulationProxy->AsType<FDataflowPhysicsSolverProxy>())
				{
					SolverProxy->AdvanceSolverDatas(SimulationDeltaTime);
				}
			}
		}
	}
	SetValue(SimulationContext, SolverProperties, &PhysicsSolvers);
}

void FGetPhysicsSolversDataflowNode::EvaluateSimulation(UE::Dataflow::FDataflowSimulationContext& SimulationContext, const FDataflowOutput* Output) const
{
	TArray<FDataflowSimulationProxy*> SimulationProxies;
	SimulationContext.GetSimulationProxies(FDataflowPhysicsSolverProxy::StaticStruct()->GetName(), SimulationGroups, SimulationProxies);

	TArray<FDataflowSimulationProperty> SolverProperties;
	for(FDataflowSimulationProxy* SimulationProxy : SimulationProxies)
	{
		SolverProperties.Add({SimulationProxy});
	}
	
	SetValue(SimulationContext, SolverProperties, &PhysicsSolvers);
}

void FFilterSimulationProxiesDataflowNode::EvaluateSimulation(UE::Dataflow::FDataflowSimulationContext& SimulationContext, const FDataflowOutput* Output) const
{
	const TArray<FDataflowSimulationProperty> SimulationProperties = GetValue(SimulationContext, &SimulationProxies);

	TArray<FDataflowSimulationProperty> FilteredProperties;
	if(!SimulationProperties.IsEmpty())
	{
		TBitArray<> GroupBits;
		SimulationContext.BuildGroupBits(SimulationGroups, GroupBits);

		for(const FDataflowSimulationProperty& SimulationProperty : SimulationProperties)
		{
			if(SimulationProperty.SimulationProxy && SimulationProperty.SimulationProxy->HasGroupBit(GroupBits))
			{
				FilteredProperties.Add({SimulationProperty.SimulationProxy});
			}
		}
	}
	
	SetValue(SimulationContext, FilteredProperties, &FilteredProxies);
}

void FSimulationProxiesTerminalDataflowNode::EvaluateSimulation(UE::Dataflow::FDataflowSimulationContext& SimulationContext, const FDataflowOutput* Output) const
{
	const TArray<FDataflowSimulationProperty> SolverProperties = GetValue(SimulationContext, &SimulationProxies);
}
