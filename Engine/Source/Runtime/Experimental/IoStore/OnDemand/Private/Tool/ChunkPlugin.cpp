// Copyright Epic Games, Inc. All Rights Reserved.

#if defined(UE_WITH_IAS_TOOL)

#include "Command.h"

#include "Algo/Find.h"
#include "Containers/StringView.h"
#include "HAL/FileManager.h"
#include "IO/IoChunkEncoding.h"
#include "IO/IoStatus.h"
#include "IO/IoStore.h"
#include "IO/IoStoreOnDemand.h"
#include "IO/IoContainerHeader.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/StringBuilder.h"
#include "S3/S3Client.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonSerializerMacros.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "UploadQueue.h"

namespace UE::IoStore::Tool
{

struct FChunkPluginStats : FJsonSerializable
{
	double TotalChunksSizeKb = -1.0;

BEGIN_JSON_SERIALIZER
	JSON_SERIALIZE("TotalChunksSizeKb", TotalChunksSizeKb);
END_JSON_SERIALIZER
};

struct FS3Params
{
public:
	FString S3ServiceUrl;
	FString S3Bucket;
	FString S3BucketPrefix;
	FString S3Region;
	FString S3AccessKey;
	FString S3SecretKey;
	FString S3SessionToken;
	int32 S3MaxConcurrentUploads;

	FS3Params(const FContext& Context)
	{
		S3ServiceUrl = FString(Context.Get<FStringView>(TEXT("-ServiceUrl"), FString()));
		S3Bucket = FString(Context.Get<FStringView>(TEXT("-Bucket"), FString()));
		S3Region = FString(Context.Get<FStringView>(TEXT("-Region"), FString()));
		S3AccessKey = FString(Context.Get<FStringView>(TEXT("-AccessKey"), FString()));
		S3SecretKey = FString(Context.Get<FStringView>(TEXT("-SecretKey"), FString()));
		S3SessionToken = FString(Context.Get<FStringView>(TEXT("-SessionToken"), FString()));


		S3BucketPrefix = FString(Context.Get<FStringView>(TEXT("-BucketPrefix"), FString()));
		S3MaxConcurrentUploads = Context.Get<int32>(TEXT("-MaxConcurrentUploads"), 10);
	}

	bool IsValid()
	{
		return !S3AccessKey.IsEmpty() && !S3SecretKey.IsEmpty() && !S3SessionToken.IsEmpty();
	}
};


////////////////////////////////////////////////////////////////////////////////
struct FChunkPluginSettings
{
	TMap<FString, TArray<FString>> PackageSets;
};

class FChunkWriterInterface
{
public:
	virtual ~FChunkWriterInterface() {}
	virtual FIoStatus WriteChunk(const FString& RelativeDir, FIoBuffer Chunk, const FIoHash& Hash) = 0;

	virtual bool Flush() = 0;
};



class FS3ChunkWriter : public FChunkWriterInterface
{
public:
	FS3ChunkWriter(const FS3Params& S3Params) : 
		BucketPrefix(S3Params.S3BucketPrefix),
		Client(FS3ClientConfig({ S3Params.S3Region, S3Params.S3ServiceUrl }), FS3ClientCredentials(S3Params.S3AccessKey, S3Params.S3SecretKey, S3Params.S3SessionToken)),
		UploadQueue(Client, S3Params.S3Bucket, S3Params.S3MaxConcurrentUploads)
	{
	}

	virtual ~FS3ChunkWriter()
	{
		// this should have already been done but do it now just in case
		UploadQueue.Flush();
	}


	virtual FIoStatus WriteChunk(const FString& RelativeDirectory, FIoBuffer Chunk, const FIoHash& Hash) override
	{
		const FString HashString = LexToString(Hash);

		TStringBuilder<256> Key;
		Key << BucketPrefix << TEXT("/")
			<< RelativeDirectory
			<< TEXT("/") << HashString.Left(2)
			<< TEXT("/") << HashString
			<< TEXT(".iochunk");

		if (UploadQueue.Enqueue(Key, Chunk) == false)
		{
			return FIoStatus(EIoErrorCode::WriteError, TEXT("Failed to upload chunk"));
		}

		return FIoStatus(EIoErrorCode::Ok);
	}

	virtual bool Flush() override
	{
		if (!UploadQueue.Flush())
		{
			return false;
		}
		return true;
	}


private:
	FString BucketPrefix;
	UE::FS3Client Client;
	FUploadQueue UploadQueue;
};


class FDiskChunkWriter : public FChunkWriterInterface
{
public:
	FDiskChunkWriter(const FString& InOutputFolder)
	{
		OutputFolder = InOutputFolder;
		
	}

	virtual ~FDiskChunkWriter()
	{
	}

	virtual  FIoStatus WriteChunk(const FString& Directory, FIoBuffer Chunk, const FIoHash& Hash) override 
	{
		IFileManager& FileMgr = IFileManager::Get();
		const FString HashString = LexToString(Hash);

		TStringBuilder<256> Sb;
		Sb << OutputFolder << TEXT("/") << Directory << TEXT("/") << HashString.Left(2);

		bool bTree = true;
		if (FileMgr.MakeDirectory(Sb.ToString(), bTree) == false)
		{
			return FIoStatusBuilder(EIoErrorCode::WriteError)
				<< TEXT("Failed to create directory '")
				<< FString(Sb.ToString())
				<< TEXT("'");
		}

		Sb << TEXT("/") << HashString << TEXT(".iochunk");

		if (TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(Sb.ToString())); Ar.IsValid())
		{
			UE_LOG(LogIoStore, Display, TEXT("Writing file '%s' (%.2lf KiB)"),
				Sb.ToString(), double(Chunk.GetView().GetSize()) / 1024.0);
			Ar->Serialize((void*)Chunk.GetView().GetData(), Chunk.GetView().GetSize());

			return EIoErrorCode::Ok;
		}

		return FIoStatusBuilder(EIoErrorCode::WriteError)
			<< TEXT("Failed to write file '")
			<< FString(Sb.ToString())
			<< TEXT("'");
	}

	bool Flush() override
	{
		return true;
	}
private:
	FString OutputFolder;
};

////////////////////////////////////////////////////////////////////////////////
static FIoStatus WriteChunk(const FString& Directory, FMemoryView Chunk, const FIoHash& Hash)
{
	IFileManager& FileMgr = IFileManager::Get();
	const FString HashString = LexToString(Hash);

	TStringBuilder<256> Sb;
	Sb << Directory << TEXT("/") << HashString.Left(2);

	bool bTree = true;
	if (FileMgr.MakeDirectory(Sb.ToString(), bTree) == false)
	{
		return FIoStatusBuilder(EIoErrorCode::WriteError)
			<< TEXT("Failed to create directory '")
			<< FString(Sb.ToString())
			<< TEXT("'");
	}

	Sb << TEXT("/") << HashString << TEXT(".iochunk");

	if (TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(Sb.ToString())); Ar.IsValid())
	{
		UE_LOG(LogIoStore, Display, TEXT("Writing file '%s' (%.2lf KiB)"),
			Sb.ToString(), double(Chunk.GetSize()) / 1024.0);
		Ar->Serialize((void*)Chunk.GetData(), Chunk.GetSize());

		return EIoErrorCode::Ok;
	}

	return FIoStatusBuilder(EIoErrorCode::WriteError)
		<< TEXT("Failed to write file '")
		<< FString(Sb.ToString())
		<< TEXT("'");
}



////////////////////////////////////////////////////////////////////////////////
static int32 ChunkPluginCommandEntry(const FContext& Context)
{
	//while (!FPlatformMisc::IsDebuggerPresent());
	//UE_DEBUG_BREAK();

	const FString Platform				= FString(Context.Get<FStringView>(TEXT("-Platform"), FString()));
	const FString BuildVersion			= FString(Context.Get<FStringView>(TEXT("-BuildVersion"), FString()));
	const FString OnDemandTocName		= FString(Context.Get<FStringView>(TEXT("-OnDemandTocName"), FString()));
	const FString InputFolder			= FString(Context.Get<FStringView>(TEXT("-InputFolder"), FString()));
	const FString InOutputFolder		= FString(Context.Get<FStringView>(TEXT("-OutputFolder"), FString()));
	const FString IntermediateFolder	= FString(Context.Get<FStringView>(TEXT("-IntermediateFolder"), FString()));
	FString SettingsFile				= FString(Context.Get<FStringView>(TEXT("-SettingsFile"), FString()));
	FString OutputStatsJson				= FString(Context.Get<FStringView>(TEXT("-OutputStatsJson"), FString()));
	
	FS3Params S3Params(Context);

	const bool bIncludeSigPak			= Context.Get<bool>(TEXT("-IncludeSigPak"), false);
	const bool bDeleteContainerFiles	= !Context.Get<bool>(TEXT("-KeepContainerFiles"), false);
	FString OutputFolder				= InOutputFolder;
	FString ContainerFolder				= InputFolder;
	FString IoStoreRelativeFolder		= TEXT("iostore");
	FString ChunksRelativeFolder		= IoStoreRelativeFolder / TEXT("chunks");

	FPaths::NormalizeDirectoryName(ContainerFolder);
	FPaths::NormalizeDirectoryName(OutputFolder);
	FPaths::NormalizeFilename(SettingsFile);
	FPaths::NormalizeFilename(OutputStatsJson);

	UE_LOG(LogIoStore, Display, TEXT("I/O store chunk plugin:"));
	UE_LOG(LogIoStore, Display, TEXT("----------------------------------------"));
	UE_LOG(LogIoStore, Display, TEXT("\tBuildVersion: %s"), *BuildVersion);
	UE_LOG(LogIoStore, Display, TEXT("\tPlatform: %s"), *Platform);
	UE_LOG(LogIoStore, Display, TEXT("\tOnDemandTocName: %s"), *OnDemandTocName);
	UE_LOG(LogIoStore, Display, TEXT("\tInputFolder: %s"), *InputFolder);
	UE_LOG(LogIoStore, Display, TEXT("\tOutputFolder: %s"), *OutputFolder);
	UE_LOG(LogIoStore, Display, TEXT("\tIntermediateFolder: %s"), *IntermediateFolder);
	UE_LOG(LogIoStore, Display, TEXT("\tSettingsFile: %s"), *SettingsFile);
	UE_LOG(LogIoStore, Display, TEXT("\tOutputStatsJson: %s"), *OutputStatsJson);
	UE_LOG(LogIoStore, Display, TEXT("\tIncludeSigPak: %s"), bIncludeSigPak ? TEXT("true") : TEXT("false"));
	UE_LOG(LogIoStore, Display, TEXT("\tDeleteContainerFiles: %s"), bDeleteContainerFiles ? TEXT("true") : TEXT("false"));

	IFileManager& FileMgr = IFileManager::Get();

	TUniquePtr<FChunkWriterInterface> ChunkWriter;
	bool bUseS3 = false;
	if (S3Params.IsValid())
	{
		ChunkWriter = MakeUnique<FS3ChunkWriter>(S3Params);
	}
	else
	{
		ChunkWriter = MakeUnique<FDiskChunkWriter>(OutputFolder);
	}
	check(ChunkWriter != nullptr)

	FChunkPluginSettings Settings;
	if (!SettingsFile.IsEmpty())
	{
		TUniquePtr<FArchive> Ar(FileMgr.CreateFileReader(*SettingsFile));
		if (!Ar || Ar->IsError())
		{
			UE_LOG(LogIoStore, Error, TEXT("Failed to open settings file '%s'"), *SettingsFile);
			return -1;
		}

		TSharedRef<TJsonReader<UTF8CHAR>> JsonReader = TJsonReaderFactory<UTF8CHAR>::Create(Ar.Get());

		TSharedPtr<FJsonValue> JsonSettings;
		if (!FJsonSerializer::Deserialize(*JsonReader, JsonSettings))
		{
			UE_LOG(LogIoStore, Error, TEXT("Failed to read settings file '%s'"), *SettingsFile);
			return -1;
		}

		TSharedPtr<FJsonObject> JsonSettingsObject = JsonSettings->AsObject();
		if (!JsonSettingsObject)
		{
			UE_LOG(LogIoStore, Error, TEXT("Bad settings file '%s'"), *SettingsFile);
			return -1;
		}

		if (TSharedPtr<FJsonValue> PackageSets = JsonSettingsObject->TryGetField(TEXT("PackageSets")))
		{
			TSharedPtr<FJsonObject> PackageSetsObject = PackageSets->AsObject();
			if (!PackageSetsObject)
			{
				UE_LOG(LogIoStore, Error, TEXT("Bad settings file '%s'"), *SettingsFile);
				return -1;
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PackageSetsObject->Values)
			{
				TArray<FString> Packages;
				if (!PackageSetsObject->TryGetStringArrayField(Pair.Key, Packages))
				{
					UE_LOG(LogIoStore, Error, TEXT("Bad settings file '%s'"), *SettingsFile);
					return -1;
				}

				UE_LOG(LogIoStore, Display, TEXT("Found Package Set '%s'"), *Pair.Key);
				for (const FString& Package : Packages)
				{
					UE_LOG(LogIoStore, Display, TEXT("-> \t'%s'"), *Package);
				}

				Settings.PackageSets.Add(Pair.Key, MoveTemp(Packages));
			}
		}
	}

	FChunkPluginStats Stats;

	TMap<FGuid, FAES::FAESKey> EncryptionKeys;
	if (FileMgr.DirectoryExists(*ContainerFolder) == false)
	{
		UE_LOG(LogIoStore, Error, TEXT("Directory '%s' does not exist"), *ContainerFolder);
		return -1;
	}

	TArray<FString> ContainerFilenames;
	FileMgr.FindFiles(ContainerFilenames, *ContainerFolder, TEXT("*.utoc"));
	UE_LOG(LogIoStore, Display, TEXT("Found %d container files(s)"), ContainerFilenames.Num());

	FOnDemandToc OnDemandToc;
	//OnDemandToc.Header.ChunksDirectory = TODO 
	OnDemandToc.Containers.Reserve(ContainerFilenames.Num());

	TMap<FString, TArray<FOnDemandTocTagSetPackageList>> FoundTagSets;
	FoundTagSets.Reserve(Settings.PackageSets.Num());

	TArray<FString> FilesToDelete;

	for (const FString& Filename : ContainerFilenames)
	{
		const FString FullPath = ContainerFolder / Filename;
		FIoStoreReader ContainerFileReader;
		{
			FIoStatus Status = ContainerFileReader.Initialize(*FPaths::ChangeExtension(FullPath, TEXT("")), EncryptionKeys);
			if (!Status.IsOk())
			{
				UE_LOG(LogIoStore, Error, TEXT("Failed to open container '%s' for reading"), *FullPath);
				continue;
			}
		}

		UE_LOG(LogIoStore, Display, TEXT("Processing container '%s'"), *FullPath);

		/*TArray<FString> ContainerFileList;
		ContainerFileReader.GetFilenames(ContainerFileList);
		for (const FString& File : ContainerFileList)
		{
			UE_LOG(LogIoStore, Display, TEXT("Found file in container '%s'"), *File);
		}*/

		const uint32 BlockSize = ContainerFileReader.GetCompressionBlockSize();
		if (OnDemandToc.Header.BlockSize == 0)
		{
			OnDemandToc.Header.BlockSize = ContainerFileReader.GetCompressionBlockSize();
		}
		check(OnDemandToc.Header.BlockSize == ContainerFileReader.GetCompressionBlockSize());

		const int32 ContainerIndex = OnDemandToc.Containers.AddDefaulted();
		FOnDemandTocContainerEntry& ContainerEntry = OnDemandToc.Containers[ContainerIndex];
		ContainerEntry.ContainerId = ContainerFileReader.GetContainerId();
		ContainerEntry.ContainerName = FPaths::GetBaseFilename(FullPath);

		if (EnumHasAnyFlags(ContainerFileReader.GetContainerFlags(), EIoContainerFlags::Encrypted))
		{
			ContainerEntry.EncryptionKeyGuid = LexToString(ContainerFileReader.GetEncryptionKeyGuid());
		}

		TArray<FOnDemandTocTagSet> TagSets;
		if (Settings.PackageSets.Num() > 0)
		{
			const FIoChunkId HeaderChunkId = CreateContainerHeaderChunkId(ContainerFileReader.GetContainerId());
			TIoStatusOr<FIoBuffer> Status = ContainerFileReader.Read(HeaderChunkId, FIoReadOptions());
			if (!Status.IsOk())
			{
				if (Status.Status().GetErrorCode() == EIoErrorCode::NotFound)
				{
					UE_LOG(LogIoStore, Display, TEXT("No header chunk for container '%s'"), *FullPath);
				}
				else
				{
					UE_LOG(LogIoStore, Error, TEXT("Failed to read header chunk for container '%s'"), *FullPath);
					return -1;
				}
			}
			else
			{
				FIoContainerHeader ContainerHeader;
				FMemoryReaderView Ar(Status.ValueOrDie().GetView());
				Ar << ContainerHeader;
				Ar.Close();
				if (Ar.IsError() || Ar.IsCriticalError())
				{
					UE_LOG(LogIoStore, Error, TEXT("Failed to deserialize header chunk for container '%s'"), *FullPath);
					return -1;
				}

				for (const TPair<FString, TArray<FString>>& Pair : Settings.PackageSets)
				{
					const FString& Tag = Pair.Key;

					FOnDemandTocTagSetPackageList TagSetPackageList;
					TagSetPackageList.ContainerIndex = ContainerIndex;

					for (const FString& Package : Pair.Value)
					{
						const FPackageId PackageId = FPackageId::FromName(FName(Package));
						int32 PackageIndex = INDEX_NONE;
						if (ContainerHeader.PackageIds.Find(PackageId, PackageIndex))
						{
							UE_LOG(LogIoStore, Display, TEXT("Found package %s for tag %s in container %s"), *Package, *Tag, *Filename)
							TagSetPackageList.PackageIndicies.Add(PackageIndex);
						}
					}

					if (TagSetPackageList.PackageIndicies.Num() > 0)
					{
						FoundTagSets.FindOrAdd(Tag).Add(MoveTemp(TagSetPackageList));
					}
				}
			}
		}

		TArray<FIoStoreTocChunkInfo> ChunkInfos;
		ContainerFileReader.EnumerateChunks([&ChunkInfos](FIoStoreTocChunkInfo&& Info)
		{ 
			ChunkInfos.Emplace(MoveTemp(Info));
			return true;
		});

		UE_LOG(LogIoStore, Display, TEXT("Serializing %d chunks"), ChunkInfos.Num());

		for (const FIoStoreTocChunkInfo& ChunkInfo : ChunkInfos)
		{
			const bool bDecrypt = false;
			TIoStatusOr<FIoStoreCompressedReadResult> Status = ContainerFileReader.ReadCompressed(ChunkInfo.Id, FIoReadOptions(), bDecrypt);
			if (!Status.IsOk())
			{
				UE_LOG(LogIoStore, Error, TEXT("Failed to read container chunk, Container='%s', Reason='%s'"),
					*FullPath, *Status.Status().ToString());
				return -1;
			}

			FIoStoreCompressedReadResult ReadResult = Status.ConsumeValueOrDie();

			const uint32 BlockOffset	= ContainerEntry.BlockSizes.Num();
			const uint32 BlockCount		= ReadResult.Blocks.Num();
			const FIoHash ChunkHash		= FIoHash::HashBuffer(ReadResult.IoBuffer.GetView());
			const FString HashString	= LexToString(ChunkHash);

			FMemoryView EncodedBlocks = ReadResult.IoBuffer.GetView();
			uint64 RawChunkSize = 0;
			uint64 EncodedChunkSize = 0;
			for (const FIoStoreCompressedBlockInfo& BlockInfo : ReadResult.Blocks)
			{
				check(Align(BlockInfo.CompressedSize, FAES::AESBlockSize) == BlockInfo.AlignedSize);
				const uint64 EncodedBlockSize = BlockInfo.AlignedSize;
				ContainerEntry.BlockSizes.Add(uint32(BlockInfo.CompressedSize));

				FMemoryView EncodedBlock = EncodedBlocks.Left(EncodedBlockSize);
				EncodedBlocks += EncodedBlock.GetSize();
				ContainerEntry.BlockHashes.Add(FIoChunkEncoding::HashBlock(EncodedBlock));

				EncodedChunkSize += EncodedBlockSize;
				RawChunkSize += BlockInfo.UncompressedSize;

				if (OnDemandToc.Header.CompressionFormat.IsEmpty() && BlockInfo.CompressionMethod != NAME_None)
				{
					OnDemandToc.Header.CompressionFormat = BlockInfo.CompressionMethod.ToString();
				}
			}

			if (EncodedChunkSize != ReadResult.IoBuffer.GetSize())
			{
				UE_LOG(LogIoStore, Error, TEXT("Chunk size mismatch, Container='%s', ChunkId='%s'"),
					*FullPath, *LexToString(ChunkInfo.Id));
				return -1;
			}

			FIoStatus WriteStatus = ChunkWriter->WriteChunk(ChunksRelativeFolder, ReadResult.IoBuffer, ChunkHash);
			if (WriteStatus.IsOk() == false)
			{
				UE_LOG(LogIoStore, Error, TEXT("%s"), *WriteStatus.ToString());
				return -1;
			}

			FOnDemandTocEntry& TocEntry = ContainerEntry.Entries.AddDefaulted_GetRef();
			TocEntry.ChunkId = ChunkInfo.Id;
			TocEntry.Hash = ChunkHash;
			TocEntry.RawSize = RawChunkSize;
			TocEntry.EncodedSize = EncodedChunkSize;
			TocEntry.BlockOffset = BlockOffset;
			TocEntry.BlockCount = BlockCount;

			Stats.TotalChunksSizeKb += (double)EncodedChunkSize / 1024.0;
		}

		if (bDeleteContainerFiles)
		{
			FilesToDelete.Add(FullPath);
			ContainerFileReader.GetContainerFilePaths(FilesToDelete);
		}
	}

	OnDemandToc.TagSets.Reserve(FoundTagSets.Num());
	for (TPair<FString, TArray<FOnDemandTocTagSetPackageList>>& Pair : FoundTagSets)
	{
		FOnDemandTocTagSet& TocTagSet = OnDemandToc.TagSets.Emplace_GetRef();
		TocTagSet.Tag = Pair.Key;
		TocTagSet.Packages = MoveTemp(Pair.Value);
	}

	for (const FString& Path : FilesToDelete)
	{
		//UE_LOG(LogIas, Display, TEXT("Attempt Deleting '%s'"), *Path);
		if (FileMgr.FileExists(*Path))
		{
			UE_LOG(LogIas, Display, TEXT("Deleting '%s'"), *Path);
			if (!FileMgr.Delete(*Path, /*RequireExists*/true))
			{
				UE_LOG(LogIas, Error, TEXT("Failed to delete '%s'"), *Path);
			}
		}
	}

	// Write additional file(s)
	if (bIncludeSigPak)
	{
		const FStringView AllowedExt[]
		{
			TEXTVIEW("pak"),
			TEXTVIEW("sig"),
		};

		TStringBuilder<256> Sb;
		TArray<FString> AdditionalFiles;
		FileMgr.FindFiles(AdditionalFiles, *ContainerFolder, TEXT("*.*"));

		UE_LOG(LogIoStore, Display, TEXT("Serializing %d additional file(s)"), AdditionalFiles.Num());
		for (const FString& Filename : AdditionalFiles)
		{
			const bool bIncludeDot = false;
			const FStringView Ext = FPathViews::GetExtension(Filename, bIncludeDot);

			if (const FStringView* Result =
				Algo::FindByPredicate(AllowedExt, [&Ext](const FStringView& E) { return Ext == E; });
				Result == nullptr)
			{
				continue;
			}

			const FString FullPath = ContainerFolder / Filename;
			TArray<uint8> FileData;
			if (!FFileHelper::LoadFileToArray(FileData, *FullPath))
			{
				UE_LOG(LogIoStore, Error, TEXT("Failed reading file '%s'"), *FullPath);
				return -1;
			}

			FMemoryView Chunk = MakeMemoryView(FileData.GetData(), FileData.Num());
			const FIoHash ChunkHash = FIoHash::HashBuffer(Chunk);

			FIoBuffer Buffer(FIoBuffer::Clone, Chunk);

			FIoStatus WriteStatus = ChunkWriter->WriteChunk(ChunksRelativeFolder, Buffer, ChunkHash);

			if (WriteStatus.IsOk() == false)
			{
				UE_LOG(LogIoStore, Error, TEXT("%s"), *WriteStatus.ToString());
				return -1;
			}

			UE_LOG(LogIoStore, Display, TEXT("Adding additional file '%s'"), *Filename);
			OnDemandToc.AdditionalFiles.Add(FOnDemandTocAdditionalFile
			{
				.Hash = ChunkHash,
				.Filename = Filename,
				.FileSize = IntCastChecked<uint64>(FileData.Num())
			});
		}
	}

	OnDemandToc.Meta.EpochTimestamp = FDateTime::UtcNow().ToUnixTimestamp();
	OnDemandToc.Meta.TargetPlatform = Platform;
	OnDemandToc.Meta.BuildVersion	= BuildVersion;
	{
		FString Filename = FPathViews::SetExtension(OnDemandTocName, TEXT(".uondemandtoc"));
		Filename.ToLowerInline();

		// todo, we actually want this file in the base directory
		// const FString TocPath = OutputFolder / Filename; // like this yo
		const FString TocPath = OutputFolder / IoStoreRelativeFolder / Filename;
		if (TUniquePtr<FArchive> Ar(FileMgr.CreateFileWriter(*TocPath)); Ar.IsValid())
		{
			*Ar << OnDemandToc;
			Ar->Close();
			if (Ar->IsError())
			{
				UE_LOG(LogIoStore, Error, TEXT("Failed to serialize TOC '%s'"), *TocPath);
				return -1;
			}
			else
			{
				const int64 TocSize = Ar->Tell();
				UE_LOG(LogIoStore, Display, TEXT("Writing file '%s' (%.2lf KiB)"),
					*TocPath, double(TocSize) / 1024);
			}
		}
		else
		{
			UE_LOG(LogIoStore, Display, TEXT("Failed writing file '%s'"), *TocPath );
			return -1;
		}
	}

	// Write dummy containers if necessary
	if (bDeleteContainerFiles)
	{
		TSet<FString> DummyContainerPaths;
		FileMgr.IterateDirectory(*ContainerFolder, [&DummyContainerPaths](const TCHAR* FilenameOrDirectory, bool bIsDirectory)
		{
			if (!bIsDirectory)
			{
				FStringView MaybePak(FilenameOrDirectory);
				if (FPathViews::GetExtension(MaybePak) == TEXTVIEW("pak"))
				{
					DummyContainerPaths.Emplace(FPathViews::GetBaseFilenameWithPath(MaybePak));
				}
			}
			return true;
		});

		for (const FString& Filename : ContainerFilenames)
		{
			const FString FullPath = ContainerFolder / Filename;
			const FStringView ContainerPath = FPathViews::GetBaseFilenameWithPath(FullPath);
			if (!DummyContainerPaths.ContainsByHash(GetTypeHash(ContainerPath), ContainerPath))
			{
				continue;
			}

			FNameBuilder NameBuilder;
			NameBuilder << BuildVersion << FPathViews::GetBaseFilename(Filename) << TEXTVIEW("dummy");

			FIoContainerSettings ContainerSettings;
			ContainerSettings.ContainerId = FIoContainerId::FromName(FName(NameBuilder));

			FIoStoreTocResource Toc;

			TIoStatusOr<uint64> Status = FIoStoreTocResource::Write(*FullPath, Toc, 0, 0, ContainerSettings);
			if (!Status.IsOk())
			{
				UE_LOG(LogIoStore, Error, TEXT("Failed to write dummy container '%s' (%s)"), *FullPath, *Status.Status().ToString());
				return -1;
			}

			UE_LOG(LogIoStore, Display, TEXT("Wrote dummy file '%s' (%.2lf KiB)"), *FullPath, double(Status.ValueOrDie()) / 1024);
		}
	}


	if (!ChunkWriter->Flush())
	{
		UE_LOG(LogIoStore, Error, TEXT("Writer error: Failed to upload chunk(s)"));
		return -1; 
	}

	if (!OutputStatsJson.IsEmpty())
	{
		TUniquePtr<FArchive> FileWriter(FileMgr.CreateFileWriter(*OutputStatsJson));
		if (!FileWriter)
		{
			UE_LOG(LogIoStore, Display, TEXT("Failed writing stats file '%s'"), *OutputStatsJson);
			return -1;
		}
		TSharedRef<TJsonWriter<UTF8CHAR>> JsonWriter = TJsonWriterFactory<UTF8CHAR>::Create(FileWriter.Get());
		Stats.ToJson(JsonWriter, false);
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////
extern FArgumentSet S3Arguments;

static FCommand ChunkPluginCommand(
	ChunkPluginCommandEntry,
	TEXT("ChunkPlugin"),
	TEXT(""),
	{
		TArgument<FStringView>(TEXT("-Platform"),			TEXT("Platform name.")),
		TArgument<FStringView>(TEXT("-BuildVersion"),		TEXT("Build version")),
		TArgument<FStringView>(TEXT("-OnDemandTocName"),	TEXT("On Demand TOC Name")),
		TArgument<FStringView>(TEXT("-InputFolder"),		TEXT("Input folder to plugin information.")),
		TArgument<FStringView>(TEXT("-OutputFolder"),		TEXT("Ouptut folder.")),
		TArgument<FStringView>(TEXT("-IntermediateFolder"),	TEXT("Intermediate folder.")),
		TArgument<FStringView>(TEXT("-SettingsFile"),		TEXT("Optional settings file.")),
		TArgument<FStringView>(TEXT("-OutputStatsJson"),	TEXT("Path to write a json file with statistics.")),
		TArgument<bool>(TEXT("-IncludeSigPak"),				TEXT("Include .sig and .pak file in the uondemandtoc")),
		TArgument<bool>(TEXT("-KeepContainerFiles"),		TEXT("Should we keep the container files after processing them.")),
		TArgument<FStringView>(TEXT("-BucketPrefix"),		TEXT("Path to prefix to bucket objects")),
		TArgument<int32>(TEXT("-MaxConcurrentUploads"),		TEXT("Number of simultaneous uploads")),
		S3Arguments,
	}
);

} // namespace UE::IoStore::Tool

#endif // UE_WITH_IAS_TOOL
