// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Stateless/NiagaraStatelessModule.h"
#include "Stateless/NiagaraStatelessEmitterDataBuildContext.h"
#include "Stateless/NiagaraStatelessModuleShaderParameters.h"

#include "NiagaraStatelessModule_ScaleMeshSizeBySpeed.generated.h"

// Applies a modifier to mesh scale based on the velocity of the particle
UCLASS(MinimalAPI, EditInlineNew, meta = (DisplayName = "Scale Mesh Size By Speed"))
class UNiagaraStatelessModule_ScaleMeshSizeBySpeed : public UNiagaraStatelessModule
{
	GENERATED_BODY()

	static constexpr float DefaultVelocity = 1000.0f;

public:
	using FParameters = NiagaraStateless::FScaleMeshSizeBySpeedModule_ShaderParameters;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (ClampMin = "0.01", UIMin = "0.01", DisableRangeDistribution))
	FNiagaraDistributionRangeFloat VelocityThreshold = FNiagaraDistributionRangeFloat(DefaultVelocity);

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (ClampMin = "0.01", UIMin = "0.01", DisableRangeDistribution))
	FNiagaraDistributionRangeVector3 MinScaleFactor = FNiagaraDistributionRangeVector3(FVector3f(1.0f, 1.0f, 1.0f));

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (ClampMin = "0.01", UIMin = "0.01", DisableRangeDistribution))
	FNiagaraDistributionRangeVector3 MaxScaleFactor = FNiagaraDistributionRangeVector3(FVector3f(2.0f, 2.0f, 2.0f));

	UPROPERTY(EditAnywhere, Category = "Parameters")
	bool bSampleScaleFactorByCurve = false;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisableRangeDistribution, DisableBindingDistribution, EditCondition = "bSampleScaleFactorByCurve", EditConditionHides))
	FNiagaraDistributionFloat SampleFactorCurve = FNiagaraDistributionFloat({0.0f, 1.0f});

	virtual void BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const override;
	virtual void BuildShaderParameters(FNiagaraStatelessShaderParametersBuilder& ShaderParametersBuilder) const override;
	virtual void SetShaderParameters(const FNiagaraStatelessSetShaderParameterContext& SetShaderParameterContext) const override;

#if WITH_EDITOR
	virtual bool CanDisableModule() const override { return true; }
#endif
#if WITH_EDITORONLY_DATA
	virtual void GetOutputVariables(TArray<FNiagaraVariableBase>& OutVariables) const override;
#endif
};
