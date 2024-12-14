// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Dataflow/DataflowSimulationInterface.h"
#include "Dataflow/DataflowSimulationProxy.h"
#include "DataflowPhysicsObject.generated.h"

/**
 * Dataflow physics object proxy (PT)
 */
USTRUCT()
struct DATAFLOWSIMULATION_API FDataflowPhysicsObjectProxy : public FDataflowSimulationProxy
{
	GENERATED_BODY()
	
	FDataflowPhysicsObjectProxy();
	virtual ~FDataflowPhysicsObjectProxy() override = default;

	/** Get the proxy script struct */
	virtual const UScriptStruct* GetScriptStruct() const override
	{
		return StaticStruct();
	}
};

UINTERFACE()
class DATAFLOWSIMULATION_API UDataflowPhysicsObjectInterface : public UDataflowSimulationInterface
{
	GENERATED_BODY()
};

/**
 * Dataflow physics object interface to send/receive datas (GT <-> PT)
 */
class DATAFLOWSIMULATION_API IDataflowPhysicsObjectInterface : public IDataflowSimulationInterface
{
	GENERATED_BODY()
	
public:
	IDataflowPhysicsObjectInterface();

	/** Get the simulation type */
	virtual FString GetSimulationType() const override
	{
		return FDataflowPhysicsObjectProxy::StaticStruct()->GetName();
	}
};
