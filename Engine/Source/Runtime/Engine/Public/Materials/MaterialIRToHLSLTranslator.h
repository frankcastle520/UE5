// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Materials/MaterialIRCommon.h"

#if WITH_EDITOR

//
struct FMaterialIRToHLSLTranslation
{
	//
	const FMaterialIRModule* Module;

	//
	const FMaterial* Material;
	
	//
	const FStaticParameterSet* StaticParameters;
	
	//
	const ITargetPlatform* TargetPlatform;
	
	//
	void Run(TMap<FString, FString>& OutParameters, FShaderCompilerEnvironment& OutEnvironment);
};

#endif // #if WITH_EDITOR
