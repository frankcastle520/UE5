// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuixelAssetTypes.h"

#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"

TTuple<FString, FString> FQuixelAssetTypes::ExtractMeta(const FString& JsonFile)
{
	FString FileContent;
	FFileHelper::LoadFileToString(FileContent, *JsonFile);

	FAssetMetaDataJson AssetMetaDataJson;
	FJsonObjectConverter::JsonObjectStringToUStruct(FileContent, &AssetMetaDataJson);
	const FString AssetId = AssetMetaDataJson.Id;

	if (AssetMetaDataJson.Categories.Num() == 0)
	{
		return {AssetId, ""};
	}

	if (AssetMetaDataJson.Categories[0] == "3d")
	{
		return {AssetId, "3D"};
	}

	if (AssetMetaDataJson.Categories[0] == "surface")
	{
		return {AssetId, "Surfaces"};
	}

	if (AssetMetaDataJson.Categories[0] == "3dplant")
	{
		return {AssetId, "Plants"};
	}

	if (AssetMetaDataJson.Categories[0] == "atlas" && AssetMetaDataJson.Categories.Num() > 1)
	{
		if (AssetMetaDataJson.Categories[1] == "decals")
		{
			return {AssetId, "Decals"};
		}
		if (AssetMetaDataJson.Categories[1] == "imperfections")
		{
			return {AssetId, "Imperfections"};
		}
	}

	if (AssetMetaDataJson.SemanticTags.Asset_Type == "decal")
	{
		return {AssetId, "Decals"};
	}

	return {AssetId, ""};
}
