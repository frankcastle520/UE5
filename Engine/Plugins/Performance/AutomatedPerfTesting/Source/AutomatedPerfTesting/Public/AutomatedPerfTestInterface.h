// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "AutomatedPerfTestInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(BlueprintType)
class AUTOMATEDPERFTESTING_API UAutomatedPerfTestInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class AUTOMATEDPERFTESTING_API IAutomatedPerfTestInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category="Automated Perf Test")
	void SetupTest();

	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category="Automated Perf Test")
	void TeardownTest();

	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category="Automated Perf Test")
	void RunTest();

	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category="Automated Perf Test")
	void Exit();
};
