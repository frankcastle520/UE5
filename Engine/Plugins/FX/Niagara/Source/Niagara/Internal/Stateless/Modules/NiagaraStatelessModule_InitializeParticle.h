// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Stateless/NiagaraStatelessModule.h"
#include "Stateless/NiagaraStatelessEmitterDataBuildContext.h"
#include "Stateless/NiagaraStatelessModuleShaderParameters.h"
#include "Stateless/NiagaraStatelessParticleSimContext.h"
#include "Stateless/Modules/NiagaraStatelessModuleCommon.h"

#include "NiagaraParameterBinding.h"
#include "NiagaraParameterStore.h"

#include "NiagaraStatelessModule_InitializeParticle.generated.h"

// Initialize common particle attributes using common settings and options.
UCLASS(MinimalAPI, EditInlineNew, meta = (DisplayName = "Initialize Particle"))
class UNiagaraStatelessModule_InitializeParticle : public UNiagaraStatelessModule
{
	GENERATED_BODY()

	static const uint32 EInitializeParticleModuleFlag_UniformSpriteSize	= 1 << 0;
	static const uint32 EInitializeParticleModuleFlag_UniformMeshScale	= 1 << 1;

	struct FModuleBuiltData
	{
		uint32							ModuleFlags = 0;
		FUintVector3					InitialPosition = FUintVector3::ZeroValue;
		FNiagaraStatelessRangeFloat		LifetimeRange;
		FNiagaraStatelessRangeColor		ColorRange;
		FNiagaraStatelessRangeFloat		MassRange;
		FNiagaraStatelessRangeVector2	SpriteSizeRange;
		FNiagaraStatelessRangeFloat		SpriteRotationRange;
		FNiagaraStatelessRangeVector3	MeshScaleRange;
		FNiagaraStatelessRangeFloat		RibbonWidthRange;

		int32							PositionVariableOffset = INDEX_NONE;
		int32							ColorVariableOffset = INDEX_NONE;
		int32							RibbonWidthVariableOffset = INDEX_NONE;
		int32							SpriteSizeVariableOffset = INDEX_NONE;
		int32							SpriteRotationVariableOffset = INDEX_NONE;
		int32							ScaleVariableOffset = INDEX_NONE;

		int32							PreviousPositionVariableOffset = INDEX_NONE;
		int32							PreviousRibbonWidthVariableOffset = INDEX_NONE;
		int32							PreviousSpriteSizeVariableOffset = INDEX_NONE;
		int32							PreviousSpriteRotationVariableOffset = INDEX_NONE;
		int32							PreviousScaleVariableOffset = INDEX_NONE;
	};

public:
	using FParameters = NiagaraStateless::FInitializeParticleModule_ShaderParameters;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayName = "Lifetime", Units="s"))
	FNiagaraDistributionRangeFloat LifetimeDistribution = FNiagaraDistributionRangeFloat(FNiagaraStatelessGlobals::GetDefaultLifetimeValue());

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayName = "Color", DisableCurveDistribution))
	FNiagaraDistributionRangeColor ColorDistribution = FNiagaraDistributionRangeColor(FNiagaraStatelessGlobals::GetDefaultColorValue());

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayName = "Mass"))
	FNiagaraDistributionRangeFloat MassDistribution = FNiagaraDistributionRangeFloat(FNiagaraStatelessGlobals::GetDefaultMassValue());

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayName = "Sprite Size"))
	FNiagaraDistributionRangeVector2 SpriteSizeDistribution = FNiagaraDistributionRangeVector2(FNiagaraStatelessGlobals::GetDefaultSpriteSizeValue());

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayName = "Sprite Rotation", Units="deg"))
	FNiagaraDistributionRangeFloat SpriteRotationDistribution = FNiagaraDistributionRangeFloat(FNiagaraStatelessGlobals::GetDefaultSpriteRotationValue());

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayName = "Mesh Scale"))
	FNiagaraDistributionRangeVector3 MeshScaleDistribution = FNiagaraDistributionRangeVector3(FNiagaraStatelessGlobals::GetDefaultScaleValue());

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	bool bWriteRibbonWidth = false;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayName = "Ribbon Width", EditCondition="bWriteRibbonWidth"))
	FNiagaraDistributionRangeFloat RibbonWidthDistribution = FNiagaraDistributionRangeFloat(FNiagaraStatelessGlobals::GetDefaultRibbonWidthValue());

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisableCurveDistribution))
	FNiagaraDistributionPosition InitialPositionDistribution = FNiagaraDistributionPosition(FVector3f::ZeroVector);

	virtual void BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const override
	{
		BuildContext.AddParticleSimulationExecSimulate(&UNiagaraStatelessModule_InitializeParticle::ParticleSimulate);

		FModuleBuiltData* BuiltData			= BuildContext.AllocateBuiltData<FModuleBuiltData>();
		BuiltData->ModuleFlags				 = SpriteSizeDistribution.IsUniform() ? EInitializeParticleModuleFlag_UniformSpriteSize : 0;
		BuiltData->ModuleFlags				|= MeshScaleDistribution.IsUniform() ? EInitializeParticleModuleFlag_UniformMeshScale : 0;

		BuiltData->InitialPosition			= BuildContext.AddDistribution(InitialPositionDistribution);
		BuiltData->LifetimeRange			= LifetimeDistribution.CalculateRange(FNiagaraStatelessGlobals::GetDefaultLifetimeValue());
		BuiltData->ColorRange				= BuildContext.ConvertDistributionToRange(ColorDistribution, FNiagaraStatelessGlobals::GetDefaultColorValue());
		BuiltData->MassRange				= BuildContext.ConvertDistributionToRange(MassDistribution, FNiagaraStatelessGlobals::GetDefaultMassValue());
		BuiltData->SpriteSizeRange			= BuildContext.ConvertDistributionToRange(SpriteSizeDistribution, FNiagaraStatelessGlobals::GetDefaultSpriteSizeValue());
		BuiltData->SpriteRotationRange		= BuildContext.ConvertDistributionToRange(SpriteRotationDistribution, FNiagaraStatelessGlobals::GetDefaultSpriteRotationValue());
		BuiltData->MeshScaleRange			= BuildContext.ConvertDistributionToRange(MeshScaleDistribution, FNiagaraStatelessGlobals::GetDefaultScaleValue());
		BuiltData->RibbonWidthRange			= BuildContext.ConvertDistributionToRange(RibbonWidthDistribution, FNiagaraStatelessGlobals::GetDefaultRibbonWidthValue());

		const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
		BuiltData->PositionVariableOffset				= BuildContext.FindParticleVariableIndex(StatelessGlobals.PositionVariable);
		BuiltData->ColorVariableOffset					= BuildContext.FindParticleVariableIndex(StatelessGlobals.ColorVariable);
		BuiltData->RibbonWidthVariableOffset			= BuildContext.FindParticleVariableIndex(StatelessGlobals.RibbonWidthVariable);
		BuiltData->SpriteSizeVariableOffset				= BuildContext.FindParticleVariableIndex(StatelessGlobals.SpriteSizeVariable);
		BuiltData->SpriteRotationVariableOffset			= BuildContext.FindParticleVariableIndex(StatelessGlobals.SpriteRotationVariable);
		BuiltData->ScaleVariableOffset					= BuildContext.FindParticleVariableIndex(StatelessGlobals.ScaleVariable);
		BuiltData->PreviousPositionVariableOffset		= BuildContext.FindParticleVariableIndex(StatelessGlobals.PreviousPositionVariable);
		BuiltData->PreviousRibbonWidthVariableOffset	= BuildContext.FindParticleVariableIndex(StatelessGlobals.PreviousRibbonWidthVariable);
		BuiltData->PreviousSpriteSizeVariableOffset		= BuildContext.FindParticleVariableIndex(StatelessGlobals.PreviousSpriteSizeVariable);
		BuiltData->PreviousSpriteRotationVariableOffset	= BuildContext.FindParticleVariableIndex(StatelessGlobals.PreviousSpriteRotationVariable);
		BuiltData->PreviousScaleVariableOffset			= BuildContext.FindParticleVariableIndex(StatelessGlobals.PreviousScaleVariable);

		NiagaraStateless::FPhysicsBuildData& PhysicsBuildData = BuildContext.GetTransientBuildData<NiagaraStateless::FPhysicsBuildData>();
		PhysicsBuildData.MassRange			= MassDistribution.CalculateRange(FNiagaraStatelessGlobals::GetDefaultMassValue());
	}

	virtual void BuildShaderParameters(FNiagaraStatelessShaderParametersBuilder& ShaderParametersBuilder) const override
	{
		ShaderParametersBuilder.AddParameterNestedStruct<FParameters>();
	}

	virtual void SetShaderParameters(const FNiagaraStatelessSetShaderParameterContext& SetShaderParameterContext) const override
	{
		FParameters* Parameters = SetShaderParameterContext.GetParameterNestedStruct<FParameters>();
		const FModuleBuiltData* ModuleBuiltData = SetShaderParameterContext.ReadBuiltData<FModuleBuiltData>();

		Parameters->InitializeParticle_ModuleFlags			= ModuleBuiltData->ModuleFlags;
		Parameters->InitializeParticle_InitialPosition		= ModuleBuiltData->InitialPosition;
		SetShaderParameterContext.ConvertRangeToScaleBias(ModuleBuiltData->ColorRange,			Parameters->InitializeParticle_ColorScale, Parameters->InitializeParticle_ColorBias);
		SetShaderParameterContext.ConvertRangeToScaleBias(ModuleBuiltData->SpriteSizeRange,		Parameters->InitializeParticle_SpriteSizeScale, Parameters->InitializeParticle_SpriteSizeBias);
		SetShaderParameterContext.ConvertRangeToScaleBias(ModuleBuiltData->SpriteRotationRange, Parameters->InitializeParticle_SpriteRotationScale, Parameters->InitializeParticle_SpriteRotationBias);
		SetShaderParameterContext.ConvertRangeToScaleBias(ModuleBuiltData->MeshScaleRange,		Parameters->InitializeParticle_MeshScaleScale, Parameters->InitializeParticle_MeshScaleBias);
		SetShaderParameterContext.ConvertRangeToScaleBias(ModuleBuiltData->RibbonWidthRange,	Parameters->InitializeParticle_RibbonWidthScale, Parameters->InitializeParticle_RibbonWidthBias);
	}

	static void ParticleSimulate(const NiagaraStateless::FParticleSimulationContext& ParticleSimulationContext)
	{
		using namespace NiagaraStateless;

		const FModuleBuiltData* ModuleBuiltData = ParticleSimulationContext.ReadBuiltData<FModuleBuiltData>();
		const FParameters* ShaderParameters = ParticleSimulationContext.ReadParameterNestedStruct<FParameters>();

		const bool bUniformSpriteSize = (ModuleBuiltData->ModuleFlags & EInitializeParticleModuleFlag_UniformSpriteSize) != 0;
		const bool bUniformMeshScale  = (ModuleBuiltData->ModuleFlags & EInitializeParticleModuleFlag_UniformMeshScale) != 0;

		for (uint32 i = 0; i < ParticleSimulationContext.GetNumInstances(); ++i)
		{
			const FStatelessDistributionSampler<FVector3f> PositionSampler(ParticleSimulationContext, ModuleBuiltData->InitialPosition, i, 0);

			const FVector3f		Position	= PositionSampler.GetValue(ParticleSimulationContext, 0.0f);
			const FLinearColor	Color		= ParticleSimulationContext.RandomScaleBiasFloat(i, 1, ShaderParameters->InitializeParticle_ColorScale, ShaderParameters->InitializeParticle_ColorBias);
			const float			RibbonWidth	= ParticleSimulationContext.RandomScaleBiasFloat(i, 2, ShaderParameters->InitializeParticle_RibbonWidthScale, ShaderParameters->InitializeParticle_RibbonWidthBias);
			const FVector2f		SpriteSize	= ParticleSimulationContext.RandomScaleBiasFloat(i, 3, ShaderParameters->InitializeParticle_SpriteSizeScale, ShaderParameters->InitializeParticle_SpriteSizeBias, bUniformSpriteSize);
			const float			SpriteRot	= ParticleSimulationContext.RandomScaleBiasFloat(i, 4, ShaderParameters->InitializeParticle_SpriteRotationScale, ShaderParameters->InitializeParticle_SpriteRotationBias);
			const FVector3f		Scale		= ParticleSimulationContext.RandomScaleBiasFloat(i, 5, ShaderParameters->InitializeParticle_MeshScaleScale, ShaderParameters->InitializeParticle_MeshScaleBias, bUniformMeshScale);

			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PositionVariableOffset,				i, Position);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->ColorVariableOffset,					i, Color);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->RibbonWidthVariableOffset,				i, RibbonWidth);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->SpriteSizeVariableOffset,				i, SpriteSize);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->SpriteRotationVariableOffset,			i, SpriteRot);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->ScaleVariableOffset,					i, Scale);

			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PreviousPositionVariableOffset,		i, Position);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PreviousRibbonWidthVariableOffset,		i, RibbonWidth);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PreviousSpriteSizeVariableOffset,		i, SpriteSize);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PreviousSpriteRotationVariableOffset,	i, SpriteRot);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PreviousScaleVariableOffset,			i, Scale);
		}
	}

#if WITH_EDITORONLY_DATA
	virtual void GetOutputVariables(TArray<FNiagaraVariableBase>& OutVariables) const override
	{
		const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
		OutVariables.AddUnique(StatelessGlobals.UniqueIDVariable);
		OutVariables.AddUnique(StatelessGlobals.PositionVariable);
		OutVariables.AddUnique(StatelessGlobals.ColorVariable);
		OutVariables.AddUnique(StatelessGlobals.SpriteSizeVariable);
		OutVariables.AddUnique(StatelessGlobals.SpriteRotationVariable);
		OutVariables.AddUnique(StatelessGlobals.ScaleVariable);

		OutVariables.AddUnique(StatelessGlobals.PreviousPositionVariable);
		OutVariables.AddUnique(StatelessGlobals.PreviousSpriteSizeVariable);
		OutVariables.AddUnique(StatelessGlobals.PreviousSpriteRotationVariable);
		OutVariables.AddUnique(StatelessGlobals.PreviousScaleVariable);

		if (bWriteRibbonWidth)
		{
			OutVariables.AddUnique(StatelessGlobals.RibbonWidthVariable);
			OutVariables.AddUnique(StatelessGlobals.PreviousRibbonWidthVariable);
		}
	}
#endif
};
