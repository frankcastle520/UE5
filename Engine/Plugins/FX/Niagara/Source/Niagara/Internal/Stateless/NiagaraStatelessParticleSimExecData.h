// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraStatelessCommon.h"

struct FNiagaraDataSetCompiledData;

namespace NiagaraStateless
{
class FParticleSimulationContext;

class FParticleSimulationExecData
{
public:
	struct FCallback
	{
		FCallback() = default;
		explicit FCallback(TFunction<void(const FParticleSimulationContext&)> InFunc, int32 InBuiltDataOffset, int32 InShaderParameterOffset, uint32 InRandomSeedOffset)
			: Function(MoveTemp(InFunc))
			, BuiltDataOffset(InBuiltDataOffset)
			, ShaderParameterOffset(InShaderParameterOffset)
			, RandomSeedOffset(InRandomSeedOffset)
		{
		}

		TFunction<void(const FParticleSimulationContext&)>	Function;
		int32	BuiltDataOffset = 0;
		int32	ShaderParameterOffset = 0;
		uint32	RandomSeedOffset = 0;
	};

	struct FVariableOffset
	{
		const bool IsFloat() const { return Type == 0; }
		const bool IsInt32() const { return Type == 1; }
		const uint32 GetOffset() const { return Offset; }

		uint16	Type	: 1	 = 0;
		uint16	Offset	: 15 = 0;
	};

	explicit FParticleSimulationExecData(const FNiagaraDataSetCompiledData& ParticleDataSetCompiledData);

	TArray<FVariableOffset>	VariableComponentOffsets;		// Stored offsets per variable
	int32					UniqueIDIndex = INDEX_NONE;		// Variable index for Unique ID, since we always generate this we need to know if we should output of keep internal
	TArray<FCallback>		SimulateFunctions;				// Series of functions to simulate particles
};

} //namespace NiagaraStateless
