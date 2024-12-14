// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "ModularVehicleAnimationInstance.generated.h"

class UModularVehicleBaseComponent;

struct FModuleAnimationData
{
	FName BoneName;
	FRotator RotOffset;
	FVector LocOffset;
	uint16 Flags;
};

/** Proxy override for this UAnimInstance-derived class */
USTRUCT()
	struct CHAOSMODULARVEHICLEENGINE_API FModularVehicleAnimationInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

		FModularVehicleAnimationInstanceProxy()
		: FAnimInstanceProxy()
	{
	}

	FModularVehicleAnimationInstanceProxy(UAnimInstance* Instance)
		: FAnimInstanceProxy(Instance)
	{
	}

public:

	void SetModularVehicleComponent(const UModularVehicleBaseComponent* InWheeledVehicleComponent);

	/** FAnimInstanceProxy interface begin*/
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	/** FAnimInstanceProxy interface end*/

	const TArray<FModuleAnimationData>& GetModuleAnimData() const
	{
		return ModuleInstances;
	}

private:
	TArray<FModuleAnimationData> ModuleInstances;
};

UCLASS(transient)
	class CHAOSMODULARVEHICLEENGINE_API UModularVehicleAnimationInstance : public UAnimInstance
{
	GENERATED_UCLASS_BODY()

		/** Makes a montage jump to the end of a named section. */
		UFUNCTION(BlueprintCallable, Category = "Animation")
		class AModularVehicleClusterPawn* GetVehicle();

public:
	TArray<TArray<FModuleAnimationData>> ModuleData;

public:
	void SetModularVehicleComponent(const UModularVehicleBaseComponent* InWheeledVehicleComponent)
	{
		ModularVehicleComponent = InWheeledVehicleComponent;
		AnimInstanceProxy.SetModularVehicleComponent(InWheeledVehicleComponent);
	}

	const UModularVehicleBaseComponent* GetModularVehicleComponent() const
	{
		return ModularVehicleComponent;
	}

private:
	/** UAnimInstance interface begin*/
	virtual void NativeInitializeAnimation() override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
	/** UAnimInstance interface end*/

	FModularVehicleAnimationInstanceProxy AnimInstanceProxy;

	UPROPERTY(transient)
	TObjectPtr<const UModularVehicleBaseComponent> ModularVehicleComponent;
};


