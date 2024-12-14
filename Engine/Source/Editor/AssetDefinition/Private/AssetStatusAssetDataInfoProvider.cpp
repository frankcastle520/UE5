// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetStatusAssetDataInfoProvider.h"

#if UE_CONTENTBROWSER_NEW_STYLE
#include "AssetRegistry/AssetData.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"

UPackage* FAssetStatusAssetDataInfoProvider::FindPackage() const
{
	return FindObjectSafe<UPackage>(NULL, *FNameBuilder(AssetData.PackageName), true);
}

FString FAssetStatusAssetDataInfoProvider::TryGetFilename() const
{
	FString OutFileName;
	const FString* PackageExtension = AssetData.HasAnyPackageFlags(PKG_ContainsMap) ? &FPackageName::GetMapPackageExtension() : &FPackageName::GetAssetPackageExtension();
	FPackageName::TryConvertLongPackageNameToFilename(AssetData.PackageName.ToString(), OutFileName, *PackageExtension);
	return OutFileName;
}
#endif
