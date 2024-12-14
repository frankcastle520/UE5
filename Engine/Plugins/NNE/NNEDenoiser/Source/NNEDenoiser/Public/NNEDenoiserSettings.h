// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettingsBackedByCVars.h"
#include "NNEDenoiserAsset.h"
#include "NNEDenoiserTemporalAsset.h"

#include "NNEDenoiserSettings.generated.h"

/** An enum to represent denoiser NNE runtime type */
UENUM()
enum EDenoiserRuntimeType : uint8
{
	CPU,
	GPU,
	RDG
};

/** Settings to select a NNE Denoiser and its runtime */
UCLASS(Config = Engine, meta = (DisplayName = "NNE Denoiser"))
class NNEDENOISER_API UNNEDenoiserSettings : public UDeveloperSettingsBackedByCVars
{
	GENERATED_BODY()

public:
	UNNEDenoiserSettings();

	virtual void PostInitProperties() override;

	/** Denoiser asset data used to create a NNE Denoiser */
	UPROPERTY(Config, EditAnywhere, Category = "NNE Denoiser", meta = (DisplayName = "Denoiser Asset", ToolTip = "Select the denoiser asset"))
	TSoftObjectPtr<UNNEDenoiserAsset> DenoiserAsset;

	/** Temporal denoiser asset data used to create a NNE Denoiser (Currently not used and therefore "hidden") */
	TSoftObjectPtr<UNNEDenoiserTemporalAsset> TemporalDenoiserAsset;

private:
	/** Runtime type used to run the NNE Denoiser model. Backed by the console variable 'NNEDenoiser.Runtime.Type'. */
	UPROPERTY(Config, EditAnywhere, Category = "NNE Denoiser", meta = (DisplayName = "Runtime Type", ToolTip = "Select a Runtime type", ConsoleVariable = "NNEDenoiser.Runtime.Type"))
	TEnumAsByte<EDenoiserRuntimeType> RuntimeType;

	/** Runtime name used to run the NNE Denoiser model. Backed by the console variable 'NNEDenoiser.Runtime.Name'. */
	UPROPERTY(Config, EditAnywhere, Category = "NNE Denoiser", meta = (DisplayName = "Runtime Name Override", ToolTip = "(Optional) Specify the Runtime name", ConsoleVariable = "NNEDenoiser.Runtime.Name"))
	FString RuntimeName;
};
