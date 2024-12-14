// Copyright Epic Games, Inc. All Rights Reserved.

#include "BuiltInHttpClientFSocket.h"
#include "StorageServerConnection.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Misc/ScopeExit.h"

#if !UE_BUILD_SHIPPING

FBuiltInHttpClientFSocket::FBuiltInHttpClientFSocket(FSocket* InSocket)
	: Socket(InSocket)
{
}

FBuiltInHttpClientFSocket::~FBuiltInHttpClientFSocket()
{
	if (Socket)
	{
		Socket->Close();
		delete Socket;
		Socket = nullptr;
	}
}

bool FBuiltInHttpClientFSocket::Send(const uint8* Data, const uint64 DataSize)
{
	if (!Socket)
	{
		return false;
	}

	int32 TotalBytesSent = 0;
	while (TotalBytesSent < (int64)DataSize)
	{
		int32 BytesSent = 0;
		if (!Socket->Send(Data, DataSize, BytesSent))
		{
			return false;
		}

		TotalBytesSent += BytesSent;
	}

	return true;
}

bool FBuiltInHttpClientFSocket::Recv(uint8* Data, const uint64 DataSize, uint64& BytesRead, ESocketReceiveFlags::Type ReceiveFlags)
{
	if (!Socket)
	{
		return false;
	}

	int32 ReadBytes = 0;
	if (!Socket->Recv(Data, DataSize, ReadBytes, ReceiveFlags))
	{
		return false;
	}

	BytesRead = ReadBytes;
	return true;
}

bool FBuiltInHttpClientFSocket::HasPendingData(uint64& PendingDataSize) const
{
	uint32 PendingData;
	bool bRes = Socket->HasPendingData(PendingData);

	PendingDataSize = PendingData;
	return bRes;
}

void FBuiltInHttpClientFSocket::Close()
{
	Socket->Close();
}

FBuiltInHttpClientFSocketPool::FBuiltInHttpClientFSocketPool(TSharedPtr<FInternetAddr> InServerAddr, ISocketSubsystem& InSocketSubsystem)
	: ServerAddr(InServerAddr)
	, SocketSubsystem(InSocketSubsystem)
{
}

FBuiltInHttpClientFSocketPool::~FBuiltInHttpClientFSocketPool()
{
	IBuiltInHttpClientSocket* Socket = nullptr;
	while ((Socket = SocketPool.Pop()) != nullptr)
	{
		delete Socket;
	}
}

IBuiltInHttpClientSocket* FBuiltInHttpClientFSocketPool::AcquireSocket(float TimeoutSeconds)
{
	IBuiltInHttpClientSocket* SocketWrapper = SocketPool.Pop();
	if (SocketWrapper)
	{
		return SocketWrapper;
	}

	FSocket* Socket = SocketSubsystem.CreateSocket(NAME_Stream, TEXT("StorageServer"), ServerAddr->GetProtocolType());

	if (TimeoutSeconds > 0.0f)
	{
		Socket->SetNonBlocking(true);

		ON_SCOPE_EXIT
		{
			Socket->SetNonBlocking(false);
		};

		if (Socket->Connect(*ServerAddr) && Socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromSeconds(TimeoutSeconds)))
		{
			return new FBuiltInHttpClientFSocket(Socket);
		}
	}
	else
	{
		if (Socket->Connect(*ServerAddr))
		{
			return new FBuiltInHttpClientFSocket(Socket);
		}
	}

	delete Socket;
	return nullptr;
}

void FBuiltInHttpClientFSocketPool::ReleaseSocket(IBuiltInHttpClientSocket* Socket, bool bKeepAlive)
{
	uint64 PendingDataSize = 0;
	if (bKeepAlive && !Socket->HasPendingData(PendingDataSize))
	{
		SocketPool.Push(Socket);
	}
	else
	{
		if (PendingDataSize > 0)
		{
			UE_LOG(LogStorageServerConnection, Fatal, TEXT("Socket was not fully drained"));
		}

		delete Socket;
	}
}

#endif