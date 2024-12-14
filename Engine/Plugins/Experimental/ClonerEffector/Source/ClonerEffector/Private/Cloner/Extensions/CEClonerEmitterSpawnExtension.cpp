// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cloner/Extensions/CEClonerEmitterSpawnExtension.h"

#include "Cloner/CEClonerComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraUserRedirectionParameterStore.h"

void UCEClonerEmitterSpawnExtension::SetSpawnLoopMode(ECEClonerSpawnLoopMode InMode)
{
	if (SpawnLoopMode == InMode)
	{
		return;
	}

	SpawnLoopMode = InMode;
	MarkExtensionDirty();
}

void UCEClonerEmitterSpawnExtension::SetSpawnLoopIterations(int32 InIterations)
{
	if (SpawnLoopIterations == InIterations)
	{
		return;
	}

	if (InIterations < 1)
	{
		return;
	}

	SpawnLoopIterations = InIterations;
	MarkExtensionDirty();
}

void UCEClonerEmitterSpawnExtension::SetSpawnLoopInterval(float InInterval)
{
	if (SpawnLoopInterval == InInterval)
	{
		return;
	}

	if (InInterval < 0.f)
	{
		return;
	}

	SpawnLoopInterval = InInterval;
	MarkExtensionDirty();
}

void UCEClonerEmitterSpawnExtension::SetSpawnBehaviorMode(ECEClonerSpawnBehaviorMode InMode)
{
	if (SpawnBehaviorMode == InMode)
	{
		return;
	}

	SpawnBehaviorMode = InMode;
	MarkExtensionDirty();
}

void UCEClonerEmitterSpawnExtension::SetSpawnRate(float InRate)
{
	if (SpawnRate == InRate)
	{
		return;
	}

	if (InRate < 0)
	{
		return;
	}

	SpawnRate = InRate;
	MarkExtensionDirty();
}

void UCEClonerEmitterSpawnExtension::OnExtensionParametersChanged(UCEClonerComponent* InComponent)
{
	Super::OnExtensionParametersChanged(InComponent);

	FNiagaraUserRedirectionParameterStore& ExposedParameters = InComponent->GetOverrideParameters();

	const FNiagaraVariable SpawnLoopModeVar(FNiagaraTypeDefinition(StaticEnum<ECEClonerSpawnLoopMode>()), TEXT("SpawnLoopMode"));
	ExposedParameters.SetParameterValue<int32>(static_cast<int32>(SpawnLoopMode), SpawnLoopModeVar);

	const ECEClonerSpawnBehaviorMode BehaviorMode = SpawnLoopMode == ECEClonerSpawnLoopMode::Once ? ECEClonerSpawnBehaviorMode::Instant : SpawnBehaviorMode;
	const FNiagaraVariable SpawnBehaviorModeVar(FNiagaraTypeDefinition(StaticEnum<ECEClonerSpawnBehaviorMode>()), TEXT("SpawnBehaviorMode"));
	ExposedParameters.SetParameterValue<int32>(static_cast<int32>(BehaviorMode), SpawnBehaviorModeVar);

	InComponent->SetFloatParameter(TEXT("SpawnLoopInterval"), SpawnLoopInterval);

	InComponent->SetIntParameter(TEXT("SpawnLoopIterations"), SpawnLoopIterations);

	InComponent->SetFloatParameter(TEXT("SpawnRate"), SpawnRate);
}

#if WITH_EDITOR
const TCEPropertyChangeDispatcher<UCEClonerEmitterSpawnExtension> UCEClonerEmitterSpawnExtension::PropertyChangeDispatcher =
{
	/** Spawn */
	{ GET_MEMBER_NAME_CHECKED(UCEClonerEmitterSpawnExtension, SpawnLoopMode), &UCEClonerEmitterSpawnExtension::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerEmitterSpawnExtension, SpawnLoopInterval), &UCEClonerEmitterSpawnExtension::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerEmitterSpawnExtension, SpawnLoopIterations), &UCEClonerEmitterSpawnExtension::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerEmitterSpawnExtension, SpawnBehaviorMode), &UCEClonerEmitterSpawnExtension::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerEmitterSpawnExtension, SpawnRate), &UCEClonerEmitterSpawnExtension::OnExtensionPropertyChanged },
};

void UCEClonerEmitterSpawnExtension::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	PropertyChangeDispatcher.OnPropertyChanged(this, InPropertyChangedEvent);
}
#endif
