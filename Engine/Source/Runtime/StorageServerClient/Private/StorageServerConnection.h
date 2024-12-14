// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Compression/CompressedBuffer.h"
#include "Containers/UnrealString.h"
#include "Containers/AnsiString.h"
#include "Misc/StringBuilder.h"
#include "Memory/MemoryFwd.h"
#include "Templates/SharedPointer.h"
#include "SocketTypes.h"
#include "HAL/PlatformTime.h"
#include "StorageServerHttpClient.h"
#include "IO/IoChunkId.h"
#include "IStorageServerPlatformFile.h"

#if !UE_BUILD_SHIPPING

DECLARE_LOG_CATEGORY_EXTERN(LogStorageServerConnection, Log, All);

struct FPackageStoreEntryResource;

class FStorageServerConnection
{
public:
	FStorageServerConnection() = default;
	~FStorageServerConnection() = default;

	bool Initialize(TArrayView<const FString> HostAddresses, const int32 Port, const FAnsiStringView& InBaseURI);

	void PackageStoreRequest(TFunctionRef<void(FPackageStoreEntryResource&&)> Callback);
	void FileManifestRequest(TFunctionRef<void(FIoChunkId Id, FStringView Path, int64 RawSize)> Callback);
	int64 ChunkSizeRequest(const FIoChunkId& ChunkId);
	TIoStatusOr<FIoBuffer> ReadChunkRequest(
		const FIoChunkId& ChunkId,
		const uint64 Offset,
		const uint64 Size,
		const TOptional<FIoBuffer> OptDestination,
		const bool bHardwareTargetBuffer
	);
	void ReadChunkRequestAsync(
		const FIoChunkId& ChunkId,
		const uint64 Offset,
		const uint64 Size,
		const TOptional<FIoBuffer> OptDestination,
		const bool bHardwareTargetBuffer,
		TFunctionRef<void(TIoStatusOr<FIoBuffer> Data)> OnResponse
	);

	FStringView GetHostAddr() const
	{
		return CurrentHostAddr;
	}

	void GetAndResetStats(IStorageServerPlatformFile::FConnectionStats& OutStats);

private:
	TUniquePtr<IStorageServerHttpClient> HttpClient;
	FAnsiString BaseURI;
	FString CurrentHostAddr;

	// Stats
	std::atomic<uint64> AccumulatedBytes = 0;
	std::atomic<uint32> RequestCount = 0;
	std::atomic<double> MinRequestThroughput = DBL_MAX;
	std::atomic<double> MaxRequestThroughput = -DBL_MAX;

	TArray<FString> SortHostAddressesByLocalSubnet(TArrayView<const FString> HostAddresses, const int32 Port);
	static bool IsPlatformSocketAddress(const FString Address); 
	TUniquePtr<IStorageServerHttpClient> CreateHttpClient(const FString Address, const int32 Port);
	TSharedPtr<FInternetAddr> StringToInternetAddr(const FString Address, const int32 Port);
	bool HandshakeRequest();
	void BuildReadChunkRequestUrl(FAnsiStringBuilderBase& Builder, const FIoChunkId& ChunkId, const uint64 Offset, const uint64 Size);
	static TIoStatusOr<FIoBuffer> ReadChunkRequestProcessHttpResult(
		IStorageServerHttpClient::FResult ResultTuple,
		const uint64 Offset,
		const uint64 Size,
		const TOptional<FIoBuffer> OptDestination,
		const bool bHardwareTargetBuffer
	);
	static uint64 GetCompressedOffset(const FCompressedBuffer& Buffer, uint64 RawOffset);
	void AddTimingInstance(const double Duration, const uint64 Bytes);
};

#endif
