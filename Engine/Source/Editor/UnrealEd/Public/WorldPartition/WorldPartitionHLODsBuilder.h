// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/EnumClassFlags.h"
#include "WorldPartition/WorldPartitionBuilder.h"
#include "WorldPartition/WorldPartitionBuilderHelpers.h"
#include "WorldPartitionHLODsBuilder.generated.h"

enum class EHLODBuildStep : uint8
{
	None		= 0,
	HLOD_Setup	= 1 << 0,		// Create/delete HLOD actors to populate the world.
	HLOD_Build	= 1 << 1,		// Create components/merged meshes/etc - can run on multiple machines if this step is distributed.
	HLOD_Finalize = 1 << 2,		// When performing a distributed build, this step will gather the result generated from the different machines and, optionnaly, will submit it to source control.
	HLOD_Delete = 1 << 3,		// Delete all HLOD actors from the given world.
	HLOD_Stats	= 1 << 4		// Print stats on all the HLOD actors.
};
ENUM_CLASS_FLAGS(EHLODBuildStep);

UCLASS(MinimalAPI)
class UWorldPartitionHLODsBuilder : public UWorldPartitionBuilder
{
	GENERATED_UCLASS_BODY()
public:
	// UWorldPartitionBuilder interface begin
	UNREALED_API virtual bool RequiresCommandletRendering() const override;
	virtual ELoadingMode GetLoadingMode() const override { return ELoadingMode::Custom; }
	UNREALED_API virtual bool PreWorldInitialization(UWorld* World, FPackageSourceControlHelper& PackageHelper) override;

protected:
	UNREALED_API virtual bool RunInternal(UWorld* World, const FCellInfo& InCellInfo, FPackageSourceControlHelper& PackageHelper) override;
	virtual bool CanProcessNonPartitionedWorlds() const override { return true; }
	UNREALED_API virtual bool ShouldProcessWorld(UWorld* World) const override;
	// UWorldPartitionBuilder interface end

	bool IsDistributedBuild() const { return bDistributedBuild; }
	bool IsUsingBuildManifest() const { return !BuildManifest.IsEmpty(); }
	UNREALED_API bool ValidateParams() const;

	UNREALED_API bool SetupHLODActors();
	UNREALED_API bool BuildHLODActors();
	UNREALED_API bool DeleteHLODActors();
	UNREALED_API bool SubmitHLODActors();
	UNREALED_API bool DumpStats();

	UNREALED_API bool GenerateBuildManifest(TMap<FString, int32>& FilesToBuilderMap) const;
	UNREALED_API bool GetHLODActorsToBuild(TArray<FGuid>& HLODActorsToBuild) const;

	UNREALED_API TArray<TArray<FGuid>> GetHLODWorkloads(int32 NumWorkloads) const;
	UNREALED_API bool ValidateWorkload(const TArray<FGuid>& Workload) const;

	UNREALED_API bool CopyFilesToWorkingDir(const FString& TargetDir, const FBuilderModifiedFiles& ModifiedFiles, TArray<FString>& BuildProducts);
	UNREALED_API bool CopyFilesFromWorkingDir(const FString& SourceDir);

	UNREALED_API bool ShouldRunStep(const EHLODBuildStep BuildStep) const;

	UNREALED_API bool AddBuildProducts(const TArray<FString>& BuildProducts) const;

private:
	UWorld* World;
	class UWorldPartition* WorldPartition;
	class FSourceControlHelper* SourceControlHelper;

	// Options --
	EHLODBuildStep BuildOptions;

	bool bDistributedBuild;
	bool bForceBuild;
	bool bReportOnly;
	FString BuildManifest;
	int32 BuilderIdx;
	int32 BuilderCount;
	bool bResumeBuild;
	int32 ResumeBuildIndex;
	FName HLODLayerToBuild;
	FName HLODActorToBuild;

	FString DistributedBuildWorkingDir;
	FString DistributedBuildManifest;
	
	FBuilderModifiedFiles ModifiedFiles;
};