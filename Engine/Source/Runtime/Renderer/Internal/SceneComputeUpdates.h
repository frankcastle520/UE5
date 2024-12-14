// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphBlackboard.h"
#include "GPUSceneWriter.h"

class FPrimitiveSceneInfo;
class FScene;

/**
 * Experimental interface that is able to submit updates to modify GPU-scene. 
 * Note: This interface is subject to change without deprecation.
 */
class ISceneComputeUpdates
{
public:
	virtual ~ISceneComputeUpdates() = default;

	/**
	 * Enqueue an update to the FScene that signals that we will modify the GPU-Scene data for this primitive. 
	 * The CPU-side logic will assume all instances are changed and perform appropriate invalidations.
	 * The delegate will be invoked during the GPU-Scene update.
	 */
	void EnqueueUpdate(FPrimitiveSceneInfo* PrimitiveSceneInfo, FGPUSceneWriteDelegate&& DataWriterGPU)
	{
		EnqueueUpdateInternal(PrimitiveSceneInfo, MoveTemp(DataWriterGPU));
	}

	virtual void SetScene(FScene* InScene) = 0;

protected:
	virtual void EnqueueUpdateInternal(FPrimitiveSceneInfo* PrimitiveSceneInfo, FGPUSceneWriteDelegate&& DataWriterGPU) = 0;
};

/**
 * Container for interface pointer, used to store the interface in the render graph builder blackboard so that it can only
 * be obtained in the same builder as the scene update.
 * Note: This interface is subject to change without deprecation.
 */
class FSceneComputeUpdatesBlackboardEntry
{
public:
	ISceneComputeUpdates* SceneComputeUpdates = nullptr;
};

RDG_REGISTER_BLACKBOARD_STRUCT(FSceneComputeUpdatesBlackboardEntry);
