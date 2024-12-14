// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeUtilsPrivate.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "LandscapeComponent.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"

#if WITH_EDITOR
#include "EditorDirectories.h"
#include "ObjectTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#endif

// Channel remapping
extern const size_t ChannelOffsets[4];

namespace UE::Landscape::Private
{

bool DoesPlatformSupportEditLayers(EShaderPlatform InShaderPlatform)
{
	// Edit layers work on the GPU and are only available on SM5+ and in the editor : 
	return IsFeatureLevelSupported(InShaderPlatform, ERHIFeatureLevel::SM5)
		&& !IsConsolePlatform(InShaderPlatform)
		&& !IsMobilePlatform(InShaderPlatform);
}

int32 ComputeMaxDeltasOffsetForMip(int32 InMipIndex, int32 InNumRelevantMips)
{
	int32 Offset = 0;
	for (int32 X = 0; X < InMipIndex; ++X)
	{
		Offset += InNumRelevantMips - 1 - X;
	}
	return Offset;
}

int32 ComputeMaxDeltasCountForMip(int32 InMipIndex, int32 InNumRelevantMips)
{
	return InNumRelevantMips - 1 - InMipIndex;
}

int32 ComputeMipToMipMaxDeltasIndex(int32 InSourceMipIndex, int32 InDestinationMipIndex, int32 InNumRelevantMips)
{
	check((InSourceMipIndex >= 0) && (InSourceMipIndex < InNumRelevantMips));
	check((InDestinationMipIndex > InSourceMipIndex) && (InDestinationMipIndex < InNumRelevantMips));
	return ComputeMaxDeltasOffsetForMip(InSourceMipIndex, InNumRelevantMips) + InDestinationMipIndex - InSourceMipIndex - 1;
}

int32 ComputeMipToMipMaxDeltasCount(int32 InNumRelevantMips)
{
	int32 Count = 0;
	for (int32 MipIndex = 0; MipIndex < InNumRelevantMips - 1; ++MipIndex)
	{
		Count += InNumRelevantMips - 1 - MipIndex;
	}
	return Count;
}

#if WITH_EDITOR

int32 LandscapeMobileWeightTextureArray = 0;
static FAutoConsoleVariableRef CVarLandscapeMobileWeightTextureArray(
	TEXT("landscape.MobileWeightTextureArray"),
	LandscapeMobileWeightTextureArray,
	TEXT("Use Texture Arrays for weights on Mobile platforms"),
	ECVF_ReadOnly | ECVF_MobileShaderChange);

bool IsMobileWeightmapTextureArrayEnabled()
{
	return LandscapeMobileWeightTextureArray != 0;	
}
	
bool UseWeightmapTextureArray(EShaderPlatform InPlatform)
{
	return IsMobilePlatform(InPlatform) && (LandscapeMobileWeightTextureArray != 0);	
}
#endif //!WITH_EDITOR

FIntPoint FLandscapeComponent2DIndexerKeyFuncs::GetKey(ULandscapeComponent* InComponent)
{
	return InComponent->GetComponentKey();
}

FLandscapeComponent2DIndexer CreateLandscapeComponent2DIndexer(const ULandscapeInfo* InInfo)
{
	TArray<ULandscapeComponent*> AllValidComponents;
	InInfo->XYtoComponentMap.GenerateValueArray(AllValidComponents);
	return FLandscapeComponent2DIndexer(AllValidComponents);
}

} // end namespace UE::Landscape::Private