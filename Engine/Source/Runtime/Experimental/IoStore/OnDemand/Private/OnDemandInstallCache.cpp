// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnDemandInstallCache.h"
#include "OnDemandHttpClient.h"
#include "OnDemandIoStore.h"

#include "Algo/Accumulate.h"
#include "Async/Mutex.h"
#include "Async/UniqueLock.h"
#include "Async/AsyncFileHandle.h"
#include "Containers/UnrealString.h"
#include "GenericHash.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "IO/IoContainerHeader.h"
#include "IO/IoChunkId.h"
#include "IO/IoChunkEncoding.h"
#include "Misc/DateTime.h"
#include "Misc/PathViews.h"
#include "Misc/ScopeExit.h"
#include "Misc/StringBuilder.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Tasks/Task.h"

namespace UE::IoStore
{

///////////////////////////////////////////////////////////////////////////////
namespace CVars
{

bool bForceSyncIO = false;
static FAutoConsoleVariableRef CVarForceSyncIO(
	TEXT("IoStore.OnDemand.ForceSyncIO"),
	bForceSyncIO,
	TEXT("Whether to force using synchronous file reads even if cache block is immutable"),
	ECVF_ReadOnly
);

}

///////////////////////////////////////////////////////////////////////////////
double ToKiB(uint64 Value)
{
	return double(Value) / 1024.0;
}

///////////////////////////////////////////////////////////////////////////////
double ToMiB(uint64 Value)
{
	return double(Value) / 1024.0 / 1024.0;
}

///////////////////////////////////////////////////////////////////////////////
using FSharedAsyncFileHandle	= TSharedPtr<IAsyncReadFileHandle, ESPMode::ThreadSafe>;
using FWeakAsyncFileHandle		= TWeakPtr<IAsyncReadFileHandle, ESPMode::ThreadSafe>;
using FUniqueFileHandle			= TUniquePtr<IFileHandle>;
using FCasAddr					= FHash96;

///////////////////////////////////////////////////////////////////////////////
struct FCasBlockId
{
	FCasBlockId() = default;
	explicit FCasBlockId(uint32 InId)
		: Id(InId) { }

	bool IsValid() const { return Id != 0; }

	friend inline bool operator==(FCasBlockId LHS, FCasBlockId RHS)
	{
		return LHS.Id == RHS.Id;
	}

	friend inline uint32 GetTypeHash(FCasBlockId BlockId)
	{
		return GetTypeHash(BlockId.Id);
	}

	static const FCasBlockId Invalid;

	uint32 Id = 0;
};

const FCasBlockId FCasBlockId::Invalid = FCasBlockId();

///////////////////////////////////////////////////////////////////////////////
struct FCasLocation
{
	bool IsValid() const { return BlockId.IsValid() && BlockOffset != MAX_uint32; }

	static const FCasLocation Invalid;

	FCasBlockId	BlockId;
	uint32		BlockOffset = MAX_uint32; 
};

const FCasLocation FCasLocation::Invalid = FCasLocation();

///////////////////////////////////////////////////////////////////////////////
struct FCasBlockInfo
{
	uint64	FileSize = 0;
	int64	LastAccess = 0;
	uint32	RefCount = 0;
};

using FCasBlockInfoMap = TMap<FCasBlockId, FCasBlockInfo>;

///////////////////////////////////////////////////////////////////////////////
struct FCas
{
	using FLookup			= TMap<FCasAddr, FCasLocation>;
	using FReadHandles		= TMap<FCasBlockId, FWeakAsyncFileHandle>;
	using FLastAccess		= TMap<FCasBlockId, int64>;

	FIoStatus				Initialize(FStringView Directory);
	FCasLocation			FindChunk(const FIoHash& Hash, bool& bIsLocationInCurrentBlock) const;
	FCasLocation			FindChunk(const FIoHash& Hash) const;
	FCasBlockId				CreateBlock();
	FIoStatus				DeleteBlock(FCasBlockId BlockId, TArray<FCasAddr>& OutAddrs);
	FString					GetBlockFilename(FCasBlockId BlockId) const;
	FFileOpenResult 		OpenRead(FCasBlockId BlockId);
	FSharedAsyncFileHandle	OpenAsyncRead(FCasBlockId  BlockId);
	FUniqueFileHandle		OpenWrite(FCasBlockId BlockId);
	void					TrackAccess(FCasBlockId BlockId, int64 UtcTicks);
	void					TrackAccess(FCasBlockId BlockId) { TrackAccess(BlockId, FDateTime::UtcNow().GetTicks()); }
	uint64					GetBlockInfo(FCasBlockInfoMap& OutBlockInfo);
	void					Compact();
	FIoStatus				Verify(TArray<FCasAddr>& OutAddrs);

	FStringView			RootDirectory;
	FLookup				Lookup;
	TSet<FCasBlockId>	BlockIds;
	FLastAccess			LastAccess;
	FReadHandles		ReadHandles;	
	uint32				MaxBlockSize = 32 << 20; //TODO: Make configurable
	FCasBlockId			CurrentBlock;
	mutable UE::FMutex	Mutex;
};

///////////////////////////////////////////////////////////////////////////////
FIoStatus FCas::Initialize(FStringView Directory)
{
	RootDirectory = Directory;

	Lookup.Empty();
	BlockIds.Empty();
	LastAccess.Empty();
	CurrentBlock = FCasBlockId::Invalid;

	TStringBuilder<256> Path;
	FPathViews::Append(Path, RootDirectory, TEXT("blocks"));

	IFileManager& Ifm = IFileManager::Get();
	if (Ifm.DirectoryExists(Path.ToString()) == false)
	{
		const bool bTree = true;
		if (Ifm.MakeDirectory(Path.ToString(), bTree) == false)
		{
			FIoStatus Status = FIoStatusBuilder(EIoErrorCode::WriteError)
				<< TEXT("Failed to create directory '")
				<< Path.ToString()
				<< TEXT("'");
			return Status;
		}
	}

	return EIoErrorCode::Ok;
};

FCasLocation FCas::FindChunk(const FIoHash& Hash, bool& bIsLocationInCurrentBlock) const
{
	const FCasAddr* Addr	= reinterpret_cast<const FCasAddr*>(&Hash);
	const uint32 TypeHash	= GetTypeHash(*Addr);
	{
		UE::TUniqueLock Lock(Mutex);
		if (const FCasLocation* Loc = Lookup.FindByHash(TypeHash, *Addr))
		{
			bIsLocationInCurrentBlock = Loc->BlockId == CurrentBlock;
			return *Loc;
		}
	}

	return FCasLocation{};
}

FCasLocation FCas::FindChunk(const FIoHash& Hash) const
{
	bool bTmp = false;
	return FindChunk(Hash, bTmp);
}

FCasBlockId FCas::CreateBlock()
{
	IPlatformFile&	Ipf = FPlatformFileManager::Get().GetPlatformFile();
	FCasBlockId		Out = FCasBlockId::Invalid;

	UE::TUniqueLock Lock(Mutex);

	for (uint32 Id = 1; Id < MAX_uint32 && !Out.IsValid(); Id++)
	{
		const FCasBlockId BlockId(Id);
		if (BlockIds.Contains(BlockId))
		{
			continue;
		}

		const FString Filename = GetBlockFilename(BlockId);
		if (Ipf.FileExists(*Filename))
		{
			UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Unused CAS block id %u already exists on disk"), BlockId.Id);
			continue;
		}

		BlockIds.Add(BlockId);
		LastAccess.FindOrAdd(BlockId, FDateTime::UtcNow().GetTicks());
		Out = BlockId;
	}

	return Out;
}

FIoStatus FCas::DeleteBlock(FCasBlockId BlockId, TArray<FCasAddr>& OutAddrs)
{
	UE::TUniqueLock Lock(Mutex);

	IPlatformFile&	Ipf = FPlatformFileManager::Get().GetPlatformFile();
	const FString	Filename = GetBlockFilename(BlockId);

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Deleting CAS block '%s'"), *Filename);
	if (Ipf.DeleteFile(*Filename) == false)
	{
		return FIoStatusBuilder(EIoErrorCode::WriteError)
			<< TEXT("Failed to delete CAS block '")
			<< Filename
			<< TEXT("'");
	}

	BlockIds.Remove(BlockId);
	for (auto It = Lookup.CreateIterator(); It; ++It)
	{
		if (It->Value.BlockId == BlockId)
		{
			OutAddrs.Add(It->Key);
			It.RemoveCurrent();
		}
	}

	return FIoStatus::Ok;
}

FString FCas::GetBlockFilename(FCasBlockId BlockId) const
{
	check(BlockId.IsValid());
	const uint32 Id = NETWORK_ORDER32(BlockId.Id);
	FString Hex;
	BytesToHexLower(reinterpret_cast<const uint8*>(&Id), sizeof(int32), Hex);
	TStringBuilder<256> Path;
	FPathViews::Append(Path, RootDirectory, TEXT("blocks"), Hex);
	Path << TEXT(".ucas");

	return FString(Path.ToView());
}

FFileOpenResult FCas::OpenRead(FCasBlockId BlockId)
{
	const FString	Filename = GetBlockFilename(BlockId);
	IPlatformFile&	Ipf = FPlatformFileManager::Get().GetPlatformFile();

	return Ipf.OpenRead(*Filename, IPlatformFile::EOpenReadFlags::AllowWrite);
}

FSharedAsyncFileHandle FCas::OpenAsyncRead(FCasBlockId BlockId)
{
	UE::TUniqueLock Lock(Mutex);

	if (FWeakAsyncFileHandle* MaybeHandle = ReadHandles.Find(BlockId))
	{
		if (FSharedAsyncFileHandle Handle = MaybeHandle->Pin(); Handle.IsValid())
		{
			return Handle;
		}
	}

	IPlatformFile&			Ipf = FPlatformFileManager::Get().GetPlatformFile();
	const FString			Filename = GetBlockFilename(BlockId);
	FSharedAsyncFileHandle	NewHandle(Ipf.OpenAsyncRead(*Filename));

	if (NewHandle.IsValid())
	{
		ReadHandles.FindOrAdd(BlockId, NewHandle);
	}

	return NewHandle;
}

FUniqueFileHandle FCas::OpenWrite(FCasBlockId BlockId)
{
	IPlatformFile&	Ipf = FPlatformFileManager::Get().GetPlatformFile();
	const FString	Filename = GetBlockFilename(BlockId);
	const bool		bAppend = true;
	const bool		bAllowRead = true;

	return FUniqueFileHandle(Ipf.OpenWrite(*Filename, bAppend, bAllowRead));
}

void FCas::TrackAccess(FCasBlockId BlockId, int64 UtcTicks)
{
	check(BlockId.IsValid());
	UE::TUniqueLock Lock(Mutex);
	LastAccess.FindOrAdd(BlockId, UtcTicks);
}

uint64 FCas::GetBlockInfo(FCasBlockInfoMap& OutBlockInfo)
{
	TStringBuilder<256> Path;
	FPathViews::Append(Path, RootDirectory, TEXT("blocks"));

	struct FDirectoryVisitor final
		: public IPlatformFile::FDirectoryVisitor
	{
		FDirectoryVisitor(IPlatformFile& PlatformFile, FCasBlockInfoMap& InBlockInfo, FLastAccess&& Access)
			: Ipf(PlatformFile)
			, BlockInfo(InBlockInfo)
			, LastAccess(MoveTemp(Access))
		{ }
		
		virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
		{
			if (bIsDirectory)
			{
				return true;
			}

			const FStringView Filename(FilenameOrDirectory);
			if (FPathViews::GetExtension(Filename) == TEXTVIEW("ucas") == false)
			{
				return true;
			}

			const int64			FileSize = Ipf.FileSize(FilenameOrDirectory);
			const FStringView	IndexHex = FPathViews::GetBaseFilename(Filename);
			const FCasBlockId	BlockId(FParse::HexNumber(WriteToString<128>(IndexHex).ToString()));

			if (BlockId.IsValid() == false || FileSize < 0)
			{
				UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Found invalid CAS block '%s', FileSize=%lld"),
					FilenameOrDirectory, FileSize);
				return true;
			}

			if (BlockInfo.Contains(BlockId))
			{
				UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Found duplicate CAS block '%s'"), FilenameOrDirectory);
				return true;
			}

			const int64* UtcTicks = LastAccess.Find(BlockId);

			BlockInfo.Add(BlockId, FCasBlockInfo
			{
				.FileSize = uint64(FileSize),
				.LastAccess = UtcTicks != nullptr ? *UtcTicks : 0
			});
			TotalSize += uint64(FileSize);

			return true;
		}

		IPlatformFile&		Ipf;
		FCasBlockInfoMap&	BlockInfo;
		FLastAccess			LastAccess;
		uint64				TotalSize = 0;
	};

	FLastAccess Access;
	{
		TUniqueLock Lock(Mutex);
		Access = LastAccess;
	}
	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();
	FDirectoryVisitor Visitor(Ipf, OutBlockInfo, MoveTemp(Access));
	Ipf.IterateDirectory(Path.ToString(), Visitor);

	return Visitor.TotalSize;
}

void FCas::Compact()
{
	UE::TUniqueLock Lock(Mutex);
	Lookup.Compact();
	BlockIds.Compact();
	ReadHandles.Compact();
	LastAccess.Compact();
}

FIoStatus FCas::Verify(TArray<FCasAddr>& OutAddrs)
{
	FCasBlockInfoMap	BlockInfo;
	const uint64		TotalSize = GetBlockInfo(BlockInfo);
	uint64				TotalVerifiedBytes = 0;
	FIoStatus			Status = FIoStatus::Ok;

	for (auto BlockIt = BlockIds.CreateIterator(); BlockIt; ++BlockIt)
	{
		const FCasBlockId BlockId = *BlockIt;
		if (const FCasBlockInfo* Info = BlockInfo.Find(BlockId))
		{
			TotalVerifiedBytes += Info->FileSize;
			continue;
		}

		const FString Filename = GetBlockFilename(BlockId);
		UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Missing CAS block '%s'"), *Filename);

		LastAccess.Remove(BlockId);
		BlockIt.RemoveCurrent();
		Status = EIoErrorCode::NotFound;
	}

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Verified %d CAS blocks of total %.2lf MiB"),
		BlockIds.Num(), ToMiB(TotalVerifiedBytes));

	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();
	for (const TPair<FCasBlockId, FCasBlockInfo>& Kv : BlockInfo)
	{
		const FCasBlockId BlockId = Kv.Key;
		if (BlockIds.Contains(BlockId))
		{
			continue;
		}

		const FString Filename = GetBlockFilename(BlockId);
		if (Ipf.DeleteFile(*Filename))
		{
			UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Deleted orphaned CAS block '%s'"), *Filename);
		}
	}

	TSet<FString> MissingReferencedBlocks;
	for (auto It = Lookup.CreateIterator(); It; ++It)
	{
		if (!BlockIds.Contains(It->Value.BlockId))
		{
			MissingReferencedBlocks.Add(GetBlockFilename(It->Value.BlockId));
			
			OutAddrs.Add(It->Key);
			It.RemoveCurrent();

			Status = EIoErrorCode::NotFound;
		}
	}

	for (const FString& Filename : MissingReferencedBlocks)
	{
		UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Lookup references missing CAS block '%s'"), *Filename);
	}

	return Status; 
}

///////////////////////////////////////////////////////////////////////////////
struct FCasJournal
{
	enum class EVersion : uint32
	{
		Invalid	= 0,
		Initial,

		LatestPlusOne,
		Latest = LatestPlusOne - 1
	};

	struct FHeader
	{
		static const inline uint8 MagicSequence[16] = {'C', 'A', 'S', 'J', 'O', 'U', 'R', 'N', 'A', 'L', 'H', 'E', 'A', 'D', 'E', 'R'};

		bool		IsValid() const;

		uint8		Magic[16] = {0};
		EVersion	Version = EVersion::Invalid;
		uint8		Pad[12] = {0};
	};
	static_assert(sizeof(FHeader) == 32);

	struct FFooter
	{
		static const inline uint8 MagicSequence[16] = {'C', 'A', 'S', 'J', 'O', 'U', 'R', 'N', 'A', 'L', 'F', 'O', 'O', 'T', 'E', 'R'};

		bool IsValid() const;

		uint8 Magic[16] = {0};
	};
	static_assert(sizeof(FFooter) == 16);

	struct FEntry
	{
		enum class EType : uint8
		{
			None = 0,
			ChunkLocation,
			BlockCreated,
			BlockDeleted,
			BlockAccess
		};

		struct FChunkLocation
		{
			EType			Type = EType::ChunkLocation;
			uint8			Pad[3]= {0};
			FCasLocation	CasLocation;
			FCasAddr		CasAddr;
		};
		static_assert(sizeof(FChunkLocation) == 24);

		struct FBlockOperation
		{
			EType		Type = EType::None;
			uint8		Pad[3]= {0};
			FCasBlockId	BlockId;
			int64		UtcTicks = 0;
			uint8		Pad1[8]= {0};
		};
		static_assert(sizeof(FBlockOperation) == 24);

		union
		{
			FChunkLocation	ChunkLocation;
			FBlockOperation	BlockOperation;
		};

		EType Type() const { return *reinterpret_cast<const EType*>(this); }
	};
	static_assert(sizeof(FEntry) == 24);

	struct FTransaction
	{
		void			ChunkLocation(const FCasLocation& Location, const FCasAddr& Addr);
		void			BlockCreated(FCasBlockId BlockId);
		void			BlockDeleted(FCasBlockId BlockId);
		void			BlockAccess(FCasBlockId BlockId, int64 UtcTicks);

		FString			JournalFile;
		TArray<FEntry>	Entries;
	};

	using FEntryHandler		= TFunction<void(const FEntry&)>;

	static FIoStatus		Replay(const FString& JournalFile, FEntryHandler&& Handler);
	static FIoStatus		Create(const FString& JournalFile);
	static FTransaction		Begin(FString&& JournalFile);
	static FIoStatus		Commit(FTransaction&& Transaction);
};

///////////////////////////////////////////////////////////////////////////////
bool FCasJournal::FHeader::IsValid() const
{
	if (FMemory::Memcmp(&Magic, &FHeader::MagicSequence, sizeof(FHeader::MagicSequence)) != 0)
	{
		return false;
	}

	if (static_cast<uint32>(Version) > static_cast<uint32>(EVersion::Latest))
	{
		return false;
	}

	return true;
}

bool FCasJournal::FFooter::IsValid() const
{
	return FMemory::Memcmp(Magic, FFooter::MagicSequence, sizeof(FFooter::MagicSequence)) == 0;
}

FIoStatus FCasJournal::Replay(const FString& JournalFile, FEntryHandler&& Handler)
{
	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();

	if (Ipf.FileExists(*JournalFile) == false)
	{
		return EIoErrorCode::NotFound;
	}

	TUniquePtr<IFileHandle> FileHandle(Ipf.OpenRead(*JournalFile));
	if (FileHandle.IsValid() == false)
	{
		return EIoErrorCode::FileNotOpen;
	}

	FHeader Header;
	if ((FileHandle->Read(reinterpret_cast<uint8*>(&Header), sizeof(FHeader)) == false) || (Header.IsValid() == false))
	{
		return FIoStatusBuilder(EIoErrorCode::ReadError)
			<< TEXT("Failed to validate journal header '")
			<< JournalFile
			<< TEXT("'");
	}

	const int64 FileSize	= FileHandle->Size();
	const int64 EntryCount	= (FileSize - sizeof(FHeader) - sizeof(FFooter)) / sizeof(FEntry);

	if (EntryCount < 0)
	{
		return EIoErrorCode::ReadError;
	}

	if (EntryCount == 0)
	{
		return EIoErrorCode::Ok;
	}

	const int64 FooterPos = FileSize - sizeof(FFooter);
	if (FooterPos < 0)
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc)
			<< TEXT("Invalid journal footer");
	}

	const int64 EntriesPos = FileHandle->Tell();
	if (FileHandle->Seek(FooterPos) == false)
	{
		return EIoErrorCode::ReadError;
	}

	FFooter Footer;
	if ((FileHandle->Read(reinterpret_cast<uint8*>(&Footer), sizeof(FFooter)) == false) || (Footer.IsValid() == false))
	{
		return FIoStatusBuilder(EIoErrorCode::ReadError)
			<< TEXT("Failed to validate journal footer '")
			<< JournalFile
			<< TEXT("'");
	}

	if (FileHandle->Seek(EntriesPos) == false)
	{
		return EIoErrorCode::ReadError;
	}

	TArray<FEntry> Entries;
	Entries.SetNumZeroed(IntCastChecked<int32>(EntryCount));

	if (FileHandle->Read(reinterpret_cast<uint8*>(Entries.GetData()), sizeof(FEntry) * EntryCount) == false)
	{
		return EIoErrorCode::ReadError;
	}

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Replaying %d CAS journal entries of total %.2lf KiB from '%s'"),
		EntryCount, ToKiB(sizeof(FEntry) * EntryCount), *JournalFile);

	for (const FEntry& Entry : Entries)
	{
		Handler(Entry);
	}

	return EIoErrorCode::Ok;
}

FIoStatus FCasJournal::Create(const FString& JournalFile)
{
	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();
	Ipf.DeleteFile(*JournalFile);

	TUniquePtr<IFileHandle> FileHandle(Ipf.OpenWrite(*JournalFile));
	if (FileHandle.IsValid() == false)
	{
		return EIoErrorCode::FileNotOpen;
	}

	FHeader Header;
	FMemory::Memcpy(&Header.Magic, &FHeader::MagicSequence, sizeof(FHeader::MagicSequence));
	Header.Version = EVersion::Latest;

	if (FileHandle->Write(reinterpret_cast<uint8*>(&Header), sizeof(FHeader)) == false)
	{
		return EIoErrorCode::WriteError;
	}

	FFooter Footer;
	FMemory::Memcpy(&Footer.Magic, &FFooter::MagicSequence, sizeof(FFooter::MagicSequence));
	if (FileHandle->Write(reinterpret_cast<uint8*>(&Footer), sizeof(FFooter)) == false)
	{
		return EIoErrorCode::WriteError;
	}

	return EIoErrorCode::Ok;
}

FCasJournal::FTransaction FCasJournal::Begin(FString&& JournalFile)
{
	return FTransaction
	{
		.JournalFile = MoveTemp(JournalFile)
	};
}

FIoStatus FCasJournal::Commit(FTransaction&& Transaction)
{
	if (Transaction.Entries.IsEmpty())
	{
		return EIoErrorCode::Ok;
	}

	IPlatformFile&	Ipf = FPlatformFileManager::Get().GetPlatformFile();

	// Validate header and footer
	{
		TUniquePtr<IFileHandle> FileHandle(Ipf.OpenRead(*Transaction.JournalFile));
		const int64				FileSize = FileHandle.IsValid() ? FileHandle->Size() : -1;

		if (FileSize < sizeof(FHeader))
		{
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed)
				<< TEXT("Failed to validate CAS journal file '")
				<< Transaction.JournalFile
				<< TEXT("'");
		}

		FHeader Header;
		if ((FileHandle->Read(reinterpret_cast<uint8*>(&Header), sizeof(FHeader)) == false) || (Header.IsValid() == false))
		{
			return FIoStatusBuilder(EIoErrorCode::ReadError)
				<< TEXT("Failed to validate CAS journal header '")
				<< Transaction.JournalFile
				<< TEXT("'");
		}

		const int64 FooterPos = FileSize - sizeof(FFooter);
		if (FileHandle->Seek(FooterPos) == false)
		{
			return FIoStatusBuilder(EIoErrorCode::ReadError)
				<< TEXT("Failed to validate CAS journal footer '")
				<< Transaction.JournalFile
				<< TEXT("'");
		}

		FFooter Footer;
		if ((FileHandle->Read(reinterpret_cast<uint8*>(&Footer), sizeof(FFooter)) == false) || (Footer.IsValid() == false))
		{
			return FIoStatusBuilder(EIoErrorCode::ReadError)
				<< TEXT("Failed to validate CAS journal footer '")
				<< Transaction.JournalFile
				<< TEXT("'");
		}
	}

	// Append entries
	{
		const bool				bAppend = true;
		TUniquePtr<IFileHandle> FileHandle(Ipf.OpenWrite(*Transaction.JournalFile, bAppend));
		const int64				FileSize	= FileHandle.IsValid() ? FileHandle->Size() : -1;
		const int64				EntriesPos	= FileSize > 0 ? FileSize - sizeof(FFooter) : -1;

		if ((EntriesPos < 0) || (FileHandle->Seek(EntriesPos) == false))
		{
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed)
				<< TEXT("Failed to open CAS journal '")
				<< Transaction.JournalFile
				<< TEXT("'");
		}

		const int64 TotalEntrySize = Transaction.Entries.Num() * sizeof(FEntry);
		if (FileHandle->Write(
			reinterpret_cast<const uint8*>(Transaction.Entries.GetData()),
			TotalEntrySize) == false)
		{
			return FIoStatusBuilder(EIoErrorCode::WriteError)
				<< TEXT("Failed to write CAS journal entries to '")
				<< Transaction.JournalFile
				<< TEXT("'");
		}

		FFooter Footer;
		FMemory::Memcpy(&Footer.Magic, &FFooter::MagicSequence, sizeof(FFooter::MagicSequence));
		if (FileHandle->Write(reinterpret_cast<uint8*>(&Footer), sizeof(FFooter)) == false)
		{
			return FIoStatusBuilder(EIoErrorCode::WriteError)
				<< TEXT("Failed to write CAS journal footer to '")
				<< Transaction.JournalFile
				<< TEXT("'");
		}

		if (FileHandle->Flush() == false)
		{
			return EIoErrorCode::WriteError;
		}

		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Committed %d CAS journal entries of total %.2lf KiB to '%s'"),
			Transaction.Entries.Num(), ToKiB(TotalEntrySize), *Transaction.JournalFile);

		return EIoErrorCode::Ok;
	}
}

void FCasJournal::FTransaction::ChunkLocation(const FCasLocation& Location, const FCasAddr& Addr)
{
	Entries.AddZeroed_GetRef().ChunkLocation = FEntry::FChunkLocation
	{
		.CasLocation	= Location,
		.CasAddr		= Addr
	};
}

void FCasJournal::FTransaction::BlockCreated(FCasBlockId BlockId)
{
	Entries.AddZeroed_GetRef().BlockOperation = FEntry::FBlockOperation
	{
		.Type		= FEntry::EType::BlockCreated,
		.BlockId	= BlockId,
		.UtcTicks	= FDateTime::UtcNow().GetTicks()
	};
}

void FCasJournal::FTransaction::BlockDeleted(FCasBlockId BlockId)
{
	Entries.AddZeroed_GetRef().BlockOperation = FEntry::FBlockOperation
	{
		.Type		= FEntry::EType::BlockDeleted,
		.BlockId	= BlockId,
		.UtcTicks	= FDateTime::UtcNow().GetTicks()
	};
}

void FCasJournal::FTransaction::BlockAccess(FCasBlockId BlockId, int64 UtcTicks)
{
	Entries.AddZeroed_GetRef().BlockOperation = FEntry::FBlockOperation
	{
		.Type		= FEntry::EType::BlockAccess,
		.BlockId	= BlockId,
		.UtcTicks	= UtcTicks
	};
}

///////////////////////////////////////////////////////////////////////////////
class FOnDemandInstallCache final
	: public IOnDemandInstallCache 
{
	using FSharedBackendContextRef	= TSharedRef<const FIoDispatcherBackendContext>;
	using FSharedBackendContext		= TSharedPtr<const FIoDispatcherBackendContext>;

	struct FChunkRequest
	{
		explicit FChunkRequest(
			FSharedAsyncFileHandle FileHandle,
			FIoRequestImpl* Request,
			FOnDemandChunkInfo&& Info,
			FIoOffsetAndLength Range,
			uint64 RequestedRawSize)
				: SharedFileHandle(FileHandle)
				, DispatcherRequest(Request)
				, ChunkInfo(MoveTemp(Info))
				, ChunkRange(Range)
				, EncodedChunk(ChunkRange.GetLength())
				, RawSize(RequestedRawSize)
		{
			check(DispatcherRequest != nullptr);
			check(ChunkInfo.IsValid());
			check(Request->NextRequest == nullptr);
			check(Request->BackendData == nullptr);
		}

		static FChunkRequest* Get(FIoRequestImpl& Request)
		{
			return reinterpret_cast<FChunkRequest*>(Request.BackendData);
		}

		static FChunkRequest& GetRef(FIoRequestImpl& Request)
		{
			check(Request.BackendData);
			return *reinterpret_cast<FChunkRequest*>(Request.BackendData);
		}

		static FChunkRequest& Attach(FIoRequestImpl& Request, FChunkRequest* ChunkRequest)
		{
			check(Request.BackendData == nullptr);
			check(ChunkRequest != nullptr);
			Request.BackendData = ChunkRequest;
			return *ChunkRequest;
		}

		static TUniquePtr<FChunkRequest> Detach(FIoRequestImpl& Request)
		{
			void* ChunkRequest = nullptr;
			Swap(ChunkRequest, Request.BackendData);
			return TUniquePtr<FChunkRequest>(reinterpret_cast<FChunkRequest*>(ChunkRequest));
		}

		FSharedAsyncFileHandle			SharedFileHandle;
		TUniquePtr<IAsyncReadRequest>	FileReadRequest;
		FIoRequestImpl*					DispatcherRequest;
		FOnDemandChunkInfo				ChunkInfo;
		FIoOffsetAndLength				ChunkRange;
		FIoBuffer						EncodedChunk;
		uint64							RawSize;
	};

	struct FPendingChunks
	{
		static constexpr uint64 MaxPendingBytes = 4ull << 20;

		bool IsEmpty() const
		{
			check(Chunks.Num() == ChunkHashes.Num());
			return TotalSize == 0 && Chunks.IsEmpty() && ChunkHashes.IsEmpty();
		}

		void Append(FIoBuffer&& Chunk, const FIoHash& ChunkHash)
		{
			check(Chunks.Num() == ChunkHashes.Num());
			TotalSize += Chunk.GetSize();
			ChunkHashes.Add(ChunkHash);
			Chunks.Add(MoveTemp(Chunk));
		}

		FIoBuffer Pop(FIoHash& OutChunkHash)
		{
			check(Chunks.Num() == ChunkHashes.Num());
			check(Chunks.IsEmpty() == false);
			FIoBuffer Chunk = Chunks.Pop(EAllowShrinking::No);
			TotalSize		= TotalSize - Chunk.GetSize();
			OutChunkHash	= ChunkHashes.Pop(EAllowShrinking::No);
			return Chunk;
		}

		void Reset()
		{
			Chunks.Reset();
			ChunkHashes.Reset();
			TotalSize = 0;
		}

		TArray<FIoBuffer>	Chunks;
		TArray<FIoHash>		ChunkHashes;
		uint64				TotalSize = 0;
	};

	using FUniquePendingChunks = TUniquePtr<FPendingChunks>;

public:
	FOnDemandInstallCache(const FOnDemandInstallCacheConfig& Config, FOnDemandIoStore& IoStore);
	virtual ~FOnDemandInstallCache();

	// IIoDispatcherBackend
	virtual void								Initialize(FSharedBackendContextRef Context) override;
	virtual void								Shutdown() override;
	virtual void								ResolveIoRequests(FIoRequestList Requests, FIoRequestList& OutUnresolved) override;
	virtual FIoRequestImpl*						GetCompletedIoRequests() override;
	virtual void								CancelIoRequest(FIoRequestImpl* Request) override;
	virtual void								UpdatePriorityForIoRequest(FIoRequestImpl* Request) override;
	virtual bool								DoesChunkExist(const FIoChunkId& ChunkId) const override;
	virtual TIoStatusOr<uint64>					GetSizeForChunk(const FIoChunkId& ChunkId) const override;
	virtual TIoStatusOr<FIoMappedRegion>		OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options) override;

	// IOnDemandInstallCache
	virtual bool								IsChunkCached(const FIoHash& ChunkHash) override;
	virtual FIoStatus							PutChunk(FIoBuffer&& Chunk, const FIoHash& ChunkHash) override;
	virtual FIoStatus							Purge(TMap<FIoHash, uint64>&& ChunksToIntall) override;
	virtual FIoStatus							PurgeAllUnreferenced() override;
	virtual FIoStatus							Flush() override;
	virtual FOnDemandInstallCacheStorageUsage	GetStorageUsage() override;

private:
	void										AddReferencesToBlocks(
													const TArray<FSharedOnDemandContainer>& Containers, 
													const TArray<TBitArray<>>& ChunkEntryIndices,
													FCasBlockInfoMap& BlockInfo) const;
	FIoStatus									Purge(const FCasBlockInfoMap& BlockInfo, uint64 TotalBytesToPurge, uint64& OutTotalPurgedBytes);
	bool										Resolve(FIoRequestImpl* Request);
	void										CompleteRequest(FIoRequestImpl* Request, bool bFileReadWasCancelled);
	FIoStatus									FlushPendingChunks(FPendingChunks& Block);
	FString										GetJournalFilename() const { return CacheDirectory / TEXT("cas.jrn"); }

	FOnDemandIoStore&		IoStore;
	FString					CacheDirectory;
	FCas					Cas;
	FUniquePendingChunks	PendingChunks;
	FSharedBackendContext	BackendContext;
	FIoRequestList			CompletedRequests;
	UE::FMutex				Mutex;
	uint64					MaxCacheSize;
};

///////////////////////////////////////////////////////////////////////////////
FOnDemandInstallCache::FOnDemandInstallCache(const FOnDemandInstallCacheConfig& Config, FOnDemandIoStore& InIoStore)
	: IoStore(InIoStore)
	, CacheDirectory(Config.RootDirectory)
	, MaxCacheSize(Config.DiskQuota)
{
	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Initializing install cache, MaxCacheSize=%.2lf MiB"),
		ToMiB(MaxCacheSize));

	FIoStatus Status = Cas.Initialize(CacheDirectory);
	if (Status.IsOk() == false)
	{
		UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to initialize install cache"));
		return;
	}

	// Replay the journal to get the current state 
	// TODO: Purge the journal or create snapshot when the journal gets too big
	const FString JournalFile = GetJournalFilename();
	Status = FCasJournal::Replay(JournalFile, [this](const FCasJournal::FEntry& JournalEntry)
	{
		switch(JournalEntry.Type())
		{
		case FCasJournal::FEntry::EType::ChunkLocation:
		{
			const FCasJournal::FEntry::FChunkLocation& ChunkLocation = JournalEntry.ChunkLocation;
			if (ChunkLocation.CasLocation.IsValid())
			{
				Cas.Lookup.FindOrAdd(ChunkLocation.CasAddr, ChunkLocation.CasLocation);
			}
			else
			{
				Cas.Lookup.Remove(ChunkLocation.CasAddr);
			}
			break;
		}
		case FCasJournal::FEntry::EType::BlockCreated:
		{
			const FCasJournal::FEntry::FBlockOperation& Op = JournalEntry.BlockOperation;
			Cas.CurrentBlock = Op.BlockId;
			Cas.BlockIds.Add(Op.BlockId);
			break;
		}
		case FCasJournal::FEntry::EType::BlockDeleted:
		{
			const FCasJournal::FEntry::FBlockOperation& Op = JournalEntry.BlockOperation;
			Cas.BlockIds.Remove(Op.BlockId);
			if (Cas.CurrentBlock == Op.BlockId)
			{
				Cas.CurrentBlock = FCasBlockId::Invalid;
			}
			break;
		}
		case FCasJournal::FEntry::EType::BlockAccess:
		{
			const FCasJournal::FEntry::FBlockOperation& Op = JournalEntry.BlockOperation;
			Cas.TrackAccess(Op.BlockId, Op.UtcTicks);
			break;
		}
		};
	});

	// Verify the current state with the cached content on disk 
	// TODO: Add checksums etc
	TArray<FCasAddr> RemovedChunks;
	if (FIoStatus Verify = Cas.Verify(RemovedChunks); !Verify.IsOk())
	{
		// Try to recover if the CAS blocks on disk doesn't match
		FCasJournal::FTransaction Transaction = FCasJournal::Begin(GetJournalFilename());
		for (const FCasAddr& Addr : RemovedChunks)
		{
			Transaction.ChunkLocation(FCasLocation::Invalid, Addr);
		}
		Status = FCasJournal::Commit(MoveTemp(Transaction));
	}

	Cas.Compact();

	if (Status.IsOk() == false)
	{
		if (Status.GetErrorCode() != EIoErrorCode::NotFound)
		{
			UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Failed to replay install cache journal file '%s, reason '%s'"),
				*Status.ToString(), *JournalFile);

			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Deleting installed content and reinitializing cache"));
			IFileManager::Get().DeleteDirectory(*CacheDirectory);
			if (Status = Cas.Initialize(CacheDirectory); Status.IsOk() == false)
			{
				UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to initialize install cache, reason '%s'"),
					*Status.ToString());
				return;
			}
		}

		if (Status = FCasJournal::Create(JournalFile); Status.IsOk())
		{
			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Created CAS journal '%s'"), *JournalFile);
		}
		else
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to create CAS journal '%s'"), *JournalFile);
		}
	}
}

FOnDemandInstallCache::~FOnDemandInstallCache()
{
}

void FOnDemandInstallCache::Initialize(FSharedBackendContextRef Context)
{
	BackendContext = Context;
}

void FOnDemandInstallCache::Shutdown()
{
	FCas::FLastAccess LastAccess;
	{
		TUniqueLock Lock(Cas.Mutex);
		LastAccess = MoveTemp(Cas.LastAccess);
	}

	FCasJournal::FTransaction Transaction = FCasJournal::Begin(GetJournalFilename());
	for (const TPair<FCasBlockId, int64>& Kv : LastAccess)
	{
		Transaction.BlockAccess(Kv.Key, Kv.Value);
	}
	FCasJournal::Commit(MoveTemp(Transaction));
}

void FOnDemandInstallCache::ResolveIoRequests(FIoRequestList Requests, FIoRequestList& OutUnresolved)
{
	while (FIoRequestImpl* Request = Requests.PopHead())
	{
		if (Resolve(Request) == false)
		{
			OutUnresolved.AddTail(Request);
		}
	}
}

FIoRequestImpl* FOnDemandInstallCache::GetCompletedIoRequests()
{
	FIoRequestImpl* FirstCompleted = nullptr;
	{
		UE::TUniqueLock Lock(Mutex);
		for (FIoRequestImpl& Completed : CompletedRequests)
		{
			TUniquePtr<FChunkRequest> Detached = FChunkRequest::Detach(Completed);
		}
		FirstCompleted = CompletedRequests.GetHead();
		CompletedRequests = FIoRequestList();
	}

	return FirstCompleted;
}

void FOnDemandInstallCache::CancelIoRequest(FIoRequestImpl* Request)
{
	check(Request != nullptr);
	UE::TUniqueLock Lock(Mutex);
	if (FChunkRequest* ChunkRequest = FChunkRequest::Get(*Request))
	{
		if (ChunkRequest->FileReadRequest.IsValid())
		{
			ChunkRequest->FileReadRequest->Cancel();
		}
	}
}

void FOnDemandInstallCache::UpdatePriorityForIoRequest(FIoRequestImpl* Request)
{
}

bool FOnDemandInstallCache::DoesChunkExist(const FIoChunkId& ChunkId) const
{
	const TIoStatusOr<uint64> Status = GetSizeForChunk(ChunkId);
	return Status.IsOk();
}

TIoStatusOr<uint64> FOnDemandInstallCache::GetSizeForChunk(const FIoChunkId& ChunkId) const
{
	if (FOnDemandChunkInfo ChunkInfo = IoStore.GetInstalledChunkInfo(ChunkId))
	{
		return ChunkInfo.RawSize();
	}

	return FIoStatus(EIoErrorCode::UnknownChunkID);
}

TIoStatusOr<FIoMappedRegion> FOnDemandInstallCache::OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options)
{
	return FIoStatus(EIoErrorCode::FileOpenFailed);
}

bool FOnDemandInstallCache::Resolve(FIoRequestImpl* Request)
{
	FOnDemandChunkInfo ChunkInfo = IoStore.GetInstalledChunkInfo(Request->ChunkId);
	if (ChunkInfo.IsValid() == false)
	{
		return false;
	}

	bool bIsLocationInCurrentBlock = false;
	const FCasLocation CasLoc = Cas.FindChunk(ChunkInfo.Hash(), bIsLocationInCurrentBlock);
	if (CasLoc.IsValid() == false)
	{
		return false;
	}

	const uint64 RequestSize = FMath::Min<uint64>(
		Request->Options.GetSize(),
		ChunkInfo.RawSize() - Request->Options.GetOffset());

	TIoStatusOr<FIoOffsetAndLength> ChunkRange = FIoChunkEncoding::GetChunkRange(
		ChunkInfo.RawSize(),
		ChunkInfo.BlockSize(),
		ChunkInfo.Blocks(),
		Request->Options.GetOffset(),
		RequestSize);

	if (ChunkRange.IsOk() == false)
	{
		UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to get chunk range"));
		return false;
	}

	Cas.TrackAccess(CasLoc.BlockId);

	// Use synchronous file read API when reading from and writing to the same cache block concurrently.
	const bool bSyncRead = bIsLocationInCurrentBlock || CVars::bForceSyncIO;
	if (bSyncRead)
	{
		// The internal request parameters are attached/owned by the I/O request via
		// the backend data parameter. The chunk request is deleted in GetCompletedRequests
		FChunkRequest::Attach(*Request, new FChunkRequest(
			FSharedAsyncFileHandle(),
			Request,
			MoveTemp(ChunkInfo),
			ChunkRange.ConsumeValueOrDie(),
			RequestSize));

		UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, Request, CasLoc]
		{
			FChunkRequest& ChunkRequest = FChunkRequest::GetRef(*Request);
			bool bOk = false;

			FFileOpenResult FileOpenResult = Cas.OpenRead(CasLoc.BlockId);
			if (FileOpenResult.IsValid())
			{
				TUniquePtr<IFileHandle> FileHandle = FileOpenResult.StealValue();
				const int64 CasBlockOffset = CasLoc.BlockOffset + ChunkRequest.ChunkRange.GetOffset();
				if (Request->IsCancelled() == false && FileHandle->Seek(CasBlockOffset))
				{
					bOk = FileHandle->Read(ChunkRequest.EncodedChunk.GetData(), ChunkRequest.EncodedChunk.GetSize());
					if (bOk == false)
					{
						const FString Filename = Cas.GetBlockFilename(CasLoc.BlockId);
						UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to read %llu bytes at offset %lld in CAS block '%s'"),
							ChunkRequest.EncodedChunk.GetSize(),
							CasBlockOffset,
							*Filename);
					}
				}
				else
				{
					const FString Filename = Cas.GetBlockFilename(CasLoc.BlockId);
					UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to seek to offset %lld in CAS block '%s'"), CasBlockOffset, *Filename);
				}
			}
			else
			{
				const FString Filename = Cas.GetBlockFilename(CasLoc.BlockId);
				UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to open CAS block '%s' for reading, reason '%s'"),
					*Filename, *FileOpenResult.GetError().GetMessage());
			}

			const bool bWasCancelled = bOk == false;
			CompleteRequest(Request, bWasCancelled);
		});

		return true;
	}

	FSharedAsyncFileHandle FileHandle = Cas.OpenAsyncRead(CasLoc.BlockId);
	if (FileHandle.IsValid() == false)
	{
		const FString Filename = Cas.GetBlockFilename(CasLoc.BlockId);
		UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to open CAS block '%s' for async reading"), *Filename);
		TUniquePtr<FChunkRequest> Detached = FChunkRequest::Detach(*Request);
	}

	// The internal request parameters are attached/owned by the I/O request via
	// the backend data parameter. The chunk request is deleted in GetCompletedRequests
	FChunkRequest& ChunkRequest = FChunkRequest::Attach(*Request, new FChunkRequest(
		FileHandle,
		Request,
		MoveTemp(ChunkInfo),
		ChunkRange.ConsumeValueOrDie(),
		RequestSize));

	FAsyncFileCallBack Callback = [this, Request](bool bWasCancelled, IAsyncReadRequest* ReadRequest) 
	{
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, Request, bWasCancelled]
		{
			CompleteRequest(Request, bWasCancelled);
		});
	};

	ChunkRequest.FileReadRequest.Reset(FileHandle->ReadRequest(
		CasLoc.BlockOffset + ChunkRequest.ChunkRange.GetOffset(),
		ChunkRequest.ChunkRange.GetLength(),
		EAsyncIOPriorityAndFlags::AIOP_BelowNormal,
		&Callback,
		ChunkRequest.EncodedChunk.GetData()));

	if (ChunkRequest.FileReadRequest.IsValid() == false)
	{
		TUniquePtr<FChunkRequest> Detached = FChunkRequest::Detach(*Request);
		return false;
	}

	return true;
}

bool FOnDemandInstallCache::IsChunkCached(const FIoHash& ChunkHash)
{
	const FCasLocation Loc = Cas.FindChunk(ChunkHash);
	return Loc.IsValid();
}

FIoStatus FOnDemandInstallCache::PutChunk(FIoBuffer&& Chunk, const FIoHash& ChunkHash)
{
	if (PendingChunks.IsValid() == false)
	{
		PendingChunks = MakeUnique<FPendingChunks>();
	}

	if (PendingChunks->TotalSize > FPendingChunks::MaxPendingBytes)
	{
		if (FIoStatus Status = FlushPendingChunks(*PendingChunks); Status.IsOk() == false)
		{
			return Status;
		}
		check(PendingChunks->IsEmpty());
	}

	PendingChunks->Append(MoveTemp(Chunk), ChunkHash);
	return FIoStatus::Ok;
}

FIoStatus FOnDemandInstallCache::Purge(TMap<FIoHash, uint64>&& ChunksToIntall) 
{
	FCasBlockInfoMap	BlockInfo;
	const uint64		TotalCachedBytes = Cas.GetBlockInfo(BlockInfo);
	uint64				TotalUncachedBytes = 0;

	for (const TPair<FIoHash, uint64>& Kv : ChunksToIntall)
	{
		if (FCasLocation Loc = Cas.FindChunk(Kv.Key); Loc.IsValid())
		{
			BlockInfo.FindOrAdd(Loc.BlockId).RefCount++;
		}
		else
		{
			TotalUncachedBytes += Kv.Value;
		}
	}

	const uint64 TotalRequiredBytes = TotalCachedBytes + TotalUncachedBytes;
	if (TotalRequiredBytes <= MaxCacheSize)
	{
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Skipping cache purge, MaxCacheSize=%.2lf MiB, CacheSize=%.2lf MiB, UncachedSize=%.2lf MiB"),
			ToMiB(MaxCacheSize), ToMiB(TotalCachedBytes), ToMiB(TotalUncachedBytes));
		return FIoStatus::Ok;
	}

	TArray<FSharedOnDemandContainer>	Containers; 
	TArray<TBitArray<>>					ChunkEntryIndices;

	IoStore.GetReferencedContent(Containers, ChunkEntryIndices);
	check(Containers.Num() == ChunkEntryIndices.Num());

	AddReferencesToBlocks(Containers, ChunkEntryIndices, BlockInfo);

	//TODO: Compute fragmentation metric and redownload chunks when this number gets too high

	BlockInfo.ValueSort([](const FCasBlockInfo& LHS, const FCasBlockInfo& RHS)
	{
		return LHS.LastAccess < RHS.LastAccess;
	});

	const uint64 TotalReferencedBytes = Algo::TransformAccumulate(BlockInfo,
		[](const TPair<FCasBlockId, FCasBlockInfo>& Kv){ return (Kv.Value.RefCount > 0) ? Kv.Value.FileSize : uint64(0); },
		uint64(0));

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Purging install cache, MaxCacheSize=%.2lf MiB, CacheSize=%.2lf MiB, UncachedSize=%.2lf MiB, ReferencedBytes=%.2lf MiB"),
		ToMiB(MaxCacheSize), ToMiB(TotalCachedBytes), ToMiB(TotalUncachedBytes), ToMiB(TotalReferencedBytes));

	const uint64	TotalBytesToPurge	= TotalRequiredBytes - MaxCacheSize;
	uint64			TotalPurgedBytes	= 0;

	FIoStatus Status = Purge(BlockInfo, TotalBytesToPurge, TotalPurgedBytes);

	if (TotalPurgedBytes > 0)
	{
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Purged %.2lf MiB (%.2lf%%) from install cache"),
			ToMiB(TotalPurgedBytes), 100.0 * (double(TotalPurgedBytes) / double(TotalCachedBytes)));
	}

	const uint64 NewCachedBytes = TotalCachedBytes - TotalPurgedBytes;
	UE_CLOG(NewCachedBytes > MaxCacheSize,
		LogIoStoreOnDemand, Warning, TEXT("Max install cache size exceeded by %.2lf MiB (%.2lf%%)"),
			ToMiB(NewCachedBytes - MaxCacheSize), 100.0 * (double(NewCachedBytes - MaxCacheSize) / double(MaxCacheSize)));

	if (TotalPurgedBytes < TotalBytesToPurge)
	{
		return FIoStatusBuilder(EIoErrorCode::WriteError) << FString::Printf(TEXT("Failed to purge %" UINT64_FMT " from install cache"), TotalBytesToPurge);
	}

	return Status;
}

FIoStatus FOnDemandInstallCache::PurgeAllUnreferenced()
{
	FCasBlockInfoMap	BlockInfo;
	const uint64		TotalCachedBytes = Cas.GetBlockInfo(BlockInfo);
	uint64				TotalUncachedBytes = 0;

	TArray<FSharedOnDemandContainer>	Containers;
	TArray<TBitArray<>>					ChunkEntryIndices;

	IoStore.GetReferencedContent(Containers, ChunkEntryIndices);
	check(Containers.Num() == ChunkEntryIndices.Num());

	AddReferencesToBlocks(Containers, ChunkEntryIndices, BlockInfo);

	//TODO: Compute fragmentation metric and redownload chunks when this number gets too high

	const uint64 TotalReferencedBytes = Algo::TransformAccumulate(BlockInfo,
		[](const TPair<FCasBlockId, FCasBlockInfo>& Kv) { return (Kv.Value.RefCount > 0) ? Kv.Value.FileSize : uint64(0); },
		uint64(0));

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Purging install cache, MaxCacheSize=%.2lf MiB, CacheSize=%.2lf MiB, UncachedSize=%.2lf MiB, ReferencedBytes=%.2lf MiB"),
		ToMiB(MaxCacheSize), ToMiB(TotalCachedBytes), ToMiB(TotalUncachedBytes), ToMiB(TotalReferencedBytes));

	uint64 TotalPurgedBytes = 0;
	FIoStatus Status = Purge(BlockInfo, MaxCacheSize, TotalPurgedBytes);

	if (TotalPurgedBytes > 0)
	{
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Purged %.2lf MiB (%.2lf%%) from install cache"),
			ToMiB(TotalPurgedBytes), 100.0 * (double(TotalPurgedBytes) / double(TotalCachedBytes)));
	}

	const uint64 NewCachedBytes = TotalCachedBytes - TotalPurgedBytes;
	UE_CLOG(NewCachedBytes > MaxCacheSize,
		LogIoStoreOnDemand, Warning, TEXT("Max install cache size exceeded by %.2lf MiB (%.2lf%%)"),
			ToMiB(NewCachedBytes - MaxCacheSize), 100.0 * (double(NewCachedBytes - MaxCacheSize) / double(MaxCacheSize)));

	return Status;
}

void FOnDemandInstallCache::AddReferencesToBlocks( 
	const TArray<FSharedOnDemandContainer>& Containers, 
	const TArray<TBitArray<>>& ChunkEntryIndices,
	FCasBlockInfoMap& BlockInfo) const
{
	for (int32 Index = 0; FSharedOnDemandContainer Container : Containers)
	{
		const TBitArray<>& IsReferenced = ChunkEntryIndices[Index++];
		for (int32 EntryIndex = 0; const FOnDemandChunkEntry& Entry : Container->ChunkEntries)
		{
			if (bool bIsReferenced = IsReferenced[EntryIndex++]; bIsReferenced == false)
			{
				continue;
			}

			if (FCasLocation Loc = Cas.FindChunk(Entry.Hash); Loc.IsValid())
			{
				if (FCasBlockInfo* Info = BlockInfo.Find(Loc.BlockId))
				{
					Info->RefCount++;
				}
			}
		}
	}
}

FIoStatus FOnDemandInstallCache::Purge(const FCasBlockInfoMap& BlockInfo, const uint64 TotalBytesToPurge, uint64& OutTotalPurgedBytes)
{
	OutTotalPurgedBytes = 0;

	for (const TPair<FCasBlockId, FCasBlockInfo>& Kv : BlockInfo)
	{
		const FCasBlockId BlockId = Kv.Key;
		const FCasBlockInfo& Info = Kv.Value;
		if (Info.RefCount > 0)
		{
			continue;
		}

		FCasJournal::FTransaction	Transaction = FCasJournal::Begin(GetJournalFilename());
		TArray<FCasAddr>			RemovedChunks;

		if (FIoStatus Status = Cas.DeleteBlock(BlockId, RemovedChunks); !Status.IsOk())
		{
			return Status;
		}

		if (Cas.CurrentBlock == BlockId)
		{
			Cas.CurrentBlock = FCasBlockId::Invalid;
		}

		OutTotalPurgedBytes += Info.FileSize;

		for (const FCasAddr& Addr : RemovedChunks)
		{
			Transaction.ChunkLocation(FCasLocation::Invalid, Addr);
		}
		Transaction.BlockDeleted(BlockId);

		if (FIoStatus Status = FCasJournal::Commit(MoveTemp(Transaction)); !Status.IsOk())
		{
			return Status;
		}
		
		if (OutTotalPurgedBytes >= TotalBytesToPurge)
		{
			break;
		}
	}

	return FIoStatus::Ok;
}

FIoStatus FOnDemandInstallCache::Flush()
{
	if (PendingChunks.IsValid())
	{
		FUniquePendingChunks Chunks = MoveTemp(PendingChunks);
		return FlushPendingChunks(*Chunks);
	}

	Cas.Compact();
	return FIoStatus::Ok;
}

FOnDemandInstallCacheStorageUsage FOnDemandInstallCache::GetStorageUsage()
{
	// If this is called from a thread other than the OnDemandIoStore tick thread
	// then its possible the block info and containers may not be in sync with each other
	// or the current state of the tick thread.
	// This should only be used for debugging and telemetry purposes.

	FCasBlockInfoMap	BlockInfo;
	const uint64		TotalCachedBytes = Cas.GetBlockInfo(BlockInfo);

	TArray<FSharedOnDemandContainer>	Containers;
	TArray<TBitArray<>>					ChunkEntryIndices;
	IoStore.GetReferencedContent(Containers, ChunkEntryIndices);
	check(Containers.Num() == ChunkEntryIndices.Num());

	AddReferencesToBlocks(Containers, ChunkEntryIndices, BlockInfo);

	return FOnDemandInstallCacheStorageUsage
	{
		.MaxSize = MaxCacheSize,
		.TotalSize = TotalCachedBytes,
		.ReferencedBlockSize = Algo::TransformAccumulate(BlockInfo,
			[](const TPair<FCasBlockId, FCasBlockInfo>& Kv) { return (Kv.Value.RefCount > 0) ? Kv.Value.FileSize : uint64(0); },
			uint64(0))
	};
}

FIoStatus FOnDemandInstallCache::FlushPendingChunks(FPendingChunks& Chunks)
{
	ON_SCOPE_EXIT { Chunks.Reset(); };

	while (Chunks.IsEmpty() == false)
	{
		FCasJournal::FTransaction Transaction = FCasJournal::Begin(GetJournalFilename());

		if (Cas.CurrentBlock.IsValid() == false)
		{
			Cas.CurrentBlock = Cas.CreateBlock();
			ensure(Cas.CurrentBlock.IsValid());
			Transaction.BlockCreated(Cas.CurrentBlock);
		}

		TUniquePtr<IFileHandle>	CasFileHandle = Cas.OpenWrite(Cas.CurrentBlock);
		if (CasFileHandle.IsValid() == false)
		{
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed)
				<< TEXT("Failed to open cache block file '")
				<< Cas.GetBlockFilename(Cas.CurrentBlock)
				<< TEXT("'");
		}

		const int64 CasBlockOffset = CasFileHandle->Tell();

		FLargeMemoryWriter	Ar(Chunks.TotalSize);
		TArray<FIoHash>		ChunkHashes;
		TArray<int64>		Offsets;

		while (Chunks.IsEmpty() == false)
		{
			if (CasBlockOffset > 0 && CasBlockOffset + Ar.Tell() + Chunks.Chunks[0].GetSize() > Cas.MaxBlockSize)
			{
				break;
			}
			FIoBuffer Chunk = Chunks.Pop(ChunkHashes.AddDefaulted_GetRef());
			Offsets.Add(CasBlockOffset + Ar.Tell());
			Ar.Serialize(Chunk.GetData(), Chunk.GetSize());
		}

		if (Ar.Tell() > 0)
		{
			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Writing %.2lf MiB to CAS block %u"),
				ToMiB(Ar.Tell()), Cas.CurrentBlock.Id);

			if (CasFileHandle->Write(Ar.GetData(), Ar.Tell()) == false)
			{
				return FIoStatusBuilder(EIoErrorCode::WriteError)
					<< TEXT("Failed to serialize chunks to cache block");
			}
			Cas.TrackAccess(Cas.CurrentBlock);

			if (CasFileHandle->Flush() == false)
			{
				return FIoStatusBuilder(EIoErrorCode::WriteError)
					<< TEXT("Failed to flush cache block to disk");
			}

			check(ChunkHashes.Num() == Offsets.Num());
			check(Cas.CurrentBlock.IsValid());
			for (int32 Idx = 0, Count = Offsets.Num(); Idx < Count; ++Idx)
			{
				const FCasAddr	CasAddr = FCasAddr::From(ChunkHashes[Idx]);
				const uint32	ChunkOffset = IntCastChecked<uint32>(Offsets[Idx]);

				FCasLocation& Loc = Cas.Lookup.FindOrAdd(CasAddr);
				Loc.BlockId	= Cas.CurrentBlock;
				Loc.BlockOffset	= ChunkOffset;
				Transaction.ChunkLocation(Loc, CasAddr);
			}
		}

		if (FIoStatus Status = FCasJournal::Commit(MoveTemp(Transaction)); Status.IsOk() == false)
		{
			return Status;
		}

		if (Chunks.IsEmpty() == false)
		{
			Cas.CurrentBlock = FCasBlockId::Invalid;
		}
	}

	return FIoStatus::Ok;
}

void FOnDemandInstallCache::CompleteRequest(FIoRequestImpl* Request, bool bFileReadWasCancelled)
{
	FChunkRequest& ChunkRequest			= FChunkRequest::GetRef(*Request);
	const FOnDemandChunkInfo& ChunkInfo	= ChunkRequest.ChunkInfo;
	FIoBuffer EncodedChunk				= MoveTemp(ChunkRequest.EncodedChunk);
	bool bSucceeded						= EncodedChunk.GetSize() > 0 && !bFileReadWasCancelled && !Request->IsCancelled();

	if (bSucceeded)
	{
		FIoChunkDecodingParams Params;
		Params.CompressionFormat	= ChunkInfo.CompressionFormat();
		Params.EncryptionKey		= ChunkInfo.EncryptionKey();
		Params.BlockSize			= ChunkInfo.BlockSize();
		Params.TotalRawSize			= ChunkInfo.RawSize();
		Params.RawOffset			= Request->Options.GetOffset();
		Params.EncodedOffset		= ChunkRequest.ChunkRange.GetOffset();
		Params.EncodedBlockSize		= ChunkInfo.Blocks();
		Params.BlockHash			= ChunkInfo.BlockHashes();

		Request->CreateBuffer(ChunkRequest.RawSize);
		FMutableMemoryView RawChunk = Request->GetBuffer().GetMutableView();

		bSucceeded = FIoChunkEncoding::Decode(Params, EncodedChunk.GetView(), RawChunk);
		UE_CLOG(!bSucceeded, LogIoStoreOnDemand, Error, TEXT("Failed to decode chunk, ChunkId='%s'"), *LexToString(Request->ChunkId));
	}

	if (bSucceeded == false)
	{
		Request->SetResult(FIoBuffer());
		Request->SetFailed();
	}

	{
		UE::TUniqueLock Lock(Mutex);
		CompletedRequests.AddTail(Request);
	}

	BackendContext->WakeUpDispatcherThreadDelegate.Execute();
}

///////////////////////////////////////////////////////////////////////////////
TSharedPtr<IOnDemandInstallCache> MakeOnDemandInstallCache(
	FOnDemandIoStore& IoStore,
	const FOnDemandInstallCacheConfig& Config)
{
	IFileManager& Ifm = IFileManager::Get();
	if (Config.bDropCache)
	{
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Deleting install cache directory '%s'"), *Config.RootDirectory);
		Ifm.DeleteDirectory(*Config.RootDirectory, false, true);
	}

	const bool bTree = true;
	if (!Ifm.MakeDirectory(*Config.RootDirectory, bTree))
	{
		UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to create directory '%s'"), *Config.RootDirectory);
		return TSharedPtr<IOnDemandInstallCache>();
	}

	return MakeShareable<IOnDemandInstallCache>(new FOnDemandInstallCache(Config, IoStore));
}

} // namespace UE::IoStore
