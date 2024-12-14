// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/ObjectPathHierarchy.h"

#include "Containers/Array.h"
#include "Containers/ContainersFwd.h"

struct FConcertObjectInStreamID;
struct FConcertReplicatedObjectId;
struct FConcertReplication_ChangeStream_Request;
struct FConcertReplicationStream;
struct FConcertReplication_Join_Request;

namespace UE::ConcertSyncCore
{
	/**
	 * Holds the outer hierarchy of all objects registered in any stream.
	 * Updates the hierarchy when a relevant event changing stream content happens.
	 */
	class CONCERTSYNCCORE_API FReplicatedObjectHierarchyCache : FObjectPathHierarchy
	{
	public:

		// AddObject and RemoveObject are not supposed to be invoked - use the dedicated OnX events below.
		using FObjectPathHierarchy::TraverseTopToBottom;
		using FObjectPathHierarchy::TraverseBottomToTop;
		using FObjectPathHierarchy::IsInHierarchy;
		using FObjectPathHierarchy::HasChildren;
		using FObjectPathHierarchy::IsEmpty;

		/** Removes all saved state. */
		void Clear();

		/**
		 * Checks whether the object is registered by any client (except for this in IgnoredClients).
		 *
		 * This function ignores implicit knowledge of the hierarchy.
		 * For example if you register ONLY /Game/Maps.Map:PersistentLevel.Cube.StaticMeshComponent0, then
		 * - IsObjectReferencedDirectly(/Game/Maps.Map:PersistentLevel.Cube.StaticMeshComponent0) == true,
		 * - IsObjectReferencedDirectly(/Game/Maps.Map:PersistentLevel.Cube) == false
		 * but e.g. TraverseTopToBottom would list both paths.
		 */
		bool IsObjectReferencedDirectly(const FSoftObjectPath& ObjectPath, TConstArrayView<FGuid> IgnoredClients = {}) const;

		// Validated network events that modify the known hierarchy of objects.
		void OnJoin(const FGuid& ClientId, const FConcertReplication_Join_Request& Request);
		void OnPostClientLeft(const FGuid& ClientId, TConstArrayView<FConcertReplicationStream> Streams);
		void OnChangeStreams(const FGuid& ClientId, TConstArrayView<FConcertObjectInStreamID> AddedObjects, TConstArrayView<FConcertObjectInStreamID> RemovedObjects);

	private:

		struct FStreamReferencer
		{
			FGuid ClientId;
			FGuid StreamId;

			friend bool operator==(const FStreamReferencer& Left, const FStreamReferencer& Right) { return Left.ClientId == Right.ClientId && Left.StreamId == Right.StreamId; }
			friend bool operator!=(const FStreamReferencer& Left, const FStreamReferencer& Right) { return !(Left == Right); }
		};
		
		struct FObjectMetaData
		{
			/** Keeps track of all clients referencing the stream. */
			TArray<FStreamReferencer> ReferencingStreams;
		};
		
		TMap<FSoftObjectPath, FObjectMetaData> ObjectMetaData;

		void AddObjectInternal(const FConcertReplicatedObjectId& ObjectInfo);
		void RemoveObjectInternal(const FConcertReplicatedObjectId& Object);
		bool RemoveMetaData(const FConcertReplicatedObjectId& Object);
	};
}
