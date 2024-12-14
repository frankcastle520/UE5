// Copyright Epic Games, Inc. All Rights Reserved.

#include "Replication/Processing/ReplicationDataQueuer.h"

#include "ConcertLogGlobal.h"
#include "Replication/Messages/ObjectReplication.h"
#include "Trace/ConcertProtocolTrace.h"

namespace UE::ConcertSyncCore
{
	void FReplicationDataQueuer::ForEachPendingObject(TFunctionRef<void(const FPendingObjectReplicationInfo&)> ProcessItemFunc) const
	{
		for (auto It = PendingEvents.CreateConstIterator(); It; ++It)
		{
			ProcessItemFunc({ It->Key, It->Value.SequenceId });
		}
	}

	int32 FReplicationDataQueuer::NumObjects() const
	{
		return PendingEvents.Num();
	}

	bool FReplicationDataQueuer::ExtractReplicationDataForObject(
		const FConcertReplicatedObjectId& Object,
		TFunctionRef<void(const FConcertSessionSerializedPayload& Payload)> ProcessCopyable,
		TFunctionRef<void(FConcertSessionSerializedPayload&& Payload)> ProcessMoveable
		)
	{
		FPendingObjectData EventData;
		const bool bSuccess = PendingEvents.RemoveAndCopyValue(Object, EventData);
		if (!ensureMsgf(bSuccess, TEXT("ExtractReplicationDataForObject for an item that was not returned by ForEachPendingObject")))
		{
			return false;
		}

		// Event may be shared by other FReplicationDataQueuers since it originates from the replication cache, so sadly no move.
		ProcessCopyable(EventData.DataToApply->SerializedPayload);
		return true;
	}

	void FReplicationDataQueuer::OnDataCached(const FConcertReplicatedObjectId& Object, FSequenceId SequenceId, TSharedRef<const FConcertReplication_ObjectReplicationEvent> Data)
	{
		PendingEvents.Add(Object, FPendingObjectData{ Data, SequenceId });
	}

	void FReplicationDataQueuer::OnCachedDataUpdated(const FConcertReplicatedObjectId& Object, FSequenceId SequenceId)
	{
		FPendingObjectData* ObjectData = PendingEvents.Find(Object);
		if (ensureMsgf(ObjectData, TEXT("OnCachedDataUpdated called for %s even though we have no cached data. Investigate in correct API call!"), *Object.ToString()))
		{
			// TODO DP: This may want to be a specialized macro so Insights can visually highlight that a packet was merged.
			// Trace that the old data has "finished" sending ... 
			CONCERT_TRACE_REPLICATION_OBJECT_SINK(Merged, Object.Object, ObjectData->SequenceId);
			// ... since it is merged
			ObjectData->SequenceId = SequenceId;
		}
	}

	void FReplicationDataQueuer::BindToCache(FObjectReplicationCache& InReplicationCache)
	{
		ReplicationCache = &InReplicationCache;
		ReplicationCache->RegisterDataCacheUser(AsShared());
	}
}
