// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
	MetalRayTracing.h: MetalRT Implementation
==============================================================================*/

#pragma once

#include "MetalRHIPrivate.h"

#if METAL_RHI_RAYTRACING

struct FMetalRayTracingGeometryParameters
{
	FMetalRHIBuffer* IndexBuffer;
	FMetalRHIBuffer* VertexBuffer;
	uint64 RootConstantsBufferOffsetInBytes;
	uint64 VertexBufferOffset;
};

class FMetalRayTracingGeometry : public FRHIRayTracingGeometry
{
public:
	FMetalRayTracingGeometry(FRHICommandListBase& RHICmdList, const FRayTracingGeometryInitializer& InInitializer);
	~FMetalRayTracingGeometry();

	void ReleaseUnderlyingResource();

	/** FRHIRayTracingGeometry Interface */
	virtual FRayTracingAccelerationStructureAddress GetAccelerationStructureAddress(uint64 GPUIndex) const final override { return (FRayTracingAccelerationStructureAddress)SceneIndex; }
	/** FRHIRayTracingGeometry Interface */

	void Swap(FMetalRayTracingGeometry& Other);
	void RebuildDescriptors();

	void RemoveCompactionRequest();

	using FRHIRayTracingGeometry::Initializer;
	using FRHIRayTracingGeometry::SizeInfo;

	MTL::PrimitiveAccelerationStructureDescriptor* AccelerationStructureDescriptor;

	bool bHasPendingCompactionRequests;
	uint32_t CompactionSizeIndex;

	uint32_t SceneIndex; // TODO: Workaround since we can't provide a GPU VA when we build the instance descriptors for the TLAS (we need to use the AS index instead).

	static constexpr uint32 MaxNumAccelerationStructure = 2;
	static constexpr uint32 IndicesPerPrimitive = 3; // Triangle geometry only

	inline TRefCountPtr<FMetalRHIBuffer> GetAccelerationStructureRead()
	{
		return AccelerationStructure[AccelerationStructureIndex];
	}

	inline TRefCountPtr<FMetalRHIBuffer> GetAccelerationStructureWrite()
	{
		uint32 NextAccelerationStructure = (AccelerationStructureIndex + 1) % MaxNumAccelerationStructure;
		return AccelerationStructure[NextAccelerationStructure];
	}

	inline void NextAccelerationStructure()
	{
		AccelerationStructureIndex = (++AccelerationStructureIndex % MaxNumAccelerationStructure);
	}

private:
	NSMutableArray<MTLAccelerationStructureGeometryDescriptor*>* GeomArray;

	uint32 AccelerationStructureIndex;
	TRefCountPtr<FMetalRHIBuffer> AccelerationStructure[MaxNumAccelerationStructure];
};

class FMetalRayTracingScene : public FRHIRayTracingScene
{
public:
	FMetalRayTracingScene(FRayTracingSceneInitializer InInitializer);
	virtual ~FMetalRayTracingScene();

	void BindBuffer(FRHIBuffer* InBuffer, uint32 InBufferOffset);
	void BuildAccelerationStructure(
		FMetalRHICommandContext& CommandContext,
		FMetalRHIBuffer* ScratchBuffer, uint32 ScratchOffset,
		FMetalRHIBuffer* InstanceBuffer, uint32 InstanceOffset);

	void BuildPerInstanceGeometryParameterBuffer();

	inline const FRayTracingSceneInitializer& GetInitializer() const override final { return Initializer; }

	TRefCountPtr<FMetalShaderResourceView> InstanceBufferSRV;

	TRefCountPtr<FMetalShaderResourceView> ShaderResourceView;

private:
	friend class FMetalRHICommandContext;

private:
	/** The initializer provided to build the scene. Contains all the free standing stuff that used to be owned by the RT implementation. */
	const FRayTracingSceneInitializer Initializer;

	/** Acceleration Structure for the whole scene. */
	TRefCountPtr<FMetalRHIBuffer> AccelerationStructureBuffer;

	/** Root Constants for geometry evaluation in HitGroup/Miss (emulates D3D12 RootConstants with a global scope). */
	TArray<FMetalRayTracingGeometryParameters> PerInstanceGeometryParameters;

	// Buffer that contains per-instance index and vertex buffer binding data
	TRefCountPtr<FMetalRHIBuffer> PerInstanceGeometryParameterBuffer;
	TRefCountPtr<FMetalShaderResourceView> PerInstanceGeometryParameterSRV;

	/** Segments descriptors  (populated when the constructor is called). */
	NSMutableArray<id<MTLAccelerationStructure>>* MutableAccelerationStructures;
};
#endif // METAL_RHI_RAYTRACING
