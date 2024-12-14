// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSimulationInterface.h"
#include "Dataflow/DataflowSimulationManager.h"
#include "Engine/World.h"
	
void IDataflowSimulationInterface::RegisterManagerInterface(const TObjectPtr<UWorld>& SimulationWorld)
{
	if(SimulationWorld)
	{
		if (UDataflowSimulationManager* SimulationManager = SimulationWorld->GetSubsystem<UDataflowSimulationManager>())
		{
			SimulationManager->AddSimulationInterface(this);
		}
	}
}

bool IDataflowSimulationInterface::IsInterfaceRegistered(const TObjectPtr<UWorld>& SimulationWorld) const
{
	if(SimulationWorld)
	{
		if (const UDataflowSimulationManager* SimulationManager = SimulationWorld->GetSubsystem<UDataflowSimulationManager>())
		{
			return SimulationManager->HasSimulationInterface(this);
		}
	}
	return false;
}

void IDataflowSimulationInterface::UnregisterManagerInterface(const TObjectPtr<UWorld>& SimulationWorld) const
{
	if(SimulationWorld)
	{
		if (UDataflowSimulationManager* SimulationManager = SimulationWorld->GetSubsystem<UDataflowSimulationManager>())
		{
			SimulationManager->RemoveSimulationInterface(this); 
		}
	}
}
