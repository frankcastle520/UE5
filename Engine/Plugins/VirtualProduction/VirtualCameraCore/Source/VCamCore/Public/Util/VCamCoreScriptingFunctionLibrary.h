// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UnifiedActivationDelegate.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VCamCoreScriptingFunctionLibrary.generated.h"

UCLASS()
class VCAMCORE_API UVCamCoreScriptingFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	/** Add a delegate with which you can decide whether an output provider can be activated. */
	UFUNCTION(BlueprintCallable, Category = "Virtual Camera")
	static void AddCanActivateOutputProviderDelegate(FCanChangeActiviationDynamicVCamDelegate Delegate);
	
	/** Removes all activation deciding delegates you previously bound to the provider Object. */
	UFUNCTION(BlueprintCallable, Category = "Virtual Camera")
	static void RemoveCanActivateOutputProviderDelegate(UObject* Object);
};
