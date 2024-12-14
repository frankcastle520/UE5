// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_CONTENTBROWSER_NEW_STYLE
#include "IAssetStatusInfoProvider.h"
#include "AssetRegistry/AssetData.h"

class ASSETDEFINITION_API FAssetStatusAssetDataInfoProvider : public IAssetStatusInfoProvider
{
public:
	FAssetStatusAssetDataInfoProvider(FAssetData InAssetData)
		: AssetData(InAssetData)
	{}

	virtual UPackage* FindPackage() const override;

	virtual FString TryGetFilename() const override;

private:
	FAssetData AssetData;
};
#endif
