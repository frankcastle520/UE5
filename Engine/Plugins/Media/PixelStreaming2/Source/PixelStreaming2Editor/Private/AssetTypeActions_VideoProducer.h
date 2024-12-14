// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetTypeActions_Base.h"
#include "Blueprints/PixelStreaming2VideoProducer.h"
#include "CoreMinimal.h"
#include "Toolkits/IToolkitHost.h"

class FAssetTypeActions_VideoProducer : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("PixelStreaming2", "AssetTypeActions_VideoProducer", "Video Input Actions"); }
	virtual FColor GetTypeColor() const override { return FColor(192,64,64); }
	virtual UClass* GetSupportedClass() const override { return UPixelStreaming2VideoProducer::StaticClass(); }
	virtual bool CanFilter() override { return false; }
	virtual uint32 GetCategories() override
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		return AssetTools.RegisterAdvancedAssetCategory("PixelStreaming2", NSLOCTEXT("PixelStreaming2", "AssetCategoryDisplayName", "PixelStreaming2"));
		//return EAssetTypeCategories::Misc;
	}
	virtual bool IsImportedAsset() const override { return false; }
};
