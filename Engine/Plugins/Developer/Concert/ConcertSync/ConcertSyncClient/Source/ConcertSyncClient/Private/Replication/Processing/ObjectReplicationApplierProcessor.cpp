// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectReplicationApplierProcessor.h"

#include "ConcertLogGlobal.h"
#include "Replication/IConcertClientReplicationBridge.h"
#include "Replication/Formats/IObjectReplicationFormat.h"

#include "Components/SceneComponent.h"
#include "Trace/ConcertProtocolTrace.h"

namespace UE::ConcertSyncClient::Replication
{
	FObjectReplicationApplierProcessor::FObjectReplicationApplierProcessor(
		IConcertClientReplicationBridge& ReplicationBridge,
		ConcertSyncCore::IObjectReplicationFormat& ReplicationFormat,
		ConcertSyncCore::IReplicationDataSource& DataSource
		)
		: FObjectReplicationProcessor(DataSource)
		, ReplicationBridge(ReplicationBridge)
		, ReplicationFormat(ReplicationFormat)
	{}

	void FObjectReplicationApplierProcessor::ProcessObject(const FObjectProcessArgs& Args)
	{
		CONCERT_TRACE_REPLICATION_OBJECT_SCOPE(ApplyReceivedObject, Args.ObjectInfo.ObjectId.Object, Args.ObjectInfo.SequenceId);
		UObject* Object = ReplicationBridge.FindObjectIfAvailable(Args.ObjectInfo.ObjectId.Object);
		if (!Object)
		{
			UE_LOG(LogConcert, Error, TEXT("Replication: Object %s is unavailable. The data source should not have reported it."), *Args.ObjectInfo.ObjectId.Object.ToString());
			return;
		}

		bool bAppliedData = false;
		GetDataSource().ExtractReplicationDataForObject(Args.ObjectInfo.ObjectId, [this, &Args, Object, &bAppliedData](const FConcertSessionSerializedPayload& Payload)
		{
			CONCERT_TRACE_REPLICATION_OBJECT_SCOPE(SerializeReceivedObject, Args.ObjectInfo.ObjectId.Object, Args.ObjectInfo.SequenceId);
			bAppliedData = true;

			// We're technically modifying the package so mark it modified. This will make Concert / Multi-User revert the changes on leaving the session.
			// This could be made faster by caching the package. For now leave it and possibly profile in future if needed.
			Object->MarkPackageDirty();
			
			// TODO DP UE-193659: This is very hacky and leaves performance on the table... this is in case ApplyReplicationEvent updates the transform
			if (USceneComponent* SceneComponent = Cast<USceneComponent>(Object))
			{
				ReplicationFormat.ApplyReplicationEvent(*Object, Payload);
				SceneComponent->UpdateComponentToWorld();
			}
			else
			{
				ReplicationFormat.ApplyReplicationEvent(*Object, Payload);
			}
		});
		
		// This should not happen. If it does, we're wasting network bandwidth.
		UE_CLOG(!bAppliedData, LogConcert, Warning, TEXT("Replication: Server sent data that could not be applied (likely it was empty) for object %s from stream %s"), *Args.ObjectInfo.ObjectId.Object.ToString(), *Args.ObjectInfo.ObjectId.StreamId.ToString());
		CONCERT_TRACE_REPLICATION_OBJECT_SINK(Processed, Args.ObjectInfo.ObjectId.Object, Args.ObjectInfo.SequenceId);
	}
}
