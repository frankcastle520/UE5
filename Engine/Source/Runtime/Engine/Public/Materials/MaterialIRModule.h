// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Materials/MaterialIRCommon.h"
#include "MaterialShared.h"

#if WITH_EDITOR

class FMaterialIRModule
{
public:
	struct FError
	{
		UMaterialExpression* Expression;
		FString Message;
	};

	struct FStatistics
	{
		TBitArray<> ExternalInputUsedMask[SF_NumFrequencies];
		int NumVertexTexCoords;
		int NumPixelTexCoords;
	};

public:
	FMaterialIRModule();
	~FMaterialIRModule();
	void Empty();
	EShaderPlatform GetShaderPlatform() const { return ShaderPlatform; }
	const FMaterialCompilationOutput& GetCompilationOutput() const { return CompilationOutput; }
	TArrayView<const UE::MIR::FSetMaterialOutput* const> GetOutputs() const { return Outputs; }
	const UE::MIR::FBlock& GetRootBlock() const { return *RootBlock; }
	TArrayView<const FError> GetErrors() const { return Errors; }
	const FStatistics& GetStatistics() const { return Statistics; }

private:
	EShaderPlatform ShaderPlatform;
	FMaterialCompilationOutput CompilationOutput;
	FMemStackBase Allocator{};
	TArray<UE::MIR::FValue*> Values;
	TArray<UE::MIR::FSetMaterialOutput*> Outputs;
	TArray<FError> Errors;
	UE::MIR::FBlock* RootBlock;
	FStatistics Statistics;

	friend UE::MIR::FEmitter;
	friend FMaterialIRModuleBuilderImpl;
};

#endif // #if WITH_EDITOR
