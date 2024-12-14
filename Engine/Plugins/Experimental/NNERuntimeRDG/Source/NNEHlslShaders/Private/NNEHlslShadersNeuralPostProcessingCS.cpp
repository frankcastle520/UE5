// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNEHlslShadersNeuralPostProcessingCS.h"

namespace UE::NNEHlslShaders::Internal
{
	void TNeuralPostProcessingReadInputCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(InParameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), FNeuralPostProcessingConstants::THREAD_GROUP_SIZE);
	}

	void TNeuralPostProcessingPreStepCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(InParameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), FNeuralPostProcessingConstants::THREAD_GROUP_SIZE);
	}

	void TNeuralPostProcessingPostStepCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(InParameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), FNeuralPostProcessingConstants::THREAD_GROUP_SIZE);
	}

	void TNeuralPostProcessingWriteOutputPS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(InParameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), FNeuralPostProcessingConstants::THREAD_GROUP_SIZE);
	}

	IMPLEMENT_GLOBAL_SHADER(TNeuralPostProcessingReadInputCS, "/NNEHlslShaders/NNEHlslShadersNeuralPostProcessing.usf", "ReadInput", SF_Compute);
	IMPLEMENT_GLOBAL_SHADER(TNeuralPostProcessingPreStepCS, "/NNEHlslShaders/NNEHlslShadersNeuralPostProcessing.usf", "PreStep", SF_Compute);
	IMPLEMENT_GLOBAL_SHADER(TNeuralPostProcessingPostStepCS, "/NNEHlslShaders/NNEHlslShadersNeuralPostProcessing.usf", "PostStep", SF_Compute);
	IMPLEMENT_GLOBAL_SHADER(TNeuralPostProcessingWriteOutputPS, "/NNEHlslShaders/NNEHlslShadersNeuralPostProcessing.usf", "WriteOutput", SF_Pixel);
} // UE::NNEHlslShaders::Internal