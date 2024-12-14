// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StructUtils/InstancedStruct.h"
#include "WorkspaceAssetRegistryInfo.generated.h"

USTRUCT()
struct FWorkspaceOutlinerItemData
{
	GENERATED_BODY()

	FWorkspaceOutlinerItemData() = default;
};

USTRUCT()
struct FOutlinerItemPath
{
	GENERATED_BODY()
	
	friend struct FWorkspaceOutlinerItemExport;
protected:
	UPROPERTY()
	TArray<FName> PathSegments;

public:
	static FOutlinerItemPath MakePath(const FSoftObjectPath& InSoftObjectPath)
	{
		FOutlinerItemPath Path;
		Path.PathSegments.Add(*InSoftObjectPath.ToString());
		return Path;
	}

	FOutlinerItemPath AppendSegment(const FName& InSegment) const
	{
		FOutlinerItemPath Path = *this;
		Path.PathSegments.Add(InSegment);
		return Path;
	}

	FOutlinerItemPath RemoveSegment() const
	{
		FOutlinerItemPath Path = *this;
		if (Path.PathSegments.Num())
		{
			Path.PathSegments.Pop();
		}			
		return Path;
	}

	friend uint32 GetTypeHash(const FOutlinerItemPath& Path)
	{
		uint32 Hash = INDEX_NONE;

		if (Path.PathSegments.Num() == 1)
		{
			Hash = GetTypeHash(Path.PathSegments[0]);
		}
		else if (Path.PathSegments.Num() > 1)
		{
			Hash = GetTypeHash(Path.PathSegments[0]);
			for (int32 Index = 1; Index < Path.PathSegments.Num(); ++Index)
			{
				Hash = HashCombine(Hash, GetTypeHash(Path.PathSegments[Index]));				
			}
			return Hash;
		}
		
		return Hash;
	}
};

USTRUCT()
struct FWorkspaceOutlinerItemExport
{
	GENERATED_BODY()

	FWorkspaceOutlinerItemExport() = default;

	FWorkspaceOutlinerItemExport(const FName InIdentifier, const FSoftObjectPath& InObjectPath)
	{
		Path.PathSegments.Add(*InObjectPath.ToString());
		Path.PathSegments.Add(InIdentifier);
	}
	
	FWorkspaceOutlinerItemExport(const FName InIdentifier, const FWorkspaceOutlinerItemExport& InParent)
	{
		Path = InParent.Path.AppendSegment(InIdentifier);
	}

protected:
	/** Full 'path' to item this instance represents, expected to take form of AssetPath follow by a set of identifier names */
	UPROPERTY()
	FOutlinerItemPath Path;

	UPROPERTY()
	TInstancedStruct<FWorkspaceOutlinerItemData> Data;
public:
	FName GetIdentifier() const
	{
		// Path needs atleast two segments to contain a valid identifier
		if(Path.PathSegments.Num() > 1)
		{
			return Path.PathSegments.Last();	
		}

		return NAME_None;
	}
	
	FName GetParentIdentifier() const
	{
		// Path needs atleast three segments to contain a valid _parent_ identifier
		const int32 NumSegments = Path.PathSegments.Num();
		if (NumSegments > 2)
		{
			return Path.PathSegments[FMath::Max(NumSegments - 2, 0)];
		}

		return NAME_None;
	}
	
	FSoftObjectPath GetAssetPath() const
	{
		// Path needs atleast one segment to contain a (potentially) valid asset path
		if(Path.PathSegments.Num() > 0)
		{
			return FSoftObjectPath(Path.PathSegments[0].ToString());	
		}

		return FSoftObjectPath();
	}
	
	// Remove identifier segment to retrieve parent path hash
	uint32 GetParentHash() const { return GetTypeHash(Path.RemoveSegment()); }
	
	const TInstancedStruct<FWorkspaceOutlinerItemData>& GetData() const { return Data; }
	TInstancedStruct<FWorkspaceOutlinerItemData>& GetData() { return Data; }	

	friend uint32 GetTypeHash(const FWorkspaceOutlinerItemExport& Export)
	{
		return GetTypeHash(Export.Path);
	}
};

namespace UE::Workspace
{

static const FLazyName ExportsWorkspaceItemsRegistryTag = TEXT("WorkspaceItemExports");

}

USTRUCT()
struct FWorkspaceOutlinerItemExports
{
	GENERATED_BODY()

	FWorkspaceOutlinerItemExports() = default;

	UPROPERTY()
	TArray<FWorkspaceOutlinerItemExport> Exports;
};
