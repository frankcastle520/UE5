// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Engine/EngineTypes.h"
#include "UObject/ObjectPtr.h"
#include "EngineReplicationBridge.generated.h"

class UNetDriver;
class UIrisObjectReferencePackageMap;
struct FAnalyticsEventAttribute;

#if UE_WITH_IRIS

class UActorComponent;
class UWorld;

namespace UE::Net
{
	enum class ENetRefHandleError : uint32;
}

namespace UE::Net
{

// If actor should be replicated using IRIS or old replication system
ENGINE_API bool ShouldUseIrisReplication(const UObject* Actor);

}

struct FActorReplicationParams
{
	/** When true we ignore the configured dynamic filter for this actor type and use the explicit filter instead */
	bool bOverrideDynamicFilterConfig = false;

	/** Only used when bOverrideDynamicFilterConfig is true. The name of the dynamic filter to assign to this actor. Can be NAME_None if no filters should be set. */
	FName ExplicitDynamicFilterName;
};

#endif // UE_WITH_IRIS

UCLASS(Transient, MinimalAPI)
class UEngineReplicationBridge final : public UObjectReplicationBridge
{
	GENERATED_BODY()

public:
	ENGINE_API UEngineReplicationBridge();
	virtual ENGINE_API ~UEngineReplicationBridge() override;

#if UE_WITH_IRIS

	ENGINE_API static UEngineReplicationBridge* Create(UNetDriver* NetDriver);

	/** Sets the net driver for the bridge. */
	ENGINE_API void SetNetDriver(UNetDriver* const InNetDriver);
	
	/** Get net driver used by the bridge .*/
	inline UNetDriver* GetNetDriver() const { return NetDriver; }

	/** Begin replication of an actor and its registered ActorComponents and SubObjects. */
	ENGINE_API FNetRefHandle StartReplicatingActor(AActor* Instance, const FActorReplicationParams& Params);

	/** Stop replicating an actor. Will destroy handle for actor and registered subobjects. */
	ENGINE_API void StopReplicatingActor(AActor* Actor, EEndPlayReason::Type EndPlayReason);
		
	/**
	 * Begin replication of an ActorComponent and its registered SubObjects, 
	 * if the ActorComponent already is replicated any set NetObjectConditions will be updated.
	*/
	ENGINE_API FNetRefHandle StartReplicatingComponent(FNetRefHandle RootObjectHandle, UActorComponent* ActorComponent);

	/** Begin replication of a subobject. */
	ENGINE_API FNetRefHandle StartReplicatingSubObject(UObject* SubObject, const FSubObjectReplicationParams& Params);

	/** Stop replicating an ActorComponent and its associated SubObjects. */
	ENGINE_API void StopReplicatingComponent(UActorComponent* ActorComponent, EEndReplicationFlags EndReplicationFlags = EEndReplicationFlags::None);

	/** Get object reference packagemap. Used in special cases where serialization hasn't been converted to use NetSerializers.  */
	UIrisObjectReferencePackageMap* GetObjectReferencePackageMap() const { return ObjectReferencePackageMap; }

	/** Tell the remote connection that we detected a reading error with a specific replicated object */
	ENGINE_API virtual void SendErrorWithNetRefHandle(UE::Net::ENetRefHandleError ErrorType, FNetRefHandle RefHandle, uint32 ConnectionId) override;
	
	/** Updates the level group for an actor that changed levels */
	void ActorChangedLevel(const AActor* Actor, const ULevel* PreviousLevel);

	/** Called when NetUpdateFrequency has changed on the Actor. */
	void OnNetUpdateFrequencyChanged(const AActor* Actor);

	void WakeUpObjectInstantiatedFromRemote(AActor* Actor) const;

	/**
	 * Add relevant network metrics gathered since the last call to ConsumeNetMetrics.
	 * Any periodic stat will be reset here too.
	 * @param OutAttrs A list of Name/Value pairings that will be sent to an AnalyticsProvider
	 */
	ENGINE_API void ConsumeNetMetrics(TArray<FAnalyticsEventAttribute>& OutAttrs);

protected:

	// UObjectReplicationBridge
	virtual void Initialize(UReplicationSystem* ReplicationSystem) override;
	virtual void Deinitialize() override;
	virtual void OnSubObjectCreatedFromReplication(FNetRefHandle SubObjectHandle) override;
	virtual void DestroyInstanceFromRemote(const FDestroyInstanceParams& Params) override;
	virtual void GetInitialDependencies(FNetRefHandle Handle, FNetDependencyInfoArray& OutDependencies) const override;
	virtual bool RemapPathForPIE(uint32 ConnectionId, FString& Path, bool bReading) const override;
	virtual bool ObjectLevelHasFinishedLoading(UObject* Object) const override;
	virtual bool IsAllowedToDestroyInstance(const UObject* Instance) const override;
	virtual void OnProtocolMismatchDetected(FNetRefHandle ObjectHandle) override;
	virtual void OnProtocolMismatchReported(FNetRefHandle RefHandle, uint32 ConnectionId) override;

	virtual float GetPollFrequencyOfRootObject(const UObject* ReplicatedObject) const override;

	[[nodiscard]] virtual FString PrintConnectionInfo(uint32 ConnectionId) const override;

private:
	
	void OnMaxTickRateChanged(UNetDriver* InNetDriver, int32 NewMaxTickRate, int32 OldMaxTickRate);

	void AddActorToLevelGroup(const AActor* Actor);

private:

	UE::Net::FNetObjectFactoryId ActorFactoryId;
	UE::Net::FNetObjectFactoryId SubObjectFactoryId;

#endif // UE_WITH_IRIS

	UNetDriver* NetDriver = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UIrisObjectReferencePackageMap> ObjectReferencePackageMap = nullptr;

};
