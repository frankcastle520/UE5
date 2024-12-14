// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/IoStoreOnDemand.h"

#include "HAL/FileManager.h"
#include "HAL/FileManagerGeneric.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "IO/IoChunkEncoding.h"
#include "IO/PackageId.h"
#include "IasCache.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/EncryptionKeyManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/PathViews.h"
#include "Modules/ModuleManager.h"
#include "OnDemandIoStore.h"
#include "OnDemandHttpClient.h"
#include "OnDemandIoDispatcherBackend.h"
#include "Serialization/Archive.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/CustomVersion.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Statistics.h"
#include "String/LexFromString.h"

#if PLATFORM_WINDOWS
#	include <Windows/AllowWindowsPlatformTypes.h>
#		include <winsock2.h>
#	include <Windows/HideWindowsPlatformTypes.h>
#endif //PLATFORM_WINDOWS

DEFINE_LOG_CATEGORY(LogIoStoreOnDemand);
DEFINE_LOG_CATEGORY(LogIas);

namespace UE::IoStore
{

///////////////////////////////////////////////////////////////////////////////
static FAutoConsoleCommand OnDemandPurgeCacheCommand(
	TEXT("iostore.PurgeOnDemandInstallCache"),
	TEXT("Purge On Demand Install Cache"),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		FIoStoreOnDemandModule* IOStoreOnDemandModule = FModuleManager::Get().GetModulePtr<FIoStoreOnDemandModule>(TEXT("IoStoreOnDemand"));
		if(!IOStoreOnDemandModule)
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Could not find IoStoreOnDemand module"));
			return;
		}

		UE_LOG(LogIoStoreOnDemand, Display, TEXT("Purging on demand install cache"));
		IOStoreOnDemandModule->Purge(FOnDemandPurgeArgs(), [](const FOnDemandPurgeResult& Result)
		{
			if (Result.Status.IsOk())
			{
				UE_LOG(LogIoStoreOnDemand, Display, TEXT("Purged on demand install cache"));
			}
			else
			{
				UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed Purged on demand install cache: %s"), *Result.Status.ToString());
			}
		});
	}),
	ECVF_Cheat
);

static FAutoConsoleCommand OnDemandCacheUsageCommand(
	TEXT("iostore.CacheUsage"),
	TEXT("print cache usage"),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		FIoStoreOnDemandModule* IOStoreOnDemandModule = FModuleManager::Get().GetModulePtr<FIoStoreOnDemandModule>(TEXT("IoStoreOnDemand"));
		if (!IOStoreOnDemandModule)
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Could not find IoStoreOnDemand module"));
			return;
		}

		TIoStatusOr<FOnDemandCacheUsage> MaybeUsage = IOStoreOnDemandModule->GetCacheUsage();
		if (!MaybeUsage.IsOk())
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("iostore.CacheUsage failed: %s"), *MaybeUsage.Status().ToString());
			return;
		}

		const FOnDemandCacheUsage& Usage = MaybeUsage.ValueOrDie();
		UE_LOG(LogIoStoreOnDemand, Display, TEXT("iostore.CacheUsage"));
		UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tMaxSize %" UINT64_FMT), Usage.MaxSize);
		UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tTotalSize %" UINT64_FMT), Usage.TotalSize);
		UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tReferencedBlockSize %" UINT64_FMT), Usage.ReferencedBlockSize);
	}),
	ECVF_Cheat
);

////////////////////////////////////////////////////////////////////////////////
FString GIasOnDemandTocExt = TEXT(".uondemandtoc");

static const TCHAR* NotInitializedError = TEXT("I/O store on-demand not initialized");

/** Temp cvar to allow the fallback url to be hotfixed in case of problems */
static FString GDistributedEndpointFallbackUrl;
static FAutoConsoleVariableRef CVar_DistributedEndpointFallbackUrl(
	TEXT("ias.DistributedEndpointFallbackUrl"),
	GDistributedEndpointFallbackUrl,
	TEXT("CDN url to be used if a distributed endpoint cannot be reached (overrides IoStoreOnDemand.ini)")
);

////////////////////////////////////////////////////////////////////////////////
int64 ParseSizeParam(FStringView Value)
{
	Value = Value.TrimStartAndEnd();

	int64 Size = -1;
	LexFromString(Size, Value);
	if (Size >= 0)
	{
		if (Value.EndsWith(TEXT("GB"))) return Size << 30;
		if (Value.EndsWith(TEXT("MB"))) return Size << 20;
		if (Value.EndsWith(TEXT("KB"))) return Size << 10;
	}
	return Size;
}

////////////////////////////////////////////////////////////////////////////////
static int64 ParseSizeParam(const TCHAR* CommandLine, const TCHAR* Param)
{
	FString ParamValue;
	if (!FParse::Value(CommandLine, Param, ParamValue))
	{
		return -1;
	}

	return ParseSizeParam(ParamValue);
}

////////////////////////////////////////////////////////////////////////////////
static bool ParseEncryptionKeyParam(const FString& Param, FGuid& OutKeyGuid, FAES::FAESKey& OutKey)
{
	TArray<FString> Tokens;
	Param.ParseIntoArray(Tokens, TEXT(":"), true);

	if (Tokens.Num() == 2)
	{
		TArray<uint8> KeyBytes;
		if (FGuid::Parse(Tokens[0], OutKeyGuid) && FBase64::Decode(Tokens[1], KeyBytes))
		{
			if (OutKeyGuid != FGuid() && KeyBytes.Num() == FAES::FAESKey::KeySize)
			{
				FMemory::Memcpy(OutKey.Key, KeyBytes.GetData(), FAES::FAESKey::KeySize);
				return true;
			}
		}
	}
	
	return false;
}

////////////////////////////////////////////////////////////////////////////////
static bool ApplyEncryptionKeyFromString(const FString& GuidKeyPair)
{
	FGuid KeyGuid;
	FAES::FAESKey Key;

	if (ParseEncryptionKeyParam(GuidKeyPair, KeyGuid, Key))
	{
		// TODO: PAK and I/O store should share key manager
		FEncryptionKeyManager::Get().AddKey(KeyGuid, Key);
		FCoreDelegates::GetRegisterEncryptionKeyMulticastDelegate().Broadcast(KeyGuid, Key);

		return true;
	}
	else
	{
		return false;
	}
}

////////////////////////////////////////////////////////////////////////////////
static bool TryParseConfigContent(const FString& ConfigContent, const FString& ConfigFileName, FOnDemandEndpointConfig& OutEndpoint)
{
	if (ConfigContent.IsEmpty())
	{
		return false;
	}

	FConfigFile Config;
	Config.ProcessInputFileContents(ConfigContent, ConfigFileName);

	Config.GetString(TEXT("Endpoint"), TEXT("DistributionUrl"), OutEndpoint.DistributionUrl);
	if (!OutEndpoint.DistributionUrl.IsEmpty())
	{
		Config.GetString(TEXT("Endpoint"), TEXT("FallbackUrl"), OutEndpoint.FallbackUrl);

		if (!GDistributedEndpointFallbackUrl.IsEmpty())
		{
			OutEndpoint.FallbackUrl = GDistributedEndpointFallbackUrl;
		}
	}
	
	Config.GetArray(TEXT("Endpoint"), TEXT("ServiceUrl"), OutEndpoint.ServiceUrls);
	Config.GetString(TEXT("Endpoint"), TEXT("TocPath"), OutEndpoint.TocPath);

	if (OutEndpoint.DistributionUrl.EndsWith(TEXT("/")))
	{
		OutEndpoint.DistributionUrl = OutEndpoint.DistributionUrl.Left(OutEndpoint.DistributionUrl.Len() - 1);
	}

	for (FString& ServiceUrl : OutEndpoint.ServiceUrls)
	{
		if (ServiceUrl.EndsWith(TEXT("/")))
		{
			ServiceUrl.LeftInline(ServiceUrl.Len() - 1);
		}
	}

	if (OutEndpoint.TocPath.StartsWith(TEXT("/")))
	{
		OutEndpoint.TocPath.RightChopInline(1);
	}

	FString ContentKey;
	if (Config.GetString(TEXT("Endpoint"), TEXT("ContentKey"), ContentKey))
	{
		ApplyEncryptionKeyFromString(ContentKey);
	}

	return OutEndpoint.IsValid();
}

////////////////////////////////////////////////////////////////////////////////
static bool TryParseConfigFileFromPlatformPackage(FOnDemandEndpointConfig& OutConfig)
{
	const FString ConfigFileName = TEXT("IoStoreOnDemand.ini");
	const FString ConfigPath = FPaths::Combine(TEXT("Cloud"), ConfigFileName);
	
	if (FPlatformMisc::FileExistsInPlatformPackage(ConfigPath))
	{
		const FString ConfigContent = FPlatformMisc::LoadTextFileFromPlatformPackage(ConfigPath);
		return TryParseConfigContent(ConfigContent, ConfigFileName, OutConfig);
	}
	else
	{
		return false;
	}
}

////////////////////////////////////////////////////////////////////////////////
bool TryParseEndpointConfig(const TCHAR* CommandLine, FOnDemandEndpointConfig& OutConfig)
{
	OutConfig = FOnDemandEndpointConfig();
#if !UE_BUILD_SHIPPING
	FString UrlParam;
	if (FParse::Value(CommandLine, TEXT("Ias.TocUrl="), UrlParam))
	{
		FStringView UrlView(UrlParam);
		if (UrlView.StartsWith(TEXTVIEW("http://")) && UrlView.EndsWith(TEXTVIEW(".iochunktoc")))
		{
			int32 Delim = INDEX_NONE;
			if (UrlView.RightChop(7).FindChar(TEXT('/'), Delim))
			{
				OutConfig.ServiceUrls.Add(FString(UrlView.Left(7 +  Delim)));
				OutConfig.TocPath = UrlView.RightChop(OutConfig.ServiceUrls[0].Len() + 1);
			}
		}
	}
	else
#endif
	{
		if (TryParseConfigFileFromPlatformPackage(OutConfig))
		{
			TStringBuilder<256> TocFilePath;
			FPathViews::Append(TocFilePath, TEXT("Cloud"), FPaths::GetBaseFilename(OutConfig.TocPath));
			TocFilePath.Append(TEXT(".iochunktoc"));

			if (FPlatformMisc::FileExistsInPlatformPackage(*TocFilePath))
			{
				OutConfig.TocFilePath = TocFilePath;
			}
		}
	}

	return OutConfig.IsValid();
}

////////////////////////////////////////////////////////////////////////////////
static FIasCacheConfig GetIasCacheConfig(const TCHAR* CommandLine)
{
	FIasCacheConfig Ret;

	// Fetch values from .ini files
	auto GetConfigIntImpl = [CommandLine] (const TCHAR* ConfigKey, const TCHAR* ParamName, auto& Out)
	{
		int64 Value = -1;
		if (FString Temp; GConfig->GetString(TEXT("Ias"), ConfigKey, Temp, GEngineIni))
		{
			Value = ParseSizeParam(Temp);
		}
#if !UE_BUILD_SHIPPING
		if (int64 Override = ParseSizeParam(CommandLine, ParamName); Override >= 0)
		{
			Value = Override;
		}
#endif

		if (Value >= 0)
		{
			Out = decltype(Out)(Value);
		}

		return true;
	};

#define GetConfigInt(Name, Dest) \
	do { GetConfigIntImpl(TEXT("FileCache.") Name, TEXT("Ias.FileCache.") Name TEXT("="), Dest); } while (false)
	GetConfigInt(TEXT("WritePeriodSeconds"),	Ret.WriteRate.Seconds);
	GetConfigInt(TEXT("WriteOpsPerPeriod"),		Ret.WriteRate.Ops);
	GetConfigInt(TEXT("WriteBytesPerPeriod"),	Ret.WriteRate.Allowance);
	GetConfigInt(TEXT("DiskQuota"),				Ret.DiskQuota);
	GetConfigInt(TEXT("MemoryQuota"),			Ret.MemoryQuota);
	GetConfigInt(TEXT("JournalQuota"),			Ret.JournalQuota);
	GetConfigInt(TEXT("JournalMagic"),			Ret.JournalMagic);
	GetConfigInt(TEXT("DemandThreshold"),		Ret.Demand.Threshold);
	GetConfigInt(TEXT("DemandBoost"),			Ret.Demand.Boost);
	GetConfigInt(TEXT("DemandSuperBoost"),		Ret.Demand.SuperBoost);
#undef GetConfigInt

#if !UE_BUILD_SHIPPING
	if (FParse::Param(CommandLine, TEXT("Ias.DropCache")))
	{
		Ret.DropCache = true;
	}
	if (FParse::Param(CommandLine, TEXT("Ias.NoCache")))
	{
		Ret.DiskQuota = 0;
	}
#endif

	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
static void LoadCaCerts()
{
	using namespace UE::IoStore::HTTP;

	// The following config option is used when staging to copy root certs PEM
	const TCHAR* CertSection = TEXT("/Script/Engine.NetworkSettings");
	const TCHAR* CertKey = TEXT("n.VerifyPeer");
	bool bExpectCerts = false;
	if (GConfig != nullptr)
	{
		GConfig->GetBool(CertSection, CertKey, bExpectCerts, GEngineIni);
	}

	// Open the certs file
	IFileManager& Ifm = IFileManager::Get();
	FString PemPath = FPaths::EngineContentDir() / TEXT("Certificates/ThirdParty/cacert.pem");
	FArchive* Reader = Ifm.CreateFileReader(*PemPath);
	if (!bExpectCerts && Reader == nullptr)
	{
		UE_LOG(LogIas, Warning, TEXT("Unable to load '%s'. Maybe it wasn't staged? Ensure '[%s]/%s=true' when staging"), *PemPath, CertSection, CertKey);
		return;
	}
	checkf(Reader != nullptr, TEXT("%s/%s==true but '%s' could not be loaded"), CertSection, CertKey, *PemPath);

	// Buffer certificate data
	uint32 Size = uint32(Reader->TotalSize());
	FIoBuffer PemData(Size);
	FMutableMemoryView PemView = PemData.GetMutableView();
	Reader->Serialize(PemView.GetData(), Size);

	// Load the certs
	FCertRoots CaRoots(PemData.GetView());

	uint32 NumCerts = CaRoots.Num();
	UE_LOG(LogIas, Display, TEXT("CaRoots: %u (%u .pem bytes))"), NumCerts, Size);

	FCertRoots::SetDefault(MoveTemp(CaRoots));
	delete Reader;
}

////////////////////////////////////////////////////////////////////////////////
/**
 * Utility to create a FArchive capable of reading from disk using the exact same pathing
 * rules as FPlatformMisc::LoadTextFileFromPlatformPackage but without forcing the entire
 * file to be loaded at once.
 */
static TUniquePtr<FArchive> CreateReaderFromPlatformPackage(const FString& RelPath)
{
#if PLATFORM_IOS
    // IOS OpenRead assumes it is in cookeddata, using ~ for the base path tells it to use the package base path instead
    const FString AbsPath = FPaths::Combine(TEXT("~"), RelPath);
#else
    const FString AbsPath = FPaths::Combine(FGenericPlatformMisc::RootDir(), RelPath);
#endif
	if (TUniquePtr<IFileHandle> File(IPlatformFile::GetPlatformPhysical().OpenRead(*AbsPath)); File.IsValid())
	{
#if PLATFORM_ANDROID
		// This is a handle to an asset so we need to call Seek(0) to move the internal
		// offset to the start of the asset file.
		File->Seek(0);
#endif //PLATFORM_ANDROID
		const uint32 ReadBufferSize = 256 * 1024;
		const int64 FileSize = File->Size();
		return MakeUnique<FArchiveFileReaderGeneric>(File.Release(), *AbsPath, FileSize, ReadBufferSize);
	}

	return TUniquePtr<FArchive>();
}

////////////////////////////////////////////////////////////////////////////////
FArchive& operator<<(FArchive& Ar, FTocMeta& Meta)
{
	Ar << Meta.EpochTimestamp;
	Ar << Meta.BuildVersion;
	Ar << Meta.TargetPlatform;
	return Ar;
}

FCbWriter& operator<<(FCbWriter& Writer, const FTocMeta& Meta)
{
	Writer.BeginObject();
	Writer.AddInteger(UTF8TEXTVIEW("EpochTimestamp"), Meta.EpochTimestamp);
	Writer.AddString(UTF8TEXTVIEW("BuildVersion"), Meta.BuildVersion);
	Writer.AddString(UTF8TEXTVIEW("TargetPlatform"), Meta.TargetPlatform);
	Writer.EndObject();

	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FTocMeta& OutMeta)
{
	if (FCbObjectView Obj = Field.AsObjectView())
	{
		OutMeta.EpochTimestamp = Obj["EpochTimestamp"].AsInt64();
		OutMeta.BuildVersion = FString(Obj["BuildVersion"].AsString());
		OutMeta.TargetPlatform = FString(Obj["TargetPlatform"].AsString());
		return true;
	}
	
	return false;
}

FArchive& operator<<(FArchive& Ar, FOnDemandTocHeader& Header)
{
	if (Ar.IsLoading() && Ar.TotalSize() < sizeof(FOnDemandTocHeader))
	{
		Ar.SetError();
		return Ar;
	}

	Ar << Header.Magic;
	if (Header.Magic != FOnDemandTocHeader::ExpectedMagic)
	{
		Ar.SetError();
		return Ar;
	}

	Ar << Header.Version;
	if (static_cast<EOnDemandTocVersion>(Header.Version) == EOnDemandTocVersion::Invalid)
	{
		Ar.SetError();
		return Ar;
	}

	if (uint32(Header.Version) > uint32(EOnDemandTocVersion::Latest))
	{
		Ar.SetError();
		return Ar;
	}

	Ar << Header.ChunkVersion;
	Ar << Header.BlockSize;
	Ar << Header.CompressionFormat;
	Ar << Header.ChunksDirectory;

	return Ar;
}

FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocHeader& Header)
{
	Writer.BeginObject();
	Writer.AddInteger(UTF8TEXTVIEW("Magic"), Header.Magic);
	Writer.AddInteger(UTF8TEXTVIEW("Version"), Header.Version);
	Writer.AddInteger(UTF8TEXTVIEW("ChunkVersion"), Header.ChunkVersion);
	Writer.AddInteger(UTF8TEXTVIEW("BlockSize"), Header.BlockSize);
	Writer.AddString(UTF8TEXTVIEW("CompressionFormat"), Header.CompressionFormat);
	Writer.AddString(UTF8TEXTVIEW("ChunksDirectory"), Header.ChunksDirectory);
	Writer.EndObject();

	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocHeader& OutTocHeader)
{
	if (FCbObjectView Obj = Field.AsObjectView())
	{
		OutTocHeader.Magic = Obj["Magic"].AsUInt64();
		OutTocHeader.Version = Obj["Version"].AsUInt32();
		OutTocHeader.ChunkVersion = Obj["ChunkVersion"].AsUInt32();
		OutTocHeader.BlockSize = Obj["BlockSize"].AsUInt32();
		OutTocHeader.CompressionFormat = FString(Obj["CompressionFormat"].AsString());
		OutTocHeader.ChunksDirectory = FString(Obj["ChunksDirectory"].AsString());

		return OutTocHeader.Magic == FOnDemandTocHeader::ExpectedMagic &&
			static_cast<EOnDemandTocVersion>(OutTocHeader.Version) != EOnDemandTocVersion::Invalid;
	}

	return false;
}

FArchive& operator<<(FArchive& Ar, FOnDemandTocEntry& Entry)
{
	Ar << Entry.Hash;
	Ar << Entry.ChunkId;
	Ar << Entry.RawSize;
	Ar << Entry.EncodedSize;
	Ar << Entry.BlockOffset;
	Ar << Entry.BlockCount;

	return Ar;
}

FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocEntry& Entry)
{
	Writer.BeginObject();
	Writer.AddHash(UTF8TEXTVIEW("Hash"), Entry.Hash);
	Writer << UTF8TEXTVIEW("ChunkId") << Entry.ChunkId;
	Writer.AddInteger(UTF8TEXTVIEW("RawSize"), Entry.RawSize);
	Writer.AddInteger(UTF8TEXTVIEW("EncodedSize"), Entry.EncodedSize);
	Writer.AddInteger(UTF8TEXTVIEW("BlockOffset"), Entry.BlockOffset);
	Writer.AddInteger(UTF8TEXTVIEW("BlockCount"), Entry.BlockCount);
	Writer.EndObject();

	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocEntry& OutTocEntry)
{
	if (FCbObjectView Obj = Field.AsObjectView())
	{
		if (!LoadFromCompactBinary(Obj["ChunkId"], OutTocEntry.ChunkId))
		{
			return false;
		}

		OutTocEntry.Hash = Obj["Hash"].AsHash();
		OutTocEntry.RawSize = Obj["RawSize"].AsUInt64(~uint64(0));
		OutTocEntry.EncodedSize = Obj["EncodedSize"].AsUInt64(~uint64(0));
		OutTocEntry.BlockOffset = Obj["BlockOffset"].AsUInt32(~uint32(0));
		OutTocEntry.BlockCount = Obj["BlockCount"].AsUInt32();

		return OutTocEntry.Hash != FIoHash::Zero &&
			OutTocEntry.RawSize != ~uint64(0) &&
			OutTocEntry.EncodedSize != ~uint64(0) &&
			OutTocEntry.BlockOffset != ~uint32(0);
	}

	return false;
}

FArchive& operator<<(FArchive& Ar, FOnDemandTocContainerEntry& ContainerEntry)
{
	EOnDemandTocVersion TocVersion = EOnDemandTocVersion::Latest;

	if (Ar.IsLoading())
	{
		const FCustomVersion* CustomVersion = Ar.GetCustomVersions().GetVersion(FOnDemandToc::VersionGuid);
		check(CustomVersion);
		TocVersion = static_cast<EOnDemandTocVersion>(CustomVersion->Version);

		if (TocVersion >= EOnDemandTocVersion::ContainerId)
		{
			Ar << ContainerEntry.ContainerId;
		}
	}
	else
	{
		Ar << ContainerEntry.ContainerId;
	}

	Ar << ContainerEntry.ContainerName;
	Ar << ContainerEntry.EncryptionKeyGuid;
	Ar << ContainerEntry.Entries;
	Ar << ContainerEntry.BlockSizes;
	Ar << ContainerEntry.BlockHashes;
	Ar << ContainerEntry.UTocHash;

	if (!Ar.IsLoading() || (TocVersion >= EOnDemandTocVersion::ContainerFlags))
	{
		Ar << ContainerEntry.ContainerFlags;
	}

	return Ar;
}

FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocContainerEntry& ContainerEntry)
{
	Writer.BeginObject();
	Writer << UTF8TEXTVIEW("Id") << ContainerEntry.ContainerId;
	Writer.AddString(UTF8TEXTVIEW("Name"), ContainerEntry.ContainerName);
	Writer.AddString(UTF8TEXTVIEW("EncryptionKeyGuid"), ContainerEntry.EncryptionKeyGuid);

	Writer.BeginArray(UTF8TEXTVIEW("Entries"));
	for (const FOnDemandTocEntry& Entry : ContainerEntry.Entries)
	{
		Writer << Entry;
	}
	Writer.EndArray();
	
	Writer.BeginArray(UTF8TEXTVIEW("BlockSizes"));
	for (uint32 BlockSize : ContainerEntry.BlockSizes)
	{
		Writer << BlockSize;
	}
	Writer.EndArray();

	Writer.BeginArray(UTF8TEXTVIEW("BlockHashes"));
	for (uint32 BlockHash : ContainerEntry.BlockHashes)
	{
		Writer << BlockHash;
	}
	Writer.EndArray();

	Writer.AddHash(UTF8TEXTVIEW("UTocHash"), ContainerEntry.UTocHash);

	Writer.EndObject();

	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocContainerEntry& OutContainer)
{
	if (FCbObjectView Obj = Field.AsObjectView())
	{
		OutContainer.ContainerName = FString(Obj["Name"].AsString());
		OutContainer.EncryptionKeyGuid = FString(Obj["EncryptionKeyGuid"].AsString());

		FCbArrayView Entries = Obj["Entries"].AsArrayView();
		OutContainer.Entries.Reserve(int32(Entries.Num()));
		for (FCbFieldView ArrayField : Entries)
		{
			if (!LoadFromCompactBinary(ArrayField, OutContainer.Entries.AddDefaulted_GetRef()))
			{
				return false;
			}
		}

		FCbArrayView BlockSizes = Obj["BlockSizes"].AsArrayView();
		OutContainer.BlockSizes.Reserve(int32(BlockSizes.Num()));
		for (FCbFieldView ArrayField : BlockSizes)
		{
			OutContainer.BlockSizes.Add(ArrayField.AsUInt32());
		}

		FCbArrayView BlockHashes = Obj["BlockHashes"].AsArrayView();
		OutContainer.BlockHashes.Reserve(int32(BlockHashes.Num()));
		for (FCbFieldView ArrayField : BlockHashes)
		{
			if (ArrayField.IsHash())
			{
				const FIoHash BlockHash = ArrayField.AsHash();
				OutContainer.BlockHashes.Add(*reinterpret_cast<const uint32*>(&BlockHash));
			}
			else
			{
				OutContainer.BlockHashes.Add(ArrayField.AsUInt32());
			}
		}

		OutContainer.UTocHash = Obj["UTocHash"].AsHash();

		return true;
	}

	return false;
}

bool FOnDemandTocSentinel::IsValid()
{
	return FMemory::Memcmp(&Data, FOnDemandTocSentinel::SentinelImg, FOnDemandTocSentinel::SentinelSize) == 0;
}

FArchive& operator<<(FArchive& Ar, FOnDemandTocSentinel& Sentinel)
{
	if (Ar.IsSaving())
	{	
		// We could just cast FOnDemandTocSentinel::SentinelImg to a non-const pointer but we can't be 
		// 100% sure that the FArchive won't change the data, even if it is in Saving mode. Since this 
		// isn't performance critical we will play it safe.
		uint8 Output[FOnDemandTocSentinel::SentinelSize];
		FMemory::Memcpy(Output, FOnDemandTocSentinel::SentinelImg, FOnDemandTocSentinel::SentinelSize);

		Ar.Serialize(&Output, FOnDemandTocSentinel::SentinelSize);
	}
	else
	{
		Ar.Serialize(&Sentinel.Data, FOnDemandTocSentinel::SentinelSize);
	}

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FOnDemandTocAdditionalFile& AdditionalFile)
{
	Ar << AdditionalFile.Hash;
	Ar << AdditionalFile.Filename;
	Ar << AdditionalFile.FileSize;
	return Ar;
}

FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocAdditionalFile& AdditionalFile)
{
	Writer.BeginObject();
	Writer.AddHash(UTF8TEXTVIEW("Hash"), AdditionalFile.Hash);
	Writer.AddString(UTF8TEXTVIEW("Filename"), AdditionalFile.Filename);
	Writer.AddInteger(UTF8TEXTVIEW("Filename"), AdditionalFile.FileSize);
	Writer.EndObject();

	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocAdditionalFile& AdditionalFile)
{
	if (FCbObjectView Obj = Field.AsObjectView())
	{
		AdditionalFile.Hash = Obj["Hash"].AsHash();
		AdditionalFile.Filename = FString(Obj["Filename"].AsString());
		AdditionalFile.FileSize = Obj["FileSize"].AsUInt64();
		return true;
	}

	return false;
}

FArchive& operator<<(FArchive& Ar, FOnDemandTocTagSetPackageList& TagSet)
{
	Ar << TagSet.ContainerIndex;
	Ar << TagSet.PackageIndicies;
	return Ar;
}

FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocTagSetPackageList& TagSet)
{
	Writer.BeginObject();
	Writer.AddInteger(UTF8TEXTVIEW("ContainerIndex"), TagSet.ContainerIndex);
	Writer.BeginArray(UTF8TEXTVIEW("PackageIndicies"));
	for (const uint32 Index : TagSet.PackageIndicies)
	{
		Writer << Index;
	}
	Writer.EndArray();
	Writer.EndObject();
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocTagSetPackageList& TagSet)
{
	if (FCbObjectView Obj = Field.AsObjectView())
	{
		FCbFieldView ContainerIndex = Obj["ContainerIndex"];
		TagSet.ContainerIndex = ContainerIndex.AsUInt32();
		if (ContainerIndex.HasError())
		{
			return false;
		}

		FCbFieldView PackageIndicies = Obj["PackageIndicies"];
		FCbArrayView PackageIndiciesArray = PackageIndicies.AsArrayView();
		if(PackageIndicies.HasError())
		{
			return false;
		}

		TagSet.PackageIndicies.Reserve(int32(PackageIndiciesArray.Num()));
		for (FCbFieldView ArrayField : PackageIndiciesArray)
		{
			uint32 Index = ArrayField.AsUInt32();
			if (ArrayField.HasError())
			{
				return false;
			}
			TagSet.PackageIndicies.Emplace(Index);
		}

		return true;
	}

	return false;
}

FArchive& operator<<(FArchive& Ar, FOnDemandTocTagSet& TagSet)
{
	Ar << TagSet.Tag;
	Ar << TagSet.Packages;
	return Ar;
}

FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocTagSet& TagSet)
{
	Writer.BeginObject();
	Writer.AddString(UTF8TEXTVIEW("Tag"), TagSet.Tag);
	Writer.BeginArray(UTF8TEXTVIEW("Packages"));
	for (const FOnDemandTocTagSetPackageList& PackageList : TagSet.Packages)
	{
		Writer << PackageList;
	}
	Writer.EndArray();
	Writer.EndObject();
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocTagSet& TagSet)
{
	if (FCbObjectView Obj = Field.AsObjectView())
	{
		TagSet.Tag = FString(Obj["Tag"].AsString());
		FCbArrayView Packages = Obj["Packages"].AsArrayView();
		TagSet.Packages.Reserve(int32(Packages.Num()));
		for (FCbFieldView ArrayField : Packages)
		{
			if (!LoadFromCompactBinary(ArrayField, TagSet.Packages.Emplace_GetRef()))
			{
				return false;
			}
		}
		return true;
	}

	return false;
}

FArchive& operator<<(FArchive& Ar, FOnDemandToc& Toc)
{
	Ar << Toc.Header;
	if (Ar.IsError())
	{
		return Ar;
	}

	Ar.SetCustomVersion(Toc.VersionGuid, int32(Toc.Header.Version), TEXT("OnDemandToc"));

	if (uint32(Toc.Header.Version) >= uint32(EOnDemandTocVersion::Meta))
	{
		Ar << Toc.Meta;
	}
	Ar << Toc.Containers;

	if (uint32(Toc.Header.Version) >= uint32(EOnDemandTocVersion::AdditionalFiles))
	{
		Ar << Toc.AdditionalFiles;
	}

	if (uint32(Toc.Header.Version) >= uint32(EOnDemandTocVersion::TagSets))
	{
		Ar << Toc.TagSets;
	}

	return Ar;
}

FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandToc& Toc)
{
	Writer.BeginObject();
	Writer << UTF8TEXTVIEW("Header") << Toc.Header;

	Writer.BeginArray(UTF8TEXTVIEW("Containers"));
	for (const FOnDemandTocContainerEntry& Container : Toc.Containers)
	{
		Writer << Container;
	}
	Writer.EndArray();

	if (Toc.AdditionalFiles.Num() > 0)
	{
		Writer.BeginArray(UTF8TEXTVIEW("Files"));
		for (const FOnDemandTocAdditionalFile& File : Toc.AdditionalFiles)
		{
			Writer << File;
		}
		Writer.EndArray();
	}

	if (Toc.TagSets.Num() > 0)
	{
		Writer.BeginArray(UTF8TEXTVIEW("TagSets"));
		for (const FOnDemandTocTagSet& TagSet : Toc.TagSets)
		{
			Writer << TagSet;
		}
		Writer.EndArray();
	}

	Writer.EndObject();
	
	return Writer;
}

FGuid FOnDemandToc::VersionGuid = FGuid("C43DD98F353F499D9A0767F6EA0155EB");

bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandToc& OutToc)
{
	if (FCbObjectView Obj = Field.AsObjectView())
	{
		if (!LoadFromCompactBinary(Obj["Header"], OutToc.Header))
		{
			return false;
		}

		if (uint32(OutToc.Header.Version) >= uint32(EOnDemandTocVersion::Meta))
		{
			if (!LoadFromCompactBinary(Obj["Meta"], OutToc.Meta))
			{
				return false;
			}
		}

		FCbArrayView Containers = Obj["Containers"].AsArrayView();
		OutToc.Containers.Reserve(int32(Containers.Num()));
		for (FCbFieldView ArrayField : Containers)
		{
			if (!LoadFromCompactBinary(ArrayField, OutToc.Containers.AddDefaulted_GetRef()))
			{
				return false;
			}
		}

		if (uint32(OutToc.Header.Version) >= uint32(EOnDemandTocVersion::AdditionalFiles))
		{
			FCbArrayView Files = Obj["Files"].AsArrayView();
			OutToc.AdditionalFiles.Reserve(int32(Files.Num()));
			for (FCbFieldView ArrayField : Files)
			{
				if (!LoadFromCompactBinary(ArrayField, OutToc.AdditionalFiles.AddDefaulted_GetRef()))
				{
					return false;
				}
			}
		}

		if (uint32(OutToc.Header.Version) >= uint32(EOnDemandTocVersion::TagSets))
		{
			FCbArrayView TagSets = Obj["TagSets"].AsArrayView();
			OutToc.TagSets.Reserve(int32(TagSets.Num()));
			for (FCbFieldView ArrayField : TagSets)
			{
				if (!LoadFromCompactBinary(ArrayField, OutToc.TagSets.AddDefaulted_GetRef()))
				{
					return false;
				}
			}
		}

		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////
TIoStatusOr<FOnDemandToc> FOnDemandToc::LoadFromFile(const FString& FilePath, bool bValidate)
{
	TUniquePtr<FArchive> Ar;
	if (FPlatformMisc::FileExistsInPlatformPackage(FilePath))
	{
		Ar = CreateReaderFromPlatformPackage(FilePath);
	}
	else
	{
		Ar.Reset(IFileManager::Get().CreateFileReader(*FilePath));
	}

	if (Ar.IsValid() == false)
	{
		FIoStatus Status = FIoStatusBuilder(EIoErrorCode::FileNotOpen) << TEXT("Failed to open '") << FilePath << TEXT("'");
		return Status;
	}

	if (bValidate)
	{
		const int64 SentinelPos = Ar->TotalSize() - FOnDemandTocSentinel::SentinelSize;

		if (SentinelPos < 0)
		{
			FIoStatus Status = FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("Unexpected file size");
			return Status;
		}

		Ar->Seek(SentinelPos);

		FOnDemandTocSentinel Sentinel;
		*Ar << Sentinel;

		if (!Sentinel.IsValid())
		{
			return FIoStatus(EIoErrorCode::CorruptToc);
		}

		Ar->Seek(0);
	}

	FOnDemandToc Toc;
	*Ar << Toc;

	if (Ar->IsError() || Ar->IsCriticalError())
	{
		FIoStatus Status = FIoStatusBuilder(EIoErrorCode::FileNotOpen) << TEXT("Failed to serialize TOC file");
		return Status;
	}

	return Toc; 
}

////////////////////////////////////////////////////////////////////////////////
TIoStatusOr<FOnDemandToc> FOnDemandToc::LoadFromUrl(FAnsiStringView Url, uint32 RetryCount, bool bFollowRedirects)
{
	const EHttpRedirects Redirects = bFollowRedirects ? EHttpRedirects::Follow : EHttpRedirects::Disabled;
	TIoStatusOr<FIoBuffer> Response = FHttpClient::Get(Url, RetryCount, Redirects); 

	if (!Response.IsOk())
	{
		FIoStatus Status = FIoStatusBuilder(EIoErrorCode::ReadError) << TEXT("Failed to fetch TOC from URL");
		return Status;
	}

	FMemoryReaderView Ar(Response.ValueOrDie().GetView());
	FOnDemandToc Toc;
	Ar << Toc;

	if (Ar.IsError() || Ar.IsCriticalError())
	{
		FIoStatus Status = FIoStatusBuilder(EIoErrorCode::ReadError) << TEXT("Failed to serialize TOC from HTTP response");
		return Status;
	}

	return Toc; 
}

////////////////////////////////////////////////////////////////////////////////
TIoStatusOr<FOnDemandToc> FOnDemandToc::LoadFromUrl(FStringView Url, uint32 RetryCount, bool bFollowRedirects)
{
	auto AnsiUrl = StringCast<ANSICHAR>(Url.GetData(), Url.Len());
	return LoadFromUrl(AnsiUrl, RetryCount, bFollowRedirects);
}

////////////////////////////////////////////////////////////////////////////////
void FIoStoreOnDemandModule::SetBulkOptionalEnabled(bool bInEnabled)
{
	if (HttpIoDispatcherBackend.IsValid())
	{
		HttpIoDispatcherBackend->SetBulkOptionalEnabled(bInEnabled);
	}
	else
	{
		UE_LOG(LogIas, Log, TEXT("Deferring call to FIoStoreOnDemandModule::SetBulkOptionalEnabled(%s)"), bInEnabled ? TEXT("true") : TEXT("false"));
		DeferredBulkOptionalEnabled = bInEnabled;
	}
}

void FIoStoreOnDemandModule::SetEnabled(bool bInEnabled)
{
	if (HttpIoDispatcherBackend.IsValid())
	{
		HttpIoDispatcherBackend->SetEnabled(bInEnabled);
	}
	else
	{
		UE_LOG(LogIas, Log, TEXT("Deferring call to FIoStoreOnDemandModule::SetEnabled(%s)"), bInEnabled ? TEXT("true") : TEXT("false"));
		DeferredEnabled = bInEnabled;
	}
}

void FIoStoreOnDemandModule::AbandonCache()
{
	if (HttpIoDispatcherBackend.IsValid())
	{
		HttpIoDispatcherBackend->AbandonCache();
	}
	else
	{
		UE_LOG(LogIas, Log, TEXT("Deferring call to FIoStoreOnDemandModule::AbandonCache"));
		DeferredAbandonCache = true;
	}
}

bool FIoStoreOnDemandModule::IsEnabled() const
{
	return HttpIoDispatcherBackend.IsValid()? HttpIoDispatcherBackend->IsEnabled():DeferredAbandonCache.IsSet();
}

void FIoStoreOnDemandModule::ReportAnalytics(TArray<FAnalyticsEventAttribute>& OutAnalyticsArray) const
{
	if (HttpIoDispatcherBackend.IsValid())
	{
		HttpIoDispatcherBackend->ReportAnalytics(OutAnalyticsArray);
	}
}

void FIoStoreOnDemandModule::Mount(FOnDemandMountArgs&& Args, FOnDemandMountCompleted&& OnCompleted)
{
	if (IoStore.IsValid() == false)
	{
		IoStore = MakeShared<FOnDemandIoStore>();
		if (FIoStatus Status = IoStore->Initialize(); !Status.IsOk())
		{
			UE_LOG(LogIas, Error, TEXT("Failed to initialize I/O store on-demand, reason '%s'"), *Status.ToString());
			IoStore.Reset();
			return OnCompleted(FOnDemandMountResult{ Args.MountId, Status });
		}
	}

	IoStore->Mount(MoveTemp(Args), MoveTemp(OnCompleted));
}

void FIoStoreOnDemandModule::Install(
	FOnDemandInstallArgs&& Args,
	FOnDemandInstallCompleted&& OnCompleted,
	FOnDemandInstallProgressed&& OnProgress /*= nullptr*/,
	const FOnDemandCancellationToken* CancellationToken)
{
	if (IoStore.IsValid() == false)
	{
		IoStore = MakeShared<FOnDemandIoStore>();
		if (FIoStatus Status = IoStore->Initialize(); !Status.IsOk())
		{
			UE_LOG(LogIas, Error, TEXT("Failed to initialize I/O store on-demand, reason '%s'"), *Status.ToString());
			IoStore.Reset();
			return OnCompleted(FOnDemandInstallResult{ .Status = Status });
		}
	}

	IoStore->Install(MoveTemp(Args), MoveTemp(OnCompleted), MoveTemp(OnProgress), CancellationToken);
}

void FIoStoreOnDemandModule::Purge(FOnDemandPurgeArgs&& Args, FOnDemandPurgeCompleted&& OnCompleted)
{
	if (IoStore.IsValid() == false)
	{
		IoStore = MakeShared<FOnDemandIoStore>();
		if (FIoStatus Status = IoStore->Initialize(); !Status.IsOk())
		{
			UE_LOG(LogIas, Error, TEXT("Failed to initialize I/O store on-demand, reason '%s'"), *Status.ToString());
			IoStore.Reset();
			return OnCompleted(FOnDemandPurgeResult{ .Status = Status });
		}
	}

	IoStore->Purge(MoveTemp(Args), MoveTemp(OnCompleted));
}

FIoStatus FIoStoreOnDemandModule::Unmount(FStringView MountId)
{
	if (IoStore.IsValid())
	{
		return IoStore->Unmount(MountId);
	}
	return FIoStatus(EIoErrorCode::InvalidCode, NotInitializedError);
}

TIoStatusOr<uint64> FIoStoreOnDemandModule::GetInstallSize(const FOnDemandGetInstallSizeArgs& Args) const
{
	if (IoStore)
	{
		return IoStore->GetInstallSize(Args);
	}

	return FIoStatus(EIoErrorCode::InvalidCode, NotInitializedError);
}

FIoStatus FIoStoreOnDemandModule::GetInstallSizesByMountId(const FOnDemandGetInstallSizeArgs& Args, TMap<FString, uint64>& OutSizesByMountId) const
{
	if (IoStore)
	{
		return IoStore->GetInstallSizesByMountId(Args, OutSizesByMountId);
	}

	return FIoStatus(EIoErrorCode::InvalidCode, NotInitializedError);
}

TIoStatusOr<FOnDemandCacheUsage> FIoStoreOnDemandModule::GetCacheUsage() const
{
	if (IoStore)
	{
		return IoStore->GetCacheUsage();
	}

	return FIoStatus(EIoErrorCode::InvalidCode, NotInitializedError);
}

void FIoStoreOnDemandModule::InitializeInternal()
{
	LLM_SCOPE_BYTAG(Ias);

#if WITH_EDITOR
	bool bEnabledInEditor = false;
	GConfig->GetBool(TEXT("Ias"), TEXT("EnableInEditor"), bEnabledInEditor, GEngineIni);

	if (!bEnabledInEditor)
	{
		return;
	}
#endif //WITH_EDITOR

	const TCHAR* CommandLine = FCommandLine::Get();
	
#if !UE_BUILD_SHIPPING
	if (FParse::Param(CommandLine, TEXT("NoIas")))
	{
		return;
	}
#endif

	if (IoStore.IsValid() == false)
	{
		IoStore = MakeShared<FOnDemandIoStore>();
		if (FIoStatus Status = IoStore->Initialize(); !Status.IsOk())
		{
			UE_LOG(LogIas, Error, TEXT("Failed to initialize I/O store on demand, reason '%s'"), *Status.ToString());
			return;
		}
	}

	LoadCaCerts();

	// Make sure we haven't called initialize before
	check(!HttpIoDispatcherBackend.IsValid());

	FOnDemandEndpointConfig EndpointConfig;
	if (TryParseEndpointConfig(CommandLine, EndpointConfig) == false)
	{
		return;
	}

	{
		FString EncryptionKey;
		if (FParse::Value(CommandLine, TEXT("Ias.EncryptionKey="), EncryptionKey))
		{
			ApplyEncryptionKeyFromString(EncryptionKey);
		}
	}

	TUniquePtr<IIasCache> Cache;
	FIasCacheConfig CacheConfig = GetIasCacheConfig(CommandLine);

	CacheConfig.DropCache = DeferredAbandonCache.Get(CacheConfig.DropCache);
	if (CacheConfig.DiskQuota > 0)
	{
		if (FPaths::HasProjectPersistentDownloadDir())
		{
			FString CacheDir = FPaths::ProjectPersistentDownloadDir();
			Cache = MakeIasCache(*CacheDir, CacheConfig);

			UE_CLOG(!Cache.IsValid(), LogIas, Warning, TEXT("File cache disabled - streaming only (init-fail)"));
		}
		else
		{
			UE_LOG(LogIas, Warning, TEXT("File cache disabled - streaming only (project has no persistent download dir enabled for this platform)"));
		}
	}
	else
	{
		UE_LOG(LogIas, Log, TEXT("File cache disabled - streaming only (zero-quota)"));
	}

	HttpIoDispatcherBackend = MakeOnDemandIoDispatcherBackend(EndpointConfig, *IoStore, MoveTemp(Cache));

	int32 BackendPriority = -10;
#if !UE_BUILD_SHIPPING
	if (FParse::Param(CommandLine, TEXT("Ias")))
	{
		// Bump the priority to be higher then the file system backend
		BackendPriority = 10;
	}
#endif

	// Setup any states changes issued before initialization
	if (DeferredEnabled.IsSet())
	{
		HttpIoDispatcherBackend->SetEnabled(*DeferredEnabled);
	}
	if (DeferredBulkOptionalEnabled.IsSet())
	{
		HttpIoDispatcherBackend->SetBulkOptionalEnabled(*DeferredBulkOptionalEnabled);
	}
	
	FIoDispatcher::Get().Mount(HttpIoDispatcherBackend.ToSharedRef(), BackendPriority);

	bool bUsePerContainerTocsConfigValue = false;
	if (GConfig)
	{
		GConfig->GetBool(TEXT("Ias"), TEXT("UsePerContainerTocs"), bUsePerContainerTocsConfigValue, GEngineIni);
	}
	bool bUsePerContainerTocsParam = false;
#if !UE_BUILD_SHIPPING
	bUsePerContainerTocsParam = FParse::Param(CommandLine, TEXT("Ias.UsePerContainerTocs"));
#endif

	const bool bUsePerContainerTocs = bUsePerContainerTocsConfigValue || bUsePerContainerTocsParam;
	UE_LOG(LogIas, Log, TEXT("Using per container TOCs=%s"), bUsePerContainerTocs ? TEXT("True") : TEXT("False"));

	TOptional<FOnDemandMountArgs> MountArgs;
	if (EndpointConfig.TocFilePath.IsEmpty() == false)
	{
		if (bUsePerContainerTocs == false)
		{
			MountArgs.Emplace(FOnDemandMountArgs
			{
				.MountId = EndpointConfig.TocFilePath,
				.FilePath = EndpointConfig.TocFilePath,
				.Options = EOnDemandMountOptions::StreamOnDemand
			});
		}
	}
	else if (!EndpointConfig.ServiceUrls.IsEmpty() && !EndpointConfig.TocPath.IsEmpty())
	{
		const FString TocUrl = EndpointConfig.ServiceUrls[0] / EndpointConfig.TocPath;
		MountArgs.Emplace(FOnDemandMountArgs
		{
			.MountId = TocUrl,
			.Url = TocUrl,
			.Options = EOnDemandMountOptions::StreamOnDemand
		});
	}

#if !UE_BUILD_SHIPPING
	TOptional<FOnDemandInstallArgs> InstallArgs;
	if (FParse::Param(FCommandLine::Get(), TEXT("Iad")))
	{
		if (MountArgs)
		{
			MountArgs.GetValue().Options = EOnDemandMountOptions::InstallOnDemand;
			MountArgs.GetValue().Url = EndpointConfig.ServiceUrls[0] / EndpointConfig.TocPath;

			static FOnDemandContentHandle ContentHandle = FOnDemandContentHandle::Create(TEXT("AllContent"));
			InstallArgs.Emplace();
			InstallArgs->Url = EndpointConfig.ServiceUrls[0] / EndpointConfig.TocPath;
			InstallArgs->MountId = MountArgs.GetValue().MountId;
			InstallArgs->ContentHandle = ContentHandle;
		}
	}
#endif

	if (MountArgs)
	{
		IoStore->Mount(
			MoveTemp(MountArgs.GetValue()),
			[](FOnDemandMountResult MountResult)
			{
				UE_CLOG(!MountResult.Status.IsOk(), LogIas, Error,
					TEXT("Failed to mount TOC for '%s', reason '%s'"), *MountResult.MountId, *MountResult.Status.ToString());
			});
#if !UE_BUILD_SHIPPING
		if (InstallArgs)
		{
			IoStore->Install(
				MoveTemp(InstallArgs.GetValue()),
				[](FOnDemandInstallResult InstallResult)
				{
					UE_CLOG(!InstallResult.Status.IsOk(), LogIoStoreOnDemand, Error,
						TEXT("Failed to install content, reason '%s'"), *InstallResult.Status.ToString());
				});
		}
#endif
	}
}
	
void FIoStoreOnDemandModule::StartupModule()
{
#if PLATFORM_WINDOWS
	{
		WSADATA WsaData;
		int Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
		if (Result == 0)
		{
			bPlatformSpecificSetup = true;
		}
		else
		{
			TCHAR SystemErrorMsg[MAX_SPRINTF] = { 0 };
			FPlatformMisc::GetSystemErrorMessage(SystemErrorMsg, sizeof(SystemErrorMsg), Result);

			UE_LOG(LogIas, Error, TEXT("WSAStartup failed due to: %s (%d)"), SystemErrorMsg, Result);
		}
	}
#endif //PLATFORM_WINDOWS

#if !UE_IAS_CUSTOM_INITIALIZATION
	InitializeInternal();
#endif // !UE_IAS_CUSTOM_INITIALIZATION
}

void FIoStoreOnDemandModule::ShutdownModule()
{
#if PLATFORM_WINDOWS
	if (bPlatformSpecificSetup)
	{
		if (WSACleanup() != 0)
		{
			const uint32 SystemError = FPlatformMisc::GetLastError();

			TCHAR SystemErrorMsg[MAX_SPRINTF] = { 0 };
			FPlatformMisc::GetSystemErrorMessage(SystemErrorMsg, sizeof(SystemErrorMsg), SystemError);

			UE_LOG(LogIas, Error, TEXT("WSACleanup failed due to: %s (%u)"), SystemErrorMsg, SystemError);
		}

		bPlatformSpecificSetup = false;
	}
#endif //PLATFORM_WINDOWS
}

#if UE_IAS_CUSTOM_INITIALIZATION

EOnDemandInitResult FIoStoreOnDemandModule::Initialize()
{
	InitializeInternal();

	return HttpIoDispatcherBackend.IsValid() ? EOnDemandInitResult::Success : EOnDemandInitResult::Disabled;
};

#endif // UE_IAS_CUSTOM_INITIALIZATION

} // namespace UE::IoStore

////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(UE::IoStore::FIoStoreOnDemandModule, IoStoreOnDemand);
