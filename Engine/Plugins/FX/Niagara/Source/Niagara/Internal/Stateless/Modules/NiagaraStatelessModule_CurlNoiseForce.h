// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Stateless/NiagaraStatelessModule.h"
#include "Stateless/NiagaraStatelessEmitterDataBuildContext.h"
#include "Stateless/NiagaraStatelessModuleShaderParameters.h"
#include "Stateless/Modules/NiagaraStatelessModuleCommon.h"

#include "NiagaraStatelessModule_CurlNoiseForce.generated.h"

class UVectorField;

UENUM()
enum class ENSM_NoiseMode
{
	VectorField,
	JacobNoise,
	LUTJacob,
	LUTJacobBicubic,
};

UCLASS(MinimalAPI, EditInlineNew, Experimental, meta = (DisplayName = "Curl Noise Force"))
class UNiagaraStatelessModule_CurlNoiseForce : public UNiagaraStatelessModule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Parameters")
	float NoiseAmplitude = 200.0f;
	UPROPERTY(EditAnywhere, Category = "Parameters")
	float NoiseFrequency = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Parameters")
	ENSM_NoiseMode NoiseMode = ENSM_NoiseMode::VectorField;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (AllowedClasses = "/Script/Engine.VectorField,/Script/Engine.VolumeTexture,/Script/Engine.TextureRenderTargetVolume"))
	TObjectPtr<UObject> NoiseTexture;

	//-TODO: Add support for GPU once we settle on a feature set for this module
	virtual ENiagaraStatelessFeatureMask GetFeatureMask() const { return ENiagaraStatelessFeatureMask::ExecuteGPU; }

	virtual void BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const override;

#if WITH_EDITOR
	virtual bool CanDisableModule() const override { return true; }
#endif
};
