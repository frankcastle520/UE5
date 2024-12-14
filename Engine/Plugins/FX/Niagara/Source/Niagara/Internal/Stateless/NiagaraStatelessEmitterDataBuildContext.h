// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraStatelessCommon.h"
#include "NiagaraStatelessDistribution.h"
#include "NiagaraStatelessBuiltDistribution.h"

struct FNiagaraDataSetCompiledData;
struct FNiagaraParameterBinding;
struct FNiagaraParameterBindingWithValue;
struct FNiagaraParameterStore;
namespace NiagaraStateless
{
	class FParticleSimulationContext;
	class FParticleSimulationExecData;
}

class FNiagaraStatelessEmitterDataBuildContext
{
public:
	UE_NONCOPYABLE(FNiagaraStatelessEmitterDataBuildContext);

	FNiagaraStatelessEmitterDataBuildContext(FNiagaraDataSetCompiledData& InParticleDataSet, FNiagaraParameterStore& InRendererBindings, TArray<uint8>& InBuiltData, TArray<float>& InStaticFloatData, NiagaraStateless::FParticleSimulationExecData* InParticleExecData)
		: ParticleDataSet(InParticleDataSet)
		, RendererBindings(InRendererBindings)
		, BuiltData(InBuiltData)
		, StaticFloatData(InStaticFloatData)
		, ParticleExecData(InParticleExecData)
	{
	}

	void PreModuleBuild(int32 InShaderParameterOffset);

	uint32 AddStaticData(TConstArrayView<float> FloatData) const;
	uint32 AddStaticData(TConstArrayView<FVector2f> FloatData) const;
	uint32 AddStaticData(TConstArrayView<FVector3f> FloatData) const;
	uint32 AddStaticData(TConstArrayView<FVector4f> FloatData) const;
	uint32 AddStaticData(TConstArrayView<FLinearColor> FloatData) const;

	template<typename T>
	T* AllocateBuiltData() const
	{
		static_assert(TIsTrivial<T>::Value, "Only trivial types can be used for built data");

		const int32 Offset = Align(BuiltData.Num(), alignof(T));
		BuiltData.AddZeroed(Offset + sizeof(T) - BuiltData.Num());
		void* NewData = BuiltData.GetData() + Offset;
		return new(NewData) T();
	}

	template<typename T>
	T& GetTransientBuildData() const
	{
		TUniquePtr<FTransientObject>& TransientObj = TransientBuildData.FindOrAdd(T::GetName());
		if (TransientObj.IsValid() == false)
		{
			TransientObj.Reset(new TTransientObject<T>);
		}
		return *reinterpret_cast<T*>(TransientObj->GetObject());
	}

	// Adds a binding to the renderer parameter store
	// This allows you to read the parameter data inside the simulation process
	// The returned value is INDEX_NONE is the variables is index otherwise the offset in DWORDs
	int32 AddRendererBinding(const FNiagaraVariableBase& Variable) const;
	int32 AddRendererBinding(const FNiagaraParameterBinding& Binding) const;
	int32 AddRendererBinding(const FNiagaraParameterBindingWithValue& Binding) const;

	// Adds an distribution into the LUT if enabled
	template<typename TType>
	FNiagaraStatelessBuiltDistributionType AddDistribution(ENiagaraDistributionMode Mode, TConstArrayView<TType> Values, const FVector2f& TimeRange) const
	{
		using namespace NiagaraStateless;

		FNiagaraStatelessBuiltDistributionType BuiltDistribution = FNiagaraStatelessBuiltDistribution::GetDefault();
		if (Values.Num() > 0)
		{
			switch (Mode)
			{
				case ENiagaraDistributionMode::Binding:				checkNoEntry(); break;
				case ENiagaraDistributionMode::UniformConstant:		FNiagaraStatelessBuiltDistribution::SetIsRandom(BuiltDistribution); FNiagaraStatelessBuiltDistribution::SetIsUniform(BuiltDistribution); break;
				case ENiagaraDistributionMode::NonUniformConstant:	FNiagaraStatelessBuiltDistribution::SetIsRandom(BuiltDistribution); break;
				case ENiagaraDistributionMode::UniformRange:		FNiagaraStatelessBuiltDistribution::SetIsRandom(BuiltDistribution); FNiagaraStatelessBuiltDistribution::SetIsUniform(BuiltDistribution); break;
				case ENiagaraDistributionMode::NonUniformRange:		FNiagaraStatelessBuiltDistribution::SetIsRandom(BuiltDistribution); break;
				case ENiagaraDistributionMode::UniformCurve:		FNiagaraStatelessBuiltDistribution::SetIsUniform(BuiltDistribution); break;
				case ENiagaraDistributionMode::NonUniformCurve:		break;
				default:											checkNoEntry(); break;
			}

			FNiagaraStatelessBuiltDistribution::SetLookupParameters(BuiltDistribution, AddStaticData(Values), Values.Num(), TimeRange);
		}
		return BuiltDistribution;
	}

	// Adds a distribution into the LUT if enabled and returns the packed information to send to the shader
	template<typename TDistribution>
	FNiagaraStatelessBuiltDistributionType AddDistribution(const TDistribution& Distribution) const
	{
		FNiagaraStatelessBuiltDistributionType BuiltDistribution = FNiagaraStatelessBuiltDistribution::GetDefault();
		if (Distribution.Mode == ENiagaraDistributionMode::Binding)
		{
			const int32 ParameterOffset = AddRendererBinding(Distribution.ParameterBinding);
			if (ParameterOffset >= 0)
			{
				FNiagaraStatelessBuiltDistribution::SetIsBinding(BuiltDistribution);
				FNiagaraStatelessBuiltDistribution::SetLookupParameters(BuiltDistribution, ParameterOffset);
			}
		}
		else
		{
			BuiltDistribution = AddDistribution(Distribution.Mode, MakeArrayView(Distribution.Values), Distribution.ValuesTimeRange);
		}
		return BuiltDistribution;
	}

	template<typename TRange, typename TDistribution, typename TDefaultValue>
	TRange ConvertDistributionToRangeHelper(const TDistribution& Distribution, const TDefaultValue& DefaultValue) const
	{
		TRange Range(DefaultValue);
		if (Distribution.Mode == ENiagaraDistributionMode::Binding)
		{
			Range.ParameterOffset = AddRendererBinding(Distribution.ParameterBinding);
		}
		else
		{
			Range = Distribution.CalculateRange(DefaultValue);
		}
		return Range;
	}

	FNiagaraStatelessRangeFloat   ConvertDistributionToRange(const FNiagaraDistributionFloat& Distribution, float DefaultValue) const { return ConvertDistributionToRangeHelper<FNiagaraStatelessRangeFloat>(Distribution, DefaultValue); }
	FNiagaraStatelessRangeVector2 ConvertDistributionToRange(const FNiagaraDistributionVector2& Distribution, const FVector2f& DefaultValue) const { return ConvertDistributionToRangeHelper<FNiagaraStatelessRangeVector2>(Distribution, DefaultValue); }
	FNiagaraStatelessRangeVector3 ConvertDistributionToRange(const FNiagaraDistributionVector3& Distribution, const FVector3f& DefaultValue) const { return ConvertDistributionToRangeHelper<FNiagaraStatelessRangeVector3>(Distribution, DefaultValue); }
	FNiagaraStatelessRangeColor   ConvertDistributionToRange(const FNiagaraDistributionColor& Distribution, const FLinearColor& DefaultValue) const { return ConvertDistributionToRangeHelper<FNiagaraStatelessRangeColor>(Distribution, DefaultValue); }

	FNiagaraStatelessRangeFloat   ConvertDistributionToRange(const FNiagaraDistributionRangeFloat& Distribution, float DefaultValue) const { return ConvertDistributionToRangeHelper<FNiagaraStatelessRangeFloat>(Distribution, DefaultValue); }
	FNiagaraStatelessRangeVector2 ConvertDistributionToRange(const FNiagaraDistributionRangeVector2& Distribution, const FVector2f& DefaultValue) const { return ConvertDistributionToRangeHelper<FNiagaraStatelessRangeVector2>(Distribution, DefaultValue); }
	FNiagaraStatelessRangeVector3 ConvertDistributionToRange(const FNiagaraDistributionRangeVector3& Distribution, const FVector3f& DefaultValue) const { return ConvertDistributionToRangeHelper<FNiagaraStatelessRangeVector3>(Distribution, DefaultValue); }
	FNiagaraStatelessRangeColor   ConvertDistributionToRange(const FNiagaraDistributionRangeColor& Distribution, const FLinearColor& DefaultValue) const { return ConvertDistributionToRangeHelper<FNiagaraStatelessRangeColor>(Distribution, DefaultValue); }
	FNiagaraStatelessRangeInt     ConvertDistributionToRange(const FNiagaraDistributionRangeInt& Distribution, int32 DefaultValue) const { return ConvertDistributionToRangeHelper<FNiagaraStatelessRangeInt>(Distribution, DefaultValue); }

	void AddParticleSimulationExecSimulate(TFunction<void(const NiagaraStateless::FParticleSimulationContext&)> Func) const;

	int32 FindParticleVariableIndex(const FNiagaraVariableBase& Variable) const;

private:
	FNiagaraDataSetCompiledData&					ParticleDataSet;
	FNiagaraParameterStore&							RendererBindings;
	TArray<uint8>&									BuiltData;
	TArray<float>&									StaticFloatData;
	NiagaraStateless::FParticleSimulationExecData*	ParticleExecData = nullptr;

	int32											ModuleBuiltDataOffset = 0;
	int32											ShaderParameterOffset = 0;
	int32											RandomSeedOffest = 0;

	struct FTransientObject
	{
		virtual ~FTransientObject() = default;
		virtual void* GetObject() = 0;
	};

	template <typename T>
	struct TTransientObject final : FTransientObject
	{
		template <typename... TArgs>
		FORCEINLINE TTransientObject(TArgs&&... Args) : TheObject(Forward<TArgs&&>(Args)...) {}
		virtual void* GetObject() { return &TheObject; }

		T TheObject;
	};

	mutable TMap<FName, TUniquePtr<FTransientObject>>	TransientBuildData;
};
