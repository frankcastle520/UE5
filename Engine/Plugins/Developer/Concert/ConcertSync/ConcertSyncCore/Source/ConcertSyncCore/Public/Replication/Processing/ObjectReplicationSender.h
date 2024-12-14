// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectReplicationProcessor.h"
#include "Replication/Messages/ObjectReplication.h"
#include "Trace/ConcertTraceConfig.h"

class IConcertSession;

namespace UE::ConcertSyncCore
{
	/**
	 * Sends object data to a specific endpoint ID.
	 */
	class CONCERTSYNCCORE_API FObjectReplicationSender : public FObjectReplicationProcessor
	{
	public:
		
		/**
		 * @param TargetEndpointId The endpoints to send to
		 * @param Session The session to use for sending
		 * @param DataSource Source of the data that is to be sent
		 */
		FObjectReplicationSender(
			const FGuid& TargetEndpointId,
			IConcertSession& Session UE_LIFETIMEBOUND, 
			IReplicationDataSource& DataSource UE_LIFETIMEBOUND
			);
		
		//~ Begin FObjectReplicationProcessor Interface
		virtual void ProcessObjects(const FProcessObjectsParams& Params) override;
		//~ End FObjectReplicationProcessor Interface

	protected:

		//~ Begin FObjectReplicationProcessor Interface
		virtual void ProcessObject(const FObjectProcessArgs& Args) override;
		//~ End FObjectReplicationProcessor Interface

	private:

		/** The endpoint data will be sent to */
		const FGuid TargetEndpointId;
		
		/** The session through which replication messages are sent. */
		IConcertSession& Session;

#if UE_CONCERT_TRACE_ENABLED
		/** Set by ProcessObject. Used when we actually start sending the data. */
		TMap<FConcertReplicatedObjectId, FSequenceId> ObjectsToTraceThisFrame;
#endif

		/** This event is filled in ProcessObjects and finally sent to TargetEndpointId. */
		FConcertReplication_BatchReplicationEvent EventToSend;

		void MarkObjectForTrace(const FConcertReplicatedObjectId& Object, FSequenceId Id);
		void TraceStartSendingMarkedObjects();
	};
}
