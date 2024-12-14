// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StaticMeshSceneProxyDesc.h"
#include "Engine/InstancedStaticMesh.h"

class UInstancedStaticMeshComponent;

struct FInstancedStaticMeshSceneProxyDesc : public FStaticMeshSceneProxyDesc
{		
	FInstancedStaticMeshSceneProxyDesc() = default;
	ENGINE_API FInstancedStaticMeshSceneProxyDesc(UInstancedStaticMeshComponent*);

	void InitializeFromInstancedStaticMeshComponent(UInstancedStaticMeshComponent*);

	UE_DEPRECATED(5.5, "Use InitializeFromInstancedStaticMeshComponent instead.")
	void InitializeFrom(UInstancedStaticMeshComponent* InComponent) { InitializeFromInstancedStaticMeshComponent(InComponent); }

	TSharedPtr<FInstanceDataSceneProxy, ESPMode::ThreadSafe> InstanceDataSceneProxy;
#if WITH_EDITOR
	bool bHasSelectedInstances = false;
#endif

	int32 InstanceStartCullDistance = 0;
	int32 InstanceEndCullDistance = 0;
	float InstanceLODDistanceScale = 1.0f;

	bool bUseGpuLodSelection = false;

};
