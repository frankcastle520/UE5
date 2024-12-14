// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowObject.h"
#include "UObject/Interface.h"

#include "DataflowSimulationInterface.generated.h"

struct FDataflowSimulationProxy;

/**
 * Dataflow simulation asset (should be in the interface children)
 */
USTRUCT(BlueprintType)
struct DATAFLOWSIMULATION_API FDataflowSimulationAsset
{
	GENERATED_BODY()

	/* Simulation dataflow asset used to advance in time on Pt */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	TObjectPtr<UDataflow> DataflowAsset;

	/* Simulation groups used to filter within the simulation nodes*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	TSet<FString> SimulationGroups;
};

UINTERFACE()
class DATAFLOWSIMULATION_API UDataflowSimulationInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Dataflow simulation interface to send/receive datas (GT <-> PT)
 */
class DATAFLOWSIMULATION_API IDataflowSimulationInterface
{
	GENERATED_BODY()
	
public:
	IDataflowSimulationInterface() {}

	/** Get the dataflow simulation asset */
	virtual FDataflowSimulationAsset& GetSimulationAsset() = 0;

	/** Get the const dataflow simulation asset */
	virtual const FDataflowSimulationAsset& GetSimulationAsset() const = 0;
	
	/** Build the simulation proxy */
	virtual void BuildSimulationProxy() = 0;
	
	/** Reset the simulation proxy */
	virtual void ResetSimulationProxy() = 0;

	/** Get the const simulation proxy */
	virtual const FDataflowSimulationProxy* GetSimulationProxy() const = 0;

	/** Get the simulation proxy */
	virtual FDataflowSimulationProxy* GetSimulationProxy() = 0;

	/** Get the simulation name */
	virtual FString GetSimulationName() const = 0;

	/** Preprocess data before simulation */
	virtual void PreProcessSimulation(const float DeltaTime) {};

	/** Write data to be sent to the simulation proxy */
	virtual void WriteToSimulation(const float DeltaTime, const bool bAsyncTask) {};

	/** Read data received from the simulation proxy */
	virtual void ReadFromSimulation(const float DeltaTime, const bool bAsyncTask) {};

	/** Postprocess data after simulation */
	virtual void PostProcessSimulation(const float DeltaTime) {};
	
	/** Get the simulation type */
    virtual FString GetSimulationType() const {return TEXT("");};

	/** Register simulation interface solver to manager */
	void RegisterManagerInterface(const TObjectPtr<UWorld>& SimulationWorld);

	/** Unregister simulation interface from the manager */
	void UnregisterManagerInterface(const TObjectPtr<UWorld>& SimulationWorld) const;

	/** Check if the interface has been registered to the manager */
	bool IsInterfaceRegistered(const TObjectPtr<UWorld>& SimulationWorld) const;
};
