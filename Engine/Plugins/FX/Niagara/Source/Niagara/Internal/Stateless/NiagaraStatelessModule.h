// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraMergeable.h"
#include "NiagaraStatelessCommon.h"
#include "NiagaraStatelessDistribution.h"
#include "NiagaraStatelessSetShaderParameterContext.h"
#include "NiagaraStatelessShaderParametersBuilder.h"

#include "NiagaraStatelessModule.generated.h"

class FNiagaraStatelessEmitterDataBuildContext;
#if WITH_EDITOR
struct FNiagaraStatelessDrawDebugContext;
#endif

UCLASS(MinimalAPI, abstract, EditInlineNew)
class UNiagaraStatelessModule : public UNiagaraMergeable
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayPriority = 0, HideInStack))
	uint32 bModuleEnabled : 1 = true;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Transient, Category = "Parameters", meta = (DisplayPriority = 0, HideInStack, StackItemHeaderIcon = "Icons.Visibility"))
	uint32 bDebugDrawEnabled : 1 = false;
#endif

public:
	bool IsModuleEnabled() const { return bModuleEnabled; }
#if WITH_EDITOR
	bool IsDebugDrawEnabled() const { return bModuleEnabled && bDebugDrawEnabled; }

	struct PrivateMemberNames
	{
		static NIAGARA_API const FName bModuleEnabled;
		static NIAGARA_API const FName bDebugDrawEnabled;
	};
#endif

	virtual ENiagaraStatelessFeatureMask GetFeatureMask() const { return ENiagaraStatelessFeatureMask::All; }

	virtual void BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const {}
	virtual void BuildShaderParameters(FNiagaraStatelessShaderParametersBuilder& ShaderParametersBuilder) const {}
	virtual void SetShaderParameters(const FNiagaraStatelessSetShaderParameterContext& SetShaderParameterContext) const {}

#if WITH_EDITOR
	virtual bool CanDisableModule() const { return false; }
	void SetIsModuleEnabled(bool bInIsEnabled) { bModuleEnabled = bInIsEnabled; }

	virtual bool CanDebugDraw() const { return false; }
	virtual void DrawDebug(const FNiagaraStatelessDrawDebugContext& DrawDebugContext) const {}
#endif

#if WITH_EDITORONLY_DATA
	virtual void GetOutputVariables(TArray<FNiagaraVariableBase>& OutVariables) const {}
#endif

	//~UObject interface Begin
#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif //WITH_EDITOR
	//~UObject interface End
};
