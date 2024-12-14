// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Stateless/NiagaraStatelessCommon.h"
#include "Stateless/NiagaraStatelessModule.h"
#include "Stateless/NiagaraStatelessEmitterDataBuildContext.h"
#include "Stateless/NiagaraStatelessModuleShaderParameters.h"
#include "Stateless/NiagaraStatelessParticleSimContext.h"

#include "NiagaraStatelessModule_ScaleSpriteSize.generated.h"

// Multiply Particle.SpriteSize by the module calculated scale value
// This can be a constant, random or curve indexed by Particle.NormalizedAge
UCLASS(MinimalAPI, EditInlineNew, meta = (DisplayName = "Scale Sprite Size"))
class UNiagaraStatelessModule_ScaleSpriteSize : public UNiagaraStatelessModule
{
	GENERATED_BODY()

	struct FModuleBuiltData
	{
		FUintVector3	DistributionParameters = FUintVector3::ZeroValue;
		FVector2f		CurveScale = FVector2f::One();
		int32			CurveScaleOffset = INDEX_NONE;

		int32			SpriteSizeVariableOffset = INDEX_NONE;
		int32			PreviousSpriteSizeVariableOffset = INDEX_NONE;
	};

public:
	using FParameters = NiagaraStateless::FScaleSpriteSizeModule_ShaderParameters;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayName = "Scale"))
	FNiagaraDistributionVector2 ScaleDistribution = FNiagaraDistributionVector2(FVector2f::One());

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (EditConditionHides, EditCondition = "UseScaleCurveRange()"))
	FNiagaraParameterBindingWithValue ScaleCurveRange;

	virtual void PostInitProperties() override
	{
		Super::PostInitProperties();

	#if WITH_EDITORONLY_DATA
		if (HasAnyFlags(RF_ClassDefaultObject) == false)
		{
			ScaleCurveRange.SetUsage(ENiagaraParameterBindingUsage::NotParticle);
			ScaleCurveRange.SetAllowedTypeDefinitions({ FNiagaraTypeDefinition::GetVec2Def() });
			ScaleCurveRange.SetDefaultParameter(FNiagaraTypeDefinition::GetVec2Def(), FVector2f::One());
		}
	#endif
	}

	virtual void BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const override
	{
		FModuleBuiltData* BuiltData = BuildContext.AllocateBuiltData<FModuleBuiltData>();

		const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
		BuiltData->SpriteSizeVariableOffset			= BuildContext.FindParticleVariableIndex(StatelessGlobals.SpriteSizeVariable);
		BuiltData->PreviousSpriteSizeVariableOffset	= BuildContext.FindParticleVariableIndex(StatelessGlobals.PreviousSpriteSizeVariable);

		const bool bAttributesUsed = (BuiltData->SpriteSizeVariableOffset != INDEX_NONE || BuiltData->PreviousSpriteSizeVariableOffset != INDEX_NONE);
		if (IsModuleEnabled() && bAttributesUsed)
		{
			BuiltData->DistributionParameters = BuildContext.AddDistribution(ScaleDistribution);
			if (UseScaleCurveRange())
			{
				BuiltData->CurveScaleOffset = BuildContext.AddRendererBinding(ScaleCurveRange.ResolvedParameter);
				BuiltData->CurveScale = ScaleCurveRange.GetDefaultValue<FVector2f>();
			}

			BuildContext.AddParticleSimulationExecSimulate(&UNiagaraStatelessModule_ScaleSpriteSize::ParticleSimulate);
		}
	}

	virtual void BuildShaderParameters(FNiagaraStatelessShaderParametersBuilder& ShaderParametersBuilder) const override
	{
		ShaderParametersBuilder.AddParameterNestedStruct<FParameters>();
	}

	virtual void SetShaderParameters(const FNiagaraStatelessSetShaderParameterContext& SetShaderParameterContext) const override
	{
		const FModuleBuiltData* ModuleBuiltData = SetShaderParameterContext.ReadBuiltData<FModuleBuiltData>();

		FParameters* Parameters						= SetShaderParameterContext.GetParameterNestedStruct<FParameters>();
		Parameters->ScaleSpriteSize_Distribution	= ModuleBuiltData->DistributionParameters;
		SetShaderParameterContext.GetRendererParameterValue(Parameters->ScaleSpriteSize_CurveScale, ModuleBuiltData->CurveScaleOffset, ModuleBuiltData->CurveScale);
	}

	static void ParticleSimulate(const NiagaraStateless::FParticleSimulationContext& ParticleSimulationContext)
	{
		using namespace NiagaraStateless;

		const FModuleBuiltData* ModuleBuiltData = ParticleSimulationContext.ReadBuiltData<FModuleBuiltData>();

		const FVector2f ScaleFactor				= ParticleSimulationContext.GetParameterBufferFloat(ModuleBuiltData->CurveScaleOffset, ModuleBuiltData->CurveScale);
		const float* NormalizedAgeData			= ParticleSimulationContext.GetParticleNormalizedAge();
		const float* PreviousNormalizedAgeData	= ParticleSimulationContext.GetParticlePreviousNormalizedAge();

		for (uint32 i = 0; i < ParticleSimulationContext.GetNumInstances(); ++i)
		{
			const FStatelessDistributionSampler<FVector2f> SpriteScaleSampler(ParticleSimulationContext, ModuleBuiltData->DistributionParameters, i, 0);

			const float NormalizedAge			= NormalizedAgeData[i];
			const float PreviousNormalizedAge	= PreviousNormalizedAgeData[i];

			const FVector2f SpriteSize			= ParticleSimulationContext.ReadParticleVariable(ModuleBuiltData->SpriteSizeVariableOffset, i, FVector2f::ZeroVector);
			const FVector2f PreviousSpriteSize	= ParticleSimulationContext.ReadParticleVariable(ModuleBuiltData->PreviousSpriteSizeVariableOffset, i, FVector2f::ZeroVector);
			const FVector2f Scale				= SpriteScaleSampler.GetValue(ParticleSimulationContext, NormalizedAge) * ScaleFactor;
			const FVector2f PreviousScale		= SpriteScaleSampler.GetValue(ParticleSimulationContext, PreviousNormalizedAge) * ScaleFactor;

			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->SpriteSizeVariableOffset, i, SpriteSize * Scale);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PreviousSpriteSizeVariableOffset, i, PreviousSpriteSize * PreviousScale);
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
		OutVariables.AddUnique(StatelessGlobals.SpriteSizeVariable);
		OutVariables.AddUnique(StatelessGlobals.PreviousSpriteSizeVariable);
	}
#endif
};
