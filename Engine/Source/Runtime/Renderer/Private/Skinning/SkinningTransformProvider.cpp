// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkinningTransformProvider.h"
#include "ScenePrivate.h"
#include "RenderUtils.h"
#include "SkeletalRenderPublic.h"
#include "Nanite/NaniteSkinningSceneExtension.h"

IMPLEMENT_SCENE_EXTENSION(FSkinningTransformProvider);

bool FSkinningTransformProvider::ShouldCreateExtension(FScene& InScene)
{
	return NaniteSkinnedMeshesSupported() && DoesRuntimeSupportNanite(GetFeatureLevelShaderPlatform(InScene.GetFeatureLevel()), true, true);
}

void FSkinningTransformProvider::InitExtension(FScene& InScene)
{
	Scene = &InScene;
}

void FSkinningTransformProvider::RegisterProvider(const FSkinningTransformProvider::FProviderId& Id, const FOnProvideTransforms& Delegate)
{
#if DO_CHECK
	for (const FTransformProvider& ProviderCheck : Providers)
	{
		check(ProviderCheck.Id != Id);
	}
#endif

	check(Delegate.IsBound());
	FTransformProvider& Provider = Providers.Emplace_GetRef();
	Provider.Id = Id;
	Provider.Delegate = Delegate;
}

void FSkinningTransformProvider::UnregisterProvider(const FSkinningTransformProvider::FProviderId& Id)
{
	for (int32 ProviderIndex = 0; ProviderIndex < Providers.Num(); ++ProviderIndex)
	{
		const FTransformProvider& Provider = Providers[ProviderIndex];
		if (Provider.Id == Id)
		{
			Providers.RemoveAtSwap(ProviderIndex);
			return;
		}
	}

	checkNoEntry(); // No provider found with this id - error!
}

void FSkinningTransformProvider::Broadcast(const TConstArrayView<FProviderRange> Ranges, FProviderContext& Context)
{
	const TConstArrayView<FUintVector2> IndirectionView = Context.Indirections;

	for (const FTransformProvider& Provider : Providers)
	{
		for (const FProviderRange& Range : Ranges)
		{
			if (Provider.Id == Range.Id)
			{
				if (Range.Count > 0)
				{
					Context.Indirections = MakeArrayView(IndirectionView.GetData() + Range.Offset, Range.Count);
					Provider.Delegate.ExecuteIfBound(Context);
				}
				break;
			}
		}
	}
}

const FSkinningTransformProvider::FProviderId& GetRefPoseProviderId()
{
	// TODO: Temp until skinning scene extension is refactored into a public API outside of Nanite
	return Nanite::FSkinningSceneExtension::GetRefPoseProviderId();
}

const FSkinningTransformProvider::FProviderId& GetAnimRuntimeProviderId()
{
	// TODO: Temp until skinning scene extension is refactored into a public API outside of Nanite
	return Nanite::FSkinningSceneExtension::GetAnimRuntimeProviderId();
}