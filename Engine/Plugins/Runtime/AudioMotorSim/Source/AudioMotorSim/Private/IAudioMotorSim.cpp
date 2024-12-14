// Copyright Epic Games, Inc. All Rights Reserved.

#include "IAudioMotorSim.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IAudioMotorSim)

UAudioMotorSim::UAudioMotorSim(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UAudioMotorSimComponent::UAudioMotorSimComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UAudioMotorSimComponent::BeginPlay()
{
	Super::BeginPlay();

	static const FName UpdateFunctionName(TEXT("BP_Update"));
	if (!GetClass()->IsFunctionImplementedInScript(UpdateFunctionName))
	{
		bUpdateImplemented = false;
	}
}

void UAudioMotorSimComponent::Update(FAudioMotorSimInputContext& Input, FAudioMotorSimRuntimeContext& RuntimeInfo)
{
	QUICK_SCOPE_CYCLE_COUNTER(UAudioMotorSimComponent_Update);

	if (bEnabled && bUpdateImplemented)
	{
		BP_Update(Input, RuntimeInfo);
	}

#if WITH_EDITORONLY_DATA
	CachedInput = Input;
	CachedRuntimeInfo = RuntimeInfo;
#endif
}

void UAudioMotorSimComponent::Reset()
{
	BP_Reset();

#if WITH_EDITORONLY_DATA
	CachedInput = FAudioMotorSimInputContext();
	CachedRuntimeInfo = FAudioMotorSimRuntimeContext();
#endif
}

void UAudioMotorSimComponent::SetEnabled(bool bNewEnabled)
{
	bEnabled = bNewEnabled;
}

#if WITH_EDITORONLY_DATA
void UAudioMotorSimComponent::GetCachedData(FAudioMotorSimInputContext& OutInput, FAudioMotorSimRuntimeContext& OutRuntimeInfo)
{
	OutInput = CachedInput;
	OutRuntimeInfo = CachedRuntimeInfo;
}
#endif
