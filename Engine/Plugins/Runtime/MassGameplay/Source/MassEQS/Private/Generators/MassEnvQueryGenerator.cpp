// Copyright Epic Games, Inc. All Rights Reserved.

#include "Generators/MassEnvQueryGenerator.h"
#include "MassEQSSubsystem.h"

UMassEnvQueryGenerator::UMassEnvQueryGenerator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCanRunAsync = true;
}

void UMassEnvQueryGenerator::GenerateItems(FEnvQueryInstance& QueryInstance) const
{
	MassEQSRequestHandler.SendOrRecieveRequest(QueryInstance, *this);
}
