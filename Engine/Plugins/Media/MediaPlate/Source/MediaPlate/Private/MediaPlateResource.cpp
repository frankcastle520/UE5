// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlateResource.h"
#include "MediaPlaylist.h"
#include "MediaSource.h"

void FMediaPlateResource::SetResourceType(EMediaPlateResourceType InType)
{
	Type = InType;

	if (const UMediaPlaylist* Playlist = GetActivePlaylist())
	{
		RefreshActivePlaylist(Playlist->GetOuter());
	}
}

void FMediaPlateResource::SelectAsset(const UMediaSource* InMediaSource, UObject* InOuter)
{
	SetResourceType(EMediaPlateResourceType::Asset);
	MediaAsset = const_cast<UMediaSource*>(InMediaSource);
	RefreshActivePlaylist(InOuter);
}

void FMediaPlateResource::LoadExternalMedia(const FString& InFilePath, UObject* InOuter)
{
	SetResourceType(EMediaPlateResourceType::External);
	ExternalMediaPath = InFilePath;
	ExternalMedia = UMediaSource::SpawnMediaSourceForString(InFilePath, InOuter);
	RefreshActivePlaylist(InOuter);
}

void FMediaPlateResource::SelectPlaylist(const UMediaPlaylist* InPlaylist)
{
	SetResourceType(EMediaPlateResourceType::Playlist);
	SourcePlaylist = const_cast<UMediaPlaylist*>(InPlaylist);
	ActivePlaylist = GetSourcePlaylist();
}

#if WITH_EDITOR
void FMediaPlateResource::Modify() const
{
	if (ActivePlaylist)
	{
		ActivePlaylist->Modify();
	}
}
#endif

void FMediaPlateResource::Init(const FMediaPlateResource& InOther)
{
	if (!ExternalMediaPath.IsEmpty())
	{
		ExternalMediaPath = FString(InOther.GetExternalMediaPath());
	}

	if (UMediaSource* OtherMediaAsset = InOther.GetMediaAsset())
	{
		MediaAsset = OtherMediaAsset;
	}

	if (UMediaPlaylist* OtherMediaPlaylist = InOther.GetSourcePlaylist())
	{
		SourcePlaylist = OtherMediaPlaylist;
	}

	Type = InOther.GetResourceType();
}

void FMediaPlateResource::RefreshActivePlaylist(UObject* InOuter)
{
	if (Type == EMediaPlateResourceType::Playlist)
	{
		ActivePlaylist = GetSourcePlaylist();
	}
	else if (UMediaSource* MediaSource = GetSelectedMedia())
	{
		if (InOuter)
		{
			ActivePlaylist = nullptr;
			ActivePlaylist = NewObject<UMediaPlaylist>(InOuter, NAME_None, RF_Transactional);
			ActivePlaylist->Add(MediaSource);
		}
	}
}

UMediaSource* FMediaPlateResource::GetMediaAsset() const
{
	return MediaAsset.LoadSynchronous();
}

UMediaPlaylist* FMediaPlateResource::GetSourcePlaylist() const
{
	return SourcePlaylist.LoadSynchronous();
}

UMediaPlaylist* FMediaPlateResource::GetActivePlaylist() const
{
	return ActivePlaylist;
}

UMediaSource* FMediaPlateResource::GetSelectedMedia() const
{
	switch (Type)
	{
		case EMediaPlateResourceType::Playlist:
			return nullptr;

		case EMediaPlateResourceType::External:
			return ExternalMedia;

		case EMediaPlateResourceType::Asset:
			return MediaAsset.LoadSynchronous();

		default:
			return nullptr;
	}
}
