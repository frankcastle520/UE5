// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDTranslatorUtils.h"

#include "USDAssetCache3.h"
#include "USDPrimLinkCache.h"

#include "AssetRegistry/AssetRegistryModule.h"

void UsdUnreal::TranslatorUtils::AbandonFailedAsset(UObject* Asset, UUsdAssetCache3* AssetCache, FUsdPrimLinkCache* PrimLinkCache)
{
	if (!Asset)
	{
		return;
	}

	// These come from the internals of ObjectTools::DeleteSingleObject
	Asset->MarkPackageDirty();
#if WITH_EDITOR
	FAssetRegistryModule::AssetDeleted(Asset);
#endif	  // WITH_EDITOR
	Asset->ClearFlags(RF_Standalone | RF_Public);

	Asset->MarkAsGarbage();

	if (AssetCache)
	{
		const FString Hash = AssetCache->GetHashForAsset(Asset);
		if (!Hash.IsEmpty())
		{
			AssetCache->StopTrackingAsset(Hash);
		}
	}

	if (PrimLinkCache)
	{
		PrimLinkCache->RemoveAllAssetPrimLinks(Asset);
	}
}
