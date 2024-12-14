// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCO/CustomizableObjectCompilerTypes.h"

#include "CustomizableObject_Deprecated.generated.h"


USTRUCT()
struct FCompilationOptions_DEPRECATED
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	ECustomizableObjectTextureCompression TextureCompression = ECustomizableObjectTextureCompression::Fast;

	UPROPERTY()
	int32 OptimizationLevel = UE_MUTABLE_MAX_OPTIMIZATION;

	UPROPERTY()
	bool bUseDiskCompilation = false;

	UPROPERTY()
	uint64 PackagedDataBytesLimit = 256 * 1024 * 1024;

	UPROPERTY()
	uint64 EmbeddedDataBytesLimit = 1024;
};