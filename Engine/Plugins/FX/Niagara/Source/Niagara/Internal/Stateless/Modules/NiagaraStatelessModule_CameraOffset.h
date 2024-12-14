// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Stateless/NiagaraStatelessModule.h"
#include "Stateless/NiagaraStatelessEmitterDataBuildContext.h"
#include "Stateless/NiagaraStatelessModuleShaderParameters.h"
#include "Stateless/NiagaraStatelessParticleSimContext.h"

#include "NiagaraStatelessModule_CameraOffset.generated.h"

// Offset the particle along the vector between the particle and the camera.
UCLASS(MinimalAPI, EditInlineNew, meta = (DisplayName = "Camera Offset"))
class UNiagaraStatelessModule_CameraOffset : public UNiagaraStatelessModule
{
public:
	GENERATED_BODY()

	struct FModuleBuiltData
	{
		FUintVector3	DistributionParameters			= FUintVector3::ZeroValue;
		int32			CameraVariableOffset			= INDEX_NONE;
		int32			PreviousCameraVariableOffset	= INDEX_NONE;
	};

public:
	using FParameters = NiagaraStateless::FCameraOffsetModule_ShaderParameters;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayName = "CameraOffset"))
	FNiagaraDistributionFloat CameraOffsetDistribution = FNiagaraDistributionFloat(0.0f);

	virtual void BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const override
	{
		const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();

		FModuleBuiltData* BuiltData				= BuildContext.AllocateBuiltData<FModuleBuiltData>();
		BuiltData->CameraVariableOffset			= BuildContext.FindParticleVariableIndex(StatelessGlobals.CameraOffsetVariable);
		BuiltData->PreviousCameraVariableOffset	= BuildContext.FindParticleVariableIndex(StatelessGlobals.PreviousCameraOffsetVariable);

		const bool bAttributesUsed = (BuiltData->CameraVariableOffset != INDEX_NONE || BuiltData->PreviousCameraVariableOffset != INDEX_NONE);
		if (IsModuleEnabled() && bAttributesUsed)
		{
			BuiltData->DistributionParameters = BuildContext.AddDistribution(CameraOffsetDistribution);

			BuildContext.AddParticleSimulationExecSimulate(&UNiagaraStatelessModule_CameraOffset::ParticleSimulate);
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
		Parameters->CameraOffset_Distribution = ModuleBuiltData->DistributionParameters;
	}

	static void ParticleSimulate(const NiagaraStateless::FParticleSimulationContext& ParticleSimulationContext)
	{
		using namespace NiagaraStateless;

		const FModuleBuiltData* ModuleBuiltData	= ParticleSimulationContext.ReadBuiltData<FModuleBuiltData>();
		const float* NormalizedAgeData			= ParticleSimulationContext.GetParticleNormalizedAge();
		const float* PreviousNormalizedAgeData	= ParticleSimulationContext.GetParticlePreviousNormalizedAge();

		for (uint32 i = 0; i < ParticleSimulationContext.GetNumInstances(); ++i)
		{
			const FStatelessDistributionSampler<float> CameraOffsetSampler(ParticleSimulationContext, ModuleBuiltData->DistributionParameters, i, 0);

			const float NormalizedAge			= NormalizedAgeData[i];
			const float PreviousNormalizedAge	= PreviousNormalizedAgeData[i];

			const float CameraOffset			= CameraOffsetSampler.GetValue(ParticleSimulationContext, NormalizedAge);
			const float PreviousCameraOffset	= CameraOffsetSampler.GetValue(ParticleSimulationContext, PreviousNormalizedAge);

			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->CameraVariableOffset, i, CameraOffset);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PreviousCameraVariableOffset, i, PreviousCameraOffset);
		}
	}

#if WITH_EDITOR
	virtual bool CanDisableModule() const override { return true; }
#endif
#if WITH_EDITORONLY_DATA
	virtual void GetOutputVariables(TArray<FNiagaraVariableBase>& OutVariables) const override
	{
		const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
		OutVariables.AddUnique(StatelessGlobals.CameraOffsetVariable);
		OutVariables.AddUnique(StatelessGlobals.PreviousCameraOffsetVariable);
	}
#endif
};
