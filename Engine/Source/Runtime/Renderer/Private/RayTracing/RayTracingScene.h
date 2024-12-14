// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"

#if RHI_RAYTRACING

#include "Async/TaskGraphInterfaces.h"
#include "Math/DoubleFloat.h"
#include "RHI.h"
#include "RHIUtilities.h"
#include "RHIGPUReadback.h"
#include "RenderGraphResources.h"
#include "Misc/MemStack.h"
#include "Containers/ArrayView.h"
#include "MeshPassProcessor.h"
#include "RayTracingShaderBindingTable.h"
#include "RayTracingInstanceBufferUtil.h"
#include "RayTracingDebugTypes.h"

class FGPUScene;
class FRHIRayTracingScene;
class FRHIShaderResourceView;
class FRayTracingGeometry;
class FRDGBuilder;
class FPrimitiveSceneProxy;

namespace Nanite
{
	using CoarseMeshStreamingHandle = int16;
}

/**
* Persistent representation of the scene for ray tracing.
* Manages top level acceleration structure instances, memory and build process.
*/
class FRayTracingScene
{
public:

	struct FInstanceHandle
	{
		FInstanceHandle()
			: Layer(ERayTracingSceneLayer::NUM)
			, Index(UINT32_MAX)
		{}

		bool IsValid() const
		{
			return Layer < ERayTracingSceneLayer::NUM && Index != UINT32_MAX;
		}

	private:
		FInstanceHandle(ERayTracingSceneLayer InLayer, uint32 InIndex)
			: Layer(InLayer)
			, Index(InIndex)
		{}

		ERayTracingSceneLayer Layer;
		uint32 Index;

		friend class FRayTracingScene;
	};

	static const FInstanceHandle INVALID_INSTANCE_HANDLE;

	struct FInstanceRange
	{
	private:
		FInstanceRange(ERayTracingSceneLayer InLayer, uint32 InStartIndex, uint32 InNum)
			: Layer(InLayer)
			, StartIndex(InStartIndex)
			, Num(InNum)
		{}

		ERayTracingSceneLayer Layer;
		uint32 StartIndex;
		uint32 Num;

		friend class FRayTracingScene;
	};

	FRayTracingScene();
	~FRayTracingScene();

	FInstanceHandle AddInstance(FRayTracingGeometryInstance Instance, ERayTracingSceneLayer Layer, const FPrimitiveSceneProxy* Proxy = nullptr, bool bDynamic = false);

	FInstanceRange AllocateInstanceRangeUninitialized(uint32 NumInstances, ERayTracingSceneLayer Layer);

	void SetInstance(FInstanceRange InstanceRange, uint32 InstanceIndexInRange, FRayTracingGeometryInstance Instance, const FPrimitiveSceneProxy* Proxy = nullptr, bool bDynamic = false);

	// Allocates RayTracingSceneRHI and builds various metadata required to create the final scene.
	// Note: Calling this method is optional as Create() will do it if necessary. However applications may call it on async tasks to improve performance.
	void BuildInitializationData();

	// Allocates GPU memory to fit at least the current number of instances.
	// Kicks off instance buffer build to parallel thread along with RDG pass.
	void Create(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FGPUScene* GPUScene, ERDGPassFlags ComputePassFlags);

	void Build(FRDGBuilder& GraphBuilder, ERDGPassFlags ComputePassFlags, FRDGBufferRef DynamicGeometryScratchBuffer);

	// Resets the instance list and reserves memory for this frame.
	void Reset(bool bInstanceDebugDataEnabled);

	void EndFrame();

	// Allocates temporary memory that will be valid until the next Reset().
	// Can be used to store temporary instance transforms, user data, etc.
	template <typename T>
	TArrayView<T> Allocate(int32 Count)
	{
		return MakeArrayView(new(Allocator) T[Count], Count);
	}

	// Returns true if RHI ray tracing scene has been created.
	// i.e. returns true after BeginCreate() and before Reset().
	RENDERER_API bool IsCreated() const;

	// Returns RayTracingSceneRHI object (may return null).
	RENDERER_API  FRHIRayTracingScene* GetRHIRayTracingScene(ERayTracingSceneLayer Layer) const;

	// Similar to GetRayTracingScene, but checks that ray tracing scene RHI object is valid.
	RENDERER_API  FRHIRayTracingScene* GetRHIRayTracingSceneChecked(ERayTracingSceneLayer Layer) const;

	// Creates new RHI view of a layer. Can only be used on valid ray tracing scene. 
	RENDERER_API FShaderResourceViewRHIRef CreateLayerViewRHI(FRHICommandListBase& RHICmdList, ERayTracingSceneLayer Layer) const;

	// Returns RDG view of a layer. Can only be used on valid ray tracing scene.
	RENDERER_API FRDGBufferSRVRef GetLayerView(ERayTracingSceneLayer Layer) const;

	FRDGBufferRef GetInstanceBuffer(ERayTracingSceneLayer Layer) const { return Layers[uint8(Layer)].InstanceBuffer; }

	TConstArrayView<FRayTracingGeometryInstance> GetInstances(ERayTracingSceneLayer Layer) const { return MakeArrayView(Layers[uint8(Layer)].Instances); }

	FRayTracingGeometryInstance& GetInstance(FInstanceHandle Handle) { return Layers[uint8(Handle.Layer)].Instances[Handle.Index]; }

	uint32 GetTotalNumSegments() const { return NumSegments; }

	uint32 GetNumNativeInstances(ERayTracingSceneLayer Layer) const;

	FRDGBufferRef GetInstanceDebugBuffer(ERayTracingSceneLayer Layer) const { return Layers[uint8(Layer)].InstanceDebugBuffer; }
	FRDGBufferRef GetDebugInstanceGPUSceneIndexBuffer(ERayTracingSceneLayer Layer) const { return Layers[uint8(Layer)].DebugInstanceGPUSceneIndexBuffer; }

	void InitPreViewTranslation(const FViewMatrices& ViewMatrices);

public:

	// Public members for initial refactoring step (previously were public members of FViewInfo).

	uint32 NumSegments = 0;
	uint32 NumMissShaderSlots = 1; // we must have a default miss shader, so always include it from the start
	uint32 NumCallableShaderSlots = 0;
	TArray<FRayTracingShaderCommand> CallableCommands;

	// Helper array to hold references to single frame uniform buffers used in SBTs
	TArray<FUniformBufferRHIRef> UniformBuffers;

	// Geometries which still have a pending build request but are used this frame and require a force build.
	TArray<const FRayTracingGeometry*> GeometriesToBuild;

	// Used coarse mesh streaming handles during the last TLAS build
	TArray<Nanite::CoarseMeshStreamingHandle> UsedCoarseMeshStreamingHandles;

	bool bNeedsDebugInstanceGPUSceneIndexBuffer = false;

	// Used for transforming to translated world space in which TLAS was built.
	FVector PreViewTranslation {};
private:
	void ReleaseReadbackBuffers();

	struct FLayer
	{
		FRayTracingSceneInitializationData InitializationData;

		FRayTracingSceneRHIRef RayTracingSceneRHI;

		FRDGBufferRef InstanceBuffer;
		FRDGBufferRef BuildScratchBuffer;

		TRefCountPtr<FRDGPooledBuffer> RayTracingScenePooledBuffer;
		FRDGBufferRef RayTracingSceneBufferRDG;
		FRDGBufferSRVRef RayTracingSceneBufferSRV;

		FBufferRHIRef InstanceUploadBuffer;
		FShaderResourceViewRHIRef InstanceUploadSRV;

		FBufferRHIRef TransformUploadBuffer;
		FShaderResourceViewRHIRef TransformUploadSRV;

		FByteAddressBuffer AccelerationStructureAddressesBuffer;

		// Special data for debugging purposes
		FRDGBufferRef InstanceDebugBuffer = nullptr;
		FRDGBufferRef DebugInstanceGPUSceneIndexBuffer = nullptr;

		// Persistent storage for ray tracing instance descriptors.
		// Cleared every frame without releasing memory to avoid large heap allocations.
		// This must be filled before calling CreateRayTracingSceneWithGeometryInstances() and Create().
		TArray<FRayTracingGeometryInstance> Instances;

		TArray<FRayTracingInstanceDebugData> InstancesDebugData;
	};

	TArray<FLayer> Layers;

	// Transient memory allocator
	FMemStackBase Allocator;

	bool bInstanceDebugDataEnabled = false;

	bool bInitializationDataBuilt = false;
	bool bUsedThisFrame = false;

#if STATS
	const uint32 MaxReadbackBuffers = 4;

	TArray<FRHIGPUBufferReadback*> StatsReadbackBuffers;
	uint32 StatsReadbackBuffersWriteIndex = 0;
	uint32 StatsReadbackBuffersNumPending = 0;

	uint32 NumActiveInstances = 0;
#endif
};

#endif // RHI_RAYTRACING
