// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Containers/UnrealString.h"
#include "HAL/CriticalSection.h"
#include "Templates/UniquePtr.h"

namespace UE::Trace
{

class FStoreClient;

class TRACEANALYSIS_API FStoreConnection
{
public:
	FStoreConnection();
	virtual ~FStoreConnection();

	bool ConnectToStore(const TCHAR* Host, uint32 Port = 0);

	/**
	 * Attempt to reconnect to the store if the connection was severed, without recreating the store client.
	 * @return True on success, false on failure
	 */
	bool ReconnectToStore();

	void Disconnect();

	UE::Trace::FStoreClient* GetStoreClient() const { return StoreClient.Get(); }
	FCriticalSection& GetStoreClientCriticalSection() const { return StoreClientCriticalSection; }

	bool GetStoreAddressAndPort(uint32& OutStoreAddress, uint32& OutStorePort) const;
	FString GetStoreDir() const;

	const FString& GetLastStoreHost() const { return LastStoreHost; }
	uint32 GetLastStorePort() const { return LastStorePort; }

	const bool IsLocalHost() const { return bIsLocalHost; }
	const bool CanChangeStoreSettings() const { return bIsLocalHost && StoreClient.IsValid(); }

private:
	/** The client used to connect to the trace store. It is not thread safe! */
	TUniquePtr<UE::Trace::FStoreClient> StoreClient;

	/** CriticalSection for using the store client's API. */
	mutable FCriticalSection StoreClientCriticalSection;

	FString LastStoreHost;
	uint32 LastStorePort = 0;

	bool bIsLocalHost = false;
};

} // namespace UE::Trace
