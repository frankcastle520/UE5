// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSimulationVisualization.h"
#include "Misc/LazySingleton.h"
#include "ChaosLog.h"

#define LOCTEXT_NAMESPACE "DataflowSimulationVisualization"

namespace UE::Dataflow
{
	FDataflowSimulationVisualizationRegistry& FDataflowSimulationVisualizationRegistry::GetInstance()
	{
		return TLazySingleton<FDataflowSimulationVisualizationRegistry>::Get();
	}

	void FDataflowSimulationVisualizationRegistry::TearDown()
	{
		return TLazySingleton<FDataflowSimulationVisualizationRegistry>::TearDown();
	}

	void FDataflowSimulationVisualizationRegistry::RegisterVisualization(TUniquePtr<IDataflowSimulationVisualization>&& Visualization)
	{
		const FName NewVisualizationName = Visualization->GetName();
		if (VisualizationMap.Contains(NewVisualizationName))
		{
			UE_LOG(LogChaos, Warning, TEXT("Dataflow simulation visualization registration conflicts with existing visualization: %s"), *NewVisualizationName.ToString());
		}
		else
		{
			VisualizationMap.Add(NewVisualizationName, MoveTemp(Visualization));
		}
	}

	void FDataflowSimulationVisualizationRegistry::DeregisterVisualization(const FName& VisualizationName)
	{
		if (!VisualizationMap.Contains(VisualizationName))
		{
			UE_LOG(LogChaos, Warning, TEXT("Dataflow visualization deregistration -- visualization not registered : %s"), *VisualizationName.ToString());
		}
		else
		{
			VisualizationMap.Remove(VisualizationName);
		}
	}

	const TMap<FName, TUniquePtr<IDataflowSimulationVisualization>>& FDataflowSimulationVisualizationRegistry::GetVisualizations() const
	{
		return VisualizationMap;
	}

	const IDataflowSimulationVisualization* FDataflowSimulationVisualizationRegistry::GetVisualization(const FName& Name) const
	{
		if (VisualizationMap.Contains(Name))
		{
			return VisualizationMap[Name].Get();
		}
		return nullptr;
	}

}

#undef LOCTEXT_NAMESPACE 

