// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataDrivenShaderPlatformInfo.h"
#include "RHIGlobals.h"
#include "Shader.h"

namespace UE::ShaderPermutationUtils
{
	inline bool ShouldCompileWithWaveSize(const FShaderPermutationParameters& Parameters, int32 WaveSize)
	{
		if (WaveSize)
		{
			if (!RHISupportsWaveOperations(Parameters.Platform))
			{
				return false;
			}

			if (WaveSize < int32(FDataDrivenShaderPlatformInfo::GetMinimumWaveSize(Parameters.Platform)) ||
				WaveSize > int32(FDataDrivenShaderPlatformInfo::GetMaximumWaveSize(Parameters.Platform)))
			{
				return false;
			}
		}

		return true;
	}

	inline bool ShouldPrecacheWithWaveSize(const FShaderPermutationParameters& Parameters, int32 WaveSize)
	{
		if (WaveSize)
		{
			if (WaveSize < GRHIGlobals.MinimumWaveSize || WaveSize > GRHIGlobals.MaximumWaveSize)
			{
				return false;
			}
		}

		return true;
	}
}
