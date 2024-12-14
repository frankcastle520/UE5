// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutomatedPerfTestGameModeBase.h"
#include "AutomatedPerfTesting.h"

void AAutomatedPerfTestGameModeBase::SetupTest_Implementation()
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("AutomatedPerfTestGameModeBase SetupTest"))
}

void AAutomatedPerfTestGameModeBase::TeardownTest_Implementation()
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("AutomatedPerfTestGameModeBase TeardownTest"))
}

void AAutomatedPerfTestGameModeBase::RunTest_Implementation()
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("AutomatedPerfTestGameModeBase RunTest"))
}

void AAutomatedPerfTestGameModeBase::Exit_Implementation()
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("AutomatedPerfTestGameModeBase Exit"))
}