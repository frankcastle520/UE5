// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Dataflow/DataflowSimulationInterface.h"
#include "Dataflow/DataflowSimulationProxy.h"
#include "DataflowConstraintObject.generated.h"

/**
 * Dataflow collision object proxy (PT)
 */
USTRUCT()
struct DATAFLOWSIMULATION_API FDataflowConstraintObjectProxy : public FDataflowSimulationProxy
{
	GENERATED_BODY()
	
	FDataflowConstraintObjectProxy();
	virtual ~FDataflowConstraintObjectProxy() override = default;

	/** Get the proxy script struct */
	virtual const UScriptStruct* GetScriptStruct() const override
	{
		return StaticStruct();
	}
};

UINTERFACE()
class DATAFLOWSIMULATION_API UDataflowConstraintObjectInterface : public UDataflowSimulationInterface
{
	GENERATED_BODY()
};

/**
 * Dataflow collision object interface to send/receive datas (GT <-> PT)
 */
class DATAFLOWSIMULATION_API IDataflowConstraintObjectInterface : public IDataflowSimulationInterface
{
	GENERATED_BODY()
	
public:
	IDataflowConstraintObjectInterface();

	/** Get the simulation type */
	virtual FString GetSimulationType() const override
	{
		return FDataflowConstraintObjectProxy::StaticStruct()->GetName();
	}
};