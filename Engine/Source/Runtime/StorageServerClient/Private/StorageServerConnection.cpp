// Copyright Epic Games, Inc. All Rights Reserved.

#include "StorageServerConnection.h"

#include "IO/IoDispatcher.h"
#include "IPAddress.h"
#include "Misc/App.h"
#include "Misc/ScopeLock.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/StringBuilder.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Memory/MemoryView.h"
#include "Memory/SharedBuffer.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "SocketSubsystem.h"
#include "IO/PackageStore.h"
#include "BuiltInHttpClient/BuiltInHttpClient.h"
#include "BuiltInHttpClient/BuiltInHttpClientFSocket.h"
#include "BuiltInHttpClient/BuiltInHttpClientPlatformSocket.h"
#include "HAL/PlatformMath.h"

#if !UE_BUILD_SHIPPING

DEFINE_LOG_CATEGORY(LogStorageServerConnection);

TRACE_DECLARE_INT_COUNTER(ZenHttpClientSerializedBytes, TEXT("ZenClient/SerializedBytes (compressed)"));
TRACE_DECLARE_INT_COUNTER(ZenHttpClientThroughputBytes, TEXT("ZenClient/ThroughputBytes (decompressed)"));

bool FStorageServerConnection::Initialize(TArrayView<const FString> HostAddresses, const int32 Port, const FAnsiStringView& InBaseURI)
{
	BaseURI = InBaseURI;
	TArray<FString> SortedHostAddresses = SortHostAddressesByLocalSubnet(HostAddresses, Port);

	for (const FString& HostAddress : SortedHostAddresses)
	{
		HttpClient = CreateHttpClient(HostAddress, Port);
		CurrentHostAddr = HostAddress;
		if (HandshakeRequest())
		{
			return true;
		}
	}

	HttpClient.Reset();
	return false;
}

void FStorageServerConnection::PackageStoreRequest(TFunctionRef<void(FPackageStoreEntryResource&&)> Callback)
{
	TAnsiStringBuilder<256> ResourceBuilder;
	ResourceBuilder.Append(BaseURI).Append("/entries?fieldfilter=packagestoreentry");

	IStorageServerHttpClient::FResult ResultTuple = HttpClient->RequestSync(*ResourceBuilder, EStorageServerContentType::CbObject);
	TIoStatusOr<FIoBuffer> Result = ResultTuple.Get<0>();
	if (Result.IsOk())
	{
		FMemoryReaderView Reader(Result.ValueOrDie().GetView());
		FCbObject ResponseObj = LoadCompactBinary(Reader).AsObject();
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(StorageServerPackageStoreRequestParseEntries);
			for (FCbField& OplogEntry : ResponseObj["entries"].AsArray())
			{
				FCbObject OplogObj = OplogEntry.AsObject();
				FPackageStoreEntryResource Entry = FPackageStoreEntryResource::FromCbObject(OplogObj["packagestoreentry"].AsObject());
				Callback(MoveTemp(Entry));
			}
		}
	}
	else
	{
		UE_LOG(LogStorageServerConnection, Fatal, TEXT("Failed to read oplog from storage server. '%s'"), *Result.Status().ToString());
	}
}

void FStorageServerConnection::FileManifestRequest(TFunctionRef<void(FIoChunkId Id, FStringView Path, int64 RawSize)> Callback)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStorageServerConnection::FileManifestRequest);

	TAnsiStringBuilder<256> ResourceBuilder;
	ResourceBuilder.Append(BaseURI).Append("/files?fieldnames=id,clientpath,rawsize");

	IStorageServerHttpClient::FResult ResultTuple = HttpClient->RequestSync(*ResourceBuilder, EStorageServerContentType::CbObject);
	TIoStatusOr<FIoBuffer> Result = ResultTuple.Get<0>();
	if (Result.IsOk())
	{
		FMemoryReaderView Reader(Result.ValueOrDie().GetView());
		FCbObject ResponseObj = LoadCompactBinary(Reader).AsObject();
		for (FCbField& FileArrayEntry : ResponseObj["files"].AsArray())
		{
			FCbObject Entry = FileArrayEntry.AsObject();
			FCbObjectId Id = Entry["id"].AsObjectId();
			int64 ResponseRawSize = Entry["rawsize"].AsInt64(-1);

			TStringBuilder<128> WidePath;
			WidePath.Append(FUTF8ToTCHAR(Entry["clientpath"].AsString()));

			FIoChunkId ChunkId;
			ChunkId.Set(Id.GetView());

			Callback(ChunkId, WidePath, ResponseRawSize);
		}
	}
	else
	{
		UE_LOG(LogStorageServerConnection, Fatal, TEXT("Failed to read file manifest from storage server. '%s'"), *Result.Status().ToString());
	}
}

int64 FStorageServerConnection::ChunkSizeRequest(const FIoChunkId& ChunkId)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStorageServerConnection::ChunkSizeRequest);

	TAnsiStringBuilder<256> ResourceBuilder;
	ResourceBuilder.Append(BaseURI);
	ResourceBuilder << "/" << ChunkId << "/info";

	const double StartTime = FPlatformTime::Seconds();
	IStorageServerHttpClient::FResult ResultTuple = HttpClient->RequestSync(*ResourceBuilder, EStorageServerContentType::CbObject);
	TIoStatusOr<FIoBuffer> Result = ResultTuple.Get<0>();
	if (Result.IsOk())
	{
		const double Duration = FPlatformTime::Seconds() - StartTime;
		AddTimingInstance(Duration, Result.ValueOrDie().GetSize());

		FMemoryReaderView Reader(Result.ValueOrDie().GetView());
		FCbObject ResponseObj = LoadCompactBinary(Reader).AsObject();
		const int64 ChunkSize = ResponseObj["size"].AsInt64(0);
		return ChunkSize;
	}
	else if (Result.Status().GetErrorCode() != EIoErrorCode::NotFound)
	{
		UE_LOG(LogStorageServerConnection, Fatal, TEXT("Failed to get chunk size from storage server. '%s'"), *Result.Status().ToString());
	}

	return -1;
}

TIoStatusOr<FIoBuffer> FStorageServerConnection::ReadChunkRequest(
	const FIoChunkId& ChunkId,
	const uint64 Offset,
	const uint64 Size,
	const TOptional<FIoBuffer> OptDestination,
	const bool bHardwareTargetBuffer
)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStorageServerConnection::ReadChunkRequest);

	const double StartTime = FPlatformTime::Seconds();

	TAnsiStringBuilder<256> ResourceBuilder;
	BuildReadChunkRequestUrl(ResourceBuilder, ChunkId, Offset, Size);

	IStorageServerHttpClient::FResult HttpResultTuple = HttpClient->RequestSync(*ResourceBuilder);
	TIoStatusOr<FIoBuffer> ResultBuffer = ReadChunkRequestProcessHttpResult(HttpResultTuple, Offset, Size, OptDestination, bHardwareTargetBuffer);

	if (ResultBuffer.IsOk())
	{
		const double Duration = FPlatformTime::Seconds() - StartTime;
		AddTimingInstance(Duration, ResultBuffer.ValueOrDie().GetSize());
	}

	return ResultBuffer;
}

void FStorageServerConnection::ReadChunkRequestAsync(
	const FIoChunkId& ChunkId,
	const uint64 Offset,
	const uint64 Size,
	const TOptional<FIoBuffer> OptDestination,
	const bool bHardwareTargetBuffer,
	TFunctionRef<void(TIoStatusOr<FIoBuffer> Data)> OnResponse
)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStorageServerConnection::ReadChunkRequestAsync);

	const double StartTime = FPlatformTime::Seconds();

	TAnsiStringBuilder<256> ResourceBuilder;
	BuildReadChunkRequestUrl(ResourceBuilder, ChunkId, Offset, Size);

	HttpClient->RequestAsync([this, Offset, Size, OptDestination, bHardwareTargetBuffer, OnResponse, StartTime](IStorageServerHttpClient::FResult HttpResultTuple)
	{
		TIoStatusOr<FIoBuffer> ResultBuffer = ReadChunkRequestProcessHttpResult(HttpResultTuple, Offset, Size, OptDestination, bHardwareTargetBuffer);

		if (ResultBuffer.IsOk())
		{
			const double Duration = FPlatformTime::Seconds() - StartTime;
			AddTimingInstance(Duration, ResultBuffer.ValueOrDie().GetSize());
		}

		OnResponse(ResultBuffer);
	}, *ResourceBuilder);
}

void FStorageServerConnection::GetAndResetStats(IStorageServerPlatformFile::FConnectionStats& OutStats)
{
	OutStats.AccumulatedBytes = AccumulatedBytes.exchange(0, std::memory_order_relaxed);
	OutStats.RequestCount = RequestCount.exchange(0, std::memory_order_relaxed);
	OutStats.MinRequestThroughput = MinRequestThroughput.exchange(DBL_MAX, std::memory_order_relaxed);
	OutStats.MaxRequestThroughput = MaxRequestThroughput.exchange(-DBL_MAX, std::memory_order_relaxed);
}

TArray<FString> FStorageServerConnection::SortHostAddressesByLocalSubnet(TArrayView<const FString> HostAddresses, const int32 Port)
{
	bool bAllArePlatformSocketAddresses = true;
	for (const FString& HostAddress : HostAddresses)
	{
		if (!IsPlatformSocketAddress(HostAddress))
		{
			bAllArePlatformSocketAddresses = false;
			break;
		}
	}

	// return array without sorting if it's 0 or 1 addresses or all of them are platform sockets
	if (HostAddresses.Num() <= 1 || bAllArePlatformSocketAddresses)
	{
		return TArray<FString>(HostAddresses);
	}

	TArray<FString> Result;
	ISocketSubsystem& SocketSubsystem = *ISocketSubsystem::Get();

	// Sorting logic in order:
	// - special platform socket address, see PlatformSocketAddress
	// - on desktop, if it's an IPV6 address loopback (ends with ":1")
	// - on desktop, if it's and IPV4 address loopback (starts with "127.0.0")
	// - host IPV4 subnet matches the client subnet (xxx.xxx.xxx)
	// - remaining addresses
	bool bCanBindAll = false;
	bool bAppendPort = false;
	TSharedPtr<FInternetAddr> localAddr = SocketSubsystem.GetLocalHostAddr(*GLog, bCanBindAll);
	FString localAddrStringSubnet = localAddr->ToString(bAppendPort);

	int32 localLastDotPos = INDEX_NONE;
	if (localAddrStringSubnet.FindLastChar(TEXT('.'), localLastDotPos))
	{
		localAddrStringSubnet = localAddrStringSubnet.LeftChop(localAddrStringSubnet.Len() - localLastDotPos);
	}

	TArray<FString> PlatformSocketAddress;
	TArray<FString> IPV6Loopback;
	TArray<FString> IPV4Loopback;
	TArray<FString> RegularAddresses;

	for (const FString& HostAddress : HostAddresses)
	{
		if (IsPlatformSocketAddress(HostAddress))
		{
			PlatformSocketAddress.Push(HostAddress);
			continue;
		}

		TSharedPtr<FInternetAddr> Addr = StringToInternetAddr(HostAddress, Port);

		if (!Addr)
		{
			continue;
		}

		FString tempAddrStringSubnet = Addr->ToString(bAppendPort);

#if PLATFORM_DESKTOP || PLATFORM_ANDROID
		if (Addr->GetProtocolType() == FNetworkProtocolTypes::IPv6)
		{
			if (tempAddrStringSubnet.EndsWith(":1"))
			{
				IPV6Loopback.Push(HostAddress);
				continue;
			}
		}
		else
		{
			if (tempAddrStringSubnet.StartsWith("127.0.0."))
			{
				IPV4Loopback.Push(HostAddress);
				continue;
			}
		}
#endif

		int32 LastDotPos = INDEX_NONE;
		if (tempAddrStringSubnet.FindLastChar(TEXT('.'), LastDotPos))
		{
			tempAddrStringSubnet = tempAddrStringSubnet.LeftChop(tempAddrStringSubnet.Len() - LastDotPos);
		}

		if (localAddrStringSubnet.Equals(tempAddrStringSubnet))
			RegularAddresses.Insert(HostAddress, 0);
		else
			RegularAddresses.Push(HostAddress);
	}

	for (const FString& PlatformAddr : PlatformSocketAddress)
	{
		Result.Push(PlatformAddr);
	}

	for (const FString& Addrv6lb : IPV6Loopback)
	{
		Result.Push(Addrv6lb);
	}

	for (const FString& Addrv4lb : IPV4Loopback)
	{
		Result.Push(Addrv4lb);
	}

	for (const FString& RegularAddr : RegularAddresses)
	{
		Result.Push(RegularAddr);
	}

	return Result;
}

bool FStorageServerConnection::IsPlatformSocketAddress(const FString Address)
{
	return Address.StartsWith(TEXT("platform://"));
}

TUniquePtr<IStorageServerHttpClient> FStorageServerConnection::CreateHttpClient(const FString Address, const int32 Port)
{
	TSharedPtr<FInternetAddr> Addr = StringToInternetAddr(Address, Port);

	// Use Address as Hostname if we can't resolve FInternetAddr
	FString HostName = Addr.IsValid() ? Addr->ToString(false) : Address;

	UE_LOG(LogStorageServerConnection, Display, TEXT("Creating zen store connection to %s:%i (\"%s\")."), *Address, Port, *HostName);

	TUniquePtr<IBuiltInHttpClientSocketPool> SocketPool;
	if (IsPlatformSocketAddress(Address))
	{
		SocketPool = MakeUnique<FBuiltInHttpClientPlatformSocketPool>(Address); 
	}
	else
	{
		SocketPool = MakeUnique<FBuiltInHttpClientFSocketPool>(Addr, *ISocketSubsystem::Get());
	}

	return MakeUnique<FBuiltInHttpClient>(MoveTemp(SocketPool), HostName);
}

TSharedPtr<FInternetAddr> FStorageServerConnection::StringToInternetAddr(const FString HostAddr, const int32 Port)
{
	TSharedPtr<FInternetAddr> Result = TSharedPtr<FInternetAddr>();

	if (IsPlatformSocketAddress(HostAddr))
	{
		return Result;
	}

	ISocketSubsystem& SocketSubsystem = *ISocketSubsystem::Get();

	// Numeric IPV6 addresses can be enclosed in brackets, and must have the brackets stripped before calling GetAddressFromString
	FString ModifiedHostAddr;
	const FString* EffectiveHostAddr = &HostAddr;
	if (!HostAddr.IsEmpty() && HostAddr[0] == TEXT('[') && HostAddr[HostAddr.Len() - 1] == TEXT(']'))
	{
#if PLATFORM_HAS_BSD_SOCKETS && !PLATFORM_HAS_BSD_IPV6_SOCKETS
		// If the platform doesn't have IPV6 BSD Sockets, then handle an attempt at conversion of loopback addresses, and skip and warn about other addresses
		if (HostAddr == TEXT("[::1]"))
		{
			// Substitute IPV4 loopback for IPV6 loopback
			ModifiedHostAddr = TEXT("127.0.0.1");
		}
		else
		{
			UE_LOG(LogStorageServerConnection, Warning, TEXT("Ignoring storage server host IPV6 address on platform that doesn't support IPV6: %s"), *HostAddr);
			return TSharedPtr<FInternetAddr>();
		}
#else
		ModifiedHostAddr = FStringView(HostAddr).Mid(1, HostAddr.Len() - 2);
#endif
		EffectiveHostAddr = &ModifiedHostAddr;
	}

	Result = SocketSubsystem.GetAddressFromString(*EffectiveHostAddr);
	if (!Result.IsValid() || !Result->IsValid())
	{
		FAddressInfoResult GAIRequest = SocketSubsystem.GetAddressInfo(**EffectiveHostAddr, nullptr, EAddressInfoFlags::Default, NAME_None);
		if (GAIRequest.ReturnCode == SE_NO_ERROR && GAIRequest.Results.Num() > 0)
		{
			Result = GAIRequest.Results[0].Address;
		}
	}

	if (Result.IsValid() && Result->IsValid())
	{
		Result->SetPort(Port);
	}

	return Result;
}

bool FStorageServerConnection::HandshakeRequest()
{
	TAnsiStringBuilder<256> ResourceBuilder;
	ResourceBuilder.Append(BaseURI);

	// Handshakes are done with a limited connection timeout so that we can find out if the destination is unreachable in a timely manner.
	const float ConnectionTimeoutSeconds = 5.0f;

	IStorageServerHttpClient::FResult ResultTuple = HttpClient->RequestSync(
		*ResourceBuilder,
		EStorageServerContentType::Unknown,
		"GET",
		TOptional<FIoBuffer>(),
		TOptional<FIoBuffer>(),
		ConnectionTimeoutSeconds,
		false
	);
	TIoStatusOr<FIoBuffer> Result = ResultTuple.Get<0>();
	if (Result.IsOk())
	{
		FMemoryReaderView Reader(Result.ValueOrDie().GetView());
		FCbObject ResponseObj = LoadCompactBinary(Reader).AsObject();
		// we currently don't have any concept of protocol versioning, if
		// we succeed in communicating with the endpoint we're good since
		// any breaking API change would need to be done in a backward
		// compatible manner
		return true;
	}

	return false;
}

void FStorageServerConnection::BuildReadChunkRequestUrl(FAnsiStringBuilderBase& Builder, const FIoChunkId& ChunkId, const uint64 Offset, const uint64 Size)
{
	Builder.Append(BaseURI) << "/" << ChunkId;
	bool HaveQuery = false;
	auto AppendQueryDelimiter = [&]
	{
		if (HaveQuery)
		{
			Builder.Append(ANSITEXTVIEW("&"));
		}
		else
		{
			Builder.Append(ANSITEXTVIEW("?"));
			HaveQuery = true;
		}
	};
	if (Offset)
	{
		AppendQueryDelimiter();
		Builder.Appendf("offset=%" UINT64_FMT, Offset);
	}
	if (Size != ~uint64(0))
	{
		AppendQueryDelimiter();
		Builder.Appendf("size=%" UINT64_FMT, Size);
	}
}

TIoStatusOr<FIoBuffer> FStorageServerConnection::ReadChunkRequestProcessHttpResult(
	IStorageServerHttpClient::FResult ResultTuple,
	const uint64 Offset,
	const uint64 Size,
	const TOptional<FIoBuffer> OptDestination,
	const bool bHardwareTargetBuffer
)
{
	TIoStatusOr<FIoBuffer> Result = ResultTuple.Get<0>();
	EStorageServerContentType MimeType = ResultTuple.Get<1>();
	if (!Result.IsOk())
	{
		UE_LOG(LogStorageServerConnection, Fatal, TEXT("Failed read chunk from storage server. '%s'  Offset:%ull  Size:%ull"), *Result.Status().ToString(), Offset, Size);
		return Result.Status();
	}

	FIoBuffer Buffer = Result.ValueOrDie();
	TRACE_COUNTER_ADD(ZenHttpClientSerializedBytes, Buffer.GetSize());

	if (MimeType == EStorageServerContentType::Binary)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FStorageServerConnection::ReadChunkRequest::Binary);

		if (OptDestination.IsSet())
		{
			ensure(OptDestination->GetSize() >= Buffer.GetSize());

			FIoBuffer Destination = OptDestination.GetValue();
			FMemory::Memcpy(Destination.GetData(), Buffer.GetData(), Buffer.GetSize());
			Destination.SetSize(Buffer.GetSize());
			return Destination;
		}
		else
		{
			return Buffer;
		}
	}
	else if (MimeType == EStorageServerContentType::CompressedBinary)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FStorageServerConnection::ReadChunkRequest::CompressedBinary);

		FMemoryReaderView Reader(Buffer.GetView());
		FCompressedBuffer CompressedBuffer = FCompressedBuffer::FromCompressed(FSharedBuffer::MakeView(Buffer.GetData(), Buffer.GetSize()));
		FCompressedBufferReader CompressedBufferReader(CompressedBuffer);
		const uint64 RawSize = CompressedBufferReader.GetRawSize();
		if (RawSize > 0)
		{
			const uint64 CompressedOffset = GetCompressedOffset(CompressedBuffer, Offset);
			const uint64 BytesToReadNonTrimmed = Size > 0 ? FMath::Min(Size, RawSize) : RawSize;
			const uint64 BytesToRead = FMath::Min(BytesToReadNonTrimmed, RawSize - CompressedOffset);

			ensure(!OptDestination.IsSet() || OptDestination->GetSize() >= BytesToRead);

			FIoBuffer OutChunk = OptDestination.IsSet() ? OptDestination.GetValue() : FIoBuffer(BytesToRead);
			OutChunk.SetSize(BytesToRead);

			if (CompressedBufferReader.TryDecompressTo(OutChunk.GetMutableView(), CompressedOffset, bHardwareTargetBuffer ? ECompressedBufferDecompressFlags::IntermediateBuffer : ECompressedBufferDecompressFlags::None))
			{
				return OutChunk;
			}
		}
	}

	return FIoStatus(EIoErrorCode::Unknown);
}

uint64 FStorageServerConnection::GetCompressedOffset(const FCompressedBuffer& Buffer, uint64 RawOffset)
{
	if (RawOffset > 0)
	{
		uint64 BlockSize = 0;
		ECompressedBufferCompressor Compressor;
		ECompressedBufferCompressionLevel CompressionLevel;
		const bool bOk = Buffer.TryGetCompressParameters(Compressor, CompressionLevel, BlockSize);
		check(bOk);

		return BlockSize > 0 ? RawOffset % BlockSize : 0;
	}

	return 0;
}

void FStorageServerConnection::AddTimingInstance(const double Duration, const uint64 Bytes)
{
	if ((Duration >= 0.0))
	{
		double tr = ((double)(Bytes * 8) / Duration) / 1000000.0; //Mbps

		AccumulatedBytes.fetch_add(Bytes, std::memory_order_relaxed);
		RequestCount.fetch_add(1, std::memory_order_relaxed);

		double MinTemp = MinRequestThroughput.load(std::memory_order_relaxed);
		while (!MinRequestThroughput.compare_exchange_weak(MinTemp, FMath::Min(MinTemp, tr), std::memory_order_relaxed))
		{
			MinTemp = MinRequestThroughput.load(std::memory_order_relaxed);
		}

		double MaxTemp = MaxRequestThroughput.load(std::memory_order_relaxed);
		while (!MaxRequestThroughput.compare_exchange_weak(MaxTemp, FMath::Max(MaxTemp, tr), std::memory_order_relaxed))
		{
			MaxTemp = MaxRequestThroughput.load(std::memory_order_relaxed);
		}
	}

	TRACE_COUNTER_ADD(ZenHttpClientThroughputBytes, Bytes);
}

// TODO revive FStorageServerChunkBatchRequest
#if 0

class FStorageServerChunkBatchRequest : private FStorageServerRequest
{
public:
	FStorageServerChunkBatchRequest& AddChunk(const FIoChunkId& ChunkId, int64 Offset, int64 Size);
	bool Issue(TFunctionRef<void(uint32 ChunkCount, uint32* ChunkIndices, uint64* ChunkSizes, FStorageServerResponse& ChunkDataStream)> OnResponse);

private:
	friend FStorageServerConnection;

	FStorageServerChunkBatchRequest(FStorageServerConnection& Owner, FAnsiStringView Resource, FAnsiStringView Hostname);

	FStorageServerConnection& Owner;
	int32 ChunkCountOffset = 0;
};

FStorageServerChunkBatchRequest::FStorageServerChunkBatchRequest(FStorageServerConnection& InOwner, FAnsiStringView Resource, FAnsiStringView Hostname)
	: FStorageServerRequest("POST", Resource, Hostname)
	, Owner(InOwner)
{
	uint32 Magic = 0xAAAA'77AC;
	uint32 ChunkCountPlaceHolder = 0;
	uint32 Reserved1 = 0;
	uint32 Reserved2 = 0;
	*this << Magic;
	ChunkCountOffset = BodyBuffer.Num();
	*this << ChunkCountPlaceHolder << Reserved1 << Reserved2;
}

FStorageServerChunkBatchRequest& FStorageServerChunkBatchRequest::AddChunk(const FIoChunkId& ChunkId, int64 Offset, int64 Size)
{
	uint32* ChunkCount = reinterpret_cast<uint32*>(BodyBuffer.GetData() + ChunkCountOffset);
	*this << const_cast<FIoChunkId&>(ChunkId) << *ChunkCount << Offset << Size;
	++(*ChunkCount);
	return *this;
}

bool FStorageServerChunkBatchRequest::Issue(TFunctionRef<void(uint32 ChunkCount, uint32* ChunkIndices, uint64* ChunkSizes, FStorageServerResponse& ChunkDataStream)> OnResponse)
{
	IStorageConnectionSocket* Socket = Send(Owner);
	if (!Socket)
	{
		UE_LOG(LogStorageServerConnection, Fatal, TEXT("Failed to send chunk batch request to storage server."));
		return false;
	}
	FStorageServerResponse Response(Owner, *Socket);
	if (!Response.IsOk())
	{
		UE_LOG(LogStorageServerConnection, Fatal, TEXT("Failed to read chunk batch from storage server. '%s'"), *Response.GetErrorMessage());
		return false;
	}

	uint32 Magic;
	uint32 ChunkCount;
	uint32 Reserved1;
	uint32 Reserved2;
	Response << Magic;
	if (Magic != 0xbada'b00f)
	{
		UE_LOG(LogStorageServerConnection, Fatal, TEXT("Invalid magic in chunk batch response from storage server."));
		return false;
	}
	Response << ChunkCount;
	if (ChunkCount > INT32_MAX)
	{
		UE_LOG(LogStorageServerConnection, Fatal, TEXT("Invalid chunk count in chunk batch response from storage server."));
		return false;
	}
	Response << Reserved1;
	Response << Reserved2;

	TArray<uint32, TInlineAllocator<64>> ChunkIndices;
	ChunkIndices.Reserve(ChunkCount);
	TArray<uint64, TInlineAllocator<64>> ChunkSizes;
	ChunkSizes.Reserve(ChunkCount);
	for (uint32 Index = 0; Index < ChunkCount; ++Index)
	{
		uint32 ChunkIndex;
		uint32 Flags;
		int64 ChunkSize;
		Response << ChunkIndex;
		Response << Flags;
		Response << ChunkSize;
		ChunkIndices.Add(ChunkIndex);
		ChunkSizes.Emplace(ChunkSize);
	}
	OnResponse(ChunkCount, ChunkIndices.GetData(), ChunkSizes.GetData(), Response);
	Owner.AddTimingInstance(GetDuration(), (double)Response.Tell());
	return true;
}

#endif

#endif
