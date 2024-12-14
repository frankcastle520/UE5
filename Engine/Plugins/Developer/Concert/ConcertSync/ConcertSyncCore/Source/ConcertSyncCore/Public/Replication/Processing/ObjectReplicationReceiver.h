// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

class IConcertSession;
struct FConcertReplication_BatchReplicationEvent;
struct FConcertReplication_ObjectReplicationEvent;
struct FConcertSessionContext;
struct FConcertReplication_StreamReplicationEvent;

namespace UE::ConcertSyncCore
{
	class FObjectReplicationCache;
	
	/** Receives replicated object data from all message endpoints and stores it in a FObjectReplicationCache. */
	class CONCERTSYNCCORE_API FObjectReplicationReceiver
	{
	public:

		FObjectReplicationReceiver(IConcertSession& Session UE_LIFETIMEBOUND, FObjectReplicationCache& ReplicationCache UE_LIFETIMEBOUND);
		virtual ~FObjectReplicationReceiver();

	protected:

		/** Whether the object should be processed. */
		virtual bool ShouldAcceptObject(const FConcertSessionContext& SessionContext, const FConcertReplication_StreamReplicationEvent& StreamEvent, const FConcertReplication_ObjectReplicationEvent& ObjectEvent) const { return true; }

	private:

		/** The session that is being received on. */
		IConcertSession& Session;
		/** Where received data is stored. */
		FObjectReplicationCache& ReplicationCache;

		void HandleBatchReplicationEvent(const FConcertSessionContext& SessionContext, const FConcertReplication_BatchReplicationEvent& Event);
	};
}
