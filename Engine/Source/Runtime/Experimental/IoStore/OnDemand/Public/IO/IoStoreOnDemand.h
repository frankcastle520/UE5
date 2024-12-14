// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Containers/StringFwd.h"
#include "Containers/SharedString.h"
#include "IO/IoChunkId.h"
#include "IO/IoContainerId.h"
#include "IO/IoHash.h"
#include "IO/PackageId.h"
#include "IO/IoStatus.h"
#include "Misc/EnumClassFlags.h"
#include "Misc/Guid.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"

#include <atomic>

#if (IS_PROGRAM || WITH_EDITOR)
#include "Containers/Map.h"
#include "Misc/AES.h"
#endif // (IS_PROGRAM || WITH_EDITOR)

#define UE_API IOSTOREONDEMAND_API

class FArchive;
class FCbFieldView;
class FCbWriter;
struct FKeyChain;
struct FAnalyticsEventAttribute;
struct FIoContainerSettings;
struct FIoStoreWriterSettings;
namespace UE::IoStore { struct FOnDemandEndpoint; }
namespace UE::IoStore { class FOnDemandInternalContentHandle; }
namespace UE::IoStore { class FOnDemandIoStore; }
namespace UE::IoStore { class IOnDemandIoDispatcherBackend; }
using FIoBlockHash = uint32;

// Custom initialization allows users to control when
// the system should be initialized.
#if !defined(UE_IAS_CUSTOM_INITIALIZATION)
	#define UE_IAS_CUSTOM_INITIALIZATION 0
#endif

UE_API DECLARE_LOG_CATEGORY_EXTERN(LogIoStoreOnDemand, Log, All);
UE_API DECLARE_LOG_CATEGORY_EXTERN(LogIas, Log, All);

namespace UE::IoStore
{

////////////////////////////////////////////////////////////////////////////////

int64 ParseSizeParam(FStringView Value);

////////////////////////////////////////////////////////////////////////////////

bool TryParseConfigFile(const FString& ConfigPath, FOnDemandEndpoint& OutEndpoint);

////////////////////////////////////////////////////////////////////////////////
enum class EOnDemandTocVersion : uint32
{
	Invalid			= 0,
	Initial			= 1,
	UTocHash		= 2,
	BlockHash32		= 3,
	NoRawHash		= 4,
	Meta			= 5,
	ContainerId		= 6,
	AdditionalFiles	= 7,
	TagSets			= 8,
	ContainerFlags	= 9,

	LatestPlusOne,
	Latest			= (LatestPlusOne - 1)
};

enum class EOnDemandChunkVersion : uint32
{
	Invalid			= 0,
	Initial			= 1,

	LatestPlusOne,
	Latest			= (LatestPlusOne - 1)
};

struct FTocMeta
{
	int64 EpochTimestamp = 0;
	FString BuildVersion;
	FString TargetPlatform;

	UE_API friend FArchive& operator<<(FArchive& Ar, FTocMeta& Meta);
	UE_API friend FCbWriter& operator<<(FCbWriter& Writer, const FTocMeta& Meta);
};

UE_API bool LoadFromCompactBinary(FCbFieldView Field, FTocMeta& OutMeta);

struct FOnDemandTocHeader
{
	static constexpr uint64 ExpectedMagic = 0x6f6e64656d616e64; // ondemand

	uint64 Magic = ExpectedMagic;
	uint32 Version = uint32(EOnDemandTocVersion::Latest);
	uint32 ChunkVersion = uint32(EOnDemandChunkVersion::Latest);
	uint32 BlockSize = 0;
	FString CompressionFormat;
	FString ChunksDirectory;
	
	UE_API friend FArchive& operator<<(FArchive& Ar, FOnDemandTocHeader& Header);
	UE_API friend FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocHeader& Header);
};

UE_API bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocHeader& OutTocHeader);

struct FOnDemandTocEntry
{
	FIoHash Hash = FIoHash::Zero;
	FIoChunkId ChunkId = FIoChunkId::InvalidChunkId;
	uint64 RawSize = 0;
	uint64 EncodedSize = 0;
	uint32 BlockOffset = ~uint32(0);
	uint32 BlockCount = 0; 
	
	UE_API friend FArchive& operator<<(FArchive& Ar, FOnDemandTocEntry& Entry);
	UE_API friend FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocEntry& Entry);
};

UE_API bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocEntry& OutTocEntry);

struct FOnDemandTocContainerEntry
{
	FIoContainerId ContainerId;
	FString ContainerName;
	FString EncryptionKeyGuid;
	TArray<FOnDemandTocEntry> Entries;
	TArray<uint32> BlockSizes;
	TArray<FIoBlockHash> BlockHashes;

	/** Hash of the .utoc file (on disk) used to generate this data */
	FIoHash UTocHash;
	uint8 ContainerFlags = 0;

	UE_API friend FArchive& operator<<(FArchive& Ar, FOnDemandTocContainerEntry& ContainerEntry);
	UE_API friend FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocContainerEntry& ContainerEntry);
};

UE_API bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocContainerEntry& OutContainer);

struct FOnDemandTocSentinel
{
public:
	static constexpr inline char SentinelImg[] = "-[]--[]--[]--[]-";
	static constexpr uint32 SentinelSize = 16;

	bool IsValid();

	UE_API friend FArchive& operator<<(FArchive& Ar, FOnDemandTocSentinel& Sentinel);

private:
	uint8 Data[SentinelSize] = { 0 };
};

struct FOnDemandTocAdditionalFile
{
	FIoHash Hash;
	FString Filename;
	uint64 FileSize = 0;

	UE_API friend FArchive& operator<<(FArchive& Ar, FOnDemandTocAdditionalFile& AdditionalFile);
	UE_API friend FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocAdditionalFile& AdditionalFile);
};

UE_API bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocAdditionalFile& AdditionalFile);

struct FOnDemandTocTagSetPackageList
{
	uint32 ContainerIndex = 0;
	TArray<uint32> PackageIndicies;

	UE_API friend FArchive& operator<<(FArchive& Ar, FOnDemandTocTagSetPackageList& TagSet);
	UE_API friend FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocTagSetPackageList& TagSet);
};

UE_API bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocTagSetPackageList& TagSet);

struct FOnDemandTocTagSet
{
	FString Tag;
	TArray<FOnDemandTocTagSetPackageList> Packages;

	UE_API friend FArchive& operator<<(FArchive& Ar, FOnDemandTocTagSet& TagSet);
	UE_API friend FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocTagSet& TagSet);
};

UE_API bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocTagSet& TagSet);

struct FOnDemandToc
{
	FOnDemandToc() = default;
	~FOnDemandToc() = default;

	FOnDemandToc(FOnDemandToc&&) = default;
	FOnDemandToc& operator= (FOnDemandToc&&) = default;

	// Copying this structure would be quite expensive so we want to make sure that it doesn't happen.

	FOnDemandToc(const FOnDemandToc&) = delete;
	FOnDemandToc&  operator= (const FOnDemandToc&) = delete;

	FOnDemandTocHeader Header;
	FTocMeta Meta;
	TArray<FOnDemandTocContainerEntry> Containers;
	TArray<FOnDemandTocAdditionalFile> AdditionalFiles;
	TArray<FOnDemandTocTagSet> TagSets;

	UE_API friend FArchive& operator<<(FArchive& Ar, FOnDemandToc& Toc);
	UE_API friend FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandToc& Toc);

	static FGuid VersionGuid;

	UE_API static TIoStatusOr<FOnDemandToc> LoadFromFile(const FString& FilePath, bool bValidate);
	UE_API static TIoStatusOr<FOnDemandToc> LoadFromUrl(FAnsiStringView Url, uint32 RetryCount = 0, bool bFollowRedirects = false);
	UE_API static TIoStatusOr<FOnDemandToc> LoadFromUrl(FStringView Url, uint32 RetryCount = 0, bool bFollowRedirects = false);
};

UE_API bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandToc& OutToc);

#if UE_IAS_CUSTOM_INITIALIZATION

/** Result of calling FIoStoreOnDemandModule::Initialize */
enum class EOnDemandInitResult
{
	/** The module initialized correctly and can be used */
	Success = 0,
	/** The module is disabled as OnDemand data is not required for the current process*/
	Disabled,
	/** The module was unable to start up correctly due to an unexpected error */
	Error,
};

#endif // UE_IAS_CUSTOM_INITIALIZATION

/**
 * Keeps referenced data pinned in the cache until released.
 */
class FOnDemandContentHandle
{
public:
	/** Creates a new invalid content handle. */ 
	UE_API FOnDemandContentHandle();
	/** Destroy the handle and release any referenced content. */ 
	UE_API ~FOnDemandContentHandle();
	/** Destroy the handle and release any referenced content. */ 
	void Reset() { Handle.Reset(); }
	/** Returns whether the handle is valid. */
	bool IsValid() const { return Handle.IsValid(); }
	/** Create a new content handle .*/
	UE_API static FOnDemandContentHandle Create();
	/** Create a new content handle with a debug name. */
	UE_API static FOnDemandContentHandle Create(FSharedString DebugName);
	/** Create a new content handle with a debug name. */
	UE_API static FOnDemandContentHandle Create(FStringView DebugName);
	/** Returns a string representing the content handle. */
	UE_API friend FString LexToString(const FOnDemandContentHandle& Handle);

private:
	friend class FOnDemandIoStore;
	TSharedPtr<FOnDemandInternalContentHandle, ESPMode::ThreadSafe> Handle;
};

/** Options for controlling the behavior of mounted container(s). */
enum class EOnDemandMountOptions
{
	/** Mount containers with the purpose of streaming the content on-demand. */
	StreamOnDemand			= 1 << 0,
	/** Mount containers with the purpose of installing/downloading the content on-demand. */
	InstallOnDemand			= 1 << 1,
	/** Trigger callback on game thread. */
	CallbackOnGameThread	= 1 << 2,
};
ENUM_CLASS_FLAGS(EOnDemandMountOptions);

/** Arguments for mounting on-demand container TOC(s). */
struct FOnDemandMountArgs
{
	/** Mount an already serialized TOC. */
	TUniquePtr<FOnDemandToc> Toc;
	/** Mandatory ID to be used for unmounting all container file(s) included in the TOC. */
	FString MountId;
	/** Download the TOC from the specified URL. */
	FString Url;
	/** Serialize the TOC from the specified file path. */
	FString FilePath;
	/** Mount options. */
	EOnDemandMountOptions Options = EOnDemandMountOptions::StreamOnDemand;
};

/** Holds information about a mount request. */
struct FOnDemandMountResult
{
	/** The mount ID used for mounting the container(s). */
	FString MountId;
	/** The status of the mount request. */
	FIoStatus Status;
	/** Duration in seconds. */
	double DurationInSeconds = 0.0;
};

/** Mount completion callback. */
using FOnDemandMountCompleted = TUniqueFunction<void(FOnDemandMountResult)>;

/** Options for controlling the behavior of the install request. */
enum class EOnDemandInstallOptions
{
	/** No additional options. */
	None				 = 0,
	/** Trigger callback on game thread. */
	CallbackOnGameThread = 1 << 0,
};
ENUM_CLASS_FLAGS(EOnDemandInstallOptions);

/** Arguments for installing/downloading on-demand content. */
struct FOnDemandInstallArgs 
{
	/** Install all content from containers matching this mount ID. */
	FString MountId;
	/** Install content matching a set of tag(s) and optionally the mount ID. */
	TArray<FString> TagSets;
	/** Package ID's to install. */
	TArray<FPackageId> PackageIds;
	/** URL from where to download the chunks. */
	FString Url;
	/** Content handle. */
	FOnDemandContentHandle ContentHandle;
	/** Install options. */
	EOnDemandInstallOptions Options = EOnDemandInstallOptions::None;
};

/** Holds information about progress for an install request. */
struct FOnDemandInstallProgress
{
	/** The total size of the requested content. */
	uint64 TotalContentSize = 0;
	/** The total size to be installed/downloaded (<= TotalContentSize). */
	uint64 TotalInstallSize = 0;
	/** The size currently installed/downloaded (<= TotalInstallSize). */
	uint64 CurrentInstallSize = 0;


	FORCEINLINE uint64 GetTotalDownloadSize() const
	{
		return TotalInstallSize;
	}

	FORCEINLINE uint64 GetAlreadyDownloadedSize() const
	{
		return CurrentInstallSize;
	}

	FORCEINLINE float GetProgress() const 
	{
		return float(GetTotalDownloadSize()) / float(GetAlreadyDownloadedSize());
	}

	FORCEINLINE uint64 GetCachedSize() const 
	{
		return TotalContentSize - TotalInstallSize;
	}

	FORCEINLINE uint64 GetTotalSize() const
	{
		return TotalContentSize;
	}
};

/** Install Progress callback. */
using FOnDemandInstallProgressed = TFunction<void(FOnDemandInstallProgress)>;

/** Holds information about an install request. */
struct FOnDemandInstallResult
{
	/** The status of the install request. */
	FIoStatus Status;
	/** Duration in seconds. */
	double DurationInSeconds = 0.0;
	/** Final progress for the install request. */
	FOnDemandInstallProgress Progress;
};

/** Install completion callback. */
using FOnDemandInstallCompleted = TUniqueFunction<void(FOnDemandInstallResult)>;

/** Arguments for purging on-demand content. */
struct FOnDemandPurgeArgs
{
	/** Purge options. */
	EOnDemandInstallOptions Options = EOnDemandInstallOptions::None;
};

/** Holds information about a purge request */
struct FOnDemandPurgeResult
{
	/** The status of the purge request. */
	FIoStatus Status;
	/** Duration in seconds. */
	double DurationInSeconds = 0.0;
};

/** Purge completion callback */
using FOnDemandPurgeCompleted = TUniqueFunction<void(FOnDemandPurgeResult)>;

/** Arguments for getting the size of on-demand content. */
struct FOnDemandGetInstallSizeArgs 
{
	/** Get total install size for containers matching this mount ID. */
	FString MountId;
	/** Get total install size for the specified tag(s) and optionally matching the mount ID. */
	TArray<FString> TagSets;
	/** Get total intall size for the specified package IDs. */
	TArray<FPackageId> PackageIds;
};

/** Holds information about cache usage */
struct FOnDemandCacheUsage
{
	uint64 MaxSize = 0;
	uint64 TotalSize = 0;
	uint64 ReferencedBlockSize = 0;
};

/** Token used for signalling an operation to be cancelled. */
class FOnDemandCancellationToken
{
public:
	UE_NONCOPYABLE(FOnDemandCancellationToken);
	FOnDemandCancellationToken() = default;

	/** Signal the operation to be cancelled. */
	void Cancel()
	{
		bCanceled = true;
	}

	/** Returns whether an operation should be cancelled. */
	bool IsCanceled() const
	{
		return bCanceled;
	}

private:
	std::atomic<bool> bCanceled{ false };
};

class FIoStoreOnDemandModule
	: public IModuleInterface
{
private:
	void InitializeInternal();
	TSharedPtr<IOnDemandIoDispatcherBackend> HttpIoDispatcherBackend;
	// Deferred state requests if called before backend
	// is initialized
	TOptional<bool> DeferredEnabled;
	TOptional<bool> DeferredAbandonCache;
	TOptional<bool> DeferredBulkOptionalEnabled;
	TSharedPtr<FOnDemandIoStore, ESPMode::ThreadSafe> IoStore;

	/** Indicates that platform specific setup was invoked when the module was started and needs to be cleaned up on shutdown */
	bool bPlatformSpecificSetup = false;

public:
	UE_API void SetBulkOptionalEnabled(bool bInEnabled);
	UE_API void SetEnabled(bool bInEnabled);
	UE_API bool IsEnabled() const;
	UE_API void AbandonCache();

	UE_API void ReportAnalytics(TArray<FAnalyticsEventAttribute>& OutAnalyticsArray) const;

	UE_API void Mount(FOnDemandMountArgs&& Args, FOnDemandMountCompleted&& OnCompleted);
	UE_API FIoStatus Unmount(FStringView MountId);

	UE_API void Install(
		FOnDemandInstallArgs&& Args,
		FOnDemandInstallCompleted&& OnCompleted,
		FOnDemandInstallProgressed&& OnProgress = nullptr,
		const FOnDemandCancellationToken* CancellationToken = nullptr);

	UE_API void Purge(FOnDemandPurgeArgs&& Args, FOnDemandPurgeCompleted&& OnCompleted);

	UE_API TIoStatusOr<uint64> GetInstallSize(const FOnDemandGetInstallSizeArgs& Args) const;

	UE_API FIoStatus GetInstallSizesByMountId(const FOnDemandGetInstallSizeArgs& Args, TMap<FString, uint64>& OutSizesByMountId) const;

	/** This does not queue a request so the returned state may not be completely consistent
	  * if there are requests currently being processed. 
	  * This should only be used for purposes such as debugging telemetry. 
	  */
	UE_API TIoStatusOr<FOnDemandCacheUsage> GetCacheUsage() const;

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
#if UE_IAS_CUSTOM_INITIALIZATION
	UE_API EOnDemandInitResult Initialize();
#endif //UE_IAS_CUSTOM_INITIALIZATION
};

} // namespace UE::IoStore

#undef UE_API
