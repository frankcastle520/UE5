// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/DMTextureSetBuilderEntry.h"

#include "DMTextureSetMaterialProperty.h"

FDMTextureSetBuilderEntry::FDMTextureSetBuilderEntry()
	: FDMTextureSetBuilderEntry(EDMTextureSetMaterialProperty::BaseColor, nullptr, EDMTextureChannelMask::RGBA)
{
}

FDMTextureSetBuilderEntry::FDMTextureSetBuilderEntry(EDMTextureSetMaterialProperty InMaterialProperty, UTexture* InTexture, EDMTextureChannelMask InMask)
	: MaterialProperty(InMaterialProperty)
	, Texture(InTexture)
	, ChannelMask(InMask)
{
}

FDMTextureSetBuilderEntryProvider::FDMTextureSetBuilderEntryProvider(const TSharedRef<FDMTextureSetBuilderEntry>& InEntry)
	: Entry(InEntry)
{
}

bool FDMTextureSetBuilderEntryProvider::IsValid() const
{
	return true;
}

const UStruct* FDMTextureSetBuilderEntryProvider::GetBaseStructure() const
{
	return FDMTextureSetBuilderEntry::StaticStruct();
}

void FDMTextureSetBuilderEntryProvider::GetInstances(TArray<TSharedPtr<FStructOnScope>>& OutInstances, const UStruct* ExpectedBaseStructure) const
{
	OutInstances.Add(MakeShared<FStructOnScope>(
		FDMTextureSetBuilderEntry::StaticStruct(),
		reinterpret_cast<uint8*>(&Entry.Get())
	));
}

bool FDMTextureSetBuilderEntryProvider::IsPropertyIndirection() const
{
	return false;
}
