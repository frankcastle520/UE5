// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Materials/MaterialIRCommon.h"

#if WITH_EDITOR

//
struct FMaterialIRModuleBuilder
{
	UMaterial* Material;
	EShaderPlatform ShaderPlatform;
	const ITargetPlatform* TargetPlatform;
	const FStaticParameterSet& StaticParameters;
	FMaterialInsights* TargetInsight{};

	bool Build(FMaterialIRModule* TargetModule);
};

#endif
