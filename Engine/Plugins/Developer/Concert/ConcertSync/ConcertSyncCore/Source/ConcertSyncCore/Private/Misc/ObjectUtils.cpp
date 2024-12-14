// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/ObjectUtils.h"

#include "Containers/UnrealString.h"
#include "UObject/SoftObjectPath.h"

namespace UE::ConcertSyncCore
{
	bool IsActor(const FSoftObjectPath& SoftObjectPath)
	{
		// Example of an actor called floor
		// SoftObjectPath = { AssetPath = {PackageName = "/Game/Maps/SyncBoxLevel", AssetName = "SyncBoxLevel"}, SubPathString = "PersistentLevel.Floor" } }
		const FString& SubPathString = SoftObjectPath.GetSubPathString();

		constexpr int32 PersistentLevelStringLength = 16; // "PersistentLevel." has 16 characters
		const bool bIsWorldObject = SubPathString.Contains(TEXT("PersistentLevel."), ESearchCase::CaseSensitive);
		if (!bIsWorldObject)
		{
			// Not a path to a world object
			return {};
		}

		// Start search after the . behind PersistentLevel
		const int32 StartSearch = PersistentLevelStringLength + 1;
		const int32 IndexOfDotAfterActorName = SubPathString.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, StartSearch);
		return IndexOfDotAfterActorName == INDEX_NONE;
	}
	
	TOptional<FSoftObjectPath> GetActorOf(const FSoftObjectPath& SoftObjectPath)
	{
		// Example of an actor called floor
		// SoftObjectPath = { AssetPath = {PackageName = "/Game/Maps/SyncBoxLevel", AssetName = "SyncBoxLevel"}, SubPathString = "PersistentLevel.Floor" } }
		const FString& SubPathString = SoftObjectPath.GetSubPathString();

		constexpr int32 PersistentLevelStringLength = 16; // "PersistentLevel." has 16 characters
		const bool bIsWorldObject = SubPathString.Contains(TEXT("PersistentLevel."), ESearchCase::CaseSensitive);
		if (!bIsWorldObject)
		{
			// Not a path to a world object
			return {};
		}

		// Start search after the . behind PersistentLevel
		const int32 StartSearch = PersistentLevelStringLength + 1;
		const int32 IndexOfDotAfterActorName = SubPathString.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, StartSearch);
		if (IndexOfDotAfterActorName == INDEX_NONE)
		{
			// SoftObjectPath points to an actor
			return {};
		}

		const int32 NumToChopOffRight = SubPathString.Len() - IndexOfDotAfterActorName;
		const FString NewSubstring = SubPathString.LeftChop(NumToChopOffRight);
		const FSoftObjectPath PathToOwningActor(SoftObjectPath.GetAssetPath(), NewSubstring);
		return PathToOwningActor;
	}
	
	FString ExtractObjectNameFromPath(const FSoftObjectPath& Object)
	{
		// Subpath looks like this PersistentLevel.Actor.Component
		const FString& Subpath = Object.GetSubPathString();
		const int32 LastDotIndex = Subpath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (LastDotIndex == INDEX_NONE)
		{
			return {};
		}
		return Subpath.RightChop(LastDotIndex + 1);
	}

	TOptional<FSoftObjectPath> ReplaceActorInPath(const FSoftObjectPath& OldPath, const FSoftObjectPath& NewActor)
	{
		if (!IsActor(NewActor))
		{
			return {};
		}
		
		// Example of an actor called floor
		// SoftObjectPath = { AssetPath = {PackageName = "/Game/Maps/SyncBoxLevel", AssetName = "SyncBoxLevel"}, SubPathString = "PersistentLevel.Floor" } }
		const FString& OldSubPathString = OldPath.GetSubPathString();
		if (!OldSubPathString.Contains(TEXT("PersistentLevel."), ESearchCase::CaseSensitive))
		{
			return {};
		}
		const FString& NewSubPathString = NewActor.GetSubPathString();
		
		constexpr int32 PersistentLevelStringLength = 16; // "PersistentLevel." has 16 characters
		constexpr int32 FirstActorCharIndex = PersistentLevelStringLength + 1;
		const int32 IndexOfDotAfterActorName_OldSubPath = OldSubPathString.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, FirstActorCharIndex);
		
		const bool bOldIsOnlyActor = !OldSubPathString.IsValidIndex(IndexOfDotAfterActorName_OldSubPath);
		if (bOldIsOnlyActor)
		{
			return NewActor;
		}

		const FString ReplacedSubPathString = NewSubPathString
			+ TEXT(".")
			+ OldSubPathString.RightChop(IndexOfDotAfterActorName_OldSubPath + 1);
		return FSoftObjectPath(NewActor.GetAssetPath(), ReplacedSubPathString);
	}
};

