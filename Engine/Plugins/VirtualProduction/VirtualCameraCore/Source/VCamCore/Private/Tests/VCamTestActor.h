// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VCamBaseActor.h"
#include "VCamComponent.h"
#include "Output/VCamOutputViewport.h"
#include "VCamTestActor.generated.h"

UCLASS(NotPlaceable, NotBlueprintable)
class AVCamTestActor : public AVCamBaseActor
{
	GENERATED_BODY()
public:

	UPROPERTY()
	TObjectPtr<UVCamOutputViewport> OutputProvider1;
	
	UPROPERTY()
	TObjectPtr<UVCamOutputViewport> OutputProvider2;

	static FIntPoint DefaultOverrideResolution() { return { 600, 400 }; }
	
	AVCamTestActor(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
		UVCamOutputProviderBase* NakedOutputProvider1;
		UVCamOutputProviderBase* NakedOutputProvider2;
		
		GetVCamComponent()->AddOutputProvider(UVCamOutputViewport::StaticClass(), NakedOutputProvider1);
		GetVCamComponent()->AddOutputProvider(UVCamOutputViewport::StaticClass(), NakedOutputProvider2);
		GetVCamComponent()->SetViewportLockState(
			FVCamViewportLocker()
			.SetLockState(EVCamTargetViewportID::Viewport1, true)
			.SetLockState(EVCamTargetViewportID::Viewport2, true)
			.SetLockState(EVCamTargetViewportID::Viewport3, true)
			.SetLockState(EVCamTargetViewportID::Viewport4, true)
			);
		
		OutputProvider1 = Cast<UVCamOutputViewport>(NakedOutputProvider1);
		OutputProvider2 = Cast<UVCamOutputViewport>(NakedOutputProvider2);

		NakedOutputProvider1->bUseOverrideResolution = true;
		NakedOutputProvider2->bUseOverrideResolution = true;
		NakedOutputProvider1->OverrideResolution = DefaultOverrideResolution();
		NakedOutputProvider2->OverrideResolution = DefaultOverrideResolution();
		OutputProvider2->InitTargetViewport(EVCamTargetViewportID::Viewport1);
		OutputProvider2->InitTargetViewport(EVCamTargetViewportID::Viewport2);
	}
};