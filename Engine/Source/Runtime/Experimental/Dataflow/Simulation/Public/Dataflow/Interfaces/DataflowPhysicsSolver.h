// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Dataflow/DataflowSimulationInterface.h"
#include "Dataflow/DataflowSimulationProxy.h"
#include "DataflowPhysicsSolver.generated.h"

/**
 * Dataflow simulation physics solver proxy (PT)
 */
USTRUCT()
struct DATAFLOWSIMULATION_API FDataflowPhysicsSolverProxy : public FDataflowSimulationProxy
{
	GENERATED_BODY()

	FDataflowPhysicsSolverProxy();
	virtual ~FDataflowPhysicsSolverProxy() override = default;

	/** Get the proxy script struct */
	virtual const UScriptStruct* GetScriptStruct() const override
	{
		return StaticStruct();
	}
	
	/** Advance the solver datas in time */
	virtual void AdvanceSolverDatas(const float DeltaTime) {}

	/** Get the solver time step */
	virtual float GetTimeStep() { return 0.033f;}
};

UINTERFACE()
class DATAFLOWSIMULATION_API UDataflowPhysicsSolverInterface : public UDataflowSimulationInterface
{
	GENERATED_BODY()
};

/**
 * Dataflow physics solver interface to send/receive (GT)
 */
class DATAFLOWSIMULATION_API IDataflowPhysicsSolverInterface : public IDataflowSimulationInterface
{
	GENERATED_BODY()
	
public:
	IDataflowPhysicsSolverInterface();
	
	/** Get the simulation type */
	virtual FString GetSimulationType() const override
	{
		return FDataflowPhysicsSolverProxy::StaticStruct()->GetName();
	}
};




