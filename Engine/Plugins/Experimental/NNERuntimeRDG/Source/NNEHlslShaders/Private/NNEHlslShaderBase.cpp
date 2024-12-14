// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNEHlslShadersBase.h"
#include "DataDrivenShaderPlatformInfo.h"

namespace UE::NNEHlslShaders::Internal
{
	bool FHlslShaderBase::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return FDataDrivenShaderPlatformInfo::GetSupportsNNEShaders(Parameters.Platform);
	}
	
} // UE::NNEHlslShaders::Internal