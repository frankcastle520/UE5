// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "UObject/ObjectMacros.h"

#include "GenerateMountPointPayloadManifestCommandlet.generated.h"

/**
 * Because the commandlet is the VirtualizationEditor module it needs to be invoked 
 * with the command line:
 * -run="VirtualizationEditor.GenerateMountPointPayloadManifestCommandlet"
 */
UCLASS()
class UGenerateMountPointPayloadManifestCommandlet
	: public UCommandlet
{
	GENERATED_UCLASS_BODY()

	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

	static int32 StaticMain(const FString& Params);

private:

	bool ParseCmdline(const FString& Params);

	bool bDetailedFilterReasons = false;
};
