// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/MakeBinaryConfigCommandlet.h"
#include "CoreGlobals.h"
#include "Logging/LogCategory.h"
#include "Logging/LogMacros.h"


UMakeBinaryConfigCommandlet::UMakeBinaryConfigCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UMakeBinaryConfigCommandlet::Main(const FString& Params)
{
	UE_LOG(LogInit, Fatal, TEXT("This commandlet is no longer supported. Use UnrealPak instead, which now has the BinaryConfig functionality"));
	return 0;
}
