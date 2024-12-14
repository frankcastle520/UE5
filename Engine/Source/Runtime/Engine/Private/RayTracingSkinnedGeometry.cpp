// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracingSkinnedGeometry.h"

#if RHI_RAYTRACING

#include "RenderGraphBuilder.h"
#include "RayTracingGeometry.h"

DECLARE_GPU_STAT(SkinnedGeometryBuildBLAS);
DECLARE_GPU_STAT(SkinnedGeometryUpdateBLAS);

DECLARE_DWORD_COUNTER_STAT(TEXT("Ray tracing skinned build primitives"), STAT_RayTracingSkinnedBuildPrimitives, STATGROUP_SceneRendering);
DECLARE_DWORD_COUNTER_STAT(TEXT("Ray tracing skinned update primitives"), STAT_RayTracingSkinnedUpdatePrimitives, STATGROUP_SceneRendering);

static TAutoConsoleVariable<int32> CVarSkinCacheRayTracingMaxUpdatePrimitivesPerFrame(
	TEXT("r.SkinCache.RayTracing.MaxUpdatePrimitivesPerFrame"),
	-1,
	TEXT("Sets the skinned ray tracing acceleration structure build budget in terms of maximum number of updated triangles per frame (<= 0 then disabled and all acceleration structures are updated - default)"),
	ECVF_RenderThreadSafe
);

static int32 GMaxRayTracingPrimitivesPerCmdList = -1;
FAutoConsoleVariableRef CVarSkinnedGeometryMaxRayTracingPrimitivesPerCmdList(
	TEXT("r.SkinCache.MaxRayTracingPrimitivesPerCmdList"),
	GMaxRayTracingPrimitivesPerCmdList,
	TEXT("Maximum amount of primitives which are batched together into a single command list to fix potential TDRs."),
	ECVF_RenderThreadSafe
);

void FRayTracingSkinnedGeometryUpdateQueue::Add(FRayTracingGeometry* InRayTracingGeometry, const FRayTracingAccelerationStructureSize& StructureSize)
{
	checkf(InRayTracingGeometry->GetRHI() != nullptr, TEXT("FRayTracingGeometry needs to have a valid RHI to be updated by FRayTracingSkinnedGeometryUpdateQueue."));

	FScopeLock Lock(&CS);
	FRayTracingUpdateInfo* CurrentUpdateInfo = ToUpdate.Find(InRayTracingGeometry);
	if (CurrentUpdateInfo == nullptr)
	{
		FRayTracingUpdateInfo UpdateInfo;
		UpdateInfo.BuildMode = InRayTracingGeometry->GetRequiresBuild() ? EAccelerationStructureBuildMode::Build : EAccelerationStructureBuildMode::Update;
		UpdateInfo.ScratchSize = InRayTracingGeometry->GetRequiresBuild() ? StructureSize.BuildScratchSize : StructureSize.UpdateScratchSize;
		ToUpdate.Add(InRayTracingGeometry, UpdateInfo);
	}
	// If currently updating but need full rebuild then update the stored build mode
	else if (CurrentUpdateInfo->BuildMode == EAccelerationStructureBuildMode::Update && InRayTracingGeometry->GetRequiresBuild())
	{
		CurrentUpdateInfo->BuildMode = EAccelerationStructureBuildMode::Build;
		CurrentUpdateInfo->ScratchSize = StructureSize.BuildScratchSize;
	}

	InRayTracingGeometry->SetRequiresBuild(false);
}

void FRayTracingSkinnedGeometryUpdateQueue::Remove(FRayTracingGeometry* RayTracingGeometry, uint32 EstimatedMemory)
{
	FScopeLock Lock(&CS);
	if (ToUpdate.Find(RayTracingGeometry) != nullptr)
	{
		ToUpdate.Remove(RayTracingGeometry);
		EstimatedMemoryPendingRelease += EstimatedMemory;
	}
}

uint32 FRayTracingSkinnedGeometryUpdateQueue::ComputeScratchBufferSize() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FRayTracingSkinnedGeometryUpdateQueue::ComputeScratchBufferSize);

	const uint64 ScratchAlignment = GRHIRayTracingScratchBufferAlignment;
	uint32 ScratchBLASSize = 0;

	if (ToUpdate.Num())
	{
		for (TMap<FRayTracingGeometry*, FRayTracingUpdateInfo>::TRangedForConstIterator Iter = ToUpdate.begin(); Iter != ToUpdate.end(); ++Iter)
		{			
			FRayTracingUpdateInfo const& UpdateInfo = Iter.Value();
			ScratchBLASSize = Align(ScratchBLASSize + UpdateInfo.ScratchSize, ScratchAlignment);
		}
	}

	return ScratchBLASSize;
}

void FRayTracingSkinnedGeometryUpdateQueue::Commit(FRHICommandList& RHICmdList, FRHIBuffer* ScratchBuffer)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FRayTracingSkinnedGeometryUpdateQueue::Commit);

	if (ToUpdate.Num())
	{
		FScopeLock Lock(&CS);
		// Track the amount of primitives which need to be build/updated in a single batch
		uint64 PrimitivesToUpdates = 0;
		TArray<FRayTracingGeometryBuildParams> BatchedBuildParams;
		TArray<FRayTracingGeometryBuildParams> BatchedUpdateParams;

		BatchedBuildParams.Reserve(ToUpdate.Num());
		BatchedUpdateParams.Reserve(ToUpdate.Num());

		auto KickBatch = [&RHICmdList, ScratchBuffer, &BatchedBuildParams, &BatchedUpdateParams, &PrimitivesToUpdates]()
		{
			// TODO compute correct offset and increment for the next call or just always use 0 as the offset since we know that 
			// 2 calls to BuildAccelerationStructures won't overlap due to UAV barrier inside RHIBuildAccelerationStructures so scratch memory can be reused by the next call already
			uint32 ScratchBLASOffset = 0;

			if (BatchedBuildParams.Num())
			{
				RHI_BREADCRUMB_EVENT_STAT(RHICmdList, SkinnedGeometryBuildBLAS, "SkinnedGeometryBuildBLAS");
				SCOPED_GPU_STAT(RHICmdList, SkinnedGeometryBuildBLAS);
				
				if (ScratchBuffer)
				{
					FRHIBufferRange ScratchBufferRange;
					ScratchBufferRange.Buffer = ScratchBuffer;
					ScratchBufferRange.Offset = ScratchBLASOffset;
					RHICmdList.BuildAccelerationStructures(BatchedBuildParams, ScratchBufferRange);
				}
				else
				{
					RHICmdList.BuildAccelerationStructures(BatchedBuildParams);
				}
				
				BatchedBuildParams.Empty(BatchedBuildParams.Max());
			}

			if (BatchedUpdateParams.Num())
			{
				RHI_BREADCRUMB_EVENT_STAT(RHICmdList, SkinnedGeometryUpdateBLAS, "SkinnedGeometryUpdateBLAS");
				SCOPED_GPU_STAT(RHICmdList, SkinnedGeometryUpdateBLAS);

				if (ScratchBuffer)
				{
					FRHIBufferRange ScratchBufferRange;
					ScratchBufferRange.Buffer = ScratchBuffer;
					ScratchBufferRange.Offset = ScratchBLASOffset;
					RHICmdList.BuildAccelerationStructures(BatchedUpdateParams, ScratchBufferRange);
				}
				else
				{
					RHICmdList.BuildAccelerationStructures(BatchedUpdateParams);
				}
				BatchedUpdateParams.Empty(BatchedUpdateParams.Max());
			}

			PrimitivesToUpdates = 0;
		};

		const uint64 ScratchAlignment = GRHIRayTracingScratchBufferAlignment;
		uint32 ScratchBLASCurrentOffset = 0;
		uint32 ScratchBLASNextOffset = 0;

		// Iterate all the geometries which need an update
		for (TMap<FRayTracingGeometry*, FRayTracingUpdateInfo>::TRangedForIterator Iter = ToUpdate.begin(); Iter != ToUpdate.end(); ++Iter)
		{
			FRayTracingGeometry* RayTracingGeometry = Iter.Key();
			FRayTracingUpdateInfo& UpdateInfo = Iter.Value();

			FRayTracingGeometryBuildParams BuildParams;
			BuildParams.Geometry = RayTracingGeometry->GetRHI();
			BuildParams.BuildMode = UpdateInfo.BuildMode;
			BuildParams.Segments = RayTracingGeometry->Initializer.Segments;

			// Update the offset
			ScratchBLASNextOffset = Align(ScratchBLASNextOffset + UpdateInfo.ScratchSize, ScratchAlignment);

			// Make 'Build' 10 times more expensive than 1 'Update' of the BVH
			uint32 PrimitiveCount = RayTracingGeometry->Initializer.TotalPrimitiveCount;
			if (BuildParams.BuildMode == EAccelerationStructureBuildMode::Build)
			{
				PrimitiveCount *= 10;
				BatchedBuildParams.Add(BuildParams);
			}
			else
			{
				BatchedUpdateParams.Add(BuildParams);
			}

			PrimitivesToUpdates += PrimitiveCount;

			// Flush batch when limit is reached
			if (GMaxRayTracingPrimitivesPerCmdList > 0 && PrimitivesToUpdates >= GMaxRayTracingPrimitivesPerCmdList)
			{
				KickBatch();
				RHICmdList.SubmitCommandsHint();
			}
		}

		// Enqueue the last batch
		KickBatch();

		// Clear working data
		ToUpdate.Reset();
		EstimatedMemoryPendingRelease = 0;
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FSkinnedGeometryBLASUpdateParams, )
	RDG_BUFFER_ACCESS(SharedScratchBuffer, ERHIAccess::UAVCompute)
END_SHADER_PARAMETER_STRUCT()

void FRayTracingSkinnedGeometryUpdateQueue::Commit(FRDGBuilder& GraphBuilder, ERDGPassFlags ComputePassFlags)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FRayTracingSkinnedGeometryUpdateQueue::Commit);

	TArray<FRayTracingGeometryBuildParams> GeometryBuildRequests;
	GeometryBuildRequests.Reserve(ToUpdate.Num());

	TArray<FRayTracingGeometry*> GeometriesToUpdate;
	GeometriesToUpdate.Reserve(ToUpdate.Num());

	uint32 BLASScratchSize = 0;

	int32 NumBuiltPrimitives = 0;

	{
		FScopeLock Lock(&CS);

		for (TMap<FRayTracingGeometry*, FRayTracingUpdateInfo>::TRangedForIterator Iter = ToUpdate.begin(); Iter != ToUpdate.end(); ++Iter)
		{
			FRayTracingGeometry* RayTracingGeometry = Iter.Key();
			FRayTracingUpdateInfo& UpdateInfo = Iter.Value();

			if (!ensureMsgf(RayTracingGeometry->GetRHI(), TEXT("Skipping request with invalid ray tracing geometry in FRayTracingSkinnedGeometryUpdateQueue. Geometry->IsEvicted(): %d."), RayTracingGeometry->IsEvicted()))
			{
				continue;
			}

			FRayTracingGeometryBuildParams BuildParams;
			BuildParams.Geometry = RayTracingGeometry->GetRHI();
			BuildParams.BuildMode = UpdateInfo.BuildMode;
			BuildParams.Segments = RayTracingGeometry->Initializer.Segments;

			if (BuildParams.BuildMode == EAccelerationStructureBuildMode::Build)
			{
				GeometryBuildRequests.Add(BuildParams);

				BLASScratchSize += UpdateInfo.ScratchSize;

				RayTracingGeometry->LastUpdatedFrame = GFrameCounterRenderThread;

				NumBuiltPrimitives += RayTracingGeometry->Initializer.TotalPrimitiveCount;
			}
			else
			{
				GeometriesToUpdate.Add(RayTracingGeometry);
			}
		}

		// Clear working data
		ToUpdate.Reset();
		EstimatedMemoryPendingRelease = 0;
	}	

	const int32 MaxUpdatePrimitivesPerFrame = CVarSkinCacheRayTracingMaxUpdatePrimitivesPerFrame.GetValueOnRenderThread();

	int32 NumUpdatedPrimitives = 0;

	if (MaxUpdatePrimitivesPerFrame <= 0)
	{		
		for (FRayTracingGeometry* RayTracingGeometry : GeometriesToUpdate)
		{
			FRayTracingGeometryBuildParams BuildParams;
			BuildParams.Geometry = RayTracingGeometry->GetRHI();
			BuildParams.BuildMode = EAccelerationStructureBuildMode::Update;
			BuildParams.Segments = RayTracingGeometry->Initializer.Segments;

			GeometryBuildRequests.Add(BuildParams);

			RayTracingGeometry->LastUpdatedFrame = GFrameCounterRenderThread;

			BLASScratchSize += RayTracingGeometry->GetRHI()->GetSizeInfo().UpdateScratchSize;

			NumUpdatedPrimitives += RayTracingGeometry->Initializer.TotalPrimitiveCount;			
		}		
	}
	else
	{
		GeometriesToUpdate.Sort([](const FRayTracingGeometry& InLHS, const FRayTracingGeometry& InRHS)
			{
				return InLHS.LastUpdatedFrame < InRHS.LastUpdatedFrame;
			});			

		for (FRayTracingGeometry* RayTracingGeometry : GeometriesToUpdate)
		{
			const int32 NumPrimitives = RayTracingGeometry->Initializer.TotalPrimitiveCount;			
						
			FRayTracingGeometryBuildParams BuildParams;
			BuildParams.Geometry = RayTracingGeometry->GetRHI();
			BuildParams.BuildMode = EAccelerationStructureBuildMode::Update;
			BuildParams.Segments = RayTracingGeometry->Initializer.Segments;

			GeometryBuildRequests.Add(BuildParams);

			RayTracingGeometry->LastUpdatedFrame = GFrameCounterRenderThread;

			BLASScratchSize += RayTracingGeometry->GetRHI()->GetSizeInfo().UpdateScratchSize;

			NumUpdatedPrimitives += NumPrimitives;

			if (NumUpdatedPrimitives > MaxUpdatePrimitivesPerFrame)
			{
				break;
			}
		}
	}

	INC_DWORD_STAT_BY(STAT_RayTracingSkinnedBuildPrimitives, NumBuiltPrimitives);
	INC_DWORD_STAT_BY(STAT_RayTracingSkinnedUpdatePrimitives, NumUpdatedPrimitives);

	FRDGBufferRef SharedScratchBuffer = nullptr;

	if (BLASScratchSize > 0)
	{
		const uint32 ScratchAlignment = GRHIRayTracingScratchBufferAlignment;

		FRDGBufferDesc ScratchBufferDesc;
		ScratchBufferDesc.Usage = EBufferUsageFlags::RayTracingScratch | EBufferUsageFlags::StructuredBuffer;
		ScratchBufferDesc.BytesPerElement = ScratchAlignment;
		ScratchBufferDesc.NumElements = FMath::DivideAndRoundUp(BLASScratchSize, ScratchAlignment);

		SharedScratchBuffer = GraphBuilder.CreateBuffer(ScratchBufferDesc, TEXT("SkinnedGeometry.BLASSharedScratchBuffer"));
	}

	FSkinnedGeometryBLASUpdateParams* BLASUpdateParams = GraphBuilder.AllocParameters<FSkinnedGeometryBLASUpdateParams>();
	BLASUpdateParams->SharedScratchBuffer = SharedScratchBuffer;

	RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

	if (GeometryBuildRequests.Num())
	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, SkinnedGeometryBuildBLAS, "SkinnedGeometryBuildBLAS");
		RDG_GPU_STAT_SCOPE(GraphBuilder, SkinnedGeometryBuildBLAS);

		GraphBuilder.AddPass(RDG_EVENT_NAME("CommitRayTracingSkinnedGeometryUpdates"), BLASUpdateParams, ComputePassFlags | ERDGPassFlags::NeverCull,
			[
				BuildRequests = MoveTemp(GeometryBuildRequests),
				SharedScratchBuffer
			](FRHICommandList& RHICmdList)
			{
				FRHIBufferRange ScratchBufferRange;
				ScratchBufferRange.Buffer = SharedScratchBuffer->GetRHI();
				ScratchBufferRange.Offset = 0;
				RHICmdList.BuildAccelerationStructures(BuildRequests, ScratchBufferRange);
			});
	}
}

#endif // RHI_RAYTRACING
