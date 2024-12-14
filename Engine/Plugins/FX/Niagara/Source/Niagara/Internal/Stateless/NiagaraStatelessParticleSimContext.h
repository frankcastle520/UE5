// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraStatelessCommon.h"
#include "NiagaraStatelessBuiltDistribution.h"

#include "ShaderParameterMacros.h"

class FNiagaraDataBuffer;
struct FNiagaraStatelessEmitterData;
struct FNiagaraStatelessRuntimeSpawnInfo;
class FRHICommandListBase;

namespace NiagaraStateless
{
class FSpawnInfoShaderParameters;

enum class EParticleComponent
{
	Alive,
	Lifetime,
	Age,
	NormalizedAge,
	PreviousAge,
	PreviousNormalizedAge,
	UniqueIndex,
	Num
};

class FParticleSimulationContext
{
public:
	explicit FParticleSimulationContext(const FNiagaraStatelessEmitterData* EmitterData, const void* InShaderParametersData, TConstArrayView<uint8> DynamicBufferData);

	void Simulate(int32 EmitterRandomSeed, float EmitterAge, float DeltaTime, TConstArrayView<FNiagaraStatelessRuntimeSpawnInfo> SpawnInfos, FNiagaraDataBuffer* DestinationData);
	void SimulateGPU(FRHICommandListBase& RHICmdList, int32 EmitterRandomSeed, float EmitterAge, float DeltaTime, TConstArrayView<FNiagaraStatelessRuntimeSpawnInfo> SpawnInfos, FNiagaraDataBuffer* DestinationData);

private:
	void SimulateInternal(int32 EmitterRandomSeed, float EmitterAge, float DeltaTime, FSpawnInfoShaderParameters& SpawnParameters, uint32 ActiveParticles);

public:
	uint32 GetNumInstances() const { return NumInstances; }
	uint32 GetParticleComponentOffset(int32 iComponent) const { return BufferStride * iComponent; };

	float GetDeltaTime() const { return DeltaTime; }
	float GetInvDeltaTime() const { return InvDeltaTime; }

	// Get a pointer to required particle components
	int32* GetParticleAlive() const { return static_cast<int32*>(RequiredComponents[int32(EParticleComponent::Alive)]); }
	float* GetParticleLifetime() { return static_cast<float*>(RequiredComponents[int32(EParticleComponent::Lifetime)]); }
	float* GetParticleAge() { return static_cast<float*>(RequiredComponents[int32(EParticleComponent::Age)]); }
	float* GetParticleNormalizedAge() { return static_cast<float*>(RequiredComponents[int32(EParticleComponent::NormalizedAge)]); }
	float* GetParticlePreviousAge() { return static_cast<float*>(RequiredComponents[int32(EParticleComponent::PreviousAge)]); }
	float* GetParticlePreviousNormalizedAge() { return static_cast<float*>(RequiredComponents[int32(EParticleComponent::PreviousNormalizedAge)]); }
	int32* GetParticleUniqueIndex() { return static_cast<int32*>(RequiredComponents[int32(EParticleComponent::UniqueIndex)]); }

	const float* GetParticleLifetime() const { return static_cast<float*>(RequiredComponents[int32(EParticleComponent::Lifetime)]); }
	const float* GetParticleAge() const { return static_cast<float*>(RequiredComponents[int32(EParticleComponent::Age)]); }
	const float* GetParticleNormalizedAge() const { return static_cast<float*>(RequiredComponents[int32(EParticleComponent::NormalizedAge)]); }
	const float* GetParticlePreviousAge() const { return static_cast<float*>(RequiredComponents[int32(EParticleComponent::PreviousAge)]); }
	const float* GetParticlePreviousNormalizedAge() const { return static_cast<float*>(RequiredComponents[int32(EParticleComponent::PreviousNormalizedAge)]); }
	const int32* GetParticleUniqueIndex() const { return static_cast<int32*>(RequiredComponents[int32(EParticleComponent::UniqueIndex)]); }

	// Get a pointer to an optional particle component (i.e. one not required in the data set)
	float* GetParticleVariableFloat(int32 iVariable) const { return iVariable >= 0 ? reinterpret_cast<float*>(VariableComponents[iVariable]) : nullptr; }
	int32* GetParticleVariableInt32(int32 iVariable) const { return iVariable >= 0 ? reinterpret_cast<int32*>(VariableComponents[iVariable]) : nullptr; }

	template<typename TType>
	const TType ReadParticleVariableComponent(int32 iVariable, int32 iInstance, int32 iComponent) const { return *reinterpret_cast<const TType*>(VariableComponents[iVariable] + (BufferStride * iComponent) + (iInstance * sizeof(float))); }
	int32 ReadParticleVariable(int32 iVariable, uint32 iInstance, int32 DefaultValue) const { check(iInstance < NumInstances); return iVariable >= 0 ? ReadParticleVariableComponent<int32>(iVariable, iInstance, 0) : DefaultValue; }
	float ReadParticleVariable(int32 iVariable, uint32 iInstance, float DefaultValue) const { check(iInstance < NumInstances); return iVariable >= 0 ? ReadParticleVariableComponent<float>(iVariable, iInstance, 0) : DefaultValue; }
	FVector2f ReadParticleVariable(int32 iVariable, uint32 iInstance, const FVector2f& DefaultValue) const { check(iInstance < NumInstances); return iVariable >= 0 ? FVector2f(ReadParticleVariableComponent<float>(iVariable, iInstance, 0), ReadParticleVariableComponent<float>(iVariable, iInstance, 1)) : DefaultValue; }
	FVector3f ReadParticleVariable(int32 iVariable, uint32 iInstance, const FVector3f& DefaultValue) const { check(iInstance < NumInstances); return iVariable >= 0 ? FVector3f(ReadParticleVariableComponent<float>(iVariable, iInstance, 0), ReadParticleVariableComponent<float>(iVariable, iInstance, 1), ReadParticleVariableComponent<float>(iVariable, iInstance, 2)) : DefaultValue; }
	FVector4f ReadParticleVariable(int32 iVariable, uint32 iInstance, const FVector4f& DefaultValue) const { check(iInstance < NumInstances); return iVariable >= 0 ? FVector4f(ReadParticleVariableComponent<float>(iVariable, iInstance, 0), ReadParticleVariableComponent<float>(iVariable, iInstance, 1), ReadParticleVariableComponent<float>(iVariable, iInstance, 2), ReadParticleVariableComponent<float>(iVariable, iInstance, 3)) : DefaultValue; }
	FQuat4f ReadParticleVariable(int32 iVariable, uint32 iInstance, const FQuat4f& DefaultValue) const { check(iInstance < NumInstances); return iVariable >= 0 ? FQuat4f(ReadParticleVariableComponent<float>(iVariable, iInstance, 0), ReadParticleVariableComponent<float>(iVariable, iInstance, 1), ReadParticleVariableComponent<float>(iVariable, iInstance, 2), ReadParticleVariableComponent<float>(iVariable, iInstance, 3)) : DefaultValue; }
	FLinearColor ReadParticleVariable(int32 iVariable, uint32 iInstance, const FLinearColor& DefaultValue) const { check(iInstance < NumInstances); return iVariable >= 0 ? FLinearColor(ReadParticleVariableComponent<float>(iVariable, iInstance, 0), ReadParticleVariableComponent<float>(iVariable, iInstance, 1), ReadParticleVariableComponent<float>(iVariable, iInstance, 2), ReadParticleVariableComponent<float>(iVariable, iInstance, 3)) : DefaultValue; }

	void WriteParticleVariableComponent(int32 iVariable, int32 iInstance, int32 iComponent, float Value) const { *reinterpret_cast<float*>(VariableComponents[iVariable] + (BufferStride * iComponent) + (iInstance * sizeof(float))) = Value; }
	void WriteParticleVariableComponent(int32 iVariable, int32 iInstance, int32 iComponent, int32 Value) const { *reinterpret_cast<int32*>(VariableComponents[iVariable] + (BufferStride * iComponent) + (iInstance * sizeof(float))) = Value; }
	void WriteParticleVariable(int32 iVariable, uint32 iInstance, int32 Value) const { check(iInstance < NumInstances); if (iVariable >= 0 ) { WriteParticleVariableComponent(iVariable, iInstance, 0, Value); } }
	void WriteParticleVariable(int32 iVariable, uint32 iInstance, float Value) const { check(iInstance < NumInstances); if (iVariable >= 0 ) { WriteParticleVariableComponent(iVariable, iInstance, 0, Value); } }
	void WriteParticleVariable(int32 iVariable, uint32 iInstance, const FVector2f& Value) const { check(iInstance < NumInstances); if (iVariable >= 0 ) { WriteParticleVariableComponent(iVariable, iInstance, 0, Value.X); WriteParticleVariableComponent(iVariable, iInstance, 1, Value.Y); } }
	void WriteParticleVariable(int32 iVariable, uint32 iInstance, const FVector3f& Value) const { check(iInstance < NumInstances); if (iVariable >= 0 ) { WriteParticleVariableComponent(iVariable, iInstance, 0, Value.X); WriteParticleVariableComponent(iVariable, iInstance, 1, Value.Y); WriteParticleVariableComponent(iVariable, iInstance, 2, Value.Z); } }
	void WriteParticleVariable(int32 iVariable, uint32 iInstance, const FVector4f& Value) const { check(iInstance < NumInstances); if (iVariable >= 0) { WriteParticleVariableComponent(iVariable, iInstance, 0, Value.X); WriteParticleVariableComponent(iVariable, iInstance, 1, Value.Y); WriteParticleVariableComponent(iVariable, iInstance, 2, Value.Z); WriteParticleVariableComponent(iVariable, iInstance, 3, Value.W); } }
	void WriteParticleVariable(int32 iVariable, uint32 iInstance, const FQuat4f& Value) const { check(iInstance < NumInstances); if (iVariable >= 0) { WriteParticleVariableComponent(iVariable, iInstance, 0, Value.X); WriteParticleVariableComponent(iVariable, iInstance, 1, Value.Y); WriteParticleVariableComponent(iVariable, iInstance, 2, Value.Z); WriteParticleVariableComponent(iVariable, iInstance, 3, Value.W); } }
	void WriteParticleVariable(int32 iVariable, uint32 iInstance, const FLinearColor& Value) const { check(iInstance < NumInstances); if (iVariable >= 0 ) { WriteParticleVariableComponent(iVariable, iInstance, 0, Value.R); WriteParticleVariableComponent(iVariable, iInstance, 1, Value.G); WriteParticleVariableComponent(iVariable, iInstance, 2, Value.B); WriteParticleVariableComponent(iVariable, iInstance, 3, Value.A); } }

	template<typename TType>
	TType GetParameterBufferValue(int32 Offset, int32 Element) const
	{
		TType Value;
		FMemory::Memcpy(&Value, &DynamicBufferData[Offset * 4], sizeof(TType));
		return Value;
	}

	int32 GetParameterBufferInt(int32 Offset, int32 Element) const { return GetParameterBufferValue<int32>(Offset, Element); }

	template<typename TType>
	TType GetParameterBufferFloat(int32 Offset, int32 Element) const { return GetParameterBufferValue<TType>(Offset, Element); }
	template<typename TType>
	TType GetParameterBufferFloat(int32 Offset, const TType& DefaultValue) const { return Offset >= 0 ? GetParameterBufferValue<TType>(Offset, 0) : DefaultValue; }

	template<typename TType>
	TType GetStaticFloat(uint32 Offset, uint32 Element) const;
	template<> float GetStaticFloat<float>(uint32 Offset, uint32 Element) const { return StaticFloatData[Offset + Element]; }
	template<> FVector2f GetStaticFloat<FVector2f>(uint32 Offset, uint32 Element) const { Offset += Element * 2; return FVector2f(StaticFloatData[Offset], StaticFloatData[Offset + 1]); }
	template<> FVector3f GetStaticFloat<FVector3f>(uint32 Offset, uint32 Element) const { Offset += Element * 3; return FVector3f(StaticFloatData[Offset], StaticFloatData[Offset + 1], StaticFloatData[Offset + 2]); }
	template<> FVector4f GetStaticFloat<FVector4f>(uint32 Offset, uint32 Element) const { Offset += Element * 4; return FVector4f(StaticFloatData[Offset], StaticFloatData[Offset + 1], StaticFloatData[Offset + 2], StaticFloatData[Offset + 3]); }
	template<> FLinearColor GetStaticFloat<FLinearColor>(uint32 Offset, uint32 Element) const { Offset += Element * 4; return FLinearColor(StaticFloatData[Offset], StaticFloatData[Offset + 1], StaticFloatData[Offset + 2], StaticFloatData[Offset + 3]); }

	template<typename TType>
	TType LerpStaticFloat(FUintVector2 Parameters, float U) const
	{
		const float Offset = U * float(Parameters.Y);
		const TType Value0 = GetStaticFloat<TType>(Parameters.X, FMath::FloorToInt(Offset));
		const TType Value1 = GetStaticFloat<TType>(Parameters.X, FMath::CeilToInt(Offset));
		return FMath::Lerp(Value0, Value1, FMath::Fractional(Offset));
	}

	float			Lerp(const float& Lhs, const float& Rhs, const float& U, bool bUniform) const { return FMath::Lerp(Lhs, Rhs, U); }
	FVector2f		Lerp(const FVector2f& Lhs, const FVector2f& Rhs, const FVector2f& U, bool bUniform) const { const FVector2f V = FMath::Lerp(Lhs, Rhs, U); return bUniform ? FVector2f(V.X, V.X) : V; }
	FVector3f		Lerp(const FVector3f& Lhs, const FVector3f& Rhs, const FVector3f& U, bool bUniform) const { const FVector3f V = FMath::Lerp(Lhs, Rhs, U); return bUniform ? FVector3f(V.X, V.X, V.X) : V; }
	FVector4f		Lerp(const FVector4f& Lhs, const FVector4f& Rhs, const FVector4f& U, bool bUniform) const { const FVector4f V = FMath::Lerp(Lhs, Rhs, U); return bUniform ? FVector4f(V.X, V.X, V.X, V.X) : V; }

	template<typename TType>
	TType					TRandomFloat(uint32 iInstance, uint32 RandomSeedOffset) const;
	template<> float		TRandomFloat<float>(uint32 iInstance, uint32 RandomSeedOffset) const { return RandomFloat(iInstance, RandomSeedOffset); }
	template<> FVector2f	TRandomFloat<FVector2f>(uint32 iInstance, uint32 RandomSeedOffset) const { return RandomFloat2(iInstance, RandomSeedOffset); }
	template<> FVector3f	TRandomFloat<FVector3f>(uint32 iInstance, uint32 RandomSeedOffset) const { return RandomFloat3(iInstance, RandomSeedOffset); }
	template<> FVector4f	TRandomFloat<FVector4f>(uint32 iInstance, uint32 RandomSeedOffset) const { return RandomFloat4(iInstance, RandomSeedOffset); }
	template<> FLinearColor	TRandomFloat<FLinearColor>(uint32 iInstance, uint32 RandomSeedOffset) const { return FLinearColor(RandomFloat4(iInstance, RandomSeedOffset)); }

	uint32			RandomUInt(uint32 iInstance, uint32 RandomSeedOffset) const;
	FUintVector2	RandomUInt2(uint32 iInstance, uint32 RandomSeedOffset) const;
	FUintVector3	RandomUInt3(uint32 iInstance, uint32 RandomSeedOffset) const;
	FUintVector4	RandomUInt4(uint32 iInstance, uint32 RandomSeedOffset) const;

	float			RandomFloat(uint32 iInstance, uint32 RandomSeedOffset) const;
	FVector2f		RandomFloat2(uint32 iInstance, uint32 RandomSeedOffset) const;
	FVector3f		RandomFloat3(uint32 iInstance, uint32 RandomSeedOffset) const;
	FVector4f		RandomFloat4(uint32 iInstance, uint32 RandomSeedOffset) const;

	float			RandomScaleBiasFloat(uint32 iInstance, uint32 RandomSeedOffset, const float Scale, const float Bias) const { return Bias + (RandomFloat(iInstance, RandomSeedOffset) * Scale); }
	FVector2f		RandomScaleBiasFloat(uint32 iInstance, uint32 RandomSeedOffset, const FVector2f& Scale, const FVector2f& Bias) const { return Bias + (RandomFloat2(iInstance, RandomSeedOffset) * Scale); }
	FVector3f		RandomScaleBiasFloat(uint32 iInstance, uint32 RandomSeedOffset, const FVector3f& Scale, const FVector3f& Bias) const { return Bias + (RandomFloat3(iInstance, RandomSeedOffset) * Scale); }
	FVector4f		RandomScaleBiasFloat(uint32 iInstance, uint32 RandomSeedOffset, const FVector4f& Scale, const FVector4f& Bias) const { return Bias + (RandomFloat4(iInstance, RandomSeedOffset) * Scale); }
	FLinearColor	RandomScaleBiasFloat(uint32 iInstance, uint32 RandomSeedOffset, const FLinearColor& Scale, const FLinearColor& Bias) const { return Bias + (RandomFloat4(iInstance, RandomSeedOffset) * Scale); }

	float			RandomScaleBiasFloat(uint32 iInstance, uint32 RandomSeedOffset, const float Scale, const float Bias, bool bUniform) const { return RandomScaleBiasFloat(iInstance, RandomSeedOffset, Scale, Bias); }
	FVector2f		RandomScaleBiasFloat(uint32 iInstance, uint32 RandomSeedOffset, const FVector2f& Scale, const FVector2f& Bias, bool bUniform) const { const FVector2f Random = RandomScaleBiasFloat(iInstance, RandomSeedOffset, Scale, Bias); return bUniform ? FVector2f(Random.X, Random.X) : Random; }
	FVector3f		RandomScaleBiasFloat(uint32 iInstance, uint32 RandomSeedOffset, const FVector3f& Scale, const FVector3f& Bias, bool bUniform) const { const FVector3f Random = RandomScaleBiasFloat(iInstance, RandomSeedOffset, Scale, Bias); return bUniform ? FVector3f(Random.X, Random.X, Random.X) : Random; }
	FVector4f		RandomScaleBiasFloat(uint32 iInstance, uint32 RandomSeedOffset, const FVector4f& Scale, const FVector4f& Bias, bool bUniform) const { const FVector4f Random = RandomScaleBiasFloat(iInstance, RandomSeedOffset, Scale, Bias); return bUniform ? FVector4f(Random.X, Random.X, Random.X) : Random; }
	FLinearColor	RandomScaleBiasFloat(uint32 iInstance, uint32 RandomSeedOffset, const FLinearColor& Scale, const FLinearColor& Bias, bool bUniform) const { const FLinearColor Random = RandomScaleBiasFloat(iInstance, RandomSeedOffset, Scale, Bias); return bUniform ? FLinearColor(Random.R, Random.R, Random.R, Random.R) : Random; }

	FVector2f		RandomUnitFloat2(uint32 iInstance, uint32 RandomSeedOffset) const { return SafeNormalize(RandomFloat2(iInstance, RandomSeedOffset) - 0.5f); }
	FVector3f		RandomUnitFloat3(uint32 iInstance, uint32 RandomSeedOffset) const { return SafeNormalize(RandomFloat3(iInstance, RandomSeedOffset) - 0.5f); }

	FVector2f		SafeNormalize(const FVector2f& v, const FVector2f& Fallback) const { const float l2 = v.SquaredLength(); return l2 < UE_KINDA_SMALL_NUMBER ? Fallback : v * (1.0f / FMath::Sqrt(l2)); }
	FVector3f		SafeNormalize(const FVector3f& v, const FVector3f& Fallback) const { const float l2 = v.SquaredLength(); return l2 < UE_KINDA_SMALL_NUMBER ? Fallback : v * (1.0f / FMath::Sqrt(l2)); }
	FVector2f		SafeNormalize(const FVector2f& v) const { return SafeNormalize(v, FVector2f(1.0f, 0.0f)); }
	FVector3f		SafeNormalize(const FVector3f& v) const { return SafeNormalize(v, FVector3f(1.0f, 0.0f, 0.0f)); }

	void			ConvertRangeToScaleBias(const FNiagaraStatelessRangeFloat&   Range, float& OutScale, float& OutBias) const { OutScale = Range.ParameterOffset == INDEX_NONE ? Range.GetScale() : 0.0f; OutBias = Range.ParameterOffset == INDEX_NONE ? Range.Min : GetParameterBufferValue<float>(Range.ParameterOffset, 0); }
	void			ConvertRangeToScaleBias(const FNiagaraStatelessRangeVector2& Range, FVector2f& OutScale, FVector2f& OutBias) const { OutScale = Range.ParameterOffset == INDEX_NONE ? Range.GetScale() : FVector2f::ZeroVector; OutBias = Range.ParameterOffset == INDEX_NONE ? Range.Min : GetParameterBufferValue<FVector2f>(Range.ParameterOffset, 0); }
	void			ConvertRangeToScaleBias(const FNiagaraStatelessRangeVector3& Range, FVector3f& OutScale, FVector3f& OutBias) const { OutScale = Range.ParameterOffset == INDEX_NONE ? Range.GetScale() : FVector3f::ZeroVector; OutBias = Range.ParameterOffset == INDEX_NONE ? Range.Min : GetParameterBufferValue<FVector3f>(Range.ParameterOffset, 0); }
	void			ConvertRangeToScaleBias(const FNiagaraStatelessRangeVector4& Range, FVector4f& OutScale, FVector4f& OutBias) const { OutScale = Range.ParameterOffset == INDEX_NONE ? Range.GetScale() : FVector4f::Zero(); OutBias = Range.ParameterOffset == INDEX_NONE ? Range.Min : GetParameterBufferValue<FVector4f>(Range.ParameterOffset, 0); }
	void			ConvertRangeToScaleBias(const FNiagaraStatelessRangeColor&   Range, FLinearColor& OutScale, FLinearColor& OutBias) const { OutScale = Range.ParameterOffset == INDEX_NONE ? Range.GetScale() : FLinearColor(0.0f, 0.0f, 0.0f, 0.0f); OutBias = Range.ParameterOffset == INDEX_NONE ? Range.Min : GetParameterBufferValue<FLinearColor>(Range.ParameterOffset, 0); }

	template<typename T>
	const T* ReadBuiltData() const
	{
		const int32 Offset = Align(BuiltDataOffset, alignof(T));
		BuiltDataOffset = Offset + sizeof(T);
		check(BuiltDataOffset <= BuiltData.Num());
		return reinterpret_cast<const T*>(BuiltData.GetData() + Offset);
	}

	template<typename T>
	const T* ReadParameterNestedStruct() const
	{
		const uint32 Offset = Align(ShaderParameterOffset, TShaderParameterStructTypeInfo<T>::Alignment);
		ShaderParameterOffset = Offset + TShaderParameterStructTypeInfo<T>::GetStructMetadata()->GetSize();
		return reinterpret_cast<const T*>(ShaderParametersData + Offset);
	}

	static FQuat4f RotatorToQuat(FVector3f Rotator)
	{
		Rotator.X = FMath::Fractional(Rotator.X) * UE_PI;
		Rotator.Y = FMath::Fractional(Rotator.Y) * UE_PI;
		Rotator.Z = FMath::Fractional(Rotator.Z) * UE_PI;

		float SP, CP, SY, CY, SR, CR;
		FMath::SinCos(&SR, &CR, Rotator.X);
		FMath::SinCos(&SP, &CP, Rotator.Y);
		FMath::SinCos(&SY, &CY, Rotator.Z);

		return FQuat4f(
			 CR * SP * SY - SR * CP * CY,
			-CR * SP * CY - SR * CP * SY,
			 CR * CP * SY - SR * SP * CY,
			 CR * CP * CY + SR * SP * SY
		);
	}

private:
	const FNiagaraStatelessEmitterData*		EmitterData = nullptr;
	uint32									NumInstances = 0;
	float									DeltaTime = 0.0f;
	float									InvDeltaTime = 0.0f;

	uint32									EmitterRandomSeed = 0;
	uint32									ModuleRandomSeed = 0;

	uint32									BufferStride = 0;
	uint8*									BufferFloatData = nullptr;
	uint8*									BufferInt32Data = nullptr;

	void*									RequiredComponents[int32(EParticleComponent::Num)] = {};
	TArray<uint8*, TInlineAllocator<16>>	VariableComponents;

	const TConstArrayView<uint8>			BuiltData;
	mutable int32							BuiltDataOffset = 0;
	const uint8*							ShaderParametersData = nullptr;
	mutable int32							ShaderParameterOffset = 0;
	const TConstArrayView<float>			StaticFloatData;
	const TConstArrayView<uint8>			DynamicBufferData;
};

template<typename TType>
struct FStatelessDistributionSampler
{
	explicit FStatelessDistributionSampler(const FParticleSimulationContext& ParticleSimulationContext, const FNiagaraStatelessBuiltDistributionType& InBuiltDistribution, int32 iInstance, uint32 RandomSeedOffset)
		: BuiltDistribution(InBuiltDistribution)
	{
		if (FNiagaraStatelessBuiltDistribution::IsRandom(BuiltDistribution))
		{
			RandomOffset = ParticleSimulationContext.TRandomFloat<TType>(iInstance, RandomSeedOffset);
		}
	}

	bool IsValid() const { return FNiagaraStatelessBuiltDistribution::IsValid(BuiltDistribution); }

	TType GetValue(const FParticleSimulationContext& ParticleSimulationContext, float Time) const
	{
		const uint32 DataOffset = FNiagaraStatelessBuiltDistribution::GetDataOffset(BuiltDistribution);
		if (FNiagaraStatelessBuiltDistribution::IsBinding(BuiltDistribution))
		{
			return ParticleSimulationContext.GetParameterBufferFloat<TType>(DataOffset, 0);
		}
		else if (FNiagaraStatelessBuiltDistribution::IsRandom(BuiltDistribution))
		{
			//-OPT: Could move into constructor
			const TType	Value0 = ParticleSimulationContext.GetStaticFloat<TType>(DataOffset, 0);
			const TType	Value1 = ParticleSimulationContext.GetStaticFloat<TType>(DataOffset, 1);
			return ParticleSimulationContext.Lerp(Value0, Value1, RandomOffset, FNiagaraStatelessBuiltDistribution::IsUniform(BuiltDistribution));
		}
		else
		{
			const float Offset = FNiagaraStatelessBuiltDistribution::ConvertTimeToLookup(BuiltDistribution, Time);
			const TType	Value0 = ParticleSimulationContext.GetStaticFloat<TType>(DataOffset, FMath::FloorToInt(Offset));
			const TType	Value1 = ParticleSimulationContext.GetStaticFloat<TType>(DataOffset, FMath::CeilToInt(Offset));
			return FMath::Lerp(Value0, Value1, FMath::Fractional(Offset));
		}
	}

	const FNiagaraStatelessBuiltDistributionType	BuiltDistribution;
	TType											RandomOffset = {};
};

} //namespace NiagaraStateless
