// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassEnvQueryProcessorBase.generated.h"

class UEnvQueryNode;

/** Processor for completing MassEQSSubsystem Requests sent from UMassEnvQueryTest_MassEntityTags */
UCLASS(Abstract, meta = (DisplayName = "Mass EQS Processor Base"))
class UMassEnvQueryProcessorBase : public UMassProcessor
{
	GENERATED_BODY()

protected:
	virtual void Initialize(UObject& Owner);
	
	TSubclassOf<UEnvQueryNode> CorrespondingRequestClass = nullptr;
	int32 CachedRequestQueryIndex = -1;
};