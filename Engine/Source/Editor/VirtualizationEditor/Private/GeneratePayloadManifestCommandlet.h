// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "UObject/ObjectMacros.h"

#include "GeneratePayloadManifestCommandlet.generated.h"

/**
 * Creates a csv file containing info about all of the payloads in a set of packages.
 * By default the commandlet will parse the payloads of all packages in the current
 * project but this can be overridden with the cmdline switch -PackageDir=XYZ which
 * will allow the commandlet to parse the payloads of the packages in a given directory.
 * 
 * Because the commandlet is the VirtualizationEditor module it needs to be invoked 
 * with the command line:
 * -run="VirtualizationEditor.GeneratePayloadManifest"
 */
UCLASS()
class UGeneratePayloadManifestCommandlet
	: public UCommandlet
{
	GENERATED_UCLASS_BODY()

	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

	static int32 StaticMain(const FString& Params);

private:

	bool ParseCmdline(const FString& Params);

	enum class EPayloadFilter
	{
		None = 0,
		LocalOnly		=  1 << 0,
		PendingOnly		= (1 << 1) | LocalOnly,
		FilteredOnly	= (1 << 2) | LocalOnly,
		VirtualizedOnly	= (1 << 3)
	};
	FRIEND_ENUM_CLASS_FLAGS(EPayloadFilter);

	EPayloadFilter Filter = EPayloadFilter::None;
};
