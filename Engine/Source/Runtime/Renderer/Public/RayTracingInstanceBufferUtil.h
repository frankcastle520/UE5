// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"
#include "CoreMinimal.h"

#if RHI_RAYTRACING

class FGPUScene;
struct FRayTracingCullingParameters;
struct FDFVector3;

/*
* 
* Each FRayTracingGeometryInstance can translate to multiple native TLAS instances (see FRayTracingGeometryInstance::NumTransforms).
* 
* The FRayTracingGeometryInstance array (ie: FRayTracingScene::Instances) used to create FRayTracingSceneRHI
* can have mix of instances using GPUScene or CPU transforms.
* In order to reduce the number of dispatches to build the native RayTracing Instance Buffer,
* the upload buffer containing FRayTracingInstanceDescriptorInput is split in 2 sections, [GPUSceneInstances] [CPUInstances].
* This way native GPUScene and CPU instance descriptors can be built in a single dispatch per type.
* 
* If the ray tracing scene contains multiple layers, the instance buffer is divided into multiple subranges as expected by the RHI.
* 
*/

struct FRayTracingInstanceDescriptorInput
{
	uint32 GPUSceneInstanceOrTransformIndex;
	uint32 OutputDescriptorIndex;
	uint32 AccelerationStructureIndex;
	uint32 InstanceId;
	uint32 InstanceMaskAndFlags;
	uint32 InstanceContributionToHitGroupIndex;
	uint32 bApplyLocalBoundsTransform;
};

struct FRayTracingGPUInstance
{
	FShaderResourceViewRHIRef TransformSRV;
	uint32 NumInstances;
	uint32 DescBufferOffset;
};

struct FRayTracingSceneInitializationData
{
	uint32 NumNativeGPUSceneInstances;
	uint32 NumNativeCPUInstances;
	uint32 TotalNumSegments;

	// index of each instance geometry in FRayTracingSceneRHIRef ReferencedGeometries
	TArray<uint32> InstanceGeometryIndices;
	// base offset of each instance entries in the instance upload buffer
	TArray<uint32> BaseUploadBufferOffsets;
	// prefix sum of `Instance.NumTransforms` for all instances in this scene
	TArray<uint32> BaseInstancePrefixSum;

	// Unique list of geometries referenced by all instances in this scene.
	// Any referenced geometry is kept alive while the scene is alive.
	TArray<FRHIRayTracingGeometry*> PerInstanceGeometries;
	// One entry per instance
	TArray<FRHIRayTracingGeometry*> ReferencedGeometries;
};

RENDERER_API FRayTracingSceneInitializationData BuildRayTracingSceneInitializationData(TConstArrayView<FRayTracingGeometryInstance> Instances);

struct FRayTracingSceneWithGeometryInstances
{
	FRayTracingSceneRHIRef Scene;
	uint32 NumNativeGPUSceneInstances;
	uint32 NumNativeCPUInstances;
	uint32 TotalNumSegments;
	// index of each instance geometry in FRayTracingSceneRHIRef ReferencedGeometries
	TArray<uint32> InstanceGeometryIndices;
	// base offset of each instance entries in the instance upload buffer
	TArray<uint32> BaseUploadBufferOffsets;
	// prefix sum of `Instance.NumTransforms` for all instances in this scene
	TArray<uint32> BaseInstancePrefixSum;
	UE_DEPRECATED(5.5, "GPUInstances no longer supported. Use GPUSceneInstances instead.")
	TArray<FRayTracingGPUInstance> GPUInstances;

	// Unique list of geometries referenced by all instances in this scene.
	// Any referenced geometry is kept alive while the scene is alive.
	TArray<FRHIRayTracingGeometry*> ReferencedGeometries;
	// One entry per instance
	TArray<FRHIRayTracingGeometry*> PerInstanceGeometries;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FRayTracingSceneWithGeometryInstances() = default;
	FRayTracingSceneWithGeometryInstances(const FRayTracingSceneWithGeometryInstances&) = default;
	FRayTracingSceneWithGeometryInstances& operator=(const FRayTracingSceneWithGeometryInstances&) = default;
	FRayTracingSceneWithGeometryInstances(FRayTracingSceneWithGeometryInstances&&) = default;
	FRayTracingSceneWithGeometryInstances& operator=(FRayTracingSceneWithGeometryInstances&&) = default;
	~FRayTracingSceneWithGeometryInstances() = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
};

// Helper function to create FRayTracingSceneRHI using array of high level instances
// Also outputs data required to build the instance buffer
UE_DEPRECATED(5.5, "Use FRHIShaderBindingTable instead.")
RENDERER_API FRayTracingSceneWithGeometryInstances CreateRayTracingSceneWithGeometryInstances(
	TConstArrayView<FRayTracingGeometryInstance> Instances,
	uint8 NumLayers,
	uint32 NumShaderSlotsPerGeometrySegment,
	uint32 NumMissShaderSlots,
	uint32 NumCallableShaderSlots = 0,
	ERayTracingAccelerationStructureFlags BuildFlags = ERayTracingAccelerationStructureFlags::FastTrace);

// Helper function to fill upload buffers required by BuildRayTracingInstanceBuffer with instance descriptors
// Transforms of CPU instances are copied to OutTransformData
RENDERER_API void FillRayTracingInstanceUploadBuffer(
	FRayTracingSceneRHIRef RayTracingSceneRHI,
	FVector PreViewTranslation,
	TConstArrayView<FRayTracingGeometryInstance> Instances,
	TConstArrayView<uint32> InstanceGeometryIndices,
	TConstArrayView<uint32> BaseUploadBufferOffsets,
	TConstArrayView<uint32> BaseInstancePrefixSum,
	uint32 NumNativeGPUSceneInstances,
	uint32 NumNativeCPUInstances,
	TArrayView<FRayTracingInstanceDescriptorInput> OutInstanceUploadData,
	TArrayView<FVector4f> OutTransformData);

UE_DEPRECATED(5.5, "Must specify BaseInstancePrefixSum.")
RENDERER_API void FillRayTracingInstanceUploadBuffer(
	FRayTracingSceneRHIRef RayTracingSceneRHI,
	FVector PreViewTranslation,
	TConstArrayView<FRayTracingGeometryInstance> Instances,
	TConstArrayView<uint32> InstanceGeometryIndices,
	TConstArrayView<uint32> BaseUploadBufferOffsets,
	uint32 NumNativeGPUSceneInstances,
	uint32 NumNativeCPUInstances,
	TArrayView<FRayTracingInstanceDescriptorInput> OutInstanceUploadData,
	TArrayView<FVector4f> OutTransformData);

RENDERER_API void BuildRayTracingInstanceBuffer(
	FRHICommandList& RHICmdList,
	const FGPUScene* GPUScene,
	const FDFVector3& PreViewTranslation,
	FUnorderedAccessViewRHIRef InstancesUAV,
	FShaderResourceViewRHIRef InstanceUploadSRV,
	FShaderResourceViewRHIRef AccelerationStructureAddressesSRV,
	FShaderResourceViewRHIRef CPUInstanceTransformSRV,
	uint32 NumNativeGPUSceneInstances,
	uint32 NumNativeCPUInstances,
	const FRayTracingCullingParameters* CullingParameters,
	FUnorderedAccessViewRHIRef OutputStatsUAV,
	FUnorderedAccessViewRHIRef DebugInstanceGPUSceneIndexUAV);

UE_DEPRECATED(5.5, "GPUInstances no longer supported. Use GPUSceneInstances instead.")
inline void BuildRayTracingInstanceBuffer(
	FRHICommandList& RHICmdList,
	const FGPUScene* GPUScene,
	const FDFVector3& PreViewTranslation,
	FUnorderedAccessViewRHIRef InstancesUAV,
	FShaderResourceViewRHIRef InstanceUploadSRV,
	FShaderResourceViewRHIRef AccelerationStructureAddressesSRV,
	FShaderResourceViewRHIRef CPUInstanceTransformSRV,
	uint32 NumNativeGPUSceneInstances,
	uint32 NumNativeCPUInstances,
	TConstArrayView<FRayTracingGPUInstance> GPUInstances,
	const FRayTracingCullingParameters* CullingParameters,
	FUnorderedAccessViewRHIRef DebugInstanceGPUSceneIndexUAV)
{
	BuildRayTracingInstanceBuffer(RHICmdList, GPUScene, PreViewTranslation,
		InstancesUAV, InstanceUploadSRV, AccelerationStructureAddressesSRV, CPUInstanceTransformSRV,
		NumNativeGPUSceneInstances, NumNativeCPUInstances,
		CullingParameters,
		nullptr,
		DebugInstanceGPUSceneIndexUAV);
}

#endif // RHI_RAYTRACING