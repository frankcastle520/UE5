// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkAutomationTest.h"
#include "NetworkAutomationTestMacros.h"
#include "ReplicatedTestObject.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "HAL/PlatformMath.h"

#include "Iris/DataStream/DataStreamDefinitions.h"
#include "Iris/DataStream/DataStreamManager.h"
#include "Iris/ReplicationSystem/NetTokenDataStream.h"
#include "Iris/ReplicationSystem/ReplicationDataStream.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"

#include "Net/Core/Misc/ResizableCircularQueue.h"

namespace UE::Net
{

// Utility class to use DataStreams in test code
class FDataStreamTestUtil
{
public:
	FDataStreamTestUtil();

	void SetUp();
	void TearDown();

	class UMyDataStreamDefinitions : public UDataStreamDefinitions
	{
	public:
		void FixupDefinitions() { return UDataStreamDefinitions::FixupDefinitions(); }
	};

	void StoreDataStreamDefinitions();
	void RestoreDataStreamDefinitions();
	void AddDataStreamDefinition(const TCHAR* StreamName, const TCHAR* ClassPath, bool bValid = true);
	void FixupDefinitions() { DataStreamDefinitions->FixupDefinitions(); }

protected:
	UMyDataStreamDefinitions* DataStreamDefinitions;
	TArray<FDataStreamDefinition>* CurrentDataStreamDefinitions;
	TArray<FDataStreamDefinition> PreviousDataStreamDefinitions;
	bool* PointerToFixupComplete;
};

// Implements everything we need to drive the replication system to test the system
class FReplicationSystemTestNode
{
public:
	uint32 MaxSendPacketSize = 2048U;

	struct FPacketData
	{
		enum { MaxPacketSize = 2048U };
		
		alignas(16) uint8 PacketBuffer[MaxPacketSize];
		uint32 BitCount;
		uint32 PacketId;
		FString Desc;
	};

	struct FConnectionInfo
	{
		UDataStreamManager* DataStreamManager = nullptr;
		UReplicationDataStream* ReplicationDataStream = nullptr;
		UNetTokenDataStream* NetTokenDataStream = nullptr;
		FNetTokenStoreState* RemoteNetTokenStoreState = nullptr;
		TResizableCircularQueue<const FDataStreamRecord*> WriteRecords;
		TResizableCircularQueue<FPacketData> WrittenPackets;
		uint32 ConnectionId = 0;
	};

	struct FReplicationSystemParamsOverride
	{
		uint32 MaxReplicatedObjectCount = 0;
		uint32 InitialNetObjectListCount = 0;
		uint32 NetObjectListGrowCount = 0;
	};

	enum EDelaySetup { DelaySetup=0 };

public:
	FReplicationSystemTestNode(bool bIsServer, const TCHAR* Name);
	explicit FReplicationSystemTestNode(FReplicationSystemTestNode::EDelaySetup);
	virtual ~FReplicationSystemTestNode();

	void Setup(bool bIsServer, const TCHAR* Name, FReplicationSystemTestNode::FReplicationSystemParamsOverride* ParamsOverride=nullptr);

	template<typename T>
	T* CreateObject()
	{
		T* CreatedObject = NewObject<T>();
		if (Cast<UReplicatedTestObject>(CreatedObject))
		{
			CreatedObjects.Add(TStrongObjectPtr<UObject>(CreatedObject));

			// Add it to the bridge for replication
			ReplicationBridge->BeginReplication(CreatedObject);
	
			return CreatedObject;
		}
		return nullptr;
	}

	template<typename T>
	T* CreateSubObject(FNetRefHandle OwnerHandle, FNetRefHandle InsertRelativeToSubObjectHandle = FNetRefHandle::GetInvalid(), UReplicationBridge::ESubObjectInsertionOrder InsertionOrder = UReplicationBridge::ESubObjectInsertionOrder::None)
	{
		T* CreatedObject = NewObject<T>();
		if (Cast<UReplicatedTestObject>(CreatedObject))
		{
			CreatedObjects.Add(TStrongObjectPtr<UObject>(CreatedObject));

			// Add it to the bridge for replication
			ReplicationBridge->BeginReplication(OwnerHandle, CreatedObject, InsertRelativeToSubObjectHandle, InsertionOrder);
	
			return CreatedObject;
		}
		return nullptr;
	}

	template <typename T>
	T* GetObjectAs(FNetRefHandle Handle)
	{
		return static_cast<T*>(ReplicationBridge->GetReplicatedObject(Handle));
	}

	UTestReplicatedIrisObject* CreateObject(const UObjectReplicationBridge::FRootObjectReplicationParams& Params, UTestReplicatedIrisObject::FComponents* ComponentsToCreate=nullptr);
	UTestReplicatedIrisObject* CreateObject(uint32 NumComponents, uint32 NumIrisComponents);
	UTestReplicatedIrisObject* CreateSubObject(FNetRefHandle Owner, uint32 NumComponents, uint32 NumIrisComponents);
	UTestReplicatedIrisObject* CreateObject(const UTestReplicatedIrisObject::FComponents& Components = UTestReplicatedIrisObject::FComponents());
	UTestReplicatedIrisObject* CreateSubObject(FNetRefHandle Owner, const UTestReplicatedIrisObject::FComponents& Components);
	UTestReplicatedIrisObject* CreateObjectWithDynamicState(uint32 NumComponents, uint32 NumIrisComponents, uint32 NumDynamicStateComponents);

	void DestroyObject(UReplicatedTestObject*, EEndReplicationFlags EndReplicationFlags = EEndReplicationFlags::Destroy);

	// Connection
	uint32 AddConnection();
	void RemoveConnection(uint32 ConnectionId);

	// System Update
	void PreSendUpdate(const UReplicationSystem::FSendUpdateParams& Params);
	void PreSendUpdate();
	void PostSendUpdate();

	// Update methods for server connections
	bool SendUpdate(uint32 ConnectionId, const TCHAR* Desc = nullptr);
	bool SendUpdate(const TCHAR* Desc = nullptr) { return SendUpdate(1, Desc); }

	void DeliverTo(FReplicationSystemTestNode& Dest, uint32 LocalConnectionId, uint32 RemoteConnectionId, bool bDeliver);
	void RecvUpdate(uint32 ConnectionId, FNetSerializationContext& Context);
	void RecvUpdate(FNetSerializationContext& Context) { RecvUpdate(1, Context); }

	UReplicatedTestObjectBridge* GetReplicationBridge() { return ReplicationBridge; }
	UReplicationSystem* GetReplicationSystem() { return ReplicationSystem; }
	uint32 GetReplicationSystemId() const;

	void SetMaxSendPacketSize(uint32 InMaxSendPacketSize) { MaxSendPacketSize = InMaxSendPacketSize; }

	FConnectionInfo& GetConnectionInfo(uint32 ConnectionId) { return Connections[ConnectionId - 1]; }

	uint32 GetNetTraceId() const;

	float ConvertPollPeriodIntoFrequency(uint32 PollPeriod) const;

public:
	TUniquePtr<UE::Net::FNetTokenStore> NetTokenStore;
	UReplicationSystem* ReplicationSystem;
	UReplicatedTestObjectBridge* ReplicationBridge;
	TArray<TStrongObjectPtr<UObject>> CreatedObjects;
	EReplicationSystemSendPass CurrentSendPass = EReplicationSystemSendPass::Invalid;

private:
	TArray<FConnectionInfo> Connections;
	uint32 PacketId = 0U;
};

class FReplicationSystemTestClient : public FReplicationSystemTestNode
{
public:
	FReplicationSystemTestClient(const TCHAR* Name);
	explicit FReplicationSystemTestClient(FReplicationSystemTestNode::EDelaySetup Delay) : FReplicationSystemTestNode(Delay) {}

	// Tick and send packets to the server
	bool UpdateAndSend(class FReplicationSystemTestServer* Server, bool bDeliver = true);

	uint32 ConnectionIdOnServer;
	uint32 LocalConnectionId;
};

class FReplicationSystemTestServer : public FReplicationSystemTestNode
{
public:
	explicit FReplicationSystemTestServer(const TCHAR* Name);
	explicit FReplicationSystemTestServer(FReplicationSystemTestNode::EDelaySetup Delay) : FReplicationSystemTestNode(Delay) {}

	// Send data and deliver to the client if bDeliver is true
	bool SendAndDeliverTo(FReplicationSystemTestClient* Client, bool bDeliver, const TCHAR* Desc = nullptr);

	// Send data, return true if data was written
	bool SendTo(FReplicationSystemTestClient* Client, const TCHAR* Desc = nullptr);

	// Explicitly set delivery status
	void DeliverTo(FReplicationSystemTestClient* Client, bool bDeliver);

	// Tick and send packets to one or all clients
	bool UpdateAndSend(const TArrayView<FReplicationSystemTestClient*const>& Clients, bool bDeliver=true);
};

class FReplicationSystemServerClientTestFixture : public FNetworkAutomationTestSuiteFixture
{
public:
	FReplicationSystemServerClientTestFixture() : FNetworkAutomationTestSuiteFixture() {}

protected:
	enum : bool
	{
		DoNotDeliverPacket = false,
		DeliverPacket = true,
	};

	virtual void SetUp() override;
	virtual void TearDown() override;

	FReplicationSystemTestClient* CreateClient();
	void DestroyClient(FReplicationSystemTestClient* Client);

	FDataStreamTestUtil DataStreamUtil;
	FReplicationSystemTestServer* Server;
	TArray<FReplicationSystemTestClient*> Clients;
};

}
