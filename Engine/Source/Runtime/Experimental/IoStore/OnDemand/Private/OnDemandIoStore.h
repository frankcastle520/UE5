// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Algo/BinarySearch.h"
#include "Async/Async.h"
#include "Async/Mutex.h"
#include "Containers/AnsiString.h"
#include "Containers/BitArray.h"
#include "IO/IoBuffer.h"
#include "IO/IoStoreOnDemand.h"
#include "IO/IoHash.h"
#include "IO/IoChunkEncoding.h"
#include "IO/IoChunkId.h"
#include "Misc/AES.h"
#include "Misc/EnumClassFlags.h"

#include <atomic>

enum class EForkProcessRole : uint8;

struct FIoContainerHeader;
using FSharedContainerHeader		= TSharedPtr<FIoContainerHeader>;

namespace UE::IoStore
{

class IOnDemandPackageStoreBackend;
class IOnDemandInstallCache;
using FSharedPackageStoreBackend	= TSharedPtr<IOnDemandPackageStoreBackend>;
using FSharedInstallCache			= TSharedPtr<IOnDemandInstallCache>;
using FWeakOnDemandIoStore			= TWeakPtr<FOnDemandIoStore, ESPMode::ThreadSafe>;

///////////////////////////////////////////////////////////////////////////////
class FOnDemandInternalContentHandle
{
public:
	FOnDemandInternalContentHandle()
		: DebugName(TEXT("NoName"))
	{ }
	FOnDemandInternalContentHandle(FSharedString InDebugName)
		: DebugName(InDebugName)
	{ }
	~FOnDemandInternalContentHandle();

	UPTRINT HandleId() const { return UPTRINT(this); }

	FSharedString			DebugName;
	FWeakOnDemandIoStore	IoStore;
};

FString LexToString(const FOnDemandInternalContentHandle& Handle);

///////////////////////////////////////////////////////////////////////////////
enum class EOnDemandContainerFlags : uint8
{
	None					= 0,
	PendingEncryptionKey	= (1 << 0),
	Mounted					= (1 << 1),
	StreamOnDemand			= (1 << 2),
	InstallOnDemand			= (1 << 3),
	Encrypted				= (1 << 4),
	Count
};
ENUM_CLASS_FLAGS(EOnDemandContainerFlags);

void LexToString(EOnDemandContainerFlags Flags, FStringBuilderBase& Out);
FString LexToString(EOnDemandContainerFlags Flags);

///////////////////////////////////////////////////////////////////////////////
struct FOnDemandChunkEntry
{
	static const FOnDemandChunkEntry Null;

	FIoHash	Hash;
	uint32	RawSize = 0;
	uint32	EncodedSize = 0;
	uint32	BlockOffset = ~uint32(0);
	uint32	BlockCount = 0;
	uint8	CompressionFormatIndex = 0;
};
static_assert(sizeof(FOnDemandChunkEntry) == 40);

///////////////////////////////////////////////////////////////////////////////
struct FOnDemandTagSet
{
	FString			Tag;
	TArray<uint32>	PackageIndicies;
};

///////////////////////////////////////////////////////////////////////////////
struct FOnDemandChunkEntryReferences
{
	UPTRINT ContentHandleId = 0;	
	TBitArray<>	Indices;
};

///////////////////////////////////////////////////////////////////////////////
struct FOnDemandContainer
{
	FString									UniqueName() const;
	inline int32							FindChunkEntryIndex(const FIoChunkId& ChunkId) const;
	inline const FOnDemandChunkEntry*		FindChunkEntry(const FIoChunkId& ChunkId) const;
	inline FOnDemandChunkEntry*				FindChunkEntry(const FIoChunkId& ChunkId);
	inline FOnDemandChunkEntryReferences&	FindOrAddChunkEntryReferences(const FOnDemandInternalContentHandle& ContentHandle);
	inline TBitArray<>						GetReferencedChunkEntries() const;

	FAES::FAESKey							EncryptionKey;
	FSharedContainerHeader					Header;
	FString									EncryptionKeyGuid;
	FString									Name;
	FString									MountId;
	FAnsiString								ChunksDirectory;
	TArray<FName>							CompressionFormats;
	TArray<uint32>							BlockSizes;
	TArray<FIoBlockHash>					BlockHashes;
	TArray<FOnDemandTagSet>					TagSets;
	TUniquePtr<uint8[]>						ChunkEntryData;
	TArrayView<FIoChunkId>					ChunkIds;
	TArrayView<FOnDemandChunkEntry> 		ChunkEntries;
	TArray<FOnDemandChunkEntryReferences>	ChunkEntryReferences;
	FIoContainerId							ContainerId;
	uint32									BlockSize = 0;
	EOnDemandContainerFlags 				Flags = EOnDemandContainerFlags::None;
};

using FSharedOnDemandContainer = TSharedPtr<FOnDemandContainer, ESPMode::ThreadSafe>;

int32 FOnDemandContainer::FindChunkEntryIndex(const FIoChunkId& ChunkId) const
{
	if (const int32 Index = Algo::LowerBound(ChunkIds, ChunkId); Index < ChunkIds.Num())
	{
		if (ChunkIds[Index] == ChunkId)
		{
			return Index; 
		}
	}

	return INDEX_NONE;
}

const FOnDemandChunkEntry* FOnDemandContainer::FindChunkEntry(const FIoChunkId& ChunkId) const
{
	if (int32 Index = FindChunkEntryIndex(ChunkId); Index != INDEX_NONE)
	{
		return &ChunkEntries[Index];
	}

	return nullptr;
}

FOnDemandChunkEntry* FOnDemandContainer::FindChunkEntry(const FIoChunkId& ChunkId)
{
	if (int32 Index = FindChunkEntryIndex(ChunkId); Index != INDEX_NONE)
	{
		return &ChunkEntries[Index];
	}

	return nullptr;
}

FOnDemandChunkEntryReferences& FOnDemandContainer::FindOrAddChunkEntryReferences(const FOnDemandInternalContentHandle& ContentHandle)
{
	const UPTRINT ContentHandleId = ContentHandle.HandleId(); 
	for (FOnDemandChunkEntryReferences& Refs : ChunkEntryReferences)
	{
		if (Refs.ContentHandleId == ContentHandleId)
		{
			return Refs;
		}
	}

	FOnDemandChunkEntryReferences& NewRef = ChunkEntryReferences.AddDefaulted_GetRef();
	NewRef.ContentHandleId = ContentHandleId;
	NewRef.Indices.SetNum(ChunkEntries.Num(), false);
	return NewRef;
}

TBitArray<> FOnDemandContainer::GetReferencedChunkEntries() const
{
	TBitArray<> Indices;
	for (const FOnDemandChunkEntryReferences& Refs : ChunkEntryReferences)
	{
		check(Refs.Indices.Num() == ChunkEntries.Num());
		Indices.CombineWithBitwiseOR(Refs.Indices, EBitwiseOperatorFlags::MaxSize);
	}

	return Indices;
}

///////////////////////////////////////////////////////////////////////////////
struct FOnDemandChunkInfo
{
	FOnDemandChunkInfo()
		: Entry(FOnDemandChunkEntry::Null)
	{ }

	const									FIoHash& Hash() const { return Entry.Hash; }
	uint32									RawSize() const { return Entry.RawSize; }
	uint32									EncodedSize() const { return Entry.EncodedSize; }
	uint32									BlockSize() const { return SharedContainer->BlockSize; }
	FName									CompressionFormat() const { return SharedContainer->CompressionFormats[Entry.CompressionFormatIndex]; }
	FMemoryView								EncryptionKey() const { return FMemoryView(SharedContainer->EncryptionKey.Key, FAES::FAESKey::KeySize); }
	inline TConstArrayView<uint32>			Blocks() const;
	inline TConstArrayView<FIoBlockHash>	BlockHashes() const;
	FAnsiStringView							ChunksDirectory() const { return SharedContainer->ChunksDirectory; }
	const FOnDemandChunkEntry&				ChunkEntry() const { return Entry; }

	bool									IsValid() const { return SharedContainer.IsValid(); }
	operator								bool() const { return IsValid(); }
	
	inline static FOnDemandChunkInfo		Find(FSharedOnDemandContainer Container, const FIoChunkId& ChunkId);

private:
	friend class FOnDemandIoStore;

	FOnDemandChunkInfo(FSharedOnDemandContainer InContainer, const FOnDemandChunkEntry& InEntry)
		: SharedContainer(InContainer)
		, Entry(InEntry)
	{ }

	FSharedOnDemandContainer	SharedContainer;
	const FOnDemandChunkEntry&	Entry;
};

TConstArrayView<uint32> FOnDemandChunkInfo::Blocks() const
{
	return TConstArrayView<uint32>(SharedContainer->BlockSizes.GetData() + Entry.BlockOffset, Entry.BlockCount);
}

TConstArrayView<FIoBlockHash> FOnDemandChunkInfo::BlockHashes() const
{
	return SharedContainer->BlockHashes.IsEmpty()
		? TConstArrayView<FIoBlockHash>()
		: TConstArrayView<FIoBlockHash>(SharedContainer->BlockHashes.GetData() + Entry.BlockOffset, Entry.BlockCount);
}

FOnDemandChunkInfo FOnDemandChunkInfo::Find(FSharedOnDemandContainer Container, const FIoChunkId& ChunkId)
{
	check(Container.IsValid());
	if (const FOnDemandChunkEntry* Entry = Container->FindChunkEntry(ChunkId))
	{
		return FOnDemandChunkInfo(Container, *Entry);
	}

	return FOnDemandChunkInfo();
}

///////////////////////////////////////////////////////////////////////////////
class FOnDemandIoStore
	: public TSharedFromThis<FOnDemandIoStore, ESPMode::ThreadSafe>
{
	struct FMountRequest
	{
		FOnDemandMountArgs		Args;
		FOnDemandMountCompleted	OnCompleted;
		double					DurationInSeconds = 0.0;
	};

	using FSharedMountRequest	= TSharedRef<FMountRequest>;

	struct FInstallRequest
	{
		FOnDemandInstallArgs				Args;
		FOnDemandInstallCompleted			OnCompleted;
		FOnDemandInstallProgressed			OnProgressed;
		const FOnDemandCancellationToken*	CancellationToken = nullptr;
	};

	using FSharedInstallRequest	= TSharedRef<FInstallRequest>;

	struct FPurgeRequest
	{
		FOnDemandPurgeArgs					Args;
		FOnDemandPurgeCompleted				OnCompleted;
	};

	using FSharedPurgeRequest = TSharedRef<FPurgeRequest>;

public:
	FOnDemandIoStore();
	~FOnDemandIoStore();
	FOnDemandIoStore(const FOnDemandIoStore&) = delete;
	FOnDemandIoStore(FOnDemandIoStore&&) = delete;
	FOnDemandIoStore& operator=(const FOnDemandIoStore&) = delete;
	FOnDemandIoStore& operator=(FOnDemandIoStore&&) = delete;

	FIoStatus				Initialize();
	void					Mount(FOnDemandMountArgs&& Args, FOnDemandMountCompleted&& OnCompleted);
	void					Install(
								FOnDemandInstallArgs&& Args,
								FOnDemandInstallCompleted&& OnCompleted,
								FOnDemandInstallProgressed&& OnProgress = nullptr,
								const FOnDemandCancellationToken* CancellationToken = nullptr);
	void					Purge(FOnDemandPurgeArgs&& Args, FOnDemandPurgeCompleted&& OnCompleted);
	FIoStatus				Unmount(FStringView MountId);
	TIoStatusOr<uint64>		GetInstallSize(const FOnDemandGetInstallSizeArgs& Args) const;
	FIoStatus				GetInstallSizesByMountId(const FOnDemandGetInstallSizeArgs& Args, TMap<FString, uint64>& OutSizesByMountId) const;
	FOnDemandChunkInfo		GetStreamingChunkInfo(const FIoChunkId& ChunkId);
	FOnDemandChunkInfo		GetInstalledChunkInfo(const FIoChunkId& ChunkId);
	void					ReleaseContent(FOnDemandInternalContentHandle& ContentHandle);
	void					GetReferencedContent(TArray<FSharedOnDemandContainer>& OutContainers, TArray<TBitArray<>>& OutChunkEntryIndices);
	FOnDemandCacheUsage		GetCacheUsage() const;

private:
	FIoStatus				GetContainersForInstall(
								FStringView MountId,
								TSet<FSharedOnDemandContainer>& OutContainersForInstallation,
								TSet<FSharedOnDemandContainer>& OutContainersWithMountId) const;
	FIoStatus				GetContainersAndPackagesForInstall(
								FStringView MountId,
								const TArray<FString>& TagSets,
								const TArray<FPackageId>& PackageIds,
								TSet<FSharedOnDemandContainer>& OutContainersForInstallation,
								TSet<FPackageId>& OutPackageIdsToInstall) const;
	void					OnPostFork(EForkProcessRole ProcessRole);
	FIoStatus				InitializeOnDemandInstallCache();
	FOnDemandChunkInfo		GetChunkInfo(const FIoChunkId& ChunkId, EOnDemandContainerFlags ContainerFlags);
	void					TryEnterTickLoop();
	void					TickLoop();
	bool					Tick();
	FIoStatus				TickMountRequest(FMountRequest& MountRequest);
	void					CompleteMountRequest(FMountRequest& MountRequest, FOnDemandMountResult&& MountResult);
	FOnDemandInstallResult	TickInstallRequest(const FInstallRequest& InstallRequest);
	void					CompleteInstallRequest(FInstallRequest& InstallRequest, FOnDemandInstallResult&& InstallResult);
	void					ProgressInstallRequest(const FInstallRequest& InstallRequest, const FOnDemandInstallProgress& Progress);
	void					CompletePurgeRequest(FPurgeRequest& PurgeRequest, FOnDemandPurgeResult&& Result);
	void					OnEncryptionKeyAdded(const FGuid& Id, const FAES::FAESKey& Key);
	static void				CreateContainersFromToc(
								FStringView MountId,
								FStringView TocPath,
								FOnDemandToc& Toc,
								TArray<FSharedOnDemandContainer>& Out);

	FSharedInstallCache					InstallCache;
	FSharedPackageStoreBackend			PackageStoreBackend;
	FDelegateHandle						OnMountPakHandle;
	TArray<FSharedOnDemandContainer>	Containers;
	TMap<FString, FIoBuffer>			PendingContainerHeaders;
	mutable UE::FMutex					ContainerMutex;

	TArray<FSharedMountRequest>			MountRequests;
	TArray<FSharedInstallRequest>		InstallRequests;
	TArray<FSharedPurgeRequest>			PurgeRequests;
	UE::FMutex							MountRequestMutex;

	bool								bTicking = false;
	bool								bTickRequested = false;
	TFuture<void>						TickFuture;
};

} // namespace UE::IoStore
