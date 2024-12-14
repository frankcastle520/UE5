// Copyright Epic Games, Inc. All Rights Reserved.

#include "Editor/AssetReferenceFilter.h"
#include "AssetRegistry/AssetData.h"

IAssetReferenceFilter::FOnIsCrossPluginReferenceAllowed IAssetReferenceFilter::OnIsCrossPluginReferenceAllowedDelegate;

bool IAssetReferenceFilter::IsCrossPluginReferenceAllowed(const FAssetData& ReferencingAssetData, const FAssetData& ReferencedAssetData)
{
	return OnIsCrossPluginReferenceAllowedDelegate.IsBound() ? OnIsCrossPluginReferenceAllowedDelegate.Execute(ReferencingAssetData, ReferencedAssetData) : false;
}