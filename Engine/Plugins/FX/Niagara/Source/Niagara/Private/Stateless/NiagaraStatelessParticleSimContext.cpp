// Copyright Epic Games, Inc. All Rights Reserved.

#include "Stateless/NiagaraStatelessParticleSimContext.h"

#include "Stateless/NiagaraStatelessEmitterData.h"
#include "Stateless/NiagaraStatelessEmitterInstance.h"
#include "Stateless/NiagaraStatelessParticleSimExecData.h"

namespace NiagaraStateless
{
	constexpr uint32 ParticleComponentOffsets[int32(EParticleComponent::Num)] =
	{
		0,	// uint32		- Alive
		1,	// float		- Lifetime
		2,	// float		- Age
		3,	// float		- NormalizedAge
		4,	// float		- PreviousAge
		5,	// float		- PreviousNormalizedAge
		6,	// int32		- UniqueIndex
	};
	constexpr uint32 ParticleComponentTotalSize = 7;

	FUintVector4 Rand4DPCG32(FUintVector4 v)
	{
		// Linear congruential step.
		v.X = v.X * 1664525u + 1013904223u;
		v.Y = v.Y * 1664525u + 1013904223u;
		v.Z = v.Z * 1664525u + 1013904223u;
		v.W = v.W * 1664525u + 1013904223u;

		// shuffle
		v.X += v.Y * v.W;
		v.Y += v.Z * v.X;
		v.Z += v.X * v.Y;
		v.W += v.Y * v.Z;

		// xoring high bits into low makes all 32 bits pretty good
		v.X ^= (v.X >> 16u);
		v.Y ^= (v.Y >> 16u);
		v.Z ^= (v.Z >> 16u);
		v.W ^= (v.W >> 16u);

		// final shuffle
		v.X += v.Y * v.W;
		v.Y += v.Z * v.X;
		v.Z += v.X * v.Y;
		v.W += v.Y * v.Z;

		return v;
	}

	FParticleSimulationContext::FParticleSimulationContext(const FNiagaraStatelessEmitterData* InEmitterData, const void* InShaderParametersData, TConstArrayView<uint8> InDynamicBufferData)
		: EmitterData(InEmitterData)
		, BuiltData(EmitterData->BuiltData)
		, ShaderParametersData(static_cast<const uint8*>(InShaderParametersData))
		, StaticFloatData(EmitterData->StaticFloatData)
		, DynamicBufferData(InDynamicBufferData)
	{
		check(EmitterData->ParticleSimExecData);
	}

	void FParticleSimulationContext::Simulate(int32 InEmitterRandomSeed, float EmitterAge, float InDeltaTime, TConstArrayView<FNiagaraStatelessRuntimeSpawnInfo> SpawnInfos, FNiagaraDataBuffer* DestinationData)
	{
		NumInstances = 0;

		FSpawnInfoShaderParameters SpawnParameters;
		const uint32 ActiveParticles = EmitterData->CalculateActiveParticles(InEmitterRandomSeed, SpawnInfos, EmitterAge, &SpawnParameters);
		if (ActiveParticles > 0)
		{
			// Setup data buffer pointers
			DestinationData->Allocate(ActiveParticles);
			BufferStride	= DestinationData->GetFloatStride();
			BufferFloatData	= DestinationData->GetComponentPtrFloat(0);
			BufferInt32Data	= DestinationData->GetComponentPtrInt32(0);

			// Run Simulation
			SimulateInternal(InEmitterRandomSeed, EmitterAge, InDeltaTime, SpawnParameters, ActiveParticles);
		}

		// Set instance count
		DestinationData->SetNumInstances(NumInstances);
	}

	void FParticleSimulationContext::SimulateGPU(FRHICommandListBase& RHICmdList, int32 InEmitterRandomSeed, float EmitterAge, float InDeltaTime, TConstArrayView<FNiagaraStatelessRuntimeSpawnInfo> SpawnInfos, FNiagaraDataBuffer* DestinationData)
	{
		NumInstances = 0;

		FSpawnInfoShaderParameters SpawnParameters;
		const uint32 ActiveParticles = EmitterData->CalculateActiveParticles(InEmitterRandomSeed, SpawnInfos, EmitterAge, &SpawnParameters);
		if (ActiveParticles > 0)
		{
			check(DestinationData->GetNumInstancesAllocated() <= ActiveParticles);

			FRWBuffer& FloatBuffer = DestinationData->GetGPUBufferFloat();
			FRWBuffer& Int32Buffer = DestinationData->GetGPUBufferInt();

			// Setup data buffer pointers
			BufferStride	= DestinationData->GetFloatStride();
			BufferFloatData	= FloatBuffer.NumBytes > 0 ? reinterpret_cast<uint8*>(RHICmdList.LockBuffer(FloatBuffer.Buffer, 0, FloatBuffer.NumBytes, RLM_WriteOnly)) : nullptr;
			BufferInt32Data	= Int32Buffer.NumBytes > 0 ? reinterpret_cast<uint8*>(RHICmdList.LockBuffer(Int32Buffer.Buffer, 0, Int32Buffer.NumBytes, RLM_WriteOnly)) : nullptr;

			// Run Simulation
			SimulateInternal(InEmitterRandomSeed, EmitterAge, InDeltaTime, SpawnParameters, ActiveParticles);

			// Unlock buffers
			if (BufferFloatData)
			{
				RHICmdList.UnlockBuffer(FloatBuffer.Buffer);
			}
			if (BufferInt32Data)
			{
				RHICmdList.UnlockBuffer(Int32Buffer.Buffer);
			}
		}

		// Set instance count
		DestinationData->SetNumInstances(NumInstances);
	}

	void FParticleSimulationContext::SimulateInternal(int32 InEmitterRandomSeed, float EmitterAge, float InDeltaTime, FSpawnInfoShaderParameters& SpawnParameters, uint32 ActiveParticles)
	{
		// Setup simulation	
		NumInstances = 0;
		DeltaTime = InDeltaTime;
		InvDeltaTime = DeltaTime > 0.0f ? 1.0f / DeltaTime : 0.0f;
		EmitterRandomSeed = InEmitterRandomSeed;
		ModuleRandomSeed = 0;

		// Setup Required Components
		{
			static TArray<uint8>		RequiredStorage;
			static uint32				RequiredStorageStride;
			if (RequiredStorageStride < int32(BufferStride))
			{
				RequiredStorageStride = BufferStride;
				RequiredStorage.SetNumUninitialized(ParticleComponentTotalSize * BufferStride);
			}

			for (int32 i = 0; i < int32(EParticleComponent::Num); ++i)
			{
				RequiredComponents[i] = RequiredStorage.GetData() + (ParticleComponentOffsets[i] * BufferStride);
			}
		}

		// Setup optional components
		const FParticleSimulationExecData* ExecData = EmitterData->ParticleSimExecData;
		VariableComponents.AddDefaulted(ExecData->VariableComponentOffsets.Num());
		for (int32 i = 0; i < ExecData->VariableComponentOffsets.Num(); ++i)
		{
			const uint32 VariableOffset = ExecData->VariableComponentOffsets[i].GetOffset();
			if (ExecData->VariableComponentOffsets[i].IsFloat())
			{
				VariableComponents[i] = BufferFloatData + (VariableOffset * BufferStride);
			}
			else //if (ExecData->VariableComponentOffsets.IsInt32())
			{
				VariableComponents[i] = BufferInt32Data + (VariableOffset * BufferStride);
			}
		}

		// Redirect Unique ID (if required)
		if (ExecData->UniqueIDIndex != INDEX_NONE)
		{
			RequiredComponents[int(EParticleComponent::UniqueIndex)] = VariableComponents[ExecData->UniqueIDIndex];
		}

		// Setup particles
		for (uint32 iParticle = 0; iParticle < ActiveParticles; ++iParticle)
		{
			uint32 Particle_UniqueIndex = 0;
			float Particle_Age = -1.0f;
			{
				uint32 SpawnInfoIndex = iParticle;
				for (int32 GpuSpawnIndex = 0; GpuSpawnIndex < NiagaraStateless::MaxGpuSpawnInfos; ++GpuSpawnIndex)
				{
					const uint32 SpawnInfoNumActive = GET_SCALAR_ARRAY_ELEMENT(SpawnParameters.SpawnInfo_NumActive, GpuSpawnIndex);
					const uint32 SpawnInfoParticleOffset = GET_SCALAR_ARRAY_ELEMENT(SpawnParameters.SpawnInfo_ParticleOffset, GpuSpawnIndex);
					const uint32 SpawnInfoUniqueOffset = GET_SCALAR_ARRAY_ELEMENT(SpawnParameters.SpawnInfo_UniqueOffset, GpuSpawnIndex);
					const float  SpawnInfoTime = GET_SCALAR_ARRAY_ELEMENT(SpawnParameters.SpawnInfo_Time, GpuSpawnIndex);
					const float  SpawnInfoRate = GET_SCALAR_ARRAY_ELEMENT(SpawnParameters.SpawnInfo_Rate, GpuSpawnIndex);
					if (SpawnInfoIndex < SpawnInfoNumActive)
					{
						const uint32 SpawnParticleIndex = SpawnInfoIndex + SpawnInfoParticleOffset;
						Particle_UniqueIndex = SpawnInfoIndex + SpawnInfoUniqueOffset + SpawnInfoParticleOffset;
						Particle_Age = EmitterAge - (SpawnInfoTime + float(SpawnParticleIndex) * SpawnInfoRate);
						break;
					}

					SpawnInfoIndex -= SpawnInfoNumActive;
				}
				if (Particle_Age < 0.0f)
				{
					continue;
				}
			}

			// Write unique index as it's needed for the random operation
			GetParticleUniqueIndex()[NumInstances] = Particle_UniqueIndex;

			float Particle_Lifetime = RandomScaleBiasFloat(NumInstances, 0, EmitterData->LifetimeRange.GetScale(), EmitterData->LifetimeRange.Min);
			if (Particle_Lifetime <= 0.0f || Particle_Age >= Particle_Lifetime)
			{
				continue;
			}

			const float PreviousAge = FMath::Max(Particle_Age - DeltaTime, 0.0f);

			// Initialize particle information
			GetParticleAlive()[NumInstances] = 1;
			GetParticleLifetime()[NumInstances] = Particle_Lifetime;
			GetParticleAge()[NumInstances] = Particle_Age;
			GetParticleNormalizedAge()[NumInstances] = Particle_Age / Particle_Lifetime;
			GetParticlePreviousAge()[NumInstances] = PreviousAge;
			GetParticlePreviousNormalizedAge()[NumInstances] = PreviousAge / Particle_Lifetime;

			++NumInstances;
		}

		if (NumInstances == 0)
		{
			return;
		}

		// Execute the simulation
		for (const auto& Callback : ExecData->SimulateFunctions)
		{
			BuiltDataOffset = Callback.BuiltDataOffset;
			ShaderParameterOffset = Callback.ShaderParameterOffset;
			ModuleRandomSeed = Callback.RandomSeedOffset;
			Callback.Function(*this);
		}
	}

	uint32 FParticleSimulationContext::RandomUInt(uint32 iInstance, uint32 RandomSeedOffset) const
	{
		const uint32 UniqueIndex = uint32(GetParticleUniqueIndex()[iInstance]);
		const FUintVector4 RandomSeed = FNiagaraStatelessDefinitions::MakeRandomSeed(EmitterRandomSeed, UniqueIndex, ModuleRandomSeed, RandomSeedOffset);
		const FUintVector4 RandomValue = Rand4DPCG32(RandomSeed);
		return RandomValue.X;
	}

	FUintVector2 FParticleSimulationContext::RandomUInt2(uint32 iInstance, uint32 RandomSeedOffset) const
	{
		const uint32 UniqueIndex = uint32(GetParticleUniqueIndex()[iInstance]);
		const FUintVector4 RandomSeed = FNiagaraStatelessDefinitions::MakeRandomSeed(EmitterRandomSeed, UniqueIndex, ModuleRandomSeed, RandomSeedOffset);
		const FUintVector4 RandomValue = Rand4DPCG32(RandomSeed);
		return FUintVector2(RandomValue.X, RandomValue.Y);
	}

	FUintVector3 FParticleSimulationContext::RandomUInt3(uint32 iInstance, uint32 RandomSeedOffset) const
	{
		const uint32 UniqueIndex = uint32(GetParticleUniqueIndex()[iInstance]);
		const FUintVector4 RandomSeed = FNiagaraStatelessDefinitions::MakeRandomSeed(EmitterRandomSeed, UniqueIndex, ModuleRandomSeed, RandomSeedOffset);
		const FUintVector4 RandomValue = Rand4DPCG32(RandomSeed);
		return FUintVector3(RandomValue.X, RandomValue.Y, RandomValue.Z);
	}

	FUintVector4 FParticleSimulationContext::RandomUInt4(uint32 iInstance, uint32 RandomSeedOffset) const
	{
		const uint32 UniqueIndex = uint32(GetParticleUniqueIndex()[iInstance]);
		const FUintVector4 RandomSeed = FNiagaraStatelessDefinitions::MakeRandomSeed(EmitterRandomSeed, UniqueIndex, ModuleRandomSeed, RandomSeedOffset);
		const FUintVector4 RandomValue = Rand4DPCG32(RandomSeed);
		return RandomValue;
	}

	float FParticleSimulationContext::RandomFloat(uint32 iInstance, uint32 RandomSeedOffset) const
	{
		const uint32 V = RandomUInt(iInstance, RandomSeedOffset);
		return float((V >> 8) & 0x00ffffff) / 16777216.0f;
	}

	FVector2f FParticleSimulationContext::RandomFloat2(uint32 iInstance, uint32 RandomSeedOffset) const
	{
		const FUintVector2 V = RandomUInt2(iInstance, RandomSeedOffset);
		return FVector2f(
			float((V.X >> 8) & 0x00ffffff) / 16777216.0f,
			float((V.Y >> 8) & 0x00ffffff) / 16777216.0f
		);
	}

	FVector3f FParticleSimulationContext::RandomFloat3(uint32 iInstance, uint32 RandomSeedOffset) const
	{
		const FUintVector3 V = RandomUInt3(iInstance, RandomSeedOffset);
		return FVector3f(
			float((V.X >> 8) & 0x00ffffff) / 16777216.0f,
			float((V.Y >> 8) & 0x00ffffff) / 16777216.0f,
			float((V.Z >> 8) & 0x00ffffff) / 16777216.0f
		);
	}

	FVector4f FParticleSimulationContext::RandomFloat4(uint32 iInstance, uint32 RandomSeedOffset) const
	{
		const FUintVector4 V = RandomUInt4(iInstance, RandomSeedOffset);
		return FVector4f(
			float((V.X >> 8) & 0x00ffffff) / 16777216.0f,
			float((V.Y >> 8) & 0x00ffffff) / 16777216.0f,
			float((V.Z >> 8) & 0x00ffffff) / 16777216.0f,
			float((V.W >> 8) & 0x00ffffff) / 16777216.0f
		);
	}
} //namespace NiagaraStateless
