// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Containers/Array.h"
#include "MultiServerSettings.generated.h"

class FString;

/** Parameters for a single server process in a multi-server setup. */
USTRUCT()
struct FMultiServerDefinition
{
	GENERATED_BODY()

	/** User-specified identifier that must be unique for each server in a MultiServer cluster. */
	UPROPERTY(Config)
	FString LocalId;

	/** UDP port on which the MultiServer beacon will listen for new connections. */
	UPROPERTY(Config)
	uint16 ListenPort = 0;
};

/**
 * Settings that control how a multi-server setup is configured and launched.
 * For example, to define 4 servers in a Game ini file it might look like this:
 * [/Script/MultiServerConfiguration.MultiServerSettings]
 * !ServerDefinitions=ClearArray
 * +ServerDefinitions=(LocalId="0",ListenPort=17000)
 * +ServerDefinitions=(LocalId="1",ListenPort=17001)
 * +ServerDefinitions=(LocalId="2",ListenPort=17002)
 * +ServerDefinitions=(LocalId="3",ListenPort=17003)
 */
UCLASS(Config=Game)
class MULTISERVERCONFIGURATION_API UMultiServerSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Config)
	TArray<FMultiServerDefinition> ServerDefinitions;
};
