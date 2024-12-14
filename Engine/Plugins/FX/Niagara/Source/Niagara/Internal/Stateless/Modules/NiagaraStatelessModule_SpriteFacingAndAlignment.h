// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Stateless/NiagaraStatelessModule.h"
#include "Stateless/NiagaraStatelessEmitterDataBuildContext.h"
#include "Stateless/NiagaraStatelessModuleShaderParameters.h"
#include "Stateless/NiagaraStatelessParticleSimContext.h"

#include "NiagaraStatelessModule_SpriteFacingAndAlignment.generated.h"

// Sets the sprite facing and alignment attributes
UCLASS(MinimalAPI, EditInlineNew, meta = (DisplayName = "Sprite Facing Alignment"))
class UNiagaraStatelessModule_SpriteFacingAndAlignment : public UNiagaraStatelessModule
{
	GENERATED_BODY()

	struct FModuleBuiltData
	{
		FVector3f	SpriteFacing	= FVector3f::XAxisVector;
		FVector3f	SpriteAlignment = FVector3f::YAxisVector;

		int32		SpriteFacingVariableOffset = INDEX_NONE;
		int32		PreviousSpriteFacingVariableOffset = INDEX_NONE;
		int32		SpriteAlignmentVariable = INDEX_NONE;
		int32		PreviousSpriteAlignmentVariable = INDEX_NONE;
	};

public:
	using FParameters = NiagaraStateless::FSpriteFacingAndAlignmentModule_ShaderParameters;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	bool bSpriteFacingEnabled = true;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	bool bSpriteAlignmentEnabled = false;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (EditCondition = "bSpriteFacingEnabled"))
	FVector3f	SpriteFacing = FVector3f::XAxisVector;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (EditCondition = "bSpriteAlignmentEnabled"))
	FVector3f	SpriteAlignment = FVector3f::YAxisVector;

	virtual void BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const override
	{
		FModuleBuiltData* BuiltData = BuildContext.AllocateBuiltData<FModuleBuiltData>();
		if (!IsModuleEnabled())
		{
			return;
		}

		const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
		if (bSpriteFacingEnabled)
		{
			BuiltData->SpriteFacingVariableOffset			= BuildContext.FindParticleVariableIndex(StatelessGlobals.SpriteFacingVariable);
			BuiltData->PreviousSpriteFacingVariableOffset	= BuildContext.FindParticleVariableIndex(StatelessGlobals.PreviousSpriteFacingVariable);
		}
		if (bSpriteAlignmentEnabled)
		{
			BuiltData->SpriteAlignmentVariable				= BuildContext.FindParticleVariableIndex(StatelessGlobals.SpriteAlignmentVariable);
			BuiltData->PreviousSpriteAlignmentVariable		= BuildContext.FindParticleVariableIndex(StatelessGlobals.PreviousSpriteAlignmentVariable);
		}

		if ((BuiltData->SpriteFacingVariableOffset == INDEX_NONE) && (BuiltData->PreviousSpriteFacingVariableOffset == INDEX_NONE) &&
			(BuiltData->SpriteAlignmentVariable == INDEX_NONE) && (BuiltData->PreviousSpriteAlignmentVariable == INDEX_NONE))
		{
			return;
		}

		BuiltData->SpriteFacing		= SpriteFacing;
		BuiltData->SpriteAlignment	= SpriteAlignment;

		BuildContext.AddParticleSimulationExecSimulate(&UNiagaraStatelessModule_SpriteFacingAndAlignment::ParticleSimulate);
	}

	virtual void BuildShaderParameters(FNiagaraStatelessShaderParametersBuilder& ShaderParametersBuilder) const override
	{
		ShaderParametersBuilder.AddParameterNestedStruct<FParameters>();
	}

	virtual void SetShaderParameters(const FNiagaraStatelessSetShaderParameterContext& SetShaderParameterContext) const override
	{
		FParameters* Parameters = SetShaderParameterContext.GetParameterNestedStruct<FParameters>();
		const FModuleBuiltData* ModuleBuiltData = SetShaderParameterContext.ReadBuiltData<FModuleBuiltData>();

		Parameters->SpriteFacingAndAlignment_SpriteFacing		= ModuleBuiltData->SpriteFacing;
		Parameters->SpriteFacingAndAlignment_SpriteAlignment	= ModuleBuiltData->SpriteAlignment;
	}

	static void ParticleSimulate(const NiagaraStateless::FParticleSimulationContext& ParticleSimulationContext)
	{
		using namespace NiagaraStateless;

		const FModuleBuiltData* ModuleBuiltData = ParticleSimulationContext.ReadBuiltData<FModuleBuiltData>();

		for (uint32 i = 0; i < ParticleSimulationContext.GetNumInstances(); ++i)
		{
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->SpriteFacingVariableOffset, i, ModuleBuiltData->SpriteFacing);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PreviousSpriteFacingVariableOffset, i, ModuleBuiltData->SpriteFacing);

			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->SpriteAlignmentVariable, i, ModuleBuiltData->SpriteAlignment);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PreviousSpriteAlignmentVariable, i, ModuleBuiltData->SpriteAlignment);
		}
	}

#if WITH_EDITOR
	virtual bool CanDisableModule() const override { return true; }
#endif
#if WITH_EDITORONLY_DATA
	virtual void GetOutputVariables(TArray<FNiagaraVariableBase>& OutVariables) const override
	{
		const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
		if (bSpriteFacingEnabled)
		{
			OutVariables.AddUnique(StatelessGlobals.SpriteFacingVariable);
			OutVariables.AddUnique(StatelessGlobals.PreviousSpriteFacingVariable);
		}
		if (bSpriteAlignmentEnabled)
		{
			OutVariables.AddUnique(StatelessGlobals.SpriteAlignmentVariable);
			OutVariables.AddUnique(StatelessGlobals.PreviousSpriteAlignmentVariable);
		}
	}
#endif
};
