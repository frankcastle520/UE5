// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetRegistry/AssetRegistryState.h"

#include "Algo/Compare.h"
#include "Algo/Sort.h"
#include "Algo/Unique.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistryArchive.h"
#include "AssetRegistryImpl.h"
#include "AssetRegistryPrivate.h"
#include "Blueprint/BlueprintSupport.h"
#include "DependsNode.h"
#include "GenericPlatform/GenericPlatformChunkInstall.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/StringBuilder.h"
#include "NameTableArchive.h"
#include "AssetRegistry/PackageReader.h"
#include "Serialization/ArrayReader.h"
#include "Serialization/LargeMemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/MetaData.h"
#include "UObject/NameBatchSerialization.h"
#include "UObject/PrimaryAssetId.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

// Even if bSerializeDependencies is enabled, this can bypass serializing at runtime.
// Update your <project>.Target.cs build scripts to define as needed.
#ifndef ASSET_REGISTRY_ALLOW_DEPENDENCY_SERIALIZATION
#define ASSET_REGISTRY_ALLOW_DEPENDENCY_SERIALIZATION 1
#endif

FAssetRegistryState& FAssetRegistryState::operator=(FAssetRegistryState&& Rhs)
{
	Reset();

	CachedAssets						= MoveTemp(Rhs.CachedAssets);
#if UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	IndirectAssetDataArrays				= MoveTemp(Rhs.IndirectAssetDataArrays);
#endif
	CachedAssetsByPackageName			= MoveTemp(Rhs.CachedAssetsByPackageName);
	CachedAssetsByPath					= MoveTemp(Rhs.CachedAssetsByPath);
	CachedAssetsByClass					= MoveTemp(Rhs.CachedAssetsByClass);
#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
	CachedAssetsByTag					= MoveTemp(Rhs.CachedAssetsByTag);
#else
	CachedClassesByTag					= MoveTemp(Rhs.CachedClassesByTag);
#endif
	CachedDependsNodes					= MoveTemp(Rhs.CachedDependsNodes);
	CachedPackageData					= MoveTemp(Rhs.CachedPackageData);
	PreallocatedAssetDataBuffers		= MoveTemp(Rhs.PreallocatedAssetDataBuffers);
	PreallocatedDependsNodeDataBuffers	= MoveTemp(Rhs.PreallocatedDependsNodeDataBuffers);
	PreallocatedPackageDataBuffers		= MoveTemp(Rhs.PreallocatedPackageDataBuffers);
	Swap(NumAssets,				Rhs.NumAssets);
	Swap(NumDependsNodes,		Rhs.NumDependsNodes);
	Swap(NumPackageData,		Rhs.NumPackageData);

	return *this;
}

FAssetRegistryState::~FAssetRegistryState()
{
	Reset();
}

void FAssetRegistryState::Reset()
{
	// if we have preallocated all the FAssetData's in a single block, free it now, instead of one at a time
	if (PreallocatedAssetDataBuffers.Num())
	{
		for (FAssetData* Buffer : PreallocatedAssetDataBuffers)
		{
			delete[] Buffer;
		}
		PreallocatedAssetDataBuffers.Reset();

		NumAssets = 0;
	}
	else
	{
		// Delete all assets in the cache
		for (FAssetData* AssetData : CachedAssets)
		{
			delete AssetData;
			NumAssets--;
		}
	}

	// Make sure we have deleted all our allocated FAssetData objects
	// March 2021: Temporarily remove this ensure to allow passing builds  while we find and fix the cause
	// TODO: Restore the ensure
	// ensure(NumAssets == 0);
	UE_CLOG(NumAssets != 0, LogAssetRegistry, Display,
		TEXT("AssetRegistryState::Reset: NumAssets does not match the number of CachedAssets entries. Leaking some allocations."));

	if (PreallocatedDependsNodeDataBuffers.Num())
	{
		for (FDependsNode* Buffer : PreallocatedDependsNodeDataBuffers)
		{
			delete[] Buffer;
		}
		PreallocatedDependsNodeDataBuffers.Reset();
		NumDependsNodes = 0;
	}
	else
	{
		// Delete all depends nodes in the cache
		for (TMap<FAssetIdentifier, FDependsNode*>::TConstIterator DependsIt(CachedDependsNodes); DependsIt; ++DependsIt)
		{
			if (DependsIt.Value())
			{
				delete DependsIt.Value();
				NumDependsNodes--;
			}
		}
	}

	// Make sure we have deleted all our allocated FDependsNode objects
	ensure(NumDependsNodes == 0);

	if (PreallocatedPackageDataBuffers.Num())
	{
		for (FAssetPackageData* Buffer : PreallocatedPackageDataBuffers)
		{
			delete[] Buffer;
		}
		PreallocatedPackageDataBuffers.Reset();
		NumPackageData = 0;
	}
	else
	{
		// Delete all depends nodes in the cache
		for (TMap<FName, FAssetPackageData*>::TConstIterator PackageDataIt(CachedPackageData); PackageDataIt; ++PackageDataIt)
		{
			if (PackageDataIt.Value())
			{
				delete PackageDataIt.Value();
				NumPackageData--;
			}
		}
	}

	// Make sure we have deleted all our allocated package data objects
	ensure(NumPackageData == 0);

	// Clear cache
	CachedAssetsByPackageName.Empty();
	CachedAssetsByPath.Empty();
	CachedAssetsByClass.Empty();
#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
	CachedAssetsByTag.Empty();
#else
	CachedClassesByTag.Empty();
#endif
	CachedDependsNodes.Empty();
	CachedPackageData.Empty();
#if UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	IndirectAssetDataArrays.Empty();
#endif
	CachedAssets.Empty();
}

void FAssetRegistryState::FilterTags(const FAssetDataTagMapSharedView& InTagsAndValues, FAssetDataTagMap& OutTagsAndValues,
	const TSet<FName>* ClassSpecificFilterList, const FAssetRegistrySerializationOptions& Options)
{
	const TSet<FName>* AllClassesFilterList = Options.CookFilterlistTagsByClass.Find(UE::AssetRegistry::WildcardPathName);

	TStringBuilder<64> TagNameStr;

	// Exclude denied tags or include only allowed tags, based on how we were configured in ini
	for (const auto& TagPair : InTagsAndValues)
	{
		bool bKeep = false;

		// Cook_ tags, aka DevelopmentAssetRegistryTags are special; they are kept depending on whether the Options
		// are development or runtime and they do not use the options' filter list.
		TagNameStr.Reset();
		TagNameStr << TagPair.Key;
		if (FStringView(TagNameStr).StartsWith(UE::AssetRegistry::CookTagPrefix, ESearchCase::IgnoreCase))
		{
			bKeep = Options.bKeepDevelopmentAssetRegistryTags;
		}
		else
		{
			const bool bInAllClassesList = AllClassesFilterList
				&& (AllClassesFilterList->Contains(TagPair.Key)
					|| AllClassesFilterList->Contains(UE::AssetRegistry::WildcardFName));
			const bool bInClassSpecificList = ClassSpecificFilterList
				&& (ClassSpecificFilterList->Contains(TagPair.Key)
					|| ClassSpecificFilterList->Contains(UE::AssetRegistry::WildcardFName));
			if (Options.bUseAssetRegistryTagsAllowListInsteadOfDenyList)
			{
			// It's an allow list, only include it if it is in the all classes list or in the class specific list
				bKeep = bInAllClassesList || bInClassSpecificList;
			}
			else
			{
				// It's a deny list, include it unless it is in the all classes list or in the class specific list
				bKeep = !bInAllClassesList && !bInClassSpecificList;
			}
		}
		if (bKeep)
		{
			OutTagsAndValues.Add(TagPair.Key, TagPair.Value.ToLoose());
		}
	}
}

void FAssetRegistryState::InitializeFromExistingAndPrune(const FAssetRegistryState & ExistingState,
	const TSet<FName>& RequiredPackages, const TSet<FName>& RemovePackages,
	const TSet<int32> ChunksToKeep, const FAssetRegistrySerializationOptions& Options)
{
	const bool bIsFilteredByChunkId = ChunksToKeep.Num() != 0;
	const bool bIsFilteredByRequiredPackages = RequiredPackages.Num() != 0;
	const bool bIsFilteredByRemovedPackages = RemovePackages.Num() != 0;

	TSet<FName> RequiredDependNodePackages;

	// Duplicate asset data entries
	ExistingState.EnumerateAllMutableAssets(
		[&RequiredPackages, &RemovePackages, &ChunksToKeep, &Options,
		bIsFilteredByChunkId, bIsFilteredByRequiredPackages, bIsFilteredByRemovedPackages,
		&RequiredDependNodePackages, this]
		(FAssetData& AssetData)
	{
		bool bRemoveAssetData = false;
		bool bRemoveDependencyData = true;

		if (bIsFilteredByChunkId &&
			!AssetData.GetChunkIDs().ContainsByPredicate([&ChunksToKeep](int32 ChunkId)
				{
					return ChunksToKeep.Contains(ChunkId);
				}))
		{
			bRemoveAssetData = true;
		}
		else if (bIsFilteredByRequiredPackages && !RequiredPackages.Contains(AssetData.PackageName))
		{
			bRemoveAssetData = true;
		}
		else if (bIsFilteredByRemovedPackages && RemovePackages.Contains(AssetData.PackageName))
		{
			bRemoveAssetData = true;
		}
		else if (Options.bFilterAssetDataWithNoTags && AssetData.TagsAndValues.Num() == 0 &&
			!FPackageName::IsLocalizedPackage(WriteToString<256>(AssetData.PackageName)))
		{
			bRemoveAssetData = true;
			bRemoveDependencyData = Options.bFilterDependenciesWithNoTags;
		}

		if (bRemoveAssetData)
		{
			if (!bRemoveDependencyData)
			{
				RequiredDependNodePackages.Add(AssetData.PackageName);
			}
			return;
		}

		FAssetDataTagMap NewTagsAndValues;
		FAssetRegistryState::FilterTags(AssetData.TagsAndValues, NewTagsAndValues,
			Options.CookFilterlistTagsByClass.Find(AssetData.AssetClassPath), Options);

		FAssetData* NewAssetData = new FAssetData(AssetData.PackageName, AssetData.PackagePath, AssetData.AssetName,
			AssetData.AssetClassPath, NewTagsAndValues, AssetData.GetChunkIDs(), AssetData.PackageFlags);

		NewAssetData->TaggedAssetBundles = AssetData.TaggedAssetBundles;

		// Add asset to new state
		AddAssetData(NewAssetData);
	});

	// Create package data for all script and required packages
	for (const TPair<FName, FAssetPackageData*>& Pair : ExistingState.CachedPackageData)
	{
		if (Pair.Value)
		{
			// Only add if also in asset data map, or script package
			if (CachedAssetsByPackageName.Find(Pair.Key) ||
				FPackageName::IsScriptPackage(WriteToString<256>(Pair.Key)))
			{
				FAssetPackageData* NewData = CreateOrGetAssetPackageData(Pair.Key);
				*NewData = *Pair.Value;
			}
		}
	}

	// Find valid dependency nodes for all script and required packages
	TSet<FDependsNode*> ValidDependsNodes;
	ValidDependsNodes.Reserve(ExistingState.CachedDependsNodes.Num());
	for (const TPair<FAssetIdentifier, FDependsNode*>& Pair : ExistingState.CachedDependsNodes)
	{
		FDependsNode* Node = Pair.Value;
		const FAssetIdentifier& Id = Node->GetIdentifier();
		bool bRemoveDependsNode = false;

		if (Options.bFilterSearchableNames && Id.IsValue())
		{
			bRemoveDependsNode = true;
		}
		else if (Id.IsPackage() &&
			!CachedAssetsByPackageName.Contains(Id.PackageName) &&
			!RequiredDependNodePackages.Contains(Id.PackageName) &&
			!FPackageName::IsScriptPackage(WriteToString<256>(Id.PackageName)))
		{
			bRemoveDependsNode = true;
		}

		if (!bRemoveDependsNode)
		{
			ValidDependsNodes.Add(Node);
		}
	}

	// Duplicate dependency nodes
	for (FDependsNode* OldNode : ValidDependsNodes)
	{
		FDependsNode* NewNode = CreateOrFindDependsNode(OldNode->GetIdentifier());
		NewNode->Reserve(OldNode);
	}
	
	for (FDependsNode* OldNode : ValidDependsNodes)
	{
		FDependsNode* NewNode = CreateOrFindDependsNode(OldNode->GetIdentifier());
		OldNode->IterateOverDependencies([&, OldNode, NewNode](FDependsNode* InDependency,
			UE::AssetRegistry::EDependencyCategory InCategory,
			UE::AssetRegistry::EDependencyProperty InFlags,
			bool bDuplicate)
		{
			if (ValidDependsNodes.Contains(InDependency))
			{
				// Only add link if it's part of the filtered asset set
				FDependsNode* NewDependency = CreateOrFindDependsNode(InDependency->GetIdentifier());
				NewNode->SetIsDependencyListSorted(InCategory, false);
				NewNode->AddDependency(NewDependency, InCategory, InFlags);
				NewDependency->SetIsReferencersSorted(false);
				NewDependency->AddReferencer(NewNode);
			}
		});
		NewNode->SetIsDependenciesInitialized(true);
	}

	// Remove any orphaned depends nodes. This will leave cycles in but those might represent useful data
	TArray<FDependsNode*> AllDependsNodes;
	CachedDependsNodes.GenerateValueArray(AllDependsNodes);
	for (FDependsNode* DependsNode : AllDependsNodes)
	{
		if (DependsNode->GetConnectionCount() == 0)
		{
			RemoveDependsNode(DependsNode->GetIdentifier());
		}
	}

	// Restore the sortedness that we turned off for performance when creating each DependsNode
	SetDependencyNodeSorting(true, true);
}

void FAssetRegistryState::InitializeFromExisting(const FAssetDataMap& AssetDataMap,
	const TMap<FAssetIdentifier, FDependsNode*>& DependsNodeMap, 
	const TMap<FName, FAssetPackageData*>& AssetPackageDataMap,
	const FAssetRegistrySerializationOptions& Options,
	EInitializationMode InInitializationMode,
	FAssetRegistryAppendResult* OutAppendResult)
{
	if (InInitializationMode == EInitializationMode::Rebuild)
	{
		Reset();
	}

	for (const FAssetData* AssetDataPtr : AssetDataMap)
	{
		if (AssetDataPtr == nullptr)
		{
			// don't do anything 
			continue;
		}
		const FAssetData& AssetData = *AssetDataPtr;

		FAssetData* ExistingData = nullptr;
		if (InInitializationMode != EInitializationMode::Rebuild) // minor optimization to avoid lookup in rebuild mode
		{
			if (FAssetData*const* Ptr = CachedAssets.Find(FCachedAssetKey(AssetData)))
			{
				ExistingData = *Ptr;
			}
		}
		if (InInitializationMode == EInitializationMode::OnlyUpdateExisting && ExistingData == nullptr)
		{
			continue;
		}
		if (InInitializationMode == EInitializationMode::OnlyUpdateNew && ExistingData != nullptr)
		{
			continue;
		}

		// Filter asset registry tags now
		FAssetDataTagMap LocalTagsAndValues;
		FAssetRegistryState::FilterTags(AssetData.TagsAndValues, LocalTagsAndValues,
			Options.CookFilterlistTagsByClass.Find(AssetData.AssetClassPath), Options);
		
		if (ExistingData)
		{
			FAssetData NewData(AssetData);
			NewData.TagsAndValues = FAssetDataTagMapSharedView(MoveTemp(LocalTagsAndValues));
			bool bIsModified = false;
			UpdateAssetData(ExistingData, MoveTemp(NewData), &bIsModified);
			if (OutAppendResult && bIsModified)
			{
				OutAppendResult->UpdatedAssets.Add(ExistingData);
			}
		}
		else
		{
			FAssetData* NewData = new FAssetData(AssetData);
			NewData->TagsAndValues = FAssetDataTagMapSharedView(MoveTemp(LocalTagsAndValues));
			AddAssetData(NewData);
			if (OutAppendResult)
			{
				OutAppendResult->AddedAssets.Add(NewData);
			}
		}
	}

	TSet<FAssetIdentifier> ScriptPackages;

	if (InInitializationMode != EInitializationMode::OnlyUpdateExisting)
	{
		for (const TPair<FName, FAssetPackageData*>& Pair : AssetPackageDataMap)
		{
			bool bIsScriptPackage = FPackageName::IsScriptPackage(WriteToString<256>(Pair.Key));
			if (InInitializationMode == EInitializationMode::OnlyUpdateNew && CachedPackageData.Find(Pair.Key))
			{
				continue;
			}
			if (Pair.Value)
			{
				// Only add if also in asset data map, or script package
				if (bIsScriptPackage)
				{
					ScriptPackages.Add(Pair.Key);

					FAssetPackageData* NewData = CreateOrGetAssetPackageData(Pair.Key);
					*NewData = *Pair.Value;
				}
				else if (CachedAssetsByPackageName.Find(Pair.Key))
				{
					FAssetPackageData* NewData = CreateOrGetAssetPackageData(Pair.Key);
					*NewData = *Pair.Value;
				}
			}
		}

		TMap<FAssetIdentifier, FDependsNode*> FilteredDependsNodeMap;
		const TMap<FAssetIdentifier, FDependsNode*>* DependsNodesToAdd = &DependsNodeMap;
		if (InInitializationMode == EInitializationMode::OnlyUpdateNew)
		{
			// Keep the original DependsNodeMap for reference,
			// but remove from NodesToAdd all nodes that already have dependency data
			// Also reserve up-front all (unfiltered) nodes we are adding, to avoid reallocating the Referencers array.
			FilteredDependsNodeMap.Reserve(DependsNodeMap.Num());
			DependsNodesToAdd = &FilteredDependsNodeMap;
			for (const TPair<FAssetIdentifier, FDependsNode*>& Pair : DependsNodeMap)
			{
				FDependsNode* SourceNode = Pair.Value;
				FDependsNode* TargetNode = CreateOrFindDependsNode(Pair.Key);
				if (!TargetNode->IsDependenciesInitialized())
				{
					FilteredDependsNodeMap.Add(Pair.Key, SourceNode);
				}
				TargetNode->Reserve(SourceNode);
			}
		}
		else
		{
			// Reserve up-front all the nodes that we are adding, so we do not reallocate
			// the Referencers array multiple times on a node as we add nodes that refer to it
			for (const TPair<FAssetIdentifier, FDependsNode*>& Pair : DependsNodeMap)
			{
				FDependsNode* SourceNode = Pair.Value;
				FDependsNode* TargetNode = CreateOrFindDependsNode(Pair.Key);
				TargetNode->Reserve(SourceNode);
			}
		}

		for (const TPair<FAssetIdentifier, FDependsNode*>& Pair : *DependsNodesToAdd)
		{
			FDependsNode* SourceNode = Pair.Value;
			FDependsNode* TargetNode = CreateOrFindDependsNode(Pair.Key);
			SourceNode->IterateOverDependencies([this, &DependsNodeMap, &ScriptPackages, TargetNode]
			(FDependsNode* InDependency, UE::AssetRegistry::EDependencyCategory InCategory,
				UE::AssetRegistry::EDependencyProperty InFlags,
				bool bDuplicate)
			{
				const FAssetIdentifier& Identifier = InDependency->GetIdentifier();
				if (DependsNodeMap.Find(Identifier) || ScriptPackages.Contains(Identifier))
				{
					// Only add if this node is in the incoming map
					FDependsNode* TargetDependency = CreateOrFindDependsNode(Identifier);
					TargetNode->SetIsDependencyListSorted(InCategory, false);
					TargetNode->AddDependency(TargetDependency, InCategory, InFlags);
					TargetDependency->SetIsReferencersSorted(false);
					TargetDependency->AddReferencer(TargetNode);
				}
			});
			TargetNode->SetIsDependenciesInitialized(true);
		}

		// Restore the sortedness that we turned off for performance when creating each DependsNode
		SetDependencyNodeSorting(true, true);
	}
}

void FAssetRegistryState::PruneAssetData(const TSet<FName>& RequiredPackages, const TSet<FName>& RemovePackages,
	const FAssetRegistrySerializationOptions& Options)
{
	PruneAssetData(RequiredPackages, RemovePackages, TSet<int32>(), Options);
}

void FAssetRegistryState::PruneAssetData(const TSet<FName>& RequiredPackages, const TSet<FName>& RemovePackages,
	const TSet<int32> ChunksToKeep, const FAssetRegistrySerializationOptions& Options)
{
	FAssetRegistryPruneOptions PruneOptions;
	PruneOptions.RequiredPackages = RequiredPackages;
	PruneOptions.RemovePackages = RemovePackages;
	PruneOptions.ChunksToKeep = ChunksToKeep;
	PruneOptions.Options = Options;
	Prune(PruneOptions);
}

void FAssetRegistryState::Prune(const FAssetRegistryPruneOptions& PruneOptions)
{
	const TSet<FName>& RequiredPackages = PruneOptions.RequiredPackages;
	const TSet<FName>& RemovePackages = PruneOptions.RemovePackages;
	const TSet<int32>& ChunksToKeep = PruneOptions.ChunksToKeep;
	const FAssetRegistrySerializationOptions& Options = PruneOptions.Options;

	const bool bIsFilteredByChunkId = ChunksToKeep.Num() != 0;
	const bool bIsFilteredByRequiredPackages = RequiredPackages.Num() != 0;
	const bool bIsFilteredByRemovedPackages = RemovePackages.Num() != 0;

	TSet<FName> RequiredDependNodePackages;

	// Generate list up front as the maps will get cleaned up
	TArray<FAssetData*> AllAssetData = CachedAssets.Array();
	TSet<FDependsNode*> RemoveDependsNodes;

	TSet<FPrimaryAssetId> KnownPrimaryAssetIds;

	// Remove assets and mark-for-removal any dependencynodes for assets removed due to having no tags
	for (FAssetData* AssetData : AllAssetData)
	{
		bool bRemoveAssetData = false;
		bool bRemoveDependencyData = true;

		if (bIsFilteredByChunkId &&
			!AssetData->GetChunkIDs().ContainsByPredicate([&](int32 ChunkId)
				{
					return ChunksToKeep.Contains(ChunkId);
				}))
		{
			bRemoveAssetData = true;
		}
		else if (bIsFilteredByRequiredPackages && !RequiredPackages.Contains(AssetData->PackageName))
		{
			bRemoveAssetData = true;
		}
		else if (bIsFilteredByRemovedPackages && RemovePackages.Contains(AssetData->PackageName))
		{
			bRemoveAssetData = true;
		}
		else if (Options.bFilterAssetDataWithNoTags && AssetData->TagsAndValues.Num() == 0 &&
			!FPackageName::IsLocalizedPackage(WriteToString<256>(AssetData->PackageName)) &&
			// TODO: Add a package flag for PKG_CookGenerator and check that here as well.
			!(AssetData->PackageFlags & PKG_CookGenerated))
		{
			bRemoveAssetData = true;
			bRemoveDependencyData = Options.bFilterDependenciesWithNoTags;
		}

		if (bRemoveAssetData)
		{
			bool bRemovedAssetData, bRemovedPackageData;
			FName AssetPackageName = AssetData->PackageName;
			// AssetData might be deleted after this call
			RemoveAssetData(AssetData, false /* bRemoveDependencyData */, bRemovedAssetData, bRemovedPackageData);
			if (!bRemoveDependencyData)
			{
				RequiredDependNodePackages.Add(AssetPackageName);
			}
			else if (bRemovedPackageData)
			{
				FDependsNode** RemovedNode = CachedDependsNodes.Find(AssetPackageName);
				if (RemovedNode)
				{
					RemoveDependsNodes.Add(*RemovedNode);
				}
			}
		}
		else if (PruneOptions.bRemoveDependenciesWithoutPackages)
		{
			FPrimaryAssetId PrimaryAssetId = AssetData->GetPrimaryAssetId();
			if (PrimaryAssetId.IsValid())
			{
				KnownPrimaryAssetIds.Add(PrimaryAssetId);
			}
		}
	}

	TArray<FDependsNode*> AllDependsNodes;
	CachedDependsNodes.GenerateValueArray(AllDependsNodes);

	// Mark-for-removal all other dependsnodes that are filtered out by our settings
	for (FDependsNode* DependsNode : AllDependsNodes)
	{
		const FAssetIdentifier& Id = DependsNode->GetIdentifier();
		bool bRemoveDependsNode = false;
		if (RemoveDependsNodes.Contains(DependsNode))
		{
			continue;
		}

		if (Options.bFilterSearchableNames && Id.IsValue())
		{
			bRemoveDependsNode = true;
		}
		else if (Id.IsPackage() &&
			!CachedAssetsByPackageName.Contains(Id.PackageName) &&
			!RequiredDependNodePackages.Contains(Id.PackageName) &&
			!FPackageName::IsScriptPackage(WriteToString<256>(Id.PackageName)))
		{
			bRemoveDependsNode = true;
		}
		else if (PruneOptions.bRemoveDependenciesWithoutPackages)
		{
			const FPrimaryAssetId PrimaryAssetId = Id.GetPrimaryAssetId();
			if (PrimaryAssetId.IsValid() && Id.IsObject())
			{
				if (!KnownPrimaryAssetIds.Contains(PrimaryAssetId))
				{
					if (!PruneOptions.RemoveDependenciesWithoutPackagesKeepPrimaryAssetTypes.Contains(
						PrimaryAssetId.PrimaryAssetType))
					{
						bRemoveDependsNode = true;
					}
				}
			}
		}
		
		if (bRemoveDependsNode)
		{
			RemoveDependsNodes.Add(DependsNode);
		}
	}

	// Batch-remove all of the marked-for-removal dependsnodes
	for (FDependsNode* DependsNode : AllDependsNodes)
	{
		check(DependsNode != nullptr);
		if (RemoveDependsNodes.Contains(DependsNode))
		{
			CachedDependsNodes.Remove(DependsNode->GetIdentifier());
			NumDependsNodes--;
			// if the depends nodes were preallocated in a block, we can't delete them one at a time,
			// only the whole chunk in the destructor
			if (PreallocatedDependsNodeDataBuffers.Num() == 0)
			{
				delete DependsNode;
			}
		}
		else
		{
			DependsNode->RemoveLinks([&RemoveDependsNodes](const FDependsNode* ExistingDependsNode)
				{
					return RemoveDependsNodes.Contains(ExistingDependsNode);
				});
		}
	}

	// Remove any orphaned depends nodes. This will leave cycles in but those might represent useful data
	CachedDependsNodes.GenerateValueArray(AllDependsNodes);
	for (FDependsNode* DependsNode : AllDependsNodes)
	{
		if (DependsNode->GetConnectionCount() == 0)
		{
			RemoveDependsNode(DependsNode->GetIdentifier());
		}
	}
}

bool FAssetRegistryState::HasAssets(const FName PackagePath, bool bSkipARFilteredAssets) const
{
	bool bHasAssets = false;
	EnumerateAssetsByPackagePath(PackagePath, [this, bSkipARFilteredAssets, &bHasAssets](const FAssetData* AssetData)
		{
			if (AssetData && !IsPackageUnmountedAndFiltered(AssetData->PackageName)
				&& (!bSkipARFilteredAssets
					|| !UE::AssetRegistry::FFiltering::ShouldSkipAsset(
						AssetData->AssetClassPath, AssetData->PackageFlags)))
			{
				bHasAssets = true;
				return false; // Stop iterating
			}
			return true; // keep iterating
		});
	return bHasAssets;
}

bool FAssetRegistryState::GetAssets(const FARCompiledFilter& Filter, const TSet<FName>& PackageNamesToSkip,
	TArray<FAssetData>& OutAssetData, bool bSkipARFilteredAssets) const
{
	const UE::AssetRegistry::EEnumerateAssetsFlags Flags = bSkipARFilteredAssets
		? UE::AssetRegistry::EEnumerateAssetsFlags::None
		: UE::AssetRegistry::EEnumerateAssetsFlags::AllowUnfilteredArAssets;
	return EnumerateAssets(Filter, PackageNamesToSkip, [&OutAssetData](const FAssetData& AssetData)
	{
		OutAssetData.Emplace(AssetData);
		return true;
	},
	Flags);
}

namespace UE::AssetRegistry::Private
{

bool DecideIntersectionMethod(int32 PreviousSize, int32 FilterResultsSize, int32 FilterComplexity)
{
	// Cost of intersection of previous results with new results is the cost to construct a TMap of the smaller
	// set plus the cost to query larger set against the TMap. TMap construction is more expensive than TMap query.
	constexpr uint64 TMapConstructionCost = 3;
	uint64 SmallSize;
	uint64 LargeSize;
	if (PreviousSize < FilterResultsSize)
	{
		SmallSize = (uint64)PreviousSize;
		LargeSize = (uint64)FilterResultsSize;
	}
	else
	{
		SmallSize = (uint64)FilterResultsSize;
		LargeSize = (uint64)PreviousSize;
	}
	uint64 ArrayCost = SmallSize * TMapConstructionCost + LargeSize;
	// Cost of filtering previous results by FilterFunction is filtercomplexity times size of the previous results
	uint64 FilterCost = ((uint64)FilterComplexity) * ((uint64)PreviousSize);

	// Our two sets of cost calculation are not on the same scale; they are off by some factor that is dependent 
	// upon ArrayIntersection code, TMap code, and hardware dependent factors. But we assume they are for on the
	// same scale for simplicity. Despite the invalid assumption, our comparison will still work in the important
	// cases: a large FilterComplexity will use ArrayIntersection and a large FilterResultsSize will use filtering.
	return FilterCost < ArrayCost;
}

void ArrayIntersection(TArray<const FAssetData*>& InOutResults,
	TConstArrayView<TConstArrayView<const FAssetData*>> Matches, int32 TotalMatches)
{
	if (InOutResults.Num() < TotalMatches)
	{
		TMap<const FAssetData*, bool> Exists;
		Exists.Reserve(InOutResults.Num());
		for (const FAssetData* Result : InOutResults)
		{
			Exists.Add(Result, false);
		}
		InOutResults.Empty();
		for (TConstArrayView<const FAssetData*> Assets : Matches)
		{
			for (const FAssetData* Asset : Assets)
			{
				bool* Result = Exists.Find(Asset);
				if (Result && 
					!(*Result) // If there are duplicates of an Asset in multiple elements of Matches,
					           // only add the first one
				)
				{
					*Result = true;
					InOutResults.Add(Asset);
				}
 			}
		}
	}
	else
	{
		TSet<const FAssetData*> Exists;
		Exists.Reserve(TotalMatches);
		for (TConstArrayView<const FAssetData*> Assets : Matches)
		{
			for (const FAssetData* Asset : Assets)
			{
				Exists.Add(Asset);
			}
		}

		InOutResults.RemoveAllSwap([&Exists](const FAssetData* Asset)
			{
				return !Exists.Contains(Asset);
			});
	}
}

#if UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
void ArrayIntersection(TArray<const FAssetData*>& InOutResults,
	TConstArrayView<TConstArrayView<FAssetDataPtrIndex>> Matches, int32 TotalMatches,
	const FAssetDataMap& CachedAssets)
{
	if (InOutResults.Num() < TotalMatches)
	{
		TMap<const FAssetData*, bool> Exists;
		Exists.Reserve(InOutResults.Num());
		for (const FAssetData* Result : InOutResults)
		{
			Exists.Add(Result, false);
		}
		InOutResults.Empty();
		for (TConstArrayView<FAssetDataPtrIndex> Assets : Matches)
		{
			for (FAssetDataPtrIndex AssetIndex: Assets)
			{
				const FAssetData* Asset = CachedAssets[AssetIndex];
				bool* Result = Exists.Find(Asset);
				if (Result && 
					!(*Result) // If there are duplicates of an Asset in multiple elements of Matches,
					           // only add the first one
				)
				{
					*Result = true;
					InOutResults.Add(Asset);
				}
 			}
		}
	}
	else
	{
		TSet<const FAssetData*> Exists;
		Exists.Reserve(TotalMatches);
		for (TConstArrayView<FAssetDataPtrIndex> Assets : Matches)
		{
			for (FAssetDataPtrIndex AssetIndex : Assets)
			{
				Exists.Add(CachedAssets[AssetIndex]);
			}
		}

		InOutResults.RemoveAllSwap([&Exists](const FAssetData* Asset)
			{
				return !Exists.Contains(Asset);
			});
	}
}
#endif

template<class ArrayType, typename KeyType, typename CallbackType>
void FilterAssets(TArray<const FAssetData*>& InOutResults, const TMap<KeyType, ArrayType>& AccelerationMap,
	const TSet<KeyType>& Keys, CallbackType&& FunctionToKeepAsset, int32 FilterComplexity, const FAssetDataMap& CachedAssets)
{
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	TArray<TConstArrayView<const FAssetData*>, TInlineAllocator<10>> Matches;
#else
	TArray<TConstArrayView<FAssetDataPtrIndex>, TInlineAllocator<10>> Matches;
#endif
	Matches.Reserve(Keys.Num());
	uint32 TotalMatches = 0;

	for (const KeyType& Key : Keys)
	{
		if (const ArrayType* Assets = AccelerationMap.Find(Key))
		{
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
			const FAssetData** AssetPtr = const_cast<const FAssetData**>(Assets->GetData());
			Matches.Add(TConstArrayView<const FAssetData*>(AssetPtr, Assets->Num()));
#else
			FAssetDataPtrIndex* AssetPtr = const_cast<FAssetDataPtrIndex*>(Assets->GetData());
			Matches.Add(TConstArrayView<FAssetDataPtrIndex>(AssetPtr, Assets->Num()));
#endif
			TotalMatches += Assets->Num();
		}
	}

	// Keys is a TSet and entries in the AccelerationMap do not overlap,
	// so there should be no duplicates to remove in Matches
	if (InOutResults.IsEmpty())
	{
		// No previous Results; set Results equal to the values found in AccelerationMap
		InOutResults.Reserve(TotalMatches);
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
		for (TConstArrayView<const FAssetData*> Assets : Matches)
		{
			InOutResults.Append(Assets);
		}
#else
		for (TConstArrayView<FAssetDataPtrIndex> Assets : Matches)
		{
			for (FAssetDataPtrIndex AssetIndex : Assets)
			{
				InOutResults.Add(CachedAssets[AssetIndex]);
			}
		}
#endif
	}
	else
	{
		bool bUseFiltering = DecideIntersectionMethod(InOutResults.Num(), TotalMatches, FilterComplexity);
		if (bUseFiltering)
		{
			InOutResults.RemoveAllSwap([&FunctionToKeepAsset](const FAssetData* AssetData)
				{
					return !FunctionToKeepAsset(AssetData);
				});
		}
		else
		{
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
			ArrayIntersection(InOutResults, TConstArrayView<TConstArrayView<const FAssetData*>>(Matches), TotalMatches);
#else
			ArrayIntersection(InOutResults, TConstArrayView<TConstArrayView<FAssetDataPtrIndex>>(Matches), TotalMatches, CachedAssets);
#endif
		}
	}
}

#if UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
template<typename CallbackType>
void FilterAssetsByPackageName(
	TArray<const FAssetData*>& InOutResults,
	const FAssetPackageNameMap& AccelerationMap,
	const TSet<FName>& Keys, CallbackType&& FunctionToKeepAsset, int32 FilterComplexity,
	const FAssetDataMap& CachedAssets)
{
	TArray<TConstArrayView<FAssetDataPtrIndex>, TInlineAllocator<10>> Matches;
	Matches.Reserve(Keys.Num());
	uint32 TotalMatches = 0;

	for (const FName& Key : Keys)
	{
		if (TOptional<TConstArrayView<FAssetDataPtrIndex>> Array = AccelerationMap.Find(Key))
		{
			Matches.Add(*Array);
			TotalMatches += Matches.Num();
		}
	}

	// Keys is a TSet and entries in the AccelerationMap do not overlap,
	// so there should be no duplicates to remove in Matches
	if (InOutResults.IsEmpty())
	{
		// No previous Results; set Results equal to the values found in AccelerationMap
		InOutResults.Reserve(TotalMatches);
		for (TConstArrayView<FAssetDataPtrIndex> Assets : Matches)
		{
			for (FAssetDataPtrIndex AssetIndex : Assets)
			{
				InOutResults.Add(CachedAssets[AssetIndex]);
			}
		}
	}
	else
	{
		bool bUseFiltering = DecideIntersectionMethod(InOutResults.Num(), TotalMatches, FilterComplexity);
		if (bUseFiltering)
		{
			InOutResults.RemoveAllSwap([&FunctionToKeepAsset](const FAssetData* AssetData)
				{
					return !FunctionToKeepAsset(AssetData);
				});
		}
		else
		{
			ArrayIntersection(InOutResults, TConstArrayView<TConstArrayView<FAssetDataPtrIndex>>(Matches), TotalMatches, CachedAssets);
		}
	}
}
#endif

template<typename CallbackType>
void FilterAssets(TArray<const FAssetData*>&InOutResults,
	const UE::AssetRegistry::Private::FAssetDataMap &AccelerationMap, const TSet<FSoftObjectPath>& Keys,
	CallbackType&& FunctionToKeepAsset, int32 FilterComplexity)
{
	TArray<const FAssetData*, TInlineAllocator<10>> Matches;
	Matches.Reserve(Keys.Num());

	for (const FSoftObjectPath& Key : Keys)
	{
		if (FAssetData* const* AssetDataPtr = AccelerationMap.Find(UE::AssetRegistry::Private::FCachedAssetKey(Key)))
		{
			Matches.Add(*AssetDataPtr);
		}
	}

	// Keys is a TSet, so there should be no duplicates to remove in Matches
	if (InOutResults.IsEmpty())
	{
		// No previous Results; set Results equal to the values found in AccelerationMap
		InOutResults = MoveTemp(Matches);
	}
	else
	{
		bool bUseFiltering = DecideIntersectionMethod(InOutResults.Num(), Matches.Num(), FilterComplexity);
		if (bUseFiltering)
		{
			InOutResults.RemoveAllSwap([&FunctionToKeepAsset](const FAssetData* AssetData)
				{
					return !FunctionToKeepAsset(AssetData);
				});
		}
		else
		{
			TConstArrayView<const FAssetData*> ArrayView(Matches);
			TConstArrayView<TConstArrayView<const FAssetData*>> ArrayViewOfArrayViews(&ArrayView, 1);
			ArrayIntersection(InOutResults, ArrayViewOfArrayViews, Matches.Num());
		}
	}
}

bool AssetDataMatchesTag(const FAssetData* AssetData, const TPair<FName, TOptional<FString>>& TagPair)
{
	if (!AssetData)
	{
		return false;
	}
	if (!TagPair.Value.IsSet())
	{
		return AssetData->TagsAndValues.Contains(TagPair.Key);
	}
	else
	{
		return AssetData->TagsAndValues.ContainsKeyValue(TagPair.Key, TagPair.Value.GetValue());
	}
}

template<typename CallbackType, typename AccelerationMapType>
void FilterAssets(TArray<const FAssetData*>& InOutResults, const AccelerationMapType& AccelerationMap,
	const TMultiMap<FName, TOptional<FString>>& TagsAndValues,
	CallbackType&& FunctionToKeepAsset, int32 FilterComplexity, const FAssetDataMap& CachedAssets)
{
	TArray<TArray<const FAssetData*>, TInlineAllocator<10>> Matches;
	Matches.Reserve(TagsAndValues.Num());
	uint32 TotalMatches = 0;

	for (const TPair<FName, TOptional<FString>>& TagPair : TagsAndValues)
	{
		TArray<const FAssetData*>& Results = Matches.Emplace_GetRef();
		if (const auto* TagAssets = AccelerationMap.Find(TagPair.Key))
		{
			Results.Reserve(TagAssets->Num());
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
			for (FAssetData* AssetData : *TagAssets)
			{
#else
			for (FAssetDataPtrIndex AssetIndex : *TagAssets)
			{
				FAssetData* AssetData = CachedAssets[AssetIndex];
#endif
				if (AssetDataMatchesTag(AssetData, TagPair))
				{
					Results.Add(AssetData);
				}
			}
		}
		TotalMatches += Results.Num();
	}

	if (InOutResults.IsEmpty())
	{
		// No previous Results; set Results equal to the values found in AccelerationMap
		InOutResults.Reserve(TotalMatches);
		for (TArray<const FAssetData*>& Assets : Matches)
		{
			InOutResults.Append(Assets);
		}
		// Remove duplicates
		Algo::Sort(InOutResults);
		InOutResults.SetNum(Algo::Unique(InOutResults));
	}
	else
	{
		bool bUseFiltering = DecideIntersectionMethod(InOutResults.Num(), TotalMatches, FilterComplexity);
		if (bUseFiltering)
		{
			InOutResults.RemoveAllSwap([&FunctionToKeepAsset](const FAssetData* AssetData)
				{
					return !FunctionToKeepAsset(AssetData);
				});
		}
		else
		{
			// Convert Array of Arrays into format required by ArrayIntersection: Array of ArrayViews
			TArray<TConstArrayView<const FAssetData*>, TInlineAllocator<10>> ArrayViewMatches;
			ArrayViewMatches.Reserve(Matches.Num());
			for (TArray<const FAssetData*>& MatchesElement : Matches)
			{
				ArrayViewMatches.Emplace(MatchesElement);
			}

			// ArrayIntersection handles removing any duplicates from Matches
			ArrayIntersection(InOutResults, ArrayViewMatches, TotalMatches);
		}
	}
}

#if !UE_ASSETREGISTRY_CACHEDASSETSBYTAG
template<typename CallbackType>
void FilterAssetsByCachedClassesByTag(TArray<const FAssetData*>& InOutResults,
	const TMap<FName, TSet<FTopLevelAssetPath>>& CachedClassesByTag,
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	const TMap<FTopLevelAssetPath, TArray<FAssetData*>>& CachedAssetsByClass,
#else
	const TMap<FTopLevelAssetPath, TArray<FAssetDataPtrIndex>>& CachedAssetsByClass,
#endif
	const TMultiMap<FName, TOptional<FString>>& TagsAndValues,
	CallbackType&& FunctionToKeepAsset, int32 FilterComplexity,
	const FAssetDataMap& CachedAssets)
{
	TArray<TArray<const FAssetData*>, TInlineAllocator<10>> Matches;
	Matches.Reserve(TagsAndValues.Num());
	uint32 TotalMatches = 0;

	for (const TPair<FName, TOptional<FString>>& TagPair : TagsAndValues)
	{
		TArray<const FAssetData*>& Results = Matches.Emplace_GetRef();

		// The lists of assets in CachedAssetsByClass are non-intersecting (each list is only the exact instances of that
		// class and does not include subclasses), so we do not need to handle removing duplicates when merging lists from
		// multiple classes.
		if (const TSet<FTopLevelAssetPath>* TagClasses = CachedClassesByTag.Find(TagPair.Key))
		{
			for (const FTopLevelAssetPath& ClassPath : *TagClasses)
			{
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
				if (const TArray<FAssetData*>* ClassAssets = CachedAssetsByClass.Find(ClassPath))
				{
					Results.Append(*ClassAssets);
				}
#else
				if (const TArray<FAssetDataPtrIndex>* ClassAssets = CachedAssetsByClass.Find(ClassPath))
				{
					Results.Reserve(Results.Num() + ClassAssets->Num());
					for (FAssetDataPtrIndex Index : *ClassAssets)
					{
						Results.Add(CachedAssets[Index]);
					}
				}
#endif
			}
		}
		// Some assets are in a class that could have the tag, but the specific asset actually does not have the tag.
		for (TArray<const FAssetData*>::TIterator Iter(Results); Iter; ++Iter)
		{
			const FAssetData* AssetData = *Iter;
			if (!AssetDataMatchesTag(AssetData, TagPair))
			{
				Iter.RemoveCurrentSwap();
			}
		}
		TotalMatches += Results.Num();
	}

	if (InOutResults.IsEmpty())
	{
		// No previous Results; set Results equal to the values found
		InOutResults.Reserve(TotalMatches);
		for (TArray<const FAssetData*>& Assets : Matches)
		{
			InOutResults.Append(Assets);
		}
		// Remove duplicates
		Algo::Sort(InOutResults);
		InOutResults.SetNum(Algo::Unique(InOutResults));
	}
	else
	{
		bool bUseFiltering = DecideIntersectionMethod(InOutResults.Num(), TotalMatches, FilterComplexity);
		if (bUseFiltering)
		{
			InOutResults.RemoveAllSwap([&FunctionToKeepAsset](const FAssetData* AssetData)
				{
					return !FunctionToKeepAsset(AssetData);
				});
		}
		else
		{
			// Convert Array of Arrays into format required by ArrayIntersection: Array of ArrayViews
			TArray<TConstArrayView<const FAssetData*>, TInlineAllocator<10>> ArrayViewMatches;
			ArrayViewMatches.Reserve(Matches.Num());
			for (TArray<const FAssetData*>& MatchesElement : Matches)
			{
				ArrayViewMatches.Emplace(MatchesElement);
			}

			// ArrayIntersection handles removing any duplicates from Matches
			ArrayIntersection(InOutResults, ArrayViewMatches, TotalMatches);
		}
	}
}
#endif

} // namespace UE::AssetRegistry::Private

bool FAssetRegistryState::EnumerateAssets(const FARCompiledFilter& Filter, const TSet<FName>& PackageNamesToSkip,
	TFunctionRef<bool(const FAssetData&)> Callback, bool bSkipARFilteredAssets) const
{
	const UE::AssetRegistry::EEnumerateAssetsFlags Flags = bSkipARFilteredAssets
		? UE::AssetRegistry::EEnumerateAssetsFlags::None
		: UE::AssetRegistry::EEnumerateAssetsFlags::AllowUnfilteredArAssets;
	return EnumerateAssets(Filter, PackageNamesToSkip, Callback, Flags);
}

bool FAssetRegistryState::EnumerateAssets(const FARCompiledFilter& Filter, const TSet<FName>& PackageNamesToSkip,
	TFunctionRef<bool(const FAssetData&)> Callback) const
{
	return EnumerateAssets(Filter, PackageNamesToSkip, Callback,
		UE::AssetRegistry::EEnumerateAssetsFlags::AllowUnfilteredArAssets);
}

bool FAssetRegistryState::EnumerateAssets(const FARCompiledFilter& Filter, const TSet<FName>& PackageNamesToSkip,
	TFunctionRef<bool(const FAssetData&)> Callback, UE::AssetRegistry::EEnumerateAssetsFlags InEnumerateFlags) const
{
	using namespace UE::AssetRegistry::Private;

	// Verify filter input. If all assets are needed, use EnumerateAllAssets() instead.
	if (Filter.IsEmpty() || !IsFilterValid(Filter))
	{
		return false;
	}

	const uint32 FilterWithoutPackageFlags = Filter.WithoutPackageFlags;
	const uint32 FilterWithPackageFlags = Filter.WithPackageFlags;
	auto ShouldSkipAssetData =
		[this, &PackageNamesToSkip, InEnumerateFlags, FilterWithoutPackageFlags, FilterWithPackageFlags]
		(const FAssetData* AssetData)
		{
			if (PackageNamesToSkip.Contains(AssetData->PackageName) |			//-V792
				AssetData->HasAnyPackageFlags(FilterWithoutPackageFlags) |		//-V792
				!AssetData->HasAllPackageFlags(FilterWithPackageFlags))			//-V792
			{
				return true;
			}

			if (!EnumHasAnyFlags(InEnumerateFlags, UE::AssetRegistry::EEnumerateAssetsFlags::AllowUnmountedPaths)
				&& IsPackageUnmountedAndFiltered(AssetData->PackageName))
			{
				return true;
			}

			return (!EnumHasAnyFlags(InEnumerateFlags,
				UE::AssetRegistry::EEnumerateAssetsFlags::AllowUnfilteredArAssets)
				&& UE::AssetRegistry::FFiltering::ShouldSkipAsset(AssetData->AssetClassPath, AssetData->PackageFlags));
		};


	// Some of our filters are accelerated: we have TMaps that list for each value of the filter all of the assets
	// that pass that filter. But some of those assets-passing-FilterN-ValueV are very large, and just merging the
	// lists of FAssetData* can be expensive. So for each new filter we need to decide whether it is more expensive to
	// merge previous results with the acceleration list or to apply the filter to every element in previous results.
	// This decision is handled by FilterAssets.
	// To benefit from the filter method we want to have as small a list of results as possible at each step, so
	// order the filters from most-likely to have few results to least-likely to have few results.
	TArray<const FAssetData*> AccumulatedResults;

	if (Filter.SoftObjectPaths.Num() > 0)
	{
		FilterAssets(AccumulatedResults, CachedAssets, Filter.SoftObjectPaths,
			[&Filter](const FAssetData* AssetData)
			{
				return Filter.SoftObjectPaths.Contains(AssetData->GetSoftObjectPath());
			},
			Filter.SoftObjectPaths.Num());
		if (AccumulatedResults.IsEmpty())
		{
			return true;
		}
	}

	if (Filter.PackageNames.Num() > 0)
	{
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
		FilterAssets(
#else
		FilterAssetsByPackageName(
#endif
			AccumulatedResults, CachedAssetsByPackageName, Filter.PackageNames,
			[&Filter](const FAssetData* AssetData)
			{
				return Filter.PackageNames.Contains(AssetData->PackageName);
			},
			Filter.PackageNames.Num(), CachedAssets);
		if (AccumulatedResults.IsEmpty())
		{
			return true;
		}
	}

	if (Filter.PackagePaths.Num() > 0)
	{
		FilterAssets(AccumulatedResults, CachedAssetsByPath, Filter.PackagePaths,
			[&Filter](const FAssetData* AssetData)
			{
				return Filter.PackagePaths.Contains(AssetData->PackagePath);
			},
			Filter.PackagePaths.Num(), CachedAssets);
		if (AccumulatedResults.IsEmpty())
		{
			return true;
		}
	}

	if (Filter.TagsAndValues.Num() > 0)
	{
#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
		FilterAssets(AccumulatedResults, CachedAssetsByTag,
#else
		FilterAssetsByCachedClassesByTag(AccumulatedResults, CachedClassesByTag, CachedAssetsByClass,
#endif
			Filter.TagsAndValues,
			[&Filter](const FAssetData* AssetData)
			{
				for (const TPair<FName, TOptional<FString>>& TagPair : Filter.TagsAndValues)
				{
					if (AssetDataMatchesTag(AssetData, TagPair))
					{
						return true; // keep
					}
				}
				return false; // remove
			},
			Filter.TagsAndValues.Num(), CachedAssets);
		if (AccumulatedResults.IsEmpty())
		{
			return true;
		}
	}

	if (Filter.ClassPaths.Num() > 0)
	{
		FilterAssets(AccumulatedResults, CachedAssetsByClass, Filter.ClassPaths,
			[&Filter](const FAssetData* AssetData)
			{
				return Filter.ClassPaths.Contains(AssetData->AssetClassPath);
			},
			Filter.ClassPaths.Num(), CachedAssets);
		if (AccumulatedResults.IsEmpty())
		{
			return true;
		}
	}

	// Run the remaining non-accelerated filters on every element of AccumulatedResults
	for (const FAssetData* AssetData : AccumulatedResults)
	{
		if (ShouldSkipAssetData(AssetData))
		{
			continue;
		}
		bool bContinueFiltering = Callback(*AssetData);
		if (!bContinueFiltering)
		{
			return true;
		}
	}

	return true;
}

bool FAssetRegistryState::GetAllAssets(const TSet<FName>& PackageNamesToSkip, TArray<FAssetData>& OutAssetData,
	bool bSkipARFilteredAssets) const
{
	const UE::AssetRegistry::EEnumerateAssetsFlags EnumerateFlags = bSkipARFilteredAssets
		? UE::AssetRegistry::EEnumerateAssetsFlags::None
		: UE::AssetRegistry::EEnumerateAssetsFlags::AllowUnfilteredArAssets;
	OutAssetData.Reserve(OutAssetData.Num() + CachedAssets.Num() - PackageNamesToSkip.Num());
	return EnumerateAllAssets(PackageNamesToSkip, [&OutAssetData](const FAssetData& AssetData)
	{
		OutAssetData.Emplace(AssetData);
		return true;
	},
	EnumerateFlags);
}

bool FAssetRegistryState::EnumerateAllAssets(const TSet<FName>& PackageNamesToSkip,
	TFunctionRef<bool(const FAssetData&)> Callback, bool bSkipARFilteredAssets) const
{
	const UE::AssetRegistry::EEnumerateAssetsFlags EnumerateFlags = bSkipARFilteredAssets
		? UE::AssetRegistry::EEnumerateAssetsFlags::None
		: UE::AssetRegistry::EEnumerateAssetsFlags::AllowUnfilteredArAssets;
	return EnumerateAllAssets(PackageNamesToSkip, Callback, EnumerateFlags);
}

bool FAssetRegistryState::EnumerateAllAssets(const TSet<FName>& PackageNamesToSkip,
	TFunctionRef<bool(const FAssetData&)> Callback) const
{
	return EnumerateAllAssets(PackageNamesToSkip, Callback,
		UE::AssetRegistry::EEnumerateAssetsFlags::AllowUnfilteredArAssets);
}

void FAssetRegistryState::EnumerateAllAssets(TFunctionRef<void(const FAssetData&)> Callback) const
{
	EnumerateAllMutableAssets([&Callback](FAssetData& AssetData)
		{
			Callback(AssetData);
		});
}

void FAssetRegistryState::EnumerateAllMutableAssets(TFunctionRef<void(FAssetData&)> Callback) const
{
	for (FAssetData* AssetData : CachedAssets)
	{
		check(AssetData);
		Callback(*AssetData);
	}
}

bool FAssetRegistryState::EnumerateAllAssets(const TSet<FName>& PackageNamesToSkip,
	TFunctionRef<bool(const FAssetData&)> Callback,
	UE::AssetRegistry::EEnumerateAssetsFlags InEnumerateFlags) const
{
	using namespace UE::AssetRegistry;

	EnumerateAllMutableAssets([&PackageNamesToSkip, &Callback, InEnumerateFlags, this](const FAssetData& AssetData)
		{
			if (!PackageNamesToSkip.Contains(AssetData.PackageName)
				&& (EnumHasAnyFlags(InEnumerateFlags, EEnumerateAssetsFlags::AllowUnmountedPaths)
					|| !IsPackageUnmountedAndFiltered(AssetData.PackageName))
				&& (EnumHasAnyFlags(InEnumerateFlags, EEnumerateAssetsFlags::AllowUnfilteredArAssets)
					|| !FFiltering::ShouldSkipAsset(AssetData.AssetClassPath,
						AssetData.PackageFlags)))
			{
				if (!Callback(AssetData))
				{
					return false; // Stop iterating
				}
			}
			return true; // keep iterating
		});
	return true;
}

void FAssetRegistryState::EnumerateAllPaths(TFunctionRef<void(FName PathName)> Callback) const
{
	for (const auto& Pair : CachedAssetsByPath)
	{
		Callback(Pair.Key);
	}
}

void FAssetRegistryState::GetPackagesByName(FStringView PackageName, TArray<FName>& OutPackageNames) const
{
	// Note that we use CachedAssetsByPackageName rather than CachedPackageData because CachedPackageData
	// is often stripped out of the runtime AssetRegistry
	if (!FPackageName::IsShortPackageName(PackageName))
	{
		FName PackageFName(PackageName);
		if (CachedAssetsByPackageName.Contains(PackageFName))
		{
			OutPackageNames.Add(PackageFName);
		}
	}
	else
	{
		TStringBuilder<128> PackageNameStr;
		for (const auto& It : CachedAssetsByPackageName)
		{
			It.Key.ToString(PackageNameStr);
			FStringView ExistingBaseName = FPathViews::GetBaseFilename(PackageNameStr);
			if (ExistingBaseName.Equals(PackageName, ESearchCase::IgnoreCase))
			{
				OutPackageNames.Add(It.Key);
			}
		}
	}
}

FName FAssetRegistryState::GetFirstPackageByName(FStringView PackageName) const
{
	TArray<FName> LongPackageNames;
	GetPackagesByName(PackageName, LongPackageNames);
	if (LongPackageNames.Num() == 0)
	{
		return NAME_None;
	}
	if (LongPackageNames.Num() > 1)
	{
		LongPackageNames.Sort(FNameLexicalLess());
		UE_LOG(LogAssetRegistry, Warning,
			TEXT("GetFirstPackageByName('%.*s') is returning '%s', but it also found '%s'%s."),
			PackageName.Len(), PackageName.GetData(), *LongPackageNames[0].ToString(), *LongPackageNames[1].ToString(),
			(LongPackageNames.Num() > 2 ? *FString::Printf(TEXT(" and %d others"), LongPackageNames.Num() - 2) : TEXT("")));
	}
	return LongPackageNames[0];
}

bool FAssetRegistryState::GetDependencies(const FAssetIdentifier& AssetIdentifier,
	TArray<FAssetIdentifier>& OutDependencies,
	UE::AssetRegistry::EDependencyCategory Category, const UE::AssetRegistry::FDependencyQuery& Flags) const
{
	const FDependsNode* const* NodePtr = CachedDependsNodes.Find(AssetIdentifier);
	const FDependsNode* Node = nullptr;
	if (NodePtr != nullptr)
	{
		Node = *NodePtr;
	}

	if (Node != nullptr)
	{
		Node->GetDependencies(OutDependencies, Category, Flags);
		return true;
	}
	else
	{
		return false;
	}
}

bool FAssetRegistryState::GetDependencies(const FAssetIdentifier& AssetIdentifier,
	TArray<FAssetDependency>& OutDependencies,
	UE::AssetRegistry::EDependencyCategory Category, const UE::AssetRegistry::FDependencyQuery& Flags) const
{
	const FDependsNode* const* NodePtr = CachedDependsNodes.Find(AssetIdentifier);
	const FDependsNode* Node = nullptr;
	if (NodePtr != nullptr)
	{
		Node = *NodePtr;
	}

	if (Node != nullptr)
	{
		Node->GetDependencies(OutDependencies, Category, Flags);
		return true;
	}
	else
	{
		return false;
	}
}

bool FAssetRegistryState::GetReferencers(const FAssetIdentifier& AssetIdentifier,
	TArray<FAssetIdentifier>& OutReferencers,
	UE::AssetRegistry::EDependencyCategory Category, const UE::AssetRegistry::FDependencyQuery& Flags) const
{
	const FDependsNode* const* NodePtr = CachedDependsNodes.Find(AssetIdentifier);
	const FDependsNode* Node = nullptr;

	if (NodePtr != nullptr)
	{
		Node = *NodePtr;
	}

	if (Node != nullptr)
	{
		TArray<FDependsNode*> DependencyNodes;
		Node->GetReferencers(DependencyNodes, Category, Flags);

		OutReferencers.Reserve(OutReferencers.Num() + DependencyNodes.Num());
		for (FDependsNode* DependencyNode : DependencyNodes)
		{
			OutReferencers.Add(DependencyNode->GetIdentifier());
		}

		return true;
	}
	else
	{
		return false;
	}
}

bool FAssetRegistryState::GetReferencers(const FAssetIdentifier& AssetIdentifier,
	TArray<FAssetDependency>& OutReferencers,
	UE::AssetRegistry::EDependencyCategory Category, const UE::AssetRegistry::FDependencyQuery& Flags) const
{
	const FDependsNode* const* NodePtr = CachedDependsNodes.Find(AssetIdentifier);
	const FDependsNode* Node = nullptr;

	if (NodePtr != nullptr)
	{
		Node = *NodePtr;
	}

	if (Node != nullptr)
	{
		Node->GetReferencers(OutReferencers, Category, Flags);
		return true;
	}
	else
	{
		return false;
	}
}

void FAssetRegistryState::ClearDependencies(const FAssetIdentifier& AssetIdentifier,
	UE::AssetRegistry::EDependencyCategory Category)
{
	FDependsNode* ReferencerNode = FindDependsNode(AssetIdentifier);
	if (!ReferencerNode)
	{
		return;
	}

	TArray<FDependsNode*> OldDependencies;
	ReferencerNode->GetDependencies(OldDependencies);
	ReferencerNode->ClearDependencies(Category);

	for (FDependsNode* DependencyNode : OldDependencies)
	{
		if (!ReferencerNode->ContainsDependency(DependencyNode))
		{
			DependencyNode->RemoveReferencer(ReferencerNode);
		}
	}
}

void FAssetRegistryState::AddDependencies(const FAssetIdentifier& AssetIdentifier,
	TConstArrayView<FAssetDependency> Dependencies)
{
	if (Dependencies.IsEmpty())
	{
		return;
	}
	FDependsNode* ReferencerNode = CreateOrFindDependsNode(AssetIdentifier);
	for (const FAssetDependency& Dependency : Dependencies)
	{
		FDependsNode* DependencyNode = CreateOrFindDependsNode(Dependency.AssetId);
		ReferencerNode->AddDependency(DependencyNode, Dependency.Category, Dependency.Properties);
		DependencyNode->AddReferencer(ReferencerNode);
	}
}

void FAssetRegistryState::SetDependencies(const FAssetIdentifier& AssetIdentifier,
	TConstArrayView<FAssetDependency> Dependencies, UE::AssetRegistry::EDependencyCategory Category)
{
	for (const FAssetDependency& Dependency : Dependencies)
	{
		checkf(!(Dependency.Category & ~Category),
			TEXT("Input dependency has category %d which is outside of the requested categories %d."),
			(int32)Dependency.Category, (int32)Category);
	}

	ClearDependencies(AssetIdentifier, Category);
	AddDependencies(AssetIdentifier, Dependencies);
}

void FAssetRegistryState::ClearReferencers(const FAssetIdentifier& AssetIdentifier,
	UE::AssetRegistry::EDependencyCategory Category)
{
	FDependsNode* DependencyNode = FindDependsNode(AssetIdentifier);
	if (!DependencyNode)
	{
		return;
	}

	TArray<FDependsNode*> OldExisting;
	DependencyNode->GetReferencers(OldExisting, Category);
	for (FDependsNode* ReferencerNode : OldExisting)
	{
		ReferencerNode->RemoveDependency(DependencyNode, Category);
		if (!ReferencerNode->ContainsDependency(DependencyNode))
		{
			DependencyNode->RemoveReferencer(ReferencerNode);
		}
	}
}

void FAssetRegistryState::AddReferencers(const FAssetIdentifier& AssetIdentifier,
	TConstArrayView<FAssetDependency> Referencers)
{
	if (Referencers.IsEmpty())
	{
		return;
	}
	FDependsNode* DependencyNode = CreateOrFindDependsNode(AssetIdentifier);
	for (const FAssetDependency& Referencer : Referencers)
	{
		FDependsNode* ReferencerNode = CreateOrFindDependsNode(Referencer.AssetId);
		ReferencerNode->AddDependency(DependencyNode, Referencer.Category, Referencer.Properties);
		DependencyNode->AddReferencer(ReferencerNode);
	}
}

void FAssetRegistryState::SetReferencers(const FAssetIdentifier& AssetIdentifier,
	TConstArrayView<FAssetDependency> Referencers, UE::AssetRegistry::EDependencyCategory Category)
{
	for (const FAssetDependency& Referencer : Referencers)
	{
		checkf(!(Referencer.Category & ~Category),
			TEXT("Input referencer has category %d which is outside of the requested categories %d."),
			(int32)Referencer.Category, (int32)Category);
	}

	ClearReferencers(AssetIdentifier, Category);
	AddReferencers(AssetIdentifier, Referencers);
}

bool FAssetRegistryState::Serialize(FArchive& Ar, const FAssetRegistrySerializationOptions& Options)
{
	return Ar.IsSaving() ? Save(Ar, Options) : Load(Ar, FAssetRegistryLoadOptions(Options));
}

bool FAssetRegistryState::Save(FArchive& OriginalAr, const FAssetRegistrySerializationOptions& Options)
{
	SCOPED_BOOT_TIMING("FAssetRegistryState::Save");

	check(!OriginalAr.IsLoading());

#if !ALLOW_NAME_BATCH_SAVING
	checkf(false, TEXT("Cannot save cooked AssetRegistryState in this configuration"));
#else
	check(CachedAssets.Num() == NumAssets);

	FAssetRegistryHeader Header;
	Header.Version = FAssetRegistryVersion::LatestVersion;
	Header.bFilterEditorOnlyData = OriginalAr.IsFilterEditorOnly();
	Header.SerializeHeader(OriginalAr);

	// Set up fixed asset registry writer
	FAssetRegistryWriter Ar(FAssetRegistryWriterOptions(Options), OriginalAr);

	// serialize number of objects
	int32 AssetCount = CachedAssets.Num();
	Ar << AssetCount;

	// Write asset data first
	{
		TArray<TPair<FAssetData*, FSoftObjectPath>> SortedAssetsByObjectPath;
		SortedAssetsByObjectPath.Reserve(AssetCount);
		EnumerateAllMutableAssets([&SortedAssetsByObjectPath](FAssetData& AssetData)
			{
				SortedAssetsByObjectPath.Add({ &AssetData, AssetData.GetSoftObjectPath() });
			});
		Algo::Sort(SortedAssetsByObjectPath, [](const TPair<FAssetData*, FSoftObjectPath>& A,
			const TPair<FAssetData*, FSoftObjectPath>& B)
		{
			return A.Value.LexicalLess(B.Value);
		});

		for (TPair<FAssetData*, FSoftObjectPath>& Asset : SortedAssetsByObjectPath)
		{
			// Hardcoding FAssetRegistryVersion::LatestVersion here so that branches can get optimized out in
			// the forceinlined SerializeForCache
			Asset.Key->SerializeForCache(Ar);
		}
	}

	// Serialize Dependencies
	// Write placeholder data for the size
	int64 OffsetToDependencySectionSize = Ar.Tell();
	int64 DependencySectionSize = 0;
	Ar << DependencySectionSize;
	int64 DependencySectionStart = Ar.Tell();
	if (!Options.bSerializeDependencies)
	{
		int32 NumDependencies = 0;
		Ar << NumDependencies;
	}
	else
	{
		TMap<FDependsNode*, FDependsNode*> RedirectCache;
		TArray<FDependsNode*> Dependencies;

		// Scan dependency nodes, we won't save all of them if we filter out certain types
		for (TPair<FAssetIdentifier, FDependsNode*>& Pair : CachedDependsNodes)
		{
			FDependsNode* Node = Pair.Value;

			if (Node->GetIdentifier().IsPackage() 
				|| (Options.bSerializeSearchableNameDependencies && Node->GetIdentifier().IsValue())
				|| (Options.bSerializeManageDependencies && Node->GetIdentifier().GetPrimaryAssetId().IsValid()))
			{
				Dependencies.Add(Node);
			}
		}
		Algo::Sort(Dependencies, [](FDependsNode* A, FDependsNode* B) 
			{
				return A->GetIdentifier().LexicalLess(B->GetIdentifier());
			});
		int32 NumDependencies = Dependencies.Num();

		TMap<FDependsNode*, int32> DependsIndexMap;
		DependsIndexMap.Reserve(NumDependencies);
		int32 Index = 0;
		for (FDependsNode* Node : Dependencies)
		{
			DependsIndexMap.Add(Node, Index++);
		}

		TUniqueFunction<int32(FDependsNode*, bool bAsReferencer)> GetSerializeIndexFromNode =
			[this, &RedirectCache, &DependsIndexMap](FDependsNode* InDependency, bool bAsReferencer)
		{
			if (!bAsReferencer)
			{
				InDependency = ResolveRedirector(InDependency, CachedAssets, RedirectCache);
			}
			if (!InDependency)
			{
				return -1;
			}
			int32* DependencyIndex = DependsIndexMap.Find(InDependency);
			if (!DependencyIndex)
			{
				return -1;
			}
			return *DependencyIndex;
		};

		FDependsNode::FSaveScratch Scratch;
		Ar << NumDependencies;
		for (FDependsNode* DependentNode : Dependencies)
		{
			DependentNode->SerializeSave(Ar, GetSerializeIndexFromNode, Scratch, Options);
		}
	}
	// Write the real value to the placeholder data for the DependencySectionSize
	int64 DependencySectionEnd = Ar.Tell();
	DependencySectionSize = DependencySectionEnd - DependencySectionStart;
	Ar.Seek(OffsetToDependencySectionSize);
	Ar << DependencySectionSize;
	check(Ar.Tell() == DependencySectionStart);
	Ar.Seek(DependencySectionEnd);


	// Serialize the PackageData
	int32 PackageDataCount = 0;
	if (Options.bSerializePackageData)
	{
		PackageDataCount = CachedPackageData.Num();
		Ar << PackageDataCount;

		TArray<TPair<FName, FAssetPackageData*>> SortedPackageData = CachedPackageData.Array();
		Algo::Sort(SortedPackageData, [](TPair<FName, FAssetPackageData*>& A, TPair<FName, FAssetPackageData*>& B)
			{
				return A.Key.LexicalLess(B.Key);
			});
		for (TPair<FName, FAssetPackageData*>& Pair : SortedPackageData)
		{
			Ar << Pair.Key;
			Pair.Value->SerializeForCache(Ar);
		}
	}
	else
	{
		Ar << PackageDataCount;
	}
#endif // ALLOW_NAME_BATCH_SAVING

	return !OriginalAr.IsError();
}

bool FAssetRegistryState::Load(FArchive& OriginalAr, const FAssetRegistryLoadOptions& Options,
	FAssetRegistryVersion::Type* OutVersion)
{
	LLM_SCOPE(ELLMTag::AssetRegistry);
	FAssetRegistryHeader Header;
	Header.SerializeHeader(OriginalAr);
	if (OutVersion != nullptr)
	{
		*OutVersion = Header.Version;
	}

	FSoftObjectPathSerializationScope SerializationScope(NAME_None, NAME_None,
		ESoftObjectPathCollectType::NonPackage, ESoftObjectPathSerializeType::AlwaysSerialize);

	if (Header.Version < FAssetRegistryVersion::RemovedMD5Hash)
	{
		// Cannot read states before this version
		return false;
	}
	else if (Header.Version < FAssetRegistryVersion::FixedTags)
	{
		FNameTableArchiveReader NameTableReader(OriginalAr);
		Load(NameTableReader, Header, Options);
	}
	else
	{
		FAssetRegistryReader Reader(OriginalAr, Options.ParallelWorkers, Header);

		if (Reader.IsError())
		{
			return false;
		}

		// Load won't resolve asset registry tag values loaded in parallel
		// and can run before WaitForTasks
		Load(Reader, Header, Options);

		Reader.WaitForTasks();
	}

	return !OriginalAr.IsError();
}

/* static */ bool FAssetRegistryState::LoadFromDisk(const TCHAR* InPath, const FAssetRegistryLoadOptions& InOptions,
	FAssetRegistryState& OutState, FAssetRegistryVersion::Type* OutVersion)
{
	check(InPath);

	TUniquePtr<FArchive> FileReader(IFileManager::Get().CreateFileReader(InPath));
	if (FileReader)
	{
		// It's faster to load the whole file into memory on a Gen5 console
		TArray64<uint8> Data;
		Data.SetNumUninitialized(FileReader->TotalSize());
		FileReader->Serialize(Data.GetData(), Data.Num());
		check(!FileReader->IsError());

		FLargeMemoryReader MemoryReader(Data.GetData(), Data.Num());
		return OutState.Load(MemoryReader, InOptions, OutVersion);
	}

	return false;
}

template<class Archive>
void FAssetRegistryState::Load(Archive&& Ar, const FAssetRegistryHeader& Header,
	const FAssetRegistryLoadOptions& Options)
{
	FAssetRegistryVersion::Type Version = Header.Version;

	// serialize number of objects
	int32 LocalNumAssets = 0;
	Ar << LocalNumAssets;

	// allocate one single block for all asset data structs (to reduce tens of thousands of heap allocations)
	TArrayView<FAssetData> PreallocatedAssetDataBuffer(new FAssetData[LocalNumAssets], LocalNumAssets);
	PreallocatedAssetDataBuffers.Add(PreallocatedAssetDataBuffer.GetData());

	// Optimizing serialization of latest asset data format by moving version checking out of SerializeForCache
	// function and falling back to versioned serialization should we attempt to load an older version of AR
	// (usually commandlets)
	if (Version == FAssetRegistryVersion::LatestVersion)
	{
		for (FAssetData& NewAssetData : PreallocatedAssetDataBuffer)
		{
			NewAssetData.SerializeForCache(Ar);
		}
	}
	else
	{
		for (FAssetData& NewAssetData : PreallocatedAssetDataBuffer)
		{
			NewAssetData.SerializeForCacheOldVersion(Ar, Version);
		}
	}

	SetAssetDatas(PreallocatedAssetDataBuffer, Options);

	if (Version < FAssetRegistryVersion::AddedDependencyFlags)
	{
		LoadDependencies_BeforeFlags(Ar, Options.bLoadDependencies, Version);
	}
	else
	{
		int64 DependencySectionSize;
		Ar << DependencySectionSize;
		int64 DependencySectionEnd = Ar.Tell() + DependencySectionSize;

#if ASSET_REGISTRY_ALLOW_DEPENDENCY_SERIALIZATION
		if (Options.bLoadDependencies)
		{
			LoadDependencies(Ar);
		}
			
		if (!Options.bLoadDependencies || Ar.IsError())
		{
			Ar.Seek(DependencySectionEnd);
		}
#else
		Ar.Seek(DependencySectionEnd);
#endif
	}

	int32 LocalNumPackageData = 0;
	Ar << LocalNumPackageData;

	if (LocalNumPackageData > 0)
	{
		FAssetPackageData SerializedElement;
		TArrayView<FAssetPackageData> PreallocatedPackageDataBuffer;
		if (Options.bLoadPackageData)
		{
			PreallocatedPackageDataBuffer = TArrayView<FAssetPackageData>(
				new FAssetPackageData[LocalNumPackageData], LocalNumPackageData);
			PreallocatedPackageDataBuffers.Add(PreallocatedPackageDataBuffer.GetData());
			CachedPackageData.Reserve(LocalNumPackageData);
		}
		for (int32 PackageDataIndex = 0; PackageDataIndex < LocalNumPackageData; PackageDataIndex++)
		{
			FName PackageName;
			Ar << PackageName;
			FAssetPackageData* NewPackageData;
			if (Options.bLoadPackageData)
			{
				NewPackageData = &PreallocatedPackageDataBuffer[PackageDataIndex];
				CachedPackageData.Add(PackageName, NewPackageData);
			}
			else
			{
				NewPackageData = &SerializedElement;
			}
			if (Version >= FAssetRegistryVersion::LatestVersion)
			{
				NewPackageData->SerializeForCache(Ar);
			}
			else
			{
				NewPackageData->SerializeForCacheOldVersion(Ar, Version);
			}
		}
	}
}

void FAssetRegistryState::LoadDependencies(FArchive& Ar)
{
	int32 LocalNumDependsNodes = 0;
	Ar << LocalNumDependsNodes;

	if (LocalNumDependsNodes <= 0)
	{
		return;
	}

	FDependsNode* PreallocatedDependsNodeDataBuffer = new FDependsNode[LocalNumDependsNodes];
	PreallocatedDependsNodeDataBuffers.Add(PreallocatedDependsNodeDataBuffer);
	CachedDependsNodes.Reserve(LocalNumDependsNodes);
	
	TUniqueFunction<FDependsNode*(int32)> GetNodeFromSerializeIndex =
		[&PreallocatedDependsNodeDataBuffer, LocalNumDependsNodes](int32 Index) -> FDependsNode*
	{
		if (Index < 0 || LocalNumDependsNodes <= Index)
		{
			return nullptr;
		}
		return &PreallocatedDependsNodeDataBuffer[Index];
	};

	FDependsNode::FLoadScratch Scratch;
	for (int32 DependsNodeIndex = 0; DependsNodeIndex < LocalNumDependsNodes; DependsNodeIndex++)
	{
		FDependsNode* DependsNode = &PreallocatedDependsNodeDataBuffer[DependsNodeIndex];
		DependsNode->SerializeLoad(Ar, GetNodeFromSerializeIndex, Scratch);
		CachedDependsNodes.Add(DependsNode->GetIdentifier(), DependsNode);
	}
}

void FAssetRegistryState::LoadDependencies_BeforeFlags(FArchive& Ar, bool bSerializeDependencies,
	FAssetRegistryVersion::Type Version)
{
	int32 LocalNumDependsNodes = 0;
	Ar << LocalNumDependsNodes;

	FDependsNode Placeholder;
	FDependsNode* PreallocatedDependsNodeDataBuffer = nullptr;
	if (bSerializeDependencies && LocalNumDependsNodes > 0)
	{
		PreallocatedDependsNodeDataBuffer = new FDependsNode[LocalNumDependsNodes];
		PreallocatedDependsNodeDataBuffers.Add(PreallocatedDependsNodeDataBuffer);
		CachedDependsNodes.Reserve(LocalNumDependsNodes);
	}
	TUniqueFunction<FDependsNode* (int32)> GetNodeFromSerializeIndex =
		[&PreallocatedDependsNodeDataBuffer, LocalNumDependsNodes](int32 Index)->FDependsNode *
	{
		if (Index < 0 || LocalNumDependsNodes <= Index)
		{
			return nullptr;
		}
		return &PreallocatedDependsNodeDataBuffer[Index];
	};

	uint32 HardBits, SoftBits, HardManageBits, SoftManageBits;
	FDependsNode::GetPropertySetBits_BeforeFlags(HardBits, SoftBits, HardManageBits, SoftManageBits);

	TArray<FDependsNode*> DependsNodes;
	for (int32 DependsNodeIndex = 0; DependsNodeIndex < LocalNumDependsNodes; DependsNodeIndex++)
	{
		// Create the node if we're actually saving dependencies, otherwise just fake serialize
		FDependsNode* DependsNode = nullptr;
		if (bSerializeDependencies)
		{
			DependsNode = &PreallocatedDependsNodeDataBuffer[DependsNodeIndex];
		}
		else
		{
			DependsNode = &Placeholder;
		}

		// Call the DependsNode legacy serialization function
		DependsNode->SerializeLoad_BeforeFlags(Ar, Version, PreallocatedDependsNodeDataBuffer, LocalNumDependsNodes,
			bSerializeDependencies, HardBits, SoftBits, HardManageBits, SoftManageBits);

		// Register the DependsNode with its AssetIdentifier
		if (bSerializeDependencies)
		{
			CachedDependsNodes.Add(DependsNode->GetIdentifier(), DependsNode);
		}
	}
}

SIZE_T FAssetRegistryState::GetAllocatedSize(bool bLogDetailed) const
{
	SIZE_T MapMemory = CachedAssets.GetAllocatedSize();
#if UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	MapMemory += IndirectAssetDataArrays.GetAllocatedSize();
#endif
	MapMemory += CachedAssetsByPackageName.GetAllocatedSize();
	MapMemory += CachedAssetsByPath.GetAllocatedSize();
	MapMemory += CachedAssetsByClass.GetAllocatedSize();
#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
	MapMemory += CachedAssetsByTag.GetAllocatedSize();
#else
	MapMemory += CachedClassesByTag.GetAllocatedSize();
#endif
	MapMemory += CachedDependsNodes.GetAllocatedSize();
	MapMemory += CachedPackageData.GetAllocatedSize();
	MapMemory += PreallocatedAssetDataBuffers.GetAllocatedSize();
	MapMemory += PreallocatedDependsNodeDataBuffers.GetAllocatedSize();
	MapMemory += PreallocatedPackageDataBuffers.GetAllocatedSize();

	SIZE_T MapArrayMemory = 0;
	auto SubArray = 
		[&MapArrayMemory](const auto& A)
	{
		for (auto& Pair : A)
		{
			MapArrayMemory += Pair.Value.GetAllocatedSize();
		}
	};
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	SubArray(CachedAssetsByPackageName);
#endif
	SubArray(CachedAssetsByPath);

	for (auto& Pair : CachedAssetsByClass)
	{
		MapArrayMemory += Pair.Value.GetAllocatedSize();
	}

#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
	SubArray(CachedAssetsByTag);
#else
	SubArray(CachedClassesByTag);
#endif

	if (bLogDetailed)
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("Index Size: %" SIZE_T_FMT "k"), MapMemory / 1024);
	}

	SIZE_T AssetDataSize = 0;
	SIZE_T AssetBundlesSize = 0;
	SIZE_T NumAssetBundles = 0;
	SIZE_T NumSoftObjectPaths = 0;
	SIZE_T NumTopLevelAssetPaths = 0;
	FAssetDataTagMapSharedView::FMemoryCounter TagMemoryUsage;

	EnumerateAllAssets(
		[&AssetDataSize, &TagMemoryUsage, &NumAssetBundles, &NumSoftObjectPaths, &AssetBundlesSize,
		&NumTopLevelAssetPaths]
		(const FAssetData& AssetData)
	{
		AssetDataSize += sizeof(AssetData);
		TagMemoryUsage.Include(AssetData.TagsAndValues);
		if (AssetData.TaggedAssetBundles.IsValid())
		{
			AssetBundlesSize += sizeof(FAssetBundleData);
			AssetBundlesSize += AssetData.TaggedAssetBundles->Bundles.GetAllocatedSize();
			NumAssetBundles += AssetData.TaggedAssetBundles->Bundles.Num();
			for (const FAssetBundleEntry& Entry : AssetData.TaggedAssetBundles->Bundles)
			{
#if WITH_EDITORONLY_DATA
				PRAGMA_DISABLE_DEPRECATION_WARNINGS;
				AssetBundlesSize += Entry.BundleAssets.GetAllocatedSize();
				NumSoftObjectPaths += Entry.BundleAssets.Num();
				for (const FSoftObjectPath& Path : Entry.BundleAssets)
				{
					AssetBundlesSize += Path.GetSubPathString().GetAllocatedSize();
				}
				PRAGMA_ENABLE_DEPRECATION_WARNINGS;
#endif
				AssetBundlesSize += Entry.AssetPaths.GetAllocatedSize();
				NumTopLevelAssetPaths += Entry.AssetPaths.Num();
			}
		}
	});

	if (bLogDetailed)
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("AssetData Count: %d"), CachedAssets.Num());
		UE_LOG(LogAssetRegistry, Log, TEXT("AssetData Static Size: %" SIZE_T_FMT "k"), AssetDataSize / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("Loose Tags: %" SIZE_T_FMT "k"), TagMemoryUsage.GetLooseSize() / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("Fixed Tags: %" SIZE_T_FMT "k"), TagMemoryUsage.GetFixedSize() / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("TArray<FAssetData*>: %" SIZE_T_FMT "k"), MapArrayMemory / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("AssetBundle Count: %u"), NumAssetBundles);
		UE_LOG(LogAssetRegistry, Log, TEXT("AssetBundle Size: %uk"), AssetBundlesSize / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("AssetBundle FSoftObjectPath Count: %u"), NumSoftObjectPaths);
		UE_LOG(LogAssetRegistry, Log, TEXT("AssetBundle FTopLevelAssetPath Count: %u"), NumTopLevelAssetPaths);
	}

	SIZE_T DependNodesSize = 0, DependenciesSize = 0;

	for (const TPair<FAssetIdentifier, FDependsNode*>& DependsNodePair : CachedDependsNodes)
	{
		const FDependsNode& DependsNode = *DependsNodePair.Value;
		DependNodesSize += sizeof(DependsNode);

		DependenciesSize += DependsNode.GetAllocatedSize();
	}

	if (bLogDetailed)
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("Dependency Node Count: %d"), CachedDependsNodes.Num());
		UE_LOG(LogAssetRegistry, Log, TEXT("Dependency Node Static Size: %" SIZE_T_FMT "k"), DependNodesSize / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("Dependency Arrays Size: %" SIZE_T_FMT "k"), DependenciesSize / 1024);
	}

	SIZE_T PackageDataSize = CachedPackageData.Num() * (sizeof(FAssetPackageData) + sizeof(FAssetPackageData*));
	for (const TPair<FName, FAssetPackageData*>& PackageDataPair : CachedPackageData)
	{
		PackageDataSize += PackageDataPair.Value->GetAllocatedSize();
	}

	SIZE_T TotalBytes = MapMemory + AssetDataSize + AssetBundlesSize + TagMemoryUsage.GetFixedSize()
		+ TagMemoryUsage.GetLooseSize() + DependNodesSize + DependenciesSize + PackageDataSize + MapArrayMemory;

	if (bLogDetailed)
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("PackageData Count: %d"), CachedPackageData.Num());
		UE_LOG(LogAssetRegistry, Log, TEXT("PackageData Static Size: %" SIZE_T_FMT "k"), PackageDataSize / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("Total State Size: %" SIZE_T_FMT "k"), TotalBytes / 1024);
	}

	return TotalBytes;
}

FDependsNode* FAssetRegistryState::ResolveRedirector(FDependsNode* InDependency,
													 const FAssetDataMap& InAllowedAssets,
													 TMap<FDependsNode*, FDependsNode*>& InCache)
{
	if (InCache.Contains(InDependency))
	{
		return InCache[InDependency];
	}

	FDependsNode* CurrentDependency = InDependency;
	FDependsNode* Result = nullptr;

	TSet<FName> EncounteredDependencies;

	while (Result == nullptr)
	{
		checkSlow(CurrentDependency);

		if (EncounteredDependencies.Contains(CurrentDependency->GetPackageName()))
		{
			break;
		}

		EncounteredDependencies.Add(CurrentDependency->GetPackageName());

		if (CachedAssetsByPackageName.Contains(CurrentDependency->GetPackageName()))
		{
			// Get the list of assets contained in this package
			EnumerateAssetsByPackageName(CurrentDependency->GetPackageName(), 
				[&CurrentDependency, &InAllowedAssets, &Result, this](const FAssetData* Asset)
			{
				if (Asset->IsRedirector())
				{
					FDependsNode* ChainedRedirector = nullptr;
					// This asset is a redirector, so we want to look at its dependencies and find the asset that
					// it is redirecting to
					CurrentDependency->IterateOverDependencies(
						[&InAllowedAssets, &ChainedRedirector, &Result, this](FDependsNode* InDepends,
						UE::AssetRegistry::EDependencyCategory Category,
						UE::AssetRegistry::EDependencyProperty Property, bool bDuplicate)
					{
						if (bDuplicate)
						{
							return; // Already looked at this dependency node
						}
						const FAssetIdentifier& AssetId = InDepends->GetIdentifier();
						FSoftObjectPath AssetPath(AssetId.PackageName, AssetId.ObjectName, FString());
						if (InAllowedAssets.Contains(FCachedAssetKey(AssetPath)))
						{
							// This asset is in the allowed asset list, so take this as the redirect target
							Result = InDepends;
						}
						else if (CachedAssetsByPackageName.Contains(InDepends->GetPackageName()))
						{
							// This dependency isn't in the allowed list, but it is a valid asset in the registry.
							// Because this is a redirector, this should mean that the redirector is pointing at
							// ANOTHER redirector (or itself in some horrible situations) so we'll move to that node
							// and try again
							ChainedRedirector = InDepends;
						}
					}, UE::AssetRegistry::EDependencyCategory::Package);

					if (ChainedRedirector)
					{
						CurrentDependency = ChainedRedirector;
						return false; // Found a redirector, stop iterating assets for the current package
					}
				}
				else
				{
					Result = CurrentDependency;
				}

				if (Result)
				{
					// We found an allowed asset from the original dependency node. We're finished!
					return false; // stop iterating assets for the current package
				}
				return true; // keep iterating assets for the current package
			});
		}
		else
		{
			Result = CurrentDependency;
		}
	}

	InCache.Add(InDependency, Result);
	return Result;
}

template <typename KeyType, typename ValueType>
void ShrinkMultimap(TMap<KeyType, TArray<ValueType>>& Map)
{
	Map.Shrink();
	for (auto& Pair : Map)
	{
		Pair.Value.Shrink();
	}
};

void FAssetRegistryState::SetAssetDatas(TArrayView<FAssetData> AssetDatas, const FAssetRegistryLoadOptions& Options)
{
	using namespace UE::AssetRegistry::Private;

	UE_CLOG(NumAssets != 0, LogAssetRegistry, Fatal,
		TEXT("Can only load into empty asset registry states. Load into temporary and append using InitializeFromExisting() instead."));

	NumAssets = AssetDatas.Num();
	
	auto SetObjectPathCache = [this, &AssetDatas]()
	{
		CachedAssets.Empty(NumAssets);
		for (FAssetData& AssetData: AssetDatas)
		{
			CachedAssets.Add(&AssetData);
		}
		ensure(NumAssets == CachedAssets.Num());
	};

	// FAssetDatas sharing package name are very rare.
	// Reserve up front and don't bother shrinking. 
	auto SetPackageNameCache = [this, &AssetDatas]()
	{
		CachedAssetsByPackageName.Empty(AssetDatas.Num());
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
		for (FAssetData& AssetData : AssetDatas)
		{
			CachedAssetsByPackageName.FindOrAdd(AssetData.PackageName).Add(&AssetData);
		}
#else
		CachedAssets.Enumerate([this](FAssetData& AssetData, FAssetDataPtrIndex AssetIndex)
			{
				CachedAssetsByPackageName.Add(AssetData.PackageName, AssetIndex);
				return true;
			});
#endif
	};

	auto SetPackagePathCache = [this, &AssetDatas]()
	{
		CachedAssetsByPath.Empty();
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
		for (FAssetData& AssetData : AssetDatas)
		{
			CachedAssetsByPath.FindOrAdd(AssetData.PackagePath).Add(&AssetData);
		}
#else
		CachedAssets.Enumerate([this](FAssetData& AssetData, FAssetDataPtrIndex AssetIndex)
			{
				CachedAssetsByPath.FindOrAdd(AssetData.PackagePath).Add(AssetIndex);
				return true;
			});
#endif
		ShrinkMultimap(CachedAssetsByPath);
	};

	auto SetClassAndTagCaches = [this, &AssetDatas]()
	{
		CachedAssetsByClass.Empty();
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
		for (FAssetData& AssetData : AssetDatas)
		{
			CachedAssetsByClass.FindOrAdd(AssetData.AssetClassPath).Add(&AssetData);
		}
#else
		CachedAssets.Enumerate([this](FAssetData& AssetData, FAssetDataPtrIndex AssetIndex)
			{
				CachedAssetsByClass.FindOrAdd(AssetData.AssetClassPath).Add(AssetIndex);
				return true;
			});
#endif
		ShrinkMultimap(CachedAssetsByClass);

#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
		CachedAssetsByTag.Empty();
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
		for (FAssetData& AssetData : AssetDatas)
		{
			for (const TPair<FName, FAssetTagValueRef>& Pair : AssetData.TagsAndValues)
			{
				CachedAssetsByTag.FindOrAdd(Pair.Key).Add(&AssetData);
			}
		}
#else
		CachedAssets.Enumerate([this](FAssetData& AssetData, FAssetDataPtrIndex AssetIndex)
			{
				for (const TPair<FName, FAssetTagValueRef>& Pair : AssetData.TagsAndValues)
				{
					CachedAssetsByTag.FindOrAdd(Pair.Key).Add(AssetIndex);
				}
				return true;
			});
#endif
		CachedAssetsByTag.Shrink();
		for (auto& Pair : CachedAssetsByTag)
		{
			Pair.Value.Shrink();
		}
#else
		CachedClassesByTag.Empty();
		for (FAssetData& AssetData : AssetDatas)
		{
			for (const TPair<FName, FAssetTagValueRef>& Pair : AssetData.TagsAndValues)
			{
				CachedClassesByTag.FindOrAdd(Pair.Key).Add(AssetData.AssetClassPath);
			}
		}

		CachedClassesByTag.Shrink();
		for (TPair<FName, TSet<FTopLevelAssetPath>>& Pair : CachedClassesByTag)
		{
			Pair.Value.Shrink();
		}
#endif
	};

	if (Options.ParallelWorkers <= 1)
	{
		SetObjectPathCache();
		SetPackageNameCache();
		SetPackagePathCache();
		SetClassAndTagCaches();
	}
	else
	{
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
		TFuture<void> Task1 = Async(EAsyncExecution::TaskGraph, [&SetObjectPathCache]() { SetObjectPathCache(); });
		TFuture<void> Task2 = Async(EAsyncExecution::TaskGraph, [&SetPackageNameCache]() { SetPackageNameCache(); });
		SetPackagePathCache();
		SetClassAndTagCaches();
		Task1.Wait();
		Task2.Wait();
#else
		SetObjectPathCache();
		TFuture<void> Task1 = Async(EAsyncExecution::TaskGraph, [&SetPackagePathCache]() { SetPackagePathCache(); });
		TFuture<void> Task2 = Async(EAsyncExecution::TaskGraph, [&SetPackageNameCache]() { SetPackageNameCache(); });
		SetClassAndTagCaches();
		Task1.Wait();
		Task2.Wait();
#endif
	}
}

void FAssetRegistryState::AddAssetData(FAssetData* AssetData)
{
	using namespace UE::AssetRegistry::Private;

	bool bAlreadyInSet = false;
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	CachedAssets.Add(AssetData, &bAlreadyInSet);
	FAssetData* MapElement = AssetData;
#else
	FAssetDataPtrIndex MapElement = CachedAssets.Add(AssetData, &bAlreadyInSet);
#endif
	if (bAlreadyInSet)
	{
		UE_LOG(LogAssetRegistry, Error, TEXT("AddAssetData called with ObjectPath %s which already exists. ")
			TEXT("This will overwrite and leak the existing AssetData."), *FCachedAssetKey(*AssetData).ToString());
	}
	else
	{
		++NumAssets;
	}

#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	CachedAssetsByPackageName.FindOrAdd(AssetData->PackageName).Add(MapElement);
#else
	CachedAssetsByPackageName.Add(AssetData->PackageName, MapElement);
#endif
	CachedAssetsByPath.FindOrAdd(AssetData->PackagePath).Add(MapElement);
	CachedAssetsByClass.FindOrAdd(AssetData->AssetClassPath).Add(MapElement);

	for (auto TagIt = AssetData->TagsAndValues.CreateConstIterator(); TagIt; ++TagIt)
	{
		FName Key = TagIt.Key();

#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
		CachedAssetsByTag.FindOrAdd(Key).Add(MapElement);
#else
		CachedClassesByTag.FindOrAdd(Key).Add(AssetData->AssetClassPath);
#endif
	}
}

void FAssetRegistryState::AddTagsToAssetData(const FSoftObjectPath& InObjectPath, FAssetDataTagMap&& InTagsAndValues)
{
	using namespace UE::AssetRegistry::Private;

	if (InTagsAndValues.IsEmpty())
	{
		return;
	}

#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	FSetElementId Id = CachedAssets.FindId(FCachedAssetKey(InObjectPath));
	if (!Id.IsValidId())
#else
	FAssetDataPtrIndex Id = CachedAssets.FindId(FCachedAssetKey(InObjectPath));
	if (Id == AssetDataPtrIndexInvalid)
#endif
	{
		UE_LOG(LogAssetRegistry, Warning,
			TEXT("AddTagsToAssetData called with asset data that doesn't exist! Tags not added. ObjectPath: %s"),
			*InObjectPath.ToString());
		return;
	}
	FAssetData* AssetData = CachedAssets[Id];
	FAssetDataTagMap Tags = AssetData->TagsAndValues.CopyMap();
	Tags.Append(MoveTemp(InTagsAndValues));
	SetTagsOnExistingAsset(AssetData, MoveTemp(Tags));
}


void FAssetRegistryState::FilterTags(const FAssetRegistrySerializationOptions& Options)
{
	using namespace UE::AssetRegistry::Private;

	// Calling SetTagsOnExistingAsset for any changed tags might be slow.
	// For cases where many Assets change it might be faster to recreate CachedAssetsByTag/CachedClassesByTag rather
	// than trying to update its elements for each Asset change. For that reason we (currently) always recreate
	// CachedAssetsByTag/CachedClassesByTag.
#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
	for (auto& Pair : CachedAssetsByTag)
	{
		Pair.Value.Reset();
	}
#else
	for (TPair<FName, TSet<FTopLevelAssetPath>>& Pair : CachedClassesByTag)
	{
		Pair.Value.Reset();
	}
#endif

#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	for (FAssetData* AssetDataPtr : CachedAssets)
	{
		FAssetData& AssetData = *AssetDataPtr;
		FAssetData* AssetIndex = AssetDataPtr;
#else
	CachedAssets.Enumerate([this, &Options](FAssetData& AssetData, FAssetDataPtrIndex AssetIndex)
	{
#endif
		FAssetDataTagMap LocalTagsAndValues;
		FAssetRegistryState::FilterTags(AssetData.TagsAndValues, LocalTagsAndValues,
			Options.CookFilterlistTagsByClass.Find(AssetData.AssetClassPath), Options);
		if (LocalTagsAndValues != AssetData.TagsAndValues)
		{
			AssetData.TagsAndValues = FAssetDataTagMapSharedView(MoveTemp(LocalTagsAndValues));
		}

		// Add the AssetData to all its CachedAssetsByTag/CachedClassesByTag keys even if nothing changed, because
		// we are reconstructing all CachedAssetsByTag.
		for (auto TagIt = AssetData.TagsAndValues.CreateConstIterator(); TagIt; ++TagIt)
		{
#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
			CachedAssetsByTag.FindOrAdd(TagIt.Key()).Add(AssetIndex);
#else
			CachedClassesByTag.FindOrAdd(TagIt.Key()).Add(AssetData.AssetClassPath);
#endif
		}
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	}
#else
		return true;
	});
#endif
}

void FAssetRegistryState::SetTagsOnExistingAsset(FAssetData* AssetData, FAssetDataTagMap&& NewTags)
{
	// Update the tag cache map to remove deleted tags
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	FAssetData* AssetIndex = AssetData;
#else
	FAssetDataPtrIndex AssetIndex = CachedAssets.FindId(FCachedAssetKey(AssetData));
#endif
	for (auto TagIt = AssetData->TagsAndValues.CreateConstIterator(); TagIt; ++TagIt)
	{
		const FName FNameKey = TagIt.Key();

		if (!NewTags.Contains(FNameKey))
		{
#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
			auto* OldTagAssets = CachedAssetsByTag.Find(FNameKey);
			if (OldTagAssets)
			{
				OldTagAssets->Remove(AssetIndex);
			}
#else
			// For CachedClassesByTag, we do not need to remove the asset's class from the entries
			// for the old tags. The class is not changed and still has the possibility of containing the tags.
#endif
		}
	}
	// Update the tag cache map to add added tags
	for (auto TagIt = NewTags.CreateConstIterator(); TagIt; ++TagIt)
	{
		const FName FNameKey = TagIt.Key();

		if (!AssetData->TagsAndValues.Contains(FNameKey))
		{
#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
			CachedAssetsByTag.FindOrAdd(FNameKey).Add(AssetIndex);
#else
			CachedClassesByTag.FindOrAdd(FNameKey).Add(AssetData->AssetClassPath);
#endif
		}
	}
	AssetData->TagsAndValues = FAssetDataTagMapSharedView(MoveTemp(NewTags));
}

void FAssetRegistryState::SetDependencyNodeSorting(bool bSortDependencies, bool bSortReferencers)
{
	for (TPair<FAssetIdentifier, FDependsNode*>& Pair : CachedDependsNodes)
	{
		FDependsNode* DependsNode = Pair.Value;
		DependsNode->SetIsDependencyListSorted(UE::AssetRegistry::EDependencyCategory::All, bSortDependencies);
		DependsNode->SetIsReferencersSorted(bSortReferencers);
	}
}

void FAssetRegistryState::UpdateAssetData(const FAssetData& NewAssetData, bool bCreateIfNotExists)
{
	FAssetData* AssetData = GetMutableAssetByObjectPath(FCachedAssetKey(NewAssetData));
	if (AssetData)
	{
		UpdateAssetData(AssetData, NewAssetData);
	}
	else if (bCreateIfNotExists)
	{
		AddAssetData(new FAssetData(NewAssetData));
	}
}

void FAssetRegistryState::UpdateAssetData(FAssetData&& NewAssetData, bool bCreateIfNotExists)
{
	FAssetData* AssetData = GetMutableAssetByObjectPath(FCachedAssetKey(NewAssetData));
	if (AssetData)
	{
		UpdateAssetData(AssetData, MoveTemp(NewAssetData));
	}
	else if (bCreateIfNotExists)
	{
		AddAssetData(new FAssetData(MoveTemp(NewAssetData)));
	}
}

void FAssetRegistryState::UpdateAssetData(FAssetData* AssetData, const FAssetData& NewAssetData, bool* bOutModified)
{
	UpdateAssetData(AssetData, FAssetData(NewAssetData), bOutModified);
}

void FAssetRegistryState::UpdateAssetData(FAssetData* AssetData, FAssetData&& NewAssetData, bool* bOutModified)
{
	using namespace UE::AssetRegistry::Private;

	bool bKeyFieldIsModified = false;
	FCachedAssetKey OldKey(AssetData);
	FCachedAssetKey NewKey(NewAssetData);

#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	FAssetData* AssetIndex = AssetData;
#else
	FAssetDataPtrIndex AssetIndex = CachedAssets.FindId(OldKey);
	check(AssetIndex != AssetDataPtrIndexInvalid);
#endif

	// Update ObjectPath
	if (OldKey != NewKey)
	{
		bKeyFieldIsModified = true;
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
		int32 NumRemoved = CachedAssets.Remove(OldKey);
#else
		int32 NumRemoved = CachedAssets.RemoveOnlyKeyLookup(OldKey);
#endif
		check(NumRemoved <= 1);
		if (NumRemoved == 0)
		{
			UE_LOG(LogAssetRegistry, Error,
				TEXT("UpdateAssetData called on AssetData %s that is not present in the AssetRegistry."),
				*AssetData->GetObjectPathString());
		}
		NumAssets -= NumRemoved;
	}

	// Update PackageName
	if (AssetData->PackageName != NewAssetData.PackageName)
	{
		bKeyFieldIsModified = true;
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
		CachedAssetsByPackageName.Find(AssetData->PackageName)->Remove(AssetIndex);
		CachedAssetsByPackageName.FindOrAdd(NewAssetData.PackageName).Add(AssetIndex);
#else
		CachedAssetsByPackageName.Remove(AssetData->PackageName, AssetIndex);
		CachedAssetsByPackageName.Add(NewAssetData.PackageName, AssetIndex);
#endif
	}

	// Update PackagePath
	if (AssetData->PackagePath != NewAssetData.PackagePath)
	{
		bKeyFieldIsModified = true;

		CachedAssetsByPath.Find(AssetData->PackagePath)->Remove(AssetIndex);
		CachedAssetsByPath.FindOrAdd(NewAssetData.PackagePath).Add(AssetIndex);
	}

	// AssetName is not a keyfield; compared below

	// Update AssetClass
	if (AssetData->AssetClassPath != NewAssetData.AssetClassPath)
	{
		bKeyFieldIsModified = true;

		CachedAssetsByClass.Find(AssetData->AssetClassPath)->Remove(AssetIndex);
		CachedAssetsByClass.FindOrAdd(NewAssetData.AssetClassPath).Add(AssetIndex);
	}

	// PackageFlags is not a keyfield; compared below

	// Update Tags
	if (AssetData->TagsAndValues != NewAssetData.TagsAndValues)
	{ 
		bKeyFieldIsModified = true;
		for (auto TagIt = AssetData->TagsAndValues.CreateConstIterator(); TagIt; ++TagIt)
		{
			const FName FNameKey = TagIt.Key();

			if (!NewAssetData.TagsAndValues.Contains(FNameKey))
			{
#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
				CachedAssetsByTag.Find(FNameKey)->Remove(AssetIndex);
#else
				// For CachedClassesByTag, we do not need to remove the asset's class from the entries
				// for the old tags. The class is not changed and still has the possibility of containing the tags.
#endif
			}
		}

		for (auto TagIt = NewAssetData.TagsAndValues.CreateConstIterator(); TagIt; ++TagIt)
		{
			const FName FNameKey = TagIt.Key();

			if (!AssetData->TagsAndValues.Contains(FNameKey))
			{
#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
				CachedAssetsByTag.FindOrAdd(FNameKey).Add(AssetIndex);
#else
				CachedClassesByTag.FindOrAdd(FNameKey).Add(AssetData->AssetClassPath);
#endif
			}
		}
	}

	// TaggedAssetBundles is not a keyfield; compared below

	// ChunkIds is not a keyfield; compared below

	if (bOutModified)
	{
		// Computing equality is expensive; if the caller needs to know it, do cheap compares first 
		// so we can skip the more expensive compares if the inequality is already known
		// This is not possible for keyfields - we have to take action on those even if inequality is already known -
		// so we start with whether bKeyFieldIsModified
		*bOutModified = bKeyFieldIsModified ||
			AssetData->AssetName != NewAssetData.AssetName ||
			AssetData->PackageFlags != NewAssetData.PackageFlags ||
			!AssetData->HasSameChunkIDs(NewAssetData) ||
			(AssetData->TaggedAssetBundles.IsValid() != NewAssetData.TaggedAssetBundles.IsValid() ||
				(AssetData->TaggedAssetBundles.IsValid() &&
					// First check whether the pointers are the same
					AssetData->TaggedAssetBundles.Get() != NewAssetData.TaggedAssetBundles.Get()
					// If the pointers differ, check whether the contents differ
					&& *AssetData->TaggedAssetBundles != *NewAssetData.TaggedAssetBundles
					));
	}

	// Copy in new values
	*AssetData = MoveTemp(NewAssetData);

	// Can only re-add to asset map after we update the key fields, because those change the hashvalue in CachedAssets
	if (OldKey != NewKey)
	{
		bool bExisting = false;
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
		CachedAssets.Add(AssetData, &bExisting);
#else
		CachedAssets.AddKeyLookup(AssetData, AssetIndex, &bExisting);
#endif
		if (bExisting)
		{
			UE_LOG(LogAssetRegistry, Error,
				TEXT("UpdateAssetData called with a change in ObjectPath from Old=\"%s\" to New=\"%s\", ")
				TEXT("but the new ObjectPath is already present with another AssetData. This will overwrite and leak the existing AssetData."),
				*OldKey.ToString(), *NewKey.ToString());
}
		else
		{
			++NumAssets;
		}
	}
}

bool FAssetRegistryState::UpdateAssetDataPackageFlags(FName PackageName, uint32 PackageFlags)
{
	bool bFoundValue = false;
	EnumerateMutableAssetsByPackageName(PackageName, [&bFoundValue, PackageFlags](FAssetData* AssetData)
		{
			AssetData->PackageFlags = PackageFlags;
			bFoundValue = true;
			return true;
		});
	return bFoundValue;
}

void FAssetRegistryState::RemoveAssetData(FAssetData* AssetData, bool bRemoveDependencyData,
	bool& bOutRemovedAssetData, bool& bOutRemovedPackageData)
{
	using namespace UE::AssetRegistry::Private;

	if (!ensure(AssetData))
	{
		bOutRemovedAssetData = false;
		bOutRemovedPackageData = false;
		return;
	}
	FCachedAssetKey AssetKey(AssetData);
#if UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	FAssetDataPtrIndex AssetIndex = CachedAssets.FindId(AssetKey);
	if (AssetIndex == AssetDataPtrIndexInvalid)
	{
		bOutRemovedAssetData = false;
		bOutRemovedPackageData = false;
	}
	else
#endif
	{
		RemoveAssetData(AssetData, AssetKey, bRemoveDependencyData,
#if UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
			AssetIndex,
#endif
			bOutRemovedAssetData, bOutRemovedPackageData
		);
	}
	if (!bOutRemovedAssetData)
	{
		UE_LOG(LogAssetRegistry, Error,
			TEXT("RemoveAssetData called on AssetData %s that is not present in the AssetRegistry."),
			*FCachedAssetKey(*AssetData).ToString());
	}
}

void FAssetRegistryState::RemoveAssetData(const FSoftObjectPath& AssetPath, bool bRemoveDependencyData,
	bool& bOutRemovedAssetData, bool& bOutRemovedPackageData)
{
	using namespace UE::AssetRegistry::Private;

	FCachedAssetKey Key(AssetPath);
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	FAssetData** AssetDataPtrPtr = CachedAssets.Find(Key);
	FAssetData* AssetData = AssetDataPtrPtr ? *AssetDataPtrPtr : nullptr;
#else
	FAssetDataPtrIndex AssetIndex = CachedAssets.FindId(Key);
	FAssetData* AssetData = AssetIndex != AssetDataPtrIndexInvalid ? CachedAssets[AssetIndex] : nullptr;
#endif
	if (!AssetData)
	{
		bOutRemovedAssetData = false;
		bOutRemovedPackageData = false;
		return;
	}
	RemoveAssetData(AssetData, Key, bRemoveDependencyData,
#if UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
		AssetIndex,
#endif
		bOutRemovedAssetData, bOutRemovedPackageData
	);
}

void FAssetRegistryState::RemoveAssetData(FAssetData* AssetData, const FCachedAssetKey& Key,
	bool bRemoveDependencyData,
#if UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	UE::AssetRegistry::Private::FAssetDataPtrIndex AssetIndex,
#endif
	bool& bOutRemovedAssetData, bool& bOutRemovedPackageData
)
{
	using namespace UE::AssetRegistry::Private;

	bOutRemovedAssetData = false;
	bOutRemovedPackageData = false;

	if (!CachedAssets.Find(Key))
	{
		return;
	}

#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	FAssetData* AssetIndex = AssetData;

	TArray<FAssetData*, TInlineAllocator<1>>* OldPackageAssets =
		CachedAssetsByPackageName.Find(AssetData->PackageName);
	OldPackageAssets->RemoveSingleSwap(AssetIndex);
	bool bOldPackageAssetsEmpty = OldPackageAssets->Num() == 0;
	if (bOldPackageAssetsEmpty)
	{
		CachedAssetsByPackageName.Remove(AssetData->PackageName);
	}
#else
	CachedAssetsByPackageName.Remove(AssetData->PackageName, AssetIndex);
	bool bOldPackageAssetsEmpty = !CachedAssetsByPackageName.Contains(AssetData->PackageName);
#endif
	CachedAssetsByPath.Find(AssetData->PackagePath)->RemoveSingleSwap(AssetIndex);
	CachedAssetsByClass.Find(AssetData->AssetClassPath)->RemoveSingleSwap(AssetIndex);

#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
	for (auto TagIt = AssetData->TagsAndValues.CreateConstIterator(); TagIt; ++TagIt)
	{
		CachedAssetsByTag.Find(TagIt.Key())->Remove(AssetIndex);
	}
#else
	// For CachedClassesByTag, we do not need to remove the asset's class from the entries
	// for the old tags. The class is not changed and still has the possibility of containing the tags.
#endif

	// In the UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS case the other containers hold an index into CachedAssets,
	// so we can only remove from CachedAssets after removing from all other containers.
	CachedAssets.Remove(Key);

	// Only remove dependencies and package data if there are no other known assets in the package
	if (bOldPackageAssetsEmpty)
	{
		// We need to update the cached dependencies references cache so that they know we no
		// longer exist and so don't reference them.
		if (bRemoveDependencyData)
		{
			RemoveDependsNode(AssetData->PackageName);
		}

		// Remove the package data as well
		RemovePackageData(AssetData->PackageName);
		bOutRemovedPackageData = true;
	}

	// if the assets were preallocated in a block, we can't delete them one at a time,
	// only the whole chunk in the destructor
	if (PreallocatedAssetDataBuffers.Num() == 0)
	{
		delete AssetData;
	}
	NumAssets--;
	bOutRemovedAssetData = true;
}

FDependsNode* FAssetRegistryState::FindDependsNode(const FAssetIdentifier& Identifier) const
{
	FDependsNode*const* FoundNode = CachedDependsNodes.Find(Identifier);
	if (FoundNode)
	{
		return *FoundNode;
	}
	else
	{
		return nullptr;
	}
}

FDependsNode* FAssetRegistryState::CreateOrFindDependsNode(const FAssetIdentifier& Identifier)
{
	FDependsNode*& Node = CachedDependsNodes.FindOrAdd(Identifier);
	if (Node == nullptr)
	{
		Node = new FDependsNode(Identifier);
		NumDependsNodes++;
	}
	return Node;
}

bool FAssetRegistryState::RemoveDependsNode(const FAssetIdentifier& Identifier)
{
	FDependsNode** NodePtr = CachedDependsNodes.Find(Identifier);

	if (NodePtr != nullptr)
	{
		FDependsNode* Node = *NodePtr;
		if (Node != nullptr)
		{
			TArray<FDependsNode*> DependencyNodes;
			Node->GetDependencies(DependencyNodes);

			// Remove the reference to this node from all dependencies
			for (FDependsNode* DependencyNode : DependencyNodes)
			{
				DependencyNode->RemoveReferencer(Node);
			}

			TArray<FDependsNode*> ReferencerNodes;
			Node->GetReferencers(ReferencerNodes);

			// Remove the reference to this node from all referencers
			for (FDependsNode* ReferencerNode : ReferencerNodes)
			{
				ReferencerNode->RemoveDependency(Node);
			}

			// Remove the node and delete it
			CachedDependsNodes.Remove(Identifier);
			NumDependsNodes--;

			// if the depends nodes were preallocated in a block, we can't delete them one at a time,
			// only the whole chunk in the destructor
			if (PreallocatedDependsNodeDataBuffers.Num() == 0)
			{
				delete Node;
			}

			return true;
		}
	}

	return false;
}

void FAssetRegistryState::GetPrimaryAssetsIds(TSet<FPrimaryAssetId>& OutPrimaryAssets) const
{
	EnumerateAllAssets([&OutPrimaryAssets](const FAssetData& AssetData)
		{
			FPrimaryAssetId PrimaryAssetId = AssetData.GetPrimaryAssetId();
			if (PrimaryAssetId.IsValid())
			{
				OutPrimaryAssets.Add(PrimaryAssetId);
			}
		});
}

const FAssetPackageData* FAssetRegistryState::GetAssetPackageData(FName PackageName) const
{
	FAssetPackageData* const* FoundData = CachedPackageData.Find(PackageName);
	return FoundData ? *FoundData : nullptr;
}

FAssetPackageData* FAssetRegistryState::GetAssetPackageData(FName PackageName)
{
	FAssetPackageData** FoundData = CachedPackageData.Find(PackageName);
	return FoundData ? *FoundData : nullptr;
}

const FAssetPackageData* FAssetRegistryState::GetAssetPackageData(FName PackageName,
	FName& OutCorrectCasePackageName) const
{
	// CachedPackageData is keyed using the Package Names whose casing matches the filesystem. In order to perform a
	// single look up for the AssetPackageData while also returning the value of the key used to add to the map
	// originally we create a KeyIterator which is currently the only means to get a TPair<Key,Value> from a
	// TMap<Key,Value>
	TMap<FName, FAssetPackageData*>::TConstKeyIterator It = CachedPackageData.CreateConstKeyIterator(PackageName);
	FSetElementId Id = It.GetId();
	if (!Id.IsValidId())
	{
		return nullptr;
	}

	const TPair<FName, FAssetPackageData*>& Pair = CachedPackageData.Get(Id);
	OutCorrectCasePackageName = Pair.Key;
	return Pair.Value;
}

FAssetPackageData* FAssetRegistryState::CreateOrGetAssetPackageData(FName PackageName)
{
	FAssetPackageData*& Data = CachedPackageData.FindOrAdd(PackageName);
	if (Data == nullptr)
	{
		Data = new FAssetPackageData();
		NumPackageData++;
	}
	return Data;
}

bool FAssetRegistryState::RemovePackageData(FName PackageName)
{
	FAssetPackageData** DataPtr = CachedPackageData.Find(PackageName);

	if (DataPtr != nullptr)
	{
		FAssetPackageData* Data = *DataPtr;
		if (Data != nullptr)
		{
			CachedPackageData.Remove(PackageName);
			NumPackageData--;

			// if the package data was preallocated in a block, we can't delete them one at a time,
			// only the whole chunk in the destructor
			if (PreallocatedPackageDataBuffers.Num() == 0)
			{
				delete Data;
			}

			return true;
		}
	}
	return false;
}

bool FAssetRegistryState::IsFilterValid(const FARCompiledFilter& Filter)
{
	return UE::AssetRegistry::Utils::IsFilterValid(Filter);
}

void FAssetRegistryState::EnumerateAssetsByTagName(const FName TagName,
	TFunctionRef<bool(const FAssetData* AssetData)> Callback) const
{
	using namespace UE::AssetRegistry::Private;

#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	const TSet<FAssetData*>* FoundAssets = CachedAssetsByTag.Find(TagName);
	if (FoundAssets)
	{
		for (const FAssetData* AssetData : *FoundAssets)
		{
			if (!Callback(AssetData))
			{
				break;
			}
		}
	}
#else
	const TSet<FAssetDataPtrIndex>* FoundAssets = CachedAssetsByTag.Find(TagName);
	if (FoundAssets)
	{
		for (FAssetDataPtrIndex AssetIndex : *FoundAssets)
		{
			if (!Callback(CachedAssets[AssetIndex]))
			{
				break;
			}
		}
	}
#endif
#else
	const TSet<FTopLevelAssetPath>* FoundClasses = CachedClassesByTag.Find(TagName);
	if (!FoundClasses)
	{
		return;
	}

	// The lists of assets in CachedAssetsByClass are non-intersecting (each list is only the exact instances of that
	// class and does not include subclasses), so we do not need to handle removing duplicates when merging lists from
	// multiple classes.
	TArray<FAssetData*> PossibleAssets;
	for (const FTopLevelAssetPath& ClassPath : *FoundClasses)
	{
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
		const TArray<FAssetData*>* ClassAssets = CachedAssetsByClass.Find(ClassPath);
		if (ClassAssets)
		{
			PossibleAssets.Append(*ClassAssets);
		}
#else
		const TArray<FAssetDataPtrIndex>* ClassAssets = CachedAssetsByClass.Find(ClassPath);
		if (ClassAssets)
		{
			PossibleAssets.Reserve(PossibleAssets.Num() + ClassAssets->Num());
			for (FAssetDataPtrIndex AssetIndex : *ClassAssets)
			{
				PossibleAssets.Add(CachedAssets[AssetIndex]);
			}
		}
#endif
	}

	for (const FAssetData* AssetData : PossibleAssets)
	{
		// Some assets are in a class that could have the tag, but the specific asset actually does not have the tag.
		if (AssetData->FindTag(TagName))
		{
			if (!Callback(AssetData))
			{
				break;
			}
		}
	}
#endif
}

void FAssetRegistryState::EnumerateTagToAssetDatas(
	TFunctionRef<bool(FName TagName, IAssetRegistry::FEnumerateAssetDatasFunc EnumerateAssets)> Callback) const
{
#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
	for (const TPair<FName, TSet<FAssetData*>>& Pair : CachedAssetsByTag)
	{
#else
	for (const TPair<FName, TSet<FAssetDataPtrIndex>>& Pair : CachedAssetsByTag)
	{
#endif
		const bool bKeepEnumerating = Callback(Pair.Key,
			[&Pair, this](IAssetRegistry::FAssetDataFunc AssetCallback)
			{
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
				for (const FAssetData* AssetData : Pair.Value)
				{
#else
				for (FAssetDataPtrIndex AssetIndex : Pair.Value)
				{
					FAssetData* AssetData = CachedAssets[AssetIndex];
#endif
					if (!AssetCallback(AssetData))
					{
						return false;
					}
				}

				return true;
			});

		if (!bKeepEnumerating)
		{
			break;
		}
	}
#else
	for (const TPair<FName, TSet<FTopLevelAssetPath>>& Pair : CachedClassesByTag)
	{
		const bool bKeepEnumerating = Callback(Pair.Key, [&Pair, this](IAssetRegistry::FAssetDataFunc AssetCallback)
			{
				EnumerateAssetsByTagName(Pair.Key, [&AssetCallback](const FAssetData* AssetData)
					{
						if (!AssetCallback(AssetData))
						{
							return false;
						}
						return true;
					});

				return true;
			});

		if (!bKeepEnumerating)
		{
			break;
		}
	}
#endif
}

bool FAssetRegistryState::IsPackageUnmountedAndFiltered(const FName PackageName) const
{
	// TODO: This can be removed once UE-178174 is fixed, as there will no longer be unmounted content to enumerate
#if WITH_EDITOR
	// Note: We currently only perform this filtering in the editor; runtime use will have to perform its own
	//       filtering via FPackageName::IsValidPath so that it can choose to accept the additional cost of running
	//       that filter
	return bCookedGlobalAssetRegistryState && GIsEditor && !FPackageName::IsValidPath(WriteToString<256>(PackageName));
#else
	return false;
#endif
}

namespace UE::AssetRegistry::Utils
{

bool IsFilterValid(const FARCompiledFilter& Filter)
{
	if (Filter.PackageNames.Contains(NAME_None) ||
		Filter.PackagePaths.Contains(NAME_None) ||
		Filter.SoftObjectPaths.Contains(FSoftObjectPath()) ||
		Filter.ClassPaths.Contains(FTopLevelAssetPath()) ||
		Filter.TagsAndValues.Contains(NAME_None)
		)
	{
		return false;
	}

	return true;
}

}

#if ASSET_REGISTRY_STATE_DUMPING_ENABLED
namespace UE::AssetRegistry
{

void PropertiesToString(EDependencyProperty Properties, FStringBuilderBase& Builder, EDependencyCategory CategoryFilter)
{
	bool bFirst = true;
	auto AppendPropertyName = [&Properties, &Builder, &bFirst](EDependencyProperty TestProperty,
		const TCHAR* NameWith, const TCHAR* NameWithout)
	{
		if (!bFirst)
		{
			Builder.Append(TEXT(","));
		}
		if (!!(Properties & TestProperty))
		{
			Builder.Append(NameWith);
		}
		else
		{
			Builder.Append(NameWithout);
		}
		bFirst = false;
	};
	if (!!(CategoryFilter & EDependencyCategory::Package))
	{
		AppendPropertyName(EDependencyProperty::Hard, TEXT("Hard"), TEXT("Soft"));
		AppendPropertyName(EDependencyProperty::Game, TEXT("Game"), TEXT("EditorOnly"));
		AppendPropertyName(EDependencyProperty::Build, TEXT("Build"), TEXT("NotBuild"));
	}
	if (!!(CategoryFilter & EDependencyCategory::Manage))
	{
		AppendPropertyName(EDependencyProperty::Direct, TEXT("Direct"), TEXT("Indirect"));
	}
	static_assert((EDependencyProperty::PackageMask
		| EDependencyProperty::SearchableNameMask
		| EDependencyProperty::ManageMask)
		== EDependencyProperty::AllMask,
		"Need to handle new flags in this function");
}

} // namespace UE::AssetRegistry

bool PrintAssetDataMapKeyIsLess(FName A, FName B)
{
	return A.Compare(B) < 0;
}

bool PrintAssetDataMapKeyIsLess(const FString& A, const FString& B)
{
	return A.Compare(B, ESearchCase::IgnoreCase) < 0;
}

bool PrintAssetDataMapKeyIsLess(const FTopLevelAssetPath& A, const FTopLevelAssetPath& B)
{
	return A.Compare(B) < 0;
}

template <typename KeyType>
struct FPrintAssetDataMapKeyIsLess
{
	bool operator()(const KeyType& A, const KeyType& B) const
	{
		return PrintAssetDataMapKeyIsLess(A, B);
	}
};

template <typename MapType>
static void PrintAssetDataMap(FString Name, const MapType& AssetMap, TStringBuilder<16>& PageBuffer,
	const TFunctionRef<void()>& AddLine,
	const UE::AssetRegistry::Private::FAssetDataMap& CachedAssets,
	TUniqueFunction<void(const typename MapType::KeyType& Key, const FAssetData& Data)>&& PrintValue = {})
{
	using namespace UE::AssetRegistry::Private;

	PageBuffer.Appendf(TEXT("--- Begin %s ---"), *Name);
	AddLine();

	TArray<typename MapType::KeyType> Keys;
	AssetMap.GenerateKeyArray(Keys);

	Keys.Sort(FPrintAssetDataMapKeyIsLess<typename MapType::KeyType>());

	TArray<FAssetData*> Items;
	Items.Reserve(1024);

	int32 ValidCount = 0;
	for (const typename MapType::KeyType& Key : Keys)
	{
		const auto& AssetArray = AssetMap.FindChecked(Key);
		if (AssetArray.Num() == 0)
		{
			continue;
		}
		++ValidCount;

		Items.Reset();
		Items.Reserve(AssetArray.Num());
		for (const auto& It : AssetArray)
		{
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
			Items.Add(It);
#else
			Items.Add(CachedAssets[It]);
#endif
		}
		Items.Sort([](const FAssetData& A, const FAssetData& B)
			{
				return A.GetSoftObjectPath().LexicalLess(B.GetSoftObjectPath());
			});

		PageBuffer.Append(TEXT("\t"));
		Key.AppendString(PageBuffer);
		PageBuffer.Appendf(TEXT(" : %d item(s)"), Items.Num());
		AddLine();
		for (const FAssetData* Data : Items)
		{
			PageBuffer.Append(TEXT("\t\t"));
			Data->AppendObjectPath(PageBuffer);
			if (PrintValue)
			{
				PrintValue(Key, *Data);
			}
			AddLine();
		}
	}

	PageBuffer.Appendf(TEXT("--- End %s : %d entries ---"), *Name, ValidCount);
	AddLine();
};

template <typename MapType>
static void PrintClassDataMap(FString Name, const MapType& ClassPathMap, TStringBuilder<16>& PageBuffer,
	const TFunctionRef<void()>& AddLine)
{
	PageBuffer.Appendf(TEXT("--- Begin %s ---"), *Name);
	AddLine();

	TArray<typename MapType::KeyType> Keys;
	ClassPathMap.GenerateKeyArray(Keys);

	Keys.Sort(FPrintAssetDataMapKeyIsLess<typename MapType::KeyType>());

	TArray<FTopLevelAssetPath> Items;
	Items.Reserve(1024);

	int32 ValidCount = 0;
	for (const typename MapType::KeyType& Key : Keys)
	{
		const TSet<FTopLevelAssetPath>& ClassPaths = ClassPathMap.FindChecked(Key);
		if (ClassPaths.Num() == 0)
		{
			continue;
		}
		++ValidCount;

		Items.Reset();
		Items.Reserve(ClassPaths.Num());
		for (const FTopLevelAssetPath& ClassPath : ClassPaths)
		{
			Items.Add(ClassPath);
		}
		Items.Sort([](const FTopLevelAssetPath& A, const FTopLevelAssetPath& B)
			{
				return A.Compare(B) < 0;
			});

		PageBuffer.Append(TEXT("\t"));
		Key.AppendString(PageBuffer);
		PageBuffer.Appendf(TEXT(" : %d item(s)"), Items.Num());
		AddLine();
		for (const FTopLevelAssetPath& Data : Items)
		{
			PageBuffer.Append(TEXT("\t\t"));
			Data.AppendString(PageBuffer);
			AddLine();
		}
	}

	PageBuffer.Appendf(TEXT("--- End %s : %d entries ---"), *Name, ValidCount);
	AddLine();
};

#if UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
template <typename MapType>
static void PrintPackageNameMap(FString Name, const MapType& AssetMap, TStringBuilder<16>& PageBuffer,
	const TFunctionRef<void()>& AddLine,
	const UE::AssetRegistry::Private::FAssetDataMap& CachedAssets)
{
	using namespace UE::AssetRegistry::Private;

	PageBuffer.Appendf(TEXT("--- Begin %s ---"), *Name);
	AddLine();

	TArray<typename MapType::KeyType> Keys;
	AssetMap.GenerateKeyArray(Keys);

	Keys.Sort(FPrintAssetDataMapKeyIsLess<typename MapType::KeyType>());

	TArray<FAssetData*> Items;
	Items.Reserve(1024);

	int32 ValidCount = 0;
	for (const typename MapType::KeyType& Key : Keys)
	{
		TOptional<TConstArrayView<FAssetDataPtrIndex>> AssetArrayPtr = AssetMap.Find(Key);
		TConstArrayView<FAssetDataPtrIndex> AssetArray = AssetArrayPtr
			? *AssetArrayPtr : TConstArrayView<FAssetDataPtrIndex>();
		if (AssetArray.Num() == 0)
		{
			continue;
		}
		++ValidCount;

		Items.Reset();
		Items.Reserve(AssetArray.Num());
		for (const auto& It : AssetArray)
		{
			Items.Add(CachedAssets[It]);
		}
		Items.Sort([](const FAssetData& A, const FAssetData& B)
			{
				return A.GetSoftObjectPath().LexicalLess(B.GetSoftObjectPath());
			});

		PageBuffer.Append(TEXT("\t"));
		Key.AppendString(PageBuffer);
		PageBuffer.Appendf(TEXT(" : %d item(s)"), Items.Num());
		AddLine();
		for (const FAssetData* Data : Items)
		{
			PageBuffer.Append(TEXT("\t\t"));
			Data->AppendObjectPath(PageBuffer);
			AddLine();
		}
	}

	PageBuffer.Appendf(TEXT("--- End %s : %d entries ---"), *Name, ValidCount);
	AddLine();
};
#endif

void FAssetRegistryState::Dump(const TArray<FString>& Arguments, TArray<FString>& OutPages, int32 LinesPerPage) const
{
	int32 ExpectedNumLines = 14 + CachedAssets.Num() * 5 + CachedDependsNodes.Num() + CachedPackageData.Num();
	const int32 EstimatedLinksPerNode = 10*2; // Each dependency shows up once as a dependency and once as a reference
	const int32 EstimatedCharactersPerLine = 100;
	bool bAllFields = Arguments.Contains(TEXT("All"));

	const bool bDumpDependencyDetails = bAllFields || Arguments.Contains(TEXT("DependencyDetails"));
	if (bDumpDependencyDetails)
	{
		ExpectedNumLines += CachedDependsNodes.Num() * (3 + EstimatedLinksPerNode);
	}
	LinesPerPage = FMath::Max(0, LinesPerPage);
	const int32 ExpectedNumPages = LinesPerPage > 0 ? (ExpectedNumLines / LinesPerPage) : 1;
	const int32 PageEndSearchLength = FMath::Min(LinesPerPage, ExpectedNumLines) / 20;
	// Pick a large starting value to bias against picking empty string
	const uint32 HashStartValue = MAX_uint32 - 49979693; 
	const uint32 HashMultiplier = 67867967;
	TStringBuilder<16> PageBuffer;
	TStringBuilder<16> OverflowText;

	OutPages.Reserve(ExpectedNumPages);
	// TODO: Add Reserve function to TStringBuilder
	PageBuffer.AddUninitialized(FMath::Min(LinesPerPage, ExpectedNumLines) * EstimatedCharactersPerLine);
	PageBuffer.Reset();
	OverflowText.AddUninitialized(PageEndSearchLength * EstimatedCharactersPerLine);
	OverflowText.Reset();
	int32 NumLinesInPage = 0;
	const int32 LineTerminatorLen = TCString<TCHAR>::Strlen(LINE_TERMINATOR);

	auto FinishPage = [&PageBuffer, &NumLinesInPage, HashStartValue, HashMultiplier, PageEndSearchLength,
		&OutPages, &OverflowText, LineTerminatorLen](bool bManualPageBreak)
	{
		int32 PageEndIndex = PageBuffer.Len();
		const TCHAR* BufferEnd = PageBuffer.GetData() + PageEndIndex;
		int32 NumOverflowLines = 0;
		// We want to facilitate diffing dumps between two different versions that should be similar,
		// but naively breaking up the dump into pages makes this difficult.
		// because after one missing or added line, every page from that point on will be offset and
		// therefore different, making false positive differences.
		// To make pages after one missing or added line the same, we look for a good page ending based 
		// on the text of all the lines near the end of the current page.
		// By choosing specific-valued texts as page breaks, we will usually randomly get lucky and
		// have the two diffs pick the same line for the end of the page.
		if (!bManualPageBreak && NumLinesInPage > PageEndSearchLength)
		{
			const TCHAR* WinningLineEnd = BufferEnd;
			uint32 WinningLineValue = 0;
			int32 WinningSearchIndex = 0;
			const TCHAR* LineEnd = BufferEnd;
			for (int32 SearchIndex = 0; SearchIndex < PageEndSearchLength; ++SearchIndex)
			{
				uint32 LineValue = HashStartValue;
				const TCHAR* LineStart = LineEnd;
				while (LineStart[-LineTerminatorLen] != LINE_TERMINATOR[0]
					|| TCString<TCHAR>::Strncmp(LINE_TERMINATOR,
						LineStart - LineTerminatorLen, LineTerminatorLen) != 0)
				{
					--LineStart;
					LineValue = LineValue * HashMultiplier + static_cast<uint32>(TChar<TCHAR>::ToLower(*LineStart));
				}
				// We arbitrarily choose the smallest hash as the winning value
				if (SearchIndex == 0 || LineValue < WinningLineValue)
				{
					WinningLineValue = LineValue;
					WinningLineEnd = LineEnd;
					WinningSearchIndex = SearchIndex;
				}
				LineEnd = LineStart - LineTerminatorLen;
			}
			if (WinningLineEnd != BufferEnd)
			{
				PageEndIndex = UE_PTRDIFF_TO_INT32(WinningLineEnd - PageBuffer.GetData());
				NumOverflowLines = WinningSearchIndex;
			}
		}

		OutPages.Add(FString::ConstructFromPtrSize(PageBuffer.GetData(), PageEndIndex));
		if (PageEndIndex != PageBuffer.Len())
		{
			PageEndIndex += LineTerminatorLen; // Skip the newline
			OverflowText.Reset();
			OverflowText.Append(PageBuffer.GetData() + PageEndIndex, PageBuffer.Len() - PageEndIndex);
			PageBuffer.Reset();
			PageBuffer.Append(OverflowText);
			PageBuffer.Append(LINE_TERMINATOR);
			NumLinesInPage = NumOverflowLines;
		}
		else
		{
			PageBuffer.Reset();
			NumLinesInPage = 0;
		}
	};
	auto AddLine = [&PageBuffer, LinesPerPage, &NumLinesInPage, &FinishPage, &OutPages]()
	{
		if (LinesPerPage == 1)
		{
			OutPages.Add(FString::ConstructFromPtrSize(PageBuffer.GetData(), PageBuffer.Len()));
			PageBuffer.Reset();
		}
		else
		{
			++NumLinesInPage;
			if (LinesPerPage == 0 || NumLinesInPage < LinesPerPage)
			{
				PageBuffer.Append(LINE_TERMINATOR);
			}
			else
			{
				FinishPage(false);
			}
		}
	};
	auto AddPageBreak = [LinesPerPage, &NumLinesInPage, &FinishPage]()
	{
		if (LinesPerPage > 1 && NumLinesInPage != 0)
		{
			FinishPage(true);
		}
	};

	if (bAllFields || Arguments.Contains(TEXT("ObjectPath")))
	{
		AddPageBreak();
		PageBuffer.Append(TEXT("--- Begin CachedAssetsByObjectPath ---"));
		AddLine();

		TArray<FCachedAssetKey> Keys;
		Keys.Reserve(CachedAssets.Num());
		EnumerateAllAssets([&Keys](const FAssetData& AssetData)
			{
				Keys.Emplace(FCachedAssetKey(AssetData));
			});
		Keys.Sort([](const FCachedAssetKey& A, const FCachedAssetKey& B) {
			return WriteToString<1024>(A).ToView().Compare(WriteToString<1024>(B).ToView(),
				ESearchCase::IgnoreCase) < 0;
		});

		for (const FCachedAssetKey& Key : Keys)
		{
			PageBuffer.Append(TEXT("	"));
			Key.AppendString(PageBuffer);
			AddLine();
		}

		PageBuffer.Appendf(TEXT("--- End CachedAssetsByObjectPath : %d entries ---"), CachedAssets.Num());
		AddLine();
	}

	if (bAllFields || Arguments.Contains(TEXT("PackageName")))
	{
		AddPageBreak();
#if !UE_ASSETREGISTRY_INDIRECT_ASSETDATA_POINTERS
		PrintAssetDataMap(TEXT("CachedAssetsByPackageName"), CachedAssetsByPackageName, PageBuffer, AddLine, CachedAssets);
#else
		PrintPackageNameMap(TEXT("CachedAssetsByPackageName"), CachedAssetsByPackageName, PageBuffer, AddLine, CachedAssets);
#endif
	}

	if (bAllFields || Arguments.Contains(TEXT("Path")))
	{
		AddPageBreak();
		PrintAssetDataMap(TEXT("CachedAssetsByPath"), CachedAssetsByPath, PageBuffer, AddLine, CachedAssets);
	}

	if (bAllFields || Arguments.Contains(TEXT("Class")))
	{
		AddPageBreak();
		PrintAssetDataMap(TEXT("CachedAssetsByClass"), CachedAssetsByClass, PageBuffer, AddLine, CachedAssets);
	}

	// Only print this if it's requested specifically - '-all' will print tags-per-asset rather than assets-per-tag.
	if (Arguments.Contains(TEXT("Tag")))
	{
		AddPageBreak();
#if UE_ASSETREGISTRY_CACHEDASSETSBYTAG
		PrintAssetDataMap(TEXT("CachedAssetsByTag"), CachedAssetsByTag, PageBuffer, AddLine, CachedAssets,
			[&PageBuffer, &AddLine](const FName& TagName, const FAssetData& Data)
			{
				PageBuffer << TEXT(", ") << Data.TagsAndValues.FindTag(TagName).ToLoose();
			});
#else
		PrintClassDataMap(TEXT("CachedClassesByTag"), CachedClassesByTag, PageBuffer, AddLine);
#endif
	}

	TArray<const FAssetData*> SortedAssets;
	auto InitializeSortedAssets = [&SortedAssets, this]()
	{
		if (SortedAssets.Num() != CachedAssets.Num())
		{
			SortedAssets.Reserve(CachedAssets.Num());
			EnumerateAllAssets([&SortedAssets](const FAssetData& AssetData)
				{
					SortedAssets.Add(&AssetData);
				});
			Algo::Sort(SortedAssets, [](const FAssetData* A, const FAssetData* B)
				{ return A->GetSoftObjectPath().LexicalLess(B->GetSoftObjectPath()); }
			);
		}
	};

	if (bAllFields || Arguments.Contains(TEXT("AssetTags")))
	{
		int32 Counter = 0;
		AddPageBreak();
		PageBuffer.Append(TEXT("--- Begin AssetTags ---"));
		AddLine();

		InitializeSortedAssets();
		TArray<FName> SortedTagKeys;
		for (const FAssetData* AssetData : SortedAssets)
		{
			if (AssetData->TagsAndValues.Num() == 0)
			{
				continue;
			}
			++Counter;

			PageBuffer << TEXT("  ") << FCachedAssetKey(AssetData);
			AddLine();

			SortedTagKeys.Reset();
			AssetData->TagsAndValues.ForEach([&SortedTagKeys](const TPair<FName, FAssetTagValueRef>& TagPair)
			{
				SortedTagKeys.Add(TagPair.Key);
			});
			Algo::Sort(SortedTagKeys, FNameLexicalLess());
			for (FName TagKey : SortedTagKeys)
			{
				FAssetTagValueRef Value = AssetData->TagsAndValues.FindTag(TagKey);
				PageBuffer << TEXT("    ") << TagKey << TEXT(" : ") << *Value.AsString();
				AddLine();
			}
		}

		PageBuffer.Appendf(TEXT("--- End AssetTags : %d entries ---"), Counter);
		AddLine();
	}


	if ((bAllFields || Arguments.Contains(TEXT("Dependencies"))) && !bDumpDependencyDetails)
	{
		AddPageBreak();
		PageBuffer.Appendf(TEXT("--- Begin CachedDependsNodes ---"));
		AddLine();

		TArray<FDependsNode*> Nodes;
		CachedDependsNodes.GenerateValueArray(Nodes);
		Nodes.Sort([](const FDependsNode& A, const FDependsNode& B)
			{ return A.GetIdentifier().ToString() < B.GetIdentifier().ToString(); }
			);

		for (const FDependsNode* Node : Nodes)
		{
			PageBuffer.Append(TEXT("	"));
			Node->GetIdentifier().AppendString(PageBuffer);
			PageBuffer.Appendf(TEXT(" : %d connection(s)"), Node->GetConnectionCount());
			AddLine();
		}

		PageBuffer.Appendf(TEXT("--- End CachedDependsNodes : %d entries ---"), CachedDependsNodes.Num());
		AddLine();
	}

	if (bDumpDependencyDetails)
	{
		using namespace UE::AssetRegistry;
		AddPageBreak();
		PageBuffer.Append(TEXT("--- Begin CachedDependsNodes ---"));
		AddLine();

		auto SortByAssetID = [](const FDependsNode& A, const FDependsNode& B)
			{
				return A.GetIdentifier().ToString() < B.GetIdentifier().ToString();
			};
		TArray<FDependsNode*> Nodes;
		CachedDependsNodes.GenerateValueArray(Nodes);
		Nodes.Sort(SortByAssetID);

		if (Arguments.Contains(TEXT("LegacyDependencies"))) // LegacyDependencies are not show by all; they have to be directly requested
		{
			EDependencyCategory CategoryTypes[] =
			{
				EDependencyCategory::Package,
				EDependencyCategory::Package,
				EDependencyCategory::SearchableName,
				EDependencyCategory::Manage,
				EDependencyCategory::Manage,
				EDependencyCategory::None
			};
			EDependencyQuery CategoryQueries[] =
			{
				EDependencyQuery::Hard,
				EDependencyQuery::Soft,
				EDependencyQuery::NoRequirements,
				EDependencyQuery::Direct,
				EDependencyQuery::Indirect,
				EDependencyQuery::NoRequirements
			};
			const TCHAR* CategoryNames[] =
			{
				TEXT("Hard"),
				TEXT("Soft"),
				TEXT("SearchableName"),
				TEXT("HardManage"),
				TEXT("SoftManage"),
				TEXT("References")
			};
			const int NumCategories = UE_ARRAY_COUNT(CategoryTypes);
			check(NumCategories == UE_ARRAY_COUNT(CategoryNames) && NumCategories == UE_ARRAY_COUNT(CategoryQueries));

			TArray<FDependsNode*> Links;
			for (const FDependsNode* Node : Nodes)
			{
				PageBuffer.Append(TEXT("	"));
				Node->GetIdentifier().AppendString(PageBuffer);
				AddLine();
				for (int CategoryIndex = 0; CategoryIndex < NumCategories; ++CategoryIndex)
				{
					EDependencyCategory CategoryType = CategoryTypes[CategoryIndex];
					EDependencyQuery CategoryQuery = CategoryQueries[CategoryIndex];
					const TCHAR* CategoryName = CategoryNames[CategoryIndex];
					Links.Reset();
					if (CategoryType != EDependencyCategory::None)
					{
						Node->GetDependencies(Links, CategoryType, CategoryQuery);
					}
					else
					{
						Node->GetReferencers(Links);
					}
					if (Links.Num() > 0)
					{
						PageBuffer.Appendf(TEXT("		%s"), CategoryName);
						AddLine();
						Links.Sort(SortByAssetID);
						for (FDependsNode* LinkNode : Links)
						{
							PageBuffer.Append(TEXT("			"));
							LinkNode->GetIdentifier().AppendString(PageBuffer);
							AddLine();
						}
					}
				}
			}
		}
		else
		{
			EDependencyCategory CategoryTypes[] =
			{
				EDependencyCategory::Package,
				EDependencyCategory::SearchableName,
				EDependencyCategory::Manage,
				EDependencyCategory::None
			};
			const TCHAR* CategoryNames[] =
			{
				TEXT("Package"),
				TEXT("SearchableName"),
				TEXT("Manage"),
				TEXT("References")
			};
			const int NumCategories = UE_ARRAY_COUNT(CategoryTypes);
			check(NumCategories == UE_ARRAY_COUNT(CategoryNames));

			TArray<FAssetDependency> Dependencies;
			TArray<FDependsNode*> References;
			for (const FDependsNode* Node : Nodes)
			{
				PageBuffer.Append(TEXT("	"));
				Node->GetIdentifier().AppendString(PageBuffer);
				AddLine();
				for (int CategoryIndex = 0; CategoryIndex < NumCategories; ++CategoryIndex)
				{
					EDependencyCategory CategoryType = CategoryTypes[CategoryIndex];
					const TCHAR* CategoryName = CategoryNames[CategoryIndex];
					if (CategoryType != EDependencyCategory::None)
					{
						Dependencies.Reset();
						Node->GetDependencies(Dependencies, CategoryType);
						if (Dependencies.Num() > 0)
						{
							PageBuffer.Appendf(TEXT("		%s"), CategoryName);
							AddLine();
							Dependencies.Sort([](const FAssetDependency& A, const FAssetDependency& B)
								{
									FString AString = A.AssetId.ToString();
									FString BString = B.AssetId.ToString();
									if (AString != BString)
									{
										return AString < BString;
									}
									return A.Properties < B.Properties;
								});
							for (const FAssetDependency& AssetDependency : Dependencies)
							{
								PageBuffer.Append(TEXT("			"));
								AssetDependency.AssetId.AppendString(PageBuffer);
								PageBuffer.Append(TEXT("\t\t{"));
								PropertiesToString(AssetDependency.Properties, PageBuffer, AssetDependency.Category);
								PageBuffer.Append(TEXT("}"));
								AddLine();
							}
						}
					}
					else
					{
						References.Reset();
						Node->GetReferencers(References);
						if (References.Num() > 0)
						{
							PageBuffer.Appendf(TEXT("		%s"), CategoryName);
							AddLine();
							References.Sort(SortByAssetID);
							for (const FDependsNode* Reference : References)
							{
								PageBuffer.Append(TEXT("			"));
								Reference->GetIdentifier().AppendString(PageBuffer);
								AddLine();
							}
						}
					}
				}
			}
		}

		PageBuffer.Appendf(TEXT("--- End CachedDependsNodes : %d entries ---"), CachedDependsNodes.Num());
		AddLine();
	}
	if (bAllFields || Arguments.Contains(TEXT("PackageData")))
	{
		AddPageBreak();
		PageBuffer.Append(TEXT("--- Begin CachedPackageData ---"));
		AddLine();

		TArray<FName> Keys;
		CachedPackageData.GenerateKeyArray(Keys);
		Keys.Sort(FNameLexicalLess());

		for (const FName& Key : Keys)
		{
			const FAssetPackageData* PackageData = CachedPackageData.FindChecked(Key);
			PageBuffer.Append(TEXT("	"));
			Key.AppendString(PageBuffer);
			PageBuffer.Append(TEXT(" : "));
			PageBuffer << PackageData->GetPackageSavedHash();
			PageBuffer.Appendf(TEXT(" : %d bytes"), PackageData->DiskSize);
			AddLine();
		}

		PageBuffer.Appendf(TEXT("--- End CachedPackageData : %d entries ---"), CachedPackageData.Num());
		AddLine();
	}

	if (bAllFields || Arguments.Contains(TEXT("AssetBundles")))
	{
		int32 Counter = 0;
		AddPageBreak();
		PageBuffer.Append(TEXT("--- Begin AssetBundles ---"));
		AddLine();

		InitializeSortedAssets();
		for (const FAssetData* AssetData : SortedAssets)
		{
			if (AssetData->TaggedAssetBundles.IsValid())
			{
				++Counter;
				for (const FAssetBundleEntry& Entry : AssetData->TaggedAssetBundles->Bundles)
				{
					PageBuffer << TEXT("  Owner: ")
						<< FCachedAssetKey(AssetData) << TEXT(" BundleName: ") << Entry.BundleName;
					AddLine();

					for (const FTopLevelAssetPath& Path : Entry.AssetPaths)
					{
						PageBuffer << TEXT("    ") << Path;
						AddLine();
					}
				}
			}
		}

		PageBuffer.Appendf(TEXT("--- End AssetBundles : %d entries ---"), Counter);
		AddLine();
	}

	if (PageBuffer.Len() > 0)
	{
		if (LinesPerPage == 1)
		{
			AddLine();
		}
		else
		{
			FinishPage(true);
		}
	}
}

#endif // ASSET_REGISTRY_STATE_DUMPING_ENABLED


////////////////////////////////////////////////////////////////////////////

#if WITH_DEV_AUTOMATION_TESTS 

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetRegistryAssetPathStringsTest, "System.AssetRegistry.AssetPathStrings",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter);

// Tests that we can produce correct paths for objects represented by FCachedAssetKey and FAssetData
// E.g	PackageName.AssetName
//		PackageName.TopLevel:Inner
//		Packagename.TopLevel:Inner.Innermost
bool FAssetRegistryAssetPathStringsTest::RunTest(const FString& Parameters)
{
	using namespace UE::AssetRegistry::Private;

	// Construct these FNames before creating FCachedAssetKey because the key tries not to create unused path names.
	const FName TopLevelOuter = TEXT("/Path/To/PackageName");
	const FName DirectSubObjectOuter = TEXT("/Path/To/PackageName.OuterName");
	const FName SubSubObjectOuter = TEXT("/Path/To/PackageName.OuterName:SubOuterName");
	const FName AssetName = TEXT("AssetName");

	const FString TopLevelPathString = FString::Printf(TEXT("%s.%s"),
		*TopLevelOuter.ToString(), *AssetName.ToString());
	const FString DirectSubObjectPathString = FString::Printf(TEXT("%s:%s"),
		*DirectSubObjectOuter.ToString(), *AssetName.ToString());
	const FString SubSubObjectPathString = FString::Printf(TEXT("%s.%s"),
		*SubSubObjectOuter.ToString(), *AssetName.ToString());

	const FSoftObjectPath TopLevelPath(TopLevelPathString);
	const FSoftObjectPath DirectSubObjectPath(DirectSubObjectPathString);
	const FSoftObjectPath SubSubObjectPath(SubSubObjectPathString);

	TestEqual(TEXT("SoftObjectPath::ToString() correct for top-level asset"),
		TopLevelPath.ToString(), TopLevelPathString);
	TestEqual(TEXT("SoftObjectPath::ToString() correct for subobject asset"),
		DirectSubObjectPath.ToString(), DirectSubObjectPathString);
	TestEqual(TEXT("SoftObjectPath::ToString() correct for sub-subobject asset"),
		SubSubObjectPath.ToString(), SubSubObjectPathString);

	// Construct FCachedAssetKey from FSoftObjectPath of various lengths + check they have the right components
	const FCachedAssetKey TopLevelAssetKey(TopLevelPath);
	TestEqual(TEXT("FCachedAssetKey::OuterPath correct for top-level asset"),
		TopLevelAssetKey.OuterPath.ToString(), TopLevelOuter.ToString());
	TestEqual(TEXT("FCachedAssetKey::ObjectName correct for top-level asset"),
		TopLevelAssetKey.ObjectName.ToString(), AssetName.ToString());

	const FCachedAssetKey DirectSubObjectKey(DirectSubObjectPath);
	TestEqual(TEXT("FCachedAssetKey::OuterPath correct for subobject asset"),
		DirectSubObjectKey.OuterPath.ToString(), DirectSubObjectOuter.ToString());
	TestEqual(TEXT("FCachedAssetKey::ObjectName correct for subobject asset"),
		DirectSubObjectKey.ObjectName.ToString(), AssetName.ToString());

	const FCachedAssetKey SubSubObjectKey(SubSubObjectPath);
	TestEqual(TEXT("FCachedAssetKey::OuterPath correct for sub-subobject asset"),
		SubSubObjectKey.OuterPath.ToString(), SubSubObjectOuter.ToString());
	TestEqual(TEXT("FCachedAssetKey::ObjectName correct for sub-subobject asset"),
		SubSubObjectKey.ObjectName.ToString(), AssetName.ToString());

	// Construct FCachedAssetKey from FSoftObjectPath of various lengths + check they give the right strings from AppendString
	TestEqual(TEXT("FCachedAssetKey::ToString() correct for top-level asset"),
		TopLevelAssetKey.ToString(), TopLevelPathString);
	TestEqual(TEXT("FCachedAssetKey::ToString() correct for subobject asset"),
		DirectSubObjectKey.ToString(), DirectSubObjectPathString);
	TestEqual(TEXT("FCachedAssetKey::ToString() correct for sub-subobject asset"),
		SubSubObjectKey.ToString(), SubSubObjectPathString);

	auto PathToAssetData = [](const FString& Path)
	{
		FString PackageName = FPackageName::ObjectPathToPackageName(Path);
		return FAssetData(PackageName, Path, FTopLevelAssetPath("/Script/CoreUObject.Object"));
	};

	FAssetData TopLevelAssetData = PathToAssetData(TopLevelPathString);
	FAssetData DirectSubObjectAssetData = PathToAssetData(DirectSubObjectPathString);
	FAssetData SubSubObjectAssetData = PathToAssetData(SubSubObjectPathString);

	// Test FAssetData::AppendPath for asset data with variable length of OptionalOuterPath
	TestEqual(TEXT("FAssetData::AppendPath() correct for top-level asset"),
		TopLevelAssetData.GetObjectPathString(), TopLevelPathString);

#if WITH_EDITORONLY_DATA
	// These tests are only enabled when WITH_EDITORONLY_DATA is active because only
	// then OuterPath is retained by FAssedData (see FAssetData::AppendObjectPath).
	TestEqual(TEXT("FAssetData::AppendPath() correct for subobject asset"),
		DirectSubObjectAssetData.GetObjectPathString(), DirectSubObjectPathString);
	TestEqual(TEXT("FAssetData::AppendPath() correct for sub-subobject asset"),
		SubSubObjectAssetData.GetObjectPathString(), SubSubObjectPathString);
#endif
	
	return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
