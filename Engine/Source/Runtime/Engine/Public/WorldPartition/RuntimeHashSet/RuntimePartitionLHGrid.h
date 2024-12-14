// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WorldGridPreviewer.h"
#include "WorldPartition/RuntimeHashSet/RuntimePartition.h"
#include "RuntimePartitionLHGrid.generated.h"

namespace UE::Private::WorldPartition
{
	struct FStreamingDescriptor;
};

UCLASS()
class ENGINE_API URuntimePartitionLHGrid : public URuntimePartition
{
	GENERATED_BODY()

	friend class UWorldPartitionRuntimeHashSet;
	friend class UWorldPartitionRuntimeSpatialHash;
	friend struct UE::Private::WorldPartition::FStreamingDescriptor;

public:
#if WITH_EDITOR
	//~ Begin UObject Interface.
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PreEditChange(FProperty* InPropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
	//~ End UObject Interface.

	//~ Begin URuntimePartition interface
	virtual bool SupportsHLODs() const override { return true; }
	virtual void InitHLODRuntimePartitionFrom(const URuntimePartition* InRuntimePartition, int32 InHLODIndex) override;
	virtual void UpdateHLODRuntimePartitionFrom(const URuntimePartition* InRuntimePartition) override;
	virtual void SetDefaultValues() override;
#endif
	virtual bool IsValidPartitionTokens(const TArray<FName>& InPartitionTokens) const override;
#if WITH_EDITOR
	virtual bool GenerateStreaming(const FGenerateStreamingParams& InParams, FGenerateStreamingResult& OutResult) override;
	virtual FArchive& AppendCellGuid(FArchive& InAr) override;
	//~ End URuntimePartition interface
#endif

private:
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = RuntimeSettings)
	uint32 CellSize = 25600;

	UPROPERTY(EditAnywhere, Category = RuntimeSettings, meta=(EditCondition="HLODIndex == INDEX_NONE", EditConditionHides))
	FVector Origin = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = RuntimeSettings, meta=(EditCondition="HLODIndex == INDEX_NONE", EditConditionHides))
	bool bIs2D = false;

	UPROPERTY(EditAnywhere, Category = RuntimeSettings, Transient, SkipSerialization)
	bool bShowGridPreview = false;
#endif

#if WITH_EDITOR
	TUniquePtr<FWorldGridPreviewer> WorldGridPreviewer;
#endif
};