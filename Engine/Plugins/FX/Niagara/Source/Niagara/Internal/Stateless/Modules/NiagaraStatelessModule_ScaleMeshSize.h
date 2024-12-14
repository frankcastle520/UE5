// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Stateless/NiagaraStatelessModule.h"
#include "Stateless/NiagaraStatelessEmitterDataBuildContext.h"
#include "Stateless/NiagaraStatelessModuleShaderParameters.h"
#include "Stateless/NiagaraStatelessParticleSimContext.h"

#include "NiagaraParameterBinding.h"

#include "NiagaraStatelessModule_ScaleMeshSize.generated.h"

// Multiply Particle.Scale by the module calculated scale value
// This can be a constant, random or curve indexed by Particle.NormalizedAge
UCLASS(MinimalAPI, EditInlineNew, meta = (DisplayName = "Scale Mesh Size"))
class UNiagaraStatelessModule_ScaleMeshSize : public UNiagaraStatelessModule
{
	GENERATED_BODY()

	struct FModuleBuiltData
	{
		FUintVector3	DistributionParameters = FUintVector3::ZeroValue;
		FVector3f		CurveScale = FVector3f::OneVector;
		int32			CurveScaleOffset = INDEX_NONE;

		int32			ScaleVariableOffset = INDEX_NONE;
		int32			PreviousScaleVariableOffset = INDEX_NONE;
	};

public:
	using FParameters = NiagaraStateless::FScaleMeshSizeModule_ShaderParameters;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayName = "Scale"))
	FNiagaraDistributionVector3 ScaleDistribution = FNiagaraDistributionVector3(FVector3f::OneVector);

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (EditConditionHides, EditCondition = "UseScaleCurveRange()"))
	FNiagaraParameterBindingWithValue ScaleCurveRange;

	virtual void PostInitProperties() override
	{
		Super::PostInitProperties();

	#if WITH_EDITORONLY_DATA
		if (HasAnyFlags(RF_ClassDefaultObject) == false)
		{
			ScaleCurveRange.SetUsage(ENiagaraParameterBindingUsage::NotParticle);
			ScaleCurveRange.SetAllowedTypeDefinitions({ FNiagaraTypeDefinition::GetVec3Def() });
			ScaleCurveRange.SetDefaultParameter(FNiagaraTypeDefinition::GetVec3Def(), FVector3f::OneVector);
		}
	#endif
	}

	virtual void BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const override
	{
		FModuleBuiltData* BuiltData = BuildContext.AllocateBuiltData<FModuleBuiltData>();
		if (!IsModuleEnabled())
		{
			return;
		}

		const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
		BuiltData->ScaleVariableOffset = BuildContext.FindParticleVariableIndex(StatelessGlobals.ScaleVariable);
		BuiltData->PreviousScaleVariableOffset = BuildContext.FindParticleVariableIndex(StatelessGlobals.PreviousScaleVariable);

		if (BuiltData->ScaleVariableOffset == INDEX_NONE && BuiltData->PreviousScaleVariableOffset == INDEX_NONE)
		{
			return;
		}

		BuiltData->DistributionParameters = BuildContext.AddDistribution(ScaleDistribution);
		if (UseScaleCurveRange())
		{
			BuiltData->CurveScaleOffset = BuildContext.AddRendererBinding(ScaleCurveRange.ResolvedParameter);
			BuiltData->CurveScale = ScaleCurveRange.GetDefaultValue<FVector3f>();
		}
		BuildContext.AddParticleSimulationExecSimulate(&UNiagaraStatelessModule_ScaleMeshSize::ParticleSimulate);
	}

	virtual void BuildShaderParameters(FNiagaraStatelessShaderParametersBuilder& ShaderParametersBuilder) const override
	{
		ShaderParametersBuilder.AddParameterNestedStruct<FParameters>();
	}

	virtual void SetShaderParameters(const FNiagaraStatelessSetShaderParameterContext& SetShaderParameterContext) const override
	{
		const FModuleBuiltData* ModuleBuiltData = SetShaderParameterContext.ReadBuiltData<FModuleBuiltData>();

		FParameters* Parameters						= SetShaderParameterContext.GetParameterNestedStruct<FParameters>();
		Parameters->ScaleMeshSize_Distribution		= ModuleBuiltData->DistributionParameters;
		Parameters->ScaleMeshSize_CurveScale		= ModuleBuiltData->CurveScale;
		Parameters->ScaleMeshSize_CurveScaleOffset	= ModuleBuiltData->CurveScaleOffset;
	}

	static void ParticleSimulate(const NiagaraStateless::FParticleSimulationContext& ParticleSimulationContext)
	{
		using namespace NiagaraStateless;

		const FModuleBuiltData* ModuleBuiltData = ParticleSimulationContext.ReadBuiltData<FModuleBuiltData>();
		const float* NormalizedAgeData = ParticleSimulationContext.GetParticleNormalizedAge();
		const float* PreviousNormalizedAgeData = ParticleSimulationContext.GetParticlePreviousNormalizedAge();

		const FVector3f CurveScale = ParticleSimulationContext.GetParameterBufferFloat(ModuleBuiltData->CurveScaleOffset, ModuleBuiltData->CurveScale);

		for (uint32 i = 0; i < ParticleSimulationContext.GetNumInstances(); ++i)
		{
			FStatelessDistributionSampler<FVector3f> ScaleSampler(ParticleSimulationContext, ModuleBuiltData->DistributionParameters, i, 0);

			FVector3f Scale = ParticleSimulationContext.ReadParticleVariable(ModuleBuiltData->ScaleVariableOffset, i, FVector3f::OneVector);
			FVector3f PreviousScale = ParticleSimulationContext.ReadParticleVariable(ModuleBuiltData->PreviousScaleVariableOffset, i, FVector3f::OneVector);

			Scale = Scale * ScaleSampler.GetValue(ParticleSimulationContext, NormalizedAgeData[i]) * CurveScale;
			PreviousScale = PreviousScale * ScaleSampler.GetValue(ParticleSimulationContext, PreviousNormalizedAgeData[i]) * CurveScale;

			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->ScaleVariableOffset, i, Scale);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PreviousScaleVariableOffset, i, PreviousScale);
		}
	}

	UFUNCTION()
	bool UseScaleCurveRange() const { return ScaleDistribution.IsCurve(); }

#if WITH_EDITOR
	virtual bool CanDisableModule() const override { return true; }
#endif
#if WITH_EDITORONLY_DATA
	virtual void GetOutputVariables(TArray<FNiagaraVariableBase>& OutVariables) const override
	{
		const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
		OutVariables.AddUnique(StatelessGlobals.ScaleVariable);
		OutVariables.AddUnique(StatelessGlobals.PreviousScaleVariable);
	}
#endif
};
