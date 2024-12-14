// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "RayTracingGeometryManagerInterface.h"

#include "Containers/SparseArray.h"
#include "Containers/Map.h"
#include "IO/IoBuffer.h"
#include "Serialization/BulkData.h"

#if RHI_RAYTRACING

class FPrimitiveSceneProxy;
class UStaticMesh;

class FRayTracingGeometryManager : public IRayTracingGeometryManager
{
public:

	ENGINE_API FRayTracingGeometryManager();
	ENGINE_API virtual ~FRayTracingGeometryManager();

	ENGINE_API virtual BuildRequestIndex RequestBuildAccelerationStructure(FRayTracingGeometry* InGeometry, ERTAccelerationStructureBuildPriority InPriority, EAccelerationStructureBuildMode InBuildMode) override;

	ENGINE_API virtual void RemoveBuildRequest(BuildRequestIndex InRequestIndex) override;
	ENGINE_API virtual void BoostPriority(BuildRequestIndex InRequestIndex, float InBoostValue) override;
	ENGINE_API virtual void ForceBuildIfPending(FRHIComputeCommandList& InCmdList, const TArrayView<const FRayTracingGeometry*> InGeometries) override;
	ENGINE_API virtual void ProcessBuildRequests(FRHIComputeCommandList& InCmdList, bool bInBuildAll = false) override;

	ENGINE_API virtual RayTracingGeometryHandle RegisterRayTracingGeometry(FRayTracingGeometry* InGeometry) override;
	ENGINE_API virtual void ReleaseRayTracingGeometryHandle(RayTracingGeometryHandle Handle) override;

	ENGINE_API virtual RayTracing::GeometryGroupHandle RegisterRayTracingGeometryGroup(uint32 NumLODs, uint32 CurrentFirstLODIdx = 0) override;
	ENGINE_API virtual void ReleaseRayTracingGeometryGroup(RayTracing::GeometryGroupHandle Handle) override;

	ENGINE_API virtual void RefreshRegisteredGeometry(RayTracingGeometryHandle Handle) override;

	void SetRayTracingGeometryStreamingData(const FRayTracingGeometry* Geometry, FByteBulkData& BulkData, uint32 Offset, uint32 Size);
	void SetRayTracingGeometryGroupCurrentFirstLODIndex(FRHICommandListBase& RHICmdList, RayTracing::GeometryGroupHandle Handle, uint8 CurrentFirstLODIdx);

	ENGINE_API virtual void PreRender() override;
	ENGINE_API virtual void Tick(FRHICommandList& RHICmdList) override;

	ENGINE_API void RegisterProxyWithCachedRayTracingState(FPrimitiveSceneProxy* Proxy, RayTracing::GeometryGroupHandle InRayTracingGeometryGroupHandle);
	ENGINE_API void UnregisterProxyWithCachedRayTracingState(FPrimitiveSceneProxy* Proxy, RayTracing::GeometryGroupHandle InRayTracingGeometryGroupHandle);

	ENGINE_API virtual void RequestUpdateCachedRenderState(RayTracing::GeometryGroupHandle InRayTracingGeometryGroupHandle) override;

	ENGINE_API void AddReferencedGeometry(const FRayTracingGeometry* Geometry);
	ENGINE_API void AddReferencedGeometryGroups(const TSet<RayTracing::GeometryGroupHandle>& GeometryGroups);

#if DO_CHECK
	ENGINE_API bool IsGeometryReferenced(const FRayTracingGeometry* Geometry) const;
	ENGINE_API bool IsGeometryGroupReferenced(RayTracing::GeometryGroupHandle GeometryGroup) const;
#endif

private:

	struct FBuildRequest
	{
		BuildRequestIndex RequestIndex = INDEX_NONE;

		float BuildPriority = 0.0f;
		FRayTracingGeometry* Owner;
		EAccelerationStructureBuildMode BuildMode;

		// TODO: Implement use-after-free checks in BuildRequestIndex using some bits to identify generation
	};

	void SetupBuildParams(const FBuildRequest& InBuildRequest, TArray<FRayTracingGeometryBuildParams>& InBuildParams, bool bRemoveFromRequestArray = true);

	void ReleaseRayTracingGeometryGroupReference(RayTracing::GeometryGroupHandle Handle);

	bool RequestRayTracingGeometryStreamIn(FRHICommandList& RHICmdList, RayTracingGeometryHandle GeometryHandle);
	void ProcessCompletedStreamingRequests(FRHICommandList& RHICmdList);

	FCriticalSection RequestCS;

	TSparseArray<FBuildRequest> GeometryBuildRequests;

	// Working array with all active build build params in the RHI
	TArray<FBuildRequest> SortedRequests;
	TArray<FRayTracingGeometryBuildParams> BuildParams;

	// Operations such as registering geometry/groups can be done from different render command pipes (eg: SkeletalMesh)
	// so need to use critical section in relevant functions
	FCriticalSection MainCS;

	struct FRayTracingGeometryGroup
	{
		TArray<RayTracingGeometryHandle> GeometryHandles;

		TSet<FPrimitiveSceneProxy*> ProxiesWithCachedRayTracingState;

		uint8 CurrentFirstLODIdx = INDEX_NONE;

		// Due to the way we batch release FRenderResource and SceneProxies, 
		// ReleaseRayTracingGeometryGroup(...) can end up being called before all FRayTracingGeometry and SceneProxies are actually released.
		// To deal with this, we keep track of whether the group is still referenced and only release the group handle once all references are released.
		uint32 NumReferences = 0;

		// TODO: Implement use-after-free checks in RayTracing::GeometryGroupHandle using some bits to identify generation
	};

	struct FRegisteredGeometry
	{
		FRayTracingGeometry* Geometry = nullptr;
		uint64 LastReferencedFrame = 0;
		uint32 Size = 0;

		FByteBulkData* StreamableData = nullptr;
		uint32 StreamableDataOffset = 0;
		uint32 StreamableDataSize = 0;

		int16 StreamingRequestIndex = INDEX_NONE;

		enum class FStatus : uint8
		{
			StreamedOut,
			Streaming,
			StreamedIn,
		};

		FStatus Status = FStatus::StreamedOut;
	};

	struct FStreamingRequest
	{
		FIoBuffer RequestBuffer;
		FBulkDataBatchRequest Request;

		RayTracingGeometryHandle GeometryHandle;

		bool IsValid() const { return GeometryHandle != INDEX_NONE; }

		void Reset()
		{
			GeometryHandle = INDEX_NONE;

			if (Request.IsPending())
			{
				Request.Cancel();
				Request.Wait(); // Even after calling Cancel(), we still need to Wait() before we can touch the RequestBuffer.
			}
			Request.Reset();
			RequestBuffer = {};
		}
	};

	TSparseArray<FRayTracingGeometryGroup> RegisteredGroups;

	// Used for keeping track of geometries when ray tracing is dynamic
	TSparseArray<FRegisteredGeometry> RegisteredGeometries;

	TSet<RayTracingGeometryHandle> ResidentGeometries;
	uint64 TotalResidentSize = 0;

	TSet<RayTracingGeometryHandle> AlwaysResidentGeometries;
	uint64 TotalAlwaysResidentSize = 0;

	TSet<RayTracingGeometryHandle> EvictableGeometries;

	TSet<RayTracingGeometryHandle> ReferencedGeometryHandles;
	TSet<RayTracing::GeometryGroupHandle> ReferencedGeometryGroups;

	TSet<RayTracingGeometryHandle> PendingStreamingRequests;

	TArray<FStreamingRequest> StreamingRequests;
	int32 NumStreamingRequests = 0;
	int32 NextStreamingRequestIndex = 0;

	bool bRenderedFrame = false;
};

#endif // RHI_RAYTRACING
