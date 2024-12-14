// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Network/Service/DisplayClusterService.h"
#include "Network/Session/IDisplayClusterSessionPacketHandler.h"
#include "Network/Protocol/IDisplayClusterProtocolGenericBarrier.h"
#include "Network/Packet/DisplayClusterPacketInternal.h"

#include "Cluster/IDisplayClusterGenericBarriersClient.h"

class FEvent;


/**
 * Generic barriers TCP server
 */
class FDisplayClusterGenericBarrierService
	: public    FDisplayClusterService
	, public    IDisplayClusterSessionPacketHandler<FDisplayClusterPacketInternal, true>
	, protected IDisplayClusterProtocolGenericBarrier
{
public:

	/**
	 * Additional barrier information that might be useful outside of the server
	 */
	struct FBarrierInfo
	{
		// Holds a set of thread markers bound to the owning cluster node
		TMap<FString, TSet<FString>> NodeToThreadsMapping;

		// Holds thread marker - to - cluster node mapping
		TMap<FString, FString> ThreadToNodeMapping;
	};

public:
	FDisplayClusterGenericBarrierService();
	virtual ~FDisplayClusterGenericBarrierService();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterServer
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void Shutdown() override final;
	virtual FString GetProtocolName() const override;

public:

	// Returns barrier by ID
	TSharedPtr<IDisplayClusterBarrier, ESPMode::ThreadSafe> GetBarrier(const FString& BarrierId);

	// Returns barrier information
	TSharedPtr<FDisplayClusterGenericBarrierService::FBarrierInfo> GetBarrierInfo(const FString& BarrierId) const;

	// Enables/disables barrier info update. The update procedure is called every time a thread joins the barrier
	// to synchronize. More threads we have more CPU time is consumed for this. For optimization purposes, this
	// function allows to stop collecting barrier information and continue updating at any time.
	void SetBarrierInfoUpdateLocked(const FString& BarrierId, bool bLocked);

protected:
	// Creates session instance for this service
	virtual TSharedPtr<IDisplayClusterSession> CreateSession(FDisplayClusterSessionInfo& SessionInfo) override;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterSessionPacketHandler
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual TSharedPtr<FDisplayClusterPacketInternal> ProcessPacket(const TSharedPtr<FDisplayClusterPacketInternal>& Request, const FDisplayClusterSessionInfo& SessionInfo) override;

private:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolGenericBarrier
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual EDisplayClusterCommResult CreateBarrier(const FString& BarrierId, const TArray<FString>& UniqueThreadMarkers, uint32 Timeout, EBarrierControlResult& Result) override;
	virtual EDisplayClusterCommResult WaitUntilBarrierIsCreated(const FString& BarrierId, EBarrierControlResult& Result) override;
	virtual EDisplayClusterCommResult IsBarrierAvailable(const FString& BarrierId, EBarrierControlResult& Result) override;
	virtual EDisplayClusterCommResult ReleaseBarrier(const FString& BarrierId, EBarrierControlResult& Result) override;
	virtual EDisplayClusterCommResult SyncOnBarrier(const FString& BarrierId, const FString& UniqueThreadMarker, EBarrierControlResult& Result) override;
	virtual EDisplayClusterCommResult SyncOnBarrierWithData(const FString& BarrierId, const FString& UniqueThreadMarker, const TArray<uint8>& RequestData, TArray<uint8>& OutResponseData, EBarrierControlResult& Result) override;

private:

	// Caches information about barrier users
	void UpdateBarrierInformation(const FString& BarrierId, const FString& NodeId, const FString& ThreadMarker);

private:

	// Barriers
	TMap<FString, TSharedPtr<IDisplayClusterBarrier, ESPMode::ThreadSafe>> Barriers;

	// Barrier creation events
	TMap<FString, FEvent*> BarrierCreationEvents;

	// Critical section for internal data access synchronization
	mutable FCriticalSection BarriersCS;

private:

	/**
	 * A helper structure that wraps the actual barrier info holder
	 * with an additional data for optimization purposes.
	 */
	struct FBarrierInfoWrapper
	{
		// The actual barrier information holder
		TSharedRef<FBarrierInfo> BarrierInfo = MakeShared<FBarrierInfo>();

		// Whether barrier information is locked from being updated
		bool bBarrierInfoLockedOut = false;
	};

	// Holds extra information per-barrier
	TMap<FString, FBarrierInfoWrapper> BarriersInfo;

	// Critical section to synchronize access to the barriers information container
	mutable FCriticalSection BarriersInfoCS;
};
