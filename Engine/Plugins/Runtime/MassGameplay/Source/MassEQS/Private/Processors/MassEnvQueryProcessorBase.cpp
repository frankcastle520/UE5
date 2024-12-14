// Copyright Epic Games, Inc. All Rights Reserved.

#include "Processors/MassEnvQueryProcessorBase.h"

#include "MassEQSSubsystem.h"

void UMassEnvQueryProcessorBase::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

	if (CorrespondingRequestClass)
	{
		UWorld* World = Owner.GetWorld();
		check(World);
		UMassEQSSubsystem* MassEQSSubsystem = World->GetSubsystem<UMassEQSSubsystem>();
		check(MassEQSSubsystem)
	
		CachedRequestQueryIndex = MassEQSSubsystem->GetRequestQueueIndex(CorrespondingRequestClass);
	}
}