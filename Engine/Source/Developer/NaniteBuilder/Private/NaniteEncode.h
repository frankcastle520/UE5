// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NaniteDefinitions.h"
#include "Math/Bounds.h"

struct FMeshNaniteSettings;

namespace Nanite
{
	struct FResources;
	class FCluster;
	struct FClusterGroup;
	
	void BuildRayTracingData(FResources& Resources, TArray<FCluster>& Clusters);
	void Encode(
		FResources& Resources,
		const FMeshNaniteSettings& Settings,
		TArray<FCluster>& Clusters,
		TArray<FClusterGroup>& Groups,
		const FBounds3f& MeshBounds,
		uint32 NumMeshes,
		uint32 NumTexCoords,
		bool bHasTangents,
		bool bHasColors,
		bool bHasSkinning,
		uint32* OutTotalGPUSize
	);
}
