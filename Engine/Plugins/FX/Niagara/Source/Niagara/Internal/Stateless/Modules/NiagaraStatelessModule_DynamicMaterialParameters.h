// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Stateless/NiagaraStatelessModule.h"
#include "Stateless/NiagaraStatelessEmitterDataBuildContext.h"
#include "Stateless/NiagaraStatelessModuleShaderParameters.h"
#include "Stateless/NiagaraStatelessParticleSimContext.h"

#include "NiagaraStatelessModule_DynamicMaterialParameters.generated.h"

USTRUCT()
struct FNiagaraStatelessDynamicParameterSet
{
	GENERATED_BODY()
		
	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	uint32 bXChannelEnabled : 1 = true;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	uint32 bYChannelEnabled : 1 = true;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	uint32 bZChannelEnabled : 1 = true;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	uint32 bWChannelEnabled : 1 = true;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayName = "X Channel", EditCondition = "bXChannelEnabled"))
	FNiagaraDistributionFloat XChannelDistribution = FNiagaraDistributionFloat(FNiagaraStatelessGlobals::GetDefaultDynamicMaterialParametersValue().X);

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayName = "Y Channel", EditCondition = "bYChannelEnabled"))
	FNiagaraDistributionFloat YChannelDistribution = FNiagaraDistributionFloat(FNiagaraStatelessGlobals::GetDefaultDynamicMaterialParametersValue().Y);

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayName = "Z Channel", EditCondition = "bZChannelEnabled"))
	FNiagaraDistributionFloat ZChannelDistribution = FNiagaraDistributionFloat(FNiagaraStatelessGlobals::GetDefaultDynamicMaterialParametersValue().Z);

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayName = "W Channel", EditCondition = "bWChannelEnabled"))
	FNiagaraDistributionFloat WChannelDistribution = FNiagaraDistributionFloat(FNiagaraStatelessGlobals::GetDefaultDynamicMaterialParametersValue().W);
};

// Write to the Dynamic Parameters that can be read in the material vertex & pixel shader vertex
UCLASS(MinimalAPI, EditInlineNew, meta = (DisplayName = "Dynamic Material Parameters"))
class UNiagaraStatelessModule_DynamicMaterialParameters : public UNiagaraStatelessModule
{
	GENERATED_BODY()

	static constexpr int32 NumParameters			= 4;
	static constexpr int32 NumChannelPerParameter	= 4;

	struct FModuleBuiltData
	{
		FModuleBuiltData()
		{
			for (FUintVector3& v : ParameterDistributions)
			{
				v = FUintVector3::ZeroValue;
			}

			for (int32& ParameterVariableOffset : ParameterVariableOffsets)
			{
				ParameterVariableOffset = INDEX_NONE;
			}
		}

		uint32			ChannelMask = 0;
		FUintVector3	ParameterDistributions[NumParameters * NumChannelPerParameter];
		int32			ParameterVariableOffsets[NumParameters];
	};

public:
	using FParameters = NiagaraStateless::FDynamicMaterialParametersModule_ShaderParameters;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	uint32 bParameter0Enabled : 1 = true;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	uint32 bParameter1Enabled : 1 = false;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	uint32 bParameter2Enabled : 1 = false;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	uint32 bParameter3Enabled : 1 = false;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (EditCondition = "bParameter0Enabled"))
	FNiagaraStatelessDynamicParameterSet Parameter0;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (EditCondition = "bParameter1Enabled"))
	FNiagaraStatelessDynamicParameterSet Parameter1;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (EditCondition = "bParameter2Enabled"))
	FNiagaraStatelessDynamicParameterSet Parameter2;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (EditCondition = "bParameter3Enabled"))
	FNiagaraStatelessDynamicParameterSet Parameter3;

	const FNiagaraStatelessDynamicParameterSet& GetParameterSet(int32 ParameterIndex) const
	{
		switch (ParameterIndex)
		{
			case 0: return Parameter0;
			case 1: return Parameter1;
			case 2: return Parameter2;
			case 3: return Parameter3;
			default: checkNoEntry(); return Parameter0;
		}
	}

	uint32 GetParameterChannelMask(int32 ParameterIndex) const
	{
		uint32 Mask = 0;
		if ((ParameterIndex == 0 && bParameter0Enabled) ||
			(ParameterIndex == 1 && bParameter1Enabled) ||
			(ParameterIndex == 2 && bParameter2Enabled) ||
			(ParameterIndex == 3 && bParameter3Enabled))
		{
			const FNiagaraStatelessDynamicParameterSet& ParameterSet = GetParameterSet(ParameterIndex);
			Mask |= ParameterSet.bXChannelEnabled ? 1 << 0 : 0;
			Mask |= ParameterSet.bYChannelEnabled ? 1 << 1 : 0;
			Mask |= ParameterSet.bZChannelEnabled ? 1 << 2 : 0;
			Mask |= ParameterSet.bWChannelEnabled ? 1 << 3 : 0;
		}
		return Mask;
	}

	const FNiagaraDistributionFloat& GetParameterChannelDistribution(int32 ParameterIndex, int32 ChannelIndex) const
	{
		const FNiagaraStatelessDynamicParameterSet& ParameterSet = GetParameterSet(ParameterIndex);
		switch (ChannelIndex)
		{
			case 0: return ParameterSet.XChannelDistribution;
			case 1: return ParameterSet.YChannelDistribution;
			case 2: return ParameterSet.ZChannelDistribution;
			case 3: return ParameterSet.WChannelDistribution;
			default: checkNoEntry(); return ParameterSet.XChannelDistribution;
		}
	}

	const FNiagaraVariableBase& GetParameterVariable(int32 ParameterIndex) const
	{
		const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
		switch (ParameterIndex)
		{
			default: checkNoEntry(); return StatelessGlobals.DynamicMaterialParameters0Variable;
			case 0: return StatelessGlobals.DynamicMaterialParameters0Variable;
			case 1: return StatelessGlobals.DynamicMaterialParameters1Variable;
			case 2: return StatelessGlobals.DynamicMaterialParameters2Variable;
			case 3: return StatelessGlobals.DynamicMaterialParameters3Variable;
		}
	}


	int32 GetRendererChannelMask() const
	{
		uint32 ChannelMask = 0;
		for (int32 iParameter = 0; iParameter < NumParameters; ++iParameter)
		{
			ChannelMask |= GetParameterChannelMask(iParameter) << (iParameter * NumChannelPerParameter);
		}
		return ChannelMask;
	}

	virtual void BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const override
	{
		FModuleBuiltData* BuiltData = BuildContext.AllocateBuiltData<FModuleBuiltData>();

		if (IsModuleEnabled())
		{
			for (int32 iParameter=0; iParameter < NumParameters; ++iParameter)
			{
				const uint32 ParameterChannelMask = GetParameterChannelMask(iParameter);
				if (ParameterChannelMask == 0)
				{
					continue;
				}

				BuiltData->ParameterVariableOffsets[iParameter] = BuildContext.FindParticleVariableIndex(GetParameterVariable(iParameter));
				if (BuiltData->ParameterVariableOffsets[iParameter] == INDEX_NONE)
				{
					continue;
				}

				BuiltData->ChannelMask |= ParameterChannelMask << (iParameter * NumChannelPerParameter);
				for (int32 iChannel = 0; iChannel < NumChannelPerParameter; ++iChannel)
				{
					BuiltData->ParameterDistributions[(iParameter * NumChannelPerParameter) + iChannel] = BuildContext.AddDistribution(GetParameterChannelDistribution(iParameter, iChannel));
				}
			}

			if (BuiltData->ChannelMask != 0)
			{
				BuildContext.AddParticleSimulationExecSimulate(&UNiagaraStatelessModule_DynamicMaterialParameters::ParticleSimulate);
			}
		}
	}

	virtual void BuildShaderParameters(FNiagaraStatelessShaderParametersBuilder& ShaderParametersBuilder) const override
	{
		ShaderParametersBuilder.AddParameterNestedStruct<FParameters>();
	}

	virtual void SetShaderParameters(const FNiagaraStatelessSetShaderParameterContext& SetShaderParameterContext) const override
	{
		const FModuleBuiltData* ModuleBuiltData = SetShaderParameterContext.ReadBuiltData<FModuleBuiltData>();

		FParameters* Parameters = SetShaderParameterContext.GetParameterNestedStruct<FParameters>();
		Parameters->DynamicMaterialParameters_ChannelMask = ModuleBuiltData->ChannelMask;
		Parameters->DynamicMaterialParameters_Parameter0X = ModuleBuiltData->ParameterDistributions[0];
		Parameters->DynamicMaterialParameters_Parameter0Y = ModuleBuiltData->ParameterDistributions[1];
		Parameters->DynamicMaterialParameters_Parameter0Z = ModuleBuiltData->ParameterDistributions[2];
		Parameters->DynamicMaterialParameters_Parameter0W = ModuleBuiltData->ParameterDistributions[3];
		Parameters->DynamicMaterialParameters_Parameter1X = ModuleBuiltData->ParameterDistributions[4];
		Parameters->DynamicMaterialParameters_Parameter1Y = ModuleBuiltData->ParameterDistributions[5];
		Parameters->DynamicMaterialParameters_Parameter1Z = ModuleBuiltData->ParameterDistributions[6];
		Parameters->DynamicMaterialParameters_Parameter1W = ModuleBuiltData->ParameterDistributions[7];
		Parameters->DynamicMaterialParameters_Parameter2X = ModuleBuiltData->ParameterDistributions[8];
		Parameters->DynamicMaterialParameters_Parameter2Y = ModuleBuiltData->ParameterDistributions[9];
		Parameters->DynamicMaterialParameters_Parameter2Z = ModuleBuiltData->ParameterDistributions[10];
		Parameters->DynamicMaterialParameters_Parameter2W = ModuleBuiltData->ParameterDistributions[11];
		Parameters->DynamicMaterialParameters_Parameter3X = ModuleBuiltData->ParameterDistributions[12];
		Parameters->DynamicMaterialParameters_Parameter3Y = ModuleBuiltData->ParameterDistributions[13];
		Parameters->DynamicMaterialParameters_Parameter3Z = ModuleBuiltData->ParameterDistributions[14];
		Parameters->DynamicMaterialParameters_Parameter3W = ModuleBuiltData->ParameterDistributions[15];
	}

	static void ParticleSimulate(const NiagaraStateless::FParticleSimulationContext& ParticleSimulationContext)
	{
		using namespace NiagaraStateless;

		const FModuleBuiltData* ModuleBuiltData = ParticleSimulationContext.ReadBuiltData<FModuleBuiltData>();
		const float* NormalizedAgeData = ParticleSimulationContext.GetParticleNormalizedAge();

		for (uint32 i = 0; i < ParticleSimulationContext.GetNumInstances(); ++i)
		{
			for (int32 iParameter = 0; iParameter < NumParameters; ++iParameter)
			{
				const uint32 ChannelMask = (ModuleBuiltData->ChannelMask >> (iParameter * NumChannelPerParameter)) & 0xf;
				if (ChannelMask == 0)
				{
					continue;
				}

				const int32 FirstChannel = iParameter * NumChannelPerParameter;
				const FStatelessDistributionSampler<float> XChannelSampler(ParticleSimulationContext, ModuleBuiltData->ParameterDistributions[FirstChannel + 0], i, FirstChannel + 0);
				const FStatelessDistributionSampler<float> YChannelSampler(ParticleSimulationContext, ModuleBuiltData->ParameterDistributions[FirstChannel + 1], i, FirstChannel + 1);
				const FStatelessDistributionSampler<float> ZChannelSampler(ParticleSimulationContext, ModuleBuiltData->ParameterDistributions[FirstChannel + 2], i, FirstChannel + 2);
				const FStatelessDistributionSampler<float> WChannelSampler(ParticleSimulationContext, ModuleBuiltData->ParameterDistributions[FirstChannel + 3], i, FirstChannel + 3);

				const float NormalizedAge = NormalizedAgeData[i];
				const FVector4f DynamicParameter(
					((ChannelMask & 0x1) != 0) ? XChannelSampler.GetValue(ParticleSimulationContext, NormalizedAge) : 0.0f,
					((ChannelMask & 0x2) != 0) ? YChannelSampler.GetValue(ParticleSimulationContext, NormalizedAge) : 0.0f,
					((ChannelMask & 0x4) != 0) ? ZChannelSampler.GetValue(ParticleSimulationContext, NormalizedAge) : 0.0f,
					((ChannelMask & 0x8) != 0) ? WChannelSampler.GetValue(ParticleSimulationContext, NormalizedAge) : 0.0f
				);

				ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->ParameterVariableOffsets[iParameter], i, DynamicParameter);
			}
		}
	}

#if WITH_EDITOR
	virtual bool CanDisableModule() const override { return true; }
#endif
#if WITH_EDITORONLY_DATA
	virtual void GetOutputVariables(TArray<FNiagaraVariableBase>& OutVariables) const override
	{
		const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();

		for (int32 i = 0; i < NumParameters; ++i)
		{
			if (GetParameterChannelMask(i) != 0)
			{
				OutVariables.AddUnique(GetParameterVariable(i));
			}
		}
	}
#endif
};
