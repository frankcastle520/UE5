// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Stateless/NiagaraStatelessModule.h"
#include "Stateless/NiagaraStatelessEmitterDataBuildContext.h"
#include "Stateless/NiagaraStatelessModuleShaderParameters.h"
#include "Stateless/NiagaraStatelessParticleSimContext.h"

#include "NiagaraStatelessModule_SubUVAnimation.generated.h"

UENUM()
enum class ENSMSubUVAnimation_Mode
{
	InfiniteLoop,
	Linear,
	Random,
};

// Sets the sub image frame index value based on the select animation mode
// The sub image index is a float value where the fractional part can be used to blend frames together
UCLASS(MinimalAPI, EditInlineNew, meta = (DisplayName = "Sub UV Animation"))
class UNiagaraStatelessModule_SubUVAnimation : public UNiagaraStatelessModule
{
	GENERATED_BODY()

	struct FModuleBuiltData
	{
		int32	Mode					= 0;
		float	NumFrames				= 0.0f;
		float	InitialFrameScale		= 0.0f;
		float	InitialFrameBias		= 0.0f;
		float	InitialFrameRateChange	= 0.0f;
		float	AnimFrameStart			= 0.0f;
		float	AnimFrameRange			= 0.0f;
		float	RateScale				= 0.0f;

		int32	SubImageIndexVariableOffset = INDEX_NONE;
	};

public:
	using FParameters = NiagaraStateless::FSubUVAnimationModule_ShaderParameters;

	UPROPERTY(EditAnywhere, Category = "Parameters")
	int32	NumFrames = 16;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	bool	bStartFrameRangeOverride_Enabled = false;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	bool	bEndFrameRangeOverride_Enabled = false;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (EditCondition = "bStartFrameRangeOverride_Enabled"))
	int32	StartFrameRangeOverride = 0;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (EditCondition = "bEndFrameRangeOverride_Enabled"))
	int32	EndFrameRangeOverride = 0;
	
	UPROPERTY(EditAnywhere, Category = "Parameters", meta=(SegmentedDisplay))
	ENSMSubUVAnimation_Mode AnimationMode = ENSMSubUVAnimation_Mode::Linear;

	//-Note: Main module has PlaybackMode (Loops / FPS) to choose between loops or frames per second
	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (EditConditionHides, EditCondition = "AnimationMode == ENSMSubUVAnimation_Mode::InfiniteLoop"))
	float LoopsPerSecond = 1.0f;

	//-Note: Main module has a few more options
	//UPROPERTY(EditAnywhere, Category = "Parameters", meta = (EditConditionHides, EditCondition = "AnimationMode == ENSMSubUVAnimation_Mode::Linear"))
	//bool bRandomStartFrame = false;
	//int32 StartFrameOffset = 0;
	//float LoopupIndexScale = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (EditConditionHides, EditCondition = "AnimationMode == ENSMSubUVAnimation_Mode::Random"))
	float RandomChangeInterval = 0.1f;

	virtual void BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const override
	{
		FModuleBuiltData* BuiltData = BuildContext.AllocateBuiltData<FModuleBuiltData>();
		if (!IsModuleEnabled())
		{
			return;
		}

		const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
		BuiltData->SubImageIndexVariableOffset = BuildContext.FindParticleVariableIndex(StatelessGlobals.SubImageIndexVariable);
		if (BuiltData->SubImageIndexVariableOffset == INDEX_NONE)
		{
			return;
		}

		const float FrameRangeStart	= bStartFrameRangeOverride_Enabled ? FMath::Clamp(float(StartFrameRangeOverride) / float(NumFrames - 1), 0.0f, 1.0f) : 0.0f;
		const float FrameRangeEnd	= bEndFrameRangeOverride_Enabled   ? FMath::Clamp(float(EndFrameRangeOverride)   / float(NumFrames - 1), 0.0f, 1.0f) : 1.0f;

		BuiltData->Mode			= (int)AnimationMode;
		BuiltData->NumFrames	= float(NumFrames);
		switch (AnimationMode)
		{
			case ENSMSubUVAnimation_Mode::InfiniteLoop:
				BuiltData->InitialFrameScale		= 0.0f;
				BuiltData->InitialFrameBias			= 0.0f;
				BuiltData->InitialFrameRateChange	= 0.0f;
				BuiltData->AnimFrameStart			= FrameRangeStart;
				BuiltData->AnimFrameRange			= FrameRangeEnd - FrameRangeStart;
				BuiltData->RateScale				= LoopsPerSecond;
				break;

			case ENSMSubUVAnimation_Mode::Linear:
				BuiltData->InitialFrameScale		= 0.0f;
				BuiltData->InitialFrameBias			= 0.0f;
				BuiltData->InitialFrameRateChange	= 0.0f;
				BuiltData->AnimFrameStart			= FrameRangeStart;
				BuiltData->AnimFrameRange			= FrameRangeEnd - FrameRangeStart;
				BuiltData->RateScale				= 1.0f;
				break;

			case ENSMSubUVAnimation_Mode::Random:
				BuiltData->InitialFrameScale		= FrameRangeEnd - FrameRangeStart;
				BuiltData->InitialFrameBias			= FrameRangeStart;
				BuiltData->InitialFrameRateChange	= RandomChangeInterval > 0.0f ? 1.0f / RandomChangeInterval : 0.0f;
				BuiltData->AnimFrameStart			= 0.0f;
				BuiltData->AnimFrameRange			= 0.0f;
				BuiltData->RateScale				= 0.0f;
				break;
		}

		BuildContext.AddParticleSimulationExecSimulate(&UNiagaraStatelessModule_SubUVAnimation::ParticleSimulate);
	}

	virtual void BuildShaderParameters(FNiagaraStatelessShaderParametersBuilder& ShaderParametersBuilder) const override
	{
		ShaderParametersBuilder.AddParameterNestedStruct<FParameters>();
	}

	virtual void SetShaderParameters(const FNiagaraStatelessSetShaderParameterContext& SetShaderParameterContext) const override
	{
		FParameters* Parameters = SetShaderParameterContext.GetParameterNestedStruct<FParameters>();
		const FModuleBuiltData* ModuleBuiltData = SetShaderParameterContext.ReadBuiltData<FModuleBuiltData>();

		Parameters->SubUVAnimation_Mode						= ModuleBuiltData->Mode;
		Parameters->SubUVAnimation_NumFrames				= ModuleBuiltData->NumFrames;
		Parameters->SubUVAnimation_InitialFrameScale		= ModuleBuiltData->InitialFrameScale;
		Parameters->SubUVAnimation_InitialFrameBias			= ModuleBuiltData->InitialFrameBias;
		Parameters->SubUVAnimation_InitialFrameRateChange	= ModuleBuiltData->InitialFrameRateChange;
		Parameters->SubUVAnimation_AnimFrameStart			= ModuleBuiltData->AnimFrameStart;
		Parameters->SubUVAnimation_AnimFrameRange			= ModuleBuiltData->AnimFrameRange;
		Parameters->SubUVAnimation_RateScale				= ModuleBuiltData->RateScale;
	}

	static void ParticleSimulate(const NiagaraStateless::FParticleSimulationContext& ParticleSimulationContext)
	{
		using namespace NiagaraStateless;

		const FModuleBuiltData* ModuleBuiltData = ParticleSimulationContext.ReadBuiltData<FModuleBuiltData>();
		const float* ParticleAges = ParticleSimulationContext.GetParticleAge();
		const float* ParticleNormalizedAges = ParticleSimulationContext.GetParticleNormalizedAge();

		for (uint32 i = 0; i < ParticleSimulationContext.GetNumInstances(); ++i)
		{
			const float Particle_Age = ParticleAges[i];
			const float Particle_NormalizedAge = ParticleNormalizedAges[i];

			const uint32 SeedOffset = uint32(Particle_Age * ModuleBuiltData->InitialFrameRateChange);
			float Frame = ParticleSimulationContext.RandomFloat(i, SeedOffset) * ModuleBuiltData->InitialFrameScale + ModuleBuiltData->InitialFrameBias;

			if (ModuleBuiltData->Mode == 0)
			{
				const float Interp = Particle_Age * ModuleBuiltData->RateScale;
				Frame = FMath::Fractional(Frame + ModuleBuiltData->AnimFrameStart + (Interp * ModuleBuiltData->AnimFrameRange));
			}
			else if (ModuleBuiltData->Mode == 1)
			{
				const float Interp = Particle_NormalizedAge * ModuleBuiltData->RateScale;
				Frame = FMath::Clamp(Frame + ModuleBuiltData->AnimFrameStart + (Interp * ModuleBuiltData->AnimFrameRange), 0.0f, 1.0f);
			}
			const float SubImageIndex = Frame * ModuleBuiltData->NumFrames;

			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->SubImageIndexVariableOffset, i, SubImageIndex);
		}
	}

#if WITH_EDITOR
	virtual bool CanDisableModule() const override { return true; }
#endif
#if WITH_EDITORONLY_DATA
	virtual void GetOutputVariables(TArray<FNiagaraVariableBase>& OutVariables) const override
	{
		const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
		OutVariables.AddUnique(StatelessGlobals.SubImageIndexVariable);
	}
#endif
};
