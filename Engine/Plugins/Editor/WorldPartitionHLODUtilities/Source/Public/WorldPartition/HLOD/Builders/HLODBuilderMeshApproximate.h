// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshMerge/MeshApproximationSettings.h"
#include "WorldPartition/HLOD/HLODBuilder.h"
#include "HLODBuilderMeshApproximate.generated.h"


class UMaterial;
class UMaterialInterface;


UCLASS(Blueprintable)
class WORLDPARTITIONHLODUTILITIES_API UHLODBuilderMeshApproximateSettings : public UHLODBuilderSettings
{
	GENERATED_UCLASS_BODY()

	virtual uint32 GetCRC() const override;

	/** Mesh approximation settings */
	UPROPERTY(EditAnywhere, Category = HLOD, meta=(EditInline))
	FMeshApproximationSettings MeshApproximationSettings;

	/** Material that will be used by the generated HLOD static mesh */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = HLOD, meta = (DisplayName = "HLOD Material"))
	TObjectPtr<UMaterialInterface> HLODMaterial;
};


/**
 * Build an approximated mesh using geometry from the provided actors
 */
UCLASS(HideDropdown)
class WORLDPARTITIONHLODUTILITIES_API UHLODBuilderMeshApproximate : public UHLODBuilder
{
	GENERATED_UCLASS_BODY()

public:
	virtual TSubclassOf<UHLODBuilderSettings> GetSettingsClass() const override;
	virtual TArray<UActorComponent*> Build(const FHLODBuildContext& InHLODBuildContext, const TArray<UActorComponent*>& InSourceComponents) const override;
};


#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
#include "Engine/MeshMerging.h"
#endif