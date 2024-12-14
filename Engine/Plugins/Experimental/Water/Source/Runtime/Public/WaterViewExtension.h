// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneViewExtension.h"
#include "WaterInfoRendering.h"
#include "Misc/ScopeLock.h"


class AWaterZone;
class FWaterMeshSceneProxy;

class FWaterViewExtension : public FWorldSceneViewExtension
{
public:
	FWaterViewExtension(const FAutoRegister& AutoReg, UWorld* InWorld);
	~FWaterViewExtension();

	void Initialize();
	void Deinitialize();

	// FSceneViewExtensionBase implementation : 
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override;
	virtual void PreRenderBasePass_RenderThread(FRDGBuilder& GraphBuilder, bool bDepthBufferIsPopulated) override;
	// End FSceneViewExtensionBase implementation

	void MarkWaterInfoTextureForRebuild(const UE::WaterInfo::FRenderingContext& RenderContext);

	void MarkGPUDataDirty();

	void AddWaterZone(AWaterZone* InWaterZone);
	void RemoveWaterZone(AWaterZone* InWaterZone);

	FVector GetZoneLocation(const AWaterZone* InWaterZone, int32 PlayerIndex) const;

	void CreateSceneProxyQuadtrees(FWaterMeshSceneProxy* SceneProxy);

	struct FWaterZoneInfo
	{
		UE::WaterInfo::FRenderingContext RenderContext;

		/**
		 * For each water zone, per view: store the bounds of the tile from which the water zone was last rendered.
		 * When the view location crosses the bounds, submit a new update to reflect the new active area
		 */
		struct FWaterZoneViewInfo
		{
			TOptional<FBox2D> UpdateBounds = FBox2D(ForceInit);
			FVector Center = FVector(ForceInit);
			FWaterMeshSceneProxy* OldSceneProxy = nullptr;
			bool bIsDirty = true;
		};
		TArray<FWaterZoneViewInfo, TInlineAllocator<4>> ViewInfos;
	};
	TWeakObjectPtrKeyMap<AWaterZone, FWaterZoneInfo> WaterZoneInfos;

private:
	int32 CurrentNumViews = 0;

	struct FWaterGPUResources
	{
		FBufferRHIRef WaterBodyDataBuffer;
		FShaderResourceViewRHIRef WaterBodyDataSRV;

		FBufferRHIRef AuxDataBuffer;
		FShaderResourceViewRHIRef AuxDataSRV;
	};

	TSharedRef<FWaterGPUResources, ESPMode::ThreadSafe> WaterGPUData;

	TArray<int32, TInlineAllocator<4>> ViewPlayerIndices;

	struct FQuadtreeUpdateInfo
	{
		AWaterZone* WaterZone;
		FVector2D Location;
		int32 Key;
	};

	TArray<FQuadtreeUpdateInfo, TInlineAllocator<4>> QuadtreeUpdates;

	FCriticalSection QuadtreeUpdateLock;

	TMap<FSceneViewStateInterface*, int32> NonDataViewsQuadtreeKeys;

	bool bWaterInfoTextureRebuildPending = true;

	bool bRebuildGPUData = true;

	// store the locations of every active water mesh scene proxy quad tree based on the key
	TMap<int32, FVector2D> QuadTreeKeyLocationMap;

	void UpdateGPUBuffers();

	void UpdateViewInfo(AWaterZone* WaterZone, FSceneView& InView);
	
	void RenderWaterInfoTexture(FSceneViewFamily& InViewFamily, FSceneView& InView, const FWaterZoneInfo* WaterZoneInfo, FSceneInterface* Scene, const FVector& ZoneCenter);

	// Returns the index in the views array corresponding to InView's PlayerIndex. If the index is not found it adds a new entry.
	int32 GetOrAddViewindex(const FSceneView& InView);
	// Returns the index in the views array corresponding to InView's PlayerIndex. INDEX_NONE if it doesn't find it
	int32 GetViewIndex(int32 PlayerIndex) const;
	int32 GetViewIndex(const FSceneView& InView) const;

	void DrawDebugInfo(FSceneView& InView, AWaterZone* WaterZone);
};

struct FWaterMeshGPUWork
{
	struct FCallback
	{
		class FWaterMeshSceneProxy* Proxy = nullptr;
		TFunction<void(FRDGBuilder&, bool)> Function;
	};
	TArray<FCallback> Callbacks;
};

extern FWaterMeshGPUWork GWaterMeshGPUWork;

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
