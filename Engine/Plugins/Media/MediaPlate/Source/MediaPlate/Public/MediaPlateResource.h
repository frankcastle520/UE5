// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectPtr.h"
#include "UObject/SoftObjectPtr.h"
#include "MediaPlateResource.generated.h"

class UMediaPlaylist;
class UMediaSource;

UENUM()
enum class EMediaPlateResourceType : uint8
{
	Playlist,
	External,
	Asset
};

/**
 * Helper struct to wrap source selection functionality,
 * and enabling the usage of media source properties for places like Remote Control.
 *
 * This struct allows to choose between Asset, External File, Playlist options.
 * It's mainly conceived to be used by MediaPlateComponent.
 *
 * See FMediaPlayerResourceCustomization class for its customization.
 */
USTRUCT(BlueprintType)
struct FMediaPlateResource
{
	GENERATED_BODY()

public:
	/**
	 * Returns the currently selected Media Source, based on current source Type.
	 * If current type is Playlist, it will return nullptr
	 */
	MEDIAPLATE_API UMediaSource* GetSelectedMedia() const;

	/**
	 * Returns the current source playlist, if any
	 */
	MEDIAPLATE_API UMediaPlaylist* GetSourcePlaylist() const;

	/**
	 * Returns the active playlist, if any
	 */
	MEDIAPLATE_API UMediaPlaylist* GetActivePlaylist() const;

	/**
	 * Returns the current external media path, if any
	 */
	MEDIAPLATE_API FStringView GetExternalMediaPath() const { return ExternalMediaPath; }

	/**
	 * Returns the current asset-based Media Source, if any
	 */
	MEDIAPLATE_API UMediaSource* GetMediaAsset() const;

	/**
	 * Return current resource type
	 */
	MEDIAPLATE_API EMediaPlateResourceType GetResourceType() const { return Type; }

	/**
	 * Set current resource type
	 */
	MEDIAPLATE_API void SetResourceType(EMediaPlateResourceType InType);

	/**
	 * Select asset based media source. Will also update source type to Asset
	 */
	MEDIAPLATE_API void SelectAsset(const UMediaSource* InMediaSource, UObject* InOuter);

	/**
	 * Loads the external source at the specified path, creating a media source with the specified outer.
	 * Will also update source type to External
	 */
	MEDIAPLATE_API void LoadExternalMedia(const FString& InFilePath, UObject* InOuter);

	/**
	 * Select the specified playlist. Will also update source type to Playlist
	 */
	MEDIAPLATE_API void SelectPlaylist(const UMediaPlaylist* InPlaylist);

#if WITH_EDITOR
	/**
	 * Note that the Active Playlist will be modified.
	 * Convenience function to avoid having to check and get the Playlist everytime to call Modify() on it.
	 */
	MEDIAPLATE_API void Modify() const;
#endif

private:
friend class UMediaPlateComponent;
	/**
	 * Initialize member properties from another MediaPlayerResource. Empty or null property will not be copied over.
	 */
	void Init(const FMediaPlateResource& InOther);

	void RefreshActivePlaylist(UObject* InOuter);

	/** Media Source Type */
	UPROPERTY()
	EMediaPlateResourceType Type = EMediaPlateResourceType::Asset;

	/** A path pointing to an external media resource */
	UPROPERTY()
	FString ExternalMediaPath;

	/** Media Source loaded from external path */
	UPROPERTY(Instanced)
	TObjectPtr<UMediaSource> ExternalMedia;

	/** Media source coming from MediaSource asset*/
	UPROPERTY()
	TSoftObjectPtr<UMediaSource> MediaAsset;

	/** User facing Playlist asset */
	UPROPERTY()
	TSoftObjectPtr<UMediaPlaylist> SourcePlaylist;

	/** Currently running Playlist asset */
	UPROPERTY(Instanced)
	TObjectPtr<UMediaPlaylist> ActivePlaylist;
};
