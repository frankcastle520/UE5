// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterUtils.h"

namespace UE::NNEDenoiserShaders::Internal
{

	enum class EDefaultIOProcessInputKind : uint8
	{
		Color = 0,
		Albedo,
		Normal,
		Flow,
		Output,
		MAX
	};

	class FDefaultIOProcessConstants
	{
	public:
		static constexpr int32 THREAD_GROUP_SIZE{16};
	};
	
	class NNEDENOISERSHADERS_API FDefaultIOProcessCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FDefaultIOProcessCS);
		SHADER_USE_PARAMETER_STRUCT(FDefaultIOProcessCS, FGlobalShader)

		class FDefaultIOProcessInputKind : SHADER_PERMUTATION_ENUM_CLASS("INPUT_KIND_INDEX", EDefaultIOProcessInputKind);
		using FPermutationDomain = TShaderPermutationDomain<FDefaultIOProcessInputKind>;

	public:

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(int32, Width)
			SHADER_PARAMETER(int32, Height)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputTexture)
		END_SHADER_PARAMETER_STRUCT()

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	};

} // namespace UE::NNEDenoiser::Private