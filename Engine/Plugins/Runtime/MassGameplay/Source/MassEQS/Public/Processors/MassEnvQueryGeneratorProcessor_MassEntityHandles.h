// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Processors/MassEnvQueryProcessorBase.h"
#include "MassEnvQueryGeneratorProcessor_MassEntityHandles.generated.h"

/** Processor for completing MassEQSSubsystem Requests sent from UMassEnvQueryGenerator_MassEntityHandles */
UCLASS(meta = (DisplayName = "Mass Entity Handles Generator Processor"))
class UMassEnvQueryGeneratorProcessor_MassEntityHandles : public UMassEnvQueryProcessorBase
{
	GENERATED_BODY()
public:
	UMassEnvQueryGeneratorProcessor_MassEntityHandles();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};