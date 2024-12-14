// Copyright Epic Games, Inc. All Rights Reserved.

#include "Stateless/Modules/NiagaraStatelessModule_ScaleSpriteSizeBySpeed.h"
#include "Stateless/NiagaraStatelessParticleSimContext.h"

namespace NSMScaleSpriteSizeBySpeedPrivate
{
	struct FModuleBuiltData
	{
		FNiagaraStatelessRangeVector2	MinScaleFactor = FNiagaraStatelessRangeVector2(FVector2f::One());
		FNiagaraStatelessRangeVector2	MaxScaleFactor = FNiagaraStatelessRangeVector2(FVector2f::One());
		FNiagaraStatelessRangeFloat		VelocityNorm = FNiagaraStatelessRangeFloat(0.0f);
		FUintVector2					ScaleDistribution = FUintVector2::ZeroValue;

		int32							PositionVariableOffset = INDEX_NONE;
		int32							PreviousPositionVariableOffset = INDEX_NONE;
		int32							SpriteSizeVariableOffset = INDEX_NONE;
		int32							PreviousSpriteSizeVariableOffset = INDEX_NONE;
	};

	static void ParticleSimulate(const NiagaraStateless::FParticleSimulationContext& ParticleSimulationContext)
	{
		using namespace NiagaraStateless;

		const FModuleBuiltData* ModuleBuiltData = ParticleSimulationContext.ReadBuiltData<FModuleBuiltData>();
		const UNiagaraStatelessModule_ScaleSpriteSizeBySpeed::FParameters* Parameters = ParticleSimulationContext.ReadParameterNestedStruct<UNiagaraStatelessModule_ScaleSpriteSizeBySpeed::FParameters>();

		for (uint32 i = 0; i < ParticleSimulationContext.GetNumInstances(); ++i)
		{
			const FVector3f Position			= ParticleSimulationContext.ReadParticleVariable(ModuleBuiltData->PositionVariableOffset, i, FVector3f::OneVector);
			const FVector3f PreviousPosition	= ParticleSimulationContext.ReadParticleVariable(ModuleBuiltData->PreviousPositionVariableOffset, i, FVector3f::OneVector);
			const FVector3f Velocity			= (Position - PreviousPosition) * ParticleSimulationContext.GetInvDeltaTime();
			const float Speed					= Velocity.SquaredLength();
			const float NormSpeed				= FMath::Clamp(Speed * Parameters->ScaleSpriteSizeBySpeed_VelocityNorm, 0.0f, 1.0f);
			const float Interp					= ParticleSimulationContext.LerpStaticFloat<float>(Parameters->ScaleSpriteSizeBySpeed_ScaleDistribution, NormSpeed);
			const FVector2f	Scale				= Parameters->ScaleSpriteSizeBySpeed_ScaleFactorBias + (Parameters->ScaleSpriteSizeBySpeed_ScaleFactorScale * FMath::Clamp(Interp, 0.0f, 1.0f));

			FVector2f SpriteSize			= ParticleSimulationContext.ReadParticleVariable(ModuleBuiltData->SpriteSizeVariableOffset, i, FVector2f::One());
			FVector2f PreviousSpriteSize	= ParticleSimulationContext.ReadParticleVariable(ModuleBuiltData->PreviousSpriteSizeVariableOffset, i, FVector2f::One());

			SpriteSize			*= Scale;
			PreviousSpriteSize	*= Scale;

			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->SpriteSizeVariableOffset, i, SpriteSize);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PreviousSpriteSizeVariableOffset, i, PreviousSpriteSize);
		}
	}
}

void UNiagaraStatelessModule_ScaleSpriteSizeBySpeed::BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const
{
	using namespace NSMScaleSpriteSizeBySpeedPrivate;

	FModuleBuiltData* BuiltData = BuildContext.AllocateBuiltData<FModuleBuiltData>();
	if (!IsModuleEnabled())
	{
		return;
	}

	const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
	BuiltData->PositionVariableOffset = BuildContext.FindParticleVariableIndex(StatelessGlobals.PositionVariable);
	BuiltData->PreviousPositionVariableOffset = BuildContext.FindParticleVariableIndex(StatelessGlobals.PreviousPositionVariable);
	BuiltData->SpriteSizeVariableOffset = BuildContext.FindParticleVariableIndex(StatelessGlobals.SpriteSizeVariable);
	BuiltData->PreviousSpriteSizeVariableOffset = BuildContext.FindParticleVariableIndex(StatelessGlobals.PreviousSpriteSizeVariable);

	if (BuiltData->SpriteSizeVariableOffset == INDEX_NONE && BuiltData->PreviousSpriteSizeVariableOffset == INDEX_NONE)
	{
		return;
	}

	BuiltData->VelocityNorm = BuildContext.ConvertDistributionToRange(VelocityThreshold, DefaultVelocity);
	BuiltData->MinScaleFactor = BuildContext.ConvertDistributionToRange(MinScaleFactor, FVector2f::One());
	BuiltData->MaxScaleFactor = BuildContext.ConvertDistributionToRange(MaxScaleFactor, FVector2f::One());
	if (bSampleScaleFactorByCurve)
	{
		if (SampleFactorCurve.IsCurve() && SampleFactorCurve.Values.Num() > 1)
		{
			BuiltData->ScaleDistribution.X = BuildContext.AddStaticData(SampleFactorCurve.Values);
			BuiltData->ScaleDistribution.Y = SampleFactorCurve.Values.Num() - 1;
		}
		else
		{
			const float Value = SampleFactorCurve.Values.Num() > 0 ? SampleFactorCurve.Values[0] : 1.0f;
			const float Values[] = { Value, Value };
			BuiltData->ScaleDistribution.X = BuildContext.AddStaticData(Values);
			BuiltData->ScaleDistribution.Y = 1;
		}
	}
	else
	{
		const float Values[] = { 1.0f, 1.0f };
		BuiltData->ScaleDistribution.X = BuildContext.AddStaticData(Values);
		BuiltData->ScaleDistribution.Y = 1;
	}
	BuildContext.AddParticleSimulationExecSimulate(&ParticleSimulate);
}

void UNiagaraStatelessModule_ScaleSpriteSizeBySpeed::BuildShaderParameters(FNiagaraStatelessShaderParametersBuilder& ShaderParametersBuilder) const
{
	ShaderParametersBuilder.AddParameterNestedStruct<FParameters>();
}

void UNiagaraStatelessModule_ScaleSpriteSizeBySpeed::SetShaderParameters(const FNiagaraStatelessSetShaderParameterContext& SetShaderParameterContext) const
{
	using namespace NSMScaleSpriteSizeBySpeedPrivate;

	const FModuleBuiltData* ModuleBuiltData = SetShaderParameterContext.ReadBuiltData<FModuleBuiltData>();

	const float VelocityNorm = SetShaderParameterContext.ConvertRangeToValue(ModuleBuiltData->VelocityNorm);
	const FVector2f MinScale = SetShaderParameterContext.ConvertRangeToValue(ModuleBuiltData->MinScaleFactor);
	const FVector2f MaxScale = SetShaderParameterContext.ConvertRangeToValue(ModuleBuiltData->MaxScaleFactor);

	FParameters* Parameters = SetShaderParameterContext.GetParameterNestedStruct<FParameters>();
	Parameters->ScaleSpriteSizeBySpeed_ScaleFactorBias = MinScale;
	Parameters->ScaleSpriteSizeBySpeed_ScaleFactorScale = MaxScale - MinScale;
	Parameters->ScaleSpriteSizeBySpeed_VelocityNorm = VelocityNorm > 0.0f ? 1.0f / (VelocityNorm * VelocityNorm) : 0.0f;
	Parameters->ScaleSpriteSizeBySpeed_ScaleDistribution = ModuleBuiltData->ScaleDistribution;
}

#if WITH_EDITORONLY_DATA
void UNiagaraStatelessModule_ScaleSpriteSizeBySpeed::GetOutputVariables(TArray<FNiagaraVariableBase>& OutVariables) const
{
	const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
	OutVariables.AddUnique(StatelessGlobals.SpriteSizeVariable);
	OutVariables.AddUnique(StatelessGlobals.PreviousSpriteSizeVariable);
}
#endif
