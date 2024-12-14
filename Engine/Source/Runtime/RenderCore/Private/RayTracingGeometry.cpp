// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracingGeometry.h"
#include "RHICommandList.h"
#include "HAL/IConsoleManager.h"
#include "RayTracingGeometryManagerInterface.h"
#include "RenderUtils.h"
#include "RHIResourceReplace.h"
#include "RHITextureReference.h" // IWYU pragma: keep

#if RHI_RAYTRACING

IRayTracingGeometryManager* GRayTracingGeometryManager = nullptr;

#endif

static TAutoConsoleVariable<int32> CVarDebugForceRuntimeBLAS(
	TEXT("r.Raytracing.DebugForceRuntimeBLAS"),
	0,
	TEXT("Force building BLAS at runtime."),
	ECVF_ReadOnly);

PRAGMA_DISABLE_DEPRECATION_WARNINGS // Remove this in UE 5.6 once RayTracingGeometryRHI is made private

#if RHI_RAYTRACING

void FRayTracingGeometry::InitRHIForStreaming(FRHIRayTracingGeometry* IntermediateGeometry, FRHIResourceReplaceBatcher& Batcher)
{
	checkf(IntermediateGeometry,
		TEXT("IntermediateGeometry should be valid when streaming-in ray tracing geometry.\n")
		TEXT("This will result in FRayTracingGeometry not being correctly initialized.\n")
	);

	checkf(RayTracingGeometryRHI, TEXT("RayTracingGeometryRHI must be valid when InitRHIForStreaming is called.\n"));

	Initializer.Type = ERayTracingGeometryInitializerType::Rendering;

	Batcher.EnqueueReplace(RayTracingGeometryRHI, IntermediateGeometry);

	EnumAddFlags(GeometryState, EGeometryStateFlags::Valid);
	EnumAddFlags(GeometryState, EGeometryStateFlags::StreamedIn);

	GRayTracingGeometryManager->RefreshRegisteredGeometry(RayTracingGeometryHandle);
}

void FRayTracingGeometry::ReleaseRHIForStreaming(FRHIResourceReplaceBatcher& Batcher)
{
	RemoveBuildRequest();

	checkf(RayTracingGeometryRHI, TEXT("RayTracingGeometryRHI must be valid when ReleaseRHIForStreaming is called.\n"));

	EnumRemoveFlags(GeometryState, EGeometryStateFlags::StreamedIn);
	EnumRemoveFlags(GeometryState, EGeometryStateFlags::Valid);

	Batcher.EnqueueReplace(RayTracingGeometryRHI, nullptr);

	Initializer.Type = ERayTracingGeometryInitializerType::StreamingDestination;

	GRayTracingGeometryManager->RefreshRegisteredGeometry(RayTracingGeometryHandle);
}

void FRayTracingGeometry::RequestBuildIfNeeded(FRHICommandListBase& RHICmdList, ERTAccelerationStructureBuildPriority InBuildPriority)
{
	if (GetRequiresBuild())
	{
		RayTracingBuildRequestIndex = GRayTracingGeometryManager->RequestBuildAccelerationStructure(this, InBuildPriority);
		SetRequiresBuild(false);
	}
}

void FRayTracingGeometry::MakeResident(FRHICommandList& RHICmdList)
{
	check(EnumHasAllFlags(GeometryState, EGeometryStateFlags::Evicted) && RayTracingGeometryRHI == nullptr);
	checkf(!EnumHasAllFlags(GeometryState, EGeometryStateFlags::StreamedIn),
		TEXT("Evicted FRayTracingGeometry shouldn't have StreamedIn flag set."));

	if (!ensureMsgf(DynamicGeometrySharedBufferGenerationID == NonSharedVertexBuffers,
		TEXT("Cannot call MakeResident(...) on FRayTracingGeometry using shared vertex buffers.\n")
		TEXT("Dynamic geometry should be rebuilt instead.")))
	{
		// if geometry is using shared buffers those buffers might not be valid at this point
		// instead of being made resident here, dynamic geometries need to be manually updated as necessary
		return;
	}

	EnumRemoveFlags(GeometryState, EGeometryStateFlags::Evicted);

	InitRHI(RHICmdList);
}

void FRayTracingGeometry::Evict()
{
	check(!EnumHasAllFlags(GeometryState, EGeometryStateFlags::Evicted) && RayTracingGeometryRHI != nullptr);

	RemoveBuildRequest();
	RayTracingGeometryRHI.SafeRelease();
	EnumAddFlags(GeometryState, EGeometryStateFlags::Evicted);

	if (EnumHasAllFlags(GeometryState, EGeometryStateFlags::StreamedIn))
	{
		EnumRemoveFlags(GeometryState, EGeometryStateFlags::StreamedIn);
		EnumRemoveFlags(GeometryState, EGeometryStateFlags::Valid);

		Initializer.Type = ERayTracingGeometryInitializerType::StreamingDestination;
	}
	
	GRayTracingGeometryManager->RefreshRegisteredGeometry(RayTracingGeometryHandle);
	
	if (GroupHandle != INDEX_NONE)
	{
		GRayTracingGeometryManager->RequestUpdateCachedRenderState(GroupHandle);
	}
}

void FRayTracingGeometry::CreateRayTracingGeometry(FRHICommandListBase& RHICmdList, ERTAccelerationStructureBuildPriority InBuildPriority)
{
	// Release previous RHI object if any
	ReleaseRHI();

	if (RawData.Num())
	{
		check(Initializer.OfflineData == nullptr);
		Initializer.OfflineData = &RawData;
	}

	if (CVarDebugForceRuntimeBLAS.GetValueOnAnyThread() && Initializer.OfflineData != nullptr)
	{
		Initializer.OfflineData->Discard();
		Initializer.OfflineData = nullptr;
	}

	bool bAllSegmentsAreValid = Initializer.Segments.Num() > 0 || Initializer.OfflineData;
	for (const FRayTracingGeometrySegment& Segment : Initializer.Segments)
	{
		if (!Segment.VertexBuffer)
		{
			bAllSegmentsAreValid = false;
			break;
		}
	}

	if (bAllSegmentsAreValid)
	{
		// Geometries with StreamingDestination type are initially created in invalid state until they are streamed in (see InitRHIForStreaming).
		const bool bWithNativeResource = Initializer.Type != ERayTracingGeometryInitializerType::StreamingDestination;
		if (bWithNativeResource)
		{
			EnumAddFlags(GeometryState, EGeometryStateFlags::Valid);
		}

		if (IsRayTracingEnabled())
		{
			RayTracingGeometryRHI = RHICmdList.CreateRayTracingGeometry(Initializer);
		}
		else
		{
			EnumAddFlags(GeometryState, EGeometryStateFlags::Evicted);
		}

		if (Initializer.OfflineData == nullptr)
		{
			// Request build if not skip
			if (InBuildPriority != ERTAccelerationStructureBuildPriority::Skip)
			{
				if (RayTracingGeometryRHI)
				{
					RayTracingBuildRequestIndex = GRayTracingGeometryManager->RequestBuildAccelerationStructure(this, InBuildPriority);
				}
				SetRequiresBuild(false);
			}
			else if (bWithNativeResource)
			{
				SetRequiresBuild(true);
			}
		}
		else
		{
			if (RayTracingGeometryRHI && RayTracingGeometryRHI->IsCompressed())
			{
				RayTracingBuildRequestIndex = GRayTracingGeometryManager->RequestBuildAccelerationStructure(this, InBuildPriority);
			}

			SetRequiresBuild(false);

			// Offline data ownership is transferred to the RHI, which discards it after use.
			// It is no longer valid to use it after this point.
			Initializer.OfflineData = nullptr;
		}
	}

	GRayTracingGeometryManager->RefreshRegisteredGeometry(RayTracingGeometryHandle);
}

bool FRayTracingGeometry::IsValid() const
{
	// can't check IsInitialized() because current implementation of hair ray tracing support doesn't initialize resource
	//check(IsInitialized());

	const bool bIsValid = EnumHasAllFlags(GeometryState, EGeometryStateFlags::Valid);

	if (bIsValid)
	{
		check(Initializer.TotalPrimitiveCount > 0);
		check(RayTracingGeometryRHI != nullptr || EnumHasAllFlags(GeometryState, EGeometryStateFlags::Evicted));
	}

	return bIsValid;
}

bool FRayTracingGeometry::IsEvicted() const
{
	// can't check IsInitialized() because current implementation of hair ray tracing support doesn't initialize resource
	//check(IsInitialized());

	const bool bIsEvicted = EnumHasAllFlags(GeometryState, EGeometryStateFlags::Evicted);

	if (bIsEvicted)
	{
		check(RayTracingGeometryRHI == nullptr);
	}

	return bIsEvicted;
}

void FRayTracingGeometry::InitRHI(FRHICommandListBase& RHICmdList)
{
	if (!IsRayTracingAllowed())
		return;

	ERTAccelerationStructureBuildPriority BuildPriority = Initializer.Type != ERayTracingGeometryInitializerType::Rendering
		? ERTAccelerationStructureBuildPriority::Skip
		: ERTAccelerationStructureBuildPriority::Normal;
	CreateRayTracingGeometry(RHICmdList, BuildPriority);
}

void FRayTracingGeometry::ReleaseRHI()
{
	RemoveBuildRequest();
	RayTracingGeometryRHI.SafeRelease();
	GeometryState = EGeometryStateFlags::Invalid;

	GRayTracingGeometryManager->RefreshRegisteredGeometry(RayTracingGeometryHandle);
}

void FRayTracingGeometry::RemoveBuildRequest()
{
	if (HasPendingBuildRequest())
	{
		GRayTracingGeometryManager->RemoveBuildRequest(RayTracingBuildRequestIndex);
		RayTracingBuildRequestIndex = INDEX_NONE;
	}
}

void FRayTracingGeometry::InitResource(FRHICommandListBase& RHICmdList)
{
	ensureMsgf(IsRayTracingAllowed(), TEXT("FRayTracingGeometry should only be initialized when Ray Tracing is allowed."));

	FRenderResource::InitResource(RHICmdList);

	if (RayTracingGeometryHandle == INDEX_NONE)
	{
		RayTracingGeometryHandle = GRayTracingGeometryManager->RegisterRayTracingGeometry(this);
	}
}

void FRayTracingGeometry::ReleaseResource()
{
	ensureMsgf(IsRayTracingAllowed() || !IsInitialized(), TEXT("FRayTracingGeometry should only be initialized when Ray Tracing is allowed."));

	if (RayTracingGeometryHandle != INDEX_NONE)
	{
		GRayTracingGeometryManager->ReleaseRayTracingGeometryHandle(RayTracingGeometryHandle);
		RayTracingGeometryHandle = INDEX_NONE;
	}

	FRenderResource::ReleaseResource();

	// Release any resource references held by the initializer.
	// This includes index and vertex buffers used for building the BLAS.
	Initializer = FRayTracingGeometryInitializer{};
}

bool FRayTracingGeometry::HasPendingBuildRequest() const
{
	const bool bHasPendingBuildRequest = RayTracingBuildRequestIndex != INDEX_NONE;

	if (bHasPendingBuildRequest)
	{
		ensure(IsValid() && !IsEvicted());
	}

	return bHasPendingBuildRequest;
}

void FRayTracingGeometry::BoostBuildPriority(float InBoostValue) const
{
	check(HasPendingBuildRequest());
	GRayTracingGeometryManager->BoostPriority(RayTracingBuildRequestIndex, InBoostValue);
}

#endif // RHI_RAYTRACING

PRAGMA_ENABLE_DEPRECATION_WARNINGS
