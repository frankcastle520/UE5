// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Containers/ContainersFwd.h"
#include "Containers/Map.h"
#include "Iris/Core/NetObjectReference.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Iris/ReplicationSystem/NetObjectGroupHandle.h"
#include "Iris/ReplicationSystem/ReplicationBridgeTypes.h"
#include "Misc/EnumClassFlags.h"
#include "Net/Core/NetHandle/NetHandle.h"
#include "UObject/ObjectKey.h"
#include "UObject/ObjectMacros.h"

#include "ReplicationBridge.generated.h"

class UObjectReplicationBridge;
class UReplicationSystem;
class UNetDriver;

namespace UE::Net
{
	enum class ENetRefHandleError : uint32;
	typedef uint8 FNetObjectFactoryId;

	struct FNetDependencyInfo;
	struct FReplicationInstanceProtocol;
	struct FReplicationProtocol;

	class FNetBitStreamReader;
	class FNetBitStreamWriter;
	class FNetSerializationContext;
	class FNetTokenStoreState;	
	class FReplicationFragment;

	namespace Private
	{
		typedef uint32 FInternalNetRefIndex;
		class FNetRefHandleManager;
		class FNetObjectGroups;
		class FNetPushObjectHandle;
		class FObjectReferenceCache;
		class FReplicationProtocolManager;
		class FReplicationReader;
		class FReplicationStateDescriptorRegistry;
		class FReplicationSystemImpl;
		class FReplicationSystemInternal;
		class FReplicationWriter;
		struct FChangeMaskCache;
	}

	typedef TArray<FNetDependencyInfo, TInlineAllocator<32> > FNetDependencyInfoArray;
}

#if UE_BUILD_SHIPPING
	IRISCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogIrisBridge, All, Display);
#else
	IRISCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogIrisBridge, Log, All);
#endif

struct FReplicationBridgeSerializationContext
{
	FReplicationBridgeSerializationContext(UE::Net::FNetSerializationContext& InSerialiazationContext, uint32 InConnectionId, bool bInIsDestructionInfo = false);

	UE::Net::FNetSerializationContext& SerializationContext;
	uint32 ConnectionId;
	bool bIsDestructionInfo;
};



UCLASS(Transient, MinimalAPI)
class UReplicationBridge : public UObject
{
	GENERATED_BODY()

protected:
	using FNetHandle = UE::Net::FNetHandle;
	using FNetRefHandle = UE::Net::FNetRefHandle;
	using FNetDependencyInfoArray = UE::Net::FNetDependencyInfoArray;

public:
	IRISCORE_API UReplicationBridge();
	IRISCORE_API virtual ~UReplicationBridge();

	// Local interface
	
	struct FEndReplicationParameters
	{
		/** The location of the object. Used for distance based prioritization. */
		FVector Location;
		/** The level the object is placed in. */
		const UObject* Level = nullptr;
		/** Whether to use distance based priority for the destruction of the object. */
		bool bUseDistanceBasedPrioritization = false;
	};

	enum class ESubObjectInsertionOrder : uint8
	{
		None,
		ReplicateWith,
	};

	/**
	* Stop replicating the NetObject associated with the handle and mark the handle to be destroyed.
	* If EEndReplication::TearOff is set the remote instance will be Torn-off rather than being destroyed on the receiving end, after the call, any state changes will not be replicated
	* If EEndReplication::Flush is set all pending states will be delivered before the remote instance is destroyed, final state will be immediately copied so it is safe to remove the object after this call
	* If EEndReplication::Destroy is set the remote instance will be destroyed, if this is set for a static instance and the EndReplicationParameters are set a permanent destruction info will be added
	* Dynamic instances are always destroyed unless the TearOff flag is set.
	*/
	IRISCORE_API void StopReplicatingNetRefHandle(FNetRefHandle Handle, EEndReplicationFlags EndReplicationFlags);

	/** Store destruction info for the referenced object. */
	IRISCORE_API FNetRefHandle StoreDestructionInfo(FNetRefHandle Handle, const FEndReplicationParameters& Parameters);

	/** Returns true if the handle is replicated. */
	IRISCORE_API bool IsReplicatedHandle(FNetRefHandle Handle) const;

	/** Get the group associated with the level in order to control connection filtering for it. */
	IRISCORE_API UE::Net::FNetObjectGroupHandle GetLevelGroup(const UObject* Level) const;

	/** Print common information about this handle and the object it is mapped to */
	[[nodiscard]] IRISCORE_API FString PrintObjectFromNetRefHandle(FNetRefHandle RefHandle) const;

protected:
	/** Initializes the bridge. Is called during ReplicationSystem initialization. */
	IRISCORE_API virtual void Initialize(UReplicationSystem* InReplicationSystem);

	/** Deinitializes the bridge. Is called during ReplicationSystem deinitialization. */
	IRISCORE_API virtual void Deinitialize();

	/** Invoked before ReplicationSystem copies dirty state data. */
	IRISCORE_API virtual void PreSendUpdate();

	/** Invoked when the ReplicationSystem starts the PreSendUpdate tick. */
	IRISCORE_API virtual void OnStartPreSendUpdate();
	
	/** Invoked after we sent data to all connections. */
	IRISCORE_API virtual void OnPostSendUpdate();
	
	/** Invoked before ReplicationSystem copies dirty state data for a single replicated object. */
	IRISCORE_API virtual void PreSendUpdateSingleHandle(FNetRefHandle Handle);

	/** Update world locations in FWorldLocations for objects that support it. */
	IRISCORE_API virtual void UpdateInstancesWorldLocation();

	// Remote interface, invoked from Replication code during serialization
	
	/**
	 * Write data required to instantiate NetObject remotely to bitstream.
	 * @param Context The serialization context parameters.
	 * @param Handle The handle of the object to write creation data for.
	 */
	IRISCORE_API virtual bool WriteNetRefHandleCreationInfo(FReplicationBridgeSerializationContext& Context, FNetRefHandle Handle);

	/**
	 * Cache info required to allow deferred writing of NetRefHandleCreationInfo
	 * @param Handle The handle of the object to store creation data for.
	 * return whether cached data is stored or not.
	*/
	IRISCORE_API virtual bool CacheNetRefHandleCreationInfo(FNetRefHandle Handle);

	/** Read data required to instantiate NetObject from bitstream. */
	IRISCORE_API virtual FReplicationBridgeCreateNetRefHandleResult CreateNetRefHandleFromRemote(FNetRefHandle RootObjectOfSubObject, FNetRefHandle WantedNetHandle, FReplicationBridgeSerializationContext& Context);

	/** Invoked right before we apply the state for a new received subobject but after we have applied state for owning/root object in order to behave like old replication system */
	IRISCORE_API virtual void SubObjectCreatedFromReplication(FNetRefHandle SubObjectRefHandle);

	/** Invoked after we have applied the initial state for an object.*/
	virtual void PostApplyInitialState(UE::Net::Private::FInternalNetRefIndex InternalObjectIndex) {}

	/**
	 * Called when the instance is detached from the protocol on request by the remote. 
	 * @param Handle The handle of the object to destroy or tear off.
	 * @param DestroyReason Reason for destroying the instance. 
	 * @param DestroyFlags Special flags such as whether the instance may be destroyed when reason is TearOff, which may revert to destroying, or Destroy.
	 */
	IRISCORE_API virtual void DetachInstanceFromRemote(FNetRefHandle Handle, EReplicationBridgeDestroyInstanceReason DestroyReason, EReplicationBridgeDestroyInstanceFlags DestroyFlags);

	/** Called when we detach instance protocol from the local instance */
	IRISCORE_API virtual void DetachInstance(FNetRefHandle Handle);

	/** Invoked post garbage collect to allow us to detect stale objects */
	IRISCORE_API virtual void PruneStaleObjects();

	/** Invoked when we start to replicate an object for a specific connection to fill in any initial dependencies */
	IRISCORE_API virtual void GetInitialDependencies(FNetRefHandle Handle, FNetDependencyInfoArray& OutDependencies) const;

protected:
	// Forward calls to internal operations that we allow replication bridges to access

	/** Create a local NetRefHandle / NetObject using the ReplicationProtocol. */
	IRISCORE_API FNetRefHandle InternalCreateNetObject(FNetRefHandle AllocatedHandle, FNetHandle GlobalHandle, const UE::Net::FReplicationProtocol* ReplicationProtocol);

	/** Create a local NetRefHandle / NetObject using the ReplicationProtocol. */
	IRISCORE_API FNetRefHandle InternalCreateNetObject(FNetRefHandle AllocatedHandle, const UE::Net::FReplicationProtocol* ReplicationProtocol);

	/** Create a NetRefHandle / NetObject on request from the authoritative end. */
	IRISCORE_API FNetRefHandle InternalCreateNetObjectFromRemote(FNetRefHandle WantedNetHandle, const UE::Net::FReplicationProtocol* ReplicationProtocol,  UE::Net::FNetObjectFactoryId FactoryId);

	/** Attach instance to NetRefHandle. */
	IRISCORE_API void InternalAttachInstanceToNetRefHandle(FNetRefHandle RefHandle, bool bBindInstanceProtocol, UE::Net::FReplicationInstanceProtocol* InstanceProtocol, UObject* Instance, FNetHandle NetHandle);

	/** Detach instance from NetRefHandle and destroy the instance protocol. */
	IRISCORE_API void InternalDetachInstanceFromNetRefHandle(FNetRefHandle Handle);

	/** Destroy the handle and all internal book keeping associated with it. */
	IRISCORE_API void InternalDestroyNetObject(FNetRefHandle Handle);
	
	/** Add SubObjectHandle as SubObject to OwnerHandle. */
	IRISCORE_API void InternalAddSubObject(FNetRefHandle OwnerHandle, FNetRefHandle SubObjectHandle, FNetRefHandle InsertRelativeToSubObjectHandle, ESubObjectInsertionOrder InsertionOrder);

	inline UE::Net::Private::FReplicationProtocolManager* GetReplicationProtocolManager() const { return ReplicationProtocolManager; }
	inline UReplicationSystem* GetReplicationSystem() const { return ReplicationSystem; }
	inline UE::Net::Private::FReplicationStateDescriptorRegistry* GetReplicationStateDescriptorRegistry() const { return ReplicationStateDescriptorRegistry; }
	inline UE::Net::Private::FObjectReferenceCache* GetObjectReferenceCache() const { return ObjectReferenceCache; }

	/** Creates a group for a level for object filtering purposes. */
	IRISCORE_API UE::Net::FNetObjectGroupHandle CreateLevelGroup(const UObject* Level, FName PackageName);

	/** Destroys the group associated with the level. */
	IRISCORE_API void DestroyLevelGroup(const UObject* Level);

	/** Called when destruction info is received to determine whether the instance may be destroyed. */
	IRISCORE_API virtual bool IsAllowedToDestroyInstance(const UObject* Instance) const;

	/** Called when a remote connection detected a protocol mismatch when trying to instantiate the NetRefHandle replicated object. */
	IRISCORE_API virtual void OnProtocolMismatchReported(FNetRefHandle RefHandle, uint32 ConnectionId) {}

	/** Called when a remote connection has a critical error caused by a specific NetRefHandle */
	IRISCORE_API virtual void OnErrorWithNetRefHandleReported(UE::Net::ENetRefHandleError ErrorType, FNetRefHandle RefHandle, uint32 ConnectionId) {}

	/** Tell the remote connection that we detected a reading error with a specific replicated object */
	IRISCORE_API virtual void SendErrorWithNetRefHandle(UE::Net::ENetRefHandleError ErrorType, FNetRefHandle RefHandle, uint32 ConnectionId) {}

private:

	// Internal operations invoked by ReplicationSystem/ReplicationWriter
	void ReadAndExecuteDestructionInfoFromRemote(FReplicationBridgeSerializationContext& Context);
	void DetachSubObjectInstancesFromRemote(FNetRefHandle Handle, EReplicationBridgeDestroyInstanceReason DestroyReason, EReplicationBridgeDestroyInstanceFlags DestroyFlags);
	void DestroyNetObjectFromRemote(FNetRefHandle Handle, EReplicationBridgeDestroyInstanceReason DestroyReason, EReplicationBridgeDestroyInstanceFlags DestroyFlags);

	enum class EPendingEndReplicationImmediate : uint8
	{
		Yes,
		No,
	};

	// Adds the Handle to the list of handles pending deferred EndReplication, if bIsImmediate is true the object will be destroyed after the next update, otherwise
	// it will be kept around until the handle is no longer ref-counted by any connection. It will however be removed from the set of scopeable objects after the first update so new connections will not add it to their scope.
	void AddPendingEndReplication(FNetRefHandle Handle, EEndReplicationFlags DestroyFlags, EPendingEndReplicationImmediate Immediate = EPendingEndReplicationImmediate::No);

	FReplicationBridgeCreateNetRefHandleResult CallCreateNetRefHandleFromRemote(FNetRefHandle RootObjectOfSubObject, FNetRefHandle WantedNetHandle, FReplicationBridgeSerializationContext& Context);
	void CallPreSendUpdate(float DeltaSeconds);	
	void CallPreSendUpdateSingleHandle(FNetRefHandle Handle);
	void CallUpdateInstancesWorldLocation();
	bool CallCacheNetRefHandleCreationInfo(FNetRefHandle Handle);
	bool CallWriteNetRefHandleCreationInfo(FReplicationBridgeSerializationContext& Context, FNetRefHandle Handle);
	bool CallWriteNetRefHandleDestructionInfo(FReplicationBridgeSerializationContext& Context, FNetRefHandle Handle);
	void CallSubObjectCreatedFromReplication(FNetRefHandle SubObjectHandle);
	void CallPostApplyInitialState(UE::Net::Private::FInternalNetRefIndex InternalObjectIndex);
	void CallPruneStaleObjects();
	void CallGetInitialDependencies(FNetRefHandle Handle, FNetDependencyInfoArray& OutDependencies) const;
	void CallDetachInstance(FNetRefHandle Handle);
	void CallDetachInstanceFromRemote(FNetRefHandle Handle, EReplicationBridgeDestroyInstanceReason DestroyReason, EReplicationBridgeDestroyInstanceFlags DestroyFlags);

private:

	void InternalFlushStateData(UE::Net::FNetSerializationContext& SerializationContext, UE::Net::Private::FChangeMaskCache& ChangeMaskCache, UE::Net::FNetBitStreamWriter& ChangeMaskWriter, uint32 InternalObjectIndex);
	// Internal method to copy state data for Handle
	void InternalFlushStateData(FNetRefHandle Handle);

	// Internal method to copy state data for Handle and any SubObjects and mark them as being torn-off
	void InternalTearOff(FNetRefHandle OwnerHandle);

	// Destroy all SubObjects owned by provided handle
	void InternalDestroySubObjects(FNetRefHandle OwnerHandle, EEndReplicationFlags Flags);

	/**
	 * Called from ReplicationSystem when a streaming level is about to unload.
	 * Will remove the group associated with the level and remove destruction infos.
	 */
	void NotifyStreamingLevelUnload(const UObject* Level);

	/**
	 * Remove destruction infos associated with group
	 * Passing in an invalid group handle indicates that we should remove all destruction infos
	 */
	void RemoveDestructionInfosForGroup(UE::Net::FNetObjectGroupHandle GroupHandle);

	void DestroyLocalNetHandle(FNetRefHandle Handle, EEndReplicationFlags Flags);

	// Tear-off all handles in the PendingTearOff list that has not yet been torn-off
	void TearOffHandlesPendingTearOff();

	// Update all the handles pending EndReplication
	void UpdateHandlesPendingEndReplication();

	void SetNetPushIdOnFragments(const TArrayView<const UE::Net::FReplicationFragment*const>& Fragments, const UE::Net::Private::FNetPushObjectHandle& PushHandle);
	void ClearNetPushIdOnFragments(const TArrayView<const UE::Net::FReplicationFragment*const>& Fragments);

	void DestroyGlobalNetHandle(UE::Net::Private::FInternalNetRefIndex InternalReplicationIndex);
	void ClearNetPushIds(UE::Net::Private::FInternalNetRefIndex InternalReplicationIndex);

	friend UReplicationSystem;
	friend UE::Net::Private::FReplicationSystemImpl;
	friend UE::Net::Private::FReplicationSystemInternal;
	friend UE::Net::Private::FReplicationWriter;
	friend UE::Net::Private::FReplicationReader;
	friend UObjectReplicationBridge;

	UReplicationSystem* ReplicationSystem;
	UE::Net::Private::FReplicationProtocolManager* ReplicationProtocolManager;
	UE::Net::Private::FReplicationStateDescriptorRegistry* ReplicationStateDescriptorRegistry;
	UE::Net::Private::FNetRefHandleManager* NetRefHandleManager;
	UE::Net::Private::FObjectReferenceCache* ObjectReferenceCache;
	UE::Net::Private::FNetObjectGroups* Groups;

	TMap<FObjectKey, UE::Net::FNetObjectGroupHandle> LevelGroups;

private:

	// DestructionInfos
	struct FDestructionInfo
	{
		UE::Net::FNetObjectReference StaticRef;
		UE::Net::FNetObjectGroupHandle LevelGroupHandle;
		UE::Net::Private::FInternalNetRefIndex InternalReplicationIndex;
	};

	const UE::Net::FReplicationProtocol* DestructionInfoProtocol;
	
	// Need to track the objects with destruction infos so that we can clean them up properly
	// We use this to be able ask remote to destroy static objects
	TMap<FNetRefHandle, FDestructionInfo> StaticObjectsPendingDestroy;

	struct FPendingEndReplicationInfo
	{
		FPendingEndReplicationInfo(FNetRefHandle InHandle, EEndReplicationFlags InDestroyFlags, EPendingEndReplicationImmediate InImmediate) : Handle(InHandle), DestroyFlags(InDestroyFlags), Immediate(InImmediate) {}

		FNetRefHandle Handle;
		EEndReplicationFlags DestroyFlags;
		EPendingEndReplicationImmediate Immediate;
	};
	TArray<FPendingEndReplicationInfo> HandlesPendingEndReplication;
};

inline FReplicationBridgeSerializationContext::FReplicationBridgeSerializationContext(UE::Net::FNetSerializationContext& InSerialiazationContext, uint32 InConnectionId, bool bInIsDestructionInfo)
: SerializationContext(InSerialiazationContext)
, ConnectionId(InConnectionId)
, bIsDestructionInfo(bInIsDestructionInfo)
{
}
