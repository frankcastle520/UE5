// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SpanAllocator.h"
#include "Containers/Map.h"
#include "SceneExtensions.h"
#include "RendererPrivateUtils.h"
#include "Matrix3x4.h"
#include "Delegates/DelegateCombinations.h"
#include "Delegates/Delegate.h"
#include "GameTime.h"

class FSkinningTransformProvider : public ISceneExtension
{
	DECLARE_SCENE_EXTENSION(RENDERER_API, FSkinningTransformProvider);

public:
	typedef FGuid FProviderId;

	struct FProviderRange
	{
		FProviderId Id;
		uint32 Count;
		uint32 Offset;
	};

	struct FProviderContext
	{
		FProviderContext(
			const TConstArrayView<FPrimitiveSceneInfo*> InPrimitives,
			const TConstArrayView<FUintVector2> InIndirections,
			float InDeltaTime,
			FRDGBuilder& InGraphBuilder,
			FRDGBufferRef InTransformBuffer
		)
		: Primitives(InPrimitives)
		, Indirections(InIndirections)
		, GraphBuilder(InGraphBuilder)
		, TransformBuffer(InTransformBuffer)
		, DeltaTime(InDeltaTime)
		{
		}

		TConstArrayView<FPrimitiveSceneInfo*> Primitives;
		TConstArrayView<FUintVector2> Indirections;

		FRDGBuilder& GraphBuilder;
		FRDGBufferRef TransformBuffer;

		float DeltaTime = 0.0f;
	};

	DECLARE_DELEGATE_OneParam(FOnProvideTransforms, FProviderContext&);

public:
	static bool ShouldCreateExtension(FScene& InScene);

	virtual void InitExtension(FScene& InScene) override;

	RENDERER_API void RegisterProvider(const FProviderId& Id, const FOnProvideTransforms& Delegate);
	RENDERER_API void UnregisterProvider(const FProviderId& Id);

	void Broadcast(const TConstArrayView<FProviderRange> Ranges, FProviderContext& Context);

	inline bool HasProviders() const
	{
		return !Providers.IsEmpty();
	}

	inline TArray<FProviderId> GetProviderIds() const
	{
		TArray<FProviderId> Ids;
		Ids.Reserve(Providers.Num());
		for (const FTransformProvider& Provider : Providers)
		{
			Ids.Add(Provider.Id);
		}
		return Ids;
	}

private:
	struct FTransformProvider
	{
		FProviderId Id;
		FOnProvideTransforms Delegate;
	};

	TArray<FTransformProvider> Providers;

	const FScene* Scene = nullptr;
};

RENDERER_API const FSkinningTransformProvider::FProviderId& GetRefPoseProviderId();
RENDERER_API const FSkinningTransformProvider::FProviderId& GetAnimRuntimeProviderId();
