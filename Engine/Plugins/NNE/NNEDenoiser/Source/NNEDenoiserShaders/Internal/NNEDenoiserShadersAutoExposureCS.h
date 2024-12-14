// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterUtils.h"

namespace UE::NNEDenoiserShaders::Internal
{
	
	class FAutoExposureDownsampleConstants
	{
	public:
		static constexpr int32 MAX_BIN_SIZE{16};
		static constexpr int32 THREAD_GROUP_SIZE{MAX_BIN_SIZE};
	};

	class NNEDENOISERSHADERS_API FAutoExposureDownsampleCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FAutoExposureDownsampleCS);
		SHADER_USE_PARAMETER_STRUCT(FAutoExposureDownsampleCS, FGlobalShader)

	public:

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(int32, InputTextureWidth)
			SHADER_PARAMETER(int32, InputTextureHeight)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
			SHADER_PARAMETER(int32, NumBinsW)
			SHADER_PARAMETER(int32, NumBinsH)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutputBins)
		END_SHADER_PARAMETER_STRUCT()

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	};

	class FAutoExposureReduceConstants
	{
	public:
		static constexpr int32 THREAD_GROUP_SIZE{32};
		static constexpr float EPS{1e-8f};
		static constexpr float KEY{0.18f};
	};

	class NNEDENOISERSHADERS_API FAutoExposureReduceCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FAutoExposureReduceCS);
		SHADER_USE_PARAMETER_STRUCT(FAutoExposureReduceCS, FGlobalShader)

	public:

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(int32, InputSize)
			SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InputBins)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutputSums)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutputCounts)
			SHADER_PARAMETER(int32, NumThreads)
		END_SHADER_PARAMETER_STRUCT()

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	};

	class NNEDENOISERSHADERS_API FAutoExposureReduceFinalCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FAutoExposureReduceFinalCS);
		SHADER_USE_PARAMETER_STRUCT(FAutoExposureReduceFinalCS, FGlobalShader)

	public:

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(int32, InputSize)
			SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InputSums)
			SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InputCounts)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutputBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	};

} // namespace UE::NNEDenoiser::Private