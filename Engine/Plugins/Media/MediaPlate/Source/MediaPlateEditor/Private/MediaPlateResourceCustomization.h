// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"
#include "MediaPlateComponent.h"

class IPropertyHandle;
class UMediaSource;
struct EVisibility;
struct FMediaPlateResource;

class FMediaPlateResourceCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ Begin IPropertyTypeCustomization
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle
		, FDetailWidgetRow& InHeaderRow
		, IPropertyTypeCustomizationUtils& InStructCustomizationUtils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle
		, IDetailChildrenBuilder& InChildBuilder
		, IPropertyTypeCustomizationUtils& InCustomizationUtils) override;
	//~ End IPropertyTypeCustomization

private:
	EMediaPlateResourceType GetAssetType() const;

	FString GetMediaAssetPath() const;
	FString GetPlaylistPath() const;
	FString GetMediaPath() const;

	void OnAssetTypeChanged(EMediaPlateResourceType InMediaSourceType);
	void OnPlaylistChanged(const FAssetData& InAssetData);
	void OnMediaAssetChanged(const FAssetData& InAssetData);
	void OnMediaPathPicked(const FString& InPickedPath);

	EVisibility GetAssetSelectorVisibility() const;
	EVisibility GetFileSelectorVisibility() const;
	EVisibility GetPlaylistSelectorVisibility() const;
	FMediaPlateResource* GetMediaPlateSourcePtr() const;
	UObject* GetMediaPlateResourceOwner() const;

	TSharedPtr<IPropertyHandle> MediaPlateResourcePropertyHandle;
};
